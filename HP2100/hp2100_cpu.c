/* hp2100_cpu.c: HP 2100 CPU simulator

   Copyright (c) 1993-2002, Robert M. Supnik

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   22-Mar-02	RMS	Changed to allocate memory array dynamically
   11-Mar-02	RMS	Cleaned up setjmp/auto variable interaction
   17-Feb-02	RMS	Added DMS support
			Fixed bugs in extended instructions
   03-Feb-02	RMS	Added terminal multiplexor support
			Changed PCQ macro to use unmodified PC
			Fixed flop restore logic (found by Bill McDermith)
			Fixed SZx,SLx,RSS bug (found by Bill McDermith)
			Added floating point support
   16-Jan-02	RMS	Added additional device support
   07-Jan-02	RMS	Fixed DMA register tables (found by Bill McDermith)
   07-Dec-01	RMS	Revised to use breakpoint package
   03-Dec-01	RMS	Added extended SET/SHOW support
   10-Aug-01	RMS	Removed register in declarations
   26-Nov-00	RMS	Fixed bug in dual device number routine
   21-Nov-00	RMS	Fixed bug in reset routine
   15-Oct-00	RMS	Added dynamic device number support

   The register state for the HP 2100 CPU is:

   AR<15:0>	A register - addressable as location 0
   BR<15:0>	B register - addressable as location 1
   PC<14:0>	P register (program counter)
   SR<15:0>	S register
   E		extend flag (carry out)
   O		overflow flag

   The 21MX adds a pair of index registers:

   XR<15:0>	X register
   YR<15:0>	Y register
      
   The original HP 2116 has four instruction formats: memory reference,
   shift, alter/skip, and I/O.  The HP 2100 added extended memory reference
   and extended arithmetic.  The HP21MX added extended byte, bit, and word
   instructions as well as extended memory.

   The memory reference format is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |in|     op    |cp|           offset            | memory reference
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   <14:11>	mnemonic	action

   0010		AND		A = A & M[MA]
   0011		JSB		M[MA] = P, P = MA + 1
   0100		XOR		A = A ^ M[MA]
   0101		JMP		P = MA
   0110		IOR		A = A | M[MA]
   0111		ISZ		M[MA] = M[MA] + 1, skip if M[MA] == 0
   1000		ADA		A = A + M[MA]
   1001		ADB		B = B + M[MA]
   1010		CPA		skip if A != M[MA]
   1011		CPB		skip if B != M[MA]
   1100		LDA		A = M[MA]
   1101		LDB		B = M[MA]
   1110		STA		M[MA] = A
   1111		STB		M[MA] = B

   <15,10>	mode		action

   0,0	page zero direct	MA = IR<9:0>
   0,1	current page direct	MA = PC<14:0>'IR,9:0>
   1,0	page zero indirect	MA = M[IR<9:0>]
   1,1	current page indirect	MA = M[PC<14:10>'IR<9:0>]

   Memory reference instructions can access an address space of 32K words.
   An instruction can directly reference the first 1024 words of memory
   (called page zero), as well as 1024 words of the current page; it can
   indirectly access all 32K.
*/

/* The shift format is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0  0  0  0|ab| 0|s1|   op1  |ce|s2|sl|   op2  | shift
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                 |     | \---+---/ |  |  | \---+---/
                 |     |     |     |  |  |     |
                 |     |     |     |  |  |     +---- shift 2 opcode
                 |     |     |     |  |  +---------- skip if low bit == 0
                 |     |     |     |  +------------- shift 2 enable
                 |     |     |     +---------------- clear Extend
                 |     |     +---------------------- shift 1 opcode
                 |     +---------------------------- shift 1 enable
                 +---------------------------------- A/B select

   The alter/skip format is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0  0  0  0|ab| 1|regop| e op|se|ss|sl|in|sz|rs| alter/skip
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                 |    \-+-/ \-+-/  |  |  |  |  |  |
                 |      |     |    |  |  |  |  |  +- reverse skip sense
                 |      |     |    |  |  |  |  +---- skip if register == 0
                 |      |     |    |  |  |  +------- increment register
                 |      |     |    |  |  +---------- skip if low bit == 0
                 |      |     |    |  +------------- skip if sign bit == 0
                 |      |     |    +---------------- skip if Extend == 0
                 |      |     +--------------------- clr/com/set Extend
                 |      +--------------------------- clr/com/set register
                 +---------------------------------- A/B select

   The I/O transfer format is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  0  0  0|ab| 1|hc| opcode |      device     | I/O transfer
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                 |     | \---+---/\-------+-------/
                 |     |     |            |
                 |     |     |            +--------- device select
                 |     |     +---------------------- opcode
                 |     +---------------------------- hold/clear flag
                 +---------------------------------- A/B select

   The IO transfer instruction controls the specified device.
   Depending on the opcode, the instruction may set or clear
   the device flag, start or stop I/O, or read or write data.
*/

/* The 2100 added an extended memory reference instruction;
   the 21MX added extended arithmetic, operate, byte, word,
   and bit instructions.  Note that the HP 21xx is, despite
   the right-to-left bit numbering, a big endian system.
   Bits <15:8> are byte 0, and bits <7:0> are byte 1.


   The extended memory reference format (HP 2100) is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 0  0  0|op| 0|            opcode           | extended mem ref
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |in|              operand address               |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The extended arithmetic format (HP 2100) is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 0  0  0  0  0|dr| 0  0| opcode |shift count| extended arithmetic
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The extended operate format (HP 21MX) is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 0  0  0|op| 0| 1  1  1  1  1|    opcode    | extended operate
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The extended byte and word format (HP 21MX) is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 0  0  0  1  0  1  1  1  1  1  1|   opcode  | extended byte/word
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |in|              operand address               |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0|
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The extended bit operate format (HP 21MX) is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 0  0  0  1  0  1  1  1  1  1  1  1| opcode | extended bit operate
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |in|              operand address               |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |in|              operand address               |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
*/

/* This routine is the instruction decode routine for the HP 2100.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

	HALT instruction
	breakpoint encountered
	infinite indirection loop
	unimplemented instruction and stop_inst flag set
	unknown I/O device and stop_dev flag set
	I/O error in I/O simulator

   2. Interrupts.  I/O devices are modelled as four parallel arrays:

	device commands as bit array dev_cmd[2][31..0]
	device flags as bit array dev_flg[2][31..0]
	device flag buffers as bit array dev_fbf[2][31..0]
	device controls as bit array dev_ctl[2][31..0]

      The HP 2100 interrupt structure is based on flag, flag buffer,.
      and control.  If a device flag is set, the flag buffer is set,
      the control bit is set, and the device is the highest priority
      on the interrupt chain, it requests an interrupt.  When the
      interrupt is acknowledged, the flag buffer is cleared, preventing
      further interrupt requests from that device.  The combination of
      flag and control set blocks interrupts from lower priority devices.

      Command plays no direct role in interrupts.  The command flop
      tells whether a device is active.  It is set by STC and cleared
      by CLC; it is also cleared when the device flag is set.  Simple
      devices don't need to track command separately from control.
 
   3. Non-existent memory.  On the HP 2100, reads to non-existent memory
      return zero, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to zero.
      Thus, only writes need be checked against actual memory size.

   4. Adding I/O devices.  These modules must be modified:

	hp2100_defs.h	add interrupt request definition
	hp2100_cpu.c	add device information table entry
	hp2100_sys.c	add sim_devices table entry
*/

#include "hp2100_defs.h"
#include <setjmp.h>

#define PCQ_SIZE	64				/* must be 2**n */
#define PCQ_MASK	(PCQ_SIZE - 1)
#define PCQ_ENTRY	pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = err_PC
#define UNIT_V_MSIZE	(UNIT_V_UF)			/* dummy mask */
#define UNIT_MSIZE	(1 << UNIT_V_MSIZE)
#define UNIT_V_2100	(UNIT_V_UF + 1)			/* 2100 vs 2116 */
#define UNIT_2100	(1 << UNIT_V_2100)
#define UNIT_V_21MX	(UNIT_V_UF + 2)			/* 21MX vs 2100 */
#define UNIT_21MX	(1 << UNIT_V_21MX)
#define ABORT(val)	longjmp (save_env, (val))

#define DMAR0		1
#define DMAR1		2
#define SEXT(x)		(((x) & SIGN)? (((int32) (x)) | ~DMASK): ((int32) (x)))

/* Memory protection tests */

#define MP_TEST(x)	(CTL (PRO) && ((x) > 1) && ((x) < mfence))

#define MP_TESTJ(x)	(CTL (PRO) && ((x) < mfence))

uint16 *M = NULL;					/* memory */
int32 saved_AR = 0;					/* A register */
int32 saved_BR = 0;					/* B register */
int32 PC = 0;						/* P register */
int32 SR = 0;						/* S register */
int32 XR = 0;						/* X register */
int32 YR = 0;						/* Y register */
int32 E = 0;						/* E register */
int32 O = 0;						/* O register */
int32 dev_cmd[2] = { 0 };				/* device command */
int32 dev_ctl[2] = { 0 };				/* device control */
int32 dev_flg[2] = { 0 };				/* device flags */
int32 dev_fbf[2] = { 0 };				/* device flag bufs */
struct DMA dmac[2] = { { 0 }, { 0 } };			/* DMA channels */
int32 ion = 0;						/* interrupt enable */
int32 ion_defer = 0;					/* interrupt defer */
int32 intaddr = 0;					/* interrupt addr */
int32 mfence = 0;					/* mem prot fence */
int32 maddr = 0;					/* mem prot err addr */
int32 err_PC = 0;					/* error PC */
int32 dms_enb = 0;					/* dms enable */
int32 dms_ump = 0;					/* dms user map */
int32 dms_sr = 0;					/* dms status register */
int32 dms_fence = 0;					/* dms base page fence */
int32 dms_vr = 0;					/* dms violation register */
int32 dms_sma = 0;					/* dms saved ma */
int32 dms_map[MAP_NUM * MAP_LNT] = { 0 };		/* dms maps */
int32 ind_max = 16;					/* iadr nest limit */
int32 stop_inst = 1;					/* stop on ill inst */
int32 stop_dev = 2;					/* stop on ill dev */
uint16 pcq[PCQ_SIZE] = { 0 };				/* PC queue */
int32 pcq_p = 0;					/* PC queue ptr */
REG *pcq_r = NULL;					/* PC queue reg ptr */
jmp_buf save_env;					/* abort handler */

extern int32 sim_interval;
extern int32 sim_int_char;
extern int32 sim_brk_types, sim_brk_dflt, sim_brk_summ;	/* breakpoint info */
extern FILE *sim_log;

uint8 ReadB (int32 addr);
uint8 ReadBA (int32 addr);
uint16 ReadW (int32 addr);
uint16 ReadWA (int32 addr);
uint32 ReadF (int32 addr);
uint16 ReadIO (int32 addr, int32 map);
void WriteB (int32 addr, int32 dat);
void WriteBA (int32 addr, int32 dat);
void WriteW (int32 addr, int32 dat);
void WriteWA (int32 addr, int32 dat);
void WriteIO (int32 addr, int32 dat, int32 map);
int32 dms (int32 va, int32 map, int32 prot);
uint16 dms_rmap (int32 mapi);
void dms_wmap (int32 mapi, int32 dat);
void dms_viol (int32 va, int32 st, t_bool io);
int32 dms_upd_sr (void);
int32 shift (int32 inval, int32 flag, int32 oper);
int32 calc_dma (void);
int32 calc_int (void);
void dma_cycle (int32 chan, int32 map);
t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat dma0_reset (DEVICE *dptr);
t_stat dma1_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_bool dev_conflict (void);

extern int32 f_as (uint32 op, t_bool sub);
extern int32 f_mul (uint32 op);
extern int32 f_div (uint32 op);
extern int32 f_fix (void);
extern void f_flt (void);

/* CPU data structures

   cpu_dev	CPU device descriptor
   cpu_unit	CPU unit descriptor
   cpu_reg	CPU register list
   cpu_mod	CPU modifiers list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, VASIZE) };

REG cpu_reg[] = {
	{ ORDATA (P, PC, 15) },
	{ ORDATA (A, saved_AR, 16) },
	{ ORDATA (B, saved_BR, 16) },
	{ ORDATA (X, XR, 16) },
	{ ORDATA (Y, YR, 16) },
	{ ORDATA (S, SR, 16) },
	{ FLDATA (E, E, 0) },
	{ FLDATA (O, O, 0) },
	{ FLDATA (ION, ion, 0) },
	{ FLDATA (ION_DEFER, ion_defer, 0) },
	{ ORDATA (IADDR, intaddr, 6) },
	{ FLDATA (MPCTL, dev_ctl[PRO/32], INT_V (PRO)) },
	{ FLDATA (MPFLG, dev_flg[PRO/32], INT_V (PRO)) },
	{ FLDATA (MPFBF, dev_fbf[PRO/32], INT_V (PRO)) },
	{ ORDATA (MFENCE, mfence, 15) },
	{ ORDATA (MADDR, maddr, 16) },
	{ FLDATA (DMSENB, dms_enb, 0) },
	{ FLDATA (DMSCUR, dms_ump, VA_N_PAG) },
	{ ORDATA (DMSSR, dms_sr, 16) },
	{ ORDATA (DMSVR, dms_vr, 16) },
	{ ORDATA (DMSSMA, dms_sma, 15), REG_HIDDEN },
	{ BRDATA (DMSMAP, dms_map, 8, PA_N_SIZE, MAP_NUM * MAP_LNT) },
	{ FLDATA (STOP_INST, stop_inst, 0) },
	{ FLDATA (STOP_DEV, stop_dev, 1) },
	{ DRDATA (INDMAX, ind_max, 16), REG_NZ + PV_LEFT },
	{ BRDATA (PCQ, pcq, 8, 15, PCQ_SIZE), REG_RO+REG_CIRC },
	{ ORDATA (PCQP, pcq_p, 6), REG_HRO },
	{ ORDATA (WRU, sim_int_char, 8) },
	{ FLDATA (T2100, cpu_unit.flags, UNIT_V_2100), REG_HRO },
	{ FLDATA (T21MX, cpu_unit.flags, UNIT_V_21MX), REG_HRO },
	{ ORDATA (HCMD, dev_cmd[0], 32), REG_HRO },
	{ ORDATA (LCMD, dev_cmd[1], 32), REG_HRO },
	{ ORDATA (HCTL, dev_ctl[0], 32), REG_HRO },
	{ ORDATA (LCTL, dev_ctl[1], 32), REG_HRO },
	{ ORDATA (HFLG, dev_flg[0], 32), REG_HRO },
	{ ORDATA (LFLG, dev_flg[1], 32), REG_HRO },
	{ ORDATA (HFBF, dev_fbf[0], 32), REG_HRO },
	{ ORDATA (LFBF, dev_fbf[1], 32), REG_HRO },
	{ NULL }  };

MTAB cpu_mod[] = {
	{ UNIT_2100 + UNIT_21MX, 0, "2116", "2116", NULL },
	{ UNIT_2100 + UNIT_21MX, UNIT_2100, "2100", "2100", NULL },
	{ UNIT_2100 + UNIT_21MX, UNIT_21MX, "21MX", "21MX", NULL },
	{ UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
	{ UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
	{ UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
	{ UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
	{ UNIT_MSIZE, 65536, NULL, "64K", &cpu_set_size },
	{ UNIT_MSIZE, 131072, NULL, "128K", &cpu_set_size },
	{ UNIT_MSIZE, 262144, NULL, "256K", &cpu_set_size },
	{ UNIT_MSIZE, 524288, NULL, "512K", &cpu_set_size },
	{ UNIT_MSIZE, 1048576, NULL, "1024K", &cpu_set_size },
	{ 0 }  };

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 8, 15, 1, 8, 16,
	&cpu_ex, &cpu_dep, &cpu_reset,
	NULL, NULL, NULL };

/* DMA controller data structures

   dmax_dev	DMAx device descriptor
   dmax_reg	DMAx register list
*/

UNIT dma0_unit = { UDATA (NULL, 0, 0) };

REG dma0_reg[] = {
	{ FLDATA (CMD, dev_cmd[DMA0/32], INT_V (DMA0)) },
	{ FLDATA (CTL, dev_ctl[DMA0/32], INT_V (DMA0)) },
	{ FLDATA (FLG, dev_flg[DMA0/32], INT_V (DMA0)) },
	{ FLDATA (FBF, dev_fbf[DMA0/32], INT_V (DMA0)) },
	{ ORDATA (CW1, dmac[0].cw1, 16) },
	{ ORDATA (CW2, dmac[0].cw2, 16) },
	{ ORDATA (CW3, dmac[0].cw3, 16) },
	{ NULL }  };

DEVICE dma0_dev = {
	"DMA0", &dma0_unit, dma0_reg, NULL,
	1, 8, 1, 1, 8, 16,
	NULL, NULL, &dma0_reset,
	NULL, NULL, NULL };

UNIT dma1_unit = { UDATA (NULL, 0, 0) };

REG dma1_reg[] = {
	{ FLDATA (CMD, dev_cmd[DMA1/32], INT_V (DMA1)) },
	{ FLDATA (CTL, dev_ctl[DMA1/32], INT_V (DMA1)) },
	{ FLDATA (FLG, dev_flg[DMA1/32], INT_V (DMA1)) },
	{ FLDATA (FBF, dev_fbf[DMA1/32], INT_V (DMA1)) },
	{ ORDATA (CW1, dmac[1].cw1, 16) },
	{ ORDATA (CW2, dmac[1].cw2, 16) },
	{ ORDATA (CW3, dmac[1].cw3, 16) },
	{ NULL }  };

DEVICE dma1_dev = {
	"DMA1", &dma1_unit, dma1_reg, NULL,
	1, 8, 1, 1, 8, 16,
	NULL, NULL, &dma1_reset,
	NULL, NULL, NULL };

/* Extended instruction decode tables */

static const uint8 ext_addr[192] = {			/* ext inst format */
 0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,			/* 1: 2 word inst */
 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,
 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };

static const uint8 exg_breq[64] = {			/* ext grp B only */
 1,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1,			/* 1: <11> must be 1 */
 1,1,0,1,0,0,0,0,0,0,1,1,1,1,1,1,
 0,0,0,1,0,1,1,0,0,0,0,1,0,1,1,0,
 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };

static const uint8 exg_addr[64] = {			/* ext grp format */
 0,0,0,0,0,0,0,0,0,0,0,0,1,3,0,0,			/* 1: 2 word inst */
 0,0,0,0,1,1,1,0,0,0,1,1,1,1,1,1,			/* 2: 3 word with count */
 1,0,1,1,0,1,1,0,1,0,1,1,0,1,1,0,			/* 3: 3 word inst */
 0,0,1,0,0,2,2,0,0,0,1,3,3,3,2,2 };

/* Interrupt defer table */

static const int32 defer_tab[] = { 0, 1, 1, 1, 0, 0, 0, 1 };

/* Device dispatch table */

int32 devdisp (int32 devno, int32 inst, int32 IR, int32 outdat);
int32 cpuio (int32 op, int32 IR, int32 outdat);
int32 ovfio (int32 op, int32 IR, int32 outdat);
int32 pwrio (int32 op, int32 IR, int32 outdat);
int32 proio (int32 op, int32 IR, int32 outdat);
int32 dmsio (int32 op, int32 IR, int32 outdat);
int32 dmpio (int32 op, int32 IR, int32 outdat);
int32 nulio (int32 op, int32 IR, int32 outdat);

int32 (*dtab[64])() = {
	&cpuio, &ovfio, &dmsio, &dmsio, &pwrio, &proio, &dmpio, &dmpio,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL  };

/* Device information blocks */

extern DIB ptr_dib, ptp_dib;
extern DIB tty_dib;
extern DIB clk_dib;
extern DIB lpt_dib;
extern DIB dp_dib[];
extern DIB dq_dib[];
extern DIB dr_dib[];
extern DIB mt_dib[];
extern DIB ms_dib[];
extern DIB mux_dib[];
extern DIB muxc_dib;

DIB *dib_tab[] = {
	&ptr_dib,
	&ptp_dib,
	&tty_dib,
	&clk_dib,
	&lpt_dib,
	&dp_dib[0],
	&dp_dib[1],
	&dq_dib[0],
	&dq_dib[1],
	&dr_dib[0],
	&dr_dib[1],
	&mt_dib[0],
	&mt_dib[1],
	&ms_dib[0],
	&ms_dib[1],
	&mux_dib[0],
	&mux_dib[1],
	&muxc_dib,
	NULL  };

t_stat sim_instr (void)
{
int32 intrq, dmarq;					/* set after setjmp */
t_stat reason;						/* set after setjmp */
int32 i, dev;						/* temp */
DIB *dibp;						/* temp */
int abortval;

/* Restore register state */

if (dev_conflict ()) return SCPE_STOP;			/* check consistency */
AR = saved_AR & DMASK;					/* restore reg */
BR = saved_BR & DMASK;
dms_fence = dms_sr & MST_FENCE;				/* separate fence */
err_PC = PC = PC & VAMASK;				/* load local PC */
reason = 0;

/* Restore I/O state */

for (i = VARDEV; i <= DEVMASK; i++) dtab[i] = NULL;	/* clr disp table */
dev_cmd[0] = dev_cmd[0] & M_FXDEV;			/* clear dynamic info */
dev_ctl[0] = dev_ctl[0] & M_FXDEV;
dev_flg[0] = dev_flg[0] & M_FXDEV;
dev_fbf[0] = dev_fbf[0] & M_FXDEV;
dev_cmd[1] = dev_ctl[1] = dev_flg[1] = dev_fbf[1] = 0;
for (i = 0; dibp = dib_tab[i]; i++) {			/* loop thru dev */
	if (dibp -> enb) {				/* enabled? */
		dev = dibp -> devno;			/* get dev # */
		if (dibp -> cmd) { setCMD (dev); }	/* restore cmd */
		if (dibp -> ctl) { setCTL (dev); }	/* restore ctl */
		if (dibp -> flg) { setFLG (dev); }	/* restore flg */
		clrFBF (dev);				/* also sets fbf */
		if (dibp -> fbf) { setFBF (dev); }	/* restore fbf */
		dtab[dev] = dibp -> iot;  }  }		/* set I/O dispatch */

/* Abort handling

   If an abort occurs in memory protection, the relocation routine
   executes a longjmp to this area OUTSIDE the main simulation loop.
   Memory protection errors are the only sources of aborts in the
   HP 2100.  All referenced variables must be globals, and all sim_instr
   scoped automatics must be set after the setjmp.
*/

abortval = setjmp (save_env);				/* set abort hdlr */
if (abortval != 0) {					/* mem mgt abort? */
	if (abortval > 0) { setFLG (PRO); }		/* dms abort? */
	else maddr = err_PC | 0100000;			/* mprot abort */
	intrq = PRO;  }					/* protection intr */
dmarq = calc_dma ();					/* recalc DMA masks */
intrq = calc_int ();					/* recalc interrupts */

/* Main instruction fetch/decode loop */

while (reason == 0) {					/* loop until halted */
int32 IR, MA, absel, i, dev, t, opnd;
int32 M1, iodata, op, sc, q, r, wc;
int32 mapi, mapj;
uint32 fop;

if (sim_interval <= 0) {				/* check clock queue */
	if (reason = sim_process_event ()) break;  
	dmarq = calc_dma ();				/* recalc DMA reqs */
	intrq = calc_int ();  }				/* recalc interrupts */

if (dmarq) {
	if (dmarq & DMAR0) dma_cycle (0, PAMAP);	/* DMA1 cycle? */
	if (dmarq & DMAR1) dma_cycle (1, PBMAP);	/* DMA2 cycle? */
	dmarq = calc_dma ();				/* recalc DMA reqs */
	intrq = calc_int ();  }				/* recalc interrupts */

if (intrq && ((intrq <= PRO) || !ion_defer)) {		/* interrupt request? */
	clrFBF (intrq);					/* clear flag buffer */
	intaddr = intrq;				/* save int addr */
	err_PC = PC;					/* save PC for error */
	if (dms_enb) dms_sr = dms_sr | MST_ENBI;	/* dms enabled? */
	else dms_sr = dms_sr & ~MST_ENBI;
	if (dms_ump) {					/* user map? */
		dms_sr = dms_sr | MST_UMPI;
		dms_ump = 0;  }				/* switch to system */
	else dms_sr = dms_sr & ~MST_UMPI;
	IR = ReadW (intrq);				/* get dispatch instr */
	ion_defer = 1;					/* defer interrupts */
	intrq = 0;					/* clear request */
	clrCTL (PRO); }					/* protection off */

else {	if (sim_brk_summ &&
	    sim_brk_test (PC, SWMASK ('E'))) {		/* breakpoint? */
		reason = STOP_IBKPT;			/* stop simulation */
		break;  }
	err_PC = PC;					/* save PC for error */
	IR = ReadW (PC);				/* fetch instr */
	PC = (PC + 1) & VAMASK;
	sim_interval = sim_interval - 1;
	ion_defer = 0;  }
absel = (IR & AB)? 1: 0;				/* get A/B select */

/* Memory reference instructions */

if (IR & MROP) {					/* mem ref? */
	MA = IR & (IA | DISP);				/* ind + disp */
	if (IR & CP) MA = ((PC - 1) & PAGENO) | MA;	/* current page? */
	for (i = 0; (i < ind_max) && (MA & IA); i++)	/* resolve multi- */
		MA = ReadW (MA & VAMASK);		/* level indirect */
	if (i >= ind_max) {				/* indirect loop? */
		reason = STOP_IND;
		break;  }

	switch ((IR >> 11) & 017) {			/* decode IR<14:11> */
	case 002:					/* AND */
		AR = AR & ReadW (MA);
		break;
	case 003:					/* JSB */
		WriteW (MA, PC);
		PCQ_ENTRY;
		PC = (MA + 1) & VAMASK;
		if (IR & IA) ion_defer = 1;
		break;
	case 004:					/* XOR */
		AR = AR ^ ReadW (MA);
		break;
	case 005:					/* JMP */
		if (MP_TESTJ (MA)) ABORT (ABORT_FENCE);
		PCQ_ENTRY;
		PC = MA;
		if (IR & IA) ion_defer = 1;
		break;
	case 006:					/* IOR */
		AR = AR | ReadW (MA);
		break;
	case 007:					/* ISZ */
		t = (ReadW (MA) + 1) & DMASK;
		WriteW (MA, t);
		if (t == 0) PC = (PC + 1) & VAMASK;
		break;

/* Memory reference instructions, continued */

	case 010:					/* ADA */
		opnd = (int32) ReadW (MA);
		t = (int32) AR + opnd;
		if (t > DMASK) E = 1;
		if (((~AR ^ opnd) & (AR ^ t)) & SIGN) O = 1;
		AR = t & DMASK;
		break;
	case 011:					/* ADB */
		opnd = (int32) ReadW (MA);
		t = (int32) BR + opnd;
		if (t > DMASK) E = 1;
		if (((~BR ^ opnd) & (BR ^ t)) & SIGN) O = 1;
		BR = t & DMASK;
		break;
	case 012:					/* CPA */
		if (AR != ReadW (MA)) PC = (PC + 1) & VAMASK;
		break;
	case 013:					/* CPB */
		if (BR != ReadW (MA)) PC = (PC + 1) & VAMASK;
		break;
	case 014:					/* LDA */
		AR = ReadW (MA);
		break;
	case 015:					/* LDB */
		BR = ReadW (MA);
		break;
	case 016:					/* STA */
		WriteW (MA, AR);
		break;
	case 017:					/* STB */
		WriteW (MA, BR);
		break;  }				/* end case IR */
	}						/* end if mem ref */

/* Alter/skip instructions */

else if ((IR & NMROP) == ASKP) {			/* alter/skip? */
	int32 skip = 0;					/* no skip */

	if (IR & 000400) t = 0;				/* CLx */
	else t = ABREG[absel];
	if (IR & 001000) t = t ^ DMASK;			/* CMx */
	if (IR & 000001) {				/* RSS? */
		if ((IR & 000040) && (E != 0)) skip = 1;/* SEZ,RSS */
		if (IR & 000100) E = 0;			/* CLE */
		if (IR & 000200) E = E ^ 1;		/* CME */
		if (((IR & 000030) == 000030) &&	/* SSx,SLx,RSS */
			((t & 0100001) == 0100001)) skip = 1;
		if (((IR & 000030) == 000020) &&	/* SSx,RSS */
			((t & SIGN) != 0)) skip = 1;
		if (((IR & 000030) == 000010) &&	/* SLx,RSS */
			((t & 1) != 0)) skip = 1;
		if (IR & 000004) {			/* INx */
			t = (t + 1) & DMASK;
			if (t == 0) E = 1;
			if (t == SIGN) O = 1;  }
		if ((IR & 000002) && (t != 0)) skip = 1;/* SZx,RSS */
		if ((IR & 000072) == 0) skip = 1;	/* RSS */
		}					/* end if RSS */
	else {	if ((IR & 000040) && (E == 0)) skip = 1;/* SEZ */
		if (IR & 000100) E = 0;			/* CLE */
		if (IR & 000200) E = E ^ 1;		/* CME */
		if ((IR & 000020) &&			/* SSx */
			((t & SIGN) == 0)) skip = 1;
		if ((IR & 000010) &&			/* SLx */
			 ((t & 1) == 0)) skip = 1;
		if (IR & 000004) {			/* INx */
			t = (t + 1) & DMASK;
			if (t == 0) E = 1;
			if (t == SIGN) O = 1;  }
		if ((IR & 000002) && (t == 0)) skip = 1;/* SZx */
		}					/* end if ~RSS */
	ABREG[absel] = t;				/* store result */
	PC = (PC + skip) & VAMASK;			/* add in skip */
	}						/* end if alter/skip */

/* Shift instructions */

else if ((IR & NMROP) == SHFT) {			/* shift? */
	t = shift (ABREG[absel], IR & 01000, IR >> 6);	/* do first shift */
	if (IR & 000040) E = 0;				/* CLE */
	if ((IR & 000010) && ((t & 1) == 0))		/* SLx */
		PC = (PC + 1) & VAMASK;
	ABREG[absel] = shift (t, IR & 00020, IR);	/* do second shift */
	}						/* end if shift */

/* I/O instructions */

else if ((IR & NMROP) == IOT) {				/* I/O? */
	dev = IR & DEVMASK;				/* get device */
	t = (IR >> 6) & 07;				/* get subopcode */
	if (CTL (PRO) && ((t == ioHLT) || (dev != OVF))) {
		ABORT (ABORT_FENCE);  }
	iodata = devdisp (dev, t, IR, ABREG[absel]);	/* process I/O */
	if ((t == ioMIX) || (t == ioLIX))		/* store ret data */
		ABREG[absel] = iodata & DMASK;
	if (t == ioHLT) reason = STOP_HALT;
	else reason = iodata >> IOT_V_REASON;
	ion_defer = defer_tab[t];			/* set defer */
	dmarq = calc_dma ();				/* recalc DMA */
	intrq = calc_int ();				/* recalc interrupts */
	}						/* end if I/O */

/* Extended instructions */

else if (cpu_unit.flags & (UNIT_2100 | UNIT_21MX)) {	/* ext instr? */
	int32 awc;

	op = (IR >> 4) & 0277;				/* get opcode */
	if (ext_addr[op]) {				/* extended mem ref? */
		MA = ReadW (PC);			/* get next address */
		PC = (PC + 1) & VAMASK;
		for (i = 0; (i < ind_max) && (MA & IA); i++)
			MA = ReadW (MA & VAMASK);
		if (i >= ind_max) {
			reason = STOP_IND;
			break;  }  }
	sc = (IR & 017);				/* get shift count */
	if (sc == 0) sc = 16;
	switch (op) {					/* decode IR<11:4> */
	case 0010:					/* MUL */
		t = SEXT (AR) * SEXT (ReadW (MA));
		BR = (t >> 16) & DMASK;
		AR = t & DMASK;
		O = 0;
		break;
	case 0020:					/* DIV */
		t = (SEXT (BR) << 16) | (int32) AR;	/* get divd */
		opnd = SEXT (ReadW (MA));		/* get divisor */
		if ((opnd == 0) ||			/* divide by zero? */
		   ((t == SIGN32) && (opnd == -1)) ||	/* -2**32 / -1? */
		   ((q = t / opnd) > 077777) ||		/* quo too big? */
		    (q < -0100000)) {			/* quo too small? */
			O = 1;				/* set overflow */
			if (BR & SIGN) {		/* divd negative? */
				BR = (-BR) & DMASK;	/* make B'A pos */
				AR = (~AR + (BR == 0)) & DMASK;  }
			}				/* end if div fail */
		else {	AR = q & DMASK;			/* set quo, rem */
			BR = (t % opnd) & DMASK;
			O = 0;  }			/* end else ok */
		break;
	case 0210:					/* DLD */
		AR = ReadW (MA);
		MA = (MA + 1) & VAMASK;
		BR = ReadW (MA);
		break;
	case 0220:					/* DST */
		WriteW (MA, AR);
		MA = (MA + 1) & VAMASK;
		WriteW (MA, BR);
		break;

/* Extended arithmetic instructions */

	case 0001:					/* ASL */
		t = (SEXT (BR) >> (16 - sc)) & DMASK;
		if (t != ((BR & SIGN)? DMASK: 0)) O = 1;
		else O = 0;
		BR = (BR & SIGN) || ((BR << sc) & 077777) ||
			(AR >> (16 - sc));
		AR = (AR << sc) & DMASK;
		break;
	case 0002:					/* LSL */
		BR = ((BR << sc) | (AR >> (16 - sc))) & DMASK;
		AR = (AR << sc) & DMASK;
		break;
	case 0004:					/* RRL */
		t = BR;
		BR = ((BR << sc) | (AR >> (16 - sc))) & DMASK;
		AR = ((AR << sc) | (t >> (16 - sc))) & DMASK;
		break;
	case 0041:					/* ASR */
		AR = ((BR << (16 - sc)) | (AR >> sc)) & DMASK;
		BR = (SEXT (BR) >> sc) & DMASK;
		O = 0;
		break;
	case 0042:					/* LSR */
		AR = ((BR << (16 - sc)) | (AR >> sc)) & DMASK;
		BR = BR >> sc;
		break;
	case 0044:					/* RRR */
		t = AR;
		AR = ((AR >> sc) | (BR << (16 - sc))) & DMASK;
		BR = ((BR >> sc) | (t << (16 - sc))) & DMASK;
		break;

/* Floating point instructions */

	case 0240:					/* FAD */
		fop = ReadF (MA);			/* get fop */
		O = O | f_as (fop, 0);			/* add */
		break;
	case 0241:					/* FSB */
		fop = ReadF (MA);			/* get fop */
		O = O | f_as (fop, 1);			/* subtract */
		break;
	case 0242:					/* FMP */
		fop = ReadF (MA);			/* get fop */
		O = O | f_mul (fop);			/* multiply */
		break;
	case 0243:					/* FDV */
		fop = ReadF (MA);			/* get fop */
		O = O | f_div (fop);			/* divide */
		break;
	case 0244:					/* FIX */
		O = O | f_fix ();			/* fix */
		break;
	case 0245:					/* FLT */
		f_flt ();				/* float */
		break;

/* Extended instruction group, including DMS */

	case 0074: case 0075:				/* DMS inst grp, A */
	case 0076: case 0077:				/* ext inst grp, A */
		if (exg_breq[IR & 077]) {		/* must have B set? */
			reason = stop_inst;
			break;  }
	case 0274: case 0275:				/* DMS inst grp, B */
	case 0276: case 0277:				/* ext inst grp, B */
		if ((cpu_unit.flags & UNIT_21MX) == 0) {
			reason = stop_inst;
			break;  }
		op = IR & 077;				/* get sub opcode */
		if (exg_addr[op]) {			/* mem addr? */
			MA = ReadW (PC);		/* get next address */
			PC = (PC + 1) & VAMASK;
			for (i = 0; (i < ind_max) && (MA & IA); i++) 
				MA = ReadW (MA & VAMASK);
			if (i >= ind_max) {
				reason = STOP_IND;
				break;  }  }
		if (exg_addr[op] == 2) {		/* word of zero? */
			wc = ReadW (MA);		/* get count */
			if (ReadW (PC) == 0) WriteW (PC, wc);
			awc = PC;			/* and addr */
			PC = (PC + 1) & VAMASK;  }
		if (exg_addr[op] == 3) {		/* second address? */
			M1 = ReadW (PC);		/* get next address */
			PC = (PC + 1) & VAMASK;
			for (i = 0; (i < ind_max) && (M1 & IA); i++)
				M1 = ReadW (M1 & VAMASK);
			if (i >= ind_max) {
				reason = STOP_IND;
				break;  }  }
		switch (op) {				/* case on sub op */

/* Extended instruction group: DMS */

		case 002:				/* MBI */
			dms_viol (err_PC, MVI_PRV, 0);	/* priv if PRO */
			if (XR == 0) break;		/* nop if X = 0 */
			AR = AR & ~1;			/* force A, B even */
			BR = BR & ~1;
			for ( ; XR == 0; XR--) {	/* loop */
			    t = ReadB (AR);		/* read curr */
			    WriteBA (BR, t);		/* write alt */
			    AR = (AR + 1) & DMASK;	/* inc ptrs */
			    BR = (BR + 1) & DMASK;  }
			break;
		case 003:				/* MBF */
			if (XR == 0) break;
			AR = AR & ~1;			/* force A, B even */
			BR = BR & ~1;
			for ( ; XR == 0; XR--) {
			    t = ReadBA (AR);
			    WriteB (BR, t);
			    AR = (AR + 1) & DMASK;
			    BR = (BR + 1) & DMASK;  }
			break;
		case 004:				/* MBW */
			dms_viol (err_PC, MVI_PRV, 0);
			if (XR == 0) break;
			AR = AR & ~1;			/* force A, B even */
			BR = BR & ~1;
			for ( ; XR == 0; XR--) {
			    t = ReadBA (AR);
			    WriteBA (BR, t);
			    AR = (AR + 1) & DMASK;
			    BR = (BR + 1) & DMASK;  }
			break;
		case 005:				/* MWI */
			dms_viol (err_PC, MVI_PRV, 0);
			if (XR == 0) break;
			for ( ; XR == 0; XR--) {
			    t = ReadW (AR & VAMASK);
			    WriteWA (BR & VAMASK, t);
			    AR = (AR + 1) & DMASK;
			    BR = (BR + 1) & DMASK;  }
			break;
		case 006:				/* MWF */
			if (XR == 0) break;
			for ( ; XR == 0; XR--) {
			    t = ReadWA (AR & VAMASK);
			    WriteW (BR & VAMASK, t);
			    AR = (AR + 1) & DMASK;
			    BR = (BR + 1) & DMASK;  }
			break;
		case 007:				/* MWW */
			dms_viol (err_PC, MVI_PRV, 0);
			if (XR == 0) break;
			for ( ; XR == 0; XR--) {
			    t = ReadWA (AR & VAMASK);
			    WriteWA (BR & VAMASK, t);
			    AR = (AR + 1) & DMASK;
			    BR = (BR + 1) & DMASK;  }
			break;

/* Extended instruction group: DMS, continued */

		case 010:				/* SYA, SYB */
		case 011:				/* USA, USB */
		case 012:				/* PAA, PAB */
		case 013:				/* PBA, PBB */
			mapi = (op & 03) << VA_N_PAG;	/* map base */
			if (ABREG[absel] & SIGN) {	/* store? */
			    for (i = 0; i < MAP_LNT; i++) {
				t = dms_rmap (mapi + i);
				WriteW ((ABREG[absel] + i) & VAMASK, t);  }  }
			else {				/* load */
			    dms_viol (err_PC, MVI_PRV, 0);	/* priv if PRO */
			    for (i = 0; i < MAP_LNT; i++) {
				t = ReadW (ABREG[absel] + i & VAMASK);
				dms_wmap (mapi + i, t);   }  }
			ABREG[absel] = (ABREG[absel] + MAP_LNT) & DMASK;
			break;
		case 014:				/* SSM */
			WriteW (MA, dms_upd_sr ());	/* store stat */
			break;
		case 015:				/* JRS */
			if (dms_ump) dms_viol (err_PC, MVI_PRV, 0);
			t = ReadW (MA);			/* get status */
			if (t & 0100000) dms_enb = 1;	/* set/clr enb */
			else dms_enb = 0;
			if (t & 0040000) dms_ump = 1;	/* set/clr usr */
			else dms_ump = 0;
			PCQ_ENTRY;			/* save old PC */
			PC = M1;			/* jump */
			ion_defer = 1;			/* defer intr */
			break;

/* Extended instruction group: DMS, continued */

		case 020:				/* XMM */
			if (XR == 0) break;		/* nop? */
			if (XR & SIGN) {		/* store? */
			    for ( ; XR == 0; XR = (XR + 1) & DMASK) {
				t = dms_rmap (AR & MAP_MASK);
				WriteW (BR & VAMASK, t);
				AR = (AR + 1) & DMASK;
				BR = (BR + 1) & DMASK;  }  }
			else {				/* load */
			    for ( ; XR == 0; XR--) {
				t = ReadW (BR & VAMASK);
				dms_wmap (AR & MAP_MASK, t);
				AR = (AR + 1) & DMASK;
				BR = (BR + 1) & DMASK;  }  }
			break;
		case 021:				/* XMS */
			if (XR & SIGN) break;		/* nop? */
			for ( ; XR == 0; XR = (XR - 1) & DMASK) {
			    dms_wmap (AR & MAP_MASK, BR);
			    AR = (AR + 1) & DMASK;
			    BR = (BR + 1) & DMASK;  }
			break;
		case 022:				/* XMA, XMB */
			if (ABREG[absel] & 0100000) mapi = SMAP;
			else mapi = UMAP;
			if (ABREG[absel] & 0040000) mapj = PAMAP;
			else mapj = PBMAP;
			for (i = 0; i < MAP_LNT; i++) {
			    t = dms_rmap (mapi + i);
			    dms_wmap (mapj + i, t);  }
			break;
		case 024:				/* XLA, XLB */
			ABREG[absel] = ReadWA (MA);	/* load alt */
			break;
		case 025:				/* XSA, XSB */
			dms_viol (err_PC, MVI_PRV, 0);	/* priv if PRO */
			WriteWA (MA, ABREG[absel]);	/* store alt */
			break;
		case 026:				/* XCA, XCB */
			if (ABREG[absel] != ReadWA (MA))
				PC = (PC + 1) & VAMASK;
			break;
		case 027:				/* LFA, LFB */
			if (dms_ump) dms_viol (err_PC, MVI_PRV, 0);
			dms_sr = (dms_sr & ~(MST_FLT | MST_FENCE)) |
				(ABREG[absel] & (MST_FLT | MST_FENCE));
			dms_fence = dms_sr & MST_FENCE;
			break;

/* Extended instruction group: DMS, continued */

		case 030:				/* RSA, RSB */
			ABREG[absel] = dms_upd_sr ();	/* save stat */
			break;
		case 031:				/* RVA, RVB */
			ABREG[absel] = dms_vr;		/* save viol */
			break;
		case 032:				/* DJP */
			if (dms_ump) dms_viol (err_PC, MVI_PRV, 0);
			if (MP_TESTJ (MA)) ABORT (ABORT_FENCE);
			dms_enb = 0;
			PCQ_ENTRY;
			PC = MA;
			ion_defer = 1;
			break;
		case 033:				/* DJS */
			if (dms_ump) dms_viol (err_PC, MVI_PRV, 0);
			dms_enb = 0;
			WriteW (MA, PC);
			PCQ_ENTRY;
			PC = (MA + 1) & VAMASK;
			ion_defer = 1;
			break;
		case 034:				/* SJP */
			if (dms_ump) dms_viol (err_PC, MVI_PRV, 0);
			if (MP_TESTJ (MA)) ABORT (ABORT_FENCE);
			dms_enb = 1;
			dms_ump = 0;
			PCQ_ENTRY;
			PC = MA;
			ion_defer = 1;
			break;
		case 035:				/* SJS */
			if (dms_ump) dms_viol (err_PC, MVI_PRV, 0);
			dms_enb = 1;
			dms_ump = 0;
			WriteW (MA, PC);
			PCQ_ENTRY;
			PC = (MA + 1) & VAMASK;
			ion_defer = 1;
			break;
		case 036:				/* UJP */
			if (dms_ump) dms_viol (err_PC, MVI_PRV, 0);
			if (MP_TESTJ (MA)) ABORT (ABORT_FENCE);
			dms_enb = 1;
			dms_ump = 1;
			PCQ_ENTRY;
			PC = MA;
			ion_defer = 1;
			break;
		case 037:				/* UJS */
			if (dms_ump) dms_viol (err_PC, MVI_PRV, 0);
			dms_enb = 1;
			dms_ump = 1;
			WriteW (MA, PC);
			PCQ_ENTRY;
			PC = (MA + 1) & VAMASK;
			ion_defer = 1;
			break;

/* Extended instruction group: index register instructions */

		case 040:				/* SAX, SBX */
			MA = (MA + XR) & VAMASK;
			WriteW (MA, ABREG[absel]);
			break;
		case 041:				/* CAX, CBX */
			XR = ABREG[absel];
			break;
		case 042:				/* LAX, LBX */
			MA = (MA + XR) & VAMASK;
			ABREG[absel] = ReadW (MA);
			break;
		case 043:				/* STX */
			WriteW (MA, XR);
			break;
		case 044:				/* CXA, CXB */
			ABREG[absel] = XR;
			break;
		case 045:				/* LDX */
			XR = ReadW (MA);
			break;
		case 046:				/* ADX */
			opnd = ReadW (MA);
			t = XR + opnd;
			if (t > DMASK) E = 1;
			if (((~XR ^ opnd) & (XR ^ t)) & SIGN) O = 1;
			XR = t & DMASK;
			break;
		case 047:				/* XAX, XBX */
			t = XR;
			XR = ABREG[absel];
			ABREG[absel] = t;
			break;
		case 050:				/* SAY, SBY */
			MA = (MA + YR) & VAMASK;
			WriteW (MA, ABREG[absel]);
			break;
		case 051:				/* CAY, CBY */
			YR = ABREG[absel];
			break;
		case 052:				/* LAY, LBY */
			MA = (MA + YR) & VAMASK;
			ABREG[absel] = ReadW (MA);
			break;
		case 053:				/* STY */
			WriteW (MA, YR);
			break;
		case 054:				/* CYA, CYB */
			ABREG[absel] = YR;
			break;
		case 055:				/* LDY */
			YR = ReadW (MA);
			break;
		case 056:				/* ADY */
			opnd = ReadW (MA);
			t = YR + opnd;
			if (t > DMASK) E = 1;
			if (((~YR ^ opnd) & (YR ^ t)) & SIGN) O = 1;
			YR = t & DMASK;
			break;
		case 057:				/* XAY, XBY */
			t = YR;
			YR = ABREG[absel];
			ABREG[absel] = t;
			break;
		case 060:				/* ISX */
			XR = (XR + 1) & DMASK;
			if (XR == 0) PC = (PC + 1) & VAMASK;
			break;
		case 061:				/* DSX */
			XR = (XR - 1) & DMASK;
			if (XR == 0) PC = (PC + 1) & VAMASK;
			break;
		case 062:				/* JLY */
			if (MP_TESTJ (MA)) ABORT (ABORT_FENCE);
			PCQ_ENTRY;
			YR = PC;
			PC = MA;
			break;
		case 070:				/* ISY */
			YR = (YR + 1) & DMASK;
			if (YR == 0) PC = (PC + 1) & VAMASK;
			break;
		case 071:				/* DSY */
			YR = (YR - 1) & DMASK;
			if (YR == 0) PC = (PC + 1) & VAMASK;
			break;
		case 072:				/* JPY */
			MA = (ReadW (PC) + YR) & VAMASK; /* no indirect */
			PC = (PC + 1) & VAMASK;
			if (MP_TESTJ (MA)) ABORT (ABORT_FENCE);
			PCQ_ENTRY;
			PC = MA;
			break;

/* Extended instruction group: byte */

		case 063:				/* LBT */
			AR = ReadB (BR);
			BR = (BR + 1) & DMASK;
			break;
		case 064:				/* SBT */
			WriteB (BR, AR);
			BR = (BR + 1) & DMASK;
			break;
		case 065:				/* MBT */
			t = ReadW (awc);		/* get wc */
			while (t) {			/* while count */
			    q = ReadB (AR);		/* move byte */
			    WriteB (BR, q);
			    AR = (AR + 1) & DMASK;	/* incr src */
			    BR = (BR + 1) & DMASK;	/* incr dst */
			    t = (t - 1) & DMASK;	/* decr cnt */
			    WriteW (awc, t);  }
			break;
		case 066:				/* CBT */
			t = ReadW (awc);		/* get wc */
			while (t) {			/* while count */
			    q = ReadB (AR);		/* get src1 */
			    r = ReadB (BR);		/* get src2 */
			    if (q != r) {		/* compare */
				PC = (PC + 1 + (q > r)) & VAMASK;
				BR = (BR + t) & DMASK;	/* update BR */
				WriteW (awc, 0);	/* clr awc */
				break;  }
			    AR = (AR + 1) & DMASK;	/* incr src1 */
			    BR = (BR + 1) & DMASK;	/* incr src2 */
			    t = (t - 1) & DMASK;	/* decr cnt */
			    WriteW (awc, t);  }
			break;
		case 067:				/* SFB */
			q = AR & 0377;			/* test byte */
			r = (AR >> 8) & 0377;		/* term byte */
			for (;;) {			/* scan */
			    t = ReadB (BR);		/* get byte */
			    if (t == q) break;		/* test match? */
			    BR = (BR + 1) & DMASK;
			    if (t == r) {		/* term match? */
				PC = (PC + 1) & VAMASK;
				break;  }  }
			break;

/* Extended instruction group: bit, word */

		case 073:				/* SBS */
			WriteW (M1, M[M1] | M [MA]);
			break;
		case 074:				/* CBS */
			WriteW (M1, M[M1] & ~M[MA]);
			break;
		case 075:				/* TBS */
			if ((M[M1] & M[MA]) != M[MA]) PC = (PC + 1) & VAMASK;
			break;
		case 076:				/* CMW */
			t = ReadW (awc);		/* get wc */
			while (t) {			/* while count */
			    q = SEXT (M[AR & VAMASK]);
			    r = SEXT (M[BR & VAMASK]);
			    if (q != r) {		/* compare */
				PC = (PC + 1 + (q > r)) & VAMASK;
				BR = (BR + t) & DMASK;	/* update BR */
				WriteW (awc, 0);	/* clr awc */
				break;  }
			    AR = (AR + 1) & DMASK;	/* incr src1 */
			    BR = (BR + 1) & DMASK;	/* incr src2 */
			    t = (t - 1) & DMASK;	/* decr cnt */
			    WriteW (awc, t);  }
			break;
		case 077:				/* MVW */
			t = ReadW (awc);		/* get wc */
			while (t) {			/* while count */
			    q = ReadW (AR & VAMASK);	/* move word */
			    WriteW (BR & VAMASK, q);
			    AR = (AR + 1) & DMASK;	/* incr src */
			    BR = (BR + 1) & DMASK;	/* incr dst */
			    t = (t - 1) & DMASK;	/* decr cnt */
			    WriteW (awc, t);  }
			break;	}			/* end ext group */
	default:
		reason = stop_inst;  }			/* end switch IR */
	}						/* end if extended */
}							/* end while */

/* Simulation halted */

saved_AR = AR & DMASK;
saved_BR = BR & DMASK;
for (i = 0; dibp = dib_tab[i]; i++) {			/* loop thru dev */
	dev = dibp -> devno;
	dibp -> cmd = CMD (dev);
	dibp -> ctl = CTL (dev);
	dibp -> flg = FLG (dev);
	dibp -> fbf = FBF (dev);  }
pcq_r -> qptr = pcq_p;					/* update pc q ptr */
return reason;
}

/* Shift micro operation */

int32 shift (int32 t, int32 flag, int32 op)
{
int32 oldE;

op = op & 07;						/* get shift op */
if (flag) {						/* enabled? */
	switch (op) {					/* case on operation */
	case 00:					/* signed left shift */
		return ((t & SIGN) | ((t << 1) & 077777));
	case 01:					/* signed right shift */
		return ((t & SIGN) | (t >> 1));
	case 02:					/* rotate left */
		return (((t << 1) | (t >> 15)) & DMASK);
	case 03:					/* rotate right */
		return (((t >> 1) | (t << 15)) & DMASK);
	case 04:					/* left shift, 0 sign */
		return ((t << 1) & 077777);
	case 05:					/* ext right rotate */
		oldE = E;
		E = t & 1;
		return ((t >> 1) | (oldE << 15));
	case 06:					/* ext left rotate */
		oldE = E;
		E = (t >> 15) & 1;
		return (((t << 1) | oldE) & DMASK);
	case 07:					/* rotate left four */
		return (((t << 4) | (t >> 12)) & DMASK);
		}					/* end case */
	}						/* end if */
if (op == 05) E = t & 1;				/* disabled ext rgt rot */
if (op == 06) E = (t >> 15) & 1;			/* disabled ext lft rot */
return t;						/* input unchanged */
}

/* Device dispatch */

int32 devdisp (int32 devno, int32 inst, int32 IR, int32 dat)
{
if (dtab[devno]) return dtab[devno] (inst, IR, dat);
else return nulio (inst, IR, dat);
}

/* Calculate DMA requests */

int32 calc_dma (void)
{
int32 r = 0;

if (CMD (DMA0) && dmac[0].cw3 &&			/* check DMA0 cycle */
	FLG (dmac[0].cw1 & DEVMASK)) r = r | DMAR0;
if (CMD (DMA0) && dmac[1].cw3 &&			/* check DMA1 cycle */
	FLG (dmac[1].cw1 & DEVMASK)) r = r | DMAR1;
return r;
}

/* Calculate interrupt requests

   This routine takes into account all the relevant state of the
   interrupt system: ion, dev_flg, dev_fbf, and dev_ctl.

   1. dev_flg & dev_ctl determines the end of the priority grant.
      The break in the chain will occur at the first device for
      which dev_flg & dev_ctl is true.  This is determined by
      AND'ing the set bits with their 2's complement; only the low
      order (highest priority) bit will differ.  1 less than
      that, or'd with the single set bit itself, is the mask of
      possible interrupting devices.  If ION is clear, only devices
      4 and 5 are eligible to interrupt.
   2. dev_flg & dev_ctl & dev_fbf determines the outstanding
      interrupt requests.  All three bits must be on for a device
      to request an interrupt.  This is the masked under the
      result from #1 to determine the highest priority interrupt,
      if any.
 */

int32 calc_int (void)
{
int32 j, lomask, mask[2], req[2];

lomask = dev_flg[0] & dev_ctl[0] & ~M_NXDEV;		/* start chain calc */
req[0] = lomask & dev_fbf[0];				/* calc requests */
lomask = lomask & (-lomask);				/* chain & -chain */
mask[0] = lomask | (lomask - 1);			/* enabled devices */
req[0] = req[0] & mask[0];				/* highest request */
if (ion) {						/* ion? */
	if (lomask == 0) {				/* no break in chn? */
		mask[1] = dev_flg[1] & dev_ctl[1];	/* do all devices */
		req[1] = mask[1] & dev_fbf[1];
		mask[1] = mask[1] & (-mask[1]);
		mask[1] = mask[1] | (mask[1] - 1);
		req[1] = req[1] & mask[1];  }
	else req[1] = 0;  }
else {	req[0] = req[0] & (INT_M (PWR) | INT_M (PRO));
	req[1] = 0;  }
if (req[0]) {						/* if low request */
	for (j = 0; j < 32; j++) {			/* find dev # */
		if (req[0] & INT_M (j)) return j;  }  }
if (req[1]) {						/* if hi request */
	for (j = 0; j < 32; j++) {			/* find dev # */
		if (req[1] & INT_M (j)) return (32 + j);  }  }
return 0;
}

/* Memory access routines */

uint8 ReadB (int32 va)
{
int32 pa;

if (dms_enb) pa = dms (va >> 1, dms_ump, RD);
else pa = va >> 1;
if (va & 1) return (M[pa] & 0377);
else return ((M[pa] >> 8) & 0377);
}

uint8 ReadBA (int32 va)
{
int32 pa;

if (dms_enb) pa = dms (va >> 1, dms_ump ^ MAP_LNT, RD);
else pa = va >> 1;
if (va & 1) return (M[pa] & 0377);
else return ((M[pa] >> 8) & 0377);
}

uint16 ReadW (int32 va)
{
int32 pa;

if (dms_enb) pa = dms (va, dms_ump, RD);
else pa = va;
return M[pa];
}

uint16 ReadWA (int32 va)
{
int32 pa;

if (dms_enb) pa = dms (va, dms_ump ^ MAP_LNT, RD);
else pa = va;
return M[pa];
}

uint32 ReadF (int32 va)
{
uint32 t = ReadW (va);
uint32 t1 = ReadW ((va + 1) & VAMASK);
return (t << 16) | t1;
}

uint16 ReadIO (int32 va, int32 map)
{
int32 pa;

if (dms_enb) pa = dms (va, map, RD);
else pa = va;
return M[pa];
}

void WriteB (int32 va, int32 dat)
{
int32 pa;

if (MP_TEST (va)) ABORT (ABORT_FENCE);
if (dms_enb) pa = dms (va >> 1, dms_ump ^ MAP_LNT, WR);
else pa = va >> 1;
if (MEM_ADDR_OK (pa)) {
	if (va & 1) M[pa] = (M[pa] & 0177400) | (dat & 0377);
	else M[pa] = (M[pa] & 0377) | ((dat & 0377) << 8); }
return;
}

void WriteBA (int32 va, int32 dat)
{
int32 pa;

if (MP_TEST (va)) ABORT (ABORT_FENCE);
if (dms_enb) pa = dms (va >> 1, dms_ump ^ MAP_LNT, WR);
else pa = va >> 1;
if (MEM_ADDR_OK (pa)) {
	if (va & 1) M[pa] = (M[pa] & 0177400) | (dat & 0377);
	else M[pa] = (M[pa] & 0377) | ((dat & 0377) << 8); }
return;
}

void WriteW (int32 va, int32 dat)
{
int32 pa;

if (MP_TEST (va)) ABORT (ABORT_FENCE);
if (dms_enb) pa = dms (va, dms_ump, WR);
else pa = va;
if (MEM_ADDR_OK (pa)) M[pa] = dat;
return;
}

void WriteWA (int32 va, int32 dat)
{
int32 pa;

if (MP_TEST (va)) ABORT (ABORT_FENCE);
if (dms_enb) pa = dms (va, dms_ump ^ MAP_LNT, WR);
else pa = va;
if (MEM_ADDR_OK (pa)) M[pa] = dat;
return;
}

void WriteIO (int32 va, int32 dat, int32 map)
{
int32 pa;

if (dms_enb) pa = dms (va, map, WR);
else pa = va;
if (MEM_ADDR_OK (pa)) M[pa] = dat;
return;
}

/* DMS relocation */

int32 dms (int32 va, int32 map, int32 prot)
{
int32 pgn, mpr;

if (va <= 1) return va;					/* A, B */
pgn = VA_GETPAG (va);					/* get page num */
if (pgn == 0) {						/* base page? */
	if ((dms_sr & MST_FLT)?				/* check unmapped */
	    (va >= dms_fence):				/* 1B10: >= fence */
	    (va < dms_fence)) {				/* 0B10: < fence */
		if (prot == WR) dms_viol (va, MVI_BPG, 0);	/* if W, viol */
		return va;  }  }			/* no mapping */
mpr = dms_map[map + pgn];				/* get map reg */
if (mpr & prot) dms_viol (va, prot << (MVI_V_WPR - MAPA_V_WPR), 0);
return (PA_GETPAG (mpr) | VA_GETOFF (va));
}

/* DMS read and write maps */

uint16 dms_rmap (int32 mapi)
{
int32 t;
mapi = mapi & MAP_MASK;
t = (((dms_map[mapi] >> VA_N_OFF) & PA_M_PAG) |
	((dms_map[mapi] & (RD | WR)) << (MAPM_V_WPR - MAPA_V_WPR)));
return (uint16) t;
}

void dms_wmap (int32 mapi, int32 dat)
{
mapi = mapi & MAP_MASK;
dms_map[mapi] = ((dat & PA_M_PAG) << VA_N_OFF) |
	((dat >> (MAPM_V_WPR - MAPA_V_WPR)) & (RD | WR));
return;
}

/* DMS violation

   DMS violation processing occurs in two parts
   - The violation register is set based on DMS status
   - An interrupt (abort) occurs only if CTL (PRO) is set

   Bit 7 (ME bus disabled/enabled) records whether relocation
   actually occurred in the aborted cycle.  For read and write
   violations, bit 7 will be set; for base page and privilege
   violations, it will be clear

   I/O map references set status bits but never abort
*/

void dms_viol (int32 va, int32 st, t_bool io)
{
dms_sr = st | VA_GETPAG (va) |
	((st & (MVI_RPR | MVI_WPR))? MVI_MEB: 0) |	/* set MEB */	
	(dms_enb? MVI_MEM: 0) |				/* set MEM */
	(dms_ump? MVI_UMP: 0);				/* set UMAP */
if (CTL (PRO) && !io) ABORT (ABORT_DMS);
return;
}

/* DMS update status */

int32 dms_upd_sr (void)
{
dms_sr = dms_sr & ~(MST_ENB | MST_UMP | MST_PRO);
if (dms_enb) dms_sr = dms_sr | MST_ENB;
if (dms_ump) dms_sr = dms_sr | MST_UMP;
if (CTL (PRO)) dms_sr = dms_sr | MST_PRO;
return dms_sr;
}

/* Device 0 (CPU) I/O routine */

int32 cpuio (int32 inst, int32 IR, int32 dat)
{
int i;

switch (inst) {						/* case on opcode */
case ioFLG:						/* flag */
	ion = (IR & HC)? 0: 1;				/* interrupts off/on */
	return dat;
case ioSFC:						/* skip flag clear */
	if (!ion) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (ion) PC = (PC + 1) & VAMASK;
	return dat;
case ioLIX:						/* load */
	dat = 0;					/* returns 0 */
	break;
case ioCTL:						/* control */
	if (IR & AB) {					/* = CLC 06..77 */
		for (i = 6; i <= DEVMASK; i++) devdisp (i, inst, AB + i, 0);  }
	break;
default:
	break;  }
if (IR & HC) ion = 0;					/* HC option */
return dat;
}

/* Device 1 (overflow) I/O routine */

int32 ovfio (int32 inst, int32 IR, int32 dat)
{
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag */
	O = (IR & HC)? 0: 1;				/* clear/set overflow */
	return dat;
case ioSFC:						/* skip flag clear */
	if (!O) PC = (PC + 1) & VAMASK;
	break;						/* can clear flag */
case ioSFS:						/* skip flag set */
	if (O) PC = (PC + 1) & VAMASK;
	break;						/* can clear flag */
case ioMIX:						/* merge */
	dat = dat | SR;
	break;
case ioLIX:						/* load */
	dat = SR;
	break;
case ioOTX:						/* output */
	SR = dat;
	break;
default:
	break;  }
if (IR & HC) O = 0;					/* HC option */
return dat;
}

/* Device 4 (power fail) I/O routine */

int32 pwrio (int32 inst, int32 IR, int32 dat)
{
switch (inst) {						/* case on opcode */
case ioMIX:						/* merge */
	dat = dat | intaddr;
	break;
case ioLIX:						/* load */
	dat = intaddr;
	break;
default:
	break;  }
return dat;
}

/* Device 5 (memory protect) I/O routine */

int32 proio (int32 inst, int32 IR, int32 dat)
{
switch (inst) {						/* case on opcode */
case ioSFC:						/* skip flag clear */
	if (FLG (PRO)) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	return dat;
case ioMIX:						/* merge */
	dat = dat | maddr;
	break;
case ioLIX:						/* load */
	dat = maddr;
	break;
case ioOTX:						/* output */
	mfence = dat & VAMASK;
	break;
case ioCTL:						/* control clear/set */
	if ((IR & AB) == 0) {				/* STC */
		setCTL (PRO);
		clrFLG (PRO);  }
	break;
default:
	break;  }
return dat;
}

/* Devices 2,3 (secondary DMA) I/O routine */

int32 dmsio (int32 inst, int32 IR, int32 dat)
{
int32 ch;

ch = IR & 1;						/* get channel num */
switch (inst) {						/* case on opcode */
case ioMIX:						/* merge */
	dat = dat | dmac[ch].cw3;
	break;
case ioLIX:						/* load */
	dat = dmac[ch].cw3;
	break;
case ioOTX:						/* output */
	if (CTL (DMALT0 + ch)) dmac[ch].cw3 = dat;
	else dmac[ch].cw2 = dat;
	break;
case ioCTL:						/* control clear/set */
	if (IR & AB) { clrCTL (DMALT0 + ch); }		/* CLC */
	else { setCTL (DMALT0 + ch); }			/* STC */
	break;
default:
	break;  }
return dat;
}

/* Devices 6,7 (primary DMA) I/O routine */

int32 dmpio (int32 inst, int32 IR, int32 dat)
{
int32 ch, ddev;

ch = IR & 1;						/* get channel number */
ddev = dmac[ch].cw1 & DEVMASK;				/* get target device */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag */
	if ((IR & HC) == 0) { clrCMD (DMA0 + ch); }	/* set -> abort */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (DMA0 + ch) == 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (DMA0 + ch) != 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioMIX: case ioLIX:					/* load, merge */
	dat = DMASK;
	break;
case ioOTX:						/* output */
	dmac[ch].cw1 = dat;
	break;
case ioCTL:						/* control */
	if (IR & AB) { clrCTL (DMA0 + ch); }		/* CLC: cmd unchgd */
	else {	setCTL (DMA0 + ch);			/* STC: set ctl, cmd */
		setCMD (DMA0 + ch);  }
	break;
default:
	break;  }
if (IR & HC) { clrFLG (DMA0 + ch); }			/* HC option */
return dat;
}

/* DMA cycle routine */

void dma_cycle (int32 ch, int32 map)
{
int32 temp, dev, MA;

dev = dmac[ch].cw1 & DEVMASK;				/* get device */
MA = dmac[ch].cw2 & VAMASK;				/* get mem addr */
if (dmac[ch].cw2 & DMA2_OI) {				/* input? */
	temp = devdisp (dev, ioLIX, HC + dev, 0);	/* do LIA dev,C */
	WriteIO (MA, temp & DMASK, map);  }		/* store data */
else devdisp (dev, ioOTX, HC + dev, ReadIO (MA, map));	/* do OTA dev,C */
dmac[ch].cw2 = (dmac[ch].cw2 & DMA2_OI) | ((dmac[ch].cw2 + 1) & VAMASK);
dmac[ch].cw3 = (dmac[ch].cw3 + 1) & DMASK;		/* incr wcount */
if (dmac[ch].cw3) {					/* more to do? */
	if (dmac[ch].cw1 & DMA1_STC) devdisp (dev, ioCTL, dev, 0);  }
else {	if (dmac[ch].cw1 & DMA1_CLC) devdisp (dev, ioCTL, AB + dev, 0);
	else if ((dmac[ch].cw1 & DMA1_STC) && ((dmac[ch].cw2 & DMA2_OI) == 0))
		devdisp (dev, ioCTL, dev, 0);
	setFLG (DMA0 + ch);				/* set DMA flg */
	clrCMD (DMA0 + ch);  }				/* clr DMA cmd */
return;
}

/* Unimplemented device routine */

int32 nulio (int32 inst, int32 IR, int32 dat)
{
switch (inst) {						/* case on opcode */
case ioSFC:						/* skip flag clear */
	PC = (PC + 1) & VAMASK;
	return (stop_dev << IOT_V_REASON) | dat;
case ioSFS:						/* skip flag set */
	return (stop_dev << IOT_V_REASON) | dat;
default:
	break;  }
if (IR & HC) { clrFLG (IR & DEVMASK); }			/* HC option */
return (stop_dev << IOT_V_REASON) | dat;
}

/* Reset routines */

t_stat cpu_reset (DEVICE *dptr)
{
saved_AR = saved_BR = 0;
XR = YR = 0;
E = 0;
O = 0;
ion = ion_defer = 0;
clrCMD (PWR);
clrCTL (PWR);
clrFLG (PWR);
clrFBF (PWR);
clrCMD (PRO);
clrCTL (PRO);
clrFLG (PRO);
clrFBF (PRO);
mfence = 0;
maddr = 0;
dms_enb = dms_ump = 0;
dms_sr = dms_fence = 0;
dms_vr = dms_sma = 0;
pcq_r = find_reg ("PCQ", NULL, dptr);
sim_brk_types = sim_brk_dflt = SWMASK ('E');
if (M == NULL) M = calloc (PASIZE, sizeof (unsigned int16));
if (M == NULL) return SCPE_MEM;
if (pcq_r) pcq_r -> qptr = 0;
else return SCPE_IERR;
return SCPE_OK;
}

t_stat dma0_reset (DEVICE *tptr)
{
clrCMD (DMA0);
clrCTL (DMA0);
clrFLG (DMA0);
clrFBF (DMA0);
dmac[0].cw1 = dmac[0].cw2 = dmac[0].cw3 = 0;
return SCPE_OK;
}

t_stat dma1_reset (DEVICE *tptr)
{
clrCMD (DMA1);
clrCTL (DMA1);
clrFLG (DMA1);
clrFBF (DMA1);
dmac[1].cw1 = dmac[1].cw2 = dmac[1].cw3 = 0;
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
int32 d;

if (addr >= MEMSIZE) return SCPE_NXM;
if (addr == 0) d = saved_AR;
else if (addr == 1) d = saved_BR;
else d = M[addr];
if (vptr != NULL) *vptr = d & DMASK;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE) return SCPE_NXM;
if (addr == 0) saved_AR = val & DMASK;
else if (addr == 1) saved_BR = val & DMASK;
else M[addr] = val & DMASK;
return SCPE_OK;
}

t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 mc = 0;
t_addr i;

if ((val <= 0) || (val > PASIZE) || ((val & 07777) != 0) ||
	(!(uptr -> flags & UNIT_21MX) && (val > 32768)))
	return SCPE_ARG;
for (i = val; i < MEMSIZE; i++) mc = mc | M[i];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
	return SCPE_OK;
MEMSIZE = val;
for (i = MEMSIZE; i < PASIZE; i++) M[i] = 0;
return SCPE_OK;
}

/* Set/show device number */

t_stat hp_setdev (UNIT *uptr, int32 num, char *cptr, void *desc)
{
int32 i, newdev;
DIB *dibp = (DIB *) desc;
t_stat r;

if (cptr == NULL) return SCPE_ARG;
if ((dibp == NULL) || (num > 1)) return SCPE_IERR;
newdev = get_uint (cptr, 8, DEVMASK - num, &r);
if (r != SCPE_OK) return r;
if (newdev < VARDEV) return SCPE_ARG;
for (i = 0; i <= num; i++, dibp++) dibp -> devno = newdev + i;
return SCPE_OK;
}

t_stat hp_showdev (FILE *st, UNIT *uptr, int32 num, void *desc)
{
int32 i;
DIB *dibp = (DIB *) desc;

if (dibp == NULL) return SCPE_IERR;
fprintf (st, "devno=%o", dibp -> devno);
for (i = 1; i <= num; i++) fprintf (st, "/%o", dibp -> devno + i);
return SCPE_OK;
}

/* Enable a device */

t_stat set_enb (UNIT *uptr, int32 num, char *cptr, void *desc)
{
int32 i;
DEVICE *dptr;
DIB *dibp;

if (cptr != NULL) return SCPE_ARG;
if ((uptr == NULL) || (desc == NULL)) return SCPE_IERR;
dptr = find_dev_from_unit (uptr);			/* find device */
if (dptr == NULL) return SCPE_IERR;
dibp = (DIB *) desc;
if (dibp -> enb) return SCPE_OK;			/* already enb? */
for (i = 0; i <= num; i++, dibp++) dibp -> enb = 1;
if (dptr -> reset) return dptr -> reset (dptr);
else return SCPE_OK;
}

/* Disable a device */

t_stat set_dis (UNIT *uptr, int32 num, char *cptr, void *desc)
{
int32 i;
DEVICE *dptr;
DIB *dibp;
UNIT *up;

if (cptr != NULL) return SCPE_ARG;
if ((uptr == NULL) || (desc == NULL)) return SCPE_IERR;
dptr = find_dev_from_unit (uptr);			/* find device */
if (dptr == NULL) return SCPE_IERR;
dibp = (DIB *) desc;
if (dibp -> enb == 0) return SCPE_OK;			/* already dis? */
for (i = 0; i < dptr -> numunits; i++) {		/* check units */
	up = (dptr -> units) + i;
	if ((up -> flags & UNIT_ATT) || sim_is_active (up))
	    return SCPE_NOFNC;  }
for (i = 0; i <= num; i++, dibp++) dibp -> enb = 0;
if (dptr -> reset) return dptr -> reset (dptr);
else return SCPE_OK;
}

/* Test for device conflict */

t_bool dev_conflict (void)
{
DIB *dibp, *chkp;
int32 i, j, dno;

for (i = 0; chkp = dib_tab[i]; i++) {
    if (chkp -> enb) {
	dno = chkp -> devno;
	for (j = 0; dibp = dib_tab[j]; j++) {
	    if (dibp -> enb && (chkp != dibp) && (dno == dibp -> devno)) {
		printf ("Device number conflict, devno = %d\n", dno);
		if (sim_log) fprintf (sim_log,
		    "Device number conflict, devno = %d\n", dno);
		return TRUE;  }  }  }  }
return FALSE;
}
