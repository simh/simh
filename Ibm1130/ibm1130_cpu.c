/* ibm1130_cpu.c: IBM 1130 CPU simulator

   Copyright (c) 2002, Brian Knittel
   Based on PDP-11 simulator written by Robert M Supnik

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

   25-Jun-01 BLK	Written
   27-Mar-02 BLK	Made BOSC work even in short form

   The register state for the IBM 1130 CPU is:

   IAR				instruction address register
   ACC				accumulator
   EXT				accumulator extension
   Oflow			overflow bit
   Carry			carry bit
   CES				console entry switches
   ipl				current interrupt level, -1 = non interrupt
   iplpending		bitmap of pending interrupts
   wait_state		current CPU state: running or waiting
   DSW				console run/stop switch device status word
   RUNMODE			processor step/run mode (may also imply IntRun)
   BREAK			breakpoint address
   WRU				simulator-break character
   IntRun			Int Run flag (causes level 5 interrupt after every instruction)
   ILSW0..5			interrupt level status words
   IPS				instructions per second throttle (not a real 1130 register)

   The SAR (storage address register) and SBR (storage buffer register) are updated
   but not saved in the CPU state; they matter only to the GUI.

   Interrupt handling: interrupts occur when any device on any level has an
   active interrupt.  XIO commands can clear specific IRQ bits. When this
   happens, we have to evaluate all devices on the same IRQ level for remaining
   indicators. The flag int_req is set with a bit corresponding to the IRQ level
   when any interrupt indicator is activated.

   The 1130 console has a switch that controls several run modes: SS (single processor
   step), SCLK (single clock step), SINST (single instruction step), INT_RUN
   (IRQ 5 after each non interrupt-handler instruction) and RUN (normal operation).
   This simulator does not implement SS and SCLK. The simulator GUI console handles
   SINST, so we only have to worry about INT_RUN. The console command SET CPU IntRun sets
   the tmode (trace mode) flag; this causes a level 5 interrupt after each
   instruction.

   The IBM 1130 instruction formats are

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+	
   |  opcode      | F|  T  |                       |   general format
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |  opcode      | 0|  T  |     DISPLACEMENT      |   short instruction
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |  opcode      | 1|  T  | I|     MODIFIER       |   long instruction
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |                  ADDRESS                      |   
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   opcode in MSBits

   F = format. 0 = short (1 word), 1 = long (2 word) instruction

   T = Tag    00 = no index register (e.g. IAR relative)
              01 = use index register 1 (e.g. core address 1 = M[1])
              02 = use index register 2 (e.g. core address 2 = M[2])
              03 = use index register 3 (e.g. core address 3 = M[3])

   DISPLACEMENT = two's complement (must be sign-extended)

   I = Indirect

   Note that IAR = instruction address+1 when instruction is being decoded.

   In normal addressing mode, effective address (EA) is computed as follows:

   F = 0  T = 0         EA = IAR + DISPLACEMENT
       0      1              IAR + DISPLACEMENT + M[1] 
       0      2              IAR + DISPLACEMENT + M[2]
       0      3              IAR + DISPLACEMENT + M[3]

   F = 1  T = 0  I = 0  EA = ADDRESS
       1      1      0       ADDRESS + M[1]
       1      2      0       ADDRESS + M[2]
       1      3      0       ADDRESS + M[3]
       1      0      1       M[ADDRESS]
       1      1      1       M[ADDRESS + M[1]]
       1      2      1       M[ADDRESS + M[2]]
       1      3      1       M[ADDRESS + M[3]]

   Loads or stores are then made to/from MEM[EA]. Some instructions have special
   weird addressing modes. Simulator code precomputes standard addressing for
   all instructions though it's not always used.

   General notes:

   Adding I/O devices requires modifications to three modules:

	ibm1130_defs.h		add interrupt request definitions
	ibm1130_cpu.c		add XIO command linkages
	ibm1130_sys.c		add to sim_devices
*/

/* ------------------------------------------------------------------------
 * Definitions
 * ------------------------------------------------------------------------ */

#include <stdarg.h>

#include "ibm1130_defs.h"

#define save_ibkpt	(cpu_unit.u3)			/* will be SAVEd */

#define UPDATE_INTERVAL	2500	 			// GUI: set to 100000/f where f = desired updates/second of 1130 time

/* ------------------------------------------------------------------------
 * initializers for globals
 * ------------------------------------------------------------------------ */

#define SIGN_BIT(v)   ((v) & 0x8000)
#define DWSIGN_BIT(v) ((v) & 0x80000000)

#define MODE_SS				3		/* RUNMODE values. SS and SMC are not implemented in this simulator */
#define MODE_SMC			2
#define MODE_INT_RUN		1
#define MODE_RUN			0
#define MODE_SI				-1
#define MODE_DISP			-2
#define MODE_LOAD			-3

uint16 M[MAXMEMSIZE];				/* core memory, up to 32Kwords */
uint16 ILSW[6] = {0,0,0,0,0,0};		/* interrupt level status words */
int32 IAR;							/* instruction address register */
int32 SAR, SBR;						/* storage address/buffer registers */
int32 OP, TAG, CCC;					/* instruction decoded pieces */
int32 CES;							/* console entry switches */
int32 ACC, EXT;						/* accumulator and extension */
int32 RUNMODE;						/* processor run/step mode */
int32 ipl = -1;						/* current interrupt level (-1 = not handling irq) */
int32 iplpending = 0;				/* interrupted IPL's */
int32 tbit = 0;						/* trace flag (causes level 5 IRQ after each instr) */
int32 V = 0, C = 0;					/* condition codes */
int32 wait_state = 0;				/* wait state (waiting for an IRQ) */
int32 int_req = 0;					/* sum of interrupt request levels active */
int32 int_mask;						/* current active interrupt mask (ipl sensitive) */
int32 SR = 0;						/* switch register */
int32 DR = 0;						/* display register */
int32 mem_mask;
int32 IPS = 0;						/* throttle: instructions per second */
int32 ibkpt_addr = -1;				/* breakpoint addr */

t_bool display_console = 1;

/* ------------------------------------------------------------------------
 * Function declarations
 * ------------------------------------------------------------------------ */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_svc (UNIT *uptr);
t_stat cpu_set_size (UNIT *uptr, int32 value);

t_stat console_reset (DEVICE *dptr);

extern t_stat ts_wr (int32 data, int32 addr, int32 access);

extern UNIT cr_unit;

void calc_ints (void);

static t_bool bsctest (int32 DSPLC, t_bool reset_V);
static void   exit_irq (void);
static void   trace_instruction (void);

static int32 int_masks[6] = {
	0x00, 0x20, 0x30, 0x38, 0x3C, 0x3E		/* IPL 0 is highest prio (sees no other interrupts) */
};

static void init_console_window (void);
static void destroy_console_window (void);
static void sleep_msec (int msec);

/* ------------------------------------------------------------------------ 
 * cpu IO state
 * ------------------------------------------------------------------------ */

static int con_dsw = 0;

/* ------------------------------------------------------------------------
 * CPU data structures:
 *    cpu_dev	CPU device descriptor
 *    cpu_unit	CPU unit descriptor
 *    cpu_reg	CPU register list
 *    cpu_mod	CPU modifier list
 * ------------------------------------------------------------------------ */

UNIT cpu_unit = { UDATA (&cpu_svc, UNIT_FIX + UNIT_BINK, INIMEMSIZE) };

REG cpu_reg[] = {
	{ HRDATA (IAR, IAR, 32) },
	{ HRDATA (ACC, ACC, 32) },
	{ HRDATA (EXT, EXT, 32) },
	{ FLDATA (Oflow, V, 1) },
	{ FLDATA (Carry, C, 1) },
	{ HRDATA (CES, CES, 32) },
	{ HRDATA (ipl, ipl, 32), REG_RO },
	{ HRDATA (iplpending, iplpending, 32), REG_RO },
	{ HRDATA (wait_state, wait_state, 32)},
	{ HRDATA (DSW, con_dsw, 32), REG_RO },
	{ HRDATA (RUNMODE, RUNMODE, 32) },
	{ HRDATA (BREAK, ibkpt_addr, 32) },
	{ ORDATA (WRU, sim_int_char, 8) },
	{ FLDATA (IntRun, tbit, 1) },

	{ HRDATA (ILSW0, ILSW[0], 32), REG_RO },
	{ HRDATA (ILSW1, ILSW[1], 32), REG_RO },
	{ HRDATA (ILSW2, ILSW[2], 32), REG_RO },
	{ HRDATA (ILSW3, ILSW[3], 32), REG_RO },
	{ HRDATA (ILSW4, ILSW[4], 32), REG_RO },
	{ HRDATA (ILSW5, ILSW[5], 32), REG_RO },

	{ HRDATA (IPS, IPS, 32) },

	{ NULL}
};

MTAB cpu_mod[] = {
	{ UNIT_MSIZE,  4096, NULL, "4KW",  &cpu_set_size},
	{ UNIT_MSIZE,  8192, NULL, "8KW",  &cpu_set_size},
	{ UNIT_MSIZE, 16384, NULL, "16KW", &cpu_set_size},
	{ UNIT_MSIZE, 32768, NULL, "32KW", &cpu_set_size},
	{ 0 }  };

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 16, 16, 1, 16, 16,
	&cpu_ex, &cpu_dep, &cpu_reset,
	NULL, NULL, NULL };

REG console_reg[] = {							// the GUI, so you can use Enable/Disable console
	{HRDATA (*DEVENB, display_console, 1) },
	{NULL}
};

DEVICE console_dev = {
	"CONSOLE", NULL, console_reg, NULL,
	0, 16, 16, 1, 16, 16,
	NULL, NULL, console_reset,
	NULL, NULL, NULL };

/* ------------------------------------------------------------------------ 
	Memory read/write -- save SAR and SBR on the way in and out
 * ------------------------------------------------------------------------ */

int32 ReadW  (int32 a)
{
	SAR = a;
	SBR = (int32) M[(a) & mem_mask];
	return SBR;
}

void WriteW (int32 a, int32 d)
{
	SAR = a;
	SBR = d;
	M[a & mem_mask] = (int16) d;
}

/* ------------------------------------------------------------------------ 
 * upcase - force a string to uppercase (ASCII)
 * ------------------------------------------------------------------------ */

char *upcase (char *str)
{
	char *s;

	for (s = str; *s; s++) {
		if (*s >= 'a' && *s <= 'z')
			*s -= 32;
	} 

	return str;
}

/* ------------------------------------------------------------------------ 
 * calc_ints - set appropriate bits in int_req if any interrupts are pending on given levels
 *
 * int_req:
 *    bit  5  4  3  2  1  0
 *          \  \  \  \  \  \
 *			 \  \  \  \  \  interrupt level 5 pending (lowest priority)
 *            \    . . .
 *             interrupt level 0 pending (highest priority)
 *
 * int_mask is set according to current interrupt level (ipl)
 *
 *			0  0  0  0  0  0	ipl = 0 (currently servicing highest priority interrupt)
 *			1  0  0  0  0  0		  1
 *			1  1  0  0  0  0		  2
 *			1  1  1  0  0  0		  3
 *			1  1  1  1  0  0		  4
 *		    1  1  1  1  1  0		  5 (currently servicing lowest priority interrupt)
 *			1  1  1  1  1  1		 -1 (not servicing an interrupt)
 * ------------------------------------------------------------------------ */

void calc_ints (void)
{
	register int i;
	register int32 newbits = 0;

    GUI_BEGIN_CRITICAL_SECTION	 			// using critical section here so we don't mislead the GUI thread

	for (i = 6; --i >= 0; ) {
		newbits >>= 1;
		if (ILSW[i])
			newbits |= 0x20;
	}

	int_req  = newbits;
	int_mask = (ipl < 0) ? 0xFFFF : int_masks[ipl];		/* be sure this is set correctly */

    GUI_END_CRITICAL_SECTION
}

/* ------------------------------------------------------------------------
 * instruction processor
 * ------------------------------------------------------------------------ */

#define INCREMENT_IAR 	IAR = (IAR + 1) & mem_mask
#define DECREMENT_IAR 	IAR = (IAR - 1) & mem_mask

void bail (char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

static void weirdop (char *msg, int offset)
{
	fprintf(stderr, "Weird opcode: %s at %04x\n", msg, IAR+offset);
}

static char *xio_devs[]  = {
	"0?", "console", "1142card", "1134papertape",
	"dsk0", "1627plot", "1132print", "switches",
	"1231omr", "2501card", "comm", "b?",
	"sys7", "d?", "e?", "f?",
	"10?", "dsk1", "dsk2", "dsk3",
	"dsk4", "dsk5", "dsk6", "dsk7+",
	"18?", "2250disp", "1a?", "1b",
	"1c?", "1d?", "1e?", "1f?"
};

static char *xio_funcs[] = {
	"0?", "write", "read", "sense_irq",
	"control", "initw", "initr", "sense"
};

static t_stat reason;						/* execution loop control */
static t_bool running = FALSE;
static t_bool power   = TRUE;

t_stat sim_instr (void)
{
	extern int32 sim_interval;
	extern UNIT *sim_clock_queue;
	int32 i, eaddr, INDIR, IR, F, DSPLC, word2, oldval, newval, src, src2, dst, abit, xbit;
	int32 iocc_addr, iocc_op, iocc_dev, iocc_func, iocc_mod;
	char msg[50];
	int cwincount = 0, idelay, status;
	static long ninstr = 0;

	if (running)							/* this is definitely not reentrant */
		return -1;

	if (! power)							/* this matters only to the GUI */
		return STOP_POWER_OFF;

	running = TRUE;

	mem_mask = MEMSIZE - 1;					/* set other useful variables */
	calc_ints();

	/* Main instruction fetch/decode loop */

	reason = 0;

	idelay = (IPS == 0) ? 0 : 1000/IPS;

#ifdef GUI_SUPPORT
	update_gui(TRUE);
#endif

	while (reason == 0)  {
#ifdef GUI_SUPPORT
		if (idelay) {						/* if we're running in slow mode, update GUI every time */
			update_gui(TRUE);
			sleep_msec(idelay);
		}
		else {
#if (UPDATE_INTERVAL > 0)
			if (--cwincount <= 0) {
				update_gui(FALSE);			/* update console lamps only every so many instructions */
				cwincount = UPDATE_INTERVAL + (rand() % MIN(UPDATE_INTERVAL, 32));
			}
#else
			update_gui(FALSE);
#endif // UPDATE_INTERVAL
		}
#endif // GUI_SUPPORT

		if (sim_interval <= 0) {			/* any events timed out? */
			if (sim_clock_queue != NULL) {
				if ((status = sim_process_event()) != 0)
					reason = status;
				calc_ints();
				continue;
			}
		}

		if (int_req & int_mask) {			/* any pending interrupts? */
			for (i = 0; i <= 5; i++)		/* find highest pending interrupt */
				if ((int_req & int_mask) & (0x20 >> i))
					break;

			if (i >= 6) {					/* nothing to do? */
				calc_ints();				/* weird. recalculate */
				continue; 					/* back to fetch */
			}

			GUI_BEGIN_CRITICAL_SECTION

			if (ipl >= 0)					/* save previous IPL in bit stack */
				iplpending |= (0x20 >> ipl);

			ipl = i;						/* set new interrupt level */
			int_mask = int_masks[i];		/* set appropriate mask */

			GUI_END_CRITICAL_SECTION

			wait_state = 0;					/* exit wait state */
			eaddr = ReadW(8+i);				/* get IRQ vector */
			WriteW(eaddr, IAR);				/* save IAR */
			IAR = (eaddr+1) & mem_mask;		/* go to next address */
			continue;						/* now continue processing */
		}									/* end if int_req */

		if (wait_state) {					/* waiting? */
			sim_interval = 0;				/* run the clock out */

			if (sim_qcount() <= 1) {		/* waiting for keyboard only */
				if (keyboard_is_locked()) {		/* CPU is not expecting a keystroke */
					if (wait_state == WAIT_OP)
						reason = STOP_WAIT;		/* end the simulation */
					else
						reason = STOP_INVALID_INSTR;
				}
				else {						/* we are actually waiting for a keystroke */
					if ((status = sim_process_event()) != 0) /* get it with wait_state still set */
						reason = status;
				}
			}
			else if (sim_clock_queue == NULL) {	/* not waiting for anything */
				if (wait_state == WAIT_OP)
					reason = STOP_WAIT;			/* end the simulation */
				else
					reason = STOP_INVALID_INSTR;
			}

			continue;
		}

		if (IAR == ibkpt_addr) {			/* simulator breakpoint? */
			save_ibkpt = ibkpt_addr;					/* save bkpt */
			ibkpt_addr = ibkpt_addr | ILL_ADR_FLAG;		/* disable */
			sim_activate(&cpu_unit, 1);					/* sched re-enable after next instruction */
			reason = STOP_IBKPT;						/* stop simulation */
			cwincount = 0;
			continue;
		}

		ninstr++;
		trace_instruction();				/* log CPU details if logging is enabled */

		IR = ReadW(IAR);					/* fetch 1st word of instruction */
		INCREMENT_IAR;
		sim_interval = sim_interval - 1;	/* this constitutes one tick of the simulation clock */

		OP  = (IR >> 11) & 0x1F;			/* opcode */
		F   = IR & 0x0400;					/* format bit: 1 = long instr */
		TAG = IR & 0x0300;					/* tag bits: index reg x */
		if (TAG)
			TAG >>= 8;

		// here I compute the usual effective address on the assumption that the instruction will need it. Some don't.

		if (F) {							/* long instruction, ASSUME it's valid (have to decrement IAR if not) */
			INDIR = IR & 0x0080;			/* indirect bit */
			DSPLC = IR & 0x007F;			/* displacement or modifier */
			if (DSPLC & 0x0040)
				DSPLC |= ~ 0x7F;			/* sign extend */

			word2 = ReadW(IAR);				/* get reference address */
			INCREMENT_IAR;					/* bump the instruction address register */

			eaddr = word2;					/* assume standard addressing & compute effective address */
			if (TAG)						/* if indexed */
				eaddr += ReadW(TAG);		/* add index register value (stored in core) */
			if (INDIR)						/* if indirect addressing */
				eaddr = ReadW(eaddr);		/* pick up referenced address */
		}
		else {								/* short instruction, use displacement */
			INDIR = 0;						/* never indirect */
			DSPLC = IR & 0x00FF;			/* get displacement */
			if (DSPLC & 0x0080)
				DSPLC |= ~ 0xFF;

			if (TAG)						/* if indexed */
				eaddr = ReadW(TAG) + DSPLC;	/* add index register value (stored in core) */
			else
				eaddr = IAR + DSPLC;		/* otherwise relative to IAR after fetch */
		}

		switch (OP) { /* decode instruction */
			case 0x01:						/* --- XIO --- */
				iocc_addr = ReadW(eaddr);	/* get IOCC packet */
				iocc_op   = ReadW(eaddr|1);	/* note 'or' not plus, address must be even for proper operation */

				iocc_dev  = (iocc_op  >> 11) & 0x001F;
				iocc_func = (iocc_op  >>  8) & 0x0007;
				iocc_mod  =  iocc_op         & 0x00FF;

				trace_io("* XIO %s %s mod %02x addr %04x", xio_funcs[iocc_func], xio_devs[iocc_dev], iocc_mod, iocc_addr);

				ACC = 0;					/* ACC is destroyed, and default XIO_SENSE_DEV result is 0 */

				switch (iocc_func) {
					case XIO_UNUSED:
						sprintf(msg, "Unknown XIO op %x on XIO device %02x", iocc_func, iocc_dev);
						xio_error(msg);
						break;
					
					case XIO_SENSE_IRQ:				/* examine current Interrupt Level Status Word */
						ACC = (ipl >= 0) ? ILSW[ipl] : 0;
						break;
					
					default:						/* perform device-specific operation */
						switch (iocc_dev) {
							case 0x01:				/* console keyboard and printer */
								xio_1131_console(iocc_addr, iocc_func, iocc_mod);
								break;
							case 0x02:				/* 1142 card reader/punch */
								xio_1142_card(iocc_addr, iocc_func, iocc_mod);
								break;
							case 0x03:				/* 1134 paper tape reader/punch */
								xio_1134_papertape(iocc_addr, iocc_func, iocc_mod);
								break;
							case 0x04:				/* CPU disk storage */
								xio_disk(iocc_addr, iocc_func, iocc_mod, 0);
								break;
							case 0x05:				/* 1627 plotter */
								xio_1627_plotter(iocc_addr, iocc_func, iocc_mod);
								break;
							case 0x06:				/* 1132 Printer */
								xio_1132_printer(iocc_addr, iocc_func, iocc_mod);
								break;
							case 0x07:				/* console switches, stop key, run mode */
								xio_1131_switches(iocc_addr, iocc_func, iocc_mod);
								break;
							case 0x08:				/* 1231 optical mark reader */
								xio_1231_optical(iocc_addr, iocc_func, iocc_mod);
								break;
							case 0x09:				/* 2501 card reader */
								xio_2501_card(iocc_addr, iocc_func, iocc_mod);
								break;
							case 0x0a:				/* synchronous comm adapter */
								xio_1131_synch(iocc_addr, iocc_func, iocc_mod);
								break;
							case 0x0c:				/* IBM System/7 interprocessor link */
								xio_system7(iocc_addr, iocc_func, iocc_mod);
								break;
							case 0x11:				/* 2310 Disk Storage, Drive 1, or 2311 Disk Storage Drive. Drive 1, Disk 1 */
								xio_disk(iocc_addr, iocc_func, iocc_mod, 1);
								break;
							case 0x12:				/* 2310 Disk Storage, Drive 2, or 2311 Disk Storage Drive. Drive 1, Disk 2 */
								xio_disk(iocc_addr, iocc_func, iocc_mod, 2);
								break;
							case 0x13:				/* 2310 Disk Storage, Drive 3, or 2311 Disk Storage Drive. Drive 1, Disk 3 */
								xio_disk(iocc_addr, iocc_func, iocc_mod, 3);
								break;
							case 0x14:				/* 2310 Disk Storage, Drive 4, or 2311 Disk Storage Drive. Drive 1, Disk 4 */
								xio_disk(iocc_addr, iocc_func, iocc_mod, 4);
								break;
							case 0x15:				/* 1403 Printer */
								xio_1403_printer(iocc_addr, iocc_func, iocc_mod);
								break;
							case 0x16:				/* 2311 Disk Storage Drive. Drive 1, Disk 5 */
								xio_disk(iocc_addr, iocc_func, iocc_mod, -1);
								break;
							case 0x17:				/* 2311 Disk Storage Drive, Drive 2, Disk 1 through 5 */
								xio_disk(iocc_addr, iocc_func, iocc_mod, -1);
								break;
							case 0x19:				/* 2250 Display Unit */
								xio_2250_display(iocc_addr, iocc_func, iocc_mod);
								break;
							default:
								sprintf(msg, "XIO on unknown device %02x", iocc_dev);
								xio_error(msg);
								break;
						}
				}

				calc_ints();				/* after every XIO, reset int_mask just in case */
				break;

			case 0x02:						/* --- SLA,SLT,SLC,SLCA,NOP - Shift Left family --- */
				if (F) {
					weirdop("Long Left Shift", -2);
					DECREMENT_IAR;
				}

				CCC = ((TAG == 0) ? DSPLC : ReadW(TAG)) & 0x003F;
				if (CCC == 0)
					break;					/* shift of zero is a NOP */

				switch (IR & 0x00C0) {
					case 0x0040:			/* SLCA */
						if (TAG) {
							while (CCC > 0 && (ACC & 0x8000) == 0) {
								ACC <<= 1;
								CCC--;
							}
							C = (CCC != 0);
							WriteW(TAG, ReadW(TAG) & 0xFF00 | CCC);		/* put low 6 bits back into index register and zero bits 8 and 9 */
							break;
						}
						/* if TAG == 0, fall through and treat like normal shift SLA */

					case 0x0000:			/* SLA  */
						while (CCC > 0) {
							C    = (ACC & 0x8000);
							ACC  = (ACC << 1) & 0xFFFF;
							CCC--;
						}
						break;

					case 0x00C0:			/* SLC  */
						if (TAG) {
							while (CCC > 0 && (ACC & 0x8000) == 0) {
								abit = (EXT & 0x8000) >> 15;
								ACC  = ((ACC << 1) & 0xFFFF) | abit;
								EXT  = (EXT << 1);
								CCC--;
							}
							C = (CCC != 0);
							WriteW(TAG, ReadW(TAG) & 0xFF00 | CCC);		/* put 6 bits back into low byte of index register */
							break;
						}
						/* if TAG == 0, fall through and treat like normal shift SLT */

					case 0x0080:			/* SLT  */
						while (CCC > 0) {
							C    = (ACC & 0x8000);
							abit = (EXT & 0x8000) >> 15;
							ACC  = ((ACC << 1) & 0xFFFF) | abit;
							EXT  =  (EXT << 1) & 0xFFFF;
							CCC--;
						}
						break;

					default:
						bail("SLA switch, can't happen");
						break;
				}
				break;

			case 0x03:						/* --- SRA, SRT, RTE - Shift Right family --- */
				if (F) {
					weirdop("Long Right Shift", -2);
					DECREMENT_IAR;
				}

				CCC = ((TAG == 0) ? DSPLC : ReadW(TAG)) & 0x3F;
				if (CCC == 0)
					break;					/* NOP */

				switch (IR & 0x00C0) {
					case 0x0000:			/* SRA  */
						ACC = (CCC < 16) ? ((ACC & 0xFFFF) >> CCC) : 0;
						CCC = 0;
						break;

					case 0x0040:			/* invalid */
						wait_state = WAIT_INVALID_OP;
						break;

					case 0x0080:			/* SRT */
						while (CCC > 0) {
							xbit = (ACC & 0x0001) << 15;
							abit = (ACC & 0x8000);
							ACC  = (ACC >> 1) & 0x7FFF | abit;
							EXT  = (EXT >> 1) & 0x7FFF | xbit;
							CCC--;
						}
						break;

					case 0x00C0:			/* RTE */
						while (CCC > 0) {
							abit = (EXT & 0x0001) << 15;
							xbit = (ACC & 0x0001) << 15;
							ACC  = (ACC >> 1) & 0x7FFF | abit;
							EXT  = (EXT >> 1) & 0x7FFF | xbit;
							CCC--;
						}
						break;

					default:
						bail("SRA switch, can't happen");
						break;
				}
				break;

			case 0x04:						/* --- LDS - Load Status --- */
				if (F) {					/* never fetches second word */
					weirdop("Long LDS", -2);
					DECREMENT_IAR;
				}

				V = (DSPLC & 1);
				C = (DSPLC & 2) >> 1;
				break;

			case 0x05:						/* --- STS - Store Status --- */
				newval = ReadW(eaddr) & 0xFF00;
				if (C)
					newval |= 2;
				if (V)
					newval |= 1;

				WriteW(eaddr, newval);
				C = V = 0;					/* clear flags after storing */
				break;

			case 0x06:						/* --- WAIT --- */
				wait_state = WAIT_OP;
				/* note: not valid in long mode, but what happens if we try? */
				if (F) {
					weirdop("Long WAIT", -2);
					DECREMENT_IAR;			/* assume it wouldn't have fetched 2nd word */
				}
				break;

			case 0x08:						/* --- BSI - Branch and store IAR --- */
				if (F) {
					if (bsctest(IR, F))		/* do standard BSC long format testing */
						break;				/* if any condition is true, do nothing */
				}
				WriteW(eaddr, IAR);			/* do subroutine call */
				IAR = (eaddr + 1) & mem_mask;
				break;

			case 0x09:						/* --- BSC - Branch and skip on Condition --- */
				if (F) {
					if (bsctest(IR, F))		/* long format; any indicator cancels branch */
						break;

					IAR = eaddr;			/* no indicator means branch taken */
				}
				else {						/* short format: skip if any indicator hits */
					if (bsctest(IR, F))
						INCREMENT_IAR;
				}
// 27Mar02: moved this test out of the (F) condition; BOSC works even in the
// short form. The displacement field in this instruction is always the set of
// condition bits, and the interrupt clear bit doesn't collide.
				if (DSPLC & 0x40) {		/* BOSC = exit from interrupt handler */
					exit_irq();
					cwincount = 0;
				}
				break;

			case 0x0c:						/* --- LDX - Load Index	--- */
				if (F)
					eaddr = (INDIR) ? ReadW(word2) : word2;
				else
					eaddr = DSPLC;

				if (TAG)
					WriteW(TAG, eaddr);
				else
					IAR = eaddr;			/* what happens in short form? can onlyjump to low addresses? */
				break;

			case 0x0d:						/* --- STX - Store Index --- */
				if (F) {					/* compute EA without any indexing */
					eaddr = (INDIR) ? ReadW(word2) : word2;
				}
				else {
					eaddr = IAR + DSPLC;
				}
				WriteW(eaddr, TAG ? ReadW(TAG) : IAR);
				break;

			case 0x0e:						/* --- MDX - Modify Index and Skip --- */
				if (F) {					/* long mode: adjust memory location */
					if (TAG) {
						oldval = ReadW(TAG);		/* add word2 to index */
						newval = oldval + (INDIR ? ReadW(word2) : word2);
						WriteW(TAG, newval);
					}
					else {
						oldval = ReadW(word2);
						DSPLC = IR & 0x00FF;		/* use extended displacement (includes INDIR bit) */
						if (DSPLC & 0x0080)
							DSPLC |= ~ 0xFF;
						newval = oldval + DSPLC;	/* add modifier to @word2 */
						WriteW(word2, newval);
					}
				}
				else {						/* short mode: adust IAR or index */
					if (TAG) {
						oldval = ReadW(TAG);/* add displacement to index */
						newval = oldval + DSPLC;
						WriteW(TAG, newval);
					}
					else {
						oldval = IAR;		/* add displacement to IAR */
						newval = IAR + DSPLC;
						IAR    = newval & mem_mask;
					}
				}

				if ((F || TAG) && ((newval == 0) || ((oldval & 0x8000) != (newval & 0x8000))))
					INCREMENT_IAR;			/* skip if index sign change or zero */

				break;

			case 0x10:						/* --- A - Add --- */
				/* in adds and subtracts, carry is set or cleared, overflow is set only */
				src  = ReadW(eaddr);
				src2 = ACC;
				ACC  = (ACC + src) & 0xFFFF;

				C = ACC < src;
				if (! V)
					V = SIGN_BIT((~src ^ src2) & (src ^ ACC));
				break;

			case 0x11:						/* --- AD - Add Double --- */
				src  = ((ACC << 16) + (EXT & 0xFFFF));
				src2 = (ReadW(eaddr) << 16) + ReadW(eaddr|1);
				dst  = src + src2;
				ACC  = (dst >> 16) & 0xFFFF;
				EXT  = dst & 0xFFFF;

				C = (unsigned int32) dst < (unsigned int32) src;
				if (! V)
					V = DWSIGN_BIT((~src ^ src2) & (src ^ dst));
				break;

			case 0x12:						/* --- S - Subtract	--- */
				src  = ACC;
				src2 = ReadW(eaddr);
				ACC  = (ACC-src2) & 0xFFFF;

				C = src2 < src;
				if (! V)
					V = SIGN_BIT((src ^ src2) & (src ^ ACC));
				break;

			case 0x13:						/* --- SD - Subtract Double	--- */
				src  = ((ACC << 16) + (EXT & 0xFFFF));
				src2 = (ReadW(eaddr) << 16) + ReadW(eaddr|1);
				dst  = src - src2;
				ACC  = (dst >> 16) & 0xFFFF;
				EXT  = dst & 0xFFFF;

				C = (unsigned int32) src2 < (unsigned int32) src;
				if (! V)
					V = DWSIGN_BIT((src ^ src2) & (src ^ dst));
				break;

			case 0x14:						/* --- M - Multiply	--- */
				dst  = ACC * ReadW(eaddr);
				ACC  = (dst >> 16) & 0xFFFF;
				EXT  = dst & 0xFFFF;
				break;

			case 0x15:						/* --- D - Divide --- */
				src  = ((ACC << 16) + EXT);
				src2 = ReadW(eaddr);
				if (src2 == 0)
					V = 1;					/* divide by zero just sets overflow, ACC & EXT are undefined */
				else {
					ACC = (src / src2) & 0xFFFF;
					EXT = (src % src2) & 0xFFFF;
				}
				break;

			case 0x18:						/* --- LD - Load ACC --- */
				ACC = ReadW(eaddr);
				break;

			case 0x19:						/* --- LDD - Load Double --- */
				ACC = ReadW(eaddr);
				EXT = ReadW(eaddr|1);		/* notice address is |1 not +1 */
				break;

			case 0x1a:						/* --- STO - Store ACC --- */
				WriteW(eaddr, ACC);
				break;

			case 0x1b:						/* --- STD - Store Double --- */
				WriteW(eaddr|1, EXT);
				WriteW(eaddr,   ACC);		/* order is important: if odd addr, only ACC is stored */
				break;

			case 0x1c:						/* --- AND - Logical AND --- */
				ACC &= ReadW(eaddr);
				break;

			case 0x1d:						/* --- OR - Logical OR --- */
				ACC |= ReadW(eaddr);
				break;

			case 0x1e:						/* --- EOR - Logical Excl OR --- */
				ACC ^= ReadW(eaddr);
				break;

			default:
/* all invalid instructions act like waits */
/*			case 0x00: */
/*			case 0x07: */
/*			case 0x0a: */
/*			case 0x0b: */
/*			case 0x0e: */
/*			case 0x0f: */
/*			case 0x16: */
/*			case 0x17: */
/*			case 0x1f: */
				wait_state = WAIT_INVALID_OP;
				if (F)
					DECREMENT_IAR;			/* assume it wouldn't have fetched 2nd word? */

				break;
		}									/* end instruction decode switch */

		if (RUNMODE != MODE_RUN && RUNMODE != MODE_INT_RUN)
			reason = STOP_WAIT;

		if (tbit && (ipl < 0)) {			/* if INT_RUN mode, set IRQ5 after this instr */
			GUI_BEGIN_CRITICAL_SECTION
			SETBIT(con_dsw, CON_DSW_INT_RUN);
			SETBIT(ILSW[5], ILSW_5_INT_RUN);
			int_req |= INT_REQ_5;
			GUI_END_CRITICAL_SECTION
		}
	}										/* end main loop */

	running = FALSE;

	if (reason == STOP_WAIT || reason == STOP_INVALID_INSTR) {
		wait_state = 0;						// on resume, don't wait
	}


	return reason;
}

/* ------------------------------------------------------------------------ 
 * bsctest - perform standard set of condition tests. We return TRUE if any
 * of the condition bits specified in DSPLC test positive, FALSE if none are true.
 * If reset_V is TRUE, we reset the oVerflow flag after testing it.
 * ------------------------------------------------------------------------ */

static t_bool bsctest (int32 DSPLC, t_bool reset_V)
{
	if (DSPLC & 0x01) {						/* Overflow off (note inverted sense) */
		if (! V)
			return TRUE;
		else if (reset_V)					/* reset after testing */
			V = 0;
	}

	if (DSPLC & 0x02) {						/* Carry off (note inverted sense) */
		if (! C)
			return TRUE;
	}

	if (DSPLC & 0x04)	   					/* Even */
		if ((ACC & 1) == 0)
			return TRUE;

	if (DSPLC & 0x08)						/* Positive */
		if ((ACC & 0x8000) == 0 && ACC != 0)
			return TRUE;

	if (DSPLC & 0x10)						/* Negative */
		if (ACC & 0x8000)
			return TRUE;

	if (DSPLC & 0x20)		 				/* Zero */
		if (ACC == 0)
			return TRUE;

	return FALSE;
}

/* ------------------------------------------------------------------------ 
 * exit_irq - pop interrupt stack as part of return from subroutine (BOSC) 
 * ------------------------------------------------------------------------ */

static void exit_irq (void)
{
	int i, bit;
	
	GUI_BEGIN_CRITICAL_SECTION

	ipl = -1;							/* default: return to main processor level */
	int_mask = 0xFFFF;

	if (iplpending) {					/* restore previous interrupt status */
		for (i = 0, bit = 0x20; i < 6; i++, bit >>= 1) {
			if (iplpending & bit) {
				iplpending &= ~bit;
				ipl = i;
				int_mask = int_masks[i];
				break;
			}
		}
	}
	GUI_END_CRITICAL_SECTION

	calc_ints();						/* recompute pending interrupt mask */
}										/* because we probably cleared some ILSW bits before this instruction */

/* ------------------------------------------------------------------------ 
 * SIMH required routines
 * ------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------ 
 * Reset routine
 * ------------------------------------------------------------------------ */

t_stat cpu_reset (DEVICE *dptr)
{
	wait_state = 0;						/* cancel wait */

	GUI_BEGIN_CRITICAL_SECTION

	int_req = 0;						/* reset all interrupts */
	ipl = -1;
	int_mask = 0xFFFF;
	iplpending = 0;
	memset(ILSW, 0, sizeof(ILSW));

	con_dsw = 0;						/* clear int req and prot stop bits */
	tbit = 0;							/* cancel INT_RUN mode */

	C = V = 0;							/* clear processor flags */
	IAR = SAR = SBR = 0;				/* clear IAR and other registers */
	ACC = EXT = OP = TAG = CCC = C = V = 0;

	mem_mask = MEMSIZE - 1;				/* wraparound mask */

	GUI_END_CRITICAL_SECTION

	return cpu_svc(&cpu_unit);			/* reset breakpoint */
}

// reset for the "console" display device 

t_stat console_reset (DEVICE *dptr)
{
	if (display_console)
		init_console_window();
	else
		destroy_console_window();

	return SCPE_OK;
}

/* ------------------------------------------------------------------------ 
 * Memory examine
 * ------------------------------------------------------------------------ */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
	if (vptr == NULL) return SCPE_ARG;

	/* check this out -- save command hits it in weird way */
	/* I wish I remembered what I meant when I wrote that */
	if (addr < MEMSIZE) {
		*vptr = M[addr] & 0xFFFF;
		return SCPE_OK;
	}
	return SCPE_NXM;
}

/* ------------------------------------------------------------------------ 
 * Memory deposit
 * ------------------------------------------------------------------------ */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
	if (addr < MEMSIZE) {
		M[addr] = val & 0xFFFF;
		return SCPE_OK;
	}
	return SCPE_NXM;
}

/* ------------------------------------------------------------------------ 
 * Breakpoint service
 * ------------------------------------------------------------------------ */

t_stat cpu_svc (UNIT *uptr)
{
	if ((ibkpt_addr & ~ILL_ADR_FLAG) == save_ibkpt)
		ibkpt_addr = save_ibkpt;

	save_ibkpt = -1;
	return SCPE_OK;
}

/* ------------------------------------------------------------------------ 
 * Memory allocation
 * ------------------------------------------------------------------------ */

t_stat cpu_set_size (UNIT *uptr, int32 value)
{
	t_bool used;
	int32 i;

	if ((value <= 0) || (value > MAXMEMSIZE) || ((value & 0xFFF) != 0))
		return SCPE_ARG;

	for (i = value, used = FALSE; i < (int32) MEMSIZE; i++) {
		if (M[i] != 0) {
			used = TRUE;
			break;
		}
	}

	if (used && ! get_yn ("Really truncate memory [N]?", FALSE))
		return SCPE_OK;

	for (i = MEMSIZE; i < value; i++)  		/* clear expanded area */
		M[i] = 0;

	MEMSIZE = value;
	mem_mask = MEMSIZE - 1;

	return SCPE_OK;
}

/* ------------------------------------------------------------------------ 
 * IO function for console switches
 * ------------------------------------------------------------------------ */

void xio_1131_switches (int32 addr, int32 func, int32 modify)
{
	char msg[80];

	switch (func) {
		case XIO_READ:
			WriteW(addr, CES);
			break;

		case XIO_SENSE_DEV:
			ACC = con_dsw;
			if (modify & 0x01) {						/* reset interrupts */
				CLRBIT(con_dsw, CON_DSW_PROGRAM_STOP|CON_DSW_INT_RUN);
				CLRBIT(ILSW[5], ILSW_5_INT_RUN);		/* (these bits are set in the keyboard handler in 1130_stddev.c) */
			}
			break;

		default:
			sprintf(msg, "Invalid console switch XIO function %x", func);
			xio_error(msg);
	}
}

/* ------------------------------------------------------------------------ 
 * Illegal IO operation.  Not yet sure what the actual CPU does in this case
 * ------------------------------------------------------------------------ */

void xio_error (char *msg)
{
	fprintf(stderr, "*** XIO error: %s\n", msg);
}

/* ------------------------------------------------------------------------ 
 * LOG device - if attached to a file, records a CPU trace
 * ------------------------------------------------------------------------ */

static t_stat log_reset (DEVICE *dptr);
static t_stat log_attach (UNIT *uptr, char *cptr);

UNIT log_unit = { UDATA (NULL, UNIT_ATTABLE + UNIT_SEQ, 0) };

DEVICE log_dev = {
	"LOG", &log_unit, NULL, NULL,
	1, 16, 16, 1, 16, 16,
	NULL, NULL, log_reset,
	NULL, log_attach, NULL };

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw);

#ifdef WIN32
#   define CRLF "\r\n"
#else
#   define CRLF "\n"
#endif

static t_bool new_log;

static t_stat log_attach (UNIT *uptr, char *cptr)
{
	unlink(cptr);							// delete old log file, if present
	new_log = TRUE;
	return attach_unit(uptr, cptr);
}

static void trace_instruction (void)
{
	t_value v[2];

	if ((log_unit.flags & UNIT_ATT) == 0)
		return;

	if (new_log) {
		fseek(log_unit.fileref, 0, SEEK_END);
		fprintf(log_unit.fileref, " IAR  ACC  EXT  XR1  XR2  XR3 CVI OPERATION" CRLF);
		fprintf(log_unit.fileref, "---- ---- ---- ---- ---- ---- --- ---------" CRLF);
		new_log = FALSE;
	}

	fprintf(log_unit.fileref, "%04x %04x %04x %04x %04x %04x %c%c%c ",
		IAR & 0xFFFF, ACC & 0xFFFF, EXT & 0xFFFF, M[1] & 0xFFFF, M[2] & 0xFFFF, M[3] & 0xFFFF,
		C ? 'C' : ' ', V ? 'V' : ' ',
		(ipl < 0) ? ' ' : (ipl+'0'));

	v[0] = M[ IAR    & mem_mask];
	v[1] = M[(IAR+1) & mem_mask];
	fprint_sym(log_unit.fileref, IAR & mem_mask, v, NULL, SWMASK('M'));

	fputs(CRLF, log_unit.fileref);
}

void trace_io (char *fmt, ...)
{
	va_list args;

	if ((log_unit.flags & UNIT_ATT) == 0)
		return;

	va_start(args, fmt);							// get pointer to argument list
	vfprintf(log_unit.fileref, fmt, args);			// write errors to terminal (stderr)
	va_end(args);

	fputs(CRLF, log_unit.fileref);
}

static t_stat log_reset (DEVICE *dptr)
{
	if ((log_unit.flags & UNIT_ATT) == 0)
		return SCPE_OK;

	fseek(log_unit.fileref, 0, SEEK_END);
	fprintf(log_unit.fileref, "---RESET---" CRLF);
	return SCPE_OK;
}

/* ------------------------------------------------------------------------ 
 * Console display - on Windows builds (only) this code displays the 1130 console
 * and toggle switches. It really enhances the experience.
 *
 * Currently, when the IPS throttle is nonzero, I update the display after every
 * UPDATE_INTERVAL instructions, plus or minus a random amount to avoid aliased
 * sampling in loops.  When UPDATE_INTERVAL is defined as zero, we update every
 * instruction no matter what the throttle. This makes the simulator too slow
 * but it's cool and helpful during development.
 * ------------------------------------------------------------------------ */

#ifndef GUI_SUPPORT

void update_gui (int force)			{}		/* stubs for non-GUI builds */
void forms_check (int set)			{}
void print_check (int set)			{}
void keyboard_select (int select)	{}
void keyboard_selected (int select) {}
void disk_ready (int ready)         {}
void disk_unlocked (int unlocked)   {}
static void init_console_window (void) 	  {}
static void destroy_console_window (void) {}
static void sleep_msec	(int msec)		  {}
#else

#ifdef WIN32

// only have a WIN32 gui right now

#include <windows.h>
#include <math.h>
#include "ibm1130res.h"

static BOOL class_defined = FALSE;
static HWND hConsoleWnd = NULL;
static HBITMAP hBitmap = NULL;
static HFONT  hFont = NULL;
static HFONT  hBtnFont = NULL;
static HBRUSH hbLampOut = NULL;
static HBRUSH hbWhite = NULL;
static HBRUSH hbBlack = NULL;
static HBRUSH hbGray  = NULL;
static HPEN   hSwitchPen = NULL;
static HPEN   hWhitePen  = NULL;
static HPEN   hBlackPen  = NULL;
static HPEN   hLtGreyPen = NULL;
static HPEN   hGreyPen   = NULL;
static HPEN   hDkGreyPen = NULL;

static HCURSOR hcArrow = NULL;
static HCURSOR hcHand  = NULL;
static HINSTANCE hInstance;
static HDC hCDC = NULL;
static char szConsoleClassName[] = "1130CONSOLE";
static DWORD PumpID = 0;
static HANDLE hPump = INVALID_HANDLE_VALUE;
static int bmwid, bmht;

#define BUTTON_WIDTH  90
#define BUTTON_HEIGHT 50

#define IDC_KEYBOARD_SELECT		0
#define IDC_DISK_UNLOCK			1
#define IDC_RUN					2
#define IDC_PARITY_CHECK		3
#define IDC_UNUSED				4
#define IDC_FILE_READY			5
#define IDC_FORMS_CHECK			6
#define IDC_POWER_ON			7
#define IDC_POWER				8
#define IDC_PROGRAM_START		9
#define IDC_PROGRAM_STOP		10
#define IDC_LOAD_IAR			11
#define IDC_KEYBOARD			12
#define IDC_IMM_STOP			13
#define IDC_RESET				14
#define IDC_PROGRAM_LOAD		15

#define LAMPTIME 500			// 500 msec delay on updating
#define UPDATE_TIMER_ID 1

static struct tag_btn {
	int x, y;
	char *txt;
	BOOL pushable, immed_off;
	DWORD offtime;
	COLORREF clr;
	HBRUSH hbrLit, hbrDark;
	HWND   hBtn;
} btn[] = {
	0, 0,	"KEYBOARD\nSELECT",		FALSE,	TRUE,  0, RGB(255,255,180),	NULL, NULL, NULL, 
	0, 1,	"DISK\nUNLOCK",			FALSE, 	TRUE,  0, RGB(255,255,180),	NULL, NULL, NULL, 
	0, 2, 	"RUN",					FALSE,	FALSE, 0, RGB(0,255,0),		NULL, NULL, NULL, 
	0, 3,	"PARITY\nCHECK",		FALSE,	TRUE,  0, RGB(255,0,0),		NULL, NULL, NULL, 

	1, 0,	"",						FALSE, 	TRUE,  0, RGB(255,255,180),	NULL, NULL, NULL, 
	1, 1,	"FILE\nREADY",			FALSE, 	TRUE,  0, RGB(0,255,0),		NULL, NULL, NULL, 
	1, 2,	"FORMS\nCHECK",			FALSE, 	TRUE,  0, RGB(255,255,0),	NULL, NULL, NULL, 
	1, 3,	"POWER\nON",			FALSE, 	TRUE,  0, RGB(255,255,180),	NULL, NULL, NULL, 

	2, 0,	"POWER",				TRUE,	TRUE,  0, RGB(255,255,180), NULL, NULL, NULL, 
	2, 1,	"PROGRAM\nSTART",		TRUE,	TRUE,  0, RGB(0,255,0),		NULL, NULL, NULL, 
	2, 2,	"PROGRAM\nSTOP",		TRUE,	TRUE,  0, RGB(255,0,0),		NULL, NULL, NULL, 
	2, 3,	"LOAD\nIAR",			TRUE,	TRUE,  0, RGB(0,0,255),		NULL, NULL, NULL, 

	3, 0,	"KEYBOARD",				TRUE, 	TRUE,  0, RGB(255,255,180),	NULL, NULL, NULL, 
	3, 1,	"IMM\nSTOP",			TRUE, 	TRUE,  0, RGB(255,0,0),		NULL, NULL, NULL, 
	3, 2,	"CHECK\nRESET",			TRUE, 	TRUE,  0, RGB(0,0,255),		NULL, NULL, NULL, 
	3, 3,	"PROGRAM\nLOAD",		TRUE, 	TRUE,  0, RGB(0,0,255),		NULL, NULL, NULL, 
};
#define NBUTTONS (sizeof(btn) / sizeof(btn[0]))

LRESULT CALLBACK ConsoleWndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static DWORD WINAPI Pump (LPVOID arg);

/* ------------------------------------------------------------------------ 
 * sleep_msec - delay msec to throttle the cpu down
 * ------------------------------------------------------------------------ */

static void sleep_msec (int msec)
{
	Sleep(msec);
}

/* ------------------------------------------------------------------------ 
 * init_console_window - display the 1130 console. Actually just creates a thread 
 * to run the Pump routine which does the actual work.
 * ------------------------------------------------------------------------ */

static void init_console_window (void)
{
	static BOOL did_atexit = FALSE;

	if (hConsoleWnd != NULL)
		return;

	if (PumpID == 0)
		hPump = CreateThread(NULL, 0, Pump, 0, 0, &PumpID);

	if (! did_atexit) {
		atexit(destroy_console_window);
		did_atexit = TRUE;
	}
}

/* ------------------------------------------------------------------------ 
 * destroy_console_window - delete GDI objects.
 * ------------------------------------------------------------------------ */

#define NIXOBJECT(hObj) if (hObj != NULL) {DeleteObject(hObj); hObj = NULL;}

static void destroy_console_window (void)
{
	int i;

	if (hConsoleWnd != NULL)
		SendMessage(hConsoleWnd, WM_CLOSE, 0, 0);	// cross thread call is OK

	if (hPump != INVALID_HANDLE_VALUE) {			// this is not the most graceful way to do it
		TerminateThread(hPump, 0);
		hPump  = INVALID_HANDLE_VALUE;
		PumpID = 0;
		hConsoleWnd = NULL;
	}
	if (hCDC != NULL) {
		DeleteDC(hCDC);
		hCDC = NULL;
	}

	NIXOBJECT(hBitmap)
	NIXOBJECT(hbLampOut)
	NIXOBJECT(hFont)
	NIXOBJECT(hBtnFont);
	NIXOBJECT(hcHand)
	NIXOBJECT(hSwitchPen)
	NIXOBJECT(hLtGreyPen)
	NIXOBJECT(hGreyPen)
	NIXOBJECT(hDkGreyPen)

	for (i = 0; i < NBUTTONS; i++) {
		NIXOBJECT(btn[i].hbrLit);
		NIXOBJECT(btn[i].hbrDark);
	}

//	if (class_defined) {
//		UnregisterClass(hInstance, szConsoleClassName);
//		class_defined = FALSE;
//	}
}

/* ------------------------------------------------------------------------ 
 * these variables hold the displayed versions of the system registers 
 * ------------------------------------------------------------------------ */

static int shown_iar = 0, shown_sar = 0, shown_sbr = 0, shown_afr = 0, shown_acc = 0, shown_ext  = 0;
static int shown_op  = 0, shown_tag = 0, shown_irq = 0, shown_ccc = 0, shown_cnd = 0, shown_wait = 0;
static int shown_ces = 0, shown_runmode = MODE_RUN;
static int CND;

/* ------------------------------------------------------------------------ 
 * RedrawRegion - mark a region for redrawing without background erase
 * ------------------------------------------------------------------------ */

static void RedrawRegion (HWND hWnd, int left, int top, int right, int bottom)
{
	RECT r;

	r.left   = left;
	r.top    = top;
	r.right  = right;
	r.bottom = bottom;

	InvalidateRect(hWnd, &r, FALSE);
}

/* ------------------------------------------------------------------------ 
 * RepaintRegion - mark a region for redrawing with background erase
 * ------------------------------------------------------------------------ */

static void RepaintRegion (HWND hWnd, int left, int top, int right, int bottom)
{
	RECT r;

	r.left   = left;
	r.top    = top;
	r.right  = right;
	r.bottom = bottom;

	InvalidateRect(hWnd, &r, TRUE);
}

/* ------------------------------------------------------------------------ 
 * update_gui - sees if anything on the console display has changed, and invalidates 
 * the changed regions. Then it calls UpdateWindow to force an immediate repaint. This
 * function (update_gui) should probably not be called every time through the main
 * instruction loop but it should be called at least whenever wait_state or int_req change, and then
 * every so many instructions.  It's also called after every simh command so manual changes are
 * reflected instantly.
 * ------------------------------------------------------------------------ */

void update_gui (BOOL force)
{	
	int i, sts;

	if (hConsoleWnd == NULL)
		return;

	CND = 0;	/* combine carry and V as two bits */
	if (C)
		CND |= 2;
	if (V)
		CND |= 1;

	if (RUNMODE == MODE_LOAD)
		SBR = CES;			/* in load mode, SBR follows the console switches */

	if (IAR != shown_iar)
			{shown_iar = IAR; 		 RedrawRegion(hConsoleWnd, 75,    8, 364,  32);}	/* lamps: don't bother erasing bkgnd */
	if (SAR != shown_sar)
			{shown_sar = SAR; 		 RedrawRegion(hConsoleWnd, 75,   42, 364,  65);}
	if (ACC != shown_acc)
			{shown_acc = ACC; 		 RedrawRegion(hConsoleWnd, 75,  141, 364, 164);}
	if (EXT != shown_ext)
			{shown_ext = EXT; 		 RedrawRegion(hConsoleWnd, 75,  174, 364, 197);}
	if (SBR != shown_sbr)
			{shown_sbr = SBR; 		 RedrawRegion(hConsoleWnd, 75,   77, 364,  97);}
	if (OP  != shown_op)		  			 
			{shown_op  = OP;  		 RedrawRegion(hConsoleWnd, 501,   8, 595,  32);}
	if (TAG != shown_tag)
			{shown_tag = TAG; 		 RedrawRegion(hConsoleWnd, 501,  77, 595,  97);}
	if (int_req != shown_irq)
			{shown_irq = int_req;    RedrawRegion(hConsoleWnd, 501, 108, 595, 130);}
	if (CCC != shown_ccc)
			{shown_ccc = CCC;		 RedrawRegion(hConsoleWnd, 501, 141, 595, 164);}
	if (CND != shown_cnd)
			{shown_cnd = CND;        RedrawRegion(hConsoleWnd, 501, 174, 595, 197);}
	if (wait_state != shown_wait)
			{shown_wait= wait_state; RedrawRegion(hConsoleWnd, 380,  77, 414,  97);}
	if (CES != shown_ces)
			{shown_ces = CES; 		 RepaintRegion(hConsoleWnd, 115, 230, 478, 275);}	/* console entry sw: do erase bkgnd */
	if (RUNMODE != shown_runmode)
			{shown_runmode = RUNMODE;RepaintRegion(hConsoleWnd, 270, 359, 330, 418);}

	for (i = 0; i < NBUTTONS; i++) {
		if (btn[i].pushable)
			continue;

		switch (i) {
			case IDC_RUN:				sts = running && ! wait_state;		break;
//			case IDC_PARITY_CHECK:		sts = FALSE;		break;
//			case IDC_POWER_ON:			sts = TRUE;			break;
			default:
				continue;

//			case IDC_FILE_READY:		these windows are enabled&disabled directly
//			case IDC_FORMS_CHECK:
//			case IDC_KEYBOARD_SELECT:
//			case IDC_DISK_UNLOCK:
		}

		if (sts != IsWindowEnabled(btn[i].hBtn)) {		// status has changed
			if (sts || force || btn[i].immed_off) {		// if lamp should be on or must be set now
				EnableWindow(btn[i].hBtn, sts);			// set it and reset cumulative off-time
				btn[i].offtime = 0;
			}
			else if (btn[i].offtime == 0) {				// it just went out, note the time
				btn[i].offtime = GetTickCount();
			}
			else if ((GetTickCount()-btn[i].offtime) >= LAMPTIME) {
				EnableWindow(btn[i].hBtn, FALSE);		// it's been long enough -- switch the lamp off
			}
		}
	}

/*	UpdateWindow(hConsoleWnd); */
}

WNDPROC oldButtonProc = NULL;

/* ------------------------------------------------------------------------ 
 * ------------------------------------------------------------------------ */

LRESULT CALLBACK ButtonProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	int i;

	i = GetWindowLong(hWnd, GWL_ID);

	if (! btn[i].pushable) {
		if (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP || uMsg == WM_LBUTTONDBLCLK)
			return 0;
		if (uMsg == WM_CHAR)
			if ((TCHAR) wParam == ' ')
				return 0;
	}

   	return CallWindowProc(oldButtonProc, hWnd, uMsg, wParam, lParam);
}

static int occurs (char *txt, char ch)
{
	int count = 0;

	while (*txt)
		if (*txt++ == ch)
			count++;

	return count;
}

// turns out to get properly colored buttons you have to paint them yourself. Sheesh.
// On the plus side, this lets do a better job of aligning the button text than
// the button would by itself.

void PaintButton (LPDRAWITEMSTRUCT dis)
{
	int i = dis->CtlID, nc, nlines, x, y, dy;
 	BOOL down = dis->itemState & ODS_SELECTED;
	HPEN hOldPen;
	HFONT hOldFont;
	UINT oldAlign;
	COLORREF oldBk;
	char *txt, *tstart;

	if (! BETWEEN(i, 0, NBUTTONS-1))
		return;

	FillRect(dis->hDC, &dis->rcItem, ((btn[i].pushable || power) && IsWindowEnabled(btn[i].hBtn)) ? btn[i].hbrLit : btn[i].hbrDark);

	if (! btn[i].pushable) {
		hOldPen = SelectObject(dis->hDC, hBlackPen);
		MoveToEx(dis->hDC, dis->rcItem.left,    dis->rcItem.top, NULL);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.top);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.bottom-1);
		LineTo(dis->hDC,   dis->rcItem.left,    dis->rcItem.bottom-1);
		LineTo(dis->hDC,   dis->rcItem.left,    dis->rcItem.top);
	}
	else if (down) {
		// do the three-D thing
		hOldPen = SelectObject(dis->hDC, hDkGreyPen);
		MoveToEx(dis->hDC, dis->rcItem.left,    dis->rcItem.bottom-2, NULL);
		LineTo(dis->hDC,   dis->rcItem.left,    dis->rcItem.top);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.top);

		SelectObject(dis->hDC, hWhitePen);
		MoveToEx(dis->hDC, dis->rcItem.left,    dis->rcItem.bottom-1, NULL);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.bottom-1);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.top);

		SelectObject(dis->hDC, hGreyPen);
		MoveToEx(dis->hDC, dis->rcItem.left+1,  dis->rcItem.bottom-3, NULL);
		LineTo(dis->hDC,   dis->rcItem.left+1,  dis->rcItem.top+1);
		LineTo(dis->hDC,   dis->rcItem.right-3, dis->rcItem.top+1);
	}
	else {
		hOldPen = SelectObject(dis->hDC, hWhitePen);
		MoveToEx(dis->hDC, dis->rcItem.left,    dis->rcItem.bottom-2, NULL);
		LineTo(dis->hDC,   dis->rcItem.left,    dis->rcItem.top);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.top);

		SelectObject(dis->hDC, hDkGreyPen);
		MoveToEx(dis->hDC, dis->rcItem.left,    dis->rcItem.bottom-1, NULL);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.bottom-1);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.top);

		SelectObject(dis->hDC, hGreyPen);
		MoveToEx(dis->hDC, dis->rcItem.left+1,  dis->rcItem.bottom-2, NULL);
		LineTo(dis->hDC,   dis->rcItem.right-2, dis->rcItem.bottom-2);
		LineTo(dis->hDC,   dis->rcItem.right-2, dis->rcItem.top+1);
	}

	SelectObject(dis->hDC, hOldPen);

	hOldFont = SelectObject(dis->hDC, hBtnFont);
	oldAlign = SetTextAlign(dis->hDC, TA_CENTER|TA_TOP);
	oldBk    = SetBkMode(dis->hDC, TRANSPARENT);

	txt = btn[i].txt;
	nlines = occurs(txt, '\n')+1;
	x  = (dis->rcItem.left + dis->rcItem.right)  / 2;
	y  = (dis->rcItem.top  + dis->rcItem.bottom) / 2;

	dy = 14;
	y  = y - (nlines*dy)/2;

	if (down) {
		x += 1;
		y += 1;
	}

	for (;;) {
		for (nc = 0, tstart = txt; *txt && *txt != '\n'; txt++, nc++)
			;

		TextOut(dis->hDC, x, y, tstart, nc);

		if (*txt == '\0')
			break;

		txt++;
		y += dy;
	}

	SetTextAlign(dis->hDC, oldAlign);
	SetBkMode(dis->hDC,    oldBk);
	SelectObject(dis->hDC, hOldFont);
}
	
/* ------------------------------------------------------------------------ 
 * ------------------------------------------------------------------------ */

HWND CreateSubclassedButton (HWND hwParent, int i)
{
	HWND hBtn;
	int x, y;
	int r, g, b;

	y = bmht - (4*BUTTON_HEIGHT) + BUTTON_HEIGHT * btn[i].y;
	x = (btn[i].x < 2) ? (btn[i].x*BUTTON_WIDTH) : (bmwid - (4-btn[i].x)*BUTTON_WIDTH);

	if ((hBtn = CreateWindow("BUTTON", btn[i].txt, WS_CHILD|WS_VISIBLE|BS_CENTER|BS_MULTILINE|BS_OWNERDRAW,
			x, y, BUTTON_WIDTH, BUTTON_HEIGHT, hwParent, (HMENU) i, hInstance, NULL)) == NULL)
		return NULL;

	btn[i].hBtn = hBtn;

	if (oldButtonProc == NULL)
		oldButtonProc = (WNDPROC) GetWindowLong(hBtn, GWL_WNDPROC);

	btn[i].hbrLit = CreateSolidBrush(btn[i].clr);

	if (! btn[i].pushable) {
		r = GetRValue(btn[i].clr) / 4;
		g = GetGValue(btn[i].clr) / 4;
		b = GetBValue(btn[i].clr) / 4;

		btn[i].hbrDark = CreateSolidBrush(RGB(r,g,b));
		EnableWindow(hBtn, FALSE);
	}

	SetWindowLong(hBtn, GWL_WNDPROC, (LONG) ButtonProc);
	return hBtn;
}

/* ------------------------------------------------------------------------ 
 * Pump - thread that takes care of the console window. It has to be a separate thread so that it gets
 * execution time even when the simulator is compute-bound or IO-blocked. This routine creates the window
 * and runs a standard Windows message pump. The window function does the actual display work.
 * ------------------------------------------------------------------------ */

static DWORD WINAPI Pump (LPVOID arg)
{
	MSG msg;
	int wx, wy, i;
	RECT r, ra;
	BITMAP bm;
	WNDCLASS cd;
	HDC hDC;
	HWND hActWnd;

	hActWnd = GetForegroundWindow();

	if (! class_defined) {							/* register Window class */
		hInstance = GetModuleHandle(NULL);

		memset(&cd, 0, sizeof(cd));
		cd.style         = CS_NOCLOSE;
		cd.lpfnWndProc   = ConsoleWndProc;
		cd.cbClsExtra    = 0;
		cd.cbWndExtra    = 0;
		cd.hInstance     = hInstance;
		cd.hIcon         = NULL;
		cd.hCursor       = hcArrow;
		cd.hbrBackground = NULL;
		cd.lpszMenuName  = NULL;
		cd.lpszClassName = szConsoleClassName;

		if (! RegisterClass(&cd)) {
			PumpID = 0;
			return 0;
		}

		class_defined = TRUE;
	}

	hbWhite = GetStockObject(WHITE_BRUSH);			/* create or fetch useful GDI objects */
	hbBlack = GetStockObject(BLACK_BRUSH);			/* create or fetch useful GDI objects */
	hbGray  = GetStockObject(GRAY_BRUSH);
	hSwitchPen = CreatePen(PS_SOLID, 5, RGB(255,255,255));

	hWhitePen  = GetStockObject(WHITE_PEN);
	hBlackPen  = GetStockObject(BLACK_PEN);
	hLtGreyPen = CreatePen(PS_SOLID, 1, RGB(190,190,190));
	hGreyPen   = CreatePen(PS_SOLID, 1, RGB(128,128,128));
	hDkGreyPen = CreatePen(PS_SOLID, 1, RGB(64,64,64));

	hcArrow = LoadCursor(NULL,      IDC_ARROW);
	hcHand  = LoadCursor(hInstance, MAKEINTRESOURCE(IDC_HAND));

	if (hBitmap   == NULL)
		hBitmap   = LoadBitmap(hInstance, MAKEINTRESOURCE(IDB_CONSOLE));
	if (hbLampOut == NULL)
		hbLampOut = CreateSolidBrush(RGB(50,50,50));
	if (hFont     == NULL)
		hFont     = CreateFont(-10, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, FIXED_PITCH, FF_SWISS, "Arial");
	if (hBtnFont  == NULL)
		hBtnFont  = CreateFont(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, FIXED_PITCH, FF_SWISS, "Arial");

	if (hConsoleWnd == NULL) {						/* create window */
		if ((hConsoleWnd = CreateWindow(szConsoleClassName, "IBM 1130", WS_OVERLAPPED, 0, 0, 200, 200, NULL, NULL, hInstance, NULL)) == NULL) {
			PumpID = 0;
			return 0;
		}
	}

	GetObject(hBitmap, sizeof(bm), &bm);			/* get bitmap size */
	bmwid = bm.bmWidth;
	bmht  = bm.bmHeight;

	for (i = 0; i < NBUTTONS; i++)
		CreateSubclassedButton(hConsoleWnd, i);

	EnableWindow(btn[IDC_POWER_ON].hBtn,    TRUE);
	EnableWindow(btn[IDC_DISK_UNLOCK].hBtn, TRUE);

	GetWindowRect(hConsoleWnd, &r);					/* get window size as created */
	wx = r.right  - r.left + 1;
	wy = r.bottom - r.top  + 1;

	if (hCDC == NULL) {								/* get a memory DC and select the bitmap into ti */
		hDC = GetDC(hConsoleWnd);
		hCDC = CreateCompatibleDC(hDC);
		SelectObject(hCDC, hBitmap);
		ReleaseDC(hConsoleWnd, hDC);
	}

	GetClientRect(hConsoleWnd, &r);
	wx = (wx - r.right  - 1) + bmwid;				/* compute new desired size based on how client area came out */
	wy = (wy - r.bottom - 1) + bmht;
	MoveWindow(hConsoleWnd, 0, 0, wx, wy, FALSE);	/* resize window */

	ShowWindow(hConsoleWnd, SW_SHOWNOACTIVATE);		/* display it */
	UpdateWindow(hConsoleWnd);

	if (hActWnd != NULL) {							/* bring console (sim) window back to top */
		GetWindowRect(hConsoleWnd, &r);
		ShowWindow(hActWnd, SW_NORMAL);				/* and move it just below the display window */
		SetWindowPos(hActWnd, HWND_TOP, 0, r.bottom, 0, 0, SWP_NOSIZE);
		GetWindowRect(hActWnd, &ra);
		if (ra.bottom >= GetSystemMetrics(SM_CYSCREEN)) {	/* resize if it goes of bottom of screen */
			ra.bottom = GetSystemMetrics(SM_CYSCREEN) - 1;
			SetWindowPos(hActWnd, 0, 0, 0, ra.right-ra.left+1, ra.bottom-ra.top+1, SWP_NOZORDER|SWP_NOMOVE);
		}
	}

	while (GetMessage(&msg, hConsoleWnd, 0, 0)) {	/* message pump - this basically loops forevermore */
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	if (hConsoleWnd != NULL) {
		DestroyWindow(hConsoleWnd);						/* but if a quit message got posted, clean up */
		hConsoleWnd = NULL;
	}

	PumpID = 0;
	return 0;
}

/* ------------------------------------------------------------------------ 
 * DrawBits - starting at position (x,y), draw lamps for nbits bits of word 'bits',	looking only at masked bits
 * ------------------------------------------------------------------------ */

static void DrawBits (HDC hDC, int x, int y, int bits, int nbits, int mask, char *syms)
{
	int i, b = 0x0001 << (nbits-1);

	for (i = 0; i < nbits; i++, b >>= 1) {
		if (mask & b) {								/* select white or black lettering then write 2 chars */
			SetTextColor(hDC, (b & bits && power) ? RGB(255,255,255) : RGB(0,0,0));
			TextOut(hDC, x, y, syms, 2);
		}
		syms += 2;									/* go to next symbol pair */

		if (i < 10)
			x += 15;								/* step between lamps */
		else
			x += 19;

		if (x < 500) {
			if (b & 0x1110)
				x += 10;							/* step over nibble divisions on left side */
			else if (b & 0x0001)
				x += 9;
		}
	}
}

/* ------------------------------------------------------------------------ 
 * DrawToggles - display the console sense switches
 * ------------------------------------------------------------------------ */

static void DrawToggles (HDC hDC, int bits)
{
	int b, x;

	for (b = 0x8000, x = 122; b != 0; b >>= 1) {
		if (shown_ces & b) {			/* up */
			SelectObject(hDC, hbWhite);
			Rectangle(hDC, x, 232, x+9, 240);
			SelectObject(hDC, hbGray);
			Rectangle(hDC, x, 239, x+9, 255);
 		}
		else {							/* down */
			SelectObject(hDC, hbWhite);
			Rectangle(hDC, x, 263, x+9, 271);
			SelectObject(hDC, hbGray);
			Rectangle(hDC, x, 248, x+9, 264);
		}

		x += (b & 0x1111) ? 31 : 21;
	}
}

/* ------------------------------------------------------------------------ 
 * DrawRunmode - draw the run mode rotary switch's little tip
 * ------------------------------------------------------------------------ */

void DrawRunmode (HDC hDC, int mode)
{
	double angle = (mode*45. + 90.) * 3.1415926 / 180.;		/* convert mode position to angle in radians */
	double ca, sa;											/* sine and cosine */
	int x0, y0, x1, y1;
	HPEN hOldPen;

	ca = cos(angle);
	sa = sin(angle);

	x0 = 301 + (int) (20.*ca + 0.5);		/* inner radius */
	y0 = 389 - (int) (20.*sa + 0.5);
	x1 = 301 + (int) (25.*ca + 0.5);		/* outer radius */
	y1 = 389 - (int) (25.*sa + 0.5);

	hOldPen = SelectObject(hDC, hSwitchPen);

	MoveToEx(hDC, x0, y0, NULL);
	LineTo(hDC, x1, y1);

	SelectObject(hDC, hOldPen);
}

/* ------------------------------------------------------------------------ 
 * HandleClick - handle mouse clicks on the console window. Now we just 
 * look at the console sense switches.  Actual says this is a real click, rather
 * than a mouse-region test.  Return value TRUE means the cursor is over a hotspot.
 * ------------------------------------------------------------------------ */

static BOOL HandleClick (HWND hWnd, int xh, int yh, BOOL actual)
{
	int b, x, r, ang, i;

	for (b = 0x8000, x = 122; b != 0; b >>= 1) {
		if (BETWEEN(xh, x-3, x+8+3) && BETWEEN(yh, 230, 275)) {
			if (actual) {
				CES ^= b;						/* a hit. Invert the bit and redisplay */
				update_gui(TRUE);
			}
			return TRUE;
		}
		x += (b & 0x1111) ? 31 : 21;
	}

	if (BETWEEN(xh, 245, 355) && BETWEEN(yh, 345, 425)) {		/* hit near rotary switch */
		ang = (int) (atan2(301.-xh, 389.-yh)*180./3.1415926);	/* this does implicit 90 deg rotation by the way */
		r = (int) sqrt((xh-301)*(xh-301)+(yh-389)*(yh-389));
		if (r > 12) {
			for (i = MODE_LOAD; i <= MODE_INT_RUN; i++) {
				if (BETWEEN(ang, i*45-12, i*45+12)) {
					if (actual) {
						RUNMODE = i;
						update_gui(TRUE);
					}
					return TRUE;
				}
			}
			
		}
	}

	return FALSE;
}

/* ------------------------------------------------------------------------ 
 * DrawConsole - refresh the console display. (This routine could be sped up by intersecting
 * the various components' bounding rectangles with the repaint rectangle.  The bounding rects
 * could be put into an array and used both here and in the refresh routine).
 *
 * RedrawRegion -> force repaint w/o background redraw. used for lamps which are drawn in the same place in either state
 * RepaintRegion-> repaint with background redraw. Used for toggles which change position.
 * ------------------------------------------------------------------------ */

static void DrawConsole (HDC hDC)
{
	static char digits[] = " 0 1 2 3 4 5 6 7 8 9101112131415";
	static char cccs[]   = "3216 8 4 2 1";
	static char cnds[]   = " C V";
	static char waits[]  = " W";
	HFONT hOldFont, hOldBrush;

	hOldFont  = SelectObject(hDC, hFont);			/* use that tiny font */
	hOldBrush = SelectObject(hDC, hbWhite);

	SetBkMode(hDC, TRANSPARENT);					/* overlay letters w/o changing background */

	DrawBits(hDC,  76,  15, shown_iar,    16, 0x3FFF, digits);
	DrawBits(hDC,  76, 	48, shown_sar,    16, 0x3FFF, digits);
	DrawBits(hDC,  76,  81, shown_sbr,    16, 0xFFFF, digits);
	DrawBits(hDC,  76, 147, shown_acc,    16, 0xFFFF, digits);
	DrawBits(hDC,  76, 180, shown_ext,    16, 0xFFFF, digits);

	DrawBits(hDC, 506,  15, shown_op,      5, 0x001F, digits);
	DrawBits(hDC, 506,  81, shown_tag,     4, 0x0007, digits);
	DrawBits(hDC, 506, 114, shown_irq,     6, 0x003F, digits);
	DrawBits(hDC, 506, 147, shown_ccc,     6, 0x003F, cccs);
	DrawBits(hDC, 506, 180, shown_cnd,     2, 0x0003, cnds);

	DrawBits(hDC, 390,  81, shown_wait?1:0,1, 0x0001, waits);

	DrawToggles(hDC, shown_ces);

	DrawRunmode(hDC, shown_runmode);

	SelectObject(hDC, hOldFont);
	SelectObject(hDC, hOldBrush);
}

/* ------------------------------------------------------------------------ 
 * Handles button presses. Remember that this occurs in the context of 
 * the Pump thread, not the simulator thread.
 * ------------------------------------------------------------------------ */

extern void stuff_cmd (char *cmd);
extern void remark_cmd (char *cmd);

void flash_run (void)              
{
	EnableWindow(btn[IDC_RUN].hBtn, TRUE);		// enable the run lamp
	btn[IDC_RUN].offtime = GetTickCount();		// reset timeout

	KillTimer(hConsoleWnd, UPDATE_TIMER_ID);	// (re)schedule lamp update
	SetTimer(hConsoleWnd, UPDATE_TIMER_ID, LAMPTIME+1, NULL);
}

void HandleCommand (HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	int i;

	switch (wParam) {
		case IDC_POWER:						/* toggle system power */
			power = ! power;
			reset_all(0);
			if (running && ! power) {		/* turning off */
				reason = STOP_POWER_OFF;
				while (running)
					Sleep(10);				/* wait for execution thread to exit */
			}
			EnableWindow(btn[IDC_POWER_ON].hBtn, power);
			for (i = 0; i < NBUTTONS; i++)
				InvalidateRect(btn[i].hBtn, NULL, TRUE);
			break;

		case IDC_PROGRAM_START:				/* begin execution */
			if (! running) {
				switch (RUNMODE) {
					case MODE_INT_RUN:
					case MODE_RUN:
					case MODE_SI:
						stuff_cmd("go");
						break;

					case MODE_DISP:			/* display core and advance IAR */
						ReadW(IAR);
						IAR = IAR+1;
						flash_run();		/* illuminate run lamp for .5 sec */
						break;

					case MODE_LOAD:			/* store to core and advance IAR */
						WriteW(IAR, CES);
						IAR = IAR+1;
						flash_run();
						break;
				}
			}
			break;

		case IDC_PROGRAM_STOP:
			if (running) {					/* potential race condition here */
				GUI_BEGIN_CRITICAL_SECTION
				SETBIT(con_dsw, CON_DSW_PROGRAM_STOP);
				SETBIT(ILSW[5], ILSW_5_PROGRAM_STOP);
				int_req |= INT_REQ_5;
				GUI_END_CRITICAL_SECTION
			}
			break;

		case IDC_LOAD_IAR:
			if (! running) {
				IAR = CES & 0x3FFF;			/* set IAR from console entry switches */
			}
			break;

		case IDC_KEYBOARD:					/* toggle between console/keyboard mode */
			break;

		case IDC_IMM_STOP:
			if (running) {
				reason = STOP_WAIT;			/* terminate execution without setting wait_mode */
				while (running)
					Sleep(10);				/* wait for execution thread to exit */
			}
			break;

		case IDC_RESET:
			if (! running) {				/* check-reset is disabled while running */
				reset_all(0);
				forms_check(0);				/* clear forms-check status */
				print_check(0);
			}
			break;

		case IDC_PROGRAM_LOAD:
			if (! running) {				/* if card reader is attached to a file, do cold start read of one card */
				IAR = 0;					/* reset IAR */
//				stuff_cmd("boot cr");
				if (cr_boot(0) != SCPE_OK)	/* load boot card */
					remark_cmd("IPL failed");
			}
			break;
	}
	
	update_gui(FALSE);
}

/* ------------------------------------------------------------------------ 
 * ConsoleWndProc - window process for the console display
 * ------------------------------------------------------------------------ */

LRESULT CALLBACK ConsoleWndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	HDC hDC;
	PAINTSTRUCT ps;
	POINT p;
	RECT clip, xsect, rbmp;
	int i;

	switch (uMsg) {
		case WM_CLOSE:
			DestroyWindow(hWnd);
			break;

		case WM_DESTROY:
			hConsoleWnd = NULL;
			break;

		case WM_ERASEBKGND:
			hDC = (HDC) wParam;
			GetClipBox(hDC, &clip);
			SetRect(&rbmp, 0, 0, bmwid, bmht);
			if (IntersectRect(&xsect, &clip, &rbmp))
				BitBlt(hDC, xsect.left, xsect.top, xsect.right-xsect.left+1, xsect.bottom-xsect.top+1, hCDC, xsect.left, xsect.top, SRCCOPY);
//			rbmp.top = rbmp.bottom;
//			rbmp.bottom += 200;
//			if (IntersectRect(&xsect, &clip, &rbmp))
//				FillRect(hDC, &xsect, hbBlack);
			return TRUE;			/* let Paint do this so we know what the update region is (ps.rcPaint) */

		case WM_PAINT:
			hDC = BeginPaint(hWnd, &ps);
			DrawConsole(hDC);
			EndPaint(hWnd, &ps);
			break;

		case WM_COMMAND:			/* button click */
			HandleCommand(hWnd, wParam, lParam);
			break;

		case WM_DRAWITEM:
			PaintButton((LPDRAWITEMSTRUCT) lParam);
			break;

		case WM_SETCURSOR:
			GetCursorPos(&p);
			ScreenToClient(hWnd, &p);
			SetCursor(HandleClick(hWnd, p.x, p.y, FALSE) ? hcHand : hcArrow);
			return TRUE;

		case WM_LBUTTONDOWN:
			HandleClick(hWnd, LOWORD(lParam), HIWORD(lParam), TRUE);
			break;

		case WM_CTLCOLORBTN:
			i = GetWindowLong((HWND) lParam, GWL_ID);
			if (BETWEEN(i, 0, NBUTTONS-1))
				return (LRESULT) (power && IsWindowEnabled((HWND) lParam) ? btn[i].hbrLit : btn[i].hbrDark);

		case WM_TIMER:
			if (wParam == UPDATE_TIMER_ID) {
				update_gui(FALSE);
				KillTimer(hWnd, UPDATE_TIMER_ID);
			}
			break;

		default:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	return 0;
}

enum {PRINTER_OK = 0, FORMS_CHECK = 1, PRINT_CHECK = 2, BOTH_CHECK = 3} printerstatus = PRINTER_OK;

void forms_check (int set)
{
	COLORREF oldcolor = btn[IDC_FORMS_CHECK].clr;

	if (set)
		SETBIT(printerstatus, FORMS_CHECK);
	else
		CLRBIT(printerstatus, FORMS_CHECK);

	btn[IDC_FORMS_CHECK].clr = (printerstatus & PRINT_CHECK) ? RGB(255,0,0) : RGB(255,255,0);

	EnableWindow(btn[IDC_FORMS_CHECK].hBtn, printerstatus);

	if (btn[IDC_FORMS_CHECK].clr != oldcolor)
		InvalidateRect(btn[IDC_FORMS_CHECK].hBtn, NULL, TRUE);		// change color in any case
}

void print_check (int set)
{
	COLORREF oldcolor = btn[IDC_FORMS_CHECK].clr;

	if (set)
		SETBIT(printerstatus, PRINT_CHECK);
	else
		CLRBIT(printerstatus, PRINT_CHECK);

	btn[IDC_FORMS_CHECK].clr = (printerstatus & PRINT_CHECK) ? RGB(255,0,0) : RGB(255,255,0);

	EnableWindow(btn[IDC_FORMS_CHECK].hBtn, printerstatus);

	if (btn[IDC_FORMS_CHECK].clr != oldcolor)
		InvalidateRect(btn[IDC_FORMS_CHECK].hBtn, NULL, TRUE);		// change color in any case
}

void keyboard_selected (int select)
{
	EnableWindow(btn[IDC_KEYBOARD_SELECT].hBtn, select);
}

void disk_ready (int ready)
{
	EnableWindow(btn[IDC_FILE_READY].hBtn, ready);
}

void disk_unlocked (int unlocked)
{
	EnableWindow(btn[IDC_DISK_UNLOCK].hBtn, unlocked);
}

CRITICAL_SECTION critsect;

void begin_critical_section (void)
{
	static BOOL mustinit = TRUE;

	if (mustinit) {
		InitializeCriticalSection(&critsect);
		mustinit = FALSE;
	}

	EnterCriticalSection(&critsect);
}

void end_critical_section (void)
{
	LeaveCriticalSection(&critsect);
}

#endif // WIN32
#endif // GUI_SUPPORT
