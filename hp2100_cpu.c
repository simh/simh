/* hp2100_cpu.c: HP 2100 CPU simulator

   Copyright (c) 1993-2000, Robert M. Supnik

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

#define ILL_ADR_FLAG	0100000
#define save_ibkpt	(cpu_unit.u3)
#define UNIT_V_MSIZE	(UNIT_V_UF)			/* dummy mask */
#define UNIT_MSIZE	(1 << UNIT_V_MSIZE)
#define UNIT_V_2100	(UNIT_V_UF + 1)			/* 2100 vs 2116 */
#define UNIT_2100	(1 << UNIT_V_2100)
#define UNIT_V_21MX	(UNIT_V_UF + 2)			/* 21MX vs 2100 */
#define UNIT_21MX	(1 << UNIT_V_21MX)

unsigned int16 M[MAXMEMSIZE] = { 0 };			/* memory */
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
int32 ind_max = 16;					/* iadr nest limit */
int32 stop_inst = 1;					/* stop on ill inst */
int32 stop_dev = 2;					/* stop on ill dev */
int32 ibkpt_addr = ILL_ADR_FLAG | AMASK;		/* breakpoint addr */
int32 old_PC = 0;					/* previous PC */

extern int32 sim_int_char;
int32 shift (int32 inval, int32 flag, int32 oper);
int32 calc_dma (void);
int32 calc_int (void);
void dma_cycle (int32 chan);
t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat dma0_reset (DEVICE *dptr);
t_stat dma1_reset (DEVICE *dptr);
t_stat cpu_svc (UNIT *uptr);
t_stat cpu_set_size (UNIT *uptr, int32 value);

/* CPU data structures

   cpu_dev	CPU device descriptor
   cpu_unit	CPU unit descriptor
   cpu_reg	CPU register list
   cpu_mod	CPU modifiers list
*/

UNIT cpu_unit = { UDATA (&cpu_svc, UNIT_FIX + UNIT_BINK,
		MAXMEMSIZE) };

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
	{ FLDATA (STOP_INST, stop_inst, 0) },
	{ FLDATA (STOP_DEV, stop_dev, 1) },
	{ DRDATA (INDMAX, ind_max, 16), REG_NZ + PV_LEFT },
	{ ORDATA (OLDP, old_PC, 15), REG_RO },
	{ ORDATA (BREAK, ibkpt_addr, 16) },
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
	{ UNIT_MSIZE, 12288, NULL, "12K", &cpu_set_size },
	{ UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
	{ UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
	{ UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
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
	{ ORDATA (CW3, dmac[0].cw3, 16) }  };

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
	{ ORDATA (CW3, dmac[1].cw3, 16) }  };

DEVICE dma1_dev = {
	"DMA1", &dma1_unit, dma1_reg, NULL,
	1, 8, 1, 1, 8, 16,
	NULL, NULL, &dma1_reset,
	NULL, NULL, NULL };

/* Extended instruction decode tables */

static const t_bool ext_addr[192] = {			/* ext inst format */
 0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };

static const t_bool exg_breq[16] = {			/* ext grp B only */
 0,0,0,1,0,1,1,0,0,0,0,1,0,1,1,0 };

static const t_bool exg_addr[32] = {			/* ext grp format */
 1,0,1,1,0,1,1,0,1,0,1,1,0,1,1,0,0,0,1,0,0,2,2,0,0,0,1,3,3,3,2,2 };

/* Interrupt defer table */

static const t_bool defer_tab[] = { 0, 1, 1, 1, 0, 0, 0, 1 };

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

/* Dynamic device information table */

extern int32 ptrio (int32 op, int32 IR, int32 outdat);
extern int32 ptpio (int32 op, int32 IR, int32 outdat);
extern int32 ttyio (int32 op, int32 IR, int32 outdat);
extern int32 clkio (int32 op, int32 IR, int32 outdat);
extern int32 lptio (int32 op, int32 IR, int32 outdat);
extern int32 mtdio (int32 op, int32 IR, int32 outdat);
extern int32 mtcio (int32 op, int32 IR, int32 outdat);
extern int32 dpdio (int32 op, int32 IR, int32 outdat);
extern int32 dpcio (int32 op, int32 IR, int32 outdat);

struct hpdev infotab[] = {
	{ PTR, 0, 0, 0, 0, &ptrio },
	{ PTP, 0, 0, 0, 0, &ptpio },
	{ TTY, 0, 0, 0, 0, &ttyio },
	{ CLK, 0, 0, 0, 0, &clkio },
	{ LPT, 0, 0, 0, 0, &lptio },
	{ MTD, 0, 0, 0, 0, &mtdio },
	{ MTC, 0, 0, 0, 0, &mtcio },
	{ DPD, 0, 0, 0, 0, &dpdio },
	{ DPC, 0, 0, 0, 0, &dpcio },
	{ 0 }  };

t_stat sim_instr (void)
{
extern int32 sim_interval;
register int32 IR, MA, absel, i, t, intrq, dmarq;
int32 err_PC, M1, dev, iodata, op, sc, q, r, wc;
t_stat reason;

#define DMAR0	1
#define DMAR1	2
#define SEXT(x) (((x) & SIGN)? (((int32) (x)) | ~DMASK): ((int32) (x)))
#define LDBY(a) ((M[(a) >> 1] >> (((a) & 1)? 0: 8)) & 0377)
#define STBY(a,d) MA = (a) >> 1; \
		MP_TEST (MA); \
		if (!MEM_ADDR_OK (MA)) break; \
		if ((a) & 1) M[MA] = (M[MA] & 0177400) | ((d) & 0377); \
		else M[MA] = (M[MA] & 0377) | (((d) & 0377) << 8)

/* Memory protection tests */

#define MP_TEST(x) if (CTL (PRO) && ((x) > 1) && ((x) < mfence)) { \
		maddr = err_PC | 0100000; \
		setFLG (PRO); \
		intrq = PRO; \
		break;  }

#define MP_TESTJ(x) if (CTL (PRO) && ((x) < mfence)) { \
		maddr = err_PC | 0100000; \
		setFLG (PRO); \
		intrq = PRO; \
		break;  }

/* Restore register state */

AR = saved_AR & DMASK;					/* restore reg */
BR = saved_BR & DMASK;
err_PC = PC = PC & AMASK;				/* load local PC */
reason = 0;

/* Restore I/O state */

for (i = VARDEV; i <= DEVMASK; i++) dtab[i] = NULL;
for (i = 0; infotab[i].devno != 0; i++) {		/* loop thru dev */
	dev = infotab[i].devno;				/* get dev # */
	if (infotab[i].ctl) { setCMD (dev); }		/* restore cmd */
	else { clrCMD (dev); }
	if (infotab[i].ctl) { setCTL (dev); }		/* restore ctl */
	else { clrCTL (dev); }
	if (infotab[i].flg) { setFLG (dev); }		/* restore flg */
	else { clrFLG (dev); }
	if (infotab[i].fbf) { setFBF (dev); }		/* restore fbf */
	else { clrFBF (dev); }
	dtab[dev] = infotab[i].iot;  }			/* set I/O dispatch */
dmarq = calc_dma ();					/* recalc DMA masks */
intrq = calc_int ();					/* recalc interrupts */

/* Main instruction fetch/decode loop */

while (reason == 0) {					/* loop until halted */
if (sim_interval <= 0) {				/* check clock queue */
	if (reason = sim_process_event ()) break;  
	dmarq = calc_dma ();				/* recalc DMA reqs */
	intrq = calc_int ();  }				/* recalc interrupts */

if (dmarq) {
	if (dmarq & DMAR0) dma_cycle (0);		/* DMA1 cycle? */
	if (dmarq & DMAR1) dma_cycle (1);		/* DMA2 cycle? */
	dmarq = calc_dma ();				/* recalc DMA reqs */
	intrq = calc_int ();  }				/* recalc interrupts */

if (intrq && ((intrq <= PRO) || !ion_defer)) {		/* interrupt request? */
	clrFBF (intrq);					/* clear flag buffer */
	intaddr = intrq;				/* save int addr */
	IR = M[intrq];					/* get dispatch instr */
	ion_defer = 1;					/* defer interrupts */
	intrq = 0;					/* clear request */
	clrCTL (PRO); }					/* protection off */

else {	if (PC == ibkpt_addr) {				/* breakpoint? */
		save_ibkpt = ibkpt_addr;		/* save address */
		ibkpt_addr = ibkpt_addr | ILL_ADR_FLAG;	/* disable */
		sim_activate (&cpu_unit, 1);		/* sched re-enable */
		reason = STOP_IBKPT;			/* stop simulation */
		break;  }
	err_PC = PC;					/* save PC for error */
	IR = M[PC];					/* fetch instr */
	PC = (PC + 1) & AMASK;
	sim_interval = sim_interval - 1;
	ion_defer = 0;  }
absel = (IR & AB)? 1: 0;				/* get A/B select */

/* Memory reference instructions */

if (IR & MROP) {					/* mem ref? */
	MA = IR & (IA | DISP);				/* ind + disp */
	if (IR & CP) MA = ((PC - 1) & PAGENO) | MA;	/* current page? */
	for (i = 0; (i < ind_max) && (MA & IA); i++)	/* resolve multi- */
		MA = M[MA & AMASK];			/* level indirect */
	if (i >= ind_max) {				/* indirect loop? */
		reason = STOP_IND;
		break;  }

	switch ((IR >> 11) & 017) {			/* decode IR<14:11> */
	case 002:					/* AND */
		AR = AR & M[MA];
		break;
	case 003:					/* JSB */
		MP_TEST (MA);
		if (MEM_ADDR_OK (MA)) M[MA] = PC;
		old_PC = PC;
		PC = (MA + 1) & AMASK;
		if (IR & IA) ion_defer = 1;
		break;
	case 004:					/* XOR */
		AR = AR ^ M[MA];
		break;
	case 005:					/* JMP */
		MP_TESTJ (MA);
		old_PC = PC;
		PC = MA;
		if (IR & IA) ion_defer = 1;
		break;
	case 006:					/* IOR */
		AR = AR | M[MA];
		break;
	case 007:					/* ISZ */
		MP_TEST (MA);
		t = (M[MA] + 1) & DMASK;
		if (MEM_ADDR_OK (MA)) M[MA] = t;
		if (t == 0) PC = (PC + 1) & AMASK;
		break;

/* Memory reference instructions, continued */

	case 010:					/* ADA */
		t = (int32) AR + (int32) M[MA];
		if (t > DMASK) E = 1;
		if (((~AR ^ M[MA]) & (AR ^ t)) & SIGN) O = 1;
		AR = t & DMASK;
		break;
	case 011:					/* ADB */
		t = (int32) BR + (int32) M[MA];
		if (t > DMASK) E = 1;
		if (((~BR ^ M[MA]) & (BR ^ t)) & SIGN) O = 1;
		BR = t & DMASK;
		break;
	case 012:					/* CPA */
		if (AR != M[MA]) PC = (PC + 1) & AMASK;
		break;
	case 013:					/* CPB */
		if (BR != M[MA]) PC = (PC + 1) & AMASK;
		break;
	case 014:					/* LDA */
		AR = M[MA];
		break;
	case 015:					/* LDB */
		BR = M[MA];
		break;
	case 016:					/* STA */
		MP_TEST (MA);
		if (MEM_ADDR_OK (MA)) M[MA] = AR;
		break;
	case 017:					/* STB */
		MP_TEST (MA);
		if (MEM_ADDR_OK (MA)) M[MA] = BR;
		break;  }				/* end case IR */
	}						/* end if mem ref */

/* Alter/skip instructions */

else if ((IR & NMROP) == ASKP) {			/* alter/skip? */
	int skip = 0;					/* no skip */

	if (IR & 000400) t = 0;				/* CLx */
	else t = ABREG[absel];
	if (IR & 001000) t = t ^ DMASK;			/* CMx */
	if (IR & 000001) {				/* RSS? */
		if ((IR & 000040) && (E != 0)) skip = 1;/* SEZ,RSS */
		if (IR & 000100) E = 0;			/* CLE */
		if (IR & 000200) E = E ^ 1;		/* CME */
		if (((IR & 000030) == 000030) &&	/* SSx,SLx,RSS */
			(t == 0100001)) skip = 1;
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
	PC = (PC + skip) & AMASK;			/* add in skip */
	}						/* end if alter/skip */

/* Shift instructions */

else if ((IR & NMROP) == SHFT) {			/* shift? */
	t = shift (ABREG[absel], IR & 01000, IR >> 6);	/* do first shift */
	if (IR & 000040) E = 0;				/* CLE */
	if ((IR & 000010) && ((t & 1) == 0))		/* SLx */
		PC = (PC + 1) & AMASK;
	ABREG[absel] = shift (t, IR & 00020, IR);	/* do second shift */
	}						/* end if shift */

/* I/O instructions */

else if ((IR & NMROP) == IOT) {				/* I/O? */
	dev = IR & DEVMASK;				/* get device */
	t = (IR >> 6) & 07;				/* get subopcode */
	if (CTL (PRO) && ((t == ioHLT) || (dev != OVF))) {
		maddr = err_PC | 0100000;
		setFLG (PRO);
		intrq = PRO;
		break;  }
	iodata = devdisp (dev, t, IR, ABREG[absel]);	/* process I/O */
	if ((t == ioMIX) || (t == ioLIX)) ABREG[absel] = iodata & DMASK;
	if (t == ioHLT) reason = STOP_HALT;
	else reason = iodata >> IOT_V_REASON;
	ion_defer = defer_tab[t];			/* set defer */
	dmarq = calc_dma ();				/* recalc DMA */
	intrq = calc_int ();				/* recalc interrupts */
	}						/* end if I/O */

/* Extended instructions */

else if (cpu_unit.flags & (UNIT_2100 | UNIT_21MX)) {	/* ext instr? */
	register int32 awc;

	op = (IR >> 4) & 0277;				/* get opcode */
	if (ext_addr[op]) {				/* extended mem ref? */
		MA = M[PC];				/* get next address */
		PC = (PC + 1) & AMASK;
		for (i = 0; (i < ind_max) && (MA & IA); i++)
			MA = M[MA & AMASK];
		if (i >= ind_max) {
			reason = STOP_IND;
			break;  }  }
	sc = (IR & 017);				/* get shift count */
	if (sc == 0) sc = 16;
	switch (op) {					/* decode IR<11:4> */
	case 0010:					/* MUL */
		t = SEXT (AR) * SEXT (M[MA]);
		BR = (t >> 16) & DMASK;
		AR = t & DMASK;
		O = 0;
		break;
	case 0020:					/* DIV */
		if ((M[MA] == 0) ||			/* divide by zero? */
		   ((BR == SIGN) && (AR == 0) && (M[MA] == DMASK))) {
			O = 1;				/* set overflow */
			break;  }
		t = (SEXT (BR) << 16) || (int32) AR;
		q = t / SEXT (M[MA]);			/* quotient */
		r = t % SEXT (M[MA]);			/* remainder */
		if ((q >= 077777) || (q < -0100000)) {	/* quo too large? */
			if (BR & SIGN) {		/* negative divd? */
				AR = (-AR) & DMASK;	/* take abs value */
				BR = ((BR ^ DMASK) + (AR == 0)) & DMASK;  }
			O = 1;  }
		else {	AR = q & DMASK;			/* set quo, rem */
			BR = r & DMASK;
			O = 0;  }
		break;
	case 0210:					/* DLD */
		AR = M[MA];
		MA = (MA + 1) & AMASK;
		BR = M[MA];
		break;
	case 0220:					/* DST */
		MP_TEST (MA);
		if (MEM_ADDR_OK (MA)) M[MA] = AR;
		MA = (MA + 1) & AMASK;
		if (MEM_ADDR_OK (MA)) M[MA] = BR;
		break;

/* Extended arithmetic instructions */

	case 0001:					/* ASL */
		t = (SEXT (BR) >> (16 - sc)) & DMASK;
		if (t != ((BR & SIGN)? DMASK: 0)) O = 1;
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

/* Extended instruction group */

	case 0076:					/* ext inst grp, A */
		if (exg_breq[IR & 017]) {		/* must have B set? */
			reason = stop_inst;
			break;  }
	case 0276: case 0277:				/* ext inst grp, B */
		if ((cpu_unit.flags & UNIT_21MX) == 0) {
			reason = stop_inst;
			break;  }
		op = IR & 037;				/* get sub opcode */
		if (exg_addr[op]) {			/* mem addr? */
			MA = M[PC];			/* get next address */
			PC = (PC + 1) & AMASK;
			for (i = 0; (i < ind_max) && (MA & IA); i++) 
				MA = M[MA & AMASK];
			if (i >= ind_max) {
				reason = STOP_IND;
				break;  }  }
		if (exg_addr[op] == 2) {		/* word of zero? */
			wc = M[MA];			/* get count */
			if (M[PC] == 0) M[PC] = wc;	/* save count */
			awc = PC;			/* and addr */
			PC = (PC + 1) & AMASK;  }
		if (exg_addr[op] == 3) {		/* second address? */
			M1 = M[PC];			/* get next address */
			PC = (PC + 1) & AMASK;
			for (i = 0; (i < ind_max) && (M1 & IA); i++)
				M1 = M[M1 & AMASK];
			if (i >= ind_max) {
				reason = STOP_IND;
				break;  }  }
		switch (op) {				/* case on sub op */
			t_bool cmpeql;

/* Extended instruction group: index register instructions */

		case 000:				/* SAX, SBX */
			MA = (MA + XR) & AMASK;
			MP_TEST (MA);
			if (MEM_ADDR_OK (MA)) M[MA] = ABREG[absel];
			break;
		case 001:				/* CAX, CBX */
			XR = ABREG[absel];
			break;
		case 002:				/* LAX, LBX */
			MA = (MA + XR) & AMASK;
			ABREG[absel] = M[MA];
			break;
		case 003:				/* STX */
			MP_TEST (MA);
			if (MEM_ADDR_OK (MA)) M[MA] = XR;
			break;
		case 004:				/* CXA, CXB */
			ABREG[absel] = XR;
			break;
		case 005:				/* LDX */
			XR = M[MA];
			break;
		case 006:				/* ADX */
			t = XR + M[MA];
			if (t > DMASK) E = 1;
			if (((~XR ^ M[MA]) & (XR ^ t)) & SIGN) O = 1;
			XR = t & DMASK;
			break;
		case 007:				/* XAX, XBX */
			t = XR;
			XR = ABREG[absel];
			ABREG[absel] = t;
			break;
		case 010:				/* SAY, SBY */
			MA = (MA + YR) & AMASK;
			MP_TEST (MA);
			if (MEM_ADDR_OK (MA)) M[MA] = ABREG[absel];
			break;
		case 011:				/* CAY, CBY */
			YR = ABREG[absel];
			break;
		case 012:				/* LAY, LBY */
			MA = (MA + YR) & AMASK;
			ABREG[absel] = M[MA];
			break;
		case 013:				/* STY */
			MP_TEST (MA);
			if (MEM_ADDR_OK (MA)) M[MA] = YR;
			break;
		case 014:				/* CYA, CYB */
			ABREG[absel] = YR;
			break;
		case 015:				/* LDY */
			YR = M[MA];
			break;
		case 016:				/* ADY */
			t = YR + M[MA];
			if (t > DMASK) E = 1;
			if (((~YR ^ M[MA]) & (YR ^ t)) & SIGN) O = 1;
			YR = t & DMASK;
			break;
		case 017:				/* XAY, XBY */
			t = YR;
			YR = ABREG[absel];
			ABREG[absel] = t;
			break;
		case 020:				/* ISX */
			XR = (XR + 1) & DMASK;
			if (XR == 0) PC = (PC + 1) & AMASK;
			break;
		case 021:				/* DSX */
			XR = (XR - 1) & DMASK;
			if (XR == 0) PC = (PC + 1) & AMASK;
			break;
		case 022:				/* JLY */
			MP_TESTJ (MA);
			old_PC = PC;
			YR = PC;
			PC = MA;
			break;
		case 030:				/* ISY */
			YR = (YR + 1) & DMASK;
			if (YR == 0) PC = (PC + 1) & AMASK;
			break;
		case 031:				/* DSY */
			YR = (YR - 1) & DMASK;
			if (YR == 0) PC = (PC + 1) & AMASK;
			break;
		case 032:				/* JPY */
			MA = (M[PC] + YR) & AMASK;	/* no indirect */
			PC = (PC + 1) & AMASK;
			MP_TESTJ (MA);
			old_PC = PC;
			PC = MA;
			break;

/* Extended instruction group: byte */

		case 023:				/* LBT */
			AR = LDBY (BR);
			break;
		case 024:				/* SBT */
			STBY (BR, AR);
			break;
		case 025:				/* MBT */
			while (M[awc]) {		/* while count */
				q = LDBY (AR);		/* load byte */
				STBY (BR, q);		/* store byte */
				AR = (AR + 1) & DMASK;	/* incr src */
				BR = (BR + 1) & DMASK;	/* incr dst */
				M[awc] = (M[awc] - 1) & DMASK;  }
			break;
		case 026:				/* CBT */
			cmpeql = TRUE;
			while (M[awc]) {		/* while count */
				q = LDBY (AR);		/* get src1 */
				r = LDBY (BR);		/* get src2 */
				if (cmpeql && (q != r)) {	/* compare */
					PC = (PC + 1 + (q > r)) & AMASK;
					cmpeql = FALSE;  }
				AR = (AR + 1) & DMASK;
				BR = (BR + 1) & DMASK;
				M[awc] = (M[awc] - 1) & DMASK;  }
			break;
		case 027:				/* SFB */
			q = AR & 0377;			/* test byte */
			r = (AR >> 8) & 0377;		/* term byte */
			for (;;) {			/* scan */
				t = LDBY (BR);		/* get byte */
				if (t == q) break;	/* test match? */
				BR = (BR + 1) & DMASK;
				if (t == r) {		/* term match? */
					PC = (PC + 1) & AMASK;
					break;  }  }
			break;

/* Extended instruction group: bit, word */

		case 033:				/* SBS */
			MP_TEST (M1);
			if (MEM_ADDR_OK (M1)) M[M1] = M[M1] | M[MA];
			break;
		case 034:				/* CBS */
			MP_TEST (M1);
			if (MEM_ADDR_OK (M1)) M[M1] = M[M1] & ~M[MA];
			break;
		case 035:				/* TBS */
			if ((M[M1] & M[MA]) != M[MA]) PC = (PC + 1) & AMASK;
			break;
		case 036:				/* CMW */
			cmpeql = TRUE;
			while (M[awc]) {		/* while count */
				q = SEXT (M[AR & AMASK]);
				r = SEXT (M[BR & AMASK]);
				if (cmpeql && (q != r)) {	/* compare */
					PC = (PC + 1 + (q > r)) & AMASK;
					cmpeql = FALSE;  }
				AR = (AR + 1) & DMASK;
				BR = (BR + 1) & DMASK;
				M[awc] = (M[awc] - 1) & DMASK;  }
			break;
		case 037:				/* MVW */
			while (M[awc]) {		/* while count */
				MP_TEST (BR & AMASK);
				if (MEM_ADDR_OK (BR & AMASK))
					M[BR & AMASK] = M[AR & AMASK];
				BR = (BR + 1) & DMASK;
				AR = (AR + 1) & DMASK;
				M[awc] = (M[awc] - 1) & DMASK;  }
			break;	}			/* end ext group */

/* Floating point instructions */

	case 0240:					/* FAD */
	case 0241:					/* FSB */
	case 0242:					/* FMP */
	case 0243:					/* FDV */
	case 0244:					/* FIX */
	case 0245:					/* FLT */
	default:
		reason = stop_inst;  }			/* end switch IR */
	}						/* end if extended */
}							/* end while */

/* Simulation halted */

saved_AR = AR & DMASK;
saved_BR = BR & DMASK;
for (i = 0; infotab[i].devno != 0; i++) {		/* save dynamic info */
	dev = infotab[i].devno;
	infotab[i].ctl = CMD (dev);
	infotab[i].ctl = CTL (dev);
	infotab[i].flg = FLG (dev);
	infotab[i].fbf = FBF (dev);  }
dev_flg[0] = dev_flg[0] & M_FXDEV;			/* clear dynamic info */
dev_fbf[0] = dev_fbf[0] & M_FXDEV;
dev_ctl[0] = dev_ctl[0] & M_FXDEV;
dev_flg[1] = dev_fbf[1] = dev_ctl[1] = 0;
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
   interrupt system: ion, dev_flg, dev_buf, and dev_ctl.

   1. dev_flg & dev_ctl determines the end of the priority grant.
      The break in the chain will occur at the first device for
      which dev_flag & dev_ctl is true.  This is determined by
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
if (req[1]) {					/* if hi request */
	for (j = 0; j < 32; j++) {			/* find dev # */
		if (req[1] & INT_M (j)) return (32 + j);  }  }
return 0;
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
	if (!ion) PC = (PC + 1) & AMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (ion) PC = (PC + 1) & AMASK;
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
	if (!O) PC = (PC + 1) & AMASK;
	break;						/* can clear flag */
case ioSFS:						/* skip flag set */
	if (O) PC = (PC + 1) & AMASK;
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
	if (FLG (PRO)) PC = (PC + 1) & AMASK;
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
	mfence = dat & AMASK;
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
	if (FLG (DMA0 + ch) == 0) PC = (PC + 1) & AMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (DMA0 + ch) != 0) PC = (PC + 1) & AMASK;
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

void dma_cycle (int32 ch)
{
int32 temp, dev, MA;

dev = dmac[ch].cw1 & DEVMASK;				/* get device */
MA = dmac[ch].cw2 & AMASK;				/* get mem addr */
if (dmac[ch].cw2 & DMA2_OI) {				/* input? */
	temp = devdisp (dev, ioLIX, HC + dev, 0);	/* do LIA dev,C */
	if (MEM_ADDR_OK (MA)) M[MA] = temp & DMASK;  }	/* store data */
else devdisp (dev, ioOTX, HC + dev, M[MA]);		/* do OTA dev,C */
dmac[ch].cw2 = (dmac[ch].cw2 & DMA2_OI) | ((dmac[ch].cw2 + 1) & AMASK);
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
	PC = (PC + 1) & AMASK;
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
return cpu_svc (&cpu_unit);
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

/* Breakpoint service */

t_stat cpu_svc (UNIT *uptr)
{
if ((ibkpt_addr & ~ILL_ADR_FLAG) == save_ibkpt) ibkpt_addr = save_ibkpt;
save_ibkpt = -1;
return SCPE_OK;
}

t_stat cpu_set_size (UNIT *uptr, int32 value)
{
int32 mc = 0;
t_addr i;

if ((value <= 0) || (value > MAXMEMSIZE) || ((value & 07777) != 0))
	return SCPE_ARG;
for (i = value; i < MEMSIZE; i++) mc = mc | M[i];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
	return SCPE_OK;
MEMSIZE = value;
for (i = MEMSIZE; i < MAXMEMSIZE; i++) M[i] = 0;
return SCPE_OK;
}

/* Set device number */

extern char *read_line (char *ptr, int size, FILE *stream);
extern t_value get_uint (char *cptr, int radix, t_value max, t_stat *status);

t_stat hp_setdev (UNIT *uptr, int32 ord)
{
char cbuf[CBUFSIZE], *cptr;
int32 i, olddev, newdev;
t_stat r;

olddev = infotab[ord].devno;
printf ("Device number:	%-o	", olddev);
cptr = read_line (cbuf, CBUFSIZE, stdin);
if ((cptr == NULL) || (*cptr == 0)) return SCPE_OK;
newdev = get_uint (cptr, 8, DEVMASK, &r);
if (r != SCPE_OK) return r;
if (newdev < VARDEV) return SCPE_ARG;
for (i = 0; infotab[i].devno != 0; i++) {
	if ((i != ord) && (newdev == infotab[i].devno))
		return SCPE_ARG;  }
infotab[ord].devno = newdev;
return SCPE_OK;
}

/* Set device number for data/control pair */

t_stat hp_setdev2 (UNIT *uptr, int32 ord)
{
int32 i, olddev;
t_stat r;

olddev = infotab[ord].devno;
if ((r = hp_setdev (uptr, ord)) != SCPE_OK) return r;
if (infotab[ord].devno == DEVMASK) {
	infotab[ord].devno = olddev;
	return SCPE_ARG;  }
for (i = 0; infotab[i].devno != 0; i++) {
	if ((i != (ord + 1)) && 
	    ((infotab[ord].devno + 1) == infotab[i].devno)) {
		infotab[ord].devno = olddev;
		return SCPE_ARG;  }  }
infotab[ord + 1].devno = infotab[ord].devno + 1;
return SCPE_OK;
}
