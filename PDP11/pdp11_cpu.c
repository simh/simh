/* pdp11_cpu.c: PDP-11 CPU simulator

   Copyright (c) 1993-2004, Robert M Supnik

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

   cpu		PDP-11 CPU (J-11 microprocessor)

   29-Dec-03	RMS	Formalized 18b Qbus support
   21-Dec-03	RMS	Added autoconfiguration controls
   05-Jun-03	RMS	Fixed bugs in memory size table
   12-Mar-03	RMS	Added logical name support
   01-Feb-03	RMS	Changed R display to follow PSW<rs>, added SP display
   19-Jan-03	RMS	Changed mode definitions for Apple Dev Kit conflict
   05-Jan-03	RMS	Added memory size restore support
   17-Oct-02	RMS	Fixed bug in examine/deposit (found by Hans Pufal)
   08-Oct-02	RMS	Revised to build dib_tab dynamically
			Added SHOW IOSPACE
   09-Sep-02	RMS	Added KW11P support
   14-Jul-02	RMS	Fixed bug in MMR0 error status load
   03-Jun-02	RMS	Fixed relocation add overflow, added PS<15:12> = 1111
			special case logic to MFPI and removed it from MTPI
			(found by John Dundas)
   29-Apr-02	RMS	More fixes to DIV and ASH/ASHC (found by John Dundas)
   28-Apr-02	RMS	Fixed bugs in illegal instruction 000010 and in
			write-only memory pages (found by Wolfgang Helbig)
   21-Apr-02	RMS	Fixed bugs in DIV by zero, DIV overflow, TSTSET, RTS,
			ASHC -32, and red zone trap (found by John Dundas)
   04-Mar-02	RMS	Changed double operand evaluation order for M+
   23-Feb-02	RMS	Fixed bug in MAINT, CPUERR, MEMERR read
   28-Jan-02	RMS	Revised for multiple timers; fixed calc_MMR1 macros
   06-Jan-02	RMS	Revised enable/disable support
   30-Dec-01	RMS	Added old PC queue
   25-Dec-01	RMS	Cleaned up sim_inst declarations
   11-Dec-01	RMS	Moved interrupt debug code
   07-Dec-01	RMS	Revised to use new breakpoint package
   08-Nov-01	RMS	Moved I/O to external module
   26-Oct-01	RMS	Revised to use symbolic definitions for IO page
   15-Oct-01	RMS	Added debug logging
   08-Oct-01	RMS	Fixed bug in revised interrupt logic
   07-Sep-01	RMS	Revised device disable and interrupt mechanisms
   26-Aug-01	RMS	Added DZ11 support
   10-Aug-01	RMS	Removed register from declarations
   17-Jul-01	RMS	Fixed warning from VC++ 6.0
   01-Jun-01	RMS	Added DZ11 interrupts
   23-Apr-01	RMS	Added RK611 support
   05-Apr-01	RMS	Added TS11/TSV05 support
   05-Mar-01	RMS	Added clock calibration support
   11-Feb-01	RMS	Added DECtape support
   25-Jan-01	RMS	Fixed 4M memory definition (found by Eric Smith)
   14-Apr-99	RMS	Changed t_addr to unsigned
   18-Aug-98	RMS	Added CIS support
   09-May-98	RMS	Fixed bug in DIV overflow test
   19-Jan-97	RMS	Added RP/RM support
   06-Apr-96	RMS	Added dynamic memory sizing
   29-Feb-96	RMS	Added TM11 support
   17-Jul-94	RMS	Corrected updating of MMR1 if MMR0 locked

   The register state for the PDP-11 is:

   REGFILE[0:5][0]	general register set
   REGFILE[0:5][1]	alternate general register set
   STACKFILE[4]		stack pointers for kernel, supervisor, unused, user
   PC			program counter
   PSW			processor status word
    <15:14> = CM	 current processor mode
    <13:12> = PM	 previous processor mode
    <11> = RS		 register set select
    <7:5> = IPL		 interrupt priority level
    <4> = TBIT		 trace trap enable
    <3:0> = NZVC	 condition codes
   FR[0:5]		floating point accumulators
   FPS			floating point status register
   FEC			floating exception code
   FEA			floating exception address
   MMR0,1,2,3		memory management control registers
   APRFILE[0:63]	memory management relocation registers for
			 kernel, supervisor, unused, user
    <31:16> = PAR	 processor address registers
    <15:0> = PDR	 processor data registers
   PIRQ			processor interrupt request register
   CPUERR		CPU error register
   MEMERR		memory system error register
   CCR			cache control register
   MAINT		maintenance register
   HITMISS		cache status register
   SR			switch register
   DR			display register
*/
	
/* The PDP-11 has many instruction formats:

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+	double operand
   |  opcode   |   source spec   |     dest spec   |	010000:067777
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+	110000:167777

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+	register + operand
   |        opcode      | src reg|     dest spec   |	004000:004777
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+	070000:077777

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+	single operand
   |           opcode            |     dest spec   |	000100:000177
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+	000300:000377
							005000:007777
							105000:107777

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+	single register
   |                opcode                |dest reg|	000200:000207
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+	000230:000237

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+	no operand
   |                     opcode                    |	000000:000007
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+	branch
   |       opcode          |  branch displacement  |	000400:003477
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+	100000:103477

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+	EMT/TRAP
   |       opcode          |       trap code       |	104000:104777
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+	cond code operator
   |                opcode             | immediate |	000240:000277
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   An operand specifier consists of an addressing mode and a register.
   The addressing modes are:

   0	register direct		R		op = R
   1	register deferred	(R)		op = M[R]
   2	autoincrement		(R)+		op = M[R]; R = R + length
   3	autoincrement deferred	@(R)+		op = M[M[R]]; R = R + 2
   4	autodecrement		-(R)		R = R - length; op = M[R]
   5	autodecrement deferred	@-(R)		R = R - 2; op = M[M[R]]
   6	displacement		d(R)		op = M[R + disp]
   7	displacement deferred	@d(R)		op = M[M[R + disp]]

   There are eight general registers, R0-R7.  R6 is the stack pointer,
   R7 the PC.  The combination of addressing modes with R7 yields:

   27	immediate		#n		op = M[PC]; PC = PC + 2
   37	absolute		@#n		op = M[M[PC]]; PC = PC + 2
   67	relative		d(PC)		op = M[PC + disp]
   77	relative deferred	@d(PC)		op = M[M[PC + disp]]
*/

/* This routine is the instruction decode routine for the PDP-11.  It
   is called from the simulator control program to execute instructions
   in simulated memory, starting at the simulated PC.  It runs until an
   enabled exception is encountered.

   General notes:

   1. Virtual address format.  PDP-11 memory management uses the 16b
      virtual address, the type of reference (instruction or data), and
      the current mode, to construct the 22b physical address.  To
      package this conveniently, the simulator uses a 19b pseudo virtual
      address, consisting of the 16b virtual address prefixed with the
      current mode and ispace/dspace indicator.  These are precalculated
      as isenable and dsenable for ispace and dspace, respectively, and
      must be recalculated whenever MMR0, MMR3, or PSW<cm> changes.

   2. Traps and interrupts.  Variable trap_req bit-encodes all possible
      traps.  In addition, an interrupt pending bit is encoded as the
      lowest priority trap.  Traps are processed by trap_vec and trap_clear,
      which provide the vector and subordinate traps to clear, respectively.

      Array int_req[0:7] bit encodes all possible interrupts.  It is masked
      under the interrupt priority level, ipl.  If any interrupt request
      is not masked, the interrupt bit is set in trap_req.  While most
      interrupts are handled centrally, a device can supply an interrupt
      acknowledge routine.

   3. PSW handling.  The PSW is kept as components, for easier access.
      Because the PSW can be explicitly written as address 17777776,
      all instructions must update PSW before executing their last write.

   4. Adding I/O devices.  These modules must be modified:

	pdp11_defs.h	add device address and interrupt definitions
	pdp11_sys.c	add to sim_devices table entry
*/

/* Definitions */

#include "pdp11_defs.h"

#define PCQ_SIZE	64				/* must be 2**n */
#define PCQ_MASK	(PCQ_SIZE - 1)
#define PCQ_ENTRY	pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = PC
#define calc_is(md)	((md) << VA_V_MODE)
#define calc_ds(md)	(calc_is((md)) | ((MMR3 & dsmask[(md)])? VA_DS: 0))
#define calc_MMR1(val)	((MMR1)? (((val) << 8) | MMR1): (val))
#define GET_SIGN_W(v)	((v) >> 15)
#define GET_SIGN_B(v)	((v) >> 7)
#define GET_Z(v)	((v) == 0)
#define JMP_PC(x)	PCQ_ENTRY; PC = (x)
#define BRANCH_F(x)	PCQ_ENTRY; PC = (PC + (((x) + (x)) & 0377)) & 0177777
#define BRANCH_B(x)	PCQ_ENTRY; PC = (PC + (((x) + (x)) | 0177400)) & 0177777
#define last_pa		(cpu_unit.u4)			/* auto save/rest */
#define UNIT_V_18B	(UNIT_V_UF)			/* force 18b addr */
#define UNIT_V_UBM	(UNIT_V_UF + 1)			/* bus map present */
#define UNIT_V_RH11	(UNIT_V_UF + 2)			/* RH11 Massbus */
#define UNIT_V_CIS	(UNIT_V_UF + 3)			/* CIS present */
#define UNIT_V_MSIZE	(UNIT_V_UF + 4)			/* dummy */
#define UNIT_18B	(1u << UNIT_V_18B)
#define UNIT_UBM	(1u << UNIT_V_UBM)
#define UNIT_RH11	(1u << UNIT_V_RH11)
#define UNIT_CIS	(1u << UNIT_V_CIS)
#define UNIT_MSIZE	(1u << UNIT_V_MSIZE)
#define UNIT_MAP	(UNIT_18B | UNIT_UBM | UNIT_RH11)

/* Global state */

extern FILE *sim_log;

uint16 *M = NULL;					/* memory */
int32 REGFILE[6][2] = { 0 };				/* R0-R5, two sets */
int32 STACKFILE[4] = { 0 };				/* SP, four modes */
int32 saved_PC = 0;					/* program counter */
int32 R[8] = { 0 };					/* working registers */
int32 PSW = 0;						/* PSW */
  int32 cm = 0;						/*   current mode */
  int32 pm = 0;						/*   previous mode */
  int32 rs = 0;						/*   register set */
  int32 ipl = 0;					/*   int pri level */
  int32 tbit = 0;					/*   trace flag */
  int32 N = 0, Z = 0, V = 0, C = 0;			/*   condition codes */
int32 wait_state = 0;					/* wait state */
int32 trap_req = 0;					/* trap requests */
int32 int_req[IPL_HLVL] = { 0 };			/* interrupt requests */
int32 PIRQ = 0;						/* programmed int req */
int32 SR = 0;						/* switch register */
int32 DR = 0;						/* display register */
fpac_t FR[6] = { 0 };					/* fp accumulators */
int32 FPS = 0;						/* fp status */
int32 FEC = 0;						/* fp exception code */
int32 FEA = 0;						/* fp exception addr */
int32 APRFILE[64] = { 0 };				/* PARs/PDRs */
int32 MMR0 = 0;						/* MMR0 - status */
int32 MMR1 = 0;						/* MMR1 - R+/-R */
int32 MMR2 = 0;						/* MMR2 - saved PC */
int32 MMR3 = 0;						/* MMR3 - 22b status */
int32 ub_map[UBM_LNT_LW] = { 0 };			/* Unibus map array */
int32 cpu_bme = 0;					/* bus map enable */
int32 cpu_18b = 0;					/* 18b CPU config'd */
int32 cpu_ubm = 0;					/* bus map config'd */
int32 cpu_rh11 = 0;					/* RH11 config'd */
int32 cpu_astop = 0;					/* address stop */
int32 isenable = 0, dsenable = 0;			/* i, d space flags */
int32 CPUERR = 0;					/* CPU error reg */
int32 MEMERR = 0;					/* memory error reg */
int32 CCR = 0;						/* cache control reg */
int32 HITMISS = 0;					/* hit/miss reg */
int32 MAINT = MAINT_Q | MAINT_NOFPA | MAINT_KDJ | MAINT_BPOK;	/* maint reg */
int32 stop_trap = 1;					/* stop on trap */
int32 stop_vecabort = 1;				/* stop on vec abort */
int32 stop_spabort = 1;					/* stop on SP abort */
int32 wait_enable = 0;					/* wait state enable */
int32 cpu_log = 0;					/* logging */
int32 autcon_enb = 1;					/* autoconfig enable */
uint16 pcq[PCQ_SIZE] = { 0 };				/* PC queue */
int32 pcq_p = 0;					/* PC queue ptr */
REG *pcq_r = NULL;					/* PC queue reg ptr */
int32 dev_enb = (-1) & ~INT_TS;				/* dev enables */
jmp_buf save_env;					/* abort handler */
int32 dsmask[4] = { MMR3_KDS, MMR3_SDS, 0, MMR3_UDS };	/* dspace enables */

extern int32 sim_interval;
extern UNIT *sim_clock_queue;
extern UNIT clk_unit, pclk_unit;
extern int32 sim_int_char;
extern int32 sim_brk_types, sim_brk_dflt, sim_brk_summ;	/* breakpoint info */
extern DEVICE *sim_devices[];

/* Function declarations */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_bus (UNIT *uptr, int32 val, char *cptr, void *desc);
int32 GeteaB (int32 spec);
int32 GeteaW (int32 spec);
int32 relocR (int32 addr);
int32 relocW (int32 addr);
int32 ReadW (int32 addr);
int32 ReadB (int32 addr);
int32 ReadMW (int32 addr);
int32 ReadMB (int32 addr);
void WriteW (int32 data, int32 addr);
void WriteB (int32 data, int32 addr);
void PWriteW (int32 data, int32 addr);
void PWriteB (int32 data, int32 addr);
t_stat CPU_rd (int32 *data, int32 addr, int32 access);
t_stat CPU_wr (int32 data, int32 addr, int32 access);
t_stat APR_rd (int32 *data, int32 addr, int32 access);
t_stat APR_wr (int32 data, int32 addr, int32 access);
t_stat SR_MMR012_rd (int32 *data, int32 addr, int32 access);
t_stat SR_MMR012_wr (int32 data, int32 addr, int32 access);
t_stat MMR3_rd (int32 *data, int32 addr, int32 access);
t_stat MMR3_wr (int32 data, int32 addr, int32 access);
t_stat ubm_rd (int32 *data, int32 addr, int32 access);
t_stat ubm_wr (int32 data, int32 addr, int32 access);
void set_r_display (int32 rs, int32 cm);

extern t_stat build_dib_tab (int32 ubm);
extern t_stat show_iospace (FILE *st, UNIT *uptr, int32 val, void *desc);
extern t_stat set_autocon (UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat show_autocon (FILE *st, UNIT *uptr, int32 val, void *desc);
extern t_stat iopageR (int32 *data, uint32 addr, int32 access);
extern t_stat iopageW (int32 data, uint32 addr, int32 access);
extern int32 calc_ints (int32 nipl, int32 trq);
extern int32 get_vector (int32 nipl);

/* Trap data structures */

int32 trap_vec[TRAP_V_MAX] = {				/* trap req to vector */
	VEC_RED, VEC_ODD, VEC_MME, VEC_NXM,
	VEC_PAR, VEC_PRV, VEC_ILL, VEC_BPT,
	VEC_IOT, VEC_EMT, VEC_TRAP, VEC_TRC,
	VEC_YEL, VEC_PWRFL, VEC_FPE };

int32 trap_clear[TRAP_V_MAX] = {			/* trap clears */
	TRAP_RED+TRAP_PAR+TRAP_YEL+TRAP_TRC+TRAP_ODD+TRAP_NXM,
	TRAP_ODD+TRAP_PAR+TRAP_YEL+TRAP_TRC,
	TRAP_MME+TRAP_PAR+TRAP_YEL+TRAP_TRC,
	TRAP_NXM+TRAP_PAR+TRAP_YEL+TRAP_TRC,
	TRAP_PAR+TRAP_TRC, TRAP_PRV+TRAP_TRC,
	TRAP_ILL+TRAP_TRC, TRAP_BPT+TRAP_TRC,
	TRAP_IOT+TRAP_TRC, TRAP_EMT+TRAP_TRC,
	TRAP_TRAP+TRAP_TRC, TRAP_TRC,
	TRAP_YEL, TRAP_PWRFL, TRAP_FPE };

/* Fixed I/O address table entries */

DIB cpu0_dib = { IOBA_CPU, IOLN_CPU, &CPU_rd, &CPU_wr, 0 };
DIB cpu1_dib = { IOBA_APR, IOLN_APR, &APR_rd, &APR_wr, 0 };
DIB cpu2_dib = { IOBA_APR1, IOLN_APR1, &APR_rd, &APR_wr, 0 };
DIB cpu3_dib = { IOBA_SRMM, IOLN_SRMM, &SR_MMR012_rd, &SR_MMR012_wr, 0 };
DIB cpu4_dib = { IOBA_MMR3, IOLN_MMR3, &MMR3_rd, &MMR3_wr, 0 };
DIB ubm_dib = { IOBA_UBM, IOLN_UBM, &ubm_rd, &ubm_wr, 0 };

/* CPU data structures

   cpu_dev	CPU device descriptor
   cpu_unit	CPU unit descriptor
   cpu_reg	CPU register list
   cpu_mod	CPU modifier list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, INIMEMSIZE) };

REG cpu_reg[] = {
	{ ORDATA (PC, saved_PC, 16) },
	{ ORDATA (R0, REGFILE[0][0], 16) },
	{ ORDATA (R1, REGFILE[1][0], 16) },
	{ ORDATA (R2, REGFILE[2][0], 16) },
	{ ORDATA (R3, REGFILE[3][0], 16) },
	{ ORDATA (R4, REGFILE[4][0], 16) },
	{ ORDATA (R5, REGFILE[5][0], 16) },
	{ ORDATA (SP, STACKFILE[MD_KER], 16) },
	{ ORDATA (R00, REGFILE[0][0], 16) },
	{ ORDATA (R01, REGFILE[1][0], 16) },
	{ ORDATA (R02, REGFILE[2][0], 16) },
	{ ORDATA (R03, REGFILE[3][0], 16) },
	{ ORDATA (R04, REGFILE[4][0], 16) },
	{ ORDATA (R05, REGFILE[5][0], 16) },
	{ ORDATA (R10, REGFILE[0][1], 16) },
	{ ORDATA (R11, REGFILE[1][1], 16) },
	{ ORDATA (R12, REGFILE[2][1], 16) },
	{ ORDATA (R13, REGFILE[3][1], 16) },
	{ ORDATA (R14, REGFILE[4][1], 16) },
	{ ORDATA (R15, REGFILE[5][1], 16) },
	{ ORDATA (KSP, STACKFILE[MD_KER], 16) },
	{ ORDATA (SSP, STACKFILE[MD_SUP], 16) },
	{ ORDATA (USP, STACKFILE[MD_USR], 16) },
	{ ORDATA (PSW, PSW, 16) },
	{ GRDATA (CM, PSW, 8, 2, PSW_V_CM) },
	{ GRDATA (PM, PSW, 8, 2, PSW_V_PM) },
	{ FLDATA (RS, PSW, PSW_V_RS) },
	{ GRDATA (IPL, PSW, 8, 3, PSW_V_IPL) },
	{ FLDATA (T, PSW, PSW_V_TBIT) },
	{ FLDATA (N, PSW, PSW_V_N) },
	{ FLDATA (Z, PSW, PSW_V_Z) },
	{ FLDATA (V, PSW, PSW_V_V) },
	{ FLDATA (C, PSW, PSW_V_C) },
	{ ORDATA (SR, SR, 16) },
	{ ORDATA (DR, DR, 16) },
	{ ORDATA (MEMERR, MEMERR, 16) },
	{ ORDATA (CCR, CCR, 16) },
	{ ORDATA (MAINT, MAINT, 16) },
	{ ORDATA (HITMISS, HITMISS, 16) },
	{ ORDATA (CPUERR, CPUERR, 16) },
	{ BRDATA (IREQ, int_req, 8, 32, IPL_HLVL), REG_RO },
	{ ORDATA (TRAPS, trap_req, TRAP_V_MAX) },
	{ ORDATA (PIRQ, PIRQ, 16) },
	{ FLDATA (WAIT, wait_state, 0) },
	{ FLDATA (WAIT_ENABLE, wait_enable, 0) },
	{ ORDATA (STOP_TRAPS, stop_trap, TRAP_V_MAX) },
	{ FLDATA (STOP_VECA, stop_vecabort, 0) },
	{ FLDATA (STOP_SPA, stop_spabort, 0) },
	{ HRDATA (DBGLOG, cpu_log, 16), REG_HIDDEN },
	{ ORDATA (FAC0H, FR[0].h, 32) },
	{ ORDATA (FAC0L, FR[0].l, 32) },
	{ ORDATA (FAC1H, FR[1].h, 32) },
	{ ORDATA (FAC1L, FR[1].l, 32) },
	{ ORDATA (FAC2H, FR[2].h, 32) },
	{ ORDATA (FAC2L, FR[2].l, 32) },
	{ ORDATA (FAC3H, FR[3].h, 32) },
	{ ORDATA (FAC3L, FR[3].l, 32) },
	{ ORDATA (FAC4H, FR[4].h, 32) },
	{ ORDATA (FAC4L, FR[4].l, 32) },
	{ ORDATA (FAC5H, FR[5].h, 32) },
	{ ORDATA (FAC5L, FR[5].l, 32) },
	{ ORDATA (FPS, FPS, 16) },
	{ ORDATA (FEA, FEA, 16) },
	{ ORDATA (FEC, FEC, 4) },
	{ ORDATA (MMR0, MMR0, 16) },
	{ ORDATA (MMR1, MMR1, 16) },
	{ ORDATA (MMR2, MMR2, 16) },
	{ ORDATA (MMR3, MMR3, 16) },
	{ GRDATA (KIPAR0, APRFILE[000], 8, 16, 16) },
	{ GRDATA (KIPDR0, APRFILE[000], 8, 16, 0) },
	{ GRDATA (KIPAR1, APRFILE[001], 8, 16, 16) },
	{ GRDATA (KIPDR1, APRFILE[001], 8, 16, 0) },
	{ GRDATA (KIPAR2, APRFILE[002], 8, 16, 16) },
	{ GRDATA (KIPDR2, APRFILE[002], 8, 16, 0) },
	{ GRDATA (KIPAR3, APRFILE[003], 8, 16, 16) },
	{ GRDATA (KIPDR3, APRFILE[003], 8, 16, 0) },
	{ GRDATA (KIPAR4, APRFILE[004], 8, 16, 16) },
	{ GRDATA (KIPDR4, APRFILE[004], 8, 16, 0) },
	{ GRDATA (KIPAR5, APRFILE[005], 8, 16, 16) },
	{ GRDATA (KIPDR5, APRFILE[005], 8, 16, 0) },
	{ GRDATA (KIPAR6, APRFILE[006], 8, 16, 16) },
	{ GRDATA (KIPDR6, APRFILE[006], 8, 16, 0) },
	{ GRDATA (KIPAR7, APRFILE[007], 8, 16, 16) },
	{ GRDATA (KIPDR7, APRFILE[007], 8, 16, 0) },
	{ GRDATA (KDPAR0, APRFILE[010], 8, 16, 16) },
	{ GRDATA (KDPDR0, APRFILE[010], 8, 16, 0) },
	{ GRDATA (KDPAR1, APRFILE[011], 8, 16, 16) },
	{ GRDATA (KDPDR1, APRFILE[011], 8, 16, 0) },
	{ GRDATA (KDPAR2, APRFILE[012], 8, 16, 16) },
	{ GRDATA (KDPDR2, APRFILE[012], 8, 16, 0) },
	{ GRDATA (KDPAR3, APRFILE[013], 8, 16, 16) },
	{ GRDATA (KDPDR3, APRFILE[013], 8, 16, 0) },
	{ GRDATA (KDPAR4, APRFILE[014], 8, 16, 16) },
	{ GRDATA (KDPDR4, APRFILE[014], 8, 16, 0) },
	{ GRDATA (KDPAR5, APRFILE[015], 8, 16, 16) },
	{ GRDATA (KDPDR5, APRFILE[015], 8, 16, 0) },
	{ GRDATA (KDPAR6, APRFILE[016], 8, 16, 16) },
	{ GRDATA (KDPDR6, APRFILE[016], 8, 16, 0) },
	{ GRDATA (KDPAR7, APRFILE[017], 8, 16, 16) },
	{ GRDATA (KDPDR7, APRFILE[017], 8, 16, 0) },
	{ GRDATA (SIPAR0, APRFILE[020], 8, 16, 16) },
	{ GRDATA (SIPDR0, APRFILE[020], 8, 16, 0) },
	{ GRDATA (SIPAR1, APRFILE[021], 8, 16, 16) },
	{ GRDATA (SIPDR1, APRFILE[021], 8, 16, 0) },
	{ GRDATA (SIPAR2, APRFILE[022], 8, 16, 16) },
	{ GRDATA (SIPDR2, APRFILE[022], 8, 16, 0) },
	{ GRDATA (SIPAR3, APRFILE[023], 8, 16, 16) },
	{ GRDATA (SIPDR3, APRFILE[023], 8, 16, 0) },
	{ GRDATA (SIPAR4, APRFILE[024], 8, 16, 16) },
	{ GRDATA (SIPDR4, APRFILE[024], 8, 16, 0) },
	{ GRDATA (SIPAR5, APRFILE[025], 8, 16, 16) },
	{ GRDATA (SIPDR5, APRFILE[025], 8, 16, 0) },
	{ GRDATA (SIPAR6, APRFILE[026], 8, 16, 16) },
	{ GRDATA (SIPDR6, APRFILE[026], 8, 16, 0) },
	{ GRDATA (SIPAR7, APRFILE[027], 8, 16, 16) },
	{ GRDATA (SIPDR7, APRFILE[027], 8, 16, 0) },
	{ GRDATA (SDPAR0, APRFILE[030], 8, 16, 16) },
	{ GRDATA (SDPDR0, APRFILE[030], 8, 16, 0) },
	{ GRDATA (SDPAR1, APRFILE[031], 8, 16, 16) },
	{ GRDATA (SDPDR1, APRFILE[031], 8, 16, 0) },
	{ GRDATA (SDPAR2, APRFILE[032], 8, 16, 16) },
	{ GRDATA (SDPDR2, APRFILE[032], 8, 16, 0) },
	{ GRDATA (SDPAR3, APRFILE[033], 8, 16, 16) },
	{ GRDATA (SDPDR3, APRFILE[033], 8, 16, 0) },
	{ GRDATA (SDPAR4, APRFILE[034], 8, 16, 16) },
	{ GRDATA (SDPDR4, APRFILE[034], 8, 16, 0) },
	{ GRDATA (SDPAR5, APRFILE[035], 8, 16, 16) },
	{ GRDATA (SDPDR5, APRFILE[035], 8, 16, 0) },
	{ GRDATA (SDPAR6, APRFILE[036], 8, 16, 16) },
	{ GRDATA (SDPDR6, APRFILE[036], 8, 16, 0) },
	{ GRDATA (SDPAR7, APRFILE[037], 8, 16, 16) },
	{ GRDATA (SDPDR7, APRFILE[037], 8, 16, 0) },
	{ GRDATA (UIPAR0, APRFILE[060], 8, 16, 16) },
	{ GRDATA (UIPDR0, APRFILE[060], 8, 16, 0) },
	{ GRDATA (UIPAR1, APRFILE[061], 8, 16, 16) },
	{ GRDATA (UIPDR1, APRFILE[061], 8, 16, 0) },
	{ GRDATA (UIPAR2, APRFILE[062], 8, 16, 16) },
	{ GRDATA (UIPDR2, APRFILE[062], 8, 16, 0) },
	{ GRDATA (UIPAR3, APRFILE[063], 8, 16, 16) },
	{ GRDATA (UIPDR3, APRFILE[063], 8, 16, 0) },
	{ GRDATA (UIPAR4, APRFILE[064], 8, 16, 16) },
	{ GRDATA (UIPDR4, APRFILE[064], 8, 16, 0) },
	{ GRDATA (UIPAR5, APRFILE[065], 8, 16, 16) },
	{ GRDATA (UIPDR5, APRFILE[065], 8, 16, 0) },
	{ GRDATA (UIPAR6, APRFILE[066], 8, 16, 16) },
	{ GRDATA (UIPDR6, APRFILE[066], 8, 16, 0) },
	{ GRDATA (UIPAR7, APRFILE[067], 8, 16, 16) },
	{ GRDATA (UIPDR7, APRFILE[067], 8, 16, 0) },
	{ GRDATA (UDPAR0, APRFILE[070], 8, 16, 16) },
	{ GRDATA (UDPDR0, APRFILE[070], 8, 16, 0) },
	{ GRDATA (UDPAR1, APRFILE[071], 8, 16, 16) },
	{ GRDATA (UDPDR1, APRFILE[071], 8, 16, 0) },
	{ GRDATA (UDPAR2, APRFILE[072], 8, 16, 16) },
	{ GRDATA (UDPDR2, APRFILE[072], 8, 16, 0) },
	{ GRDATA (UDPAR3, APRFILE[073], 8, 16, 16) },
	{ GRDATA (UDPDR3, APRFILE[073], 8, 16, 0) },
	{ GRDATA (UDPAR4, APRFILE[074], 8, 16, 16) },
	{ GRDATA (UDPDR4, APRFILE[074], 8, 16, 0) },
	{ GRDATA (UDPAR5, APRFILE[075], 8, 16, 16) },
	{ GRDATA (UDPDR5, APRFILE[075], 8, 16, 0) },
	{ GRDATA (UDPAR6, APRFILE[076], 8, 16, 16) },
	{ GRDATA (UDPDR6, APRFILE[076], 8, 16, 0) },
	{ GRDATA (UDPAR7, APRFILE[077], 8, 16, 16) },
	{ GRDATA (UDPDR7, APRFILE[077], 8, 16, 0) },
	{ BRDATA (UBMAP, ub_map, 8, 22, UBM_LNT_LW) },
	{ FLDATA (AUTOCON, autcon_enb, 0), REG_HRO },
	{ BRDATA (PCQ, pcq, 8, 16, PCQ_SIZE), REG_RO+REG_CIRC },
	{ ORDATA (PCQP, pcq_p, 6), REG_HRO },
	{ ORDATA (WRU, sim_int_char, 8) },
	{ ORDATA (DEVENB, dev_enb, 32), REG_HRO },
	{ NULL}  };

MTAB cpu_mod[] = {
	{ UNIT_MAP, UNIT_18B, "18b Unibus", "U18", &cpu_set_bus },
	{ UNIT_MAP, UNIT_UBM, "22b Unibus + RH70", "URH70", &cpu_set_bus },
	{ UNIT_MAP, UNIT_UBM + UNIT_RH11, "22b Unibus + RH11", "URH11", &cpu_set_bus },
	{ UNIT_MAP, 0, "22b Qbus", "Q22", &cpu_set_bus },
	{ UNIT_CIS, UNIT_CIS, "CIS", "CIS", NULL },
	{ UNIT_CIS, 0, "no CIS", "NOCIS", NULL },
	{ UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size},
	{ UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size},
	{ UNIT_MSIZE, 49152, NULL, "48K", &cpu_set_size},
	{ UNIT_MSIZE, 65536, NULL, "64K", &cpu_set_size},
	{ UNIT_MSIZE, 98304, NULL, "96K", &cpu_set_size},
	{ UNIT_MSIZE, 131072, NULL, "128K", &cpu_set_size},
	{ UNIT_MSIZE, 196608, NULL, "192K", &cpu_set_size},
	{ UNIT_MSIZE, 262144, NULL, "256K", &cpu_set_size},
	{ UNIT_MSIZE, 393216, NULL, "384K", &cpu_set_size},
	{ UNIT_MSIZE, 524288, NULL, "512K", &cpu_set_size},
	{ UNIT_MSIZE, 786432, NULL, "768K", &cpu_set_size},
	{ UNIT_MSIZE, 1048576, NULL, "1024K", &cpu_set_size},
	{ UNIT_MSIZE, 2097152, NULL, "2048K", &cpu_set_size},
	{ UNIT_MSIZE, 3145728, NULL, "3072K", &cpu_set_size},
	{ UNIT_MSIZE, 4186112, NULL, "4096K", &cpu_set_size},
	{ UNIT_MSIZE, 1048576, NULL, "1M", &cpu_set_size},
	{ UNIT_MSIZE, 2097152, NULL, "2M", &cpu_set_size},
	{ UNIT_MSIZE, 3145728, NULL, "3M", &cpu_set_size},
	{ UNIT_MSIZE, 4186112, NULL, "4M", &cpu_set_size},
	{ MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "IOSPACE", NULL,
	  NULL, &show_iospace },
	{ MTAB_XTD|MTAB_VDV, 1, "AUTOCONFIG", "AUTOCONFIG",
	  &set_autocon, &show_autocon },
	{ MTAB_XTD|MTAB_VDV, 0, NULL, "NOAUTOCONFIG",
	  &set_autocon, NULL },
	{ 0 }  };

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 8, 22, 2, 8, 16,
	&cpu_ex, &cpu_dep, &cpu_reset,
	NULL, NULL, NULL,
	NULL, DEV_DYNM, &cpu_set_size };

t_stat sim_instr (void)
{
int abortval, i;
volatile int32 trapea;					/* used by setjmp */
t_stat reason;
void fp11 (int32 IR);
void cis11 (int32 IR);

/* Restore register state

	1. PSW components
	2. Active register file based on PSW<rs>
	3. Active stack pointer based on PSW<cm>
	4. Memory management control flags
	5. Interrupt system
*/

cpu_18b = cpu_unit.flags & UNIT_18B;			/* export cnf flgs */
cpu_ubm = cpu_unit.flags & UNIT_UBM;
cpu_rh11 = cpu_unit.flags & UNIT_RH11;
cpu_bme = (MMR3 & MMR3_BME) && cpu_ubm;
reason = build_dib_tab (cpu_ubm);			/* build, chk dib_tab */
if (reason != SCPE_OK) return reason;
cm = (PSW >> PSW_V_CM) & 03;				/* call calc_is,ds */
pm = (PSW >> PSW_V_PM) & 03;
rs = (PSW >> PSW_V_RS) & 01;
ipl = (PSW >> PSW_V_IPL) & 07;				/* call calc_ints */
tbit = (PSW >> PSW_V_TBIT) & 01;
N = (PSW >> PSW_V_N) & 01;
Z = (PSW >> PSW_V_Z) & 01;
V = (PSW >> PSW_V_V) & 01;
C = (PSW >> PSW_V_C) & 01;

for (i = 0; i < 6; i++) R[i] = REGFILE[i][rs];
SP = STACKFILE[cm];
PC = saved_PC;

isenable = calc_is (cm);
dsenable = calc_ds (cm);

CPU_wr (PIRQ, 017777772, WRITE);			/* rewrite PIRQ */
trap_req = calc_ints (ipl, trap_req);			/* upd int req */
trapea = 0;
reason = 0;
sim_rtcn_init (clk_unit.wait, TMR_CLK);			/* init line clock */
sim_rtcn_init (pclk_unit.wait, TMR_PCLK);		/* init prog clock */

/* Abort handling

   If an abort occurs in memory management or memory access, the lower
   level routine executes a longjmp to this area OUTSIDE the main
   simulation loop.  The longjmp specifies a trap mask which is OR'd
   into the trap_req register.  Simulation then resumes at the fetch
   phase, and the trap is sprung.

   Aborts which occur within a trap sequence (trapea != 0) require
   special handling.  If the abort occured on the stack pushes, and
   the mode (encoded in trapea) is kernel, an "emergency" kernel
   stack is created at 4, and a red zone stack trap taken.

   All variables used in setjmp processing, or assumed to be valid
   after setjmp, must be volatile or global.
*/

abortval = setjmp (save_env);				/* set abort hdlr */
if (abortval != 0) {
	trap_req = trap_req | abortval;			/* or in trap flag */
	if ((trapea > 0) && (stop_vecabort)) reason = STOP_VECABORT;
	if ((trapea < 0) && (stop_spabort)) reason = STOP_SPABORT;
	if (trapea == ~MD_KER) {			/* kernel stk abort? */
	    setTRAP (TRAP_RED);
	    setCPUERR (CPUE_RED);
	    STACKFILE[MD_KER] = 4;
	    if (cm == MD_KER) SP = 4;  }  }

/* Main instruction fetch/decode loop

   Check for traps or interrupts.  If trap, locate the vector and check
   for stop condition.  If interrupt, locate the vector.
*/ 

while (reason == 0)  {

int32 IR, srcspec, srcreg, dstspec, dstreg;
int32 src, src2, dst, ea;
int32 i, t, sign, oldrs, trapnum;

if (cpu_astop) {
	cpu_astop = 0;
	reason = SCPE_STOP;
	break;  }

if (sim_interval <= 0) {				/* intv cnt expired? */
	reason = sim_process_event ();			/* process events */
	trap_req = calc_ints (ipl, trap_req);		/* recalc int req */
	continue;
	}						/* end if sim_interval */

if (trap_req) {						/* check traps, ints */
	trapea = 0;					/* assume srch fails */
	if (t = trap_req & TRAP_ALL) {			/* if a trap */
	    for (trapnum = 0; trapnum < TRAP_V_MAX; trapnum++) {
		if ((t >> trapnum) & 1) {		/* trap set? */
		    trapea = trap_vec[trapnum];		/* get vec, clr */
		    trap_req = trap_req & ~trap_clear[trapnum];
		    if ((stop_trap >> trapnum) & 1)	/* stop on trap? */
			reason = trapnum + 1;
		    break;
		    }					/* end if t & 1 */
		}					/* end for */
	    }						/* end if t */
	else {
	    trapea = get_vector (ipl);			/* get int vector */
	    trapnum = TRAP_V_MAX;			/* defang stk trap */
	    }						/* end else t*/
	if (trapea == 0) {				/* nothing to do? */
	    trap_req = calc_ints (ipl, 0);		/* recalculate */
	    continue;					/* back to fetch */
	    }						/* end if trapea */

/* Process a trap or interrupt

   1. Exit wait state
   2. Save the current SP and PSW
   3. Read the new PC, new PSW from trapea, kernel data space
   4. Get the mode and stack selected by the new PSW
   5. Push the old PC and PSW on the new stack
   6. Update SP, PSW, and PC
   7. If not stack overflow, check for stack overflow
*/

	wait_state = 0;					/* exit wait state */
	STACKFILE[cm] = SP;
	PSW = (cm << PSW_V_CM) | (pm << PSW_V_PM) | (rs << PSW_V_RS) |
		(ipl << PSW_V_IPL) | (tbit << PSW_V_TBIT) |
		(N << PSW_V_N) | (Z << PSW_V_Z) |
		(V << PSW_V_V) | (C << PSW_V_C);
	oldrs = rs;
	src = ReadW (trapea | calc_ds (MD_KER));
	src2 = ReadW ((trapea + 2) | calc_ds (MD_KER));
	t = (src2 >> PSW_V_CM) & 03;
	trapea = ~t;					/* flag pushes */
	WriteW (PSW, ((STACKFILE[t] - 2) & 0177777) | calc_ds (t));
	WriteW (PC, ((STACKFILE[t] - 4) & 0177777) | calc_ds (t));
	trapea = 0;					/* clear trap flag */
	pm = cm;
	cm = t;						/* call calc_is,ds */
	rs = (src2 >> PSW_V_RS) & 01;
	ipl = (src2 >> PSW_V_IPL) & 07;			/* call calc_ints */
	tbit = (src2 >> PSW_V_TBIT) & 01;
	N = (src2 >> PSW_V_N) & 01;
	Z = (src2 >> PSW_V_Z) & 01;
	V = (src2 >> PSW_V_V) & 01;
	C = (src2 >> PSW_V_C) & 01;
	if (rs != oldrs) {				/* if rs chg, swap */
	    for (i = 0; i < 6; i++) {
		REGFILE[i][oldrs] = R[i];
		R[i] = REGFILE[i][rs];  }  }
	SP = (STACKFILE[cm] - 4) & 0177777;		/* update SP, PC */
	JMP_PC (src);
	isenable = calc_is (cm);
	dsenable = calc_ds (cm);
	trap_req = calc_ints (ipl, trap_req);
	if ((SP < STKLIM) && (cm == MD_KER) &&
	    (trapnum != TRAP_V_RED) && (trapnum != TRAP_V_YEL)) {
	    setTRAP (TRAP_YEL);
	    setCPUERR (CPUE_YEL);  }
	continue;  }					/* end if traps */

/* Fetch and decode next instruction */

if (tbit) setTRAP (TRAP_TRC);
if (wait_state) {					/* wait state? */
	if (sim_clock_queue != NULL) sim_interval = 0;	/* force check */
	else reason = STOP_WAIT;
	continue;  }

if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) {	/* breakpoint? */
	reason = STOP_IBKPT;				/* stop simulation */
	continue;  }

if (update_MM) {					/* if mm not frozen */
	MMR1 = 0;
	MMR2 = PC;  }
IR = ReadW (PC | isenable);				/* fetch instruction */
PC = (PC + 2) & 0177777;				/* incr PC, mod 65k */
sim_interval = sim_interval - 1;
srcspec = (IR >> 6) & 077;				/* src, dst specs */
dstspec = IR & 077;
srcreg = (srcspec <= 07);				/* src, dst = rmode? */
dstreg = (dstspec <= 07);
switch ((IR >> 12) & 017) {				/* decode IR<15:12> */

/* Opcode 0: no operands, specials, branches, JSR, SOPs */

case 000:
	switch ((IR >> 6) & 077) {			/* decode IR<11:6> */
	case 000:					/* no operand */
	    if (IR >= 000010) {				/* 000010 - 000077 */
		setTRAP (TRAP_ILL);			/* illegal */
		break;  }
	    switch (IR) {				/* decode IR<2:0> */
	    case 0:					/* HALT */
	    	if ((cm == MD_KER) && ((MAINT & MAINT_HTRAP) == 0))
		    reason = STOP_HALT;
		else {
		    setTRAP (TRAP_PRV);
		    setCPUERR (CPUE_HALT);  }
		break;
	    case 1:					/* WAIT */
		if ((cm == MD_KER) && wait_enable) wait_state = 1;
		break;
	    case 3:					/* BPT */
		setTRAP (TRAP_BPT);
		break;
	    case 4:					/* IOT */
		setTRAP (TRAP_IOT);
		break;
	    case 5:					/* RESET */
		if (cm == MD_KER) {
		    reset_all (1);
		    PIRQ = 0;
		    for (i = 0; i < IPL_HLVL; i++) int_req[i] = 0;
		    MMR0 = MMR0 & ~(MMR0_MME | MMR0_FREEZE);
		    MMR3 = 0;
		    trap_req = trap_req & ~TRAP_INT;
		    dsenable = calc_ds (cm);  }
		break;

/* Opcode 0: specials, continued */

	    case 2:					/* RTI */
	    case 6:					/* RTT */
	    	src = ReadW (SP | dsenable);
	    	src2 = ReadW (((SP + 2) & 0177777) | dsenable);
		STACKFILE[cm] = SP = (SP + 4) & 0177777;
		oldrs = rs;
		if (cm == MD_KER) {
		    cm = (src2 >> PSW_V_CM) & 03;
		    pm = (src2 >> PSW_V_PM) & 03;
		    rs = (src2 >> PSW_V_RS) & 01;
		    ipl = (src2 >> PSW_V_IPL) & 07;  }
		else {
		    cm = cm | ((src2 >> PSW_V_CM) & 03);
		    pm = pm | ((src2 >> PSW_V_PM) & 03);
		    rs = rs | ((src2 >> PSW_V_RS) & 01);  }
		tbit = (src2 >> PSW_V_TBIT) & 01;
		N = (src2 >> PSW_V_N) & 01;
		Z = (src2 >> PSW_V_Z) & 01;
		V = (src2 >> PSW_V_V) & 01;
		C = (src2 >> PSW_V_C) & 01;
		trap_req = calc_ints (ipl, trap_req);
		isenable = calc_is (cm);
		dsenable = calc_ds (cm);
		if (rs != oldrs) {
		    for (i = 0; i < 6; i++) {
		    	REGFILE[i][oldrs] = R[i];
		    	R[i] = REGFILE[i][rs];  }  }
		SP = STACKFILE[cm];
		JMP_PC (src);
		if ((IR == 000002) && tbit) setTRAP (TRAP_TRC);
		break;
	    case 7:					/* MFPT */
		R[0] = 5;				/* report J-11 */
		break;	}				/* end switch no ops */
	    break;					/* end case no ops */

/* Opcode 0: specials, continued */

	case 001:					/* JMP */
	    if (dstreg) setTRAP (TRAP_ILL);
	    else {
		JMP_PC (GeteaW (dstspec) & 0177777);  }
	    break;					/* end JMP */
	case 002:					/* RTS et al*/
	    if (IR < 000210) {				/* RTS */
		dstspec = dstspec & 07;
		JMP_PC (R[dstspec]);
		R[dstspec] = ReadW (SP | dsenable);
		if (dstspec != 6) SP = (SP + 2) & 0177777;
		break;  } 				/* end if RTS */
	    if (IR < 000230) {
		setTRAP (TRAP_ILL);
		break;  }
	    if (IR < 000240) {				/* SPL */
		if (cm == MD_KER) ipl = IR & 07;
		trap_req = calc_ints (ipl, trap_req);
		break;  }				/* end if SPL */
	    if (IR < 000260) {				/* clear CC */
		if (IR & 010) N = 0;
		if (IR & 004) Z = 0;
		if (IR & 002) V = 0;
		if (IR & 001) C = 0;
		break;  }				/* end if clear CCs */
	    if (IR & 010) N = 1;			/* set CC */
	    if (IR & 004) Z = 1;
	    if (IR & 002) V = 1;
	    if (IR & 001) C = 1;
	    break;					/* end case RTS et al */
	case 003:					/* SWAB */
	    dst = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));
	    dst = ((dst & 0377) << 8) | ((dst >> 8) & 0377);
	    N = GET_SIGN_B (dst & 0377);
	    Z = GET_Z (dst & 0377);
	    V = C = 0;
	    if (dstreg) R[dstspec] = dst;
	    else PWriteW (dst, last_pa);
	    break;  					/* end SWAB */

/* Opcode 0: branches, JSR */

	case 004: case 005:				/* BR */
	    BRANCH_F (IR);
	    break;
	case 006: case 007:				/* BR */
	    BRANCH_B (IR);
	    break;
	case 010: case 011:				/* BNE */
	    if (Z == 0) { BRANCH_F (IR); } 
	    break;
	case 012: case 013:				/* BNE */
	    if (Z == 0) { BRANCH_B (IR); }
	    break;
	case 014: case 015:				/* BEQ */
	    if (Z) { BRANCH_F (IR); } 
	    break;
	case 016: case 017:				/* BEQ */
	    if (Z) { BRANCH_B (IR); }
	    break;
	case 020: case 021:				/* BGE */
	    if ((N ^ V) == 0) { BRANCH_F (IR); } 
	    break;
	case 022: case 023:				/* BGE */
	    if ((N ^ V) == 0) { BRANCH_B (IR); }
	    break;
	case 024: case 025:				/* BLT */
	    if (N ^ V) { BRANCH_F (IR); }
	    break;
	case 026: case 027:				/* BLT */
	    if (N ^ V) { BRANCH_B (IR); }
	    break;
	case 030: case 031:				/* BGT */
	    if ((Z | (N ^ V)) == 0) { BRANCH_F (IR); } 
	    break;
	case 032: case 033:				/* BGT */
	    if ((Z | (N ^ V)) == 0) { BRANCH_B (IR); }
	    break;
	case 034: case 035:				/* BLE */
	    if (Z | (N ^ V)) { BRANCH_F (IR); } 
	    break;
	case 036: case 037:				/* BLE */
	    if (Z | (N ^ V)) { BRANCH_B (IR); }
	    break;
	case 040: case 041: case 042: case 043:		/* JSR */
	case 044: case 045: case 046: case 047:
	    if (dstreg) setTRAP (TRAP_ILL);
	    else {
	    	srcspec = srcspec & 07;
		dst = GeteaW (dstspec);
		SP = (SP - 2) & 0177777;
		if (update_MM) MMR1 = calc_MMR1 (0366);
		WriteW (R[srcspec], SP | dsenable);
		if ((SP < STKLIM) && (cm == MD_KER)) {
		    setTRAP (TRAP_YEL);
		    setCPUERR (CPUE_YEL);  }
		R[srcspec] = PC;
		JMP_PC (dst & 0177777);  }
	    break;					/* end JSR */

/* Opcode 0: SOPs */

	case 050:					/* CLR */
	    N = V = C = 0;
	    Z = 1;
	    if (dstreg) R[dstspec] = 0;
	    else WriteW (0, GeteaW (dstspec));
	    break;
	case 051:					/* COM */
	    dst = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));
	    dst = dst ^ 0177777;
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    V = 0;
	    C = 1;
	    if (dstreg) R[dstspec] = dst;
	    else PWriteW (dst, last_pa);
	    break;
	case 052:					/* INC */
	    dst = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));
	    dst = (dst + 1) & 0177777;
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    V = (dst == 0100000);
	    if (dstreg) R[dstspec] = dst;
	    else PWriteW (dst, last_pa);
	    break;
	case 053:					/* DEC */
	    dst = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));
	    dst = (dst - 1) & 0177777;
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    V = (dst == 077777);
	    if (dstreg) R[dstspec] = dst;
	    else PWriteW (dst, last_pa);
	    break;
	case 054:					/* NEG */
	    dst = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));
	    dst = (-dst) & 0177777;
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    V = (dst == 0100000);
	    C = Z ^ 1;
	    if (dstreg) R[dstspec] = dst;
	    else PWriteW (dst, last_pa);
	    break;
	case 055:					/* ADC */
	    dst = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));
	    dst = (dst + C) & 0177777;
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    V = (C && (dst == 0100000));
	    C = C & Z;
	    if (dstreg) R[dstspec] = dst;
	    else PWriteW (dst, last_pa);
	    break;

/* Opcode 0: SOPs, continued */

	case 056:					/* SBC */
	    dst = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));
	    dst = (dst - C) & 0177777;
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    V = (C && (dst == 077777));
	    C = (C && (dst == 0177777));
	    if (dstreg) R[dstspec] = dst;
	    else PWriteW (dst, last_pa);
	    break;
	case 057:					/* TST */
	    dst = dstreg? R[dstspec]: ReadW (GeteaW (dstspec));
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    V = C = 0;
	    break;
	case 060:					/* ROR */
	    src = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));
	    dst = (src >> 1) | (C << 15);
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    C = (src & 1);
	    V = N ^ C;
	    if (dstreg) R[dstspec] = dst;
	    else PWriteW (dst, last_pa);
	    break;
	case 061:					/* ROL */
	    src = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));
	    dst = ((src << 1) | C) & 0177777;
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    C = GET_SIGN_W (src);
	    V = N ^ C;
	    if (dstreg) R[dstspec] = dst;
	    else PWriteW (dst, last_pa);
	    break;
	case 062:					/* ASR */
	    src = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));
	    dst = (src >> 1) | (src & 0100000);
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    C = (src & 1);
	    V = N ^ C;
	    if (dstreg) R[dstspec] = dst;
	    else PWriteW (dst, last_pa);
	    break;
	case 063:					/* ASL */
	    src = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));
	    dst = (src << 1) & 0177777;
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    C = GET_SIGN_W (src);
	    V = N ^ C;
	    if (dstreg) R[dstspec] = dst;
	    else PWriteW (dst, last_pa);
	    break;

/* Opcode 0: SOPS, continued

   Notes:
   - MxPI must mask GeteaW returned address to force ispace
   - MxPI must set MMR1 for SP recovery in case of fault
*/

	case 064:					/* MARK */
	    i = (PC + dstspec + dstspec) & 0177777;
	    JMP_PC (R[5]);
	    R[5] = ReadW (i | dsenable);
	    SP = (i + 2) & 0177777;
	    break;
	case 065:					/* MFPI */
	    if (dstreg) {
		if ((dstspec == 6) && (cm != pm)) dst = STACKFILE[pm];
		else dst = R[dstspec];  }
	    else {
	    	i = ((cm == pm) && (cm == MD_USR))? calc_ds (pm): calc_is (pm);
		dst = ReadW ((GeteaW (dstspec) & 0177777) | i);  }
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    V = 0;
	    SP = (SP - 2) & 0177777;
	    if (update_MM) MMR1 = calc_MMR1 (0366);
	    WriteW (dst, SP | dsenable);
	    if ((cm == MD_KER) && (SP < STKLIM)) {
		setTRAP (TRAP_YEL);
		setCPUERR (CPUE_YEL);  }
	    break;
	case 066:					/* MTPI */
	    dst = ReadW (SP | dsenable);
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    V = 0;
	    SP = (SP + 2) & 0177777;
	    if (update_MM) MMR1 = 026;
	    if (dstreg) {
		if ((dstspec == 6) && (cm != pm)) STACKFILE[pm] = dst;
		else R[dstspec] = dst;  }
	    else WriteW (dst, (GeteaW (dstspec) & 0177777) | calc_is (pm));
	    break;
	case 067:					/* SXT */
	    dst = N? 0177777: 0;
	    Z = N ^ 1;
	    V = 0;
	    if (dstreg) R[dstspec] = dst;
	    else WriteW (dst, GeteaW (dstspec));
	    break;

/* Opcode 0: SOPs, continued */

	case 070:					/* CSM */
	    if (((MMR3 & MMR3_CSM) == 0) || (cm == MD_KER))
		setTRAP (TRAP_ILL);
	    else {
	    	dst = dstreg? R[dstspec]: ReadW (GeteaW (dstspec));
		PSW = (cm << PSW_V_CM) | (pm << PSW_V_PM) |
			(rs << PSW_V_RS) | (ipl << PSW_V_IPL) | 
			(tbit << PSW_V_TBIT);
		STACKFILE[cm] = SP;
		WriteW (PSW, ((SP - 2) & 0177777) | calc_ds (MD_SUP));
		WriteW (PC, ((SP - 4) & 0177777) | calc_ds (MD_SUP));
		WriteW (dst, ((SP - 6) & 0177777) | calc_ds (MD_SUP));
		SP = (SP - 6) & 0177777;
		pm = cm;
		cm = MD_SUP;
		tbit = 0;
		isenable = calc_is (cm);
		dsenable = calc_ds (cm);
		PC = ReadW (010 | isenable);  }
	    break;
	case 072:					/* TSTSET */
	    if (dstreg) setTRAP (TRAP_ILL);
	    else {
		dst = ReadMW (GeteaW (dstspec));
		N = GET_SIGN_W (dst);
		Z = GET_Z (dst);
		V = 0;
		C = (dst & 1);
		R[0] = dst;				/* R[0] <- dst */
		PWriteW (R[0] | 1, last_pa);  }		/* dst <- R[0] | 1 */
	    break;
	case 073:					/* WRTLCK */
	    if (dstreg) setTRAP (TRAP_ILL);
	    else {
	    	N = GET_SIGN_W (R[0]);
		Z = GET_Z (R[0]);
		V = 0;
		WriteW (R[0], GeteaW (dstspec));  }
		break;
	default:
	    setTRAP (TRAP_ILL);
	    break;  }					/* end switch SOPs */
	break;						/* end case 000 */

/* Opcodes 01 - 06: double operand word instructions

   J-11 (and F-11) optimize away register source operand decoding.
   As a result, dop R,+/-(R) use the modified version of R as source.
   Most (but not all) other PDP-11's fetch the source operand before
   any destination operand decoding.

   Add: v = [sign (src) = sign (src2)] and [sign (src) != sign (result)]
   Cmp: v = [sign (src) != sign (src2)] and [sign (src2) = sign (result)]
*/

case 001:						/* MOV */
	if (srcreg && !dstreg) {			/* R,not R */
	    ea = GeteaW (dstspec);
	    dst = R[srcspec];  }
	else {
	    dst = srcreg? R[srcspec]: ReadW (GeteaW (srcspec));
	    if (!dstreg) ea = GeteaW (dstspec);  }
	N = GET_SIGN_W (dst);
	Z = GET_Z (dst);
	V = 0;
	if (dstreg) R[dstspec] = dst;
	else WriteW (dst, ea);
	break;
case 002:						/* CMP */
	if (srcreg && !dstreg) {			/* R,not R */
	    src2 = ReadW (GeteaW (dstspec));
	    src = R[srcspec];  }
	else {
	    src = srcreg? R[srcspec]: ReadW (GeteaW (srcspec));
	    src2 = dstreg? R[dstspec]: ReadW (GeteaW (dstspec));  }
	dst = (src - src2) & 0177777;
	N = GET_SIGN_W (dst);
	Z = GET_Z (dst);
	V = GET_SIGN_W ((src ^ src2) & (~src2 ^ dst));
	C = (src < src2);
	break;
case 003:						/* BIT */
	if (srcreg && !dstreg) {			/* R,not R */
	    src2 = ReadW (GeteaW (dstspec));
	    src = R[srcspec];  }
	else {
	    src = srcreg? R[srcspec]: ReadW (GeteaW (srcspec));
	    src2 = dstreg? R[dstspec]: ReadW (GeteaW (dstspec));  }
	dst = src2 & src;
	N = GET_SIGN_W (dst);
	Z = GET_Z (dst);
	V = 0;
	break;
case 004:						/* BIC */
	if (srcreg && !dstreg) {			/* R,not R */
	    src2 = ReadMW (GeteaW (dstspec));
	    src = R[srcspec];  }
	else {
	    src = srcreg? R[srcspec]: ReadW (GeteaW (srcspec));
	    src2 = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));  }
	dst = src2 & ~src;
	N = GET_SIGN_W (dst);
	Z = GET_Z (dst);
	V = 0;
	if (dstreg) R[dstspec] = dst;
	else PWriteW (dst, last_pa);
	break;
case 005:						/* BIS */
	if (srcreg && !dstreg) {			/* R,not R */
	    src2 = ReadMW (GeteaW (dstspec));
	    src = R[srcspec];  }
	else {
	    src = srcreg? R[srcspec]: ReadW (GeteaW (srcspec));
	    src2 = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));  }
	dst = src2 | src;
	N = GET_SIGN_W (dst);
	Z = GET_Z (dst);
	V = 0;
	if (dstreg) R[dstspec] = dst;
	else PWriteW (dst, last_pa);
	break;
case 006:						/* ADD */
	if (srcreg && !dstreg) {			/* R,not R */
	    src2 = ReadMW (GeteaW (dstspec));
	    src = R[srcspec];  }
	else {
	    src = srcreg? R[srcspec]: ReadW (GeteaW (srcspec));
	    src2 = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));  }
	dst = (src2 + src) & 0177777;
	N = GET_SIGN_W (dst);
	Z = GET_Z (dst);
	V = GET_SIGN_W ((~src ^ src2) & (src ^ dst));
	C = (dst < src);
	if (dstreg) R[dstspec] = dst;
	else PWriteW (dst, last_pa);
	break;

/* Opcode 07: EIS, FIS (not implemented), CIS

   Notes:
   - The code assumes that the host int length is at least 32 bits.
   - MUL carry: C is set if the (signed) result doesn't fit in 16 bits.
   - Divide has three error cases:
	1. Divide by zero.
	2. Divide largest negative number by -1.
	3. (Signed) quotient doesn't fit in 16 bits.
     Cases 1 and 2 must be tested in advance, to avoid C runtime errors.
   - ASHx left: overflow if the bits shifted out do not equal the sign
     of the result (convert shift out to 1/0, xor against sign).
   - ASHx right: if right shift sign extends, then the shift and
     conditional or of shifted -1 is redundant.  If right shift zero
     extends, then the shift and conditional or does sign extension.
*/

case 007:
	srcspec = srcspec & 07;
	switch ((IR >> 9) & 07)  {			/* decode IR<11:9> */
	case 0:						/* MUL */
	    src2 = dstreg? R[dstspec]: ReadW (GeteaW (dstspec));
	    src = R[srcspec];
	    if (GET_SIGN_W (src2)) src2 = src2 | ~077777;
	    if (GET_SIGN_W (src)) src = src | ~077777;
	    dst = src * src2;
	    R[srcspec] = (dst >> 16) & 0177777;
	    R[srcspec | 1] = dst & 0177777;
	    N = (dst < 0);
	    Z = GET_Z (dst);
	    V = 0;
	    C = ((dst > 077777) || (dst < -0100000));
	    break;
	case 1:						/* DIV */
	    src2 = dstreg? R[dstspec]: ReadW (GeteaW (dstspec));
	    src = (((uint32) R[srcspec]) << 16) | R[srcspec | 1];
	    if (src2 == 0) {
		N = 0;					/* J11,11/70 compat */
		Z = V = C = 1;				/* N = 0, Z = 1 */
		break;  }
	    if ((src == 020000000000) && (src2 == 0177777)) {
		V = 1;					/* J11,11/70 compat */
		N = Z = C = 0;				/* N = Z = 0 */
		break;  }
	    if (GET_SIGN_W (src2)) src2 = src2 | ~077777;
	    if (GET_SIGN_W (R[srcspec])) src = src | ~017777777777;
	    dst = src / src2;
	    N = (dst < 0);				/* N set on 32b result */
	    if ((dst > 077777) || (dst < -0100000)) {
		V = 1;					/* J11,11/70 compat */
		Z = C = 0;				/* Z = C = 0 */
		break;  }
	    R[srcspec] = dst & 0177777;
	    R[srcspec | 1] = (src - (src2 * dst)) & 0177777;
	    Z = GET_Z (dst);
	    V = C = 0;
	    break;

/* Opcode 7: EIS, continued */

	case 2:						/* ASH */
	    src2 = dstreg? R[dstspec]: ReadW (GeteaW (dstspec));
	    src2 = src2 & 077;
	    sign = GET_SIGN_W (R[srcspec]);
	    src = sign? R[srcspec] | ~077777: R[srcspec];
	    if (src2 == 0) {				/* [0] */
		dst = src;
		V = C = 0;  }
	    else if (src2 <= 15) {			/* [1,15] */
		dst = src << src2;
		i = (src >> (16 - src2)) & 0177777;
		V = (i != ((dst & 0100000)? 0177777: 0));
		C = (i & 1);  }
	    else if (src2 <= 31) {			/* [16,31] */
		dst = 0;
		V = (src != 0);
		C = (src << (src2 - 16)) & 1;  }
	    else if (src2 == 32) {			/* [32] = -32 */
		dst = -sign;
		V = C = 0;  }
	    else {					/* [33,63] = -31,-1 */
		dst = (src >> (64 - src2)) | (-sign << (src2 - 32));
		V = 0;
		C = ((src >> (63 - src2)) & 1);  }
	    dst = R[srcspec] = dst & 0177777;
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    break;
	case 3:						/* ASHC */
	    src2 = dstreg? R[dstspec]: ReadW (GeteaW (dstspec));
	    src2 = src2 & 077;
	    sign = GET_SIGN_W (R[srcspec]);
	    src = (((uint32) R[srcspec]) << 16) | R[srcspec | 1];
	    if (src2 == 0) { 				/* [0] */
		dst = src;
		V = C = 0;  }
	    else if (src2 <= 31) {			/* [1,31] */
		dst = ((uint32) src) << src2;
		i = (src >> (32 - src2)) | (-sign << src2);
		V = (i != ((dst & 020000000000)? -1: 0));
		C = (i & 1);  }
	    else if (src2 == 32) {			/* [32] = -32 */
		dst = -sign;
		V = 0;
		C = (src >> 31) & 1;  }
	    else {					/* [33,63] = -31,-1 */
		dst = (src >> (64 - src2)) | (-sign << (src2 - 32));
		V = 0;
		C = ((src >> (63 - src2)) & 1);  }
	    i = R[srcspec] = (dst >> 16) & 0177777;
	    dst = R[srcspec | 1] = dst & 0177777;
	    N = GET_SIGN_W (i);
	    Z = GET_Z (dst | i);
	    break;

/* Opcode 7: EIS, continued */

	case 4:						/* XOR */
	    dst = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));
	    dst = dst ^ R[srcspec];
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    V = 0;
	    if (dstreg) R[dstspec] = dst;
	    else PWriteW (dst, last_pa);
	    break;
	case 5:						/* FIS - not impl */
	    setTRAP (TRAP_ILL);
	    break;
	case 6:						/* CIS */
	    if (cpu_unit.flags & UNIT_CIS) cis11 (IR);
	    else setTRAP (TRAP_ILL);
	    break;
	case 7:						/* SOB */
	    R[srcspec] = (R[srcspec] - 1) & 0177777;
	    if (R[srcspec]) {
		JMP_PC ((PC - dstspec - dstspec) & 0177777);  }
	    break;  }					/* end switch EIS */
	break;						/* end case 007 */

/* Opcode 10: branches, traps, SOPs */

case 010:
	switch ((IR >> 6) & 077) {			/* decode IR<11:6> */
	case 000: case 001:				/* BPL */
	    if (N == 0) { BRANCH_F (IR); } 
	    break;
	case 002: case 003:				/* BPL */
	    if (N == 0) { BRANCH_B (IR); }
	    break;
	case 004: case 005:				/* BMI */
	    if (N) { BRANCH_F (IR); } 
	    break;
	case 006: case 007:				/* BMI */
	    if (N) { BRANCH_B (IR); }
	    break;
	case 010: case 011:				/* BHI */
	    if ((C | Z) == 0) { BRANCH_F (IR); } 
	    break;
	case 012: case 013:				/* BHI */
	    if ((C | Z) == 0) { BRANCH_B (IR); }
	    break;
	case 014: case 015:				/* BLOS */
	    if (C | Z) { BRANCH_F (IR); } 
	    break;
	case 016: case 017:				/* BLOS */
	    if (C | Z) { BRANCH_B (IR); }
	    break;
	case 020: case 021:				/* BVC */
	    if (V == 0) { BRANCH_F (IR); } 
	    break;
	case 022: case 023:				/* BVC */
	    if (V == 0) { BRANCH_B (IR); }
	    break;
	case 024: case 025:				/* BVS */
	    if (V) { BRANCH_F (IR); } 
	    break;
	case 026: case 027:				/* BVS */
	    if (V) { BRANCH_B (IR); }
	    break;
	case 030: case 031:				/* BCC */
	    if (C == 0) { BRANCH_F (IR); } 
	    break;
	case 032: case 033:				/* BCC */
	    if (C == 0) { BRANCH_B (IR); }
	    break;
	case 034: case 035:				/* BCS */
	    if (C) { BRANCH_F (IR); } 
	    break;
	case 036: case 037:				/* BCS */
	    if (C) { BRANCH_B (IR); }
	    break;
	case 040: case 041: case 042: case 043:		/* EMT */
	    setTRAP (TRAP_EMT);
	    break;
	case 044: case 045: case 046: case 047:		/* TRAP */
	    setTRAP (TRAP_TRAP);
	    break;

/* Opcode 10, continued: SOPs */

	case 050:					/* CLRB */
	    N = V = C = 0;
	    Z = 1;
	    if (dstreg) R[dstspec] = R[dstspec] & 0177400;
	    else WriteB (0, GeteaB (dstspec));
	    break;
	case 051:					/* COMB */
	    dst = dstreg? R[dstspec]: ReadMB (GeteaB (dstspec));
	    dst = (dst ^ 0377) & 0377;
	    N = GET_SIGN_B (dst);
	    Z = GET_Z (dst);
	    V = 0;
	    C = 1;
	    if (dstreg) R[dstspec] = (R[dstspec] & 0177400) | dst;
	    else PWriteB (dst, last_pa);
	    break;
	case 052:					/* INCB */
	    dst = dstreg? R[dstspec]: ReadMB (GeteaB (dstspec));
	    dst = (dst + 1) & 0377;
	    N = GET_SIGN_B (dst);
	    Z = GET_Z (dst);
	    V = (dst == 0200);
	    if (dstreg) R[dstspec] = (R[dstspec] & 0177400) | dst;
	    else PWriteB (dst, last_pa);
	    break;
	case 053:					/* DECB */
	    dst = dstreg? R[dstspec]: ReadMB (GeteaB (dstspec));
	    dst = (dst - 1) & 0377;
	    N = GET_SIGN_B (dst);
	    Z = GET_Z (dst);
	    V = (dst == 0177);
	    if (dstreg) R[dstspec] = (R[dstspec] & 0177400) | dst;
	    else PWriteB (dst, last_pa);
	    break;
	case 054:					/* NEGB */
	    dst = dstreg? R[dstspec]: ReadMB (GeteaB (dstspec));
	    dst = (-dst) & 0377;
	    N = GET_SIGN_B (dst);
	    Z = GET_Z (dst);
	    V = (dst == 0200);
	    C = (Z ^ 1);
	    if (dstreg) R[dstspec] = (R[dstspec] & 0177400) | dst;
	    else PWriteB (dst, last_pa);
	    break;
	case 055:					/* ADCB */
	    dst = dstreg? R[dstspec]: ReadMB (GeteaB (dstspec));
	    dst = (dst + C) & 0377;
	    N = GET_SIGN_B (dst);
	    Z = GET_Z (dst);
	    V = (C && (dst == 0200));
	    C = C & Z;
	    if (dstreg) R[dstspec] = (R[dstspec] & 0177400) | dst;
	    else PWriteB (dst, last_pa);
	    break;

/* Opcode 10: SOPs, continued */

	case 056:					/* SBCB */
	    dst = dstreg? R[dstspec]: ReadMB (GeteaB (dstspec));
	    dst = (dst - C) & 0377;
	    N = GET_SIGN_B (dst);
	    Z = GET_Z (dst);
	    V = (C && (dst == 0177));
	    C = (C && (dst == 0377));
	    if (dstreg) R[dstspec] = (R[dstspec] & 0177400) | dst;
	    else PWriteB (dst, last_pa);
	    break;
	case 057:					/* TSTB */
	    dst = dstreg? R[dstspec] & 0377: ReadB (GeteaB (dstspec));
	    N = GET_SIGN_B (dst);
	    Z = GET_Z (dst);
	    V = C = 0;
	    break;
	case 060:					/* RORB */
	    src = dstreg? R[dstspec]: ReadMB (GeteaB (dstspec));
	    dst = ((src & 0377) >> 1) | (C << 7);
	    N = GET_SIGN_B (dst);
	    Z = GET_Z (dst);
	    C = (src & 1);
	    V = N ^ C;
	    if (dstreg) R[dstspec] = (R[dstspec] & 0177400) | dst;
	    else PWriteB (dst, last_pa);
	    break;
	case 061:					/* ROLB */
	    src = dstreg? R[dstspec]: ReadMB (GeteaB (dstspec));
	    dst = ((src << 1) | C) & 0377;
	    N = GET_SIGN_B (dst);
	    Z = GET_Z (dst);
	    C = GET_SIGN_B (src & 0377);
	    V = N ^ C;
	    if (dstreg) R[dstspec] = (R[dstspec] & 0177400) | dst;
	    else PWriteB (dst, last_pa);
	    break;
	case 062:					/* ASRB */
	    src = dstreg? R[dstspec]: ReadMB (GeteaB (dstspec));
	    dst = ((src & 0377) >> 1) | (src & 0200);
	    N = GET_SIGN_B (dst);
	    Z = GET_Z (dst);
	    C = (src & 1);
	    V = N ^ C;
	    if (dstreg) R[dstspec] = (R[dstspec] & 0177400) | dst;
	    else PWriteB (dst, last_pa);
	    break;
	case 063:					/* ASLB */
	    src = dstreg? R[dstspec]: ReadMB (GeteaB (dstspec));
	    dst = (src << 1) & 0377;
	    N = GET_SIGN_B (dst);
	    Z = GET_Z (dst);
	    C = GET_SIGN_B (src & 0377);
	    V = N ^ C;
	    if (dstreg) R[dstspec] = (R[dstspec] & 0177400) | dst;
	    else PWriteB (dst, last_pa);
	    break;

/* Opcode 10: SOPs, continued

   Notes:
   - MTPS cannot alter the T bit
   - MxPD must mask GeteaW returned address, dspace is from cm not pm
   - MxPD must set MMR1 for SP recovery in case of fault
*/

	case 064:					/* MTPS */
	    dst = dstreg? R[dstspec]: ReadB (GeteaB (dstspec));
	    if (cm == MD_KER) {
		ipl = (dst >> PSW_V_IPL) & 07;
		trap_req = calc_ints (ipl, trap_req);  }
	    N = (dst >> PSW_V_N) & 01;
	    Z = (dst >> PSW_V_Z) & 01;
	    V = (dst >> PSW_V_V) & 01;
	    C = (dst >> PSW_V_C) & 01;
	    break;
	case 065:					/* MFPD */
	    if (dstreg) {
		if ((dstspec == 6) && (cm != pm)) dst = STACKFILE[pm];
		else dst = R[dstspec];  }
	    else dst = ReadW ((GeteaW (dstspec) & 0177777) | calc_ds (pm));
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    V = 0;
	    SP = (SP - 2) & 0177777;
	    if (update_MM) MMR1 = calc_MMR1 (0366);
	    WriteW (dst, SP | dsenable);
	    if ((cm == MD_KER) && (SP < STKLIM)) {
		setTRAP (TRAP_YEL);
		setCPUERR (CPUE_YEL);  }
	    break;
	case 066:					/* MTPD */
	    dst = ReadW (SP | dsenable);
	    N = GET_SIGN_W (dst);
	    Z = GET_Z (dst);
	    V = 0;
	    SP = (SP + 2) & 0177777;
	    if (update_MM) MMR1 = 026;
	    if (dstreg) {
		if ((dstspec == 6) && (cm != pm)) STACKFILE[pm] = dst;
		else R[dstspec] = dst;  }
	    else WriteW (dst, (GeteaW (dstspec) & 0177777) | calc_ds (pm));
	    break;
	case 067:					/* MFPS */
	    dst = (ipl << PSW_V_IPL) | (tbit << PSW_V_TBIT) |
		(N << PSW_V_N) | (Z << PSW_V_Z) |
		(V << PSW_V_V) | (C << PSW_V_C);
	    N = GET_SIGN_B (dst);
	    Z = GET_Z (dst);
	    V = 0;
	    if (dstreg) R[dstspec] = (dst & 0200)? 0177400 | dst: dst;
	    else WriteB (dst, GeteaB (dstspec));
	    break;
	default:
	    setTRAP (TRAP_ILL);
	    break; }					/* end switch SOPs */
	break;						/* end case 010 */

/* Opcodes 11 - 16: double operand byte instructions

   Cmp: v = [sign (src) != sign (src2)] and [sign (src2) = sign (result)]
   Sub: v = [sign (src) != sign (src2)] and [sign (src) = sign (result)]
*/

case 011:						/* MOVB */
	if (srcreg && !dstreg) {			/* R,not R */
	    ea = GeteaB (dstspec);
	    dst = R[srcspec] & 0377;  }
	else {
	    dst = srcreg? R[srcspec] & 0377: ReadB (GeteaB (srcspec));
	    if (!dstreg) ea = GeteaB (dstspec);  }
	N = GET_SIGN_B (dst);
	Z = GET_Z (dst);
	V = 0;
	if (dstreg) R[dstspec] = (dst & 0200)? 0177400 | dst: dst;
	else WriteB (dst, ea);
	break;
case 012:						/* CMPB */
	if (srcreg && !dstreg) {			/* R,not R */
	    src2 = ReadB (GeteaB (dstspec));
	    src = R[srcspec] & 0377;  }
	else {
	    src = srcreg? R[srcspec] & 0377: ReadB (GeteaB (srcspec));
	    src2 = dstreg? R[dstspec] & 0377: ReadB (GeteaB (dstspec));  }
	dst = (src - src2) & 0377;
	N = GET_SIGN_B (dst);
	Z = GET_Z (dst);
	V = GET_SIGN_B ((src ^ src2) & (~src2 ^ dst));
	C = (src < src2);
	break;
case 013:						/* BITB */
	if (srcreg && !dstreg) {			/* R,not R */
	    src2 = ReadB (GeteaB (dstspec));
	    src = R[srcspec] & 0377;  }
	else {
	    src = srcreg? R[srcspec] & 0377: ReadB (GeteaB (srcspec));
	    src2 = dstreg? R[dstspec] & 0377: ReadB (GeteaB (dstspec));  }
	dst = (src2 & src) & 0377;
	N = GET_SIGN_B (dst);
	Z = GET_Z (dst);
	V = 0;
	break;
case 014:						/* BICB */
	if (srcreg && !dstreg) {			/* R,not R */
	    src2 = ReadMB (GeteaB (dstspec));
	    src = R[srcspec];  }
	else {
	    src = srcreg? R[srcspec]: ReadB (GeteaB (srcspec));
	    src2 = dstreg? R[dstspec]: ReadMB (GeteaB (dstspec));  }
	dst = (src2 & ~src) & 0377;
	N = GET_SIGN_B (dst);
	Z = GET_Z (dst);
	V = 0;
	if (dstreg) R[dstspec] = (R[dstspec] & 0177400) | dst;
	else PWriteB (dst, last_pa);
	break;
case 015:						/* BISB */
	if (srcreg && !dstreg) {			/* R,not R */
	    src2 = ReadMB (GeteaB (dstspec));
	    src = R[srcspec];  }
	else {
	    src = srcreg? R[srcspec]: ReadB (GeteaB (srcspec));
	    src2 = dstreg? R[dstspec]: ReadMB (GeteaB (dstspec));  }
	dst = (src2 | src) & 0377;
	N = GET_SIGN_B (dst);
	Z = GET_Z (dst);
	V = 0;
	if (dstreg) R[dstspec] = (R[dstspec] & 0177400) | dst;
	else PWriteB (dst, last_pa);
	break;
case 016:						/* SUB */
	if (srcreg && !dstreg) {			/* R,not R */
	    src2 = ReadMW (GeteaW (dstspec));
	    src = R[srcspec];  }
	else {
	    src = srcreg? R[srcspec]: ReadW (GeteaW (srcspec));
	    src2 = dstreg? R[dstspec]: ReadMW (GeteaW (dstspec));  }
	dst = (src2 - src) & 0177777;
	N = GET_SIGN_W (dst);
	Z = GET_Z (dst);
	V = GET_SIGN_W ((src ^ src2) & (~src ^ dst));
	C = (src2 < src);
	if (dstreg) R[dstspec] = dst;
	else PWriteW (dst, last_pa);
	break;

/* Opcode 17: floating point */

case 017:
	fp11 (IR);					/* floating point */
	break;						/* end case 017 */
	}						/* end switch op */
}							/* end main loop */

/* Simulation halted */

PSW = (cm << PSW_V_CM) | (pm << PSW_V_PM) | (rs << PSW_V_RS) |
	(ipl << PSW_V_IPL) | (tbit << PSW_V_TBIT) |
	(N << PSW_V_N) | (Z << PSW_V_Z) | (V << PSW_V_V) | (C << PSW_V_C);
for (i = 0; i < 6; i++) REGFILE[i][rs] = R[i];
STACKFILE[cm] = SP;
saved_PC = PC & 0177777;
pcq_r->qptr = pcq_p;					/* update pc q ptr */
set_r_display (rs, cm);
return reason;
}

/* Effective address calculations

   Inputs:
	spec	=	specifier <5:0>
   Outputs:
	ea	=	effective address
			<15:0> =  virtual address
			<16> =    instruction/data data space
			<18:17> = mode

   Data space calculation: the PDP-11 features both instruction and data
   spaces.  Instruction space contains the instruction and any sequential
   add ons (eg, immediates, absolute addresses).  Data space contains all
   data operands and indirect addresses.  If data space is enabled, then
   memory references are directed according to these rules:

	Mode	Index ref	Indirect ref		Direct ref
	10..16	na		na			data
	17	na		na			instruction
	20..26	na		na			data
	27	na		na			instruction
	30..36	na		data			data
	37	na		instruction (absolute)	data
	40..46	na		na			data
	47	na		na			instruction
	50..56	na		data			data
	57	na		instruction		data
	60..67	instruction	na			data
	70..77	instruction	data			data

   According to the PDP-11 Architecture Handbook, MMR1 records all
   autoincrement and autodecrement operations, including those which
   explicitly reference the PC.  For the J-11, this is only true for
   autodecrement operands, autodecrement deferred operands, and
   autoincrement destination operands that involve a write to memory.
   The simulator follows the Handbook, for simplicity.

   Notes:

   - dsenable will direct a reference to data space if data space is enabled
   - ds will direct a reference to data space if data space is enabled AND if
	the specifier register is not PC; this is used for 17, 27, 37, 47, 57
   - Modes 2x, 3x, 4x, and 5x must update MMR1 if updating enabled
   - Modes 46 and 56 must check for stack overflow if kernel mode
*/

/* Effective address calculation for words */

int32 GeteaW (int32 spec)
{
int32 adr, reg, ds;

reg = spec & 07;					/* register number */
ds = (reg == 7)? isenable: dsenable;			/* dspace if not PC */
switch (spec >> 3) {					/* decode spec<5:3> */
default:						/* can't get here */
case 1:							/* (R) */
	return (R[reg] | ds);
case 2:							/* (R)+ */
	R[reg] = ((adr = R[reg]) + 2) & 0177777;
	if (update_MM) MMR1 = calc_MMR1 (020 | reg);
	return (adr | ds);
case 3:							/* @(R)+ */
	R[reg] = ((adr = R[reg]) + 2) & 0177777;
	if (update_MM) MMR1 = calc_MMR1 (020 | reg);
	adr = ReadW (adr | ds);
	return (adr | dsenable);
case 4:							/* -(R) */
	adr = R[reg] = (R[reg] - 2) & 0177777;
	if (update_MM) MMR1 = calc_MMR1 (0360 | reg);
	if ((adr < STKLIM) && (reg == 6) && (cm == MD_KER)) {
	    setTRAP (TRAP_YEL);
	    setCPUERR (CPUE_YEL);  }
	return (adr | ds);
case 5:							/* @-(R) */
	adr = R[reg] = (R[reg] - 2) & 0177777;
	if (update_MM) MMR1 = calc_MMR1 (0360 | reg);
	if ((adr < STKLIM) && (reg == 6) && (cm == MD_KER)) {
	    setTRAP (TRAP_YEL);
	    setCPUERR (CPUE_YEL);  }
	adr = ReadW (adr | ds);
	return (adr | dsenable);
case 6:							/* d(r) */
	adr = ReadW (PC | isenable);
	PC = (PC + 2) & 0177777;
	return (((R[reg] + adr) & 0177777) | dsenable);
case 7:							/* @d(R) */
	adr = ReadW (PC | isenable);
	PC = (PC + 2) & 0177777;
	adr = ReadW (((R[reg] + adr) & 0177777) | dsenable);
	return (adr | dsenable);
	}						/* end switch */
}

/* Effective address calculation for bytes */

int32 GeteaB (int32 spec)
{
int32 adr, reg, ds, delta;

reg = spec & 07;					/* reg number */
ds = (reg == 7)? isenable: dsenable;			/* dspace if not PC */
switch (spec >> 3) {					/* decode spec<5:3> */
default:						/* can't get here */
case 1:							/* (R) */
	return (R[reg] | ds);
case 2:							/* (R)+ */
	delta = 1 + (reg >= 6);				/* 2 if R6, PC */
	R[reg] = ((adr = R[reg]) + delta) & 0177777;
	if (update_MM) MMR1 = calc_MMR1 ((delta << 3) | reg);
	return (adr | ds);
case 3:							/* @(R)+ */
	R[reg] = ((adr = R[reg]) + 2) & 0177777;
	if (update_MM) MMR1 = calc_MMR1 (020 | reg);
	adr = ReadW (adr | ds);
	return (adr | dsenable);
case 4:							/* -(R) */
	delta = 1 + (reg >= 6);				/* 2 if R6, PC */
	adr = R[reg] = (R[reg] - delta) & 0177777;
	if (update_MM) MMR1 = calc_MMR1 ((((-delta) & 037) << 3) | reg);
	if ((adr < STKLIM) && (reg == 6) && (cm == MD_KER)) {
	    setTRAP (TRAP_YEL);
	    setCPUERR (CPUE_YEL);  }
	return (adr | ds);
case 5:							/* @-(R) */
	adr = R[reg] = (R[reg] - 2) & 0177777;
	if (update_MM) MMR1 = calc_MMR1 (0360 | reg);
	if ((adr < STKLIM) && (reg == 6) && (cm == MD_KER)) {
	    setTRAP (TRAP_YEL);
	    setCPUERR (CPUE_YEL);  }
	adr = ReadW (adr | ds);
	return (adr | dsenable);
case 6:							/* d(r) */
	adr = ReadW (PC | isenable);
	PC = (PC + 2) & 0177777;
	return (((R[reg] + adr) & 0177777) | dsenable);
case 7:							/* @d(R) */
	adr = ReadW (PC | isenable);
	PC = (PC + 2) & 0177777;
	adr = ReadW (((R[reg] + adr) & 0177777) | dsenable);
	return (adr | dsenable);
	}						/* end switch */
}

/* Read byte and word routines, read only and read-modify-write versions

   Inputs:
	va	=	virtual address, <18:16> = mode, I/D space
   Outputs:
	data	=	data read from memory or I/O space
*/

int32 ReadW (int32 va)
{
int32 pa, data;

if (va & 1) {						/* odd address? */
	setCPUERR (CPUE_ODD);
	ABORT (TRAP_ODD);  }
pa = relocR (va);					/* relocate */
if (ADDR_IS_MEM (pa)) return (M[pa >> 1]);		/* memory address? */
if (pa < IOPAGEBASE) {					/* I/O address? */
	setCPUERR (CPUE_NXM);
	ABORT (TRAP_NXM);  }
if (iopageR (&data, pa, READ) != SCPE_OK) {		/* invalid I/O addr? */
	setCPUERR (CPUE_TMO);
	ABORT (TRAP_NXM);  }
return data;
}

int32 ReadB (int32 va)
{
int32 pa, data;

pa = relocR (va);					/* relocate */
if (ADDR_IS_MEM (pa)) return (va & 1? M[pa >> 1] >> 8: M[pa >> 1]) & 0377;
if (pa < IOPAGEBASE) {					/* I/O address? */
	setCPUERR (CPUE_NXM);
	ABORT (TRAP_NXM);  }
if (iopageR (&data, pa, READ) != SCPE_OK) {		/* invalid I/O addr? */
	setCPUERR (CPUE_TMO);
	ABORT (TRAP_NXM);  }
return ((va & 1)? data >> 8: data) & 0377;
}

int32 ReadMW (int32 va)
{
int32 data;

if (va & 1) {						/* odd address? */
	setCPUERR (CPUE_ODD);
	ABORT (TRAP_ODD);  }
last_pa = relocW (va);					/* reloc, wrt chk */
if (ADDR_IS_MEM (last_pa)) return (M[last_pa >> 1]);	/* memory address? */
if (last_pa < IOPAGEBASE) {				/* I/O address? */
	setCPUERR (CPUE_NXM);
	ABORT (TRAP_NXM);  }
if (iopageR (&data, last_pa, READ) != SCPE_OK) {	/* invalid I/O addr? */
	setCPUERR (CPUE_TMO);
	ABORT (TRAP_NXM);  }
return data;
}

int32 ReadMB (int32 va)
{
int32 data;

last_pa = relocW (va);					/* reloc, wrt chk */
if (ADDR_IS_MEM (last_pa))
	return (va & 1? M[last_pa >> 1] >> 8: M[last_pa >> 1]) & 0377;
if (last_pa < IOPAGEBASE) {				/* I/O address? */
	setCPUERR (CPUE_NXM);
	ABORT (TRAP_NXM);  }
if (iopageR (&data, last_pa, READ) != SCPE_OK) {	/* invalid I/O addr? */
	setCPUERR (CPUE_TMO);
	ABORT (TRAP_NXM);  }
return ((va & 1)? data >> 8: data) & 0377;
}

/* Write byte and word routines

   Inputs:
	data	=	data to be written
	va	=	virtual address, <18:16> = mode, I/D space, or
	pa	=	physical address
   Outputs: none
*/

void WriteW (int32 data, int32 va)
{
int32 pa;

if (va & 1) {						/* odd address? */
	setCPUERR (CPUE_ODD);
	ABORT (TRAP_ODD);  }
pa = relocW (va);					/* relocate */
if (ADDR_IS_MEM (pa)) {					/* memory address? */
	M[pa >> 1] = data;
	return;  }
if (pa < IOPAGEBASE) {					/* I/O address? */
	setCPUERR (CPUE_NXM);
	ABORT (TRAP_NXM);  }
if (iopageW (data, pa, WRITE) != SCPE_OK) {		/* invalid I/O addr? */
	setCPUERR (CPUE_TMO);
	ABORT (TRAP_NXM);  }
return;
}

void WriteB (int32 data, int32 va)
{
int32 pa;

pa = relocW (va);					/* relocate */
if (ADDR_IS_MEM (pa)) {					/* memory address? */
	if (va & 1) M[pa >> 1] = (M[pa >> 1] & 0377) | (data << 8);
	else M[pa >> 1] = (M[pa >> 1] & ~0377) | data;
	return;  }             
if (pa < IOPAGEBASE) {					/* I/O address? */
	setCPUERR (CPUE_NXM);
	ABORT (TRAP_NXM);  }
if (iopageW (data, pa, WRITEB) != SCPE_OK) {		/* invalid I/O addr? */
	setCPUERR (CPUE_TMO);
	ABORT (TRAP_NXM);  }
return;
}

void PWriteW (int32 data, int32 pa)
{
if (ADDR_IS_MEM (pa)) {					/* memory address? */
	M[pa >> 1] = data;
	return;  }
if (pa < IOPAGEBASE) {					/* I/O address? */
	setCPUERR (CPUE_NXM);
	ABORT (TRAP_NXM);  }
if (iopageW (data, pa, WRITE) != SCPE_OK) {		/* invalid I/O addr? */
	setCPUERR (CPUE_TMO);
	ABORT (TRAP_NXM);  }
return;
}

void PWriteB (int32 data, int32 pa)
{
if (ADDR_IS_MEM (pa)) {					/* memory address? */
	if (pa & 1) M[pa >> 1] = (M[pa >> 1] & 0377) | (data << 8);
	else M[pa >> 1] = (M[pa >> 1] & ~0377) | data;
	return;  }             
if (pa < IOPAGEBASE) {					/* I/O address? */
	setCPUERR (CPUE_NXM);
	ABORT (TRAP_NXM);  }
if (iopageW (data, pa, WRITEB) != SCPE_OK) {		/* invalid I/O addr? */
	setCPUERR (CPUE_TMO);
	ABORT (TRAP_NXM);  }
return;
}

/* Relocate virtual address, read access

   Inputs:
	va	=	virtual address, <18:16> = mode, I/D space
   Outputs:
	pa	=	physical address
   On aborts, this routine aborts back to the top level simulator
   with an appropriate trap code.

   Notes:
   - APRFILE[UNUSED] is all zeroes, forcing non-resident abort
   - Aborts must update MMR0<15:13,6:1> if updating is enabled
*/

int32 relocR (int32 va)
{
int32 dbn, plf, apridx, apr, pa;

if (MMR0 & MMR0_MME) {					/* if mmgt */
	apridx = (va >> VA_V_APF) & 077;		/* index into APR */
	apr = APRFILE[apridx];				/* with va<18:13> */
	dbn = va & VA_BN;				/* extr block num */
	plf = (apr & PDR_PLF) >> 2;			/* extr page length */
	if ((apr & PDR_PRD) == 0) {			/* not readable? */
	    if (update_MM) MMR0 =			/* update MMR0 */
		(MMR0 & ~MMR0_PAGE) | (apridx << MMR0_V_PAGE);
	    MMR0 = MMR0 | MMR0_NR;			/* err non-resident */
	    ABORT (TRAP_MME);  }			/* abort ref */
	if ((apr & PDR_ED)? dbn < plf: dbn > plf) {	/* if pg lnt error */
	    if (update_MM) MMR0 =			/* update MMR0 */
		(MMR0 & ~MMR0_PAGE) | (apridx << MMR0_V_PAGE);
	    MMR0 = MMR0 | MMR0_PL;
	    ABORT (TRAP_MME);  }			/* abort ref */
	pa = ((va & VA_DF) + ((apr >> 10) & 017777700)) & PAMASK;
	if ((MMR3 & MMR3_M22E) == 0) {
	    pa = pa & 0777777;
	    if (pa >= 0760000) pa = 017000000 | pa;  }  }
else {	pa = va & 0177777;				/* mmgt off */
	if (pa >= 0160000) pa = 017600000 | pa;  }
return pa;
}

/* Relocate virtual address, write access

   Inputs:
	va	=	virtual address, <18:16> = mode, I/D space
   Outputs:
	pa	=	physical address
   On aborts, this routine aborts back to the top level simulator
   with an appropriate trap code.

   Notes:
   - APRFILE[UNUSED] is all zeroes, forcing non-resident abort
   - Aborts must update MMR0<15:13,6:1> if updating is enabled
*/

int32 relocW (int32 va)
{
int32 dbn, plf, apridx, apr, pa;

if (MMR0 & MMR0_MME) {					/* if mmgt */
	apridx = (va >> VA_V_APF) & 077;		/* index into APR */
	apr = APRFILE[apridx];				/* with va<18:13> */
	dbn = va & VA_BN;				/* extr block num */
	plf = (apr & PDR_PLF) >> 2;			/* extr page length */
	if ((apr & PDR_PRD) == 0) {			/* not readable? */
	    if (update_MM) MMR0 =			/* update MMR0 */
		(MMR0 & ~MMR0_PAGE) | (apridx << MMR0_V_PAGE);
	    MMR0 = MMR0 | MMR0_NR;			/* err non-resident */
	    ABORT (TRAP_MME);  }			/* abort ref */
	if ((apr & PDR_ED)? dbn < plf: dbn > plf) {	/* if pg lnt error */
	    if (update_MM) MMR0 =			/* update MMR0 */
		(MMR0 & ~MMR0_PAGE) | (apridx << MMR0_V_PAGE);
	    MMR0 = MMR0 | MMR0_PL;
	    ABORT (TRAP_MME);  }			/* abort ref */
	if ((apr & PDR_PWR) == 0) {			/* not writeable? */
	    if (update_MM) MMR0 =			/* update MMR0 */
		(MMR0 & ~MMR0_PAGE) | (apridx << MMR0_V_PAGE);
	    MMR0 = MMR0 | MMR0_RO;			/* err read only */
	    ABORT (TRAP_MME);  }			/* abort ref */
	APRFILE[apridx] = apr | PDR_W;			/* set W */
	pa = ((va & VA_DF) + ((apr >> 10) & 017777700)) & PAMASK;
	if ((MMR3 & MMR3_M22E) == 0) {
	    pa = pa & 0777777;
	    if (pa >= 0760000) pa = 017000000 | pa;  }  }
else {	pa = va & 0177777;				/* mmgt off */
	if (pa >= 0160000) pa = 017600000 | pa;  }
return pa;
}

/* Relocate virtual address, console access

   Inputs:
	va	=	virtual address
	sw	=	switches
   Outputs:
	pa	=	physical address
   On aborts, this routine returns MAXMEMSIZE
*/

int32 relocC (int32 va, int32 sw)
{
int32 mode, dbn, plf, apridx, apr, pa;

if (MMR0 & MMR0_MME) {					/* if mmgt */
	if (sw & SWMASK ('K')) mode = MD_KER;
	else if (sw & SWMASK ('S')) mode = MD_SUP;
	else if (sw & SWMASK ('U')) mode = MD_USR;
	else if (sw & SWMASK ('P')) mode = (PSW >> PSW_V_PM) & 03;
	else mode = (PSW >> PSW_V_CM) & 03;
	va = va | ((sw & SWMASK ('D'))? calc_ds (mode): calc_is (mode));
	apridx = (va >> VA_V_APF) & 077;		/* index into APR */
	apr = APRFILE[apridx];				/* with va<18:13> */
	dbn = va & VA_BN;				/* extr block num */
	plf = (apr & PDR_PLF) >> 2;			/* extr page length */
	if ((apr & PDR_PRD) == 0) return MAXMEMSIZE;	/* not readable? */
	if ((apr & PDR_ED)? dbn < plf: dbn > plf) return MAXMEMSIZE;
	pa = ((va & VA_DF) + ((apr >> 10) & 017777700)) & PAMASK;
	if ((MMR3 & MMR3_M22E) == 0) {
	    pa = pa & 0777777;
	    if (pa >= 0760000) pa = 017000000 | pa;  }  }
else {	pa = va & 0177777;				/* mmgt off */
	if (pa >= 0160000) pa = 017600000 | pa;  }
return pa;
}

/* I/O page routines for CPU registers

   Switch register and memory management registers

   SR 	17777570	read only
   MMR0 17777572	read/write, certain bits unimplemented or read only
   MMR1 17777574	read only
   MMR2 17777576	read only
   MMR3 17777516	read/write, certain bits unimplemented
*/

t_stat SR_MMR012_rd (int32 *data, int32 pa, int32 access)
{
switch ((pa >> 1) & 3) {				/* decode pa<2:1> */
case 0:							/* SR */
	*data = SR;
	break;
case 1:							/* MMR0 */
	*data = MMR0 & MMR0_IMP;
	break;
case 2:							/* MMR1 */
	*data = MMR1;
	break;
case 3:							/* MMR2 */
	*data = MMR2;
	break;  }					/* end switch pa */
return SCPE_OK;
}

t_stat SR_MMR012_wr (int32 data, int32 pa, int32 access)
{
switch ((pa >> 1) & 3) {				/* decode pa<2:1> */
case 0:							/* DR */
	DR = data;
	return SCPE_OK;
case 1:							/* MMR0 */
	if (access == WRITEB) data = (pa & 1)?
	    (MMR0 & 0377) | (data << 8): (MMR0 & ~0377) | data;
	MMR0 = (MMR0 & ~MMR0_RW) | (data & MMR0_RW);
	return SCPE_OK;
default:						/* MMR1, MMR2 */
	return SCPE_OK;  }				/* end switch pa */
}

t_stat MMR3_rd (int32 *data, int32 pa, int32 access)	/* MMR3 */
{
*data = MMR3 & MMR3_IMP;
return SCPE_OK;
}

t_stat MMR3_wr (int32 data, int32 pa, int32 access)	/* MMR3 */
{
if (pa & 1) return SCPE_OK;
MMR3 = data & MMR3_RW;
if (cpu_unit.flags & UNIT_18B)
	MMR3 = MMR3 & ~(MMR3_BME + MMR3_M22E);		/* for UNIX V6 */
cpu_bme = (MMR3 & MMR3_BME) && cpu_ubm;
dsenable = calc_ds (cm);
return SCPE_OK;
}

/* PARs and PDRs.  These are grouped in I/O space as follows:

	17772200 - 17772276	supervisor block
	17772300 - 17772376	kernel block
	17777600 - 17777676	user block

   Within each block, the subblocks are I PDR's, D PDR's, I PAR's, D PAR's

   Thus, the algorithm for converting between I/O space addresses and
   APRFILE indices is as follows:

	idx<3:0> =	dspace'page	=	pa<4:1>
	par	=	PDR vs PAR	=	pa<5>
	idx<5:4> =	ker/sup/user	=	pa<8>'~pa<6>

   Note that the W bit is read only; it is cleared by any write to an APR
*/

t_stat APR_rd (int32 *data, int32 pa, int32 access)
{
t_stat left, idx;

idx = (pa >> 1) & 017;					/* dspace'page */
left = (pa >> 5) & 1;					/* PDR vs PAR */
if ((pa & 0100) == 0) idx = idx | 020;			/* 1 for super, user */
if (pa & 0400) idx = idx | 040;				/* 1 for user only */
*data = left? (APRFILE[idx] >> 16) & 0177777: APRFILE[idx] & PDR_IMP;
return SCPE_OK;
}

t_stat APR_wr (int32 data, int32 pa, int32 access)
{
int32 left, idx, curr;

idx = (pa >> 1) & 017;					/* dspace'page */
left = (pa >> 5) & 1;					/* PDR vs PAR */
if ((pa & 0100) == 0) idx = idx | 020;			/* 1 for super, user */
if (pa & 0400) idx = idx | 040;				/* 1 for user only */
curr = left? (APRFILE[idx] >> 16) & 0177777: APRFILE[idx] & PDR_IMP;
if (access == WRITEB) data = (pa & 1)?
	(curr & 0377) | (data << 8): (curr & ~0377) | data;
if (left) APRFILE[idx] =
	((APRFILE[idx] & 0177777) | (((uint32) data) << 16)) & ~PDR_W;
else APRFILE[idx] =
	((APRFILE[idx] & ~PDR_RW) | (data & PDR_RW)) & ~PDR_W;
return SCPE_OK;
}

/* CPU control registers

   MEMERR	17777744	read only, clear on write
   CCR		17777746	read/write
   MAINT	17777750	read only
   HITMISS	17777752	read only
   CPUERR	17777766	read only, clear on write
   PIRQ		17777772	read/write, with side effects
   PSW		17777776	read/write, with side effects
*/

t_stat CPU_rd (int32 *data, int32 pa, int32 access)
{
switch ((pa >> 1) & 017) {				/* decode pa<4:1> */
case 2: 						/* MEMERR */
	*data = MEMERR;
	return SCPE_OK;
case 3:							/* CCR */
	*data = CCR;
	return SCPE_OK;
case 4:							/* MAINT */
	if (cpu_ubm) *data = MAINT | MAINT_U;
	else *data = MAINT & ~MAINT_U;
	return SCPE_OK;
case 5:							/* Hit/miss */
	*data = HITMISS;
	return SCPE_OK;
case 013:						/* CPUERR */
	*data = CPUERR & CPUE_IMP;
	return SCPE_OK;
case 015:						/* PIRQ */
	*data = PIRQ;
	return SCPE_OK;
case 017:						/* PSW */
	if (access == READC) *data = PSW;
	else *data = (cm << PSW_V_CM) | (pm << PSW_V_PM) | (rs << PSW_V_RS) |
		(ipl << PSW_V_IPL) | (tbit << PSW_V_TBIT) |
		(N << PSW_V_N) | (Z << PSW_V_Z) |
		(V << PSW_V_V) | (C << PSW_V_C);
	return SCPE_OK;  }				/* end switch PA */
return SCPE_NXM;					/* unimplemented */
}

/* CPU control registers, continued */

t_stat CPU_wr (int32 data, int32 pa, int32 access)
{
int32 i, pl, curr, oldrs;

switch ((pa >> 1) & 017) {				/* decode pa<4:1> */
case 2: 						/* MEMERR */
	MEMERR = 0;
	return SCPE_OK;
case 3:							/* CCR */
	if (access == WRITEB) data = (pa & 1)?
	    (CCR & 0377) | (data << 8): (CCR & ~0377) | data;
	CCR = data;
	return SCPE_OK;
case 4:							/* MAINT */
	return SCPE_OK;
case 5:							/* Hit/miss */
	return SCPE_OK;
case 013:						/* CPUERR */
	CPUERR = 0;
	return SCPE_OK;
case 015:						/* PIRQ */
	if (access == WRITEB) {
		if (pa & 1) data = data << 8;
		else return SCPE_OK;  }
	PIRQ = data & PIRQ_RW;
	pl = 0;
	if (PIRQ & PIRQ_PIR1) { SET_INT (PIR1); pl = 0042;  }
	else CLR_INT (PIR1);
	if (PIRQ & PIRQ_PIR2) { SET_INT (PIR2); pl = 0104;  }
	else CLR_INT (PIR2);
	if (PIRQ & PIRQ_PIR3) { SET_INT (PIR3); pl = 0146;  }
	else CLR_INT (PIR3);
	if (PIRQ & PIRQ_PIR4) { SET_INT (PIR4); pl = 0210;  }
	else CLR_INT (PIR4);
	if (PIRQ & PIRQ_PIR5) { SET_INT (PIR5); pl = 0252;  }
	else CLR_INT (PIR5);
	if (PIRQ & PIRQ_PIR6) { SET_INT (PIR6); pl = 0314;  }
	else CLR_INT (PIR6);
	if (PIRQ & PIRQ_PIR7) { SET_INT (PIR7); pl = 0356;  }
	else CLR_INT (PIR7);
	PIRQ = PIRQ | pl;
	return SCPE_OK;

/* CPU control registers, continued

   Note: Explicit writes to the PSW do not modify the T bit
*/

case 017:						/* PSW */
	if (access == WRITEC) {				/* console access? */
	    PSW = data & PSW_RW;
	    return SCPE_OK;  }
	curr = (cm << PSW_V_CM) | (pm << PSW_V_PM) | (rs << PSW_V_RS) |
		(ipl << PSW_V_IPL) | (tbit << PSW_V_TBIT) |
		(N << PSW_V_N) | (Z << PSW_V_Z) |
		(V << PSW_V_V) | (C << PSW_V_C);
	STACKFILE[cm] = SP;
	if (access == WRITEB) data = (pa & 1)?
	    (curr & 0377) | (data << 8): (curr & ~0377) | data;
	curr = (curr & ~PSW_RW) | (data & PSW_RW);
	oldrs = rs;
	cm = (curr >> PSW_V_CM) & 03;			/* call calc_is,ds */
	pm = (curr >> PSW_V_PM) & 03;
	rs = (curr >> PSW_V_RS) & 01;
	ipl = (curr >> PSW_V_IPL) & 07;
	N = (curr >> PSW_V_N) & 01;
	Z = (curr >> PSW_V_Z) & 01;
	V = (curr >> PSW_V_V) & 01;
	C = (curr >> PSW_V_C) & 01;
	if (rs != oldrs) {
	    for (i = 0; i < 6; i++) {
		REGFILE[i][oldrs] = R[i];
		R[i] = REGFILE[i][rs];  }  }
	SP = STACKFILE[cm];
	isenable = calc_is (cm);
	dsenable = calc_ds (cm);
	return SCPE_OK;  }				/* end switch pa */
return SCPE_NXM;					/* unimplemented */
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
int32 i;

PIRQ = MMR0 = MMR1 = MMR2 = MMR3 = 0;
cpu_bme = 0;
DR = CPUERR = MEMERR = CCR = HITMISS = 0;
PSW = 000340;
trap_req = 0;
wait_state = 0;
if (M == NULL) M = calloc (MEMSIZE >> 1, sizeof (unsigned int16));
if (M == NULL) return SCPE_MEM;
pcq_r = find_reg ("PCQ", NULL, dptr);
if (pcq_r) pcq_r->qptr = 0;
else return SCPE_IERR;
for (i = 0; i < UBM_LNT_LW; i++) ub_map[i] = 0;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
set_r_display (0, MD_KER);
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
int32 iodata;
t_stat stat;

if (vptr == NULL) return SCPE_ARG;
if (sw & SWMASK ('V')) {				/* -v */
	if (addr >= VASIZE) return SCPE_NXM;
	addr = relocC (addr, sw);			/* relocate */
	if (addr >= MAXMEMSIZE) return SCPE_REL;  }
if (addr < MEMSIZE) {
	*vptr = M[addr >> 1] & 0177777;
	return SCPE_OK;  }
if (addr < IOPAGEBASE) return SCPE_NXM;
stat = iopageR (&iodata, addr, READC);
*vptr = iodata;
return stat;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (sw & SWMASK ('V')) {				/* -v */
	if (addr >= VASIZE) return SCPE_NXM;
	addr = relocC (addr, sw);			/* relocate */
	if (addr >= MAXMEMSIZE) return SCPE_REL;  }
if (addr < MEMSIZE) {
	M[addr >> 1] = val & 0177777;
	return SCPE_OK;  }
if (addr < IOPAGEBASE) return SCPE_NXM;
return iopageW ((int32) val, addr, WRITEC);
}

/* Memory allocation */

t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 mc = 0;
uint32 i, clim;
unsigned int16 *nM;

if ((val <= 0) || (val > MAXMEMSIZE) || ((val & 07777) != 0))
	return SCPE_ARG;
for (i = val; i < MEMSIZE; i = i + 2) mc = mc | M[i >> 1];
if ((mc != 0) && !get_yn ("Really truncate memory [N]?", FALSE))
	return SCPE_OK;
nM = calloc (val >> 1, sizeof (unsigned int16));
if (nM == NULL) return SCPE_MEM;
clim = (((t_addr) val) < MEMSIZE)? val: MEMSIZE;
for (i = 0; i < clim; i = i + 2) nM[i >> 1] = M[i >> 1];
free (M);
M = nM;
MEMSIZE = val;
return cpu_set_bus (uptr, cpu_unit.flags, cptr, desc);  }

/* Bus configuration, disable Unibus or Qbus devices */

t_stat cpu_set_bus (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;
uint32 i, mask;

if ((val & UNIT_MAP) || (val & UNIT_18B))		/* Unibus variant? */
	mask = DEV_UBUS;
else if (MEMSIZE <= UNIMEMSIZE)				/* 18b Qbus? */
	mask = DEV_QBUS | DEV_Q18;
else mask = DEV_QBUS;					/* must be 22b */
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
	if ((dptr->flags & DEV_DISABLE) &&		/* disable-able? */
	    !(dptr->flags & DEV_DIS) &&			/* enabled? */
	    ((dptr->flags & mask) == 0)) {		/* not allowed? */
	    printf ("Disabling %s\n", sim_dname (dptr));
	    if (sim_log) fprintf (sim_log, "Disabling %s\n", sim_dname (dptr));
	    dptr->flags = dptr->flags | DEV_DIS;  }  }
return SCPE_OK;
}

/* Set R, SP register display addresses */

void set_r_display (int32 rs, int32 cm)
{
extern REG *find_reg (char *cptr, char **optr, DEVICE *dptr);
REG *rptr;
int32 i;

rptr = find_reg ("R0", NULL, &cpu_dev);
if (rptr == NULL) return;
for (i = 0; i < 6; i++, rptr++) rptr->loc = (void *) &REGFILE[i][rs];
rptr->loc = (void *) &STACKFILE[cm];
return;
}
