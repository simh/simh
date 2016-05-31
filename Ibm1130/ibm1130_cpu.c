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
   20-Oct-04 BLK	Changed "(unsigned int32)" to "(uint32)" to accomodate improved definitions of simh types
   					Also commented out my echo command as it's now a standard simh command
   27-Nov-05 BLK    Added Arithmetic Factor Register support per Carl Claunch  (GUI only)
   06-Dec-06 BLK	Moved CGI stuff out of ibm1130_cpu.c
   01-May-07 BLK	Changed name of function xio_1142_card to xio_1442_card. Corrected list of
   					devices in xio_devs[] (used in debugging only).
   24-Mar-11 BLK	Got the real IBM 1130 diagnostics (yay!). Fixed two errors detected by the CPU diagnostics:
					-- was not resetting overflow bit after testing with BSC short form
					   (why did I think only the long form reset OV after testing?)
					-- failed to detect numeric overflow in Divide instructions
					Also fixed bug where simulator performed 2nd word fetch on Long mode instructions
					on ops that don't have long mode, blowing out the SAR/SBR display that's important in the 
					IBM diagnostics. The simulator was decrementing the IAR after the incorrect fetch, so the 
					instructions worked correctly, but, the GUI display was wrong.

>> To do: verify actual operands stored in ARF, need to get this from state diagrams in the schematic set
   Also: determine how many bits are actually stored in the IAR in a real 1130, by forcing wraparound
   and storing the IAR.

   IBM 1800 support is just beginning. Mode set is done (SET CPU 1800 or SET CPU 1130).
   Index registers are handled (1800 has real registers, 1130 uses core locations 1, 2 and 3 -- 
      but does the 1800 make its hardware index registers appear in the address space?)
   Need to add: memory protect feature, more interrupt levels, GUI mods, IO device mods, timers, watchdog.
   Memory protect was interesting -- they borrowed one of the two parity bits. XIO(0) on 1800 is used for
   interval timers, console data switches, console sense/program select/CE switches, interrupt mask register,
   programmed interrupt, console interrupt and operations monitor (watchdog)
   very interesting stuff.

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
   XR1, 2, 3		for IBM 1800 only, index registers 1, 2, and 3

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
   (Not all operations have long versions. The bit is ignored for shifts, LDX, WAIT and invalid opcodes)

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
	ibm1130_sys.c		add to sim_devices array
*/

/* ------------------------------------------------------------------------
 * Definitions
 * ------------------------------------------------------------------------ */

#include <stdarg.h>

#include "ibm1130_defs.h"

#define save_ibkpt	(cpu_unit.u3)			/* will be SAVEd */

#define UPDATE_BY_TIMER
#define ENABLE_BACKTRACE
/* #define USE_MY_ECHO_CMD	*/				/* simh now has echo command built in */
#define ENABLE_1800_SUPPORT					/* define to enable support for 1800 CPU simulation mode */

static void cgi_start(void);
static void cgi_stop(t_stat reason);
static int simh_status_to_stopcode (int status);

/* hook pointers from scp.c */
void (*sim_vm_init) (void) = &sim_init;

/* space to store extra simulator-specific commands */
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
uint16 XR[3] = {0,0,0};				/* IBM 1800 index registers */
int32 IAR;							/* instruction address register */
int32 prev_IAR;						/* instruction address register at start of current instruction */
int32 SAR, SBR;						/* storage address/buffer registers */
int32 OP, TAG, CCC;					/* instruction decoded pieces */
int32 CES;							/* console entry switches */
int32 ACC, EXT;						/* accumulator and extension */
int32 ARF;							/* arithmetic factor, a non-addressable internal CPU register */
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
int32 mem_mask;						/* mask for memory address bits based on current memory size */
int32 cpu_dsw = 0;					/* CPU device status word */
int32 ibkpt_addr = -1;				/* breakpoint addr */
t_bool sim_gui = TRUE;				/* enable gui */
t_bool running = FALSE;				/* TRUE if CPU is running */
t_bool power   = TRUE;				/* TRUE if CPU power is on */
t_bool cgi     = FALSE;				/* TRUE if we are running as a CGI program */
t_bool cgiwritable = FALSE;			/* TRUE if we can write the disk images back to the image file in CGI mode */
t_bool is_1800 = FALSE;				/* TRUE if we are simulating an IBM 1800 processor */
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
t_stat cpu_set_size (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
t_stat cpu_set_type (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
void calc_ints (void);

extern t_stat ts_wr (int32 data, int32 addr, int32 access);
extern UNIT cr_unit, prt_unit[];

#ifdef ENABLE_BACKTRACE
	static void   archive_backtrace(const char *inst);
	static void   reset_backtrace (void);
	static void   show_backtrace (int nshow);
	static t_stat backtrace_cmd (int32 flag, CONST char *cptr);
#else
	#define archive_backtrace(inst)
	#define reset_backtrace()
	#define show_backtrace(ntrace)
#endif

#ifdef GUI_SUPPORT
#  define ARFSET(v) ARF = (v) & 0xFFFF		/* set Arithmetic Factor Register (used for display purposes only) */
#else
#  define ARFSET(v)							/* without GUI, no need for setting ARF */
#endif

static void   init_console_window (void);
static void   destroy_console_window (void);
static t_stat view_cmd (int32 flag, CONST char *cptr);
static t_stat cpu_attach (UNIT *uptr, CONST char *cptr);
static t_bool bsctest (int32 DSPLC, t_bool reset_V);
static void   exit_irq (void);
static void   trace_instruction (void);

/* ------------------------------------------------------------------------
 * CPU data structures:
 *    cpu_dev	CPU device descriptor
 *    cpu_unit	CPU unit descriptor
 *    cpu_reg	CPU register list
 *    cpu_mod	CPU modifier list
 *
 * The CPU is attachable; attaching a file to it write a log of instructions
 * and registers
 * ------------------------------------------------------------------------ */

#define UNIT_MSIZE	(1 << (UNIT_V_UF + 7))		/* flag for memory size setting */
#define UNIT_1800   (1 << (UNIT_V_UF + 8))		/* flag for 1800 mode */
#define UNIT_TRACE  (3 << (UNIT_V_UF + 9))		/* debugging tracing mode bits */

#define UNIT_TRACE_NONE	 0
#define UNIT_TRACE_IO    (1 << (UNIT_V_UF+9))
#define UNIT_TRACE_INSTR (2 << (UNIT_V_UF+9))
#define UNIT_TRACE_BOTH  (3 << (UNIT_V_UF+9))

UNIT cpu_unit = { UDATA (&cpu_svc, UNIT_FIX | UNIT_BINK | UNIT_ATTABLE | UNIT_SEQ | UNIT_TRACE_BOTH, INIMEMSIZE) };

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

#ifdef ENABLE_1800_SUPPORT
	{ HRDATA (IS_1800, is_1800, 32), REG_RO|REG_HIDDEN},		/* is_1800 flag is part of state, but hidden */
	{ HRDATA (XR1,     XR[0],   16), REG_RO|REG_HIDDEN},		/* index registers are unhidden if CPU set to 1800 mode */
	{ HRDATA (XR2,     XR[1],   16), REG_RO|REG_HIDDEN},
	{ HRDATA (XR3,     XR[2],   16), REG_RO|REG_HIDDEN},
#endif

	{ HRDATA (ARF, ARF, 32) },
	{ NULL}
};

MTAB cpu_mod[] = {
	{ UNIT_MSIZE,      4096, NULL,   "4KW",  &cpu_set_size},
	{ UNIT_MSIZE,      8192, NULL,   "8KW",  &cpu_set_size},
	{ UNIT_MSIZE,     16384, NULL,   "16KW", &cpu_set_size},
	{ UNIT_MSIZE,     32768, NULL,   "32KW", &cpu_set_size},
#ifdef ENABLE_1800_SUPPORT
	{ UNIT_1800,          0, "1130", "1130", &cpu_set_type},
	{ UNIT_1800,  UNIT_1800, "1800", "1800", &cpu_set_type},
#endif	
	{ UNIT_TRACE, UNIT_TRACE_NONE,  "notrace",    "NOTRACE",    NULL},
	{ UNIT_TRACE, UNIT_TRACE_IO,    "traceIO",    "TRACEIO",    NULL},
	{ UNIT_TRACE, UNIT_TRACE_INSTR, "traceInstr", "TRACEINSTR", NULL},
	{ UNIT_TRACE, UNIT_TRACE_BOTH,  "traceBoth",  "TRACEBOTH",  NULL},
	{ 0 }  };

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 16, 16, 1, 16, 16,
	&cpu_ex, &cpu_dep, &cpu_reset,
	NULL, cpu_attach, NULL};			/* attaching to CPU creates cpu log file */

/* ------------------------------------------------------------------------ 
 * Memory read/write -- save SAR and SBR on the way in and out
 *
 * (It can be helpful to set breakpoints on a = 1, 2, or 3 in these routines
 * to detect attempts to read/set index registers using normal memory addessing.
 * APL\1130 does this in some places, I think these are why it had to be modified
 * to run on the 1800. Of course not all read/write to 1, 2 or implies an attempt
 * to read/set and index register -- they could using the address in the normal way).
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
 * read and write index registers. On the 1130, they're in core addresses 1, 2, 3.
 * on the 1800, they're separate registers
 * ------------------------------------------------------------------------ */

static uint16 ReadIndex (int32 tag)
{
#ifdef ENABLE_1800_SUPPORT
	if (is_1800)
		return XR[tag-1];						/* 1800: fetch from register */
#endif
		
	SAR = tag;									/* 1130: ordinary read from memory (like ReadW) */
	SBR = (int32) M[(tag) & mem_mask];
	return SBR;
}

static void WriteIndex (int32 tag, int32 d)
{
#ifdef ENABLE_1800_SUPPORT
	if (is_1800) {
		XR[tag-1] = d;							/* 1800: store in register */
		return;
	}
#endif

	SAR = tag;									/* 1130: ordinary write to memory (same as WriteW) */
	SBR = d;
	M[tag & mem_mask] = (int16) d;
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

    GUI_BEGIN_CRITICAL_SECTION	 			/* using critical section here so we don't mislead the GUI thread */

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

void bail (const char *msg)
{
	printf("%s\n", msg);
	exit(1);
}

static void weirdop (const char *msg)
{
	printf("Weird opcode: %s at %04x\n", msg, IAR-1);
}

static const char *xio_devs[]  = {
	"dev-00?",	"console", 	"1442card",		"1134ptape",
	"dsk0", 	"1627plot", "1132print",	"switches",
	"1231omr", 	"2501card",	"sca",	 		"dev-0b?",
	"sys7", 	"dev-0d?", 	"dev-0e?", 		"dev-0f?",
	"dev-10?", 	"dsk1",	 	"dsk2",			"dsk3",
	"dsk4",		"1403prt",	"dsk5", 		"2311drv2",
	"dev-18?", 	"2250disp",	"2741term", 	"dev-1b",
	"dev-1c?", 	"dev-1d?",	"dev-1e?", 		"dev-1f?"
};

static const char *xio_funcs[] = {
	"func0?",  "write", "read",  "sense_irq",
	"control", "initw", "initr", "sense"
};

t_stat sim_instr (void)
{
	int32 i, eaddr, INDIR, IR, F, DSPLC, word2, oldval, newval, src, src2, dst, abit, xbit;
	int32 iocc_addr, iocc_op, iocc_dev, iocc_func, iocc_mod, result;
	char msg[50];
	int cwincount = 0, status;
	static long ninstr = 0;
	static const char *intlabel[] = {"INT0","INT1","INT2","INT3","INT4","INT5"};

	/* the F bit indicates a two-word instruction for most instructions except the ones marked FALSE below */
	static t_bool F_bit_used[] = {									/* FALSE for those few instructions that don't have a long instr version */
	  /*undef  XIO   SLx    SRx    LDS    STS   WAIT   undef */
		FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE,
	  /*BSI    BSC   undef  undef  LDX    STX   MDX    undef */
		TRUE,  TRUE, FALSE, FALSE, TRUE,  TRUE, TRUE,  FALSE,
	  /*A      AD    S      SD     M      D     CPU dependent */
		TRUE,  TRUE, TRUE,  TRUE,  TRUE,  TRUE, FALSE, FALSE,
	  /*LD     LDD   STO    STD    AND    OR    EOR    undef */
		TRUE,  TRUE, TRUE,  TRUE,  TRUE,  TRUE, TRUE,  FALSE
	};

#ifdef ENABLE_1800_SUPPORT
	F_bit_used[0x16] = is_1800;				/* these two are defined and do have long versions on the 1800 */
	F_bit_used[0x17] = is_1800;				/* but are undefined on the 1130, so set these accordingly */
#endif

	if (cgi)								/* give CGI hook function a chance to do something */
		cgi_start();

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
#endif /* ifdef  UPDATE_INTERVAL */
#endif /* ifndef UPDATE_BY_TIMER */
#endif /* ifdef  GUI_SUPPORT     */

		if (sim_interval <= 0) {			/* any events timed out? */
			if (sim_clock_queue != QUEUE_LIST_END) {
				if ((status = sim_process_event()) != 0)
					reason = simh_status_to_stopcode(status);

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
					if ((status = sim_process_event()) != SCPE_OK) 	/* get it with wait_state still set */
						reason = simh_status_to_stopcode(status);
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
		if ((cpu_unit.flags & (UNIT_ATT|UNIT_TRACE_INSTR)) == (UNIT_ATT|UNIT_TRACE_INSTR))
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

		/* here I compute the usual effective address on the assumption that the instruction will need it. Some don't. */

		if (F && F_bit_used[OP]) {			/* long instruction, except for a few that don't have a long mode, like WAIT */
			INDIR = IR & 0x0080;			/* indirect bit */
			DSPLC = IR & 0x007F;			/* displacement or modifier */
			if (DSPLC & 0x0040)
				DSPLC |= ~ 0x7F;			/* sign extend */

			word2 = ReadW(IAR);				/* get reference address */
			INCREMENT_IAR;					/* bump the instruction address register */

			eaddr = word2;					/* assume standard addressing & compute effective address */
			if (TAG)						/* if indexed */
				eaddr += ReadIndex(TAG);	/* add index register value */
			if (INDIR)						/* if indirect addressing */
				eaddr = ReadW(eaddr);		/* pick up referenced address */
			
			/* to do: the previous steps may lead to incorrect GUI SAR/SBR display if the instruction doesn't actually fetch anything. Check this. */
		}
		else {								/* short instruction, use displacement */
			INDIR = 0;						/* never indirect */
			DSPLC = IR & 0x00FF;			/* get displacement */
			if (DSPLC & 0x0080)
				DSPLC |= ~ 0xFF;

			if (TAG)						/* if indexed */
				eaddr = ReadIndex(TAG) + DSPLC;	/* add index register value */
			else
				eaddr = IAR + DSPLC;		/* otherwise relative to IAR after fetch */

			/* to do: the previous steps may lead to incorrect GUI SAR/SBR display if the instruction doesn't actually fetch the index value. Check this. */
		}

		switch (OP) { 						/* decode instruction */
			case 0x01:						/* --- XIO --- */
				iocc_addr = ReadW(eaddr);			/* get IOCC packet */
				iocc_op   = ReadW(eaddr|1);			/* note 'or' not plus, address must be even for proper operation */

				iocc_dev  = (iocc_op  >> 11) & 0x001F;
				iocc_func = (iocc_op  >>  8) & 0x0007;
				iocc_mod  =  iocc_op         & 0x00FF;

				if ((cpu_unit.flags & (UNIT_ATT|UNIT_TRACE_IO)) == (UNIT_ATT|UNIT_TRACE_IO))
					trace_io("* XIO %s %s mod %02x addr %04x", xio_funcs[iocc_func], (iocc_func == XIO_SENSE_IRQ) ? "-" : xio_devs[iocc_dev], iocc_mod, iocc_addr);

				ACC = 0;							/* ACC is destroyed, and default XIO_SENSE_DEV result is 0 */

				switch (iocc_func) {
					case XIO_UNUSED:
						sprintf(msg, "Unknown XIO op %x on device %02x (%s)", iocc_func, iocc_dev,  xio_devs[iocc_dev]);
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
							case 0x02:				/* 1442 card reader/punch */
								xio_1442_card(iocc_addr, iocc_func, iocc_mod);
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
								xio_sca(iocc_addr, iocc_func, iocc_mod);
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
							case 0x1a:				/* 2741 Attachment (nonstandard serial interface used by APL\1130 */
								xio_t2741_terminal(iocc_addr, iocc_func, iocc_mod);
								break;
							default:
								sprintf(msg, "unknown device %02x", iocc_dev);
								xio_error(msg);
								break;
						}
				}

				calc_ints();						/* after every XIO, reset int_mask just in case */
				break;

			case 0x02:						/* --- SLA,SLT,SLC,SLCA,NOP - Shift Left family --- */
				if (F)
					weirdop("Long Left Shift");

				CCC = ((TAG == 0) ? DSPLC : ReadIndex(TAG)) & 0x003F;
				ARFSET(CCC);
				if (CCC == 0)
					break;							/* shift of zero is a NOP */

				switch (IR & 0x00C0) {
					case 0x0040:					/* SLCA */
						if (TAG) {
							while (CCC > 0 && (ACC & 0x8000) == 0) {
								ACC <<= 1;
								CCC--;
							}
							C = (CCC != 0);
							WriteIndex(TAG, (ReadIndex(TAG) & 0xFF00) | CCC);	/* put low 6 bits back into index register and zero bits 8 and 9 */
							break;
						}
						/* if TAG == 0, fall through and treat like normal shift SLA */

					case 0x0000:					/* SLA  */
						while (CCC > 0) {
							C    = (ACC & 0x8000);
							ACC  = (ACC << 1) & 0xFFFF;
							CCC--;
						}
						break;

					case 0x00C0:					/* SLC  */
						if (TAG) {
							while (CCC > 0 && (ACC & 0x8000) == 0) {
								abit = (EXT & 0x8000) >> 15;
								ACC  = ((ACC << 1) & 0xFFFF) | abit;
								EXT  = (EXT << 1);
								CCC--;
							}
							C = (CCC != 0);
							WriteIndex(TAG, (ReadIndex(TAG) & 0xFF00) | CCC);		/* put 6 bits back into low byte of index register */
							break;
						}
						/* if TAG == 0, fall through and treat like normal shift SLT */

					case 0x0080:					/* SLT  */
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
				if (F)
					weirdop("Long Right Shift");

				CCC = ((TAG == 0) ? DSPLC : ReadIndex(TAG)) & 0x3F;
				ARFSET(CCC);
				if (CCC == 0)
					break;							/* NOP */

				switch (IR & 0x00C0) {
					case 0x0000:					/* SRA  */
						ACC = (CCC < 16) ? ((ACC & 0xFFFF) >> CCC) : 0;
						CCC = 0;
						break;

					case 0x0040:					/* invalid */
						wait_state = WAIT_INVALID_OP;
						break;

					case 0x0080:					/* SRT */
						while (CCC > 0) {
							xbit = (ACC & 0x0001) << 15;
							abit = (ACC & 0x8000);
							ACC  = ((ACC >> 1) & 0x7FFF) | abit;
							EXT  = ((EXT >> 1) & 0x7FFF) | xbit;
							CCC--;
						}
						break;

					case 0x00C0:					/* RTE */
						while (CCC > 0) {
							abit = (EXT & 0x0001) << 15;
							xbit = (ACC & 0x0001) << 15;
							ACC  = ((ACC >> 1) & 0x7FFF) | abit;
							EXT  = ((EXT >> 1) & 0x7FFF) | xbit;
							CCC--;
						}
						break;

					default:
						bail("SRA switch, can't happen");
						break;
				}
				break;

			case 0x04:						/* --- LDS - Load Status --- */
				if (F)						/* never fetches second word? */
					weirdop("Long LDS");

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
				C = V = 0;							/* clear flags after storing */
				break;

			case 0x06:						/* --- WAIT --- */
/* I am no longer doing the fetch if a long wait is encountered
 * The 1130 diagnostics use WAIT instructions with the F bit set in some display error codes.
 * (The wait instruction's opcode is displayed in the Storage Buffer Register on the console display, 
 * since the last thing fetched was the instruction)
 */
				wait_state = WAIT_OP;

				SAR = prev_IAR;		/* this is a hack; ensure that the SAR/SBR display shows the WAIT instruction fetch */
				SBR = IR;
				break;

			case 0x08:						/* --- BSI - Branch and store IAR --- */
				if (F) {
					if (bsctest(IR, F))				/* do standard BSC long format testing */
						break;						/* if any condition is true, do nothing */
				}
				WriteW(eaddr, IAR);					/* do subroutine call */
				archive_backtrace("BSI");			/* save info in back-trace buffer */
				IAR = (eaddr + 1) & mem_mask;
				break;

			case 0x09:						/* --- BSC - Branch and skip on Condition --- */
				if (F) {
					if (bsctest(IR, F))				/* long format; any indicator cancels branch */
						break;

					archive_backtrace((DSPLC & 0x40) ? "BOSC" : "BSC");	/* save info in back-trace buffer */
					IAR = eaddr;					/* no indicator means branch taken */
				}
				else {								/* short format: skip if any indicator hits */
					if (bsctest(IR, F)) {
						archive_backtrace((DSPLC & 0x40) ? "BOSC" : "BSC");		/* save info in back-trace buffer */
						INCREMENT_IAR;
					}
				}
/* 27Mar02: moved this test out of the (F) condition; BOSC works even in the
 * short form. The displacement field in this instruction is always the set of
 * condition bits, and the interrupt clear bit doesn't collide. */

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
					WriteIndex(TAG, eaddr);
				else {
					archive_backtrace("LDX");		/* save info in back-trace buffer */
					IAR = eaddr;					/* what happens in short form? can onlyjump to low addresses? */
				}
				break;

			case 0x0d:						/* --- STX - Store Index --- */
				if (F) {							/* compute EA without any indexing */
					eaddr = (INDIR) ? ReadW(word2) : word2;
				}
				else {
					eaddr = IAR + DSPLC;
				}
				WriteW(eaddr, TAG ? ReadIndex(TAG) : IAR);
				break;

			case 0x0e:						/* --- MDX - Modify Index and Skip --- */
				if (F) {							/* long format: adjust memory location */
					if (TAG) {
						oldval = ReadIndex(TAG);	/* add word2 to index */
						newval = oldval + (INDIR ? ReadW(word2) : word2);
						WriteIndex(TAG, newval);
					}
					else {
						oldval = ReadW(word2);
						DSPLC = IR & 0x00FF;		/* use extended displacement (no INDIR bit, it's is part of displacement in this op) */
						if (DSPLC & 0x0080)
							DSPLC |= ~ 0xFF;
						newval = oldval + DSPLC;	/* add modifier to @word2 */
						WriteW(word2, newval);
					}
				}
				else {								/* short format: adust IAR or index */
					if (TAG) {
						oldval = ReadIndex(TAG);	/* add displacement to index */
						newval = oldval + DSPLC;
						WriteIndex(TAG, newval);
					}
					else {
						oldval = IAR;				/* add displacement to IAR */
						newval = IAR + DSPLC;
						archive_backtrace("MDX");
						IAR    = newval & mem_mask;
					}
				}

				if ((F || TAG) && (((newval & 0xFFFF) == 0) || ((oldval & 0x8000) != (newval & 0x8000)))) {
					archive_backtrace("SKP");
					INCREMENT_IAR;					/* skip if index sign change or zero */
				}
				break;

			case 0x10:						/* --- A - Add --- */
				/* in adds and subtracts, carry is set or cleared, overflow is set only */
				src  = ReadW(eaddr);
				ARFSET(src);
				src2 = ACC;
				ACC  = (ACC + src) & 0xFFFF;

				C = ACC < src;
				if (! V)
					V = SIGN_BIT((~src ^ src2) & (src ^ ACC));
				break;

			case 0x11:						/* --- AD - Add Double --- */
				src  = ((ACC << 16) | (EXT & 0xFFFF));
				ARFSET(EXT);
				src2 = (ReadW(eaddr) << 16) + ReadW(eaddr|1);
				dst  = src + src2;
				ACC  = (dst >> 16) & 0xFFFF;
				EXT  = dst & 0xFFFF;

				C = (uint32) dst < (uint32) src;
				if (! V)
					V = DWSIGN_BIT((~src ^ src2) & (src ^ dst));
				break;

			case 0x12:						/* --- S - Subtract	--- */
				src  = ACC;
				ARFSET(src);
				src2 = ReadW(eaddr);
				ACC  = (ACC-src2) & 0xFFFF;

				C = src < src2;
				if (! V)
					V = SIGN_BIT((src ^ src2) & (src ^ ACC));
				break;

			case 0x13:						/* --- SD - Subtract Double	--- */
				src  = ((ACC << 16) | (EXT & 0xFFFF));
				ARFSET(EXT);
				src2 = (ReadW(eaddr) << 16) + ReadW(eaddr|1);
				dst  = src - src2;
				ACC  = (dst >> 16) & 0xFFFF;
				EXT  = dst & 0xFFFF;

				C = (uint32) src < (uint32) src2;
				if (! V)
					V = DWSIGN_BIT((src ^ src2) & (src ^ dst));
				break;

			case 0x14:						/* --- M - Multiply	--- */
				if ((src = ACC & 0xFFFF)  & 0x8000)	/* sign extend the values */
					src	 |= ~0xFFFF;
				if ((src2 = ReadW(eaddr)) & 0x8000)
					src2 |= ~0xFFFF;

				ARFSET(src2);
				dst = src * src2;
				ACC = (dst >> 16) & 0xFFFF;			/* split the results */
				EXT = dst & 0xFFFF;
				break;

			case 0x15:						/* --- D - Divide --- */
				src  = ((ACC << 16) | (EXT & 0xFFFF));
				if ((src2 = ReadW(eaddr)) & 0x8000)
					src2 |= ~0xFFFF;				/* oops: sign extend was missing, fixed 18Mar03 */

				ARFSET(src2);

	/* 24-Mar-11 - Failed IBM diagnostics because I was not checking for overflow here. Fixed.
	 *             Have to check for special case of -maxint / -1 because Windows (at least) generates an exception
	 */
				if (src2 == 0) {
					V = 1;							/* divide by zero just sets overflow, ACC & EXT are undefined */
				}
				else if ((src2 == -1) && ((uint32)src == 0x80000000)) {
					V = 1;							/* another special case: max negative int / -1 also overflows */
				}
				else {
					result = src / src2;			/* compute dividend */
					if ((result > 32767) || (result < -32768))
						V = 1;						/* if result does not fit into 16 bits, we have an overflow */
					ACC = result & 0xFFFF;
					EXT = (src % src2) & 0xFFFF;
				}
				break;

			case 0x18:						/* --- LD - Load ACC --- */
				ACC = ReadW(eaddr);
				break;

			case 0x19:						/* --- LDD - Load Double --- */
				ACC = ReadW(eaddr);
				EXT = ReadW(eaddr|1);				/* notice address is |1 not +1 */
				break;

			case 0x1a:						/* --- STO - Store ACC --- */
				WriteW(eaddr, ACC);
				break;

			case 0x1b:						/* --- STD - Store Double --- */
				WriteW(eaddr|1, EXT);
				WriteW(eaddr,   ACC);				/* order is important: if odd addr, only ACC is stored */
				break;

			case 0x1c:						/* --- AND - Logical AND --- */
				src = ReadW(eaddr); 
				ARFSET(src);
				ACC &= src;
				break;

			case 0x1d:						/* --- OR - Logical OR --- */
				src = ReadW(eaddr); 
				ARFSET(src);
				ACC |= src;
				break;

			case 0x1e:						/* --- EOR - Logical Excl OR --- */
				src = ReadW(eaddr); 
				ARFSET(src);
				ACC ^= src;
				break;

			case 0x16:
			case 0x17:
#ifdef ENABLE_1800_SUPPORT
				if (is_1800) {
					if (OP == 0x16) {		/* --- CMP - Compare --- */
						src  = ACC;						/* like subtract but result isn't stored */
						src2 = ReadW(eaddr);
						dst  = (ACC-src2) & 0xFFFF;
						C    = src < src2;

						if (dst & 0x8000)				/* if ACC <  operand, skip 1 instruction */
							IAR = IAR+1;
						else if ((dst & 0xFFFF) == 0)	/* if ACC == operand, skip 2 instructions */
							IAR = IAR+2;
					}
					else {					/* --- DCMP - Compare Double --- */
						src  = ((ACC << 16) | (EXT & 0xFFFF));
						src2 = (ReadW(eaddr) << 16) + ReadW(eaddr|1);
						dst  = src - src2;
						C    = (uint32) src < (uint32) src2;

						if (dst & 0x80000000)			/* if ACC_EXT <  operand, skip 1 instruction */
							IAR = IAR+1;
						else if (dst == 0)				/* if ACC_EXT == operand, skip 2 instructions */
							IAR = IAR+2;
					}

					break;								/* these are legal instructions on the 1800 */
				}
#endif
				/* 1130: these are not legal instructions, fall through */

			default:
/* all invalid instructions act like waits */
/*			case 0x00: */
/*			case 0x07: */
/*			case 0x0a: */
/*			case 0x0b: */
/*			case 0x0f: */
/*			case 0x1f: */
				wait_state = WAIT_INVALID_OP;
				SAR = prev_IAR;		/* this is a hack; ensure that the SAR/SBR display shows the WAIT instruction fetch */
				SBR = IR;
				break;
		}											/* end instruction decode switch */

		if (RUNMODE != MODE_RUN && RUNMODE != MODE_INT_RUN)
			reason = STOP_WAIT;

		if (tbit && (ipl < 0)) {					/* if INT_RUN mode, set IRQ5 after this instr */
			GUI_BEGIN_CRITICAL_SECTION
			SETBIT(cpu_dsw, CPU_DSW_INT_RUN);
			SETBIT(ILSW[5], ILSW_5_INT_RUN_PROGRAM_STOP);
			int_req |= INT_REQ_5;
			GUI_END_CRITICAL_SECTION
		}
	}										/* end main loop */

#ifdef GUI_SUPPORT
	gui_run(FALSE);
#endif

	running   = FALSE;
	int_lamps = 0;							/* display only currently active interrupts while halted */

	if (reason == STOP_WAIT || reason == STOP_INVALID_INSTR) {
		wait_state = 0;						/* on resume, don't wait */
		wait_lamp = TRUE;					/* but keep the lamp lit on the GUI */

		CLRBIT(cpu_dsw, CPU_DSW_PROGRAM_STOP);	/* and on resume, reset program start bit */
		if ((cpu_dsw & CPU_DSW_PROGRAM_STOP) == 0)
			CLRBIT(ILSW[5], ILSW_5_INT_RUN_PROGRAM_STOP);
	}

	if (cgi)								/* give CGI hook function a chance to do something */
		cgi_stop(reason);

	return reason;
}

/*
 * simh_status_to_stopcode - convert a SCPE_xxx value from sim_process_event into a STOP_xxx code
 */
						
static int simh_status_to_stopcode (int status)
{
	return (status == SCPE_BREAK) ? STOP_BREAK     :
		   (status == SCPE_STOP)  ? STOP_IMMEDIATE :
		   (status == SCPE_STEP)  ? STOP_STEP      : STOP_OTHER;
}

/* ------------------------------------------------------------------------ 
 * bsctest - perform standard set of condition tests. We return TRUE if any
 * of the condition bits specified in DSPLC test positive, FALSE if none are true.
 * If reset_V is TRUE, we reset the oVerflow flag after testing it.
 * 24-Mar-11: no, we reset the oVerflow flag no matter what reset_V is
 * ------------------------------------------------------------------------ */

static t_bool bsctest (int32 DSPLC, t_bool reset_V)
{
	if (DSPLC & 0x01) {						/* Overflow off (note inverted sense) */
		if (! V)
			return TRUE;
// 24-Mar-11 - V is always reset when tested, in both the long and short forms of the instructions
//		else if (reset_V)					/* reset after testing */
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

	if (ipl == 5 && tbit) {				/* if we are exiting an INT_RUN interrupt, clear it for the next instruction */
		CLRBIT(cpu_dsw, CPU_DSW_INT_RUN);
		if ((cpu_dsw & CPU_DSW_PROGRAM_STOP) == 0)
			CLRBIT(ILSW[5], ILSW_5_INT_RUN_PROGRAM_STOP);
	}

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

	if ((cpu_unit.flags & (UNIT_ATT|UNIT_TRACE_INSTR)) == (UNIT_ATT|UNIT_TRACE_INSTR)) {	/* record reset in CPU log */
		fseek(cpu_unit.fileref, 0, SEEK_END);
		fprintf(cpu_unit.fileref, "---RESET---" CRLF);
	}

	GUI_BEGIN_CRITICAL_SECTION

	CLRBIT(cpu_dsw, CPU_DSW_PROGRAM_STOP|CPU_DSW_INT_RUN);
	CLRBIT(ILSW[5], ILSW_5_INT_RUN_PROGRAM_STOP);

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

t_stat cpu_set_size (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
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

/* processor type */

t_stat cpu_set_type (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
	REG *r;

	is_1800 = (value & UNIT_1800) != 0;					/* set is_1800 mode flag */

	for (r = cpu_reg; r->name != NULL; r++) {			/* unhide or hide 1800-specific registers & state */
		if (strnicmp(r->name, "XR", 2) == 0) {
			if (value & UNIT_1800)
				CLRBIT(r->flags, REG_HIDDEN|REG_RO);
			else
				SETBIT(r->flags, REG_HIDDEN|REG_RO);
		}
	}

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
			break;

		default:
			sprintf(msg, "Invalid console switch function %x", func);
			xio_error(msg);
	}
}

/* ------------------------------------------------------------------------ 
 * Illegal IO operation.  Not yet sure what the actual CPU does in this case
 * ------------------------------------------------------------------------ */

void xio_error (const char *msg)
{
	printf("*** XIO error at %04x: %s\n", prev_IAR, msg);
	if (cgi)									/* if this happens in CGI mode, probably best to halt */
		break_simulation(STOP_CRASH);
}

/* ------------------------------------------------------------------------ 
 * register_cmd - add a command to the extensible command table
 * ------------------------------------------------------------------------ */

t_stat register_cmd (const char *name, t_stat (*action)(int32 flag, CONST char *ptr), int arg, const char *help)
{
	int i;

	for (i = 0; i < MAX_EXTRA_COMMANDS; i++) {	/* find end of command table */
		if (x_cmds[i].action == action)
			return SCPE_OK;						/* command is already there, just return */
		if (x_cmds[i].name == NULL)
			break;
	}

	if (i >= (MAX_EXTRA_COMMANDS-1)) {			/* no more room (we need room for the NULL) */
		fprintf(stderr, "The command table is full - rebuild the simulator with more free slots\n");
		return SCPE_ARG;
	}

	x_cmds[i].action = action;					/* add new command */
	x_cmds[i].name   = name;
	x_cmds[i].arg    = arg;
	x_cmds[i].help   = help;

	i++;
	x_cmds[i].action = NULL;					/* move the NULL terminator */
	x_cmds[i].name   = NULL;

	return SCPE_OK;
}

#ifdef USE_MY_ECHO_CMD
/* ------------------------------------------------------------------------ 
 * echo_cmd - just echo the command line
 * ------------------------------------------------------------------------ */

static t_stat echo_cmd (int32 flag, CONST char *cptr)
{
	printf("%s\n", cptr);
	return SCPE_OK;
}
#endif

/* ------------------------------------------------------------------------ 
 * sim_init - initialize simulator upon startup of scp, before reset
 * ------------------------------------------------------------------------ */

void sim_init (void)
{
	sim_gui = ! (sim_switches & SWMASK('G'));	/* -g means no GUI */

	sim_vm_cmd = x_cmds;						/* provide list of additional commands */

#ifdef GUI_SUPPORT
	/* set hook routines for GUI command processing */
	if (sim_gui) {
		sim_vm_read = &read_cmdline;
		sim_vm_post = &update_gui;
	}
#endif

#ifdef ENABLE_BACKTRACE
	/* add the BACKTRACE command */
	register_cmd("BACKTRACE", &backtrace_cmd, 0, "ba{cktrace} {n}          list last n branches/skips/interrupts\n");
#endif

	register_cmd("VIEW",      &view_cmd,      0, "v{iew} filename          view a text file with notepad\n");

#ifdef USE_MY_ECHO_CMD
	register_cmd("ECHO",      &echo_cmd,      0, "echo args...             echo arguments passed to command\n");
#endif
}

/* ------------------------------------------------------------------------ 
 * archive_backtrace - record a jump, skip, branch or whatever
 * ------------------------------------------------------------------------ */

#ifdef ENABLE_BACKTRACE

#define MAXARCHIVE 16

static struct tag_arch {
	int iar;
	const char *inst;
} arch[MAXARCHIVE];
int narchived = 0, archind = 0;

static void archive_backtrace (const char *inst)
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

static t_stat backtrace_cmd (int32 flag, CONST char *cptr)
{
	int n;

	if ((n = atoi(cptr)) <= 0)
		n = 6;

	show_backtrace(n);
	return SCPE_OK;
}
#else

/* stub this for the disk routine */

void void_backtrace (int afrom, int ato)
{
}

#endif

/*************************************************************************************
 * CPU log routines -- attaching a file to the CPU creates a trace of instructions and register values
 *
 * Syntax is WEIRD:
 *
 * attach cpu logfile					log instructions and registers to file "logfile"
 * attach -f cpu cpu.log				log instructions, registers and floating point acc
 * attach -m cpu mapfile logfile		read addresses from "mapfile", log instructions to "logfile"
 * attach -f -m cpu mapfile logfile		same and log floating point stuff too
 *
 * mapfile if specified is a list of symbols and addresses of the form:
 *       symbol hexval
 *
 * e.g.
 * FSIN   082E
 * FARC   09D4
 * FMPY   09A4
 * NORM   0976
 * XMDS   095A
 * START  021A
 *
 * These values are easily obtained from a load map created by
 * XEQ       L
 *
 * The log output is of the form
 *
 *  IAR             ACC  EXT  (flt)    XR1  XR2  XR3 CVI      FAC      OPERATION
 * --------------- ---- ---- -------- ---- ---- ---- --- ------------- -----------------------
 * 002a       002a 1234 5381  0.14222 00b3 0236 3f7e CV   1.04720e+000 4c80 BSC  I  ,0028   
 * 081d PAUSE+000d 1234 5381  0.14222 00b3 0236 3f7e CV   1.04720e+000 7400 MDM  L  00f0,0 (0)   
 * 0820 PAUSE+0010 1234 5381  0.14222 00b3 0236 3f7e CV   1.04720e+000 7201 MDX   2 0001   
 * 0821 PAUSE+0011 1234 5381  0.14222 00b3 0237 3f7e CV   1.04720e+000 6a03 STX   2 0003   
 * 0822 PAUSE+0012 1234 5381  0.14222 00b3 0237 3f7e CV   1.04720e+000 6600 LDX  L2 0231   
 * 0824 PAUSE+0014 1234 5381  0.14222 00b3 0231 3f7e CV   1.04720e+000 4c00 BSC  L  ,0237   
 * 0237 START+001d 1234 5381  0.14222 00b3 0231 3f7e CV   1.04720e+000 4480 BSI  I  ,3fff   
 * 082f FSIN +0001 1234 5381  0.14222 00b3 0231 3f7e CV   1.04720e+000 4356 BSI   3 0056   
 * 3fd5 ILS01+35dd 1234 5381  0.14222 00b3 0231 3f7e CV   1.04720e+000 4c00 BSC  L  ,08de   
 *
 * IAR - instruction address register value, optionally including symbol and offset
 * ACC - accumulator
 * EXT - extension
 * flt - ACC+EXT interpreted as the mantissa of a floating pt number (value 0.5 -> 1)
 * XR* - index registers
 * CVI - carry, overflow and interrupt indicators
 * FAC - floating point accumulator (exponent at 125+XR3, mantissa at 126+XR3 and 127+XR3)
 * OP  - opcode value and crude disassembly
 *
 * flt and FAC are displayed only when the -f flag is specified in the attach command
 * The label and offset and displayed only when the -m flag is specified in the attach command
 *
 * The register values shown are the values BEFORE the instruction is executed.
 *************************************************************************************/

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw);

typedef struct tag_symentry {
	struct tag_symentry *next;
	int  addr;
	char sym[6];
} SYMENTRY, *PSYMENTRY;

static PSYMENTRY syms = NULL;
static t_bool new_log, log_fac;

static t_stat cpu_attach (UNIT *uptr, CONST char *cptr)
{
	char mapfile[200], buf[200], sym[100], gbuf[2*CBUFSIZE];
	int addr;
	PSYMENTRY n, prv, s;
	FILE *fd;

	remove(cptr);							/* delete old log file, if present */
	new_log = TRUE;
	log_fac = sim_switches & SWMASK ('F');	/* display the FAC and the ACC/EXT as fixed point. */

	for (s = syms; s != NULL; s = n) {		/* free any old map entries */
		n = s->next;
		free(s);
	}
	syms = NULL;
		
	if (sim_switches & SWMASK('M')) {		/* use a map file to display relative addresses */
		cptr = get_glyph(cptr, mapfile, 0);
		if (! *mapfile) {
			printf("/m must be followed by a filename\n");
			return SCPE_ARG;
		}
		if ((fd = fopen(mapfile, "r")) == NULL) {
			sim_perror(mapfile);
			return SCPE_OPENERR;
		}

		while (fgets(buf, sizeof(buf), fd) != NULL) {		/* read symbols & addresses, link in descending address order */
			if (sscanf(buf, "%s %x", sym, &addr) != 2)
				continue;
			if (*buf == ';')
				continue;

			for (prv = NULL, s = syms; s != NULL; prv = s, s = s->next) {
				if (s->addr < addr)
					break;
			}

			if ((n = (PSYMENTRY)malloc(sizeof(SYMENTRY))) == NULL) {
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

	return attach_unit(uptr, quotefix(cptr, gbuf));			/* fix quotes in filenames & attach */
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
					fac *= 2, exp--;
			}
			else if (exp > 0)
				fac *= (float) (1 << exp);
			else if (exp < -30) {
				fac /= (float) (1 << 30);
				exp += 30;
				while (exp < 0)
					fac /= 2, exp++;
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

static void trace_common (FILE *fout)
{
	fprintf(fout, "[IAR %04x IPL %c] ", IAR, (ipl < 0) ? ' ' : ('0' + ipl));
}

void trace_io (const char *fmt, ...)
{
	va_list args;

	if ((cpu_unit.flags & UNIT_ATT) == 0)
		return;

	trace_common(cpu_unit.fileref);
	va_start(args, fmt);							/* get pointer to argument list */
	vfprintf(cpu_unit.fileref, fmt, args);			/* write errors to cpu log file */
	va_end(args);

	fputs(CRLF, cpu_unit.fileref);
}

void trace_both (const char *fmt, ...)
{
	va_list args;

	if (cpu_unit.flags & UNIT_ATT) {
		trace_common(cpu_unit.fileref);
		va_start(args, fmt);						/* get pointer to argument list */
		vfprintf(cpu_unit.fileref, fmt, args);
		va_end(args);
		fputs(CRLF, cpu_unit.fileref);
	}

	trace_common(stdout);
	va_start(args, fmt);							/* get pointer to argument list */
	vfprintf(stdout, fmt, args);
	va_end(args);
	putchar('\n');
}

/* debugging */

void debug_print (const char *fmt, ...)
{
	va_list args;
	FILE *fout = stdout;
	t_bool binarymode = FALSE;

#define DEBUG_TO_PRINTER

#ifdef DEBUG_TO_PRINTER
	if (prt_unit[0].fileref != NULL) {		/* THIS IS TEMPORARY */
		fout = prt_unit[0].fileref;
		binarymode = TRUE;
	}
#endif

	va_start(args, fmt);
	vfprintf(fout, fmt, args);
	if (cpu_unit.flags & UNIT_ATT)
		vfprintf(cpu_unit.fileref, fmt, args);
	va_end(args);

	if (strchr(fmt, '\n') == NULL) {		/* be sure to emit a newline */
		if (binarymode)
			fputs(CRLF, fout);
		else
			putc('\n', fout);

		if (cpu_unit.flags & UNIT_ATT)
			fputs(CRLF, cpu_unit.fileref);
	}
}

#ifdef _WIN32
#include <windows.h>
#endif

/* view_cmd - let user view and/or edit a file (e.g. a printer output file, script, or source deck) */

static t_stat view_cmd (int32 flag, CONST char *cptr)
{
#ifdef _WIN32
	char cmdline[256];

	sprintf(cmdline, "notepad %s", cptr);
	WinExec(cmdline, SW_SHOWNORMAL);
#endif
	return SCPE_OK;
}

/* web server version - hooks for CGI mode. These function pointer can be set by the CGI version's main() routine */

void (*cgi_start_hook)(void) = NULL;			/* these can be defined by a CGI wrapper to do things on start and stop of simulation */
void (*cgi_end_hook)(void)   = NULL;

static void cgi_start (void)
{
	if (cgi_start_hook != NULL)
		(*cgi_start_hook)();
}

static void cgi_stop (t_stat reason)
{
	if (cgi_end_hook != NULL)
		(*cgi_end_hook)();
}
