/* i1401_cpu.c: IBM 1401 CPU simulator

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

   30-Nov-01	RMS	Added extended SET/SHOW support
   10-Aug-01	RMS	Removed register in declarations
   07-Dec-00	RMS	Fixed bugs found by Charles Owen
			-- 4,7 char NOPs are legal
			-- 1 char B is chained BCE
			-- MCE moves whole char after first
   14-Apr-99	RMS	Changed t_addr to unsigned

   The register state for the IBM 1401 is:

   IS		I storage address register (PC)
   AS		A storage address register (address of first operand)
   BS		B storage address register (address of second operand)
   ind[0:63]	indicators
   SSA		sense switch A
   IOCHK	I/O check
   PRCHK	process check

   The IBM 1401 is a variable instruction length, decimal data system.
   Memory consists of 4000, 8000, 12000, or 16000 BCD characters, each
   containing six bits of data and a word mark.  There are no general
   registers; all instructions are memory to memory, using explicit
   addresses or an address pointer from a prior instruction.

   BCD numeric data consists of the low four bits of a character (DIGIT),
   encoded as X, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, X, X, X, X, X.  The high
   two bits (ZONE) encode the sign of the data as +, +, -, +.  Character
   data uses all six bits of a character.  Numeric and character fields are
   delimited by a word mark.  Fields are typically processed in descending
   address order (low-order data to high-order data).

   The 1401 encodes a decimal address, and an index register number, in
   three characters:

	character		zone			digit
	addr + 0		<1:0> of thousands	hundreds
	addr + 1		index register #	tens
	addr + 2		<3:2> of thousands	ones

   Normally the digit values 0, 11, 12, 13, 14, 15 are illegal in addresses.
   However, in indexing, digits are passed through the adder, and illegal
   values are normalized to legal counterparts.

   The 1401 has six instruction formats:

	op			A and B addresses, if any, from AS and BS
	op d			A and B addresses, if any, from AS and BS
	op aaa			B address, if any, from BS
	op aaa d		B address, if any, from BS
	op aaa bbb
	op aaa bbb d

   where aaa is the A address, bbb is the B address, and d is a modifier.
   The opcode has word mark set; all other characters have word mark clear.
*/

/* This routine is the instruction decode routine for the IBM 1401.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

	HALT instruction
	breakpoint encountered
	illegal addresses or instruction formats
	I/O error in I/O simulator

   2. Interrupts.  The 1401 has no interrupt structure.

   3. Non-existent memory.  On the 1401, references to non-existent
      memory halt the processor.

   4. Adding I/O devices.  These modules must be modified:

	i1401_cpu.c	add IO dispatches to iodisp
	i1401_sys.c	add pointer to data structures to sim_devices
*/

#include "i1401_defs.h"

#define MM(x)		x = x - 1; \
			if (x < 0) { \
				x = BA + MAXMEMSIZE - 1; \
				reason = STOP_WRAP; \
				break;  }			
#define PP(x)		x = x + 1; \
			if (ADDR_ERR (x)) { \
				x = BA + (x % MAXMEMSIZE); \
				reason = STOP_WRAP; \
				break;  }
#define BRANCH		if (ADDR_ERR (AS)) { \
				reason = STOP_INVBR; \
				break;  } \
			if (cpu_unit.flags & XSA) BS = IS; \
			else BS = BA + 0; \
			oldIS = saved_IS; \
			IS = AS;

uint8 M[MAXMEMSIZE] = { 0 };				/* main memory */
int32 saved_IS = 0;					/* saved IS */
int32 AS = 0;						/* AS */
int32 BS = 0;						/* BS */
int32 as_err = 0, bs_err = 0;				/* error flags */
int32 oldIS = 0;					/* previous IS */
int32 ind[64] = { 0 };					/* indicators */
int32 ssa = 1;						/* sense switch A */
int32 prchk = 0;					/* process check stop */
int32 iochk = 0;					/* I/O check stop */
extern int32 sim_int_char;
extern int32 sim_brk_types, sim_brk_dflt, sim_brk_summ;	/* breakpoint info */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
int32 store_addr_h (int32 addr);
int32 store_addr_t (int32 addr);
int32 store_addr_u (int32 addr);
t_stat iomod (int32 ilnt, int32 mod, const int32 *tptr);
t_stat iodisp (int32 dev, int32 unit, int32 flag, int32 mod);

extern t_stat read_card (int32 ilnt, int32 mod);
extern t_stat punch_card (int32 ilnt, int32 mod);
extern t_stat select_stack (int32 mod);
extern t_stat carriage_control (int32 mod);
extern t_stat write_line (int32 ilnt, int32 mod);
extern t_stat inq_io (int32 flag, int32 mod);
extern t_stat mt_io (int32 unit, int32 flag, int32 mod);
extern t_stat mt_func (int32 unit, int32 mod);
extern t_stat sim_activate (UNIT *uptr, int32 delay);

/* CPU data structures

   cpu_dev	CPU device descriptor
   cpu_unit	CPU unit descriptor
   cpu_reg	CPU register list
   cpu_mod	CPU modifier list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BCD + STDOPT,
	MAXMEMSIZE) };

REG cpu_reg[] = {
	{ DRDATA (IS, saved_IS, 14), PV_LEFT },
	{ DRDATA (AS, AS, 14), PV_LEFT },
	{ DRDATA (BS, BS, 14), PV_LEFT },
	{ FLDATA (ASERR, as_err, 0) },
	{ FLDATA (BSERR, bs_err, 0) },
	{ FLDATA (SSA, ssa, 0) },
	{ FLDATA (SSB, ind[IN_SSB], 0) },
	{ FLDATA (SSC, ind[IN_SSC], 0) },
	{ FLDATA (SSD, ind[IN_SSD], 0) },
	{ FLDATA (SSE, ind[IN_SSE], 0) },
	{ FLDATA (SSF, ind[IN_SSF], 0) },
	{ FLDATA (SSG, ind[IN_SSG], 0) },
	{ FLDATA (EQU, ind[IN_EQU], 0) },
	{ FLDATA (UNEQ, ind[IN_UNQ], 0) },
	{ FLDATA (HIGH, ind[IN_HGH], 0) },
	{ FLDATA (LOW, ind[IN_LOW], 0) },
	{ FLDATA (OVF, ind[IN_OVF], 0) },
	{ FLDATA (IOCHK, iochk, 0) },
	{ FLDATA (PRCHK, prchk, 0) },
	{ DRDATA (OLDIS, oldIS, 14), REG_RO + PV_LEFT },
	{ ORDATA (WRU, sim_int_char, 8) },
	{ NULL }  };

MTAB cpu_mod[] = {
	{ XSA, XSA, "XSA", "XSA", NULL },
	{ XSA, 0, "no XSA", "NOXSA", NULL },
	{ HLE, HLE, "HLE", "HLE", NULL },
	{ HLE, 0, "no HLE", "NOHLE", NULL },
	{ BBE, BBE, "BBE", "BBE", NULL },
	{ BBE, 0, "no BBE", "NOBBE", NULL },
	{ MA, MA, "MA", 0, NULL },
	{ MA, 0, "no MA", 0, NULL },
	{ MR, MR, "MR", "MR", NULL },
	{ MR, 0, "no MR", "NOMR", NULL },
	{ EPE, EPE, "EPE", "EPE", NULL },
	{ EPE, 0, "no EPE", "NOEPE", NULL },
	{ UNIT_MSIZE, 4000, NULL, "4K", &cpu_set_size },
	{ UNIT_MSIZE, 8000, NULL, "8K", &cpu_set_size },
	{ UNIT_MSIZE, 12000, NULL, "12K", &cpu_set_size },
	{ UNIT_MSIZE, 16000, NULL, "16K", &cpu_set_size },
	{ 0 }  };

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 10, 14, 1, 8, 7,
	&cpu_ex, &cpu_dep, &cpu_reset,
	NULL, NULL, NULL };

/* Opcode table - length, dispatch, and option flags.  This table is also
   used by the symbolic input routine to validate instruction lengths  */

const int32 op_table[64] = {
	0,						/* 00: illegal */
	L1 | L2 | L4 | L5,				/* read */
	L1 | L2 | L4 | L5,				/* write */
	L1 | L2 | L4 | L5,				/* write and read */
	L1 | L2 | L4 | L5,				/* punch */
	L1 | L4,					/* read and punch */
	L1 | L2 | L4 | L5,				/* write and read */
	L1 | L2 | L4 | L5,				/* write, read, punch */
	L1,						/* 10: read feed */
	L1,						/* punch feed */
	0,						/* illegal */
	L1 | L4 | L7 | AREQ | BREQ | MA,		/* modify address */
	L7 | AREQ | BREQ | MDV,				/* multiply */
	0,						/* illegal */
	0,						/* illegal */
	0,						/* illegal */
	0,						/* 20: illegal */
	L1 | L4 | L7 | BREQ | NOWM,			/* clear storage */
	L1 | L4 | L7 | AREQ | BREQ,			/* subtract */
	0,						/* illegal */
	L5 | IO,					/* magtape */
	L1 | L8 | BREQ,					/* branch wm or zone */
	L1 | L8 | BREQ | BBE,				/* branch if bit eq */
	0,						/* illegal */
	L1 | L4 | L7 | AREQ | BREQ,			/* 30: move zones */
	L7 | AREQ | BREQ,				/* move supress zero */
	0,						/* illegal */
	L1 | L4 | L7 | AREQ | BREQ | NOWM,		/* set word mark */
	L7 | AREQ | BREQ | MDV,				/* divide */
	0,						/* illegal */
	0,						/* illegal */
	0,						/* illegal */
	0,						/* 40: illegal */
	0,						/* illegal */
	L2 | L5,					/* select stacker */
	L1 | L4 | L7 | L8 | BREQ | MLS | IO,		/* load */
	L1 | L4 | L7 | L8 | BREQ | MLS | IO,		/* move */
	HNOP | L1 | L4 | L7,				/* nop */
	0,						/* illegal */
	L1 | L4 | L7 | AREQ | BREQ | MR,		/* move to record */
	L1 | L4 | AREQ | MLS,				/* 50: store A addr */
	0,						/* illegal */
	L1 | L4 | L7 | AREQ | BREQ,			/* zero and subtract */
	0,						/* illegal */
	0,						/* illegal */
	0,						/* illegal */
	0,						/* illegal */
	0,						/* illegal */
	0,						/* 60: illegal */
	L1 | L4 | L7 | AREQ | BREQ,			/* add */
	L1 | L4 | L5 | L8,				/* branch */
	L1 | L4 | L7 | AREQ | BREQ,			/* compare */
	L1 | L4 | L7 | AREQ | BREQ,			/* move numeric */
	L1 | L4 | L7 | AREQ | BREQ,			/* move char edit */
	L2 | L5,					/* carriage control */
	0,						/* illegal */
	L1 | L4 | L7 | AREQ | MLS,			/* 70: store B addr */
	0,						/* illegal */
	L1 | L4 | L7 | AREQ | BREQ,			/* zero and add */
	HNOP | L1 | L4,					/* halt */
	L1 | L4 | L7 | AREQ | BREQ,			/* clear word mark */
	0,						/* illegal */
	0,						/* illegal */
	0 };						/* illegal */

const int32 len_table[9] = { 0, L1, L2, 0, L4, L5, 0, L7, L8 };

/* Address character conversion tables.  Illegal characters are marked by
   the flag BA but also contain the post-adder value for indexing  */

const int32 hun_table[64] = {
	BA+000, 100, 200, 300, 400, 500, 600, 700,
	800, 900, 000, BA+300, BA+400, BA+500, BA+600, BA+700,
	BA+1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700,
	1800, 1900, 1000, BA+1300, BA+1400, BA+1500, BA+1600, BA+1700,
	BA+2000, 2100, 2200, 2300, 2400, 2500, 2600, 2700,
	2800, 2900, 2000, BA+2300, BA+2400, BA+2500, BA+2600, BA+2700,
	BA+3000, 3100, 3200, 3300, 3400, 3500, 3600, 3700,
	3800, 3900, 3000, BA+3300, BA+3400, BA+3500, BA+3600, BA+3700 };

const int32 ten_table[64] = {
	BA+00, 10, 20, 30, 40, 50, 60, 70,
	80, 90, 00, BA+30, BA+40, BA+50, BA+60, BA+70,
	X1+00, X1+10, X1+20, X1+30, X1+40, X1+50, X1+60, X1+70,
	X1+80, X1+90, X1+00, X1+30, X1+40, X1+50, X1+60, X1+70,
	X2+00, X2+10, X2+20, X2+30, X2+40, X2+50, X2+60, X2+70,
	X2+80, X2+90, X2+00, X2+30, X2+40, X2+50, X2+60, X2+70,
	X3+00, X3+10, X3+20, X3+30, X3+40, X3+50, X3+60, X3+70,
	X3+80, X3+90, X3+00, X3+30, X3+40, X3+50, X3+60, X3+70 };

const int32 one_table[64] = {
	BA+0, 1, 2, 3, 4, 5, 6, 7,
	8, 9, 0, BA+3, BA+4, BA+5, BA+6, BA+7,
	BA+4000, 4001, 4002, 4003, 4004, 4005, 4006, 4007,
	4008, 4009, 4000, BA+4003, BA+4004, BA+4005, BA+4006, BA+4007,
	BA+8000, 8001, 8002, 8003, 8004, 8005, 8006, 8007,
	8008, 8009, 8000, BA+8003, BA+8004, BA+8005, BA+8006, BA+8007,
	BA+12000, 12001, 12002, 12003, 12004, 12005, 12006, 12007,
	12008, 12009, 12000, BA+12003, BA+12004, BA+12005, BA+12006, BA+12007 };

static const int32 bin_to_bcd[16] = {
	10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

static const int32 bcd_to_bin[16] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 3, 4, 5, 6, 7 };

/* ASCII to BCD conversion */

const char ascii_to_bcd[128] = {
	000, 000, 000, 000, 000, 000, 000, 000,		/* 000 - 037 */
	000, 000, 000, 000, 000, 000, 000, 000,
	000, 000, 000, 000, 000, 000, 000, 000,
	000, 000, 000, 000, 000, 000, 000, 000,
	000, 052, 077, 013, 053, 034, 060, 032,		/* 040 - 077 */
	017, 074, 054, 037, 033, 040, 073, 021,
	012, 001, 002, 003, 004, 005, 006, 007,
	010, 011, 015, 056, 076, 035, 016, 072,
	014, 061, 062, 063, 064, 065, 066, 067,		/* 100 - 137 */
	070, 071, 041, 042, 043, 044, 045, 046,
	047, 050, 051, 022, 023, 024, 025, 026,
	027, 030, 031, 075, 036, 055, 020, 057,
	000, 061, 062, 063, 064, 065, 066, 067,		/* 140 - 177 */
	070, 071, 041, 042, 043, 044, 045, 046,
	047, 050, 051, 022, 023, 024, 025, 026,
	027, 030, 031, 000, 000, 000, 000, 000 };

/* BCD to ASCII conversion - also the "full" print chain */

char bcd_to_ascii[64] = {
	' ', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', '0', '#', '@', ':', '>', '(',
	'^', '/', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', '\'', ',', '%', '=', '\\', '+',
	'-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', '!', '$', '*', ']', ';', '_',
	'&', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
	'H', 'I', '?', '.', ')', '[', '<', '"' };

t_stat sim_instr (void)
{
extern int32 sim_interval;
int32 IS, D, ilnt, flags;
int32 op, xa, t, wm, dev, unit;
int32 a, b, i, bsave, carry;
int32 qzero, qawm, qbody, qsign, qdollar, qaster, qdecimal;
t_stat reason, r1, r2;

/* Indicator resets - a 1 marks an indicator that resets when tested */

static const int32 ind_table[64] = {
	0, 0, 0, 0, 0, 0, 0, 0,				/* 00 - 07 */
	0, 0, 0, 0, 0, 0, 0, 0,				/* 10 - 17 */
	0, 0, 0, 0, 0, 0, 0, 0,				/* 20 - 27 */
	0, 1, 1, 0, 1, 0, 0, 0,				/* 30 - 37 */
	0, 0, 1, 0, 0, 0, 0, 0,				/* 40 - 47 */
	0, 0, 1, 0, 1, 0, 0, 0,				/* 50 - 57 */
	0, 0, 0, 0, 0, 0, 0, 0,				/* 60 - 67 */
	0, 0, 1, 0, 0, 0, 0, 0 };			/* 70 - 77 */

/* Character collation table for compare with HLE option */

static const int32 col_table[64] = {
	000, 067, 070, 071, 072, 073, 074, 075,
	076, 077, 066, 024, 025, 026, 027, 030,
	023, 015, 056, 057, 060, 061, 062, 063,
	064, 065, 055, 016, 017, 020, 021, 022,
	014, 044, 045, 046, 047, 050, 051, 052,
	053, 054, 043, 007, 010, 011, 012, 013,
	006, 032, 033, 034, 035, 036, 037, 040,
	041, 042, 031, 001, 002, 003, 004, 005 };

/* Summing table for two decimal digits, converted back to BCD */
	
static const int32 sum_table[20] = {
	10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 1, 2, 3, 4, 5, 6, 7, 8, 9 };

/* Legal modifier tables */

static const int32 w_mod[] = { BCD_S, BCD_SQUARE, -1 };
static const int32 ss_mod[] = { 1, 2, 4, 8, -1 };
static const int32 mtf_mod[] = { BCD_B, BCD_E, BCD_M, BCD_R, BCD_U, -1 };


/* Restore saved state */

IS = saved_IS;
D = 0;
reason = 0;

/* Main instruction fetch/decode loop */

while (reason == 0) {					/* loop until halted */
saved_IS = IS;						/* commit prev instr */
if (sim_interval <= 0) {				/* check clock queue */
	if (reason = sim_process_event ()) break;  }

if (sim_brk_summ && sim_brk_test (IS, SWMASK ('E'))) {	/* breakpoint? */
	reason = STOP_IBKPT;				/* stop simulation */
	break;  }

sim_interval = sim_interval - 1;

/* Instruction fetch */

if ((M[IS] & WM) == 0) {				/* WM under op? */
	reason = STOP_NOWM;				/* no, error */
	break;  }
op = M[IS] & CHAR;					/* get opcode */
flags = op_table[op];					/* get op flags */
if ((flags == 0) || (flags & ALLOPT & ~cpu_unit.flags)) {
	reason = STOP_NXI;				/* illegal inst? */
	break;  }
if (op == OP_SAR) BS = AS;				/* SAR? save ASTAR */
PP (IS);

if ((t = M[IS]) & WM) goto CHECK_LENGTH;		/* WM? 1 char inst */
D = t;							/* could be D char */
AS = hun_table[t];					/* could be A addr */
PP (IS);						/* if %xy, BA is set */

if ((t = M[IS]) & WM) {					/* WM? 2 char inst */
	AS = AS | BA;					/* ASTAR bad */
	if (!(flags & MLS)) BS = AS;
	goto CHECK_LENGTH;  }
AS = AS + ten_table[t];					/* build A addr */
dev = t;						/* save char as dev */
PP (IS);

if ((t = M[IS]) & WM) {					/* WM? 3 char inst */
	AS = AS | BA;					/* ASTAR bad */
	if (!(flags & MLS)) BS = AS;
	goto CHECK_LENGTH;  }
AS = AS + one_table[t];					/* finish A addr */
unit = (t == BCD_ZERO)? 0: t;				/* save char as unit */
xa = (AS >> V_INDEX) & M_INDEX;				/* get index reg */
if (xa && (D != BCD_PERCNT) && (cpu_unit.flags & XSA)) {	/* indexed? */
	AS = AS + hun_table[M[xa] & CHAR] + ten_table[M[xa + 1] & CHAR] +
		one_table[M[xa + 2] & CHAR];
	AS = (AS & INDEXMASK) % MAXMEMSIZE;  }
if (!(flags & MLS)) BS = AS;				/* not MLS? B = A */
PP (IS);

if ((t = M[IS]) & WM) goto CHECK_LENGTH;		/* WM? 4 char inst */
if ((op == OP_B) && (t == BCD_BLANK)) goto CHECK_LENGTH; /* BR + space? */
D = t;							/* could be D char */
BS = hun_table[t];					/* could be B addr */
PP (IS);

if ((t = M[IS]) & WM) {					/* WM? 5 char inst */
	BS = BS | BA;					/* BSTAR bad */
	goto CHECK_LENGTH;  }
BS = BS + ten_table[t];					/* build B addr */
PP (IS);

if ((t = M[IS]) & WM) {					/* WM? 6 char inst */
	BS = BS | BA;					/* BSTAR bad */
	goto CHECK_LENGTH;  }
BS = BS + one_table[t];					/* finish B addr */
xa = (BS >> V_INDEX) & M_INDEX;				/* get index reg */
if (xa && (cpu_unit.flags & XSA)) {			/* indexed? */
	BS = BS + hun_table[M[xa] & CHAR] + ten_table[M[xa + 1] & CHAR]
		+ one_table[M[xa + 2] & CHAR];
	BS = (BS & INDEXMASK) % MAXMEMSIZE;  }
PP (IS);

if ((M[IS] & WM) || (flags & NOWM)) goto CHECK_LENGTH;	/* WM? 7 chr */
D = M[IS];						/* last char is D */
do { PP (IS);  } while ((M[IS] & WM) == 0);		/* find word mark */

CHECK_LENGTH:
ilnt = IS - saved_IS;					/* get lnt */
if (((flags & len_table [(ilnt <= 8)? ilnt: 8]) == 0) &&	/* valid lnt? */
	((flags & HNOP) == 0)) reason = STOP_INVL;
if ((flags & BREQ) && ADDR_ERR (BS)) reason = STOP_INVB;	/* valid A? */
if ((flags & AREQ) && ADDR_ERR (AS)) reason = STOP_INVA;	/* valid B? */
if (reason) break;					/* error in fetch? */
switch (op) {						/* case on opcode */	

/* Move instructions					A check	B check

   MCW: copy A to B, preserving B WM,			here	fetch
	until either A or B WM
   LCA: copy A to B, overwriting B WM,			here	fetch
	until A WM
   MCM: copy A to B, preserving B WM,			fetch	fetch
	until record or group mark
   MSZ: copy A to B, clearing B WM, until A WM;		fetch	fetch
	reverse scan and suppress leading zeroes
   MN:	copy A char digit to B char digit,		fetch	fetch
	preserving B zone and WM
   MZ:	copy A char zone to B char zone,		fetch	fetch
	preserving B digit and WM
*/

case OP_MCW:						/* move char */
	if (ilnt >= 8) {				/* I/O form? */
		reason = iodisp (dev, unit, MD_NORM, D);
		break;  }
	if (ADDR_ERR (AS)) {				/* check A addr */
		reason = STOP_INVA;
		break;  }
	do {	M[BS] = (M[BS] & WM) | (M[AS] & CHAR);	/* move char */
		wm = M[AS] | M[BS];
		MM (AS); MM (BS);  }			/* decr pointers */
	while ((wm & WM) == 0);				/* stop on A,B WM */
	break;
case OP_LCA:						/* load char */
	if (ilnt >= 8) {				/* I/O form? */
		reason = iodisp (dev, unit, MD_WM, D);
		break;  }
	if (ADDR_ERR (AS)) {				/* check A addr */
		reason = STOP_INVA;
		break;  }
	do {	wm = M[BS] = M[AS];			/* move char + wmark */
		MM (AS); MM (BS);  }			/* decr pointers */
	while ((wm & WM) == 0);				/* stop on A WM */
	break;
case OP_MCM:						/* move to rec/group */
	do {	M[BS] = (M[BS] & WM) | (M[AS] & CHAR);	/* move char */
		t = M[AS];
		PP (AS); PP (BS);  }			/* incr pointers */
	while (((t & CHAR) != BCD_RECMRK) && (t != (BCD_GRPMRK + WM)));
	break;
case OP_MSZ:						/* move suppress zero */
	bsave = BS;					/* save B start */
	qzero = 1;					/* set suppress */
	do {	M[BS] = M[AS] & ((BS != bsave)? CHAR: DIGIT);	/* copy char */
		wm = M[AS];
		MM (AS); MM (BS);  }			/* decr pointers */
	while ((wm & WM) == 0);				/* stop on A WM */
	do {	PP (BS);				/* adv B */
		t = M[BS];				/* get B, cant be WM */
		if ((t == BCD_ZERO) || (t == BCD_COMMA)) {
			if (qzero) M[BS] = 0;  }
		else if ((t == BCD_BLANK) || (t == BCD_MINUS)) ;
		else if (((t == BCD_DECIMAL) && (cpu_unit.flags & EPE)) ||
			 (t <= BCD_NINE)) qzero = 0;
		else qzero = 1;  }
	while (BS <= bsave);
	break;	
case OP_MN:						/* move numeric */
	M[BS] = (M[BS] & ~DIGIT) | (M[AS] & DIGIT);	/* move digit */
	MM (AS); MM (BS);				/* decr pointers */
	break;
case OP_MZ:						/* move zone */
	M[BS] = (M[BS] & ~ZONE) | (M[AS] & ZONE);	/* move high bits */
	MM (AS); MM (BS);				/* decr pointers */
	break;

/* Compare

   A and B are checked in fetch
*/

case OP_C:						/* compare */
	if (ilnt != 1) {				/* if not chained */
		ind[IN_EQU] = 1;			/* clear indicators */
		ind[IN_UNQ] = ind[IN_HGH] = ind[IN_LOW] = 0;  }
	do {	a = M[AS];				/* get characters */
		b = M[BS];
		wm = a | b;				/* get word marks */
		if ((a & CHAR) != (b & CHAR)) {		/* unequal? */
			ind[IN_EQU] = 0;		/* set indicators */
			ind[IN_UNQ] = 1;
			ind[IN_HGH] = col_table[b & CHAR] > col_table [a & CHAR];
			ind[IN_LOW] = ind[IN_HGH] ^ 1;  }
		MM (AS); MM (BS);  }			/* decr pointers */
	while ((wm & WM) == 0);				/* stop on A, B WM */
	if ((a & WM) && !(b & WM)) {			/* short A field? */
		ind[IN_EQU] = ind[IN_LOW] = 0;
		ind[IN_UNQ] = ind[IN_HGH] = 1;  }
	if (!(cpu_unit.flags & HLE))			/* no HLE? */
		ind[IN_EQU] = ind[IN_LOW] = ind[IN_HGH] = 0;
	break;

/* Branch instructions					A check	    B check

   B 1/8 char:	branch if B char equals d		if branch   here
   B 4 char:	unconditional branch			if branch
   B 5 char:	branch if indicator[d] is set		if branch
   BWZ:		branch if (d<0>: B char WM)		if branch   here
		(d<1>: B char zone = d zone)
   BBE:		branch if B char & d non-zero		if branch   here
*/

case OP_B:						/* branch */
	if (ilnt == 4) { BRANCH; }			/* uncond branch? */
	else if (ilnt == 5) {				/* branch on ind? */
		if (ind[D]) { BRANCH;  }		/* test indicator */
		if (ind_table[D]) ind[D] = 0;  }	/* reset if needed */
	else {	if (ADDR_ERR (BS)) {			/* branch char eq */
			reason = STOP_INVB;		/* validate B addr */
			break;  }
		if ((M[BS] & CHAR) == D) { BRANCH;  }	/* char equal? */
		else {	MM (BS);  }  }
	break;
case OP_BWZ:						/* branch wm or zone */
	if (((D & 1) && (M[BS] & WM)) ||		/* d1? test wm */
	    ((D & 2) && ((M[BS] & ZONE) == (D & ZONE)))) /* d2? test zone */
		{ BRANCH;  }
	else {  MM (BS);  }				/* decr pointer */
	break;
case OP_BBE:						/* branch if bit eq */
	if (M[BS] & D & CHAR) { BRANCH;  }		/* any bits set? */
	else {  MM (BS);  }				/* decr pointer */
	break;

/* Arithmetic instructions				A check	B check

   ZA:	move A to B, normalizing A sign,		fetch	fetch
	preserving B WM, until B WM
   ZS:	move A to B, complementing A sign,		fetch	fetch
	preserving B WM, until B WM
   A:	add A to B					fetch	fetch
   S:	subtract A from B				fetch	fetch
*/

case OP_ZA: case OP_ZS:					/* zero and add/sub */
	a = i = 0;					/* clear flags */
	do {	if (a & WM) wm = M[BS] = (M[BS] & WM) | BCD_ZERO;
		else {	a = M[AS];			/* get A char */
			t = (a & CHAR)? bin_to_bcd[a & DIGIT]: 0;
			wm = M[BS] = (M[BS] & WM) | t;	/* move digit */
			MM (AS);  }
		if (i == 0) i = M[BS] = M[BS] |
			((((a & ZONE) == BBIT) ^ (op == OP_ZS))? BBIT: ZONE);
		MM (BS);  }
	while ((wm & WM) == 0);				/* stop on B WM */
	break;
case OP_A: case OP_S:					/* add/sub */
	bsave = BS;					/* save sign pos */
	a = M[AS];					/* get A digit/sign */
	b = M[BS];					/* get B digit/sign */
	MM (AS);
	qsign = ((a & ZONE) == BBIT) ^ ((b & ZONE) == BBIT) ^ (op == OP_S);
	t = bcd_to_bin[a & DIGIT];			/* get A binary */
	t = bcd_to_bin[b & DIGIT] + (qsign? 10 - t: t);	/* sum A + B */
	carry = (t >= 10);				/* get carry */
	b = (b & ~DIGIT) | sum_table[t];		/* get result */
	if (qsign && ((b & BBIT) == 0)) b = b | ZONE;	/* normalize sign */
	M[BS] = b;					/* store result */
	MM (BS);
	if (b & WM) {					/* b wm? done */
		if (qsign && (carry == 0)) M[bsave] =	/* compl, no carry? */
			WM + ((b & ZONE) ^ ABIT) + sum_table[10 - t];
		break;  }
	do {	if (a & WM) a = WM;			/* A WM? char = 0 */
		else {	a = M[AS];			/* else get A */
			MM (AS);  }
		b = M[BS];				/* get B */
		t = bcd_to_bin[a & DIGIT];		/* get A binary */
		t = bcd_to_bin[b & DIGIT] + (qsign? 9 - t: t) + carry;
		carry = (t >= 10);			/* get carry */
		if ((b & WM) && (qsign == 0)) {		/* last, no recomp? */
			M[BS] = WM + sum_table[t] +	/* zone add */
				(((a & ZONE) + b + (carry? ABIT: 0)) & ZONE);
			ind[IN_OVF] = carry;  }		/* ovflo if carry */
		else M[BS] = (b & WM) + sum_table[t];	/* normal add */
		MM (BS);  }
	while ((b & WM) == 0);				/* stop on B WM */
	if (qsign && (carry == 0)) {			/* recompl, no carry? */
		M[bsave] = M[bsave] ^ ABIT;		/* XOR sign */
		for (carry = 1; bsave != BS; --bsave) {	/* rescan */
			t = 9 - bcd_to_bin[M[bsave] & DIGIT] + carry;
			carry = (t >= 10);
			M[bsave] = (M[bsave] & ~DIGIT) | sum_table[t];  }  }
	break;

/* I/O instructions					A check	B check

   R:	read a card					if branch
   W:	write to line printer				if branch
   WR:	write and read					if branch
   P:	punch a card					if branch
   RP:	read and punch					if branch
   WP:	write and punch					if branch
   WRP:	write read and punch				if branch
   RF:	read feed (nop)
   PF:	punch feed (nop)
   SS:	select stacker					if branch
   CC:	carriage control				if branch
   MTF:	magtape functions
*/

case OP_R:						/* read */
	if (reason = iomod (ilnt, D, NULL)) break;	/* valid modifier? */
	reason = read_card (ilnt, D);			/* read card */
	BS = CDR_BUF + CDR_WIDTH;
	if (ilnt >= 4) { BRANCH;  }			/* check for branch */
	break;
case OP_W:						/* write */
	if (reason = iomod (ilnt, D, w_mod)) break;	/* valid modifier? */
	reason = write_line (ilnt, D);			/* print line */
	BS = LPT_BUF + LPT_WIDTH;
	if (ilnt >= 4) { BRANCH;  }			/* check for branch */
	break;
case OP_P:						/* punch */
	if (reason = iomod (ilnt, D, NULL)) break;	/* valid modifier? */
	reason = punch_card (ilnt, D);			/* punch card */
	BS = CDP_BUF + CDP_WIDTH;
	if (ilnt >= 4) { BRANCH;  }			/* check for branch */
	break;
case OP_WR:						/* write and read */
	if (reason = iomod (ilnt, D, w_mod)) break;	/* valid modifier? */
	reason = write_line (ilnt, D);			/* print line */
	r1 = read_card (ilnt, D);			/* read card */
	BS = CDR_BUF + CDR_WIDTH;
	if (ilnt >= 4) { BRANCH;  }			/* check for branch */
	if (reason == SCPE_OK) reason = r1;		/* merge errors */
	break;
case OP_WP:						/* write and punch */
	if (reason = iomod (ilnt, D, w_mod)) break;	/* valid modifier? */
	reason = write_line (ilnt, D);			/* print line */
	r1 = punch_card (ilnt, D);			/* punch card */
	BS = CDP_BUF + CDP_WIDTH;
	if (ilnt >= 4) { BRANCH;  }			/* check for branch */
	if (reason == SCPE_OK) reason = r1;		/* merge errors */
	break;
case OP_RP:						/* read and punch */
	if (reason = iomod (ilnt, D, NULL)) break;	/* valid modifier? */
	reason = read_card (ilnt, D);			/* read card */
	r1 = punch_card (ilnt, D);			/* punch card */
	BS = CDP_BUF + CDP_WIDTH;  
	if (ilnt >= 4) { BRANCH;  }			/* check for branch */
	if (reason == SCPE_OK) reason = r1;		/* merge errors */
	break;
case OP_WRP:						/* write, read, punch */
	if (reason = iomod (ilnt, D, w_mod)) break;	/* valid modifier? */
	reason = write_line (ilnt, D);			/* print line */
	r1 = read_card (ilnt, D);			/* read card */
	r2 = punch_card (ilnt, D);			/* punch card */
	BS = CDP_BUF + CDP_WIDTH;
	if (ilnt >= 4) { BRANCH;  }			/* check for branch */
	if (reason == SCPE_OK) reason = (r1 == SCPE_OK)? r2: r1;
	break;
case OP_SS:						/* select stacker */
	if (reason = iomod (ilnt, D, ss_mod)) break;	/* valid modifier? */
	if (reason = select_stack (D)) break;		/* sel stack, error? */
	if (ilnt >= 4) { BRANCH;  }			/* check for branch */
	break;
case OP_CC:						/* carriage control */
	if (reason = carriage_control (D)) break;	/* car ctrl, error? */
	if (ilnt >= 4) { BRANCH;  }			/* check for branch */
	break;
case OP_MTF:						/* magtape function */
	if (reason = iomod (ilnt, D, mtf_mod)) break;	/* valid modifier? */
	if (reason = mt_func (unit, D)) break;		/* mt func, error? */
	break;						/* can't branch */
case OP_RF: case OP_PF:					/* read, punch feed */
	break;						/* nop's */

/* Move character and edit

   Control flags
	qsign		sign of A field (0 = +, 1 = minus)
	qawm		A field WM seen and processed
	qzero		zero suppression enabled
	qbody		in body (copying A field characters)
	qdollar		EPE only; $ seen in body
	qaster		EPE only; * seen in body
	qdecimal	EPE only; . seen on first rescan

   MCE operates in one to three scans, the first of which has three phases

	1	right to left	qbody = 0, qawm = 0 => right status
				qbody = 1, qawm = 0 => body
				qbody = 0, qawm = 1 => left status
	2	left to right
	3	right to left, extended print end only

   The first A field character is masked to its digit part, all others
   are copied intact		
*/

case OP_MCE:						/* edit */
	a = M[AS];					/* get A char */
	b = M[BS];					/* get B char */
	if (a & WM) {					/* one char A field? */
		reason = STOP_MCE1;
		break;  }
	if (b & WM) {					/* one char B field? */
		reason = STOP_MCE2;
		break;  }
	t = a & DIGIT; MM (AS);				/* get A digit */
	qsign = ((a & ZONE) == BBIT);			/* get A field sign */
	qawm = qzero = qbody = 0;			/* clear other flags */
	qdollar = qaster = qdecimal = 0;		/* clear EPE flags */

/* Edit pass 1 - from right to left, under B field control

	*	in status or !epe, skip B; else, set qaster, repl with A
	$	in status or !epe, skip B; else, set qdollar, repl with A
	0	in right status or body, if !qzero, set A WM; set qzero, repl with A
		else, if !qzero, skip B; else, if (!B WM) set B WM 
	blank	in right status or body, repl with A; else, skip B
	C,R,-	in status, blank B; else, skip B
	,	in status, blank B, else, skip B
	&	blank B
*/

	do {	b = M[BS];				/* get B char */
		M[BS] = M[BS] & ~WM;			/* clr WM */
		switch (b & CHAR) {			/* case on B char */
		case BCD_ASTER:				/* * */
			if (!qbody || qdollar || !(cpu_unit.flags & EPE)) break;
			qaster = 1;			/* flag */
			goto A_CYCLE;			/* take A cycle */
		case BCD_DOLLAR:			/* $ */
			if (!qbody || qaster || !(cpu_unit.flags & EPE)) break;
			qdollar = 1;			/* flag */
			goto A_CYCLE;			/* take A cycle */
		case BCD_ZERO:				/* 0 */
			if (qawm && !qzero && !(b & WM)) {
				M[BS] = BCD_ZERO + WM;	/* mark with WM */
				qzero = 1;		/* flag supress */
				break;  }
			if (!qzero) t = t | WM;		/* first? set WM */
			qzero = 1;			/* flag supress */
							/* fall through */
		case BCD_BLANK:				/* blank */
			if (qawm) break;		/* any A left? */
		A_CYCLE:
			M[BS] = t;			/* copy char */
			if (a & WM) {			/* end of A field? */
				qbody = 0;		/* end body */
				qawm = 1;  }
			else {	qbody = 1;		/* in body */
				a = M[AS]; MM (AS);	/* next A */
				t = a & CHAR;  }
			break;
		case BCD_C: case BCD_R: case BCD_MINUS:	/* C, R, - */
			if (!qsign && !qbody) M[BS] = BCD_BLANK;
			break;
		case BCD_COMMA:				/* , */
			if (!qbody) M[BS] = BCD_BLANK;	/* bl if status */
			break;
		case BCD_AMPER:				/* & */
			M[BS] = BCD_BLANK;		/* blank B field */
			break;  }			/* end switch */
		MM (BS);  }				/* decr B pointer */
	while ((b & WM) == 0);				/* stop on B WM */

	if (!qawm || !qzero) {				/* rescan? */
		if (qdollar) reason = STOP_MCE3;	/* error if $ */
		break;  }

/* Edit pass 2 - from left to right, supressing zeroes */

	do {	b = M[++BS];				/* get B char */
		switch (b & CHAR) {			/* case on B char */
		case BCD_ONE: case BCD_TWO: case BCD_THREE:
		case BCD_FOUR: case BCD_FIVE: case BCD_SIX:
		case BCD_SEVEN: case BCD_EIGHT: case BCD_NINE:
			qzero = 0;			/* turn off supr */
			break;
		case BCD_ZERO: case BCD_COMMA:		/* 0 or , */
			if (qzero && !qdecimal)		/* if supr, blank */
				M[BS] = qaster? BCD_ASTER: BCD_BLANK;
			break;
		case BCD_BLANK:				/* blank */
			if (qaster) M[BS] = BCD_ASTER;	/* if EPE *, repl */
			break;
		case BCD_DECIMAL:			/* . */
			if (qzero && (cpu_unit.flags & EPE))
				 qdecimal = 1;		/* flag for EPE */
		case BCD_PERCNT: case BCD_WM: case BCD_BS:
		case BCD_TS: case BCD_MINUS:
			break;				/* ignore */
		default:				/* other */
			qzero = 1;			/* restart supr */
			break;  }  }			/* end case, do */
	while ((b & WM) == 0);

	M[BS] = M[BS] & ~WM;				/* clear B WM */
	if (!qdollar && !(qdecimal && qzero)) break;	/* rescan again? */
	if (qdecimal && qzero) qdollar = 0;		/* no digits? clr $ */

/* Edit pass 3 (extended print only) - from right to left */

	for (;; ) {					/* until chars */
		b = M[BS];				/* get B char */
		if ((b == BCD_BLANK) && qdollar) {	/* blank & flt $? */
			M[BS] = BCD_DOLLAR;		/* insert $ */
			break;  }			/* exit for */
		if (b == BCD_DECIMAL) {			/* decimal? */
			M[BS] = qaster? BCD_ASTER: BCD_BLANK;
			break;  }			/* exit for */
		if ((b == BCD_ZERO) && !qdollar)	/* 0 & ~flt $ */
			M[BS] = qaster? BCD_ASTER: BCD_BLANK;
		BS--;  }				/* end for */
	break;						/* done at last! */	

/* Miscellaneous instructions				A check	   B check

   SWM:	set WM on A char and B char			fetch	   fetch
   CWM: clear WM on A char and B char			fetch	   fetch
   CS:	clear from B down to nearest hundreds address	if branch  fetch
   MA:	add A addr and B addr, store at B addr		fetch	   fetch
   SAR:	store A* at A addr				fetch
   SBR: store B* at A addr				fetch
   NOP:	no operation
   H:	halt
*/

case OP_SWM:						/* set word mark */
	M[BS] = M[BS] | WM;				/* set A field mark */
	M[AS] = M[AS] | WM;				/* set B field mark */
	MM (AS); MM (BS);				/* decr pointers */
	break;
case OP_CWM:						/* clear word mark */
	M[BS] = M[BS] & ~WM;				/* clear A field mark */
	M[AS] = M[AS] & ~WM;				/* clear B field mark */
	MM (AS); MM (BS);				/* decr pointers */
	break;
case OP_CS:						/* clear storage */
	t = (BS / 100) * 100;				/* lower bound */
	while (BS >= t) M[BS--] = 0;			/* clear region */
	if (BS < 0) BS = BS + MEMSIZE;			/* wrap if needed */
	if (ilnt >= 7) { BRANCH; }			/* branch variant? */
	break;
case OP_MA:						/* modify address */
	a = one_table[M[AS] & CHAR]; MM (AS);		/* get A address */
	a = a + ten_table[M[AS] & CHAR]; MM (AS);
	a = a + hun_table[M[AS] & CHAR]; MM (AS);
	b = one_table[M[BS] & CHAR]; MM (BS);		/* get B address */
	b = b + ten_table[M[BS] & CHAR]; MM (BS);
	b = b + hun_table[M[BS] & CHAR]; MM (BS);
	t = ((a + b) & INDEXMASK) % MAXMEMSIZE;		/* compute sum */
	M[BS + 3] = (M[BS + 3] & WM) | store_addr_u (t);
	M[BS + 2] = (M[BS + 2] & (WM + ZONE)) | store_addr_t (t);
	M[BS + 1] = (M[BS + 1] & WM) | store_addr_h (t);
	if (((a % 4000) + (b % 4000)) >= 4000) BS = BS + 2;	/* carry? */
	break;
case OP_SAR: case OP_SBR:				/* store A, B reg */
	M[AS] = (M[AS] & WM) | store_addr_u (BS); MM (AS);
	M[AS] = (M[AS] & WM) | store_addr_t (BS); MM (AS);
	M[AS] = (M[AS] & WM) | store_addr_h (BS); MM (AS);
	break;
case OP_NOP:						/* nop */
	break;
case OP_H:						/* halt */
	if (ilnt >= 4) { BRANCH;  }			/* branch if called */
	reason = STOP_HALT;				/* stop simulator */
	saved_IS = IS;					/* commit instruction */
	break;
default:
	reason = STOP_NXI;				/* unimplemented */
	break;  }					/* end switch */
}							/* end while */

/* Simulation halted */

as_err = (AS > ADDRMASK);
bs_err = (BS > ADDRMASK);
return reason;
}							/* end sim_instr */

/* store addr_x - convert address to BCD character in x position

   Inputs:
	addr	=	address to convert
   Outputs:
	char	=	converted address character
*/

int32 store_addr_h (int32 addr)
{
int32 thous;

thous = (addr / 1000) & 03;
return  bin_to_bcd[(addr % 1000) / 100] | (thous << V_ZONE);
}

int32 store_addr_t (int32 addr)
{
return bin_to_bcd[(addr % 100) / 10];
}

int32 store_addr_u (int32 addr)
{
int32 thous;

thous = (addr / 1000) & 014;
return bin_to_bcd[addr % 10] | (thous << (V_ZONE - 2));
}

/* iomod - check on I/O modifiers

   Inputs:
	ilnt	=	instruction length
	mod	=	modifier character
	tptr	=	pointer to table of modifiers, end is -1
   Output:
	status	=	SCPE_OK if ok, STOP_INVM if invalid
*/

t_stat iomod (int32 ilnt, int32 mod, const int32 *tptr)
{
if ((ilnt != 2) && (ilnt != 5) && (ilnt < 8)) return SCPE_OK;
if (tptr == NULL) return STOP_INVM;
do {	if (mod == *tptr++) return SCPE_OK;  }
while (*tptr >= 0);
return STOP_INVM;
}

/* iodisp - dispatch load or move to I/O routine

   Inputs:
	dev	=	device number
	unit	=	unit number
	flag	=	move (MD_NORM) vs load (MD_WM)
	mod	=	modifier
*/

t_stat iodisp (int32 dev, int32 unit, int32 flag, int32 mod)
{
if (dev == IO_INQ) return inq_io (flag, mod);		/* inq terminal? */
if (dev == IO_MT) return mt_io (unit, flag, mod);	/* magtape? */
if (dev == IO_MTB) {					/* binary? */
	if (flag == MD_WM) return STOP_INVM;		/* invalid */
	return mt_io (unit, MD_BIN, mod);  }
return STOP_NXD;					/* not implemented */
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
int32 i;

for (i = 0; i < 64; i++) ind[i] = 0;
ind[IN_UNC] = 1;
AS = 0; as_err = 1;
BS = 0; bs_err = 1;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE) return SCPE_NXM;
if (vptr != NULL) *vptr = M[addr] & (WM + CHAR);
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE) return SCPE_NXM;
M[addr] = val & (WM + CHAR);
return SCPE_OK;
}

/* Memory size change */

t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 mc = 0;
t_addr i;

if ((val <= 0) || (val > MAXMEMSIZE) || ((val % 1000) != 0))
	return SCPE_ARG;
for (i = val; i < MEMSIZE; i++) mc = mc | M[i];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
	return SCPE_OK;
MEMSIZE = val;
for (i = MEMSIZE; i < MAXMEMSIZE; i++) M[i] = 0;
if (MEMSIZE > 4000) cpu_unit.flags = cpu_unit.flags | MA;
else cpu_unit.flags = cpu_unit.flags & ~MA;
return SCPE_OK;
}
