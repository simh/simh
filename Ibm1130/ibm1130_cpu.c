/* ibm1130_cpu.c: IBM 1130 CPU simulator

   Based on the SIMH package written by Robert M Supnik

 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to simh@ibm1130.org

   25-Jun-01 BLK	Written
   10-May-02 BLK	Fixed bug in MDX instruction
   27-Mar-02 BLK	Made BOSC work even in short form
   16-Aug-02 BLK	Fixed bug in multiply instruction; didn't work with negative values
   18-Mar-03 BLK	Fixed bug in divide instruction; didn't work with negative values
   23-Jul-03 BLK	Prevented tti polling in CGI mode
   24-Nov-03 BLK	Fixed carry bit error in subtract and subtract double, found by Bob Flanders

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

#define UPDATE_BY_TIMER
#define ENABLE_BACKTRACE
#define CGI_SUPPORT

static void cgi_start(void);
static void cgi_stop(t_stat reason);

// hook pointers from scp.c
void (*sim_vm_init) (void) = &sim_init;
extern char* (*sim_vm_read) (char *ptr, int32 size, FILE *stream);
extern void (*sim_vm_post) (t_bool from_scp);
extern CTAB *sim_vm_cmd;

// space to store extra simulator-specific commands
#define MAX_EXTRA_COMMANDS 10
CTAB x_cmds[MAX_EXTRA_COMMANDS];

#ifdef _WIN32
#   define CRLF "\r\n"
#else
#   define CRLF "\n"
#endif

/* ------------------------------------------------------------------------
 * initializers for globals
 * ------------------------------------------------------------------------ */

#define SIGN_BIT(v)   ((v) & 0x8000)
#define DWSIGN_BIT(v) ((v) & 0x80000000)

uint16 M[MAXMEMSIZE];				/* core memory, up to 32Kwords (note: don't even think about trying 64K) */
uint16 ILSW[6] = {0,0,0,0,0,0};		/* interrupt level status words */
int32 IAR;							/* instruction address register */
int32 prev_IAR;						/* instruction address register at start of current instruction */
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
int32 wait_lamp = TRUE;				/* alternate indicator to light the wait lamp on the GUI */
int32 int_req = 0;					/* sum of interrupt request levels active */
int32 int_lamps = 0;				/* accumulated version of int_req - gives lamp persistence */
int32 int_mask;						/* current active interrupt mask (ipl sensitive) */
int32 mem_mask;
int32 cpu_dsw = 0;					/* CPU device status word */
int32 ibkpt_addr = -1;				/* breakpoint addr */
int32 sim_gui = TRUE;				/* enable gui */
t_bool running = FALSE;				/* TRUE if CPU is running */
t_bool power   = TRUE;				/* TRUE if CPU power is on */
t_bool cgi     = FALSE;				/* TRUE if we are running as a CGI program */
t_stat reason;						/* CPU execution loop control */

static int32 int_masks[6] = {
	0x00, 0x20, 0x30, 0x38, 0x3C, 0x3E		/* IPL 0 is highest prio (sees no other interrupts) */
};

/* ------------------------------------------------------------------------
 * Function declarations
 * ------------------------------------------------------------------------ */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_svc (UNIT *uptr);
t_stat cpu_set_size (UNIT *uptr, int32 value, char *cptr, void *desc);
void calc_ints (void);

extern t_stat ts_wr (int32 data, int32 addr, int32 access);
extern t_stat detach_cmd (int flags, char *cptr);
extern UNIT cr_unit;
extern int32 sim_switches;

#ifdef ENABLE_BACKTRACE
	static void   archive_backtrace(char *inst);
	static void   reset_backtrace (void);
	static void   show_backtrace (int nshow);
	static t_stat backtrace_cmd (int flag, char *cptr);
#else
	#define archive_backtrace(inst)
	#define reset_backtrace()
	#define show_backtrace(ntrace)
#endif

static void   init_console_window (void);
static void   destroy_console_window (void);
static t_stat view_cmd (int flag, char *cptr);
static t_stat cgi_cmd (int flag, char *cptr);
static t_stat cpu_attach (UNIT *uptr, char *cptr);
static t_bool bsctest (int32 DSPLC, t_bool reset_V);
static void   exit_irq (void);
static void   trace_instruction (void);

/* ------------------------------------------------------------------------
 * CPU data structures:
 *    cpu_dev	CPU device descriptor
 *    cpu_unit	CPU unit descriptor
 *    cpu_reg	CPU register list
 *    cpu_mod	CPU modifier list
 * ------------------------------------------------------------------------ */

UNIT cpu_unit = { UDATA (&cpu_svc, UNIT_FIX | UNIT_BINK | UNIT_ATTABLE | UNIT_SEQ, INIMEMSIZE) };

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
	{ HRDATA (DSW, cpu_dsw, 32), REG_RO },
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
	NULL, cpu_attach, NULL};			// attaching to CPU creates cpu log file

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

	int_req    = newbits;
	int_lamps |= int_req;
	int_mask   = (ipl < 0) ? 0xFFFF : int_masks[ipl];		/* be sure this is set correctly */

    GUI_END_CRITICAL_SECTION
}

/* ------------------------------------------------------------------------
 * instruction processor
 * ------------------------------------------------------------------------ */

#define INCREMENT_IAR 	IAR = (IAR + 1) & mem_mask
#define DECREMENT_IAR 	IAR = (IAR - 1) & mem_mask

void bail (char *msg)
{
	printf("%s\n", msg);
	exit(1);
}

static void weirdop (char *msg, int offset)
{
	printf("Weird opcode: %s at %04x\n", msg, IAR+offset);
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

t_stat sim_instr (void)
{
	extern int32 sim_interval;
	extern UNIT *sim_clock_queue;
	int32 i, eaddr, INDIR, IR, F, DSPLC, word2, oldval, newval, src, src2, dst, abit, xbit;
	int32 iocc_addr, iocc_op, iocc_dev, iocc_func, iocc_mod;
	char msg[50];
	int cwincount = 0, status;
	static long ninstr = 0;
	static char *intlabel[] = {"INT0","INT1","INT2","INT3","INT4","INT5"};

#ifdef CGI_SUPPORT
	if (cgi)
		cgi_start();
#endif

	if (running)							/* this is definitely not reentrant */
		return -1;

	if (! power)							/* this matters only to the GUI */
		return STOP_POWER_OFF;

	running = TRUE;

	mem_mask = MEMSIZE - 1;					/* set other useful variables */
	calc_ints();

	/* Main instruction fetch/decode loop */

	reason = 0;
	wait_lamp = 0;							/* release lock on wait lamp */

#ifdef GUI_SUPPORT
	update_gui(TRUE);
	gui_run(TRUE);
#endif

	while (reason == 0)  {
		IAR &= mem_mask;

#ifdef GUI_SUPPORT
#ifndef UPDATE_BY_TIMER
#if (UPDATE_INTERVAL > 0)
			if (--cwincount <= 0) {
				update_gui(FALSE);			/* update console lamps only every so many instructions */
				cwincount = UPDATE_INTERVAL + (rand() % MIN(UPDATE_INTERVAL, 32));
			}
#else
			update_gui(FALSE);
#endif // ifdef  UPDATE_INTERVAL
#endif // ifndef UPDATE_BY_TIMER
#endif // ifdef  GUI_SUPPORT

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
			archive_backtrace(intlabel[i]);
			WriteW(eaddr, IAR);				/* save IAR */
			IAR = (eaddr+1) & mem_mask;		/* go to next address */
			continue;						/* now continue processing */
		}									/* end if int_req */

		if (wait_state) {					/* waiting? */
			sim_interval = 0;				/* run the clock out */

			if (sim_qcount() <= (cgi ? 0 : 1)) {		/* one routine queued? we're waiting for keyboard only */
				if (keyboard_is_busy()) {						/* we are actually waiting for a keystroke */
					if ((status = sim_process_event()) != 0) 	/* get it with wait_state still set */
						reason = status;
				}
				else {						/* CPU is not expecting a keystroke (keyboard interrupt) */
					if (wait_state == WAIT_OP)
						reason = STOP_WAIT;	/* end the simulation */
					else
						reason = STOP_INVALID_INSTR;
				}
			}

			if (gdu_active())				/* but don't stop simulator if 2250 GDU is running */
				reason = 0;

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
		if (cpu_unit.flags & UNIT_ATT)
			trace_instruction();			/* log CPU details if logging is enabled */

		prev_IAR = IAR;						/* save IAR before incrementing it */

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

				if (cpu_unit.flags & UNIT_ATT)
					trace_io("* XIO %s %s mod %02x addr %04x", xio_funcs[iocc_func], xio_devs[iocc_dev], iocc_mod, iocc_addr);

//				fprintf(stderr, "* XIO %s %s mod %02x addr %04x\n", xio_funcs[iocc_func], xio_devs[iocc_dev], iocc_mod, iocc_addr);

				ACC = 0;					/* ACC is destroyed, and default XIO_SENSE_DEV result is 0 */

				switch (iocc_func) {
					case XIO_UNUSED:
						sprintf(msg, "Unknown op %x on device %02x", iocc_func, iocc_dev);
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
								sprintf(msg, "unknown device %02x", iocc_dev);
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
				archive_backtrace("BSI");	/* save info in back-trace buffer */
				IAR = (eaddr + 1) & mem_mask;
				break;

			case 0x09:						/* --- BSC - Branch and skip on Condition --- */
				if (F) {
					if (bsctest(IR, F))		/* long format; any indicator cancels branch */
						break;

					archive_backtrace((DSPLC & 0x40) ? "BOSC" : "BSC");	/* save info in back-trace buffer */
					IAR = eaddr;			/* no indicator means branch taken */
				}
				else {						/* short format: skip if any indicator hits */
					if (bsctest(IR, F)) {
						archive_backtrace((DSPLC & 0x40) ? "BOSC" : "BSC");		/* save info in back-trace buffer */
						INCREMENT_IAR;
					}
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
				else {
					archive_backtrace("LDX");			/* save info in back-trace buffer */
					IAR = eaddr;			/* what happens in short form? can onlyjump to low addresses? */
				}
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
						archive_backtrace("MDX");
						IAR    = newval & mem_mask;
					}
				}

				if ((F || TAG) && (((newval & 0xFFFF) == 0) || ((oldval & 0x8000) != (newval & 0x8000)))) {
					archive_backtrace("SKP");
					INCREMENT_IAR;			/* skip if index sign change or zero */
				}
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
				src  = ((ACC << 16) | (EXT & 0xFFFF));
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

				C = src < src2;
				if (! V)
					V = SIGN_BIT((src ^ src2) & (src ^ ACC));
				break;

			case 0x13:						/* --- SD - Subtract Double	--- */
				src  = ((ACC << 16) | (EXT & 0xFFFF));
				src2 = (ReadW(eaddr) << 16) + ReadW(eaddr|1);
				dst  = src - src2;
				ACC  = (dst >> 16) & 0xFFFF;
				EXT  = dst & 0xFFFF;

				C = (unsigned int32) src < (unsigned int32) src2;
				if (! V)
					V = DWSIGN_BIT((src ^ src2) & (src ^ dst));
				break;

			case 0x14:						/* --- M - Multiply	--- */
				if ((src = ACC & 0xFFFF)  & 0x8000)		/* sign extend the values */
					src	 |= ~0xFFFF;
				if ((src2 = ReadW(eaddr)) & 0x8000)
					src2 |= ~0xFFFF;

				dst = src * src2;
				ACC = (dst >> 16) & 0xFFFF;				/* split the results */
				EXT = dst & 0xFFFF;
				break;

			case 0x15:						/* --- D - Divide --- */
				src  = ((ACC << 16) | (EXT & 0xFFFF));
				if ((src2 = ReadW(eaddr)) & 0x8000)
					src2 |= ~0xFFFF;		/* oops: sign extend was missing, fixed 18Mar03 */

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
			SETBIT(cpu_dsw, CPU_DSW_INT_RUN);
			SETBIT(ILSW[5], ILSW_5_INT_RUN);
			int_req |= INT_REQ_5;
			GUI_END_CRITICAL_SECTION
		}
	}										/* end main loop */

#ifdef GUI_SUPPORT
	gui_run(FALSE);
#endif

	running   = FALSE;
	int_lamps = 0;			/* display only currently active interrupts while halted */

	if (reason == STOP_WAIT || reason == STOP_INVALID_INSTR) {
		wait_state = 0;						// on resume, don't wait
		wait_lamp = TRUE;					// but keep the lamp lit on the GUI
	}

#ifdef CGI_SUPPORT
	if (cgi)
		cgi_stop(reason);
#endif

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
		if ((ACC & 0xFFFF) == 0)
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

/* let a device halt the simulation */

void break_simulation (t_stat stopreason)
{
	reason = stopreason;
}

/* ------------------------------------------------------------------------ 
 * SIMH required routines
 * ------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------ 
 * Reset routine
 * ------------------------------------------------------------------------ */

t_stat cpu_reset (DEVICE *dptr)
{
	wait_state = 0;						/* cancel wait */
	wait_lamp  = TRUE;					/* but keep the wait lamp lit on the GUI */

	if (cpu_unit.flags & UNIT_ATT) {						/* record reset in CPU log */
		fseek(cpu_unit.fileref, 0, SEEK_END);
		fprintf(cpu_unit.fileref, "---RESET---" CRLF);
	}

	GUI_BEGIN_CRITICAL_SECTION

	reset_backtrace();

	ipl = -1;
	int_mask = 0xFFFF;
	int_req    = 0;						/* hmmm, it SHOULD reset the int req, right? */
	int_lamps  = 0;
	iplpending = 0;
	memset(ILSW, 0, sizeof(ILSW));

	cpu_dsw = 0;						/* clear int req and prot stop bits */
	tbit = 0;							/* cancel INT_RUN mode */

	C = V = 0;							/* clear processor flags */
	IAR = SAR = SBR = 0;				/* clear IAR and other registers */
	ACC = EXT = OP = TAG = CCC = C = V = 0;

	mem_mask = MEMSIZE - 1;				/* wraparound mask */

	GUI_END_CRITICAL_SECTION

	return cpu_svc(&cpu_unit);			/* reset breakpoint */
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
		M[addr] = (uint16) (val & 0xFFFF);
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

t_stat cpu_set_size (UNIT *uptr, int32 value, char *cptr, void *desc)
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
			ACC = cpu_dsw;
			if (modify & 0x01) {						/* reset interrupts */
				CLRBIT(cpu_dsw, CPU_DSW_PROGRAM_STOP|CPU_DSW_INT_RUN);
				CLRBIT(ILSW[5], ILSW_5_INT_RUN);		/* (these bits are set in the keyboard handler in 1130_stddev.c) */
			}
			break;

		default:
			sprintf(msg, "Invalid console switch function %x", func);
			xio_error(msg);
	}
}

/* ------------------------------------------------------------------------ 
 * Illegal IO operation.  Not yet sure what the actual CPU does in this case
 * ------------------------------------------------------------------------ */

void xio_error (char *msg)
{
	printf("*** XIO error at %04x: %s\n", prev_IAR, msg);
	if (cgi)
		break_simulation(STOP_CRASH);
}

/* ------------------------------------------------------------------------ 
 * register_cmd - add a command to the extensible command table
 * ------------------------------------------------------------------------ */

t_stat register_cmd (char *name, t_stat (*action)(int32 flag, char *ptr), int arg, char *help)
{
	int i;

	for (i = 0; i < MAX_EXTRA_COMMANDS; i++) {	// find end of command table
		if (x_cmds[i].action == action)
			return SCPE_OK;						// command is already there, just return
		if (x_cmds[i].name == NULL)
			break;
	}

	if (i >= (MAX_EXTRA_COMMANDS-1)) {			// no more room (we need room for the NULL)
		fprintf(stderr, "The command table is full - rebuild the simulator with more free slots\n");
		return SCPE_ARG;
	}

	x_cmds[i].action = action;					// add new command
	x_cmds[i].name   = name;
	x_cmds[i].arg    = arg;
	x_cmds[i].help   = help;

	i++;
	x_cmds[i].action = NULL;	// move the NULL terminator
	x_cmds[i].name   = NULL;

	return SCPE_OK;
}

/* ------------------------------------------------------------------------ 
 * echo_cmd - just echo the command line
 * ------------------------------------------------------------------------ */

static t_stat echo_cmd (int flag, char *cptr)
{
	printf("%s\n", cptr);
	return SCPE_OK;
}

/* ------------------------------------------------------------------------ 
 * sim_init - initialize simulator upon startup of scp, before reset
 * ------------------------------------------------------------------------ */

void sim_init (void)
{
	sim_gui = ! (sim_switches & SWMASK('G'));	/* -g means no GUI */

	sim_vm_cmd = x_cmds;						/* provide list of additional commands */

#ifdef GUI_SUPPORT
	// set hook routines for GUI command processing
	if (sim_gui) {
		sim_vm_read = &read_cmdline;
		sim_vm_post = &update_gui;
	}
#endif

#ifdef ENABLE_BACKTRACE
	// add the BACKTRACE command
	register_cmd("BACKTRACE", &backtrace_cmd, 0, "ba{cktrace} {n}          list last n branches/skips/interrupts\n");
#endif

	register_cmd("VIEW",      &view_cmd,      0, "v{iew} filename          view a text file with notepad\n");

#ifdef CGI_SUPPORT
	register_cmd("CGI",       &cgi_cmd,       0, "cgi                      run simulator in CGI mode\n");
#endif

	register_cmd("ECHO",      &echo_cmd,      0, "echo args...             echo arguments passed to command\n");
}

/* ------------------------------------------------------------------------ 
 * archive_backtrace - record a jump, skip, branch or whatever
 * ------------------------------------------------------------------------ */

#ifdef ENABLE_BACKTRACE

#define MAXARCHIVE 16

static struct tag_arch {
	int iar;
	char *inst;
} arch[MAXARCHIVE];
int narchived = 0, archind = 0;

static void archive_backtrace (char *inst)
{
	static int prevind;

	if (narchived < MAXARCHIVE)
		narchived++;

	if (narchived > 0 && arch[prevind].iar == prev_IAR)
		return;

	arch[archind].iar  = prev_IAR;
	arch[archind].inst = inst;

	prevind = archind;
	archind = (archind+1) % MAXARCHIVE;
}

static void reset_backtrace (void)
{
	narchived = 0;
	archind = 0;
}

void void_backtrace (int afrom, int ato)
{
	int i;

	afrom &= mem_mask;
	ato   &= mem_mask;

	for (i = 0; i < narchived; i++)
		if (arch[i].iar >= afrom && arch[i].iar <= ato)
			arch[i].inst = "OVERWRITTEN";
}

static void show_backtrace (int nshow)
{
	int n = narchived, i = archind;

	if (n > nshow) n = nshow;

	while (--n >= 0) {
		i = (i > 0) ? (i-1) : (MAXARCHIVE-1);
		printf("from %04x (%s) ", arch[i].iar, arch[i].inst);
	}

	if (narchived)
		putchar('\n');
}

static t_stat backtrace_cmd (int flag, char *cptr)
{
	int n;

	if ((n = atoi(cptr)) <= 0)
		n = 6;

	show_backtrace(n);
	return SCPE_OK;
}
#else

// stub this for the disk routine

void void_backtrace (int afrom, int ato)
{
}

#endif

// CPU log routines -- attaching a file to the CPU creates a trace of instructions and register values
//
// Syntax is WEIRD:
//
// attach cpu logfile					log instructions and registers to file "logfile"
// attach -f cpu cpu.log				log instructions, registers and floating point acc
// attach -m cpu mapfile logfile		read addresses from "mapfile", log instructions to "logfile"
// attach -f -m cpu mapfile logfile		same and log floating point stuff too
//
// mapfile if specified is a list of symbols and addresses of the form:
//       symbol hexval
//
// e.g.
// FSIN   082E
// FARC   09D4
// FMPY   09A4
// NORM   0976
// XMDS   095A
// START  021A
//
// These values are easily obtained from a load map created by
// XEQ       L
//
// The log output is of the form
//
//  IAR             ACC  EXT  (flt)    XR1  XR2  XR3 CVI      FAC      OPERATION
// --------------- ---- ---- -------- ---- ---- ---- --- ------------- -----------------------
// 002a       002a 1234 5381  0.14222 00b3 0236 3f7e CV   1.04720e+000 4c80 BSC  I  ,0028   
// 081d PAUSE+000d 1234 5381  0.14222 00b3 0236 3f7e CV   1.04720e+000 7400 MDM  L  00f0,0 (0)   
// 0820 PAUSE+0010 1234 5381  0.14222 00b3 0236 3f7e CV   1.04720e+000 7201 MDX   2 0001   
// 0821 PAUSE+0011 1234 5381  0.14222 00b3 0237 3f7e CV   1.04720e+000 6a03 STX   2 0003   
// 0822 PAUSE+0012 1234 5381  0.14222 00b3 0237 3f7e CV   1.04720e+000 6600 LDX  L2 0231   
// 0824 PAUSE+0014 1234 5381  0.14222 00b3 0231 3f7e CV   1.04720e+000 4c00 BSC  L  ,0237   
// 0237 START+001d 1234 5381  0.14222 00b3 0231 3f7e CV   1.04720e+000 4480 BSI  I  ,3fff   
// 082f FSIN +0001 1234 5381  0.14222 00b3 0231 3f7e CV   1.04720e+000 4356 BSI   3 0056   
// 3fd5 ILS01+35dd 1234 5381  0.14222 00b3 0231 3f7e CV   1.04720e+000 4c00 BSC  L  ,08de   
//
// IAR - instruction address register value, optionally including symbol and offset
// ACC - accumulator
// EXT - extension
// flt - ACC+EXT interpreted as the mantissa of a floating pt number (value 0.5 -> 1)
// XR* - index registers
// CVI - carry, overflow and interrupt indicators
// FAC - floating point accumulator (exponent at 125+XR3, mantissa at 126+XR3 and 127+XR3)
// OP  - opcode value and crude disassembly
//
// flt and FAC are displayed only when the -f flag is specified in the attach command
// The label and offset and displayed only when the -m flag is specified in the attach command
//
// The register values shown are the values BEFORE the instruction is executed.
//

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw);

typedef struct tag_symentry {
	struct tag_symentry *next;
	int  addr;
	char sym[6];
} SYMENTRY, *PSYMENTRY;

static PSYMENTRY syms = NULL;
static t_bool new_log, log_fac;

static t_stat cpu_attach (UNIT *uptr, char *cptr)
{
	char mapfile[200], buf[200], sym[100];
	int addr;
	PSYMENTRY n, prv, s;
	FILE *fd;

	remove(cptr);							// delete old log file, if present
	new_log = TRUE;
	log_fac = sim_switches & SWMASK ('F');	// display the FAC and the ACC/EXT as fixed point.

	for (s = syms; s != NULL; s = n) {		// free any old map entries
		n = s->next;
		free(s);
	}
	syms = NULL;
		
	if (sim_switches & SWMASK('M')) {		// use a map file to display relative addresses
		cptr = get_glyph(cptr, mapfile, 0);
		if (! *mapfile) {
			printf("/m must be followed by a filename\n");
			return SCPE_ARG;
		}
		if ((fd = fopen(mapfile, "r")) == NULL) {
			perror(mapfile);
			return SCPE_OPENERR;
		}

		while (fgets(buf, sizeof(buf), fd) != NULL) {		// read symbols & addresses, link in descending address order
			if (sscanf(buf, "%s %x", sym, &addr) != 2)
				continue;
			if (*buf == ';')
				continue;

			for (prv = NULL, s = syms; s != NULL; prv = s, s = s->next) {
				if (s->addr < addr)
					break;
			}

			if ((n = malloc(sizeof(SYMENTRY))) == NULL) {
				printf("out of memory reading map!\n");
				break;
			}

			sym[5] = '\0';
			strcpy(n->sym, sym);
			upcase(n->sym);
			n->addr = addr;

			if (prv == NULL) {
				n->next = syms;
				syms = n;
			}
			else {
				n->next = prv->next;
				prv ->next = n;
			}
		}
		fclose(fd);
	}

	return attach_unit(uptr, quotefix(cptr));			/* fix quotes in filenames & attach */
}

static void trace_instruction (void)
{
	t_value v[2];
	float fac;
	short exp;
	int addr;
	PSYMENTRY s;
	long mant, sign;
	char facstr[20], fltstr[20];

	if ((cpu_unit.flags & UNIT_ATT) == 0)
		return;

	if (new_log) {
		fseek(cpu_unit.fileref, 0, SEEK_END);
		new_log = FALSE;

		fprintf(cpu_unit.fileref, " IAR%s  ACC  EXT %s XR1  XR2  XR3 CVI %sOPERATION" CRLF,
			syms ? "           " : "", log_fac ? " (flt)   " : "", log_fac ? "     FAC      " : "");
		fprintf(cpu_unit.fileref, "----%s ---- ---- %s---- ---- ---- --- %s-----------------------" CRLF,
			syms ? "-----------" : "", log_fac ? "-------- " : "", log_fac ? "------------- " : "");
	}

	if (! log_fac)	
		facstr[0] = fltstr[0] = '\0';
	else {
		mant = ((ACC & 0xFFFF) << 16) | (EXT & 0xFFFF);
		if (mant == 0x80000000) {
			sign = TRUE;
			fac = 1.f;
		}
		else {
			if ((sign = mant & 0x80000000) != 0)
				mant = -mant;
			fac = (float) mant * ((float) 1./ (float) (unsigned long) 0x80000000);
		}
		sprintf(fltstr, "%c%.5f ", sign ? '-' : ' ', fac);

		if (BETWEEN(M[3], 0x300, MEMSIZE-128)) {
			exp  = (short) ((M[M[3]+125] & 0xFF) - 128);
			mant = (M[M[3]+126] << 8) | ((M[M[3]+127] >> 8) & 0xFF);
			if ((sign = (mant & 0x00800000)) != 0)
				mant = (-mant) & 0x00FFFFFF;

			fac = (float) mant * ((float) 1. / (float) 0x00800000);

			if (exp > 30) {
				fac *= (float) (1 << 30);
				exp -= 30;
				while (exp > 0)
					fac *= 2;
			}
			else if (exp > 0)
				fac *= (float) (1 << exp);
			else if (exp < -30) {
				fac /= (float) (1 << 30);
				exp += 30;
				while (exp < 0)
					fac /= 2;
			}
			else if (exp < 0)
				fac /= (float) (1 << -exp);

			sprintf(facstr, "%c%.5e ", sign ? '-' : ' ', fac);
		}
		else
			strcpy(facstr, "             ");
	}

	addr = IAR & 0xFFFF;
	fprintf(cpu_unit.fileref, "%04x ", addr);

	if (syms) {
		for (s = syms; s != NULL; s = s->next)
			if (s->addr <= addr)
				break;
		
		if (s == NULL)
			fprintf(cpu_unit.fileref, "      %04x ", addr);
		else
			fprintf(cpu_unit.fileref, "%-5s+%04x ", s->sym, addr - s->addr);
	}

	fprintf(cpu_unit.fileref, "%04x %04x %s%04x %04x %04x %c%c%c %s",
		ACC & 0xFFFF, EXT & 0xFFFF, fltstr, M[1] & 0xFFFF, M[2] & 0xFFFF, M[3] & 0xFFFF,
		C ? 'C' : ' ', V ? 'V' : ' ', (ipl < 0) ? ' ' : (ipl+'0'), facstr);

	v[0] = M[ IAR    & mem_mask];
	v[1] = M[(IAR+1) & mem_mask];
	fprint_sym(cpu_unit.fileref, IAR & mem_mask, v, NULL, SWMASK('M'));		/* disassemble instruction */

	fputs(CRLF, cpu_unit.fileref);
}

void trace_io (char *fmt, ...)
{
	va_list args;

	if ((cpu_unit.flags & UNIT_ATT) == 0)
		return;

	va_start(args, fmt);							// get pointer to argument list
	vfprintf(cpu_unit.fileref, fmt, args);			// write errors to terminal (stderr)
	va_end(args);

	fputs(CRLF, cpu_unit.fileref);
}

/* debugging */

void debug_print (char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	if (cpu_unit.flags & UNIT_ATT)
		vfprintf(cpu_unit.fileref, fmt, args);
	va_end(args);

	if (strchr(fmt, '\n') == NULL) {		// be sure to emit a newline
		putchar('\n');
		if (cpu_unit.flags & UNIT_ATT)
			putc('\n', cpu_unit.fileref);
	}
}

#ifdef _WIN32
#include <windows.h>
#endif

// view_cmd - let user view and/or edit a file (e.g. a printer output file, script, or source deck)

static t_stat view_cmd (int flag, char *cptr)
{
#ifdef _WIN32
	char cmdline[256];

	sprintf(cmdline, "notepad %s", cptr);
	WinExec(cmdline, SW_SHOWNORMAL);
#endif
	return SCPE_OK;
}

#ifdef CGI_SUPPORT

int cgi_maxsec = 0;								// default run time limit

// cgi_cmd - enable cgi mode. Specify time limit on command line if desired

static t_stat cgi_cmd (int flag, char *cptr)
{
	cgi = TRUE;									// set CGI flag

	while (*cptr && *cptr <= ' ')
		cptr++;

	if (*cptr)
		cgi_maxsec = atoi(cptr);				// set time limit, if specified

	return SCPE_OK;
}

// cgi_timeout - called when timer runs out

static void cgi_timeout (int dummy)
{
	break_simulation(STOP_TIMED_OUT);			// stop the simulator
}

// cgi_clockfail - report failure to set alarm

static void cgi_clockfail (void)
{
	printf("<B>Set CGI time limit failed!</B>");
}

// cgi_start_timer - OS dependent routine to set things up so that
// cgi_timeout() will be called after cgi_maxsec seconds.

#if defined(_WIN32)
	static DWORD WINAPI cgi_timer_thread (LPVOID arg)
	{
		Sleep(cgi_maxsec*1000);					// timer thread -- wait, then call timeout routine
		cgi_timeout(0);
		return 0;
	}

	static void cgi_start_timer (void)
	{
		DWORD dwThreadID;

		if (CreateThread(NULL, 0, cgi_timer_thread, NULL, 0, &dwThreadID) == NULL)
			cgi_clockfail();
	}
#else
	#include <signal.h>

	#if defined(SIGVTALRM)	&& defined(ITMER_VIRTUAL)

		// setitimer counts actual runtime CPU seconds, so is insensitive to
        // system load  and is a better timer to use. Be sure to check, though,
        // that it actually works on your OS. Note that time spent performing
        // I/O does not count -- this counts user mode CPU time only, so
        // the elapsed time it allows could be much larger, especially if
        // the job is spewing output.

		#include <sys/time.h>

		static void cgi_start_timer (void)
		{
			struct itimerval rtime, otime;

			rtime.it_value.tv_sec     = cgi_maxsec;
			rtime.it_value.tv_usec    = 0;
			rtime.it_interval.tv_sec  = cgi_maxsec;
			rtime.it_interval.tv_usec = 0;

			if (signal(SIGVTALRM, cgi_timeout) == SIG_ERR)		// set alarm handler
				cgi_clockfail();
			else if (setitimer(ITIMER_VIRTUAL, &rtime, &otime))	// start timer
				cgi_clockfail();
		}

	#elif defined(SIGALRM)
		#include <unistd.h>

		// if it's all we have, standard POSIX alarm will do the trick too

		static void cgi_start_timer (void)
		{
			if (signal(SIGALRM, cgi_timeout) == SIG_ERR)		// set alarm handler
				cgi_clockfail();
			else if (alarm(cgi_maxsec))							// start timer
				cgi_clockfail();
		}

	#else
		// don't seem to have a clock available. Better say so.

		static void cgi_start_timer (void)
		{
			printf("<B>CGI time limit is not supported by this build</B>\n");
		}

	#endif
#endif

static void cgi_start(void)
{
	if (cgi_maxsec > 0)							// if time limit was specified, set timer now
		cgi_start_timer();

//	printf("Content-type: text/html\n\n<HTML>\n<HEAD>\n    <TITLE>IBM 1130 Simulation Results</TITLE>\n</HEAD>\n<BODY background=\"greenbar.gif\">\n<PRE>");
}

static void cgi_stop(t_stat reason)
{
	typedef enum {O_END, O_FORTRAN, O_MONITOR} ORIGIN;
	char *errmsg = "";
	static struct tag_pretstop {
		int acc;
		ORIGIN orig;
		char *msg;
	} pretstop[] = {
		0x8000, O_FORTRAN, "I/O attempted on invalid unit # or uninstalled device (just a guess)",
		0xF000, O_FORTRAN, "No *IOCS was specified but I/O was attempted",
		0xF001, O_FORTRAN, "Local unit defined incorrectly, or no *IOCS for specified device",
		0xF002, O_FORTRAN, "Requested record exceeds buffer size",
		0xF003, O_FORTRAN, "Illegal character encountered in input record",
		0xF004, O_FORTRAN, "Exponent too large or too small in input",
		0xF005, O_FORTRAN, "More than one exponent encountered in input",
		0xF006, O_FORTRAN, "More than one sign encountered in input",
		0xF007, O_FORTRAN, "More than one decimal point encountered in input",
		0xF008, O_FORTRAN, "Read of output-only device, or write to input-only device",
		0xF009, O_FORTRAN, "Real variable transmitted with I format or integer transmitted with E or F",
		0xF020, O_FORTRAN, "Illegal unit reference",
		0xF021, O_FORTRAN, "Read list exceeds length of write list",
		0xF022, O_FORTRAN, "Record does not exist in read list",
		0xF023, O_FORTRAN, "Maximum length of $$$$$ area on disk has been exceeded",
		0xF024, O_FORTRAN, "*IOCS (UDISK) was not specified",
		0xF100, O_FORTRAN, "File not defined by DEFINE FILE statement",
		0xF101, O_FORTRAN, "File record number too large, zero or negative",
		0xF103, O_FORTRAN, "*IOCS(DISK) was not specified",
		0xF105, O_FORTRAN, "Length of a list element exceeds record length in DEFINE FILE",
		0xF107, O_FORTRAN, "Attempt to read or write an invalid sector address (may occur if a core image program is run with too little room in working storage)",
		0xF10A, O_FORTRAN, "Define file table and/or core image header corrupted, probably by an out-of-bounds array subscript",
		0x1000, O_MONITOR, "1442 card read/punch or 1442 punch: not ready or hopper empty. [emulator: attach a file to CR or CP and go]",
		0x1001, O_MONITOR, "Illegal device, function or word count",
		0x100F, O_MONITOR, "Occurs in a DUP operation after DUP error D112",
		0x2000, O_MONITOR, "Keyboard/Console Printer not ready",
		0x2001, O_MONITOR, "Illegal device, function or word count",
		0x3000, O_MONITOR, "1134/1055 Paper Tape not ready",
		0x3001, O_MONITOR, "Illegal device, function or word count, or invalid check digit",
		0x4000, O_MONITOR, "2501 Card Reader not ready",
		0x4001, O_MONITOR, "Illegal device, function or word count",
		0x5000, O_MONITOR, "Disk not ready",
		0x5001, O_MONITOR, "Illegal device, function or word count, or attempt to write in protected area",
		0x5002, O_MONITOR, "Write select or power unsafe",
		0x5003, O_MONITOR, "Read/write/seek failure after 16 attempts or disk overflow. Extension may display logical drive number in bits 0..3 and working storage address in bits 4..15. Program Start retries 16 more times.",
		0x5004, O_MONITOR, "Same as above from routine DISK1 and DISKN, or, an uninitialized cartridge is online during a cold start.",
		0x6000, O_MONITOR, "1132 Printer not ready or out of paper",
		0x6001, O_MONITOR, "Illegal device, function or word count",
		0x7000, O_MONITOR, "1627 Plotter not ready",
		0x7001, O_MONITOR, "Illegal device, function or word count",
		0x8001, O_MONITOR, "SCA error: Illegal function or word count",
		0x8002, O_MONITOR, "SCA error: if STR mode, receive or transmit operation not completed;\n"
						   "if BSC mode, invalid start characters in the I/O area for a transmit operation",
		0x8003, O_MONITOR, "SCA error: if STR mode, failed to synchronize before attempt to read or write, or, attempted to receive before receiving INQ sequence;\n"
						   "if BSC mode, invalid number of identification characters for an identification specification operation",
		0x9000, O_MONITOR, "1403 printer no ready or out of paper",
		0x9001, O_MONITOR, "Illegal device, function or word count",
		0x9002, O_MONITOR, "Parity check, scan check or ring check",
		0xA000, O_MONITOR, "1231 Optical Mark Reader not ready",
		0xA001, O_MONITOR, "Illegal device, function or word count",
		0xA002, O_MONITOR, "Feed check, last document was processed. Clear jam, do not refeed",
		0xA003, O_MONITOR, "Feed check, last document not  processed. Clear jam and refeed",
		0,      O_END, 	   NULL
	};
	int i;

	detach_cmd(0, "prt");		/* flush last print line */

	if (reason == STOP_TIMED_OUT)
		printf("\n<HR><B>Sorry, emulation run time exceeded %d second%s</B>\n", cgi_maxsec, (cgi_maxsec == 1) ? "" : "s");
	else if (IAR != 0x2a || ACC != 0x1000) {
		ACC &= 0xFFFF;
		for (i = 0; pretstop[i].orig != O_END; i++) {
			if (pretstop[i].acc == ACC) {
				errmsg = pretstop[i].msg;
				break;
			}
		}
		printf("\n<HR><B>Abnormal exit: %s</B>\nIAR = %04x, ACC = %04x\n", errmsg, IAR, ACC);
	}

//	printf("</PRE>\n</BODY>\n</HTML>\n");
	exit(0);					/* save w/o writing disk image */
}

#endif		// ifdef CGI_SUPPORT
