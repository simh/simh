/* nova_cpu.c: NOVA CPU simulator

   Copyright (c) 1993-2003, Robert M. Supnik

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

   cpu		Nova central processor

   19-Jan-03	RMS	Changed CMASK to CDMASK for Apple Dev Kit conflict
   03-Oct-02	RMS	Added DIB infrastructure
   30-Dec-01	RMS	Added old PC queue
   07-Dec-01	RMS	Revised to use breakpoint package
   30-Nov-01	RMS	Added extended SET/SHOW support
   10-Aug-01	RMS	Removed register in declarations
   17-Jul-01	RMS	Moved function prototype
   26-Apr-01	RMS	Added device enable/disable support
   05-Mar-01	RMS	Added clock calibration
   22-Dec-00	RMS	Added Bruce Ray's second terminal
   15-Dec-00	RMS	Added Charles Owen's CPU bootstrap
   08-Dec-00	RMS	Changes from Bruce Ray
			-- fixed trap test to include Nova 3
			-- fixed DIV and DIVS divide by 0
			-- fixed RETN to set SP from FP
			-- fixed IORST to preserve carry
			-- added "secret" Nova 4 PSHN/SAVEN instructions
			-- added plotter support
   15-Oct-00	RMS	Fixed bug in MDV test, added stack, byte, trap instructions
   14-Apr-98	RMS	Changed t_addr to unsigned
   15-Sep-97	RMS	Added read and write breakpoints

   The register state for the NOVA CPU is:

   AC[0:3]<0:15>	general registers
   C			carry flag
   PC<0:14>		program counter
   
   The NOVA has three instruction formats: memory reference, I/O transfer,
   and operate.  The memory reference format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0| op  | AC  |in| mode|     displacement      |	memory reference
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   <0:4>	mnemonic	action

   00000	JMP		PC = MA
   00001	JMS		AC3 = PC, PC = MA
   00010	ISZ		M[MA] = M[MA] + 1, skip if M[MA] == 0
   00011	DSZ		M[MA] = M[MA] - 1, skip if M[MA] == 0
   001'n	LDA		ACn = M[MA]
   010'n	STA		M[MA] = ACn

   <5:7>	mode		action

   000	page zero direct	MA = zext (IR<8:15>)
   001	PC relative direct	MA = PC + sext (IR<8:15>)
   010	AC2 relative direct	MA = AC2 + sext (IR<8:15>)
   011	AC3 relative direct	MA = AC3 + sext (IR<8:15>)
   100	page zero indirect	MA = M[zext (IR<8:15>)]
   101	PC relative indirect	MA = M[PC + sext (IR<8:15>)]
   110	AC2 relative indirect	MA = M[AC2 + sext (IR<8:15>)]
   111	AC3 relative indirect	MA = M[AC3 + sext (IR<8:15>)]

   Memory reference instructions can access an address space of 32K words.
   An instruction can directly reference the first 256 words of memory
   (called page zero), as well as 256 words relative to the PC, AC2, or
   AC3; it can indirectly access all 32K words.  If an indirect address
   is in locations 00020-00027, the indirect address is incremented and
   rewritten to memory before use; if in 00030-00037, decremented and
   rewritten.
*/

/*  The I/O transfer format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0  1  1| AC  | opcode |pulse|      device     |	I/O transfer
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The IOT instruction sends the opcode, pulse, and specified AC to the
   specified I/O device.  The device may accept data, provide data,
   initiate or cancel operations, or skip on status.

   The operate format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1|srcAC|dstAC| opcode |shift|carry|nl|  skip  |	operate
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                   \______/ \___/ \___/  |  |  |  |
		       |      |     |    |  |  |  +--- reverse skip sense
		       |      |     |    |  |  +--- skip if C == 0
		       |      |     |    |  +--- skip if result == 0
		       |      |     |    +--- don't load result
		       |      |     +--- carry in (load as is,
		       |      |			   set to Zero,
		       |      |			   set to One,
		       |      |			   load Complement)
		       |      +--- shift (none,
		       |		  left one,
		       |		  right one,
		       |		  byte swap)
		       +--- operation (complement,
				       negate,
				       move,
				       increment,
				       add complement,
				       subtract,
				       add,
				       and)

   The operate instruction can be microprogrammed to perform operations
   on the source and destination AC's and the Carry flag.

   Some notes from Bruce Ray:

   1.	DG uses the value of the autoindex location -before- the
	modification to determine if additional indirect address
	levels are to be performed.  Most DG emulators conform to
	this standard, but some vendor machines (i.e. Point 4 Mark 8)
	do not.

   2.	Infinite indirect references may occur on unmapped systems
	and can "hang" the hardware.  Some DG diagnostics perform
	10,000s of references during a single instruction.

   3.	Nova 3 adds the following instructions to the standard Nova
	instruction set:

	trap instructions
	stack push/pop instructions
	save/return instructions
	stack register manipulation instructions
	unsigned MUL/DIV

    4.	Nova 4 adds the following instructions to the Nova 3 instruction
	set:

	signed MUL/DIV
	load/store byte
	secret (undocumented) stack instructions [PSHN, SAVN]

    5.	Nova, Nova 3 and Nova 4 unsigned mul/div instructions are the
	same instruction code values on all machines.
*/

/* This routine is the instruction decode routine for the NOVA.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

	HALT instruction
	breakpoint encountered
	infinite indirection loop
	unknown I/O device and STOP_DEV flag set
	I/O error in I/O simulator

   2. Interrupts.  Interrupts are maintained by four parallel variables:

	dev_done 	device done flags
	dev_disable	device interrupt disable flags
	dev_busy	device busy flags
	int_req		interrupt requests

      In addition, int_req contains the interrupt enable and ION pending
      flags.  If ION and ION pending are set, and at least one interrupt
      request is pending, then an interrupt occurs.  Note that the 16b PIO
      mask must be mapped to the simulator's device bit mapping.
 
   3. Non-existent memory.  On the NOVA, reads to non-existent memory
      return zero, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to zero.
      Thus, only writes need be checked against actual memory size.

   4. Adding I/O devices.  These modules must be modified:

	nova_defs.h	add interrupt request definition
	nova_sys.c	add sim_devices entry
*/

#include "nova_defs.h"

#define PCQ_SIZE	64				/* must be 2**n */
#define PCQ_MASK	(PCQ_SIZE - 1)
#define PCQ_ENTRY	pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = PC

#define INCA(x)		(((x) + 1) & AMASK)
#define DECA(x)		(((x) - 1) & AMASK)
#define SEXT(x)		(((x) & SIGN)? ((x) | ~DMASK): (x))
#define STK_CHECK(x,y)	if (((x) & 0377) < (y)) int_req = int_req | INT_STK
#define IND_STEP(x)	M[x] & A_IND; \
			if (((x) & 077770) == AUTO_INC) \
				M[x] = (M[x] + 1) & 0177777; \
			else if (((x) & 077770) == AUTO_DEC) \
				M[x] = (M[x] - 1) & 0177777; \
			x = M[x] & AMASK

#define UNIT_V_MDV	(UNIT_V_UF + 0)			/* MDV present */
#define UNIT_V_STK	(UNIT_V_UF + 1)			/* stack instr */
#define UNIT_V_BYT	(UNIT_V_UF + 2)			/* byte instr */
#define UNIT_V_MSIZE	(UNIT_V_UF + 3)			/* dummy mask */
#define UNIT_MDV	(1 << UNIT_V_MDV)
#define UNIT_STK	(1 << UNIT_V_STK)
#define UNIT_BYT	(1 << UNIT_V_BYT)
#define UNIT_MSIZE	(1 << UNIT_V_MSIZE)
#define UNIT_IOPT	(UNIT_MDV | UNIT_STK | UNIT_BYT)
#define UNIT_NOVA3	(UNIT_MDV | UNIT_STK)
#define UNIT_NOVA4	(UNIT_MDV | UNIT_STK | UNIT_BYT)

uint16 M[MAXMEMSIZE] = { 0 };				/* memory */
int32 AC[4] = { 0 };					/* accumulators */
int32 C = 0;						/* carry flag */
int32 saved_PC = 0;					/* program counter */
int32 SP = 0;						/* stack pointer */
int32 FP = 0;						/* frame pointer */
int32 SR = 0;						/* switch register */
int32 dev_done = 0;					/* device done flags */
int32 dev_busy = 0;					/* device busy flags */
int32 dev_disable = 0;					/* int disable flags */
int32 int_req = 0;					/* interrupt requests */
int32 pimask = 0;					/* priority int mask */
int32 pwr_low = 0;					/* power fail flag */
int32 ind_max = 16;					/* iadr nest limit */
int32 stop_dev = 0;					/* stop on ill dev */
uint16 pcq[PCQ_SIZE] = { 0 };				/* PC queue */
int32 pcq_p = 0;					/* PC queue ptr */
REG *pcq_r = NULL;					/* PC queue reg ptr */
struct ndev dev_table[64];				/* dispatch table */

extern int32 sim_int_char;
extern int32 sim_brk_types, sim_brk_dflt, sim_brk_summ;	/* breakpoint info */
extern DEVICE *sim_devices[];

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_boot (int32 unitno, DEVICE *dptr);
t_stat build_devtab (void);

/* CPU data structures

   cpu_dev	CPU device descriptor
   cpu_unit	CPU unit descriptor
   cpu_reg	CPU register list
   cpu_mod	CPU modifiers list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK + UNIT_MDV,
		MAXMEMSIZE) };

REG cpu_reg[] = {
	{ ORDATA (PC, saved_PC, 15) },
	{ ORDATA (AC0, AC[0], 16) },
	{ ORDATA (AC1, AC[1], 16) },
	{ ORDATA (AC2, AC[2], 16) },
	{ ORDATA (AC3, AC[3], 16) },
	{ FLDATA (C, C, 16) },
	{ ORDATA (SP, SP, 16) },
	{ ORDATA (FP, FP, 16) },
	{ ORDATA (SR, SR, 16) },
	{ ORDATA (PI, pimask, 16) },
	{ FLDATA (ION, int_req, INT_V_ION) },
	{ FLDATA (ION_DELAY, int_req, INT_V_NO_ION_PENDING) },
	{ FLDATA (STKOVF, int_req, INT_V_STK) },
	{ FLDATA (PWR, pwr_low, 0) },
	{ ORDATA (INT, int_req, INT_V_ION+1), REG_RO },
	{ ORDATA (BUSY, dev_busy, INT_V_ION+1), REG_RO },
	{ ORDATA (DONE, dev_done, INT_V_ION+1), REG_RO },
	{ ORDATA (DISABLE, dev_disable, INT_V_ION+1), REG_RO },
	{ FLDATA (STOP_DEV, stop_dev, 0) },
	{ DRDATA (INDMAX, ind_max, 16), REG_NZ + PV_LEFT },
	{ BRDATA (PCQ, pcq, 8, 16, PCQ_SIZE), REG_RO+REG_CIRC },
	{ ORDATA (PCQP, pcq_p, 6), REG_HRO },
	{ ORDATA (WRU, sim_int_char, 8) },
	{ NULL }  };

MTAB cpu_mod[] = {
	{ UNIT_IOPT, UNIT_NOVA3, "NOVA3", "NOVA3", NULL },
	{ UNIT_IOPT, UNIT_NOVA4, "NOVA4", "NOVA4", NULL },
	{ UNIT_IOPT, UNIT_MDV, "MDV", "MDV", NULL },
	{ UNIT_IOPT, 0, "none", "NONE", NULL },
	{ UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
	{ UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
	{ UNIT_MSIZE, 12288, NULL, "12K", &cpu_set_size },
	{ UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
	{ UNIT_MSIZE, 20480, NULL, "20K", &cpu_set_size },
	{ UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
	{ UNIT_MSIZE, 28672, NULL, "28K", &cpu_set_size },
	{ UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
	{ 0 }  };

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 8, 15, 1, 8, 16,
	&cpu_ex, &cpu_dep, &cpu_reset,
	NULL, NULL, NULL };

t_stat sim_instr (void)
{
extern int32 sim_interval;
int32 PC, IR, i;
t_stat reason;
void mask_out (int32 mask);
extern int32 clk_sel, clk_time[4];

/* Restore register state */

if (build_devtab () != SCPE_OK) return SCPE_IERR;	/* build dispatch */
PC = saved_PC & AMASK;					/* load local PC */
C = C & CBIT;
mask_out (pimask);					/* reset int system */
reason = 0;
sim_rtc_init (clk_time[clk_sel]);			/* init calibration */

/* Main instruction fetch/decode loop */

while (reason == 0) {					/* loop until halted */
if (sim_interval <= 0) {				/* check clock queue */
	if (reason = sim_process_event ()) break;  }

if (int_req > INT_PENDING) {				/* interrupt? */
	int32 MA, indf;
	int_req = int_req & ~INT_ION;			/* intr off */
	PCQ_ENTRY;					/* save old PC */
	M[INT_SAV] = PC;
	if (int_req & INT_STK) {			/* stack overflow? */
	    int_req = int_req & ~INT_STK;		/* clear */
	    MA = STK_JMP;  }				/* jmp @3 */
	else MA = INT_JMP;				/* intr: jmp @1 */
	for (i = 0, indf = 1; indf && (i < ind_max); i++) {
	    indf = IND_STEP (MA);  }			/* indirect loop */
	if (i >= ind_max) {
	    reason = STOP_IND_INT;
	    break;  }
	PC = MA;  }					/* end interrupt */

if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) {	/* breakpoint? */
	reason = STOP_IBKPT;				/* stop simulation */
	break;  }

IR = M[PC];						/* fetch instr */
PC = (PC + 1) & AMASK;
int_req = int_req | INT_NO_ION_PENDING;			/* clear ION delay */
sim_interval = sim_interval - 1;

/* Operate instruction */

if (IR & I_OPR) {					/* operate? */
	int32 src, srcAC, dstAC;
	srcAC = I_GETSRC (IR);				/* get reg decodes */
	dstAC = I_GETDST (IR);
	switch (I_GETCRY (IR)) {			/* decode carry */
	case 0:						/* load */
	    src = AC[srcAC] | C;
	    break;
	case 1:						/* clear */
	    src = AC[srcAC];
	    break;
	case 2:						/* set */
	    src = AC[srcAC] | CBIT;
	    break;
	case 3:						/* complement */
	    src = AC[srcAC] | (C ^ CBIT);
	    break;  }					/* end switch carry */
	switch (I_GETALU (IR)) {			/* decode ALU */
	case 0:						/* COM */
	    src = src ^ DMASK;
	    break;
	case 1:						/* NEG */
	    src = ((src ^ DMASK) + 1) & CDMASK;
	    break;
	case 2:						/* MOV */
	    break;
	case 3:						/* INC */
	    src = (src + 1) & CDMASK;
	    break;
	case 4:						/* ADC */
	    src = ((src ^ DMASK) + AC[dstAC]) & CDMASK;
	    break;
	case 5:						/* SUB */
	    src = ((src ^ DMASK) + AC[dstAC] + 1) & CDMASK;
	    break;
	case 6:						/* ADD */
	    src = (src + AC[dstAC]) & CDMASK;
	    break;
	case 7:						/* AND */
	    src = src & (AC[dstAC] | CBIT);
	    break;  }					/* end switch oper */

/* Operate, continued */

	switch (I_GETSHF (IR)) {			/* decode shift */
	case 0:						/* nop */
	    break;
	case 1:						/* L */
	    src = ((src << 1) | (src >> 16)) & CDMASK;
	    break;
	case 2:						/* R */
	    src = ((src >> 1) | (src << 16)) & CDMASK;
	    break;
	case 3:						/* S */
	    src = ((src & 0377) << 8) | ((src >> 8) & 0377) |
		(src & CBIT);
	    break;  }					/* end switch shift */
	switch (I_GETSKP (IR)) {			/* decode skip */
	case 0:						/* nop */
	    if ((IR & I_NLD) && (cpu_unit.flags & UNIT_STK)) {
		int32 indf, MA;				/* Nova 3 or 4 trap */
		PCQ_ENTRY;				/* save old PC */
		M[TRP_SAV] = (PC - 1) & AMASK;
		MA = TRP_JMP;				/* jmp @47 */
		for (i = 0, indf = 1; indf && (i < ind_max); i++) {
		    indf = IND_STEP (MA);  }		/* resolve ind */
		if (i >= ind_max) {			/* indirect loop? */
		    reason = STOP_IND_TRP;
		    break;  }
		PC = MA;				/* new PC */
		break;  }
	    break;
	case 1:						/* SKP */
	    PC = (PC + 1) & AMASK;
	    break;
	case 2: 					/* SZC */
	    if (src < CBIT) PC = (PC + 1) & AMASK;
	    break;
	case 3:						/* SNC */
	    if (src >= CBIT) PC = (PC + 1) & AMASK;
	    break;
	case 4:						/* SZR */
	    if ((src & DMASK) == 0) PC = (PC + 1) & AMASK;
	    break;
	case 5:						/* SNR */
	    if ((src & DMASK) != 0) PC = (PC + 1) & AMASK;
	    break;
	case 6:						/* SEZ */
	    if (src <= CBIT) PC = (PC + 1) & AMASK;
	    break;
	case 7:						/* SBN */
	    if (src > CBIT) PC = (PC + 1) & AMASK;
	    break;  }					/* end switch skip */
	if ((IR & I_NLD) == 0) {			/* load? */
	    AC[dstAC] = src & DMASK;
	    C = src & CBIT;  }				/* end if load */
	}						/* end if operate */

/* Memory reference instructions */

else if (IR < 060000) {					/* mem ref? */
	int32 src, MA, indf;
	MA = I_GETDISP (IR);				/* get disp */
	switch (I_GETMODE (IR)) {			/* decode mode */
	case 0:						/* page zero */
	    break;
	case 1:						/* PC relative */
	    if (MA & DISPSIGN) MA = 077400 | MA;
	    MA = (MA + PC - 1) & AMASK;
	    break;
	case 2:						/* AC2 relative */
	    if (MA & DISPSIGN) MA = 077400 | MA;
	    MA = (MA + AC[2]) & AMASK;
	    break;
	case 3:						/* AC3 relative */
	    if (MA & DISPSIGN) MA = 077400 | MA;
	    MA = (MA + AC[3]) & AMASK;
	    break;  }					/* end switch mode */

	if (indf = IR & I_IND) {			/* indirect? */
	    for (i = 0; indf && (i < ind_max); i++) {	/* count */
		indf = IND_STEP (MA);  }		/* resolve indirect */
	    if (i >= ind_max) {				/* too many? */
		reason = STOP_IND;
		break;  }  }

/* Memory reference, continued */

	switch (I_GETOPAC (IR)) {			/* decode op + AC */
	case 001:					/* JSR */
	    AC[3] = PC;
	case 000:					/* JMP */
	    PCQ_ENTRY;
	    PC = MA;
	    break;
	case 002:					/* ISZ */
	    src = (M[MA] + 1) & DMASK;
	    if (MEM_ADDR_OK (MA)) M[MA] = src;
	    if (src == 0) PC = (PC + 1) & AMASK;
	    break;
	case 003:					/* DSZ */
	    src = (M[MA] - 1) & DMASK;
	    if (MEM_ADDR_OK (MA)) M[MA] = src;
	    if (src == 0) PC = (PC + 1) & AMASK;
	    break;
	case 004:					/* LDA 0 */
	    AC[0] = M[MA];
	    break;
	case 005:					/* LDA 1 */
	    AC[1] = M[MA];
	    break;
	case 006:					/* LDA 2 */
	    AC[2] = M[MA];
	    break;
	case 007:					/* LDA 3 */
	    AC[3] = M[MA];
	    break;
	case 010:					/* STA 0 */
	    if (MEM_ADDR_OK (MA)) M[MA] = AC[0];
	    break;
	case 011:					/* STA 1 */
	    if (MEM_ADDR_OK (MA)) M[MA] = AC[1];
	    break;
	case 012:					/* STA 2 */
	    if (MEM_ADDR_OK (MA)) M[MA] = AC[2];
	    break;
	case 013:					/* STA 3 */
	    if (MEM_ADDR_OK (MA)) M[MA] = AC[3];
	    break;  }					/* end switch */
	}						/* end mem ref */

/* IOT instruction */

else {							/* IOT */
	int32 dstAC, pulse, code, device, iodata;
	dstAC = I_GETDST (IR);				/* decode fields */
	code = I_GETIOT (IR);
	pulse = I_GETPULSE (IR);
	device = I_GETDEV (IR);
	if (code == ioSKP) {				/* IO skip? */
	    switch (pulse) {				/* decode IR<8:9> */
	    case 0:					/* skip if busy */
		if ((device == DEV_CPU)? (int_req & INT_ION) != 0:
		    (dev_busy & dev_table[device].mask) != 0)
		    PC = (PC + 1) & AMASK;
		break;
	    case 1:					/* skip if not busy */
		if ((device == DEV_CPU)? (int_req & INT_ION) == 0:
		    (dev_busy & dev_table[device].mask) == 0)
		    PC = (PC + 1) & AMASK;
		break;
	    case 2:					/* skip if done */
		if ((device == DEV_CPU)? pwr_low != 0:
		    (dev_done & dev_table[device].mask) != 0)
		    PC = (PC + 1) & AMASK;
		break;
	    case 3:					/* skip if not done */
		if ((device == DEV_CPU)? pwr_low == 0:
		    (dev_done & dev_table[device].mask) == 0)
		    PC = (PC + 1) & AMASK;
		break;  }				/* end switch */
	    }						/* end IO skip */

/* IOT, continued */

	else if (device == DEV_MDV) {
	    switch (code) {				/* case on opcode */
	    case ioNIO:					/* frame ptr */
		if (cpu_unit.flags & UNIT_STK) {
		    if (pulse == iopN) FP = AC[dstAC] & AMASK;
		    if (pulse == iopC) AC[dstAC] = FP;  }
		break;
	    case ioDIA:					/* load byte */
		if (cpu_unit.flags & UNIT_BYT)
		    AC[dstAC] = (M[AC[pulse] >> 1] >>
			 ((AC[pulse] & 1)? 0: 8)) & 0377;
		else AC[dstAC] = 0;
		break;
	    case ioDOA:					/* stack ptr */
		if (cpu_unit.flags & UNIT_STK) {
		    if (pulse == iopN) SP = AC[dstAC] & AMASK;
		    if (pulse == iopC) AC[dstAC] = SP;  }
		break;
	    case ioDIB:					/* push, pop */
		if (cpu_unit.flags & UNIT_STK) {
		    if (pulse == iopN) {		/* push */
			SP = INCA (SP);
			if (MEM_ADDR_OK (SP)) M[SP] = AC[dstAC];
			STK_CHECK (SP, 1);  }
		    if (pulse == iopC) {		/* pop */
			AC[dstAC] = M[SP];
			SP = DECA (SP);  }
		    if ((pulse == iopP) &&		/* Nova 4 pshn */
			(cpu_unit.flags & UNIT_BYT)) {
			SP = INCA (SP);
			if (MEM_ADDR_OK (SP)) M[SP] = AC[dstAC];
			if (SP > M[042]) int_req = int_req | INT_STK ;
			}
		    }
		break;
	    case ioDOB:					/* store byte */
		if (cpu_unit.flags & UNIT_BYT) {
		    int32 MA, val;
		    MA = AC[pulse] >> 1;
		    val = AC[dstAC] & 0377;
		    if (MEM_ADDR_OK (MA)) M[MA] = (AC[pulse] & 1)?
			((M[MA] & ~0377) | val):
			((M[MA] & 0377) | (val << 8));  }
		break;
	    case ioDIC:					/* save, return */
		if (cpu_unit.flags & UNIT_STK) {
		    if (pulse == iopN) {		/* save */
			SP = INCA (SP);
			if (MEM_ADDR_OK (SP)) M[SP] = AC[0];
			SP = INCA (SP);
			if (MEM_ADDR_OK (SP)) M[SP] = AC[1];
			SP = INCA (SP);
			if (MEM_ADDR_OK (SP)) M[SP] = AC[2];
			SP = INCA (SP);
			if (MEM_ADDR_OK (SP)) M[SP] = FP;
			SP = INCA (SP);
			if (MEM_ADDR_OK (SP)) M[SP] = (C >> 1) |
			    (AC[3] & AMASK);
			AC[3] = FP = SP & AMASK;  
			STK_CHECK (SP, 5);  }
		    if (pulse == iopC) {		/* retn */
			PCQ_ENTRY;
			SP = FP & AMASK;
			C = (M[SP] << 1) & CBIT;
			PC = M[SP] & AMASK;
			SP = DECA (SP);
			AC[3] = M[SP];
			SP = DECA (SP);
			AC[2] = M[SP];
			SP = DECA (SP);
			AC[1] = M[SP];
			SP = DECA (SP);
			AC[0] = M[SP];
			SP = DECA (SP);
			FP = AC[3] & AMASK;  }
		    if ((pulse == iopP) &&		/* Nova 4 saven */
			(cpu_unit.flags & UNIT_BYT)) {
			int32 frameSz = M[PC] ;
			PC = INCA (PC) ;
			SP = INCA (SP);
			if (MEM_ADDR_OK (SP)) M[SP] = AC[0];
			SP = INCA (SP);
			if (MEM_ADDR_OK (SP)) M[SP] = AC[1];
			SP = INCA (SP);
			if (MEM_ADDR_OK (SP)) M[SP] = AC[2];
			SP = INCA (SP);
			if (MEM_ADDR_OK (SP)) M[SP] = FP;
			SP = INCA (SP);
			if (MEM_ADDR_OK (SP)) M[SP] = (C >> 1) |
			    (AC[3] & AMASK);
			AC[3] = FP = SP & AMASK ;
			SP = (SP + frameSz) & AMASK ;
			if (SP > M[042]) int_req = int_req | INT_STK;
			}
		    }
		break;
	    case ioDOC:
		if ((dstAC == 2) && (cpu_unit.flags & UNIT_MDV)) {
		    uint32 mddata, uAC0, uAC1, uAC2;
		    uAC0 = (uint32) AC[0];
		    uAC1 = (uint32) AC[1];
		    uAC2 = (uint32) AC[2];
		    if (pulse == iopP) {		/* mul */
			mddata = (uAC1 * uAC2) + uAC0;
			AC[0] = (mddata >> 16) & DMASK;
			AC[1] = mddata & DMASK;  }
		    if (pulse == iopS) {		/* div */
			if ((uAC0 >= uAC2) || (uAC2 == 0)) C = CBIT;
			else {
			    C = 0;
			    mddata = (uAC0 << 16) | uAC1;
			    AC[1] = mddata / uAC2;
			    AC[0] = mddata % uAC2;  }  }  }
		if ((dstAC == 3) && (cpu_unit.flags & UNIT_BYT)) {
		    int32 mddata;
		    if (pulse == iopC) {		/* muls */
			mddata = (SEXT (AC[1]) * SEXT (AC[2])) + SEXT (AC[0]);
			AC[0] = (mddata >> 16) & DMASK;
			AC[1] = mddata & DMASK;  }
		    if (pulse == iopN) {		/* divs */
			if (AC[2] == 0) C = CBIT;
			else {
			    mddata = (SEXT (AC[0]) << 16) | AC[1];
			    AC[1] = mddata / SEXT (AC[2]);
			    AC[0] = mddata % SEXT (AC[2]);
			    if ((AC[1] > 077777) || (AC[1] < -0100000))
				C = CBIT;
			    else C = 0;
			    AC[0] = AC[0] & DMASK;  }  }  }
		break;  }				/* end case code */
	    }						/* end if mul/div */

/* IOT, continued */

	else if (device == DEV_CPU) {			/* CPU control */
	    switch (code) {				/* decode IR<5:7> */
	    case ioDIA:					/* read switches */
		AC[dstAC] = SR;
		break;
	    case ioDIB:					/* int ack */
		AC[dstAC] = 0;
		int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
		iodata = int_req & (-int_req);
		for (i = DEV_LOW; i <= DEV_HIGH; i++)  {
		    if (iodata & dev_table[i].mask) {
			AC[dstAC] = i; break;   }  }
		break;
	    case ioDOB:					/* mask out */
		mask_out (pimask = AC[dstAC]);
		break;
	    case ioDIC:					/* io reset */
		reset_all (0);				/* reset devices */
		break;
	    case ioDOC:					/* halt */
		reason = STOP_HALT;
		break;  }				/* end switch code */
	    switch (pulse) {				/* decode IR<8:9> */
	    case iopS:					/* ion */
		int_req = (int_req | INT_ION) & ~INT_NO_ION_PENDING;
		break;
	    case iopC:					/* iof */
		int_req = int_req & ~INT_ION;
		break;  }				/* end switch pulse */
	    }						/* end CPU control */
	else if (dev_table[device].routine) {		/* normal device */
	    iodata = dev_table[device].routine (pulse, code, AC[dstAC]);
	    reason = iodata >> IOT_V_REASON;
	    if (code & 1) AC[dstAC] = iodata & 0177777;  }
	else reason = stop_dev;
	}						/* end if IOT */
}							/* end while */

/* Simulation halted */

saved_PC = PC;
pcq_r->qptr = pcq_p;					/* update pc q ptr */
return reason;
}

/* New priority mask out */

void mask_out (int32 newmask)
{
int32 i;

dev_disable = 0;
for (i = DEV_LOW; i <= DEV_HIGH; i++)  {
	if (newmask & dev_table[i].pi)
	    dev_disable = dev_disable | dev_table[i].mask;  }
int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
return;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
int_req = int_req & ~(INT_ION | INT_STK);
pimask = 0;
dev_disable = 0;
pwr_low = 0;
pcq_r = find_reg ("PCQ", NULL, dptr);
if (pcq_r) pcq_r->qptr = 0;
else return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE) return SCPE_NXM;
if (vptr != NULL) *vptr = M[addr] & DMASK;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE) return SCPE_NXM;
M[addr] = val & DMASK;
return SCPE_OK;
}

/* Alter memory size */

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

/* Build dispatch table */

t_stat build_devtab (void)
{
DEVICE *dptr;
DIB *dibp;
int32 i, dn;

for (i = 0; i < 64; i++) {				/* clr dev_table */
	dev_table[i].mask = 0;
	dev_table[i].pi = 0;
	dev_table[i].routine = NULL;  }
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {	/* loop thru dev */
	if (dibp = (DIB *) dptr->ctxt) {		/* get DIB */
	    dn = dibp->dnum;				/* get dev num */
	    dev_table[dn].mask = dibp->mask;		/* copy entries */
	    dev_table[dn].pi = dibp->pi;
	    dev_table[dn].routine = dibp->routine;  }  }
return SCPE_OK;
}

/* Bootstrap routine for CPU */

#define BOOT_START 00000
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int))

static const int32 boot_rom[] = {
	0062677,	/* 	IORST		;reset all I/O  */
	0060477,	/* 	READS 0		;read SR into AC0 */
	0024026,	/*	LDA 1,C77	;get dev mask */
	0107400,	/*	AND 0,1		;isolate dev code */
	0124000,	/*	COM 1,1		;- device code - 1 */
	0010014,	/* LOOP: ISZ OP1	;device code to all */
	0010030,	/*	ISZ OP2		;I/O instructions */
	0010032,	/*	ISZ OP3		*/
	0125404,	/*	INC 1,1,SZR	;done? */
	0000005,	/*	JMP LOOP	;no, increment again */
	0030016,	/*	LDA 2,C377	;place JMP 377 into */
	0050377,	/*	STA 2,377	;location 377 */
	0060077,	/* OP1: 060077		;start device (NIOS 0) */
	00101102,	/*	MOVL 0,0,SZC	;test switch 0, low speed? */
	0000377,	/* C377: JMP 377	;no - jmp 377 & wait */
	0004030,	/* LOOP2: JSR GET+1	;get a frame */
	0101065,	/*	MOVC 0,0,SNR	;is it non-zero? */
	0000017,	/*	JMP LOOP2	;no, ignore */
	0004027,	/* LOOP4: JSR GET	;yes, get full word */
	0046026,	/*	STA 1,@C77	;store starting at 100 */
			/*			;2's complement of word ct */
	0010100,	/* 	ISZ 100		;done? */
	0000022,	/*	JMP LOOP4	;no, get another */
	0000077,	/* C77: JMP 77		;yes location ctr and */
			/*			;jmp to last word */
	0126420,	/* GET: SUBZ 1,1	; clr AC1, set carry */
			/* OP2:			*/
	0063577,	/* LOOP3: 063577	;done? (SKPDN 0) - 1 */
	0000030,	/*	JMP LOOP3	;no -- wait */
	0060477,	/* OP3: 060477		;y -- read in ac0 (DIAS 0,0) */
	0107363,	/*	ADDCS 0,1,SNC	;add 2 frames swapped - got 2nd? */
	0000030,	/*	JMP LOOP3	;no go back after it */
	0125300,	/*	MOVS 1,1	;yes swap them */
	0001400,	/*	JMP 0,3		;rtn with full word */
	0000000		/*	0		;padding */
};

t_stat cpu_boot (int32 unitno, DEVICE *dptr)
{
int32 i;
extern int32 saved_PC;

for (i = 0; i < BOOT_LEN; i++) M[BOOT_START + i] = boot_rom[i];
saved_PC = BOOT_START;
return SCPE_OK;
}

/* 1-to-1 map for I/O devices */

int32 MapAddr (int32 map, int32 addr)
{
return addr;
}
