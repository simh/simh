/* id4_cpu.c: Interdata 4 CPU simulator

   Copyright (c) 1993-2001, Robert M. Supnik

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

   10-Aug-01	RMS	Removed register in declarations
   07-Oct-00	RMS	Overhauled I/O subsystem
   14-Apr-99	RMS	Changed t_addr to unsigned

   The register state for the Interdata 4 CPU is:

   R[0:F]<0:15>		general registers
   F[0:7]<0:31>		floating point registers
   PSW<0:31>		processor status word, including
    STAT<0:11>		status flags
    CC<0:3>			condition codes
    PC<0:15>		program counter
   int_req[8]<0:31>	interrupt requests
   int_enb[8]<0:31>	interrupt enables
   
   The Interdata 4 has three instruction formats: register to register,
   register to memory, and register to storage.  The formats are:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |           op          |     R1    |     R2    |	register-register
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

      0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |           op          |     R1    |     RX    |	register-memory
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |                    address                    |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |           op          |     R1    |     RX    |	register-storage
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |                    address                    |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   For register-memory and register-storage instructions, an effective
   address is calculated as follows:

	effective addr = address + RX (if RX > 0)

   Register-memory instructions can access an address space of 65K bytes.
*/

/* This routine is the instruction decode routine for the Interdata 4.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

	HALT instruction
	breakpoint encountered
	wait state and no I/O outstanding
	invalid instruction
	I/O error in I/O simulator

   2. Interrupts.  Each device has an interrupt request flag and an
      interrupt enabled flag.  To facilitate evaluation, all interrupt
      requests are kept in int_req, and all enables in int_enb.  If
	external interrupts are enabled in the PSW, and an external request
	is pending, an interrupt occurs.

   3. Non-existent memory.  On the Interdata 4, reads to non-existent
      memory return zero, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to zero.
      Thus, only writes need be checked against actual memory size.

   4. Adding I/O devices.  These modules must be modified:

	id4_defs.h	add a definition for the device mnemonic
	id4_cpu.c	add dispatch routine to table dev_tab
	id4_sys.c	add pointer to data structures to sim_devices
*/

#include "id4_defs.h"

#define ILL_ADR_FLAG	(MAXMEMSIZE)
#define save_ibkpt	(cpu_unit.u3)
#define UNIT_V_MSIZE	(UNIT_V_UF)			/* dummy mask */
#define UNIT_MSIZE	(1 << UNIT_V_MSIZE)
#define SIGN_EXT(x)	(((x) & SIGN)? (x) | ~MAGMASK: (x))
#define CC_GL(x)	if ((x) & SIGN) CC = CC_L; \
			else if (x) CC = CC_G; \
			else CC = 0
#define CC_GL_C(x)	if ((x) & SIGN) CC = CC_L; \
			else if (x) CC = CC_G; \
			else CC = 0
/*			else CC = CC & (CC_G | CC_L) */
#define BUILD_PSW	((PSW & ~CC_MASK) | CC)
#define PSW_SWAP(o,n)		WriteW ((o), BUILD_PSW); \
				WriteW ((o) + 2, PC); \
				PSW = ReadW (n); \
				PC = ReadW ((n) + 2); \
				CC = PSW & CC_MASK

uint16 M[MAXMEMSIZE >> 1] = { 0 };			/* memory */
int32 R[16] = { 0 };					/* general registers */
uint32 F[8] = { 0 };					/* fp registers */
int32 PSW = 0;						/* processor status word */
int32 saved_PC = 0;					/* program counter */
int32 SR = 0;						/* switch register */
int32 DR = 0;						/* display register */
int32 drmod = 0;					/* mode */
int32 srpos = 0;					/* switch register pos */
int32 drpos = 0;					/* display register pos */
int32 int_req[INTSZ] = { 0 };				/* interrupt requests */
int32 int_enb[INTSZ] = { 0 };				/* interrupt enables */
t_bool qanyin = FALSE;					/* interrupt outstanding */
int32 stop_inst = 0;					/* stop on ill inst */
int32 ibkpt_addr = ILL_ADR_FLAG | AMASK;		/* breakpoint addr */
int32 old_PC = 0;					/* previous PC */

extern int32 sim_int_char;
extern UNIT *sim_clock_queue;
t_bool int_eval (void);
t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_svc (UNIT *uptr);
t_stat cpu_set_size (UNIT *uptr, int32 value);
extern int32 le (int32 op, int32 r1, int32 r2, int32 ea);
extern int32 ce (int32 op, int32 r1, int32 r2, int32 ea);
extern int32 ase (int32 op, int32 r1, int32 r2, int32 ea);
extern int32 me (int32 op, int32 r1, int32 r2, int32 ea);
extern int32 de (int32 op, int32 r1, int32 r2, int32 ea);
extern int32 display (int32 op, int32 datout);
extern int32 tt (int32 op, int32 datout);
extern int32 pt (int32 op, int32 datout);

int32 (*dev_tab[DEVNO])(int32 op, int32 datout) = {
	NULL, &display, &tt, &pt, NULL, NULL, NULL, NULL };

/* CPU data structures

   cpu_dev	CPU device descriptor
   cpu_unit	CPU unit descriptor
   cpu_reg	CPU register list
   cpu_mod	CPU modifiers list
*/

UNIT cpu_unit = { UDATA (&cpu_svc, UNIT_FIX + UNIT_BINK,
		MAXMEMSIZE) };

REG cpu_reg[] = {
	{ HRDATA (PC, saved_PC, 16) },
	{ HRDATA (R0, R[0], 16) },
	{ HRDATA (R1, R[1], 16) },
	{ HRDATA (R2, R[2], 16) },
	{ HRDATA (R3, R[3], 16) },
	{ HRDATA (R4, R[4], 16) },
	{ HRDATA (R5, R[5], 16) },
	{ HRDATA (R6, R[6], 16) },
	{ HRDATA (R7, R[7], 16) },
	{ HRDATA (R8, R[8], 16) },
	{ HRDATA (R9, R[9], 16) },
	{ HRDATA (RA, R[10], 16) },
	{ HRDATA (RB, R[11], 16) },
	{ HRDATA (RC, R[12], 16) },
	{ HRDATA (RD, R[13], 16) },
	{ HRDATA (RE, R[14], 16) },
	{ HRDATA (RF, R[15], 16) },
	{ HRDATA (F0, F[0], 32) },
	{ HRDATA (F2, F[1], 32) },
	{ HRDATA (F4, F[2], 32) },
	{ HRDATA (F6, F[3], 32) },
	{ HRDATA (F8, F[4], 32) },
	{ HRDATA (FA, F[5], 32) },
	{ HRDATA (FC, F[6], 32) },
	{ HRDATA (FE, F[7], 32) },
	{ HRDATA (PSW, PSW, 16) },
	{ HRDATA (CC, PSW, 4) },
	{ HRDATA (SR, SR, 16) },
	{ HRDATA (DR, DR, 16) },
	{ GRDATA (DR1, DR, 16, 16, 16) },
	{ FLDATA (DRMOD, drmod, 0) },
	{ FLDATA (SRPOS, srpos, 0) },
	{ HRDATA (DRPOS, drpos, 2) },
	{ HRDATA (IRQ0, int_req[0], 32) },
	{ HRDATA (IRQ1, int_req[1], 32) },
	{ HRDATA (IRQ2, int_req[2], 32) },
	{ HRDATA (IRQ3, int_req[3], 32) },
	{ HRDATA (IRQ4, int_req[4], 32) },
	{ HRDATA (IRQ5, int_req[5], 32) },
	{ HRDATA (IRQ6, int_req[6], 32) },
	{ HRDATA (IRQ7, int_req[7], 32) },
	{ HRDATA (IEN0, int_enb[0], 32) },
	{ HRDATA (IEN1, int_enb[1], 32) },
	{ HRDATA (IEN2, int_enb[2], 32) },
	{ HRDATA (IEN3, int_enb[3], 32) },
	{ HRDATA (IEN4, int_enb[4], 32) },
	{ HRDATA (IEN5, int_enb[5], 32) },
	{ HRDATA (IEN6, int_enb[6], 32) },
	{ HRDATA (IEN7, int_enb[7], 32) },
	{ FLDATA (STOP_INST, stop_inst, 0) },
	{ HRDATA (OLDPC, old_PC, 16), REG_RO },
	{ HRDATA (BREAK, ibkpt_addr, 17) },
	{ ORDATA (WRU, sim_int_char, 8) },
	{ NULL }  };

MTAB cpu_mod[] = {
	{ UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
	{ UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
	{ UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
	{ UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
	{ UNIT_MSIZE, 49152, NULL, "48K", &cpu_set_size},
	{ UNIT_MSIZE, 65536, NULL, "64K", &cpu_set_size},
	{ 0 }  };

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 16, 16, 2, 16, 16,
	&cpu_ex, &cpu_dep, &cpu_reset,
	NULL, NULL, NULL };

t_stat sim_instr (void)
{
extern int32 sim_interval;
int32 dev, i, j, r, t;
int32 PC, OP, R1, R2, EA, CC;
int32 inc, lim;
t_stat reason;

/* Restore register state */

PC = saved_PC & AMASK;
CC = PSW & CC_MASK;					/* isolate cond codes */
qanyin = int_eval ();					/* eval interrupts */
reason = 0;

/* Main instruction fetch/decode loop */

while (reason == 0) {					/* loop until halted */
if (sim_interval <= 0) {				/* check clock queue */
	if (reason = sim_process_event ()) break;
	qanyin = int_eval ();  }

if ((PSW & PSW_EXI) && qanyin) {			/* interrupt? */
	PSW_SWAP (EXOPSW, EXNPSW);			/* swap PSW */
	continue;  }

if (PSW & PSW_WAIT) {					/* wait state? */
	if (sim_clock_queue != NULL) sim_interval = 0;	/* force check */
	else reason = STOP_WAIT;
	continue;  }

if (PC == ibkpt_addr) {					/* breakpoint? */
	save_ibkpt = ibkpt_addr;			/* save address */
	ibkpt_addr = ibkpt_addr | ILL_ADR_FLAG;		/* disable */
	sim_activate (&cpu_unit, 1);			/* sched re-enable */
	reason = STOP_IBKPT;				/* stop simulation */
	break;  }

sim_interval = sim_interval - 1;
t = ReadW (PC);						/* fetch instr */
PC = (PC + 2) & AMASK;					/* increment PC */
OP = (t >> 8) & 0xFF;					/* isolate op, R1, R2 */
R1 = (t >> 4) & 0xF;
R2 = t & 0xF;
if (OP & OP_4B) {					/* RX or RS? */
	EA = ReadW (PC);				/* fetch address */
	PC = (PC + 2) & AMASK;				/* increment PC */
	if (R2) EA = (EA + R[R2]) & AMASK;		/* index calculation */
	}
else EA = R[R2];					/* RR? "EA" = reg content */
switch (OP) {						/* case on opcode */

/* Load/store instructions */

case 0x48:						/* LH */
	EA = ReadW (EA);				/* fetch operand */
case 0x08:						/* LHR */
case 0xC8:						/* LHI */
	R[R1] = EA;					/* load operand */
	CC_GL (R[R1]);					/* set G,L */
	break;

case 0x40:						/* STH */
	WriteW (EA, R[R1]);				/* store register */
	break;

case 0xD1:						/* LM */
	for ( ; R1 <= 0xF; R1++) {			/* loop thru reg */
		R[R1] = ReadW (EA);			/* load register */
		EA = (EA + 2) & AMASK;  }		/* incr mem addr */
	break;

case 0xD0:						/* STM */
	for ( ; R1 <= 0xF; R1++) {			/* loop thru reg */
		WriteW (EA, R[R1]);			/* store register */
		EA = (EA + 2) & AMASK;  }		/* incr mem addr */
	break;

case 0x93:						/* LDBR */
	R[R1] = R[R2] & 0xFF;				/* load byte */
	break;
case 0xD3:						/* LDB */
	R[R1] = ReadB (EA);				/* load byte */
	break;

case 0x92:						/* STBR */
	R[R2] = (R[R2] & ~0xFF) | (R[R1] & 0xFF);	/* store byte */
	break;
case 0xD2:						/* STB */
	WriteB (EA, R[R1] & 0xFF);			/* store byte */
	break;

/* Control instructions */

case 0x01:						/* BALR */
case 0x41:						/* BAL */
	old_PC = R[R1] = PC;				/* save cur PC */
	PC = EA;					/* branch */
	break;

case 0x02:						/* BTCR */
case 0x42:						/* BTC */
	if (CC & R1) {					/* test CC's */
		old_PC = PC;				/* branch if true */
		PC = EA;  }
	break;

case 0x03:						/* BFCR */
case 0x43:						/* BFC */
	if ((CC & R1) == 0) {				/* test CC's */
		old_PC = PC;				/* branch if false */
		PC = EA;  }
	break;

case 0xC0:						/* BXH */
	inc = R[(R1 + 1) & 0xF];			/* inc = R1 + 1 */
	lim = R[(R1 + 2) & 0xF];			/* lim = R1 + 2 */
	R[R1] = (R[R1] + inc) & DMASK;			/* or -? */
	if (R[R1] > lim) {				/* if R1 > lim */
		old_PC = PC;				/* branch */
		PC = EA;  }
	break;

case 0xC1:						/* BXLE */
	inc = R[(R1 + 1) & 0xF];			/* inc = R1 + 1 */
	lim = R[(R1 + 2) & 0xF];			/* lim = R1 + 2 */
	R[R1] = (R[R1] + inc) & DMASK;			/* R1 = R1 + inc */
	if (R[R1] <= lim) {				/* if R1 <= lim */
		old_PC = PC;				/* branch */
		PC = EA;  }
	break;

case 0xC2:						/* LPSW */
	old_PC = PC;					/* effective branch */
	PSW = ReadW (EA);				/* read PSW/CC */
	CC = PSW & CC_MASK;				/* separate CC */
	PC = ReadW ((EA + 2) & AMASK);			/* read PC */
	break;

/* Logical and shift instructions */

case 0x44:						/* NH */
	EA = ReadW (EA);				/* fetch operand */
case 0x04:						/* NHR */
case 0xC4:						/* NHI */
	R[R1] = R[R1] & EA;				/* result */
	CC_GL (R[R1]);					/* set G,L */
	break;

case 0x46:						/* OH */
	EA = ReadW (EA);				/* fetch operand */
case 0x06:						/* OHR */
case 0xC6:						/* OHI */
	R[R1] = R[R1] | EA;				/* result */
	CC_GL (R[R1]);					/* set G,L */
	break;

case 0x47:						/* XH */
	EA = ReadW (EA);				/* fetch operand */
case 0x07:						/* XHR */
case 0xC7:						/* XHI */
	R[R1] = R[R1] ^ EA;				/* result */
	CC_GL (R[R1]);					/* set G,L */
	break;

case 0xCC:						/* SRHL */
	t = EA & 0xF;					/* shift count */
	r = R[R1] >> t;					/* result */
	CC_GL (r);					/* set G,L */
	if (t && ((R[R1] >> (t - 1)) & 1)) CC = CC | CC_C;
	R[R1] = r;					/* store result */
	break;

case 0xCD:						/* SLHL */
	t = EA & 0xF;					/* shift count */
	r = R[R1] << t;					/* result */
	R[R1] = r & DMASK;				/* store masked result */
	CC_GL (R[R1]);					/* set G,L */
	if (t && (r & 0x10000)) CC = CC_C;		/* set C if shft out */
	break;

case 0xCE:						/* SRHA */
	t = EA & 0xF;					/* shift count */
	r = (SIGN_EXT (R[R1]) >> t) & DMASK;		/* result */
	CC_GL (r);					/* set G,L */
	if (t && ((R[R1] >> (t - 1)) & 1)) CC = CC | CC_C;
	R[R1] = r;					/* store result */
	break;

case 0xCF:						/* SLHA */
	t = EA & 0xF;					/* shift count */
	r = R[R1] << t;					/* raw result */
	R[R1] = (R[R1] & SIGN) | (r & MAGMASK);		/* arith result */
	CC_GL (R[R1]);					/* set G,L */
	if (t && (r & SIGN)) CC = CC | CC_C;		/* set C if shft out */
	break;

/* Arithmetic instructions */

case 0x45:						/* CLH */
	EA = ReadW (EA);				/* fetch operand */
case 0x05:						/* CLHR */
case 0xC5:						/* CLHI */
	r = (R[R1] - EA) & DMASK;			/* result */
	CC_GL (r);					/* set G,L */
	if (R[R1] < EA) CC = CC | CC_C;			/* set C if borrow */
	if (((R[R1] ^ EA) & (~R[R1] ^ r)) & SIGN) CC = CC | CC_V;
	break;

case 0x4A:						/* AH */
	EA = ReadW (EA);				/* fetch operand */
case 0x0A:						/* AHR */
case 0xCA:						/* AHI */
	r = (R[R1] + EA) & DMASK;			/* result */
	CC_GL (r);					/* set G,L */
	if (r < EA) CC = CC | CC_C;			/* set C if carry */
	if (((~R[R1] ^ EA) & (R[R1] ^ r)) & SIGN) CC = CC | CC_V;
	R[R1] = r;
	break;

case 0x4B:						/* SH */
	EA = ReadW (EA);				/* fetch operand */
case 0x0B:						/* SHR */
case 0xCB:						/* SHI */
	r = (R[R1] - EA) & DMASK;			/* result */
	CC_GL (r);					/* set G,L */
	if (R[R1] < EA) CC = CC | CC_C;			/* set C if borrow */
	if (((R[R1] ^ EA) & (~R[R1] ^ r)) & SIGN) CC = CC | CC_V;
	R[R1] = r;
	break;

case 0x4C:						/* MH */
	EA = ReadW (EA);				/* fetch operand */
case 0x0C:						/* MHR */
	r = SIGN_EXT (R[R1 | 1]) * SIGN_EXT (EA);	/* multiply */
	R[R1] = (r >> 16) & DMASK;			/* high result */
	R[R1 | 1] = r & DMASK;				/* low result */
	break;

case 0x4D:						/* DH */
	EA = ReadW (EA);				/* fetch operand */
case 0x0D:						/* DHR */
	r = (SIGN_EXT (R[R1]) << 16) | R[R1 | 1];	/* form 32b divd */
	if (EA) {					/* if divisor != 0 */
		t = r / SIGN_EXT (EA);			/* quotient */
		r = r % SIGN_EXT (EA);  }		/* remainder */
	if (EA && ((t < 0x8000) || (t >= -0x8000))) {	/* if quo fits */
		R[R1] = r;				/* store remainder */
		R[R1 | 1] = t;  }			/* store quotient */
	else if (PSW & PSW_DFI) {			/* div fault enabled? */
		PSW_SWAP (IDOPSW, IDNPSW);  }		/* swap PSW */
	break;

case 0x4E:						/* ACH */
	EA = ReadW (EA);				/* fetch operand */
case 0x0E:						/* ACHR */
	t = R[R1] + EA + ((CC & CC_C) != 0);		/* raw result */
	r = t & 0xFFFF;					/* masked result */
	CC_GL_C (r);					/* set G,L */
	if (t > DMASK) CC = CC | CC_C;			/* set C if carry */
	if (((~R[R1] ^ EA) & (R[R1] ^ r)) & SIGN) CC = CC | CC_V;
	R[R1] = r;					/* store result */
	break;

case 0x4F:						/* SCH */
	EA = ReadW (EA);				/* fetch operand */
case 0x0F:						/* SCHR */
	t = R[R1] - EA - ((CC & CC_C) != 0);		/* raw result */
	r = t & 0xFFFF;					/* masked result */
	CC_GL_C (r);					/* set G,L */
	if (t < 0) CC = CC | CC_C;			/* set C if borrow */
	if (((R[R1] ^ EA) & (~R[R1] ^ t)) & 0x8000) CC = CC | CC_V;
	R[R1] = r;					/* store result */
	break;

/* Floating point instructions */

case 0x68:						/* LE */
case 0x28:						/* LER */
	CC = le (OP, R1, R2, EA);
	break;

case 0x69:						/* CE */
case 0x29:						/* CER */
	CC = ce (OP, R1, R2, EA);
	break;

case 0x6A:						/* AE */
case 0x6B:						/* SE */
case 0x2A:						/* AER */
case 0x2B:						/* SER */
	CC = ase (OP, R1, R2, EA);
	break;

case 0x6C:						/* ME */
case 0x2C:						/* MER */
	CC = me (OP, R1, R2, EA);
	break;

case 0x6D:						/* DE */
case 0x2D:						/* DER */
	t = de (OP, R1, R2, EA);		/* perform divide */
	if (t >= 0) CC = t;			/* if ok, set CC */
	else if (PSW & PSW_FDI) {		/* else fault */
		PSW_SWAP (FDOPSW, FDNPSW);  }		/* swap PSW */
	break;

case 0x60:						/* STE */
	WriteW ((F[R1 >> 1] >> 16) & DMASK, EA);
	WriteW (F[R1 >> 1] & DMASK, ((EA + 2) & AMASK));
	break;

/* I/O instructions */

case 0xDE:						/* OC */
	EA = ReadW (EA);				/* fetch operand */
case 0x9E:						/* OCR */
	dev = R[R1] & DEV_MAX;
	if (dev_tab[dev]) {
		dev_tab[dev] (IO_ADR, EA);		/* select */
		t = dev_tab[dev] (IO_OC, EA);		/* send command */
		qanyin = int_eval ();			/* re-eval intr */
		if (t & IOT_EXM) CC = CC_V;		/* set V if err */
		else CC = 0;
		reason = t >> IOT_V_REASON;  }
	else CC = CC_V;
	break;

case 0xDA:						/* WD */
	EA = ReadW (EA);				/* fetch operand */
case 0x9A:						/* WDR */
	dev = R[R1] & DEV_MAX;
	if (dev_tab[dev]) {
		dev_tab[dev] (IO_ADR, EA);		/* select */
		t = dev_tab[dev] (IO_WD, EA);		/* send data */
		qanyin = int_eval ();			/* re-eval intr */
		if (t & IOT_EXM) CC = CC_V;		/* set V if err */
		else CC = 0;
		reason = t >> IOT_V_REASON;  }
	else CC = CC_V;
	break;

case 0xD6:						/* WB */
case 0x96:						/* WBR */
	dev = R[R1] & DEV_MAX;
	if (OP & OP_4B) {
		EA = ReadW (EA);			/* start */
		lim = ReadW ((EA + 2) & 0xFFFF);  }	/* end */
	else lim = R[(R2 + 1) & 0xF];
	if (dev_tab[dev]) {
		dev_tab[dev] (IO_ADR, EA);		/* select */
		for ( ; EA <= lim; EA = ((EA + 1) & 0xFFFF)) {
			t = dev_tab[dev] (IO_WD, ReadB (EA));
			if (reason = t >> IOT_V_REASON) break;
			t = dev_tab[dev] (IO_SS, 0);
			if (CC = t & 0xF) break;  }
		qanyin = int_eval ();  }		/* re-eval intr */
	else CC = CC_V;
	break;

case 0xDB:						/* RD */
case 0x9B:						/* RDR */
	dev = R[R1] & DEV_MAX;
	if (dev_tab[dev]) {
		dev_tab[dev] (IO_ADR, EA);		/* select */
		t = dev_tab[dev] (IO_RD, 0);		/* get data */
		qanyin = int_eval ();			/* re-eval intr */
		if (OP & OP_4B) {			/* RX or RR? */
			WriteB (EA, t & 0xFF);  }
		else R[R2] = t & 0xFF;
		if (t & IOT_EXM) CC = CC_V;		/* set V if err */
		else CC = 0;
		reason = t >> IOT_V_REASON;  }
	else CC = CC_V;
	break;

case 0xD7:						/* RB */
case 0x97:						/* RBR */
	dev = R[R1] & DEV_MAX;
	if (OP & OP_4B) {
		EA = ReadW (EA);			/* start */
		lim = ReadW ((EA + 2) & 0xFFFF);  }	/* end */
	else lim = R[(R2 + 1) & 0xF];
	if (dev_tab[dev]) {
		dev_tab[dev] (IO_ADR, EA);		/* select */
		for ( ; EA <= lim; EA = ((EA + 1) & 0xFFFF)) {
			t = dev_tab[dev] (IO_RD, 0);
			WriteB (EA, t & 0xFF);
			if (reason = t >> IOT_V_REASON) break;
			t = dev_tab[dev] (IO_SS, 0);
			if (CC = t & 0xF) break;  }
		qanyin = int_eval ();  }		/* re-eval intr */
	else CC = CC_V;
	break;

case 0xDF:						/* AI */
case 0x9F:						/* AIR */
	for (i = t = 0; i < INTSZ; i++) {		/* loop thru array */
		uint32 temp;
		if (temp = int_req[i] & int_enb[i]) {	/* loop thru word */
			for (j = 0; j < 32; j++) {
				if (temp & INT_V(j)) break;
				t = t + 1;  }  }
		else t = t + 32;  }
	R[R1] = t & DEV_MAX;
	CLR_INT (t & DEV_MAX);				/* clear int req */
							/* fall through */
case 0xDD:						/* SS */
case 0x9D:						/* SSR */
	dev = R[R1] & DEV_MAX;
	if (dev_tab[dev]) {
		dev_tab[dev] (IO_ADR, EA);		/* select */
		t = dev_tab[dev] (IO_SS, 0);		/* get status */
		qanyin = int_eval ();			/* re-eval intr */
		if (OP & OP_4B) {			/* RR or RX? */
			WriteB (EA, t & 0xFF);  }
		else R[R2] = t & 0xFF;
		CC = t & 0xF;
		reason = t >> IOT_V_REASON;  }
	else CC = CC_V;
	break;

default:						/* undefined */
	PC = (PC - ((OP & OP_4B)? 4: 2)) & AMASK;
	if (reason = stop_inst) break;			/* stop on undef? */
	PSW_SWAP (ILOPSW, ILNPSW);			/* swap PSW */
	break;  }					/* end switch */
	}						/* end while */

/* Simulation halted */

PSW = BUILD_PSW;
saved_PC = PC & AMASK;
return reason;
}

/* Evaluate interrupt */

t_bool int_eval (void)
{
int i;

for (i = 0; i < INTSZ; i++)
	if (int_req[i] & int_enb[i]) return TRUE;
return FALSE;
}

/* Display register device */

int32 display (int32 op, int32 dat)
{
int t;

switch (op) {
case IO_ADR:						/* select */
	drpos = srpos = 0;				/* clear counters */
	break;
case IO_OC:						/* command */
	op = op & 0xC0;
	if (op == 0x40) drmod = 1;			/* x40 = inc */
	else if (op == 0x80) drmod = 0;			/* x80 = norm */
	else if (op == 0xC0) drmod = drmod ^ 1;		/* xC0 = flip */
	break;
case IO_WD:						/* write */
	DR = (DR & ~(0xFF << (drpos * 8))) | (dat << (drpos * 8));
	if (drmod) drpos = (drpos + 1) & 0x3;
	break;
case IO_RD:						/* read */
	t = (SR >> (srpos * 8)) & 0xFF;
	srpos = srpos ^ 1;
	return t;
case IO_SS:						/* status */
	return 0x80;  }
return 0;
}	

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
PSW = 0;
DR = 0;
drmod = 0;
return cpu_svc (&cpu_unit);
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE) return SCPE_NXM;
if (vptr != NULL) *vptr = ReadW (addr);
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE) return SCPE_NXM;
WriteW (addr, val);
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

if ((value <= 0) || (value > MAXMEMSIZE) || ((value & 0xFFF) != 0))
	return SCPE_ARG;
for (i = value; i < MEMSIZE; i++) mc = mc | M[i];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
	return SCPE_OK;
MEMSIZE = value;
for (i = MEMSIZE; i < MAXMEMSIZE; i++) M[i] = 0;
return SCPE_OK;
}
