/* pdp1_cpu.c: PDP-1 CPU simulator

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

   cpu		PDP-1 central processor

   07-Dec-01	RMS	Revised to use breakpoint package
   30-Nov-01	RMS	Added extended SET/SHOW support
   16-Dec-00	RMS	Fixed bug in XCT address calculation
   14-Apr-99	RMS	Changed t_addr to unsigned

   The PDP-1 was Digital's first computer.  Although Digital built four
   other 18b computers, the later systems (the PDP-4, PDP-7, PDP-9, and
   PDP-15) were similar to each other and quite different from the PDP-1.
   Accordingly, the PDP-1 requires a distinct simulator.

   The register state for the PDP-1 is:

   	AC<0:17>	accumulator
   	IO<0:17>	IO register
   	OV		overflow flag
   	PC<0:15>	program counter
   	IOSTA		I/O status register
	SBS<0:2>	sequence break flip flops
	IOH		I/O halt flip flop
	IOC		I/O completion flip flop
   	EXTM		extend mode
	PF<1:6>		program flags
	SS<1:6>		sense switches
	TW<0:17>	test word (switch register)

   Questions:

	cks: which bits are line printer print done and space done?
	cks: is there a bit for sequence break enabled (yes, according
		to the 1963 Handbook)
	sbs: do sequence breaks accumulate while the system is disabled
		(yes, according to the Maintenance Manual)
*/

/* The PDP-1 has six instruction formats: memory reference, skips,
   shifts, load immediate, I/O transfer, and operate.  The memory
   reference format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |      op      |in|              address              | memory reference
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   <0:4> <5>	mnemonic	action

   00
   02		AND		AC = AC & M[MA]
   04		IOR		AC = AC | M[MA]
   06		XOR		AC = AC ^ M[MA]
   10 		XCT		M[MA] is executed as an instruction
   12
   14
   16	  0	CAL		M[100] = AC, AC = PC, PC = 101
   16     1	JDA		M[MA] = AC, AC = PC, PC = MA + 1
   20		LAC		AC = M[MA]
   22		LIO		IO = M[MA]
   24 		DAC		M[MA] = AC
   26		DAP		M[MA]<6:17> = AC<6:17>
   30 		DIP		M[MA]<0:5> = AC<0:5>
   32		DIO		M[MA] = IO
   34 		DZM		M[MA] = 0
   36
   40		ADD		AC = AC + M[MA]
   42		SUB		AC = AC - M[MA]
   44 		IDX		AC = M[MA] = M[MA] + 1
   46		ISP		AC = M[MA] = M[MA] + 1, skip if AC >= 0
   50		SAD		skip if AC != M[MA]
   52		SAS		skip if AC == M[MA]
   54		MUL		AC'IO = AC * M[MA]
   56		DIV		AC, IO = AC'IO / M[MA]
   60 		JMP		PC = MA
   62		JSP		AC = PC, PC = MA

   Memory reference instructions can access an address space of 64K words.
   The address space is divided into sixteen 4K word fields.  An
   instruction can directly address, via its 12b address, the entire
   current field.  If extend mode is off, indirect addresses access
   the current field, and indirect addressing is multi-level; if off,
   they can access all 64K, and indirect addressing is single level.
*/

/* The skip format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  0  1  0|  |  |  |  |  |  |  |  |  |  |  |  |  | skip
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
		    |     |  |  |  |  | \______/ \______/
		    |     |  |  |  |  |     |        |
		    |     |  |  |  |  |     |        +---- program flags
		    |     |  |  |  |  |     +------------- sense switches
		    |     |  |  |  |  +------------------- AC == 0
		    |     |  |  |  +---------------------- AC >= 0
		    |     |  |  +------------------------- AC < 0
		    |     |  +---------------------------- OV == 0
		    |     +------------------------------- IO >= 0
		    +------------------------------------- invert skip

   The shift format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  0  1  1| subopcode |      encoded count       | shift
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The load immediate format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  0  0| S|           immediate               | LAW
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   <0:4>	mnemonic	action

   70		LAW		if S = 0, AC = IR<6:17>
				else AC = ~IR<6:17>
*/

/* The I/O transfer format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  0  1| W| C|   subopcode  |      device     | I/O transfer
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The IO transfer instruction sends the the specified subopcode to
   specified I/O device.  The I/O device may take data from the IO or
   return data to the IO, initiate or cancel operations, etc.  The
   W bit specifies whether the CPU waits for completion, the C bit
   whether a completion pulse will be returned from the device.

   The operate format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  1  1|  |  |  |  |  |  |  |  |  |  |  |  |  | operate
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
		       |  |  |  |  |  |        |  \______/
		       |  |  |  |  |  |        |     |
		       |  |  |  |  |  |        |     +---- PF select
		       |  |  |  |  |  |        +---------- clear/set PF
		       |  |  |  |  |  +------------------- or PC
		       |  |  |  |  +---------------------- clear AC
		       |  |  |  +------------------------- halt
		       |  |  +---------------------------- CMA
		       |  +------------------------------- or TW
		       +---------------------------------- clear IO

   The operate instruction can be microprogrammed.
*/

/* This routine is the instruction decode routine for the PDP-1.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

	HALT instruction
	breakpoint encountered
	unimplemented instruction and STOP_INST flag set
	XCT loop
	indirect address loop
	infinite wait state
	I/O error in I/O simulator

   2. Interrupts.  With a single channel sequence break system, the
      PDP-1 has a single break request (flop b2, here sbs<SB_V_RQ>).
      If sequence breaks are enabled (flop sbm, here sbs<SB_V_ON>),
      and one is not already in progress (flop b4, here sbs<SB_V_IP>),
      a sequence break occurs.

   3. Arithmetic.  The PDP-1 is a 1's complement system.  In 1's
      complement arithmetic, a negative number is represented by the
      complement (XOR 0777777) of its absolute value.  Addition of 1's
      complement numbers requires propagating the carry out of the high
      order bit back to the low order bit.

   4. Adding I/O devices.  Three modules must be modified:

	pdp1_defs.h	add interrupt request definition
	pdp1_cpu.c	add IOT dispatches
	pdp1_sys.c	add pointer to data structures to sim_devices
*/

#include "pdp1_defs.h"

#define UNIT_V_MDV	(UNIT_V_UF)			/* mul/div */
#define UNIT_MDV	(1 << UNIT_V_MDV)
#define UNIT_V_MSIZE	(UNIT_V_UF+1)			/* dummy mask */
#define UNIT_MSIZE	(1 << UNIT_V_MSIZE)

int32 M[MAXMEMSIZE] = { 0 };				/* memory */
int32 AC = 0;						/* AC */
int32 IO = 0;						/* IO */
int32 PC = 0;						/* PC */
int32 OV = 0;						/* overflow */
int32 SS = 0;						/* sense switches */
int32 PF = 0;						/* program flags */
int32 TW = 0;						/* test word */
int32 iosta = 0;					/* status reg */
int32 sbs = 0;						/* sequence break */
int32 sbs_init = 0;					/* seq break startup */
int32 ioh = 0;						/* I/O halt */
int32 ioc = 0;						/* I/O completion */
int32 extm = 0;						/* ext mem mode */
int32 extm_init = 0;					/* ext mem startup */
int32 stop_inst = 0;					/* stop on rsrv inst */
int32 xct_max = 16;					/* nested XCT limit */
int32 ind_max = 16;					/* nested ind limit */
int32 old_PC = 0;					/* old PC */
extern UNIT *sim_clock_queue;
extern int32 sim_int_char;
extern int32 sim_brk_types, sim_brk_dflt, sim_brk_summ;	/* breakpoint info */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
extern int32 ptr (int32 inst, int32 dev, int32 IO);
extern int32 ptp (int32 inst, int32 dev, int32 IO);
extern int32 tti (int32 inst, int32 dev, int32 IO);
extern int32 tto (int32 inst, int32 dev, int32 IO);
extern int32 lpt (int32 inst, int32 dev, int32 IO);
extern t_stat sim_activate (UNIT *uptr, int32 delay);

int32 sc_map[512] = {
	0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,	/* 00000xxxx */
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,	/* 00001xxxx */
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,	/* 00010xxxx */
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,	/* 00011xxxx */
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,	/* 00100xxxx */
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,	/* 00101xxxx */
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,	/* 00110xxxx */
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,	/* 00111xxxx */
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,	/* 01000xxxx */
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,	/* 01001xxxx */
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,	/* 01010xxxx */
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,	/* 01011xxxx */
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,	/* 01100xxxx */
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,	/* 01101xxxx */
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,	/* 01110xxxx */
	4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8, /* 01111xxxx */
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,	/* 10000xxxx */
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,	/* 10001xxxx */
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,	/* 10010xxxx */
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,	/* 10011xxxx */
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,	/* 10100xxxx */
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,	/* 10101xxxx */
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,	/* 10110xxxx */
	4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8, /* 11011xxxx */
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,	/* 11000xxxx */
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,	/* 11001xxxx */
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,	/* 11010xxxx */
	4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8, /* 11011xxxx */
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,	/* 11100xxxx */
	4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8, /* 11101xxxx */
	4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8, /* 11110xxxx */
	5, 6, 6, 7, 6, 7, 7, 8, 6, 7, 7, 8, 7, 8, 8, 9	/* 11111xxxx */
};

/* CPU data structures

   cpu_dev	CPU device descriptor
   cpu_unit	CPU unit
   cpu_reg	CPU register list
   cpu_mod	CPU modifier list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, MAXMEMSIZE) };

REG cpu_reg[] = {
	{ ORDATA (PC, PC, ASIZE) },
	{ ORDATA (AC, AC, 18) },
	{ ORDATA (IO, IO, 18) },
	{ FLDATA (OV, OV, 0) },
	{ ORDATA (PF, PF, 6) },
	{ ORDATA (SS, SS, 6) },
	{ ORDATA (TW, TW, 18) },
	{ FLDATA (EXTM, extm, 0) },
	{ ORDATA (IOSTA, iosta, 18), REG_RO },
	{ FLDATA (SBON, sbs, SB_V_ON) },
	{ FLDATA (SBRQ, sbs, SB_V_RQ) },
	{ FLDATA (SBIP, sbs, SB_V_IP) },
	{ FLDATA (IOH, ioh, 0) },
	{ FLDATA (IOC, ioc, 0) },
	{ ORDATA (OLDPC, old_PC, ASIZE), REG_RO },
	{ FLDATA (STOP_INST, stop_inst, 0) },
	{ FLDATA (SBS_INIT, sbs_init, SB_V_ON) },
	{ FLDATA (EXTM_INIT, extm_init, 0) },
	{ FLDATA (MDV, cpu_unit.flags, UNIT_V_MDV), REG_HRO },
	{ DRDATA (XCT_MAX, xct_max, 8), PV_LEFT + REG_NZ },
	{ DRDATA (IND_MAX, ind_max, 8), PV_LEFT + REG_NZ },
	{ ORDATA (WRU, sim_int_char, 8) },
	{ NULL }  };

MTAB cpu_mod[] = {
	{ UNIT_MDV, UNIT_MDV, "multiply/divide", "MDV", NULL },
	{ UNIT_MDV, 0, "no multiply/divide", "NOMDV", NULL },
	{ UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
	{ UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
	{ UNIT_MSIZE, 12288, NULL, "12K", &cpu_set_size },
	{ UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
	{ UNIT_MSIZE, 20480, NULL, "20K", &cpu_set_size },
	{ UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
	{ UNIT_MSIZE, 28672, NULL, "28K", &cpu_set_size },
	{ UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
	{ UNIT_MSIZE, 49152, NULL, "48K", &cpu_set_size },
	{ UNIT_MSIZE, 65536, NULL, "64K", &cpu_set_size },
	{ 0 }  };

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 8, ASIZE, 1, 8, 18,
	&cpu_ex, &cpu_dep, &cpu_reset,
	NULL, NULL, NULL };

t_stat sim_instr (void)
{
extern int32 sim_interval;
int32 IR, MA, op, i, t, xct_count;
int32 sign, signd, v;
int32 dev, io_data, sc, skip;
t_stat reason;
static int32 fs_test[8] = {
	0, 040, 020, 010, 04, 02, 01, 077 };

#define EPC_WORD	((OV << 17) | (extm << 16) | PC)
#define INCR_ADDR(x)	(((x) & EPCMASK) | (((x) + 1) & DAMASK))
#define DECR_ADDR(x)	(((x) & EPCMASK) | (((x) - 1) & DAMASK))
#define ABS(x)		((x) ^ (((x) & 0400000)? 0777777: 0))

/* Main instruction fetch/decode loop: check events and interrupts */

reason = 0;
while (reason == 0) {					/* loop until halted */

if (sim_interval <= 0) {				/* check clock queue */
	if (reason = sim_process_event ()) break;  }

if (sbs == (SB_ON | SB_RQ)) {				/* interrupt? */
	sbs = SB_ON | SB_IP;				/* set in prog flag */
	old_PC = PC;					/* save old PC */
	M[0] = AC;					/* save state */
	M[1] = EPC_WORD;
	M[2] = IO;
	PC = 3;						/* fetch next from 3 */
	extm = 0;					/* extend off */
	OV = 0;  }					/* clear overflow */

if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) {	/* breakpoint? */
	reason = STOP_IBKPT;				/* stop simulation */
	break;  }

/* Fetch, decode instruction */

MA = PC;						/* PC to MA */
IR = M[MA];						/* fetch instruction */
PC = INCR_ADDR (PC);					/* increment PC */
xct_count = 0;						/* track nested XCT's */
sim_interval = sim_interval - 1;

xct_instr:						/* label for XCT */
if ((IR == 0610001) && ((MA & EPCMASK) == 0) && (sbs & SB_ON)) {
	sbs = sbs & ~SB_IP;				/* seq debreak */
	old_PC = PC;					/* save old PC */
	OV = (M[1] >> 17) & 1;				/* restore OV */
	extm = (M[1] >> 16) & 1;			/* restore ext mode */
	PC = M[1] & AMASK;				/* JMP I 1 */
	continue;  }

op = ((IR >> 13) & 037);				/* get opcode */
if ((op < 032) && (op != 007)) {			/* mem ref instr */
	MA = (MA & EPCMASK) | (IR & DAMASK);		/* direct address */
	if (IR & IA) {					/* indirect addr? */
		if (extm) MA = M[MA] & AMASK;		/* if ext, one level */
		else {	for (i = 0; i < ind_max; i++) {	/* count indirects */
				t = M[MA];		/* get indirect word */
				MA = (MA & EPCMASK) | (t & DAMASK);
				if ((t & IA) == 0) break;  }
			if (i >= ind_max) {		/* indirect loop? */
				reason = STOP_IND;
				break;  }  }  }  }

switch (op) {						/* decode IR<0:4> */

/* Logical, load, store instructions */

case 001:						/* AND */
	AC = AC & M[MA];
	break;
case 002:						/* IOR */
	AC = AC | M[MA];
	break;
case 003:						/* XOR */
	AC = AC ^ M[MA];
	break;
case 004:						/* XCT */
	if (xct_count >= xct_max) {			/* too many XCT's? */
		reason = STOP_XCT;
		break;  }
	xct_count = xct_count + 1;			/* count XCT's */
	IR = M[MA];					/* get instruction */
	goto xct_instr;					/* go execute */
case 007:						/* CAL, JDA */
	MA = (PC & EPCMASK) | ((IR & IA)? (IR & DAMASK): 0100);
	old_PC = PC;
	M[MA] = AC;
	AC = EPC_WORD;
	PC = INCR_ADDR (MA);
	break;
case 010:						/* LAC */
	AC = M[MA];
	break;
case 011:						/* LIO */
	IO = M[MA];
	break;
case 012:						/* DAC */
	if (MEM_ADDR_OK (MA)) M[MA] = AC;
	break;
case 013:						/* DAP */
	if (MEM_ADDR_OK (MA)) M[MA] = (AC & DAMASK) | (M[MA] & ~DAMASK);
	break;
case 014:						/* DIP */
	if (MEM_ADDR_OK (MA)) M[MA] = (AC & ~DAMASK) | (M[MA] & DAMASK);
	break;
case 015:						/* DIO */
	if (MEM_ADDR_OK (MA)) M[MA] = IO;
	break;
case 016:						/* DZM */
	if (MEM_ADDR_OK (MA)) M[MA] = 0;
	break;

/* Add, subtract, control

   Add is performed in sequential steps, as follows:
	1. add
	2. end around carry propagate
	3. overflow check
	4. -0 cleanup

   Subtract is performed in sequential steps, as follows:
	1. complement AC
	2. add
	3. end around carry propagate
	4. overflow check
	5. complement AC
   Because no -0 check is done, (-0) - (+0) yields a result of -0
*/

case 020:						/* ADD */
	t = AC;
	AC = AC + M[MA];
	if (AC > 0777777) AC = (AC + 1) & 0777777;	/* end around carry */
	if (((~t ^ M[MA]) & (t ^ AC)) & 0400000) OV = 1;
	if (AC == 0777777) AC = 0;			/* minus 0 cleanup */
	break;
case 021:						/* SUB */
	t = AC ^ 0777777;				/* complement AC */
	AC = t + M[MA];					/* -AC + MB */
	if (AC > 0777777) AC = (AC + 1) & 0777777;	/* end around carry */
	if (((~t ^ M[MA]) & (t ^ AC)) & 0400000) OV = 1;
	AC = AC ^ 0777777;				/* recomplement AC */
	break;
case 022:						/* IDX */
	AC = M[MA] + 1;
	if (AC >= 0777777) AC = (AC + 1) & 0777777;
	if (MEM_ADDR_OK (MA)) M[MA] = AC;
	break;
case 023:						/* ISP */
	AC = M[MA] + 1;
	if (AC >= 0777777) AC = (AC + 1) & 0777777;
	if (MEM_ADDR_OK (MA)) M[MA] = AC;
	if (AC < 0400000) PC = INCR_ADDR (PC);
	break;
case 024:						/* SAD */
	if (AC != M[MA]) PC = INCR_ADDR (PC);
	break;
case 025:						/* SAS */
	if (AC == M[MA]) PC = INCR_ADDR (PC);
	break;
case 030:						/* JMP */
	old_PC = PC;
	PC = MA;
	break;
case 031:						/* JSP */
	AC = EPC_WORD;
	old_PC = PC;
	PC = MA;
	break;
case 034:						/* LAW */
	AC = (IR & 07777) ^ ((IR & IA)? 0777777: 0);
	break;

/* Multiply and divide

   Multiply and divide step and hardware multiply are exact implementations.
   Hardware divide is a 2's complement analog to the actual hardware.
*/   

case 026:						/* MUL */
	if (cpu_unit.flags & UNIT_MDV) {		/* hardware? */
		sign = AC ^ M[MA];			/* result sign */
		IO = ABS (AC);				/* IO = |AC| */
		v = ABS (M[MA]);			/* v = |mpy| */
		for (i = AC = 0; i < 17; i++) {
			if (IO & 1) AC = AC + v;
			IO = (IO >> 1) | ((AC & 1) << 17);
			AC = AC >> 1;  }
		if ((sign & 0400000) && (AC | IO)) {	/* negative, > 0? */
			AC = AC ^ 0777777;
			IO = IO ^ 0777777;  }  }
	else {	if (IO & 1) AC = AC + M[MA];		/* multiply step */
		if (AC > 0777777) AC = (AC + 1) & 0777777;
		if (AC == 0777777) AC = 0;
		IO = (IO >> 1) | ((AC & 1) << 17);
		AC = AC >> 1;  }
	break;

case 027:						/* DIV */
	if (cpu_unit.flags & UNIT_MDV) {		/* hardware */
		sign = AC ^ M[MA];			/* result sign */
		signd = AC;				/* remainder sign */
		if (AC & 0400000) {
			AC = AC ^ 0777777;		/* AC'IO = |AC'IO| */
			IO = IO ^ 0777777;  }
		v = ABS (M[MA]);			/* v = |divr| */
		if (AC >= v) break;			/* overflow? */
		for (i = t = 0; i < 18; i++) {
			if (t) AC = (AC + v) & 0777777;
			else AC = (AC - v) & 0777777;
			t = AC >> 17;
			if (i != 17) AC = ((AC << 1) | (IO >> 17)) & 0777777;
			IO = ((IO << 1) | (t ^ 1)) & 0777777;  }
		if (t) AC = (AC + v) & 0777777;		/* correct remainder */
		t = ((signd & 0400000) && AC)? AC ^ 0777777: AC;
		AC = ((sign & 0400000) && IO)? IO ^ 0777777: IO;
		IO = t;
		PC = INCR_ADDR (PC);  }			/* skip */
	else {	t = AC >> 17;				/* divide step */
		AC = ((AC << 1) | (IO >> 17)) & 0777777;
		IO = ((IO << 1) | (t ^ 1)) & 0777777;
		if (IO & 1) AC = AC + (M[MA] ^ 0777777);
		else AC = AC + M[MA] + 1;
		if (AC > 0777777) AC = (AC + 1) & 0777777;
		if (AC == 0777777) AC = 0;  }
	break;

/* Skip and operate 

   Operates execute in the order shown; there are no timing conflicts
*/

case 032:						/* skip */
	v = (IR >> 3) & 07;				/* sense switches */
	t = IR & 07;					/* program flags */
	skip = (((IR & 02000) && (IO < 0400000)) ||	/* SPI */
		((IR & 01000) && (OV == 0)) ||		/* SZO */
		((IR & 00400) && (AC >= 0400000)) ||	/* SMA */
		((IR & 00200) && (AC < 0400000)) ||	/* SPA */
		((IR & 00100) && (AC == 0)) ||		/* SZA */
		(v && ((SS & fs_test[v]) == 0)) ||	/* SZSn */
		(t && ((PF & fs_test[t]) == 0)));	/* SZFn */
	if (IR & IA) skip = skip ^ 1;			/* invert skip? */
	if (skip) PC = INCR_ADDR (PC);
	if (IR & 01000) OV = 0;				/* SOV clears OV */
	break;

case 037:						/* operate */
	if (IR & 04000) IO = 0;				/* CLI */
	if (IR & 00200) AC = 0;				/* CLA */
	if (IR & 02000) AC = AC | TW;			/* LAT */
	if (IR & 00100) AC = AC | EPC_WORD;		/* LAP */
	if (IR & 01000) AC = AC ^ 0777777;		/* CMA */
	if (IR & 00400) reason = STOP_HALT;		/* HALT */
	t = IR & 07;					/* flag select */
	if (IR & 010) PF = PF | fs_test[t];		/* STFn */
	else PF = PF & ~fs_test[t];			/* CLFn */
	break;

/* Shifts */

case 033:
	sc = sc_map[IR & 0777];				/* map shift count */
	switch ((IR >> 9) & 017) {			/* case on IR<5:8> */
	case 001:					/* RAL */
		AC = ((AC << sc) | (AC >> (18 - sc))) & 0777777;
		break;
	case 002:					/* RIL */
		IO = ((IO << sc) | (IO >> (18 - sc))) & 0777777;
		break;
	case 003:					/* RCL */
		t = AC;
		AC = ((AC << sc) | (IO >> (18 - sc))) & 0777777;
		IO = ((IO << sc) | (t >> (18 - sc))) & 0777777;
		break;
	case 005:					/* SAL */
		t = (AC & 0400000)? 0777777: 0;
		AC = (AC & 0400000) | ((AC << sc) & 0377777) |
			(t >> (18 - sc));
		break;
	case 006:					/* SIL */
		t = (IO & 0400000)? 0777777: 0;
		IO = (IO & 0400000) | ((IO << sc) & 0377777) |
			(t >> (18 - sc));
		break;
	case 007:					/* SCL */
		t = (AC & 0400000)? 0777777: 0;
		AC = (AC & 0400000) | ((AC << sc) & 0377777) | 
			(IO >> (18 - sc));
		IO = ((IO << sc) | (t >> (18 - sc))) & 0777777;
		break;
	case 011:					/* RAR */
		AC = ((AC >> sc) | (AC << (18 - sc))) & 0777777;
		break;
	case 012:					/* RIR */
		IO = ((IO >> sc) | (IO << (18 - sc))) & 0777777;
		break;
	case 013:					/* RCR */
		t = IO;
		IO = ((IO >> sc) | (AC << (18 - sc))) & 0777777;
		AC = ((AC >> sc) | (t << (18 - sc))) & 0777777;
		break;
	case 015:					/* SAR */
		t = (AC & 0400000)? 0777777: 0;
		AC = ((AC >> sc) | (t << (18 - sc))) & 0777777;
		break;
	case 016:					/* SIR */
		t = (IO & 0400000)? 0777777: 0;
		IO = ((IO >> sc) | (t << (18 - sc))) & 0777777;
		break;
	case 017:					/* SCR */
		t = (AC & 0400000)? 0777777: 0;
		IO = ((IO >> sc) | (AC << (18 - sc))) & 0777777;
		AC = ((AC >> sc) | (t << (18 - sc))) & 0777777;
		break;
	default:					/* undefined */
		reason = stop_inst;
		break;  }				/* end switch shifts */
	break;

/* IOT */

case 035:
	if (IR & IO_WAIT) {				/* wait? */
		if (ioh) {				/* I/O halt? */
			if (ioc) ioh = 0;		/* comp pulse? done */
			else {	sim_interval = 0;	/* force event */
				PC = DECR_ADDR (PC);  }	/* re-execute */
			break;  }			/* skip iot */
		ioh = 1;				/* turn on halt */
		PC = DECR_ADDR (PC);  }			/* re-execute */
	dev = IR & 077;					/* get dev addr */
	io_data = IO;					/* default data */
	switch (dev) {					/* case on dev */
	case 000:					/* I/O wait */
		break;
	case 001: case 002: case 030:			/* paper tape rdr */
		io_data = ptr (IR, dev, IO);
		break;
	case 003:					/* typewriter */
		io_data = tto (IR, dev, IO);
		break;
	case 004:					/* keyboard */
		io_data = tti (IR, dev, IO);
		break;
	case 005: case 006:				/* paper tape punch */
		io_data = ptp (IR, dev, IO);
		break;
	case 033:					/* check status */
		io_data = iosta | ((sbs & SB_ON)? IOS_SQB: 0);
		break;
	case 045:					/* line printer */
		io_data = lpt (IR, dev, IO);
		break;
	case 054:					/* seq brk off */
		sbs = sbs & ~SB_ON;
		break;
	case 055:					/* seq brk on */
		sbs = sbs | SB_ON;
		break;
	case 056:					/* clear seq brk */
		sbs = sbs & ~SB_IP;
		break;
	case 074:					/* extend mode */
		extm = (IR >> 11) & 1;			/* set from IR<6> */
		break;
	default:					/* undefined */
		reason = stop_inst;
		break;  }				/* end switch dev */
	IO = io_data & 0777777;
	if (io_data >= IOT_REASON) reason = io_data >> IOT_V_REASON;
	break;
default:						/* undefined */
	reason = STOP_RSRV;				/* halt */
	break;  }					/* end switch opcode */
}							/* end while */
return reason;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
sbs = sbs_init;
extm = extm_init;
ioh = ioc = 0;
OV = 0;
PF = 0;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE) return SCPE_NXM;
if (vptr != NULL) *vptr = M[addr] & 0777777;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE) return SCPE_NXM;
M[addr] = val & 0777777;
return SCPE_OK;
}

/* Change memory size */

t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 mc = 0;
t_addr i;

if ((val <= 0) || (val > MAXMEMSIZE) || ((val & 07777) != 0))
	return SCPE_ARG;
for (i = val; i < MEMSIZE; i++) mc = mc | M[i];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
	return SCPE_OK;
MEMSIZE = val;
for (i = MEMSIZE; i < MAXMEMSIZE; i++) M[i] = 0;
return SCPE_OK;
}
