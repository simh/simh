/* scp.c: simulator control program

   Copyright (c) 1993-2002, Robert M Supnik

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

   16-Nov-02	RMS	Fixed bug in register name match algorithm
   13-Oct-02	RMS	Fixed Borland compiler warnings (found by Hans Pufal)
   05-Oct-02	RMS	Fixed bugs in set_logon, ssh_break (found by David Hittner)
			Added support for fixed buffer devices
			Added support for Telnet console, removed VT support
			Added help <command>
			Added VMS file optimizations (from Robert Alan Byer)
			Added quiet mode, DO with parameters, GUI interface,
			   extensible commands (from Brian Knittel)
			Added device enable/disable commands
   14-Jul-02	RMS	Fixed exit bug in do, added -v switch (from Brian Knittel)
   17-May-02	RMS	Fixed bug in fxread/fxwrite error usage (found by
			Norm Lastovic)
   02-May-02	RMS	Added VT emulation interface, changed {NO}LOG to SET {NO}LOG
   22-Apr-02	RMS	Fixed laptop sleep problem in clock calibration, added
			magtape record length error (found by Jonathan Engdahl)
   26-Feb-02	RMS	Fixed initialization bugs in do_cmd, get_avail
			(found by Brian Knittel)
   10-Feb-02	RMS	Fixed problem in clock calibration
   06-Jan-02	RMS	Moved device enable/disable to simulators
   30-Dec-01	RMS	Generalized timer packaged, added circular arrays
   19-Dec-01	RMS	Fixed DO command bug (found by John Dundas)
   07-Dec-01	RMS	Implemented breakpoint package
   05-Dec-01	RMS	Fixed bug in universal register logic
   03-Dec-01	RMS	Added read-only units, extended SET/SHOW, universal registers
   24-Nov-01	RMS	Added unit-based registers
   16-Nov-01	RMS	Added DO command
   28-Oct-01	RMS	Added relative range addressing
   08-Oct-01	RMS	Added SHOW VERSION
   30-Sep-01	RMS	Relaxed attach test in BOOT
   27-Sep-01	RMS	Added queue count routine, fixed typo in ex/mod
   17-Sep-01	RMS	Removed multiple console support
   07-Sep-01	RMS	Removed conditional externs on function prototypes
			Added special modifier print
   31-Aug-01	RMS	Changed int64 to t_int64 for Windoze (V2.7)
   18-Jul-01	RMS	Minor changes for Macintosh port
   12-Jun-01	RMS	Fixed bug in big-endian I/O (found by Dave Conroy)
   27-May-01	RMS	Added multiple console support
   16-May-01	RMS	Added logging
   15-May-01	RMS	Added features from Tim Litt
   12-May-01	RMS	Fixed missing return in disable_cmd
   25-Mar-01	RMS	Added ENABLE/DISABLE
   14-Mar-01	RMS	Revised LOAD/DUMP interface (again)
   05-Mar-01	RMS	Added clock calibration support
   05-Feb-01	RMS	Fixed bug, DETACH buffered unit with hwmark = 0
   04-Feb-01	RMS	Fixed bug, RESTORE not using device's attach routine
   21-Jan-01	RMS	Added relative time
   22-Dec-00	RMS	Fixed find_device for devices ending in numbers
   08-Dec-00	RMS	V2.5a changes
   30-Oct-00	RMS	Added output file option to examine
   11-Jul-99	RMS	V2.5 changes
   13-Apr-99	RMS	Fixed handling of 32b addresses
   04-Oct-98	RMS	V2.4 changes
   20-Aug-98	RMS	Added radix commands
   05-Jun-98	RMS	Fixed bug in ^D handling for UNIX
   10-Apr-98	RMS	Added switches to all commands
   26-Oct-97	RMS	Added search capability
   25-Jan-97	RMS	Revised data types
   23-Jan-97	RMS	Added bi-endian I/O
   06-Sep-96	RMS	Fixed bug in variable length IEXAMINE
   16-Jun-96	RMS	Changed interface to parse/print_sym
   06-Apr-96	RMS	Added error checking in reset all
   07-Jan-96	RMS	Added register buffers in save/restore
   11-Dec-95	RMS	Fixed ordering bug in save/restore
   22-May-95	RMS	Added symbolic input
   13-Apr-95	RMS	Added symbolic printouts
*/

/* Macros and data structures */

#include "sim_defs.h"
#include "sim_rev.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <signal.h>
#include <ctype.h>

#define EX_D		0				/* deposit */
#define EX_E		1				/* examine */
#define EX_I		2				/* interactive */
#define SCH_OR		0				/* search logicals */
#define SCH_AND		1
#define SCH_XOR		2
#define SCH_E		0				/* search booleans */
#define SCH_N		1
#define SCH_G		2
#define SCH_L		3
#define SCH_EE		4
#define SCH_NE		5
#define SCH_GE		6
#define SCH_LE		7
#define SSH_ST		0				/* set */
#define SSH_SH		1				/* show */
#define SSH_CL		2				/* clear */
#define RU_RUN		0				/* run */
#define RU_GO		1				/* go */
#define RU_STEP		2				/* step */
#define RU_CONT		3				/* continue */
#define RU_BOOT		4				/* boot */

#define SWHIDE		(1u << 26)			/* enable hiding */
#define SRBSIZ		1024				/* save/restore buffer */
#define SIM_BRK_INILNT	16384				/* bpt tbl length */
#define SIM_BRK_ALLTYP	0xFFFFFFFF
#define SIM_NTIMERS	8				/* # timers */
#define SIM_TMAX	500				/* max timer makeup */
#define UPDATE_SIM_TIME(x) sim_time = sim_time + (x - sim_interval); \
	sim_rtime = sim_rtime + ((uint32) (x - sim_interval)); \
	x = sim_interval

#define print_val(a,b,c,d) fprint_val (stdout, (a), (b), (c), (d))
#define SZ_D(dp) (size_map[((dp)->dwidth + CHAR_BIT - 1) / CHAR_BIT])
#define SZ_R(rp) \
	(size_map[((rp)->width + (rp)->offset + CHAR_BIT - 1) / CHAR_BIT])
#if defined (t_int64)
#define SZ_LOAD(sz,v,mb,j) \
	if (sz == sizeof (uint8)) v = *(((uint8 *) mb) + j); \
	else if (sz == sizeof (uint16)) v = *(((uint16 *) mb) + j); \
	else if (sz == sizeof (uint32)) v = *(((uint32 *) mb) + j); \
	else v = *(((t_uint64 *) mb) + j);
#define SZ_STORE(sz,v,mb,j) \
	if (sz == sizeof (uint8)) *(((uint8 *) mb) + j) = (uint8) v; \
	else if (sz == sizeof (uint16)) *(((uint16 *) mb) + j) = (uint16) v; \
	else if (sz == sizeof (uint32)) *(((uint32 *) mb) + j) = (uint32) v; \
	else *(((t_uint64 *) mb) + j) = v;
#else
#define SZ_LOAD(sz,v,mb,j) \
	if (sz == sizeof (uint8)) v = *(((uint8 *) mb) + j); \
	else if (sz == sizeof (uint16)) v = *(((uint16 *) mb) + j); \
	else v = *(((uint32 *) mb) + j);
#define SZ_STORE(sz,v,mb,j) \
	if (sz == sizeof (uint8)) *(((uint8 *) mb) + j) = (uint8) v; \
	else if (sz == sizeof (uint16)) *(((uint16 *) mb) + j) = (uint16) v; \
	else *(((uint32 *) mb) + j) = v;
#endif
#define GET_SWITCHES(cp,gb) \
	sim_switches = 0; \
	while (*cp == '-') { \
		int32 lsw; \
		cp = get_glyph (cp, gb, 0); \
		if ((lsw = get_switches (gb)) <= 0) return SCPE_INVSW; \
		sim_switches = sim_switches | lsw;  }
#define GET_RADIX(val,dft) \
	if (sim_switches & SWMASK ('O')) val = 8; \
	else if (sim_switches & SWMASK ('D')) val = 10; \
	else if (sim_switches & SWMASK ('H')) val = 16; \
	else val = dft;

struct brktab {
	t_addr	addr;
	int32	typ;
	int32	cnt;
	char	*act;
};

typedef struct brktab BRKTAB;

#if defined(VMS)
#define FOPEN(file_spec, mode) fopen (file_spec, mode, "ALQ=32", "DEQ=4096", \
         "MBF=6", "MBC=128", "FOP=cbt,tef,sqo", "ROP=rah,wbh")
#else
#define FOPEN(file_spec, mode) fopen (file_spec, mode)
#endif

/* VM interface */

extern char sim_name[];
extern DEVICE *sim_devices[];
extern REG *sim_PC;
extern char *sim_stop_messages[];
extern t_stat sim_instr (void);
extern t_stat sim_load (FILE *ptr, char *cptr, char *fnam, int32 flag);
extern int32 sim_emax;
extern t_stat fprint_sym (FILE *ofile, t_addr addr, t_value *val,
	UNIT *uptr, int32 sw);
extern t_stat parse_sym (char *cptr, t_addr addr, UNIT *uptr, t_value *val,
	int32 sw);

/* The per-simulator init routine is a weak global that defaults to NULL
   The other per-simulator pointers can be overrriden by the init routine */

void (*sim_vm_init) (void);
char* (*sim_vm_read) (char *ptr, int32 size, FILE *stream) = NULL;
void (*sim_vm_post) (t_bool from_scp) = NULL;
CTAB *sim_vm_cmd = NULL;

/* External routines */

extern t_stat ttinit (void);
extern t_stat ttrunstate (void);
extern t_stat ttcmdstate (void);
extern t_stat ttclose (void);
extern t_stat sim_os_poll_kbd (void);
extern t_stat sim_os_putchar (int32 out);
extern uint32 sim_os_msec (void);
extern int sim_os_sleep (unsigned int sec);

/* Prototypes */

t_stat sim_brk_init (void);
t_stat sim_brk_set (t_addr loc, int32 sw, int32 ncnt);
t_stat sim_brk_clr (t_addr loc, int32 sw);
t_stat sim_brk_clrall (int32 sw);
t_stat sim_brk_show (FILE *st, t_addr loc, int32 sw);
t_stat sim_brk_showall (FILE *st, int32 sw);
void sim_brk_npc (void);
t_stat set_telnet (int32 flg, char *cptr);
t_stat set_notelnet (int32 flg, char *cptr);
t_stat set_logon (int32 flag, char *cptr);
t_stat set_logoff (int32 flag, char *cptr);
t_stat set_radix (DEVICE *dptr, UNIT *uptr, int32 flag);
t_stat set_devenbdis (DEVICE *dptr, UNIT *uptr, int32 flag);
t_stat set_onoff (DEVICE *dptr, UNIT *uptr, int32 flag);
t_stat ssh_break (FILE *st, char *cptr, int32 flg);
t_stat show_config (FILE *st, int32 flag, char *cptr);
t_stat show_queue (FILE *st, int32 flag, char *cptr);
t_stat show_time (FILE *st, int32 flag, char *cptr);
t_stat show_mod_names (FILE *st, int32 flag, char *cptr);
t_stat show_log (FILE *st, int32 flag, char *cptr);
t_stat show_telnet (FILE *st, int32 flag, char *cptr);
t_stat show_version (FILE *st, int32 flag, char *cptr);
t_stat show_break (FILE *st, int32 flag, char *cptr);
t_stat show_device (FILE *st, DEVICE *dptr, int32 flag);
t_stat show_unit (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag);
t_stat show_all_mods (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flg);
t_stat show_one_mod (FILE *st, DEVICE *dptr, UNIT *uptr, MTAB *mptr, int32 flag);
t_stat sim_check_console (int32 sec);
int32 get_switches (char *cptr);
t_value get_rval (REG *rptr, int32 idx);
void put_rval (REG *rptr, int32 idx, t_value val);
t_stat get_aval (t_addr addr, DEVICE *dptr, UNIT *uptr);
t_value strtotv (char *inptr, char **endptr, int32 radix);
void fprint_stopped (FILE *stream, t_stat r);
char *read_line (char *ptr, int32 size, FILE *stream);
CTAB *find_ctab (CTAB *tab, char *gbuf);
CTAB *find_cmd (char *gbuf);
DEVICE *find_dev (char *ptr);
DEVICE *find_unit (char *ptr, UNIT **uptr);
REG *find_reg_glob (char *ptr, char **optr, DEVICE **gdptr);
t_bool restore_skip_val (FILE *rfile);
t_bool qdisable (DEVICE *dptr);
t_stat attach_err (UNIT *uptr, t_stat stat);
t_stat detach_all (int32 start_device, t_bool shutdown);
t_stat ex_reg (FILE *ofile, t_value val, int32 flag, REG *rptr, t_addr idx);
t_stat dep_reg (int32 flag, char *cptr, REG *rptr, t_addr idx);
t_stat ex_addr (FILE *ofile, int32 flag, t_addr addr, DEVICE *dptr, UNIT *uptr);
t_stat dep_addr (int32 flag, char *cptr, t_addr addr, DEVICE *dptr,
	UNIT *uptr, int32 dfltinc);
char *get_range (char *cptr, t_addr *lo, t_addr *hi, int32 rdx,
	t_addr max, char term);
SCHTAB *get_search (char *cptr, DEVICE *dptr, SCHTAB *schptr);
int32 test_search (t_value val, SCHTAB *schptr);
t_stat step_svc (UNIT *ptr);

t_stat reset_cmd (int32 flag, char *ptr);
t_stat exdep_cmd (int32 flag, char *ptr);
t_stat load_cmd (int32 flag, char *ptr);
t_stat run_cmd (int32 flag, char *ptr);
t_stat attach_cmd (int32 flag, char *ptr);
t_stat detach_cmd (int32 flag, char *ptr);
t_stat save_cmd (int32 flag, char *ptr);
t_stat restore_cmd (int32 flag, char *ptr);
t_stat exit_cmd (int32 flag, char *ptr);
t_stat set_cmd (int32 flag, char *ptr);
t_stat show_cmd (int32 flag, char *ptr);
t_stat brk_cmd (int32 flag, char *ptr);
t_stat do_cmd (int32 flag, char *ptr);
t_stat help_cmd (int32 flag, char *ptr);

/* Global data */

UNIT *sim_clock_queue = NULL;
int32 sim_interval = 0;
int32 sim_switches = 0;
int32 sim_is_running = 0;
uint32 sim_brk_summ = 0;
uint32 sim_brk_types = 0;
uint32 sim_brk_dflt = 0;
BRKTAB *sim_brk_tab = NULL;
int32 sim_brk_ent = 0;
int32 sim_brk_lnt = 0;
int32 sim_brk_ins = 0;
t_bool sim_brk_pend = FALSE;
t_addr sim_brk_ploc = 0;
int32 sim_quiet = 0;
static double sim_time;
static uint32 sim_rtime;
static int32 noqueue_time;
volatile int32 stop_cpu = 0;
t_value *sim_eval = NULL;
int32 sim_end = 1;					/* 1 = little */
FILE *sim_log = NULL;					/* log file */
unsigned char sim_flip[FLIP_SIZE];

TMLN sim_con_ldsc = { 0 };				/* console line descr */
TMXR sim_con_tmxr = { 1, 0, 0, &sim_con_ldsc };		/* console line mux */
UNIT step_unit = { UDATA (&step_svc, 0, 0)  };

/* Tables and strings */

const char save_vercur[] = "V2.10";
const char save_ver26[] = "V2.6";
const char save_ver25[] = "V2.5";
const char *scp_error_messages[] = {
	"Address space exceeded",
	"Unit not attached",
	"I/O error",
	"Checksum error",
	"Format error",
	"Unit not attachable",
	"File open error",
	"Memory exhausted",
	"Invalid argument",
	"Step expired",
	"Unknown command",
	"Read only argument",
	"Command not completed",
	"Simulation stopped",
	"Goodbye",
	"Console input I/O error",
	"Console output I/O error",
	"End of file",
	"Relocation error",
	"No settable parameters",
	"Unit already attached",
	"Hardware timer error",
	"SIGINT handler setup error",
	"Console terminal setup error",
	"Subscript out of range",
	"Command not allowed",
	"Unit disabled",
	"Read only operation not allowed",
	"Invalid switch",
	"Missing value",
	"Too few arguments",
	"Too many arguments",
	"Non-existent device",
	"Non-existent unit",
	"Non-existent register",
	"Non-existent parameter",
	"Nested DO commands",
	"Internal error",
	"Invalid magtape record length",
	"Console Telnet connection lost",
	"Console Telnet connection timed out"
};

const size_t size_map[] = { sizeof (int8),
	sizeof (int8), sizeof (int16), sizeof (int32), sizeof (int32)
#if defined (t_int64)
	, sizeof (t_int64), sizeof (t_int64), sizeof (t_int64), sizeof (t_int64)
#endif
};

const t_value width_mask[] = { 0,
	0x1, 0x3, 0x7, 0xF,
	0x1F, 0x3F, 0x7F, 0xFF,
	0x1FF, 0x3FF, 0x7FF, 0xFFF,
	0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF,
	0x1FFFF, 0x3FFFF, 0x7FFFF, 0xFFFFF,
	0x1FFFFF, 0x3FFFFF, 0x7FFFFF, 0xFFFFFF,
	0x1FFFFFF, 0x3FFFFFF, 0x7FFFFFF, 0xFFFFFFF,
	0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF
#if defined (t_int64)
	, 0x1FFFFFFFF, 0x3FFFFFFFF, 0x7FFFFFFFF, 0xFFFFFFFFF,
	0x1FFFFFFFFF, 0x3FFFFFFFFF, 0x7FFFFFFFFF, 0xFFFFFFFFFF,
	0x1FFFFFFFFFF, 0x3FFFFFFFFFF, 0x7FFFFFFFFFF, 0xFFFFFFFFFFF,
	0x1FFFFFFFFFFF, 0x3FFFFFFFFFFF, 0x7FFFFFFFFFFF, 0xFFFFFFFFFFFF,
	0x1FFFFFFFFFFFF, 0x3FFFFFFFFFFFF, 0x7FFFFFFFFFFFF, 0xFFFFFFFFFFFFF,
	0x1FFFFFFFFFFFFF, 0x3FFFFFFFFFFFFF, 0x7FFFFFFFFFFFFF, 0xFFFFFFFFFFFFFF,
	0x1FFFFFFFFFFFFFF, 0x3FFFFFFFFFFFFFF,
		0x7FFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFF,
	0x1FFFFFFFFFFFFFFF, 0x3FFFFFFFFFFFFFFF,
		 0x7FFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF
#endif
};

static CTAB cmd_table[] = {
	{ "RESET", &reset_cmd, 0,
	  "r{eset} {ALL|<device>}   reset simulator\n" },
	{ "EXAMINE", &exdep_cmd, EX_E,
	  "e{xamine} <list>         examine memory or registers\n" },
	{ "IEXAMINE", &exdep_cmd, EX_E+EX_I,
	  "ie{xamine} <list>        interactive examine memory or registers\n" },
	{ "DEPOSIT", &exdep_cmd, EX_D,
	  "d{eposit} <list> <val>   deposit in memory or registers\n" },
	{ "IDEPOSIT", &exdep_cmd, EX_D+EX_I,
	  "id{eposit} <list>        interactive deposit in memory or registers\n" },
	{ "RUN", &run_cmd, RU_RUN,
	  "ru{n} {new PC}           reset and start simulation\n" },
	{ "GO", &run_cmd, RU_GO,
	  "go {new PC}              start simulation\n" }, 
	{ "STEP", &run_cmd, RU_STEP,
	  "s{tep} {n}               simulate n instructions\n" },
	{ "CONT", &run_cmd, RU_CONT,
	  "c{ont}                   continue simulation\n" },
	{ "BOOT", &run_cmd, RU_BOOT,
	  "b{oot} <unit>            bootstrap unit\n" },
	{ "BREAK", &brk_cmd, SSH_ST,
	  "br{eak} <list>           set breakpoints\n" },
	{ "NOBREAK", &brk_cmd, SSH_CL,
	  "nobr{eak} <list>         clear breakpoints\n" },
	{ "ATTACH", &attach_cmd, 0,
	  "at{tach} <unit> <file>   attach file to simulated unit\n" },
	{ "DETACH", &detach_cmd, 0,
	  "det{ach} <unit>          detach file from simulated unit\n" },
	{ "SAVE", &save_cmd, 0,
	  "sa{ve} <file>            save simulator to file\n" },
	{ "RESTORE", &restore_cmd, 0,
	  "rest{ore}|ge{t} <file>   restore simulator from file\n" },
	{ "GET", &restore_cmd, 0, NULL },
	{ "LOAD", &load_cmd, 0,
	  "l{oad} <file> {<args>}   load binary file\n" },
	{ "DUMP", &load_cmd, 1,
	  "du(mp) <file> {<args>}   dump binary file\n" },
	{ "EXIT", &exit_cmd, 0,
	  "exi{t}|q{uit}|by{e}      exit from simulation\n" },
	{ "QUIT", &exit_cmd, 0, NULL },
	{ "BYE", &exit_cmd, 0, NULL },
	{ "SET", &set_cmd, 0,
	  "set log <file>           enable logging to file\n"
	  "set nolog                disable logging\n"
	  "set telnet <port>        enable Telnet port for console\n"
	  "set notelnet             disable Telnet for console\n"
	  "set <dev> OCT|DEC|HEX    set device display radix\n"
	  "set <dev> ENABLED        enable device\n"
	  "set <dev> DISABLED       disable device\n"
	  "set <unit> ONLINE        enable unit\n"
	  "set <unit> OFFLINE       disable unit\n"
	  "set <dev>|<unit> <prm>   set device/unit parameter\n"
	  },
	{ "SHOW", &show_cmd, 0,
	  "sh{ow} br{eak} <list>    show breakpoints on address list\n"
	  "sh{ow} c{onfiguration}   show configuration\n"  
	  "sh{ow} d{evices}         show devices\n"  
	  "sh{ow} l{og}             show log\n"  
	  "sh{ow} m{odifiers}       show modifiers\n"  
	  "sh{ow} q{ueue}           show event queue\n"  
	  "sh{ow} te{lnet}          show console Telnet status\n"  
	  "sh{ow} ti{me}            show simulated time\n"  
	  "sh{ow} ve{rsion}         show simulator version\n"  
	  "sh{ow} <dev>|<unit>      show device parameters\n"  },
	{ "DO", &do_cmd, 0,
	  "do <file> {arg,arg...}   process command file\n" },
	{ "HELP", &help_cmd, 0,
	  "h{elp}                   type this message\n"
	  "h{elp} <command>         type help for command\n" },
	{ NULL, NULL, 0 }  };

/* Main command loop */

int main (int argc, char *argv[])
{
char cbuf[CBUFSIZE], gbuf[CBUFSIZE], *cptr;
int32 i, sw;
t_bool lookswitch;
t_stat stat;
CTAB *cmdp;
union {int32 i; char c[sizeof (int32)]; } end_test;

#if defined (__MWERKS__) && defined (macintosh)
argc = ccommand (&argv);
#endif

*cbuf = 0;						/* init arg buffer */
sim_switches = 0;					/* init switches */
lookswitch = TRUE;
for (i = 1; i < argc; i++) {				/* loop thru args */
	if (argv[i] == NULL) continue;			/* paranoia */
	if ((*argv[i] == '-') && lookswitch) {		/* switch? */
		if ((sw = get_switches (argv[i])) < 0) {
		    printf ("Invalid switch %s\n", argv[i]);
		    return 0;  }
		sim_switches = sim_switches | sw;  }
	else {	if ((strlen (argv[i]) + strlen (cbuf) + 1) >= CBUFSIZE) {
		    printf ("Argument string too long\n");
		    return 0;  }
		if (*cbuf) strcat (cbuf, " ");		/* concat args */
		strcat (cbuf, argv[i]);
		lookswitch = FALSE;  }			/* no more switches */
	}						/* end for */
sim_quiet = sim_switches & SWMASK ('Q');		/* -q means quiet */

if (sim_vm_init != NULL) (*sim_vm_init)();		/* call once only */
end_test.i = 1;						/* test endian-ness */
sim_end = end_test.c[0];
stop_cpu = 0;
sim_interval = 0;
sim_time = sim_rtime = 0;
noqueue_time = 0;
sim_clock_queue = NULL;
sim_is_running = 0;
sim_log = NULL;
if (sim_emax <= 0) sim_emax = 1;

if ((stat = ttinit ()) != SCPE_OK) {
	printf ("Fatal terminal initialization error\n%s\n",
		scp_error_messages[stat - SCPE_BASE]);
	return 0;  }
if ((sim_eval = calloc (sim_emax, sizeof (t_value))) == NULL) {
	printf ("Unable to allocate examine buffer\n");
	return 0;  };
if ((stat = reset_all (0)) != SCPE_OK) {
	printf ("Fatal simulator initialization error\n%s\n",
		scp_error_messages[stat - SCPE_BASE]);
	return 0;  }
if ((stat = sim_brk_init ()) != SCPE_OK) {
	printf ("Fatal breakpoint table initialization error\n%s\n",
		scp_error_messages[stat - SCPE_BASE]);
	return 0;  }
if (!sim_quiet) {
	printf ("\n");
	show_version (stdout, 0, NULL);  }

if (*cbuf) {						/* cmd file arg? */
	stat = do_cmd (1, cbuf);			/* proc cmd file */
	if (stat == SCPE_OPENERR)			/* error? */
		 fprintf (stderr, "Can't open file %s\n", cbuf);  }
else stat = SCPE_OK;

while (stat != SCPE_EXIT) {				/* in case exit */
	printf ("sim> ");				/* prompt */
	if (sim_vm_read != NULL)			/* sim routine? */
		cptr = (*sim_vm_read) (cbuf, CBUFSIZE, stdin);
	else cptr = read_line (cbuf, CBUFSIZE, stdin);	/* read command line */
	if (cptr == NULL) continue;			/* ignore EOF */
	if (*cptr == 0) continue;			/* ignore blank */
	if (sim_log) fprintf (sim_log, "sim> %s\n", cbuf); /* log cmd */
	cptr = get_glyph (cptr, gbuf, 0);		/* get command glyph */
	if (cmdp = find_cmd (gbuf))			/* lookup command */
		stat = cmdp->action (cmdp->arg, cptr);	/* if found, exec */
	else stat = SCPE_UNK;
	if (stat >= SCPE_BASE) {			/* error? */
		printf ("%s\n", scp_error_messages[stat - SCPE_BASE]);
		if (sim_log) fprintf (sim_log, "%s\n",
			scp_error_messages[stat - SCPE_BASE]);  }
	if (sim_vm_post != NULL) (*sim_vm_post) (TRUE);
	}						/* end while */

detach_all (0, TRUE);					/* close files */
set_logoff (0, NULL);					/* close log */
set_notelnet (0, NULL);					/* close Telnet */
ttclose ();						/* close console */
return 0;
}

/* Find command routine */

CTAB *find_cmd (char *gbuf)
{
CTAB *cmdp = NULL;

if (sim_vm_cmd) cmdp = find_ctab (sim_vm_cmd, gbuf);	/* try ext commands */
if (cmdp == NULL) cmdp = find_ctab (cmd_table, gbuf);	/* try regular cmds */
return cmdp;
}

/* Exit command */

t_stat exit_cmd (int32 flag, char *cptr)
{
return SCPE_EXIT;
}

/* Help command */

void fprint_help (FILE *st)
{
CTAB *cmdp;

for (cmdp = cmd_table; cmdp && (cmdp->name != NULL); cmdp++) {
	if (cmdp->help) fprintf (st, cmdp->help);  }
for (cmdp = sim_vm_cmd; cmdp && (cmdp->name != NULL); cmdp++) {
	if (cmdp->help) fprintf (st, cmdp->help);  }
return;
}

t_stat help_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
CTAB *cmdp;

if (*cptr) {
	cptr = get_glyph (cptr, gbuf, 0);
	if (*cptr) return SCPE_2MARG;
	if (cmdp = find_cmd (gbuf)) {
		printf (cmdp->help);
		if (sim_log) fprintf (sim_log, cmdp->help);  }
	else return SCPE_ARG;  }
else {	fprint_help (stdout);
	if (sim_log) fprint_help (sim_log);  }
return SCPE_OK;
}

/* Do command */

/* Substitute_args - replace %n tokens in 'instr' with the do command's arguments

   Calling sequence
   instr	=	input string
   tmpbuf	=	temp buffer
   maxstr	=	min (len (instr), len (tmpbuf))
   nargs	=	number of arguments
   do_arg[10]	=	arguments
*/

void sub_args (char *instr, char *tmpbuf, int32 maxstr, int32 nargs, char *do_arg[])
{
char *ip, *op, *ap, *oend = tmpbuf + maxstr - 2;

for (ip = instr, op = tmpbuf; *ip && (op < oend); ) {
	if ((*ip == '\\') && (ip[1] == '%')) {		/* \% = literal % */
	    ip++;					/* skip \ */
	    if (*ip) *op++ = *ip++;  }			/* copy next */
	else if ((*ip == '%') &&			/* %n = sub */
		 ((ip[1] >= '1') && (ip[1] <= ('0'+ nargs - 1)))) {
	    ap = do_arg[ip[1] - '0'];
	    ip = ip + 2;
	    while (*ap && (op < oend)) *op++ = *ap++;  }/* copy the argument */
	else *op++ = *ip++;  }				/* literal character */
*op = 0;						/* term buffer */
strcpy (instr, tmpbuf);
return;
}

t_stat do_cmd (int flag, char *fcptr)
{
char *cptr, cbuf[CBUFSIZE], gbuf[CBUFSIZE], *c, quote, *do_arg[10];
FILE *fpin;
CTAB *cmdp;
int32 echo, nargs;
t_stat stat = SCPE_OK;

if (flag == 0) { GET_SWITCHES (fcptr, gbuf); }		/* get switches */
echo = sim_switches & SWMASK ('V');			/* -v means echo */

c = fcptr;
for (nargs = 0; nargs < 10; ) {				/* extract arguments */
	while (*c && (*c <= ' ')) c++;			/* skip blanks */
	if (! *c) break;				/* all done */
	do_arg[nargs++] = c;				/* save start */
	while (*c && (*c > ' ')) {
	    if (*c == '\'' || *c == '"') {		/* quoted string */
		for (quote = *c++; *c; )
		   if (*c++ == quote) break;  }
	     else c++;  }
	if (*c) *c++ = 0;				/* term arg at spc */
	}						/* end for */
if (nargs <= 0) return SCPE_2FARG;			/* need at least 1 */
if ((fpin = FOPEN (do_arg[0], "r")) == NULL)		/* cmd file failed to open? */
	return SCPE_OPENERR;

do {	cptr = read_line (cbuf, CBUFSIZE, fpin);	/* get cmd line */
	sub_args (cbuf, gbuf, CBUFSIZE, nargs, do_arg);
	if (cptr == NULL) break;			/* exit on eof */
	if (*cptr == 0) continue;			/* ignore blank */
	if (echo) printf("do> %s\n", cptr);		/* echo if -v */
	if (sim_log) fprintf (sim_log, "do> %s\n", cptr);
	cptr = get_glyph (cptr, gbuf, 0);		/* get command glyph */
	if (strcmp (gbuf, "do") == 0) {			/* don't recurse */
	    fclose (fpin);
	    return SCPE_NEST;  }
	if (cmdp = find_cmd (gbuf))			/* lookup command */
	    stat = cmdp->action (cmdp->arg, cptr);	/* if found, exec */
	else stat = SCPE_UNK;
	if (stat >= SCPE_BASE)				/* error? */
	    printf ("%s\n", scp_error_messages[stat - SCPE_BASE]);
	if (sim_vm_post != NULL) (*sim_vm_post) (TRUE);
} while (stat != SCPE_EXIT);

fclose (fpin);						/* close file */
return (stat == SCPE_EXIT)? SCPE_EXIT: SCPE_OK;
}

/* Set command */

t_stat set_cmd (int32 flag, char *cptr)
{
int32 lvl;
t_stat r;
char gbuf[CBUFSIZE], *cvptr;
DEVICE *dptr;
UNIT *uptr;
MTAB *mptr;
CTAB *ctbr, *glbr;
static CTAB set_glob_tab[] = {
	{ "TELNET", &set_telnet, 0 },
	{ "NOTELNET", &set_notelnet, 0 },
	{ "LOG", &set_logon, 0 },
	{ "NOLOG", &set_logoff, 0 },
	{ "BREAK", &brk_cmd, SSH_ST },
	{ NULL, NULL, 0 }  };
static CTAB set_dev_tab[] = {
	{ "OCTAL", &set_radix, 8 },
	{ "DECIMAL", &set_radix, 10 },
	{ "HEX", &set_radix, 16 },
	{ "ENABLED", &set_devenbdis, 1 },
	{ "DISABLED", &set_devenbdis, 0 },
	{ NULL, NULL, 0 }  };
static CTAB set_unit_tab[] = {
	{ "ONLINE", &set_onoff, 1 },
	{ "OFFLINE", &set_onoff, 0 },
	{ NULL, NULL, 0 }  };

GET_SWITCHES (cptr, gbuf);				/* get switches */
if (*cptr == 0) return SCPE_2FARG;			/* must be more */
cptr = get_glyph (cptr, gbuf, 0);			/* get glob/dev/unit */

if (dptr = find_dev (gbuf)) {				/* device match? */
    uptr = dptr->units;					/* first unit */
    ctbr = set_dev_tab;					/* global table */
    lvl = MTAB_VDV;  }					/* device match */
else if (dptr = find_unit (gbuf, &uptr)) {		/* unit match? */
    if (uptr == NULL) return SCPE_NXUN;			/* invalid unit */
    ctbr = set_unit_tab;				/* global table */
    lvl = MTAB_VUN;  }					/* unit match */
else if (glbr = find_ctab (set_glob_tab, gbuf))		/* global? */
    return glbr->action (glbr->arg, cptr);		/* do the rest */
else return SCPE_NXDEV;					/* no match */
if (*cptr == 0) return SCPE_2FARG;			/* must be more */

while (*cptr != 0) {					/* do all mods */
    cptr = get_glyph (cptr, gbuf, ',');			/* get modifier */
    if (cvptr = strchr (gbuf, '=')) *cvptr++ = 0;	/* = value? */
    for (mptr = dptr->modifiers; mptr && (mptr->mask != 0); mptr++) {
	if ((mptr->mstring) &&				/* match string */
	    (MATCH_CMD (gbuf, mptr->mstring) == 0)) {	/* matches option? */
	    if (mptr->mask & MTAB_XTD) {		/* extended? */
		if ((lvl & mptr->mask) == 0) return SCPE_ARG;
		if ((lvl & MTAB_VUN) && (uptr->flags & UNIT_DIS))
		    return SCPE_UDIS;			/* unit disabled? */
		if (mptr->valid) {			/* validation rtn? */
		    r = mptr->valid (uptr, mptr->match, cvptr, mptr->desc);
		    if (r != SCPE_OK) return r;  }
		else if (!mptr->desc) break;		/* value desc? */
	        else if (mptr->mask & MTAB_VAL) {	/* take a value? */
		    if (!cvptr) return SCPE_MISVAL;	/* none? error */
		    r = dep_reg (0, cvptr, (REG *) mptr->desc, 0);
		    if (r != SCPE_OK) return r;  }
		else if (cvptr) return SCPE_ARG;	/* = value? */
		else *((int32 *) mptr->desc) = mptr->match;
		}					/* end if xtd */
	    else {					/* old style */
		if (cvptr) return SCPE_ARG;		/* = value? */
		if (uptr->flags & UNIT_DIS)		/* disabled? */
		     return SCPE_UDIS;
		if ((mptr->valid) && ((r = mptr->valid
		    (uptr, mptr->match, cvptr, mptr->desc))
		    != SCPE_OK)) return r;		/* invalid? */
		uptr->flags = (uptr->flags & ~(mptr->mask)) |
		    (mptr->match & mptr->mask);		/* set new value */
		}					/* end else xtd */
	    break;					/* terminate for */
	    }						/* end if match */
	}						/* end for */
    if (!mptr || (mptr->mask == 0)) {			/* no match? */
        if (glbr = find_ctab (ctbr, gbuf)) {		/* global match? */
	    r = glbr->action (dptr, uptr, glbr->arg);	/* do global */
	    if (r != SCPE_OK) return r;  }
	else if (!dptr->modifiers) return SCPE_NOPARAM;	/* no modifiers? */
	else return SCPE_NXPAR;  }			/* end if no mat */
    }							/* end while */
return SCPE_OK;						/* done all */
}

/* Match CTAB name */

CTAB *find_ctab (CTAB *tab, char *gbuf)
{
for (; tab->name != NULL; tab++) {
    if (MATCH_CMD (gbuf, tab->name) == 0) return tab;  }
return NULL;
}

/* Log on routine */

t_stat set_logon (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];

if (*cptr == 0) return SCPE_2FARG;			/* need arg */
cptr = get_glyph_nc (cptr, gbuf, 0);			/* get file name */
if (*cptr != 0) return SCPE_2MARG;			/* now eol? */
set_logoff (0, NULL);					/* close cur log */
sim_log = FOPEN (gbuf, "a");				/* open log */
if (sim_log == NULL) return SCPE_OPENERR;		/* error? */
if (!sim_quiet) printf ("Logging to file \"%s\"\n", gbuf);
fprintf (sim_log, "Logging to file \"%s\"\n", gbuf);	/* start of log */
return SCPE_OK;
}

/* Log off routine */

t_stat set_logoff (int32 flag, char *cptr)
{
if (cptr && (*cptr != 0)) return SCPE_2MARG;		/* now eol? */
if (sim_log == NULL) return SCPE_OK;			/* no log? */
if (!sim_quiet) printf ("Log file closed\n");
fprintf (sim_log, "Log file closed\n");			/* close log */
fclose (sim_log);
sim_log = NULL;
return SCPE_OK;
}

/* Set controller data radix routine */

t_stat set_radix (DEVICE *dptr, UNIT *uptr, int32 flag)
{
dptr->dradix = flag & 037;
return SCPE_OK;
}

/* Set controller enabled/disabled routine */

t_stat set_devenbdis (DEVICE *dptr, UNIT *uptr, int32 flag)
{
UNIT *up;
int32 i;

if ((dptr->flags & DEV_DISABLE) == 0) return SCPE_NOFNC;/* allowed? */
if (flag) {						/* enable? */
    if ((dptr->flags & DEV_DIS) == 0) return SCPE_OK;	/* already enb? ok */
    dptr->flags = dptr->flags & ~DEV_DIS;  }		/* no, enable */
else {							/* disable */
    if (dptr->flags & DEV_DIS) return SCPE_OK;		/* already dsb? ok */
    for (i = 0; i < dptr->numunits; i++) {		/* check units */
	up = (dptr->units) + i;				/* att or active? */
	if ((up->flags & UNIT_ATT) || sim_is_active (up))
	    return SCPE_NOFNC;  }			/* can't do it */
    dptr->flags = dptr->flags | DEV_DIS;  }		/* disable */
if (dptr->reset) return dptr->reset (dptr);		/* reset device */
else return SCPE_OK;
}

/* Set unit online/offline routine */

t_stat set_onoff (DEVICE *dptr, UNIT *uptr, int32 flag)
{
if (!(uptr->flags & UNIT_DISABLE)) return SCPE_NOFNC;	/* allowed? */
if (flag) uptr->flags = uptr->flags & ~UNIT_DIS;	/* onl? enable */
else {							/* offline? */
    if ((uptr->flags & UNIT_ATT) || sim_is_active (uptr))
	return SCPE_NOFNC;				/* more tests */
    uptr->flags = uptr->flags | UNIT_DIS;  }		/* disable */
return SCPE_OK;
}

/* Show command */

t_stat show_cmd (int32 flag, char *cptr)
{
int32 i, lvl;
t_stat r;
char gbuf[CBUFSIZE];
DEVICE *dptr;
UNIT *uptr;
MTAB *mptr;

static CTAB show_table[] = {
	{ "CONFIGURATION", &show_config, 0 },
	{ "DEVICES", &show_config, 1 },
	{ "QUEUE", &show_queue, 0 },
	{ "TIME", &show_time, 0 },
	{ "MODIFIERS", &show_mod_names, 0 },
	{ "VERSION", &show_version, 0 },
	{ "LOG", &show_log, 0 },
	{ "TELNET", &show_telnet, 0 },
	{ "BREAK", &show_break, 0 },
	{ NULL, NULL, 0 }  };

GET_SWITCHES (cptr, gbuf);				/* test for switches */
if (*cptr == 0) return SCPE_2FARG;			/* must be more */
cptr = get_glyph (cptr, gbuf, 0);			/* get next glyph */
for (i = 0; show_table[i].name != NULL; i++) {		/* find command */
    if (MATCH_CMD (gbuf, show_table[i].name) == 0)  {
	r = show_table[i].action (stdout, show_table[i].arg, cptr);
	if (sim_log) show_table[i].action (sim_log, show_table[i].arg, cptr);
	return r;  }  }

if (dptr = find_dev (gbuf)) {				/* device match? */
    uptr = dptr->units;					/* first unit */
    lvl = MTAB_VDV;  }					/* device match */
else if (dptr = find_unit (gbuf, &uptr)) {		/* unit match? */
    if (uptr == NULL) return SCPE_NXUN;			/* invalid unit */
    if (uptr->flags & UNIT_DIS) return SCPE_UDIS;	/* disabled? */
    lvl = MTAB_VUN;  }					/* unit match */
else return SCPE_NXDEV;					/* no match */

if (*cptr == 0) {					/* now eol? */
    if (lvl == MTAB_VDV) {				/* show dev? */
	r = show_device (stdout, dptr, 0);
	if (sim_log) show_device (sim_log, dptr, 0);
	return r;  }
    else {
	r = show_unit (stdout, dptr, uptr, -1);
	if (sim_log) show_unit (sim_log, dptr, uptr, -1);
	return r;  }  }
if (dptr->modifiers == NULL) return SCPE_NOPARAM;	/* any modifiers? */

while (*cptr != 0) {					/* do all mods */
    cptr = get_glyph (cptr, gbuf, ',');			/* get modifier */
    for (mptr = dptr->modifiers; mptr->mask != 0; mptr++) {
	if (((mptr->mask & MTAB_XTD)?			/* right level? */
	    (mptr->mask & lvl): (MTAB_VUN & lvl)) && 
	    ((mptr->disp && mptr->pstring &&		/* named disp? */
		(MATCH_CMD (gbuf, mptr->pstring) == 0)) ||
	    ((mptr->mask & MTAB_VAL) &&			/* named value? */
		mptr->mstring &&
		(MATCH_CMD (gbuf, mptr->mstring) == 0)))) {
	    show_one_mod (stdout, dptr, uptr, mptr, 1);
	    if (sim_log) show_one_mod (sim_log, dptr, uptr, mptr, 1);
	    break;
	    }						/* end if */
	}						/* end for */
    if (mptr->mask == 0) return SCPE_ARG;		/* any match? */
    }							/* end while */
return SCPE_OK;
}

/* Show device and unit */

t_stat show_device (FILE *st, DEVICE *dptr, int32 flag)
{
int32 j, ucnt;
UNIT *uptr;

fprintf (st, "%s", dptr->name);				/* print dev name */
if (qdisable (dptr)) {					/* disabled? */
	fprintf (st, ", disabled\n");
	return SCPE_OK;  }
for (j = ucnt = 0; j < dptr->numunits; j++) {		/* count units */
	uptr = (dptr->units) + j;
	if (!(uptr->flags & UNIT_DIS)) ucnt++;  }
show_all_mods (st, dptr, dptr->units, MTAB_VDV);	/* show dev mods */
if (dptr->numunits == 0) fprintf (st, "\n");
else {	if (ucnt == 0) fprintf (st, ", all units disabled\n");
	else if (ucnt > 1) fprintf (st, ", %d units\n", ucnt);
	else if (flag) fprintf (st, "\n");  }
if (flag) return SCPE_OK;				/* dev only? */
for (j = 0; j < dptr->numunits; j++) {			/* loop thru units */
	uptr = (dptr->units) + j;
	if ((uptr->flags & UNIT_DIS) == 0)
		show_unit (st, dptr, uptr, ucnt);  }
return SCPE_OK;
}

t_stat show_unit (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag)
{
t_addr kval = (uptr->flags & UNIT_BINK)? 1024: 1000;
int32 u = uptr - dptr->units;

if (flag > 1) fprintf (st, "  %s%d", dptr->name, u);
else if (flag < 0) fprintf (st, "%s%d", dptr->name, u);
if (uptr->flags & UNIT_FIX) {
	if (uptr->capac < kval)
		fprintf (st, ", %d%s", uptr->capac,
		    ((dptr->dwidth / dptr->aincr) > 8)? "W": "B");
	else fprintf (st, ", %dK%s", uptr->capac / kval,
		    ((dptr->dwidth / dptr->aincr) > 8)? "W": "B");  }
if (uptr->flags & UNIT_ATT) {
	fprintf (st, ", attached to %s", uptr->filename);
	if (uptr->flags & UNIT_RO) fprintf (st, ", read only");  }
else if (uptr->flags & UNIT_ATTABLE)
	fprintf (st, ", not attached");
show_all_mods (st, dptr, uptr, MTAB_VUN);		/* show unit mods */ 
fprintf (st, "\n");
return SCPE_OK;
}

/* Show <global name> processors  */

t_stat show_version (FILE *st, int32 flag, char *cptr)
{
int32 vmaj = SIM_MAJOR, vmin = SIM_MINOR, vpat = SIM_PATCH;

if (cptr && (*cptr != 0)) return SCPE_2MARG;
fprintf (st, "%s simulator V%d.%d-%d\n", sim_name, vmaj, vmin, vpat);
return SCPE_OK;
}

t_stat show_config (FILE *st, int32 flag, char *cptr)
{
int32 i;
DEVICE *dptr;

if (cptr && (*cptr != 0)) return SCPE_2MARG;
fprintf (st, "%s simulator configuration\n\n", sim_name);
for (i = 0; (dptr = sim_devices[i]) != NULL; i++)
	show_device (st, dptr, flag);
return SCPE_OK;
}

t_stat show_queue (FILE *st, int32 flag, char *cptr)
{
DEVICE *dptr;
UNIT *uptr;
int32 accum;

if (cptr && (*cptr != 0)) return SCPE_2MARG;
if (sim_clock_queue == NULL) {
	fprintf (st, "%s event queue empty, time = %-16.0f\n",
		sim_name, sim_time);
	return SCPE_OK;  }
fprintf (st, "%s event queue status, time = %-16.0f\n",
	 sim_name, sim_time);
accum = 0;
for (uptr = sim_clock_queue; uptr != NULL; uptr = uptr->next) {
	if (uptr == &step_unit) fprintf (st, "  Step timer");
	else if ((dptr = find_dev_from_unit (uptr)) != NULL) {
		fprintf (st, "  %s", dptr->name);
		if (dptr->numunits > 1) fprintf (st, " unit %d",
			uptr - dptr->units);  }
	else fprintf (st, "  Unknown");
	fprintf (st, " at %d\n", accum + uptr->time);
	accum = accum + uptr->time;  }
return SCPE_OK;
}

t_stat show_time (FILE *st, int32 flag, char *cptr)
{
if (cptr && (*cptr != 0)) return SCPE_2MARG;
fprintf (st, "Time:	%-16.0f\n", sim_time);
return SCPE_OK;
}

t_stat show_log (FILE *st, int32 flag, char *cptr)
{
if (cptr && (*cptr != 0)) return SCPE_2MARG;
if (sim_log) fprintf (st, "Logging enabled\n");
else fprintf (st, "Logging disabled\n");
return SCPE_OK;
}

t_stat show_break (FILE *st, int32 flag, char *cptr)
{
t_stat r;

if (cptr && (*cptr != 0)) {				/* more? */
	r = ssh_break (stdout, cptr, 1);
	if (sim_log) ssh_break (sim_log, cptr, SSH_SH);  }
else {	r = sim_brk_showall (stdout, sim_switches);
	if (sim_log) sim_brk_showall (sim_log,sim_switches);  }
return r;
}

/* Show modifiers */

t_stat show_mod_names (FILE *st, int32 flag, char *cptr)
{
int32 i, any, enb;
DEVICE *dptr;
MTAB *mptr;

if (cptr && (*cptr != 0)) return SCPE_2MARG;		/* now eol? */
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
    any = enb = 0;
    if (dptr->modifiers) {
	for (mptr = dptr->modifiers; mptr->mask != 0; mptr++) {
	    if (mptr->mstring) {
	        if (strcmp (mptr->mstring, "ENABLED") == 0) enb = 1;
		if (any++) fprintf (st, ", %s", mptr->mstring);
		else fprintf (st, "%s\t%s", dptr->name, mptr->mstring);  }  }  }
    if (!enb && (dptr->flags & DEV_DISABLE)) {
	if (any++) fprintf (st, ", ENABLED, DISABLED");
	else fprintf (st, "%s\tENABLED, DISABLED", dptr->name);  }
    if (any) fprintf (st, "\n");  }
return SCPE_OK;
}

t_stat show_all_mods (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag)
{
MTAB *mptr;

if (dptr->modifiers == NULL) return SCPE_OK;
for (mptr = dptr->modifiers; mptr->mask != 0; mptr++) {
    if (mptr->pstring && ((mptr->mask & MTAB_XTD)?
	((mptr->mask & flag) && !(mptr->mask & MTAB_NMO)): 
	((MTAB_VUN & flag) && ((uptr->flags & mptr->mask) == mptr->match)))) {
	fputs (", ", st);
	show_one_mod (st, dptr, uptr, mptr, 0);  }  }
return SCPE_OK;
}

t_stat show_one_mod (FILE *st, DEVICE *dptr, UNIT *uptr, MTAB *mptr, int32 flag)
{
t_value val;

if (mptr->disp) mptr->disp (st, uptr, mptr->match, mptr->desc);
else if ((mptr->mask & MTAB_XTD) && (mptr->mask & MTAB_VAL)) {
	REG *rptr = (REG *) mptr->desc;
	fprintf (st, "%s=", mptr->pstring);
	val = get_rval (rptr, 0);
	fprint_val (st, val, rptr->radix, rptr->width,
		rptr->flags & REG_FMT);  }
else fputs (mptr->pstring, st);
if (flag && !((mptr->mask & MTAB_XTD) &&
    (mptr->mask & MTAB_NMO))) fputc ('\n', st);
return SCPE_OK;
}

/* Breakpoint commands */

t_stat brk_cmd (int32 flg, char *cptr)
{
char gbuf[CBUFSIZE];

GET_SWITCHES (cptr, gbuf);				/* test for switches */
return ssh_break (NULL, cptr, flg);			/* call common code */
}

t_stat ssh_break (FILE *st, char *cptr, int32 flg)
{
char gbuf[CBUFSIZE], *tptr, *t1ptr;
DEVICE *dptr = sim_devices[0];
UNIT *uptr = dptr->units;
t_stat r;
t_addr lo, hi, max = uptr->capac - dptr->aincr;
int32 cnt;

if (*cptr == 0) return SCPE_2FARG;
if (sim_brk_types == 0) return SCPE_NOFNC;
if ((dptr == NULL) || (uptr == NULL)) return SCPE_IERR;
while (*cptr) {
    cptr = get_glyph (cptr, gbuf, ',');
    tptr = get_range (gbuf, &lo, &hi, dptr->aradix, max, 0);
    if (tptr == NULL) return SCPE_ARG;
    if (*tptr == '[') {
	errno = 0;
	cnt = strtoul (tptr + 1, &t1ptr, 10);
	if (errno || (tptr == t1ptr) || (*t1ptr != ']') ||
	   (flg != SSH_ST)) return SCPE_ARG;
	tptr = t1ptr + 1;  }
    else cnt = 0;
    if (*tptr != 0) return SCPE_ARG;
    if ((lo == 0) && (hi == max)) {
	if (flg == SSH_CL) sim_brk_clrall (sim_switches);
	else if (flg == SSH_SH) sim_brk_showall (st, sim_switches);
	else return SCPE_ARG;  }
    else {	
	for ( ; lo <= hi; lo = lo + dptr->aincr) {
	    if (flg == SSH_ST) r = sim_brk_set (lo, sim_switches, cnt);
	    else if (flg == SSH_CL) r = sim_brk_clr (lo, sim_switches);
	    else if (flg == SSH_SH) r = sim_brk_show (st, lo, sim_switches);
	    else return SCPE_ARG;
	    if (r != SCPE_OK) return r;
	    }
	}
    }
return SCPE_OK;
}

/* Reset command and routines

   re[set]		reset all devices
   re[set] all		reset all devices
   re[set] device	reset specific device
*/

t_stat reset_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
DEVICE *dptr;

GET_SWITCHES (cptr, gbuf);				/* test for switches */
if (*cptr == 0) return (reset_all (0));			/* reset(cr) */
cptr = get_glyph (cptr, gbuf, 0);			/* get next glyph */
if (*cptr != 0) return SCPE_2MARG;			/* now eol? */
if (strcmp (gbuf, "ALL") == 0) return (reset_all (0));
dptr = find_dev (gbuf);					/* locate device */
if (dptr == NULL) return SCPE_NXDEV;			/* found it? */
if (dptr->reset != NULL) return dptr->reset (dptr);
else return SCPE_OK;
}

/* Reset devices start..end

   Inputs:
	start	=	number of starting device
   Outputs:
	status	=	error status
*/

t_stat reset_all (int32 start)
{
DEVICE *dptr;
int32 i;
t_stat reason;

if (start < 0) return SCPE_IERR;
for (i = 0; i < start; i++) {
	if (sim_devices[i] == NULL) return SCPE_IERR;  }
for (i = start; (dptr = sim_devices[i]) != NULL; i++) {
	if (dptr->reset != NULL) {
		reason = dptr->reset (dptr);
		if (reason != SCPE_OK) return reason;  }  }
return SCPE_OK;
}

/* Load and dump commands

   lo[ad] filename {arg}	load specified file
   du[mp] filename {arg}	dump to specified file
*/

t_stat load_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
FILE *loadfile;
t_stat reason;

GET_SWITCHES (cptr, gbuf);				/* test for switches */
if (*cptr == 0) return SCPE_2FARG;			/* must be more */
cptr = get_glyph_nc (cptr, gbuf, 0);			/* get file name */
loadfile = FOPEN (gbuf, flag? "wb": "rb");		/* open for wr/rd */
if (loadfile == NULL) return SCPE_OPENERR;
reason = sim_load (loadfile, cptr, gbuf, flag);		/* load or dump */
fclose (loadfile);
return reason;
}

/* Attach command

   at[tach] unit file	attach specified unit to file
*/

t_stat attach_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
DEVICE *dptr;
UNIT *uptr;

GET_SWITCHES (cptr, gbuf);				/* test for switches */
if (*cptr == 0) return SCPE_2FARG;			/* must be more */
cptr = get_glyph (cptr, gbuf, 0);			/* get next glyph */
if (*cptr == 0) return SCPE_2FARG;			/* now eol? */
dptr = find_unit (gbuf, &uptr);				/* locate unit */
if (dptr == NULL) return SCPE_NXDEV;			/* found dev? */
if (uptr == NULL) return SCPE_NXUN;			/* valid unit? */
if (dptr->attach != NULL) return dptr->attach (uptr, cptr);
return attach_unit (uptr, cptr);
}

t_stat attach_unit (UNIT *uptr, char *cptr)
{
DEVICE *dptr;
t_stat reason;

if (uptr->flags & UNIT_DIS) return SCPE_UDIS;		/* disabled? */
if (!(uptr->flags & UNIT_ATTABLE)) return SCPE_NOATT;	/* not attachable? */
if ((dptr = find_dev_from_unit (uptr)) == NULL) return SCPE_NOATT;
if (uptr->flags & UNIT_ATT) {				/* already attached? */
    reason = detach_unit (uptr);
    if (reason != SCPE_OK) return reason;  }
uptr->filename = calloc (CBUFSIZE, sizeof (char));
if (uptr->filename == NULL) return SCPE_MEM;
strncpy (uptr->filename, cptr, CBUFSIZE);
if (sim_switches & SWMASK ('R')) {			/* read only? */
    if ((uptr->flags & UNIT_ROABLE) == 0)		/* allowed? */
	return attach_err (uptr, SCPE_NORO);		/* no, error */
    uptr->fileref = FOPEN (cptr, "rb");			/* open rd only */
    if (uptr->fileref == NULL)				/* open fail? */
	return attach_err (uptr, SCPE_OPENERR);		/* yes, error */
    uptr->flags = uptr->flags | UNIT_RO;		/* set rd only */
    if (!sim_quiet) printf ("%s: unit is read only\n", dptr->name);  }
else {							/* normal */
    uptr->fileref = FOPEN (cptr, "rb+");		/* open r/w */
    if (uptr->fileref == NULL) {			/* open fail? */
	if (errno == EROFS) {				/* read only? */
	    if ((uptr->flags & UNIT_ROABLE) == 0)	/* allowed? */
		return attach_err (uptr, SCPE_NORO);	/* no error */
	    uptr->fileref = FOPEN (cptr, "rb");		/* open rd only */
	    if (uptr->fileref == NULL)			/* open fail? */
		return attach_err (uptr, SCPE_OPENERR);	/* yes, error */
	    uptr->flags = uptr->flags | UNIT_RO;	/* set rd only */
	    if (!sim_quiet) printf ("%s: unit is read only\n", dptr->name);  }
	else {						/* doesn't exist */
	    if (sim_switches & SWMASK ('E'))		/* must exist? */
		return attach_err (uptr, SCPE_OPENERR);	/* yes, error */
	    uptr->fileref = FOPEN (cptr, "wb+");	/* open new file */
	    if (uptr->fileref == NULL)			/* open fail? */
		return attach_err (uptr, SCPE_OPENERR);	/* yes, error */
	    if (!sim_quiet) printf ("%s: creating new file\n", dptr->name);  }
	}						/* end if null */
    }							/* end else */
if (uptr->flags & UNIT_BUFABLE) {			/* buffer? */
    int32 cap = uptr->capac / dptr->aincr;		/* effective size */
    if (uptr->flags & UNIT_MUSTBUF)			/* dyn alloc? */
	uptr->filebuf = calloc (cap, SZ_D (dptr));	/* allocate */
    if (uptr->filebuf == NULL)				/* no buffer? */
	    return attach_err (uptr, SCPE_MEM);		/* error */
    if (!sim_quiet) printf ("%s: buffering file in memory\n", dptr->name);
    uptr->hwmark = fxread (uptr->filebuf, SZ_D (dptr),	/* read file */
	    cap, uptr->fileref);
    uptr->flags = uptr->flags | UNIT_BUF;  }		/* set buffered */
uptr->flags = uptr->flags | UNIT_ATT;
uptr->pos = 0;
return SCPE_OK;
}

t_stat attach_err (UNIT *uptr, t_stat stat)
{
free (uptr->filename);
uptr->filename = NULL;
return stat;
}

/* Detach command

   det[ach] all		detach all units
   det[ach] unit	detach specified unit
*/

t_stat detach_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
DEVICE *dptr;
UNIT *uptr;

GET_SWITCHES (cptr, gbuf);				/* test for switches */
if (*cptr == 0) return SCPE_2FARG;			/* must be more */
cptr = get_glyph (cptr, gbuf, 0);			/* get next glyph */
if (*cptr != 0) return SCPE_2MARG;			/* now eol? */
if (strcmp (gbuf, "ALL") == 0) return (detach_all (0, FALSE));
dptr = find_unit (gbuf, &uptr);				/* locate unit */
if (dptr == NULL) return SCPE_NXDEV;			/* found dev? */
if (uptr == NULL) return SCPE_NXUN;			/* valid unit? */
if (!(uptr->flags & UNIT_ATTABLE)) return SCPE_NOATT;
if (dptr->detach != NULL) return dptr->detach (uptr);
return detach_unit (uptr);
}

/* Detach devices start..end

   Inputs:
	start	=	number of starting device
	shutdown =	TRUE if simulator shutting down
   Outputs:
	status	=	error status
*/

t_stat detach_all (int32 start, t_bool shutdown)
{
int32 i, j;
t_stat reason;
DEVICE *dptr;
UNIT *uptr;

if ((start < 0) || (start > 1)) return SCPE_IERR;
for (i = start; (dptr = sim_devices[i]) != NULL; i++) {
    for (j = 0; j < dptr->numunits; j++) {
	uptr = (dptr->units) + j;
	if ((uptr->flags & UNIT_ATTABLE) || shutdown) {
	    if (dptr->detach != NULL) reason = dptr->detach (uptr);
	    else reason = detach_unit (uptr);
	    if (reason != SCPE_OK) return reason;  }  }  }
return SCPE_OK;
}

t_stat detach_unit (UNIT *uptr)
{
DEVICE *dptr;

if (uptr == NULL) return SCPE_IERR;
if (!(uptr->flags & UNIT_ATT)) return SCPE_OK;
if ((dptr = find_dev_from_unit (uptr)) == NULL) return SCPE_OK;
if (uptr->flags & UNIT_BUF) {
    int32 cap = (uptr->hwmark + dptr->aincr - 1) / dptr->aincr;
    if (uptr->hwmark && ((uptr->flags & UNIT_RO) == 0)) {
	if (!sim_quiet) printf ("%s: writing buffer to file\n", dptr->name);
	rewind (uptr->fileref);
	fxwrite (uptr->filebuf, SZ_D (dptr), cap, uptr->fileref);
	if (ferror (uptr->fileref)) perror ("I/O error");  }
    if (uptr->flags & UNIT_MUSTBUF) {			/* dyn alloc? */
	free (uptr->filebuf);				/* free buf */
	uptr->filebuf = NULL;  }
    uptr->flags = uptr->flags & ~UNIT_BUF;  }
uptr->flags = uptr->flags & ~(UNIT_ATT | UNIT_RO);
free (uptr->filename);
uptr->filename = NULL;
return (fclose (uptr->fileref) == EOF)? SCPE_IOERR: SCPE_OK;
}

/* Save command

   sa[ve] filename		save state to specified file
*/

t_stat save_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
void *mbuf;
FILE *sfile;
int32 i, j, l, t;
t_addr k, high;
t_value val;
t_stat r;
t_bool zeroflg;
size_t sz;
DEVICE *dptr;
UNIT *uptr;
REG *rptr;

#define WRITE_I(xx) fxwrite (&(xx), sizeof (xx), 1, sfile)

GET_SWITCHES (cptr, gbuf);				/* test for switches */
if (*cptr == 0) return SCPE_2FARG;			/* must be more */
if ((sfile = FOPEN (cptr, "wb")) == NULL) return SCPE_OPENERR;
fputs (save_vercur, sfile);				/* [V2.5] save format */
fputc ('\n', sfile);
fputs (sim_name, sfile);				/* sim name */
fputc ('\n', sfile);
WRITE_I (sim_time);					/* sim time */
WRITE_I (sim_rtime);					/* [V2.6] sim rel time */

for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {	/* loop thru devices */
	fputs (dptr->name, sfile);			/* device name */
	fputc ('\n', sfile);
	WRITE_I (dptr->flags);				/* [V2.10] flags */
	for (j = 0; j < dptr->numunits; j++) {
	    uptr = (dptr->units) + j;
	    t = sim_is_active (uptr);
	    WRITE_I (j);				/* unit number */
	    WRITE_I (t);				/* activation time */
	    WRITE_I (uptr->u3);				/* unit specific */
	    WRITE_I (uptr->u4);
	    WRITE_I (uptr->flags);			/* [V2.10] flags */
	    if (uptr->flags & UNIT_ATT) fputs (uptr->filename, sfile);
	    fputc ('\n', sfile);
	    if (((uptr->flags & (UNIT_FIX + UNIT_ATTABLE)) == UNIT_FIX) &&
		 (dptr->examine != NULL) &&
		 ((high = uptr->capac) != 0)) {		/* memory-like unit? */
		WRITE_I (high);				/* [V2.5] write size */
		sz = SZ_D (dptr);
		if ((mbuf = calloc (SRBSIZ, sz)) == NULL) {
		    fclose (sfile);
		    return SCPE_MEM;  }
		for (k = 0; k < high; ) {		/* loop thru mem */
		    zeroflg = TRUE;
		    for (l = 0; (l < SRBSIZ) && (k < high); l++,
			 k = k + (dptr->aincr)) {	/* check for 0 block */
			r = dptr->examine (&val, k, uptr, 0);
			if (r != SCPE_OK) return r;
			if (val) zeroflg = FALSE;
			SZ_STORE (sz, val, mbuf, l);
			}				/* end for l */
		    if (zeroflg) {			/* all zero's? */
			l = -l;				/* invert block count */
			WRITE_I (l);  }			/* write only count */
		    else {
			WRITE_I (l);			/* block count */
			fxwrite (mbuf, l, sz, sfile);  }
		     }					/* end for k */
		free (mbuf);				/* dealloc buffer */
		}					/* end if mem */
	    else {					/* no memory */
		high = 0;				/* write 0 */
		WRITE_I (high);
		}					/* end else mem */
	    }						/* end unit loop */
	j = -1;						/* end units */
	WRITE_I (j);					/* write marker */
	for (rptr = dptr->registers; (rptr != NULL) && 	/* loop thru regs */
	     (rptr->name != NULL); rptr++) {
	    fputs (rptr->name, sfile);			/* name */
	    fputc ('\n', sfile);
	    WRITE_I (rptr->depth);			/* [V2.10] depth */
	    for (j = 0; j < rptr->depth; j++) {		/* loop thru values */
		val = get_rval (rptr, j);		/* get value */
		WRITE_I (val);  }  }			/* store */
	fputc ('\n', sfile);  }				/* end registers */
fputc ('\n', sfile);					/* end devices */
r = (ferror (sfile))? SCPE_IOERR: SCPE_OK;		/* error during save? */
fclose (sfile);
return r;
}

/* Restore command

   re[store] filename		restore state from specified file
*/

t_stat restore_cmd (int32 flag, char *cptr)
{
char buf[CBUFSIZE];
void *mbuf;
FILE *rfile;
int32 i, j, blkcnt, limit, unitno, time, flg, depth;
t_addr k, high;
t_value val, mask;
t_stat r;
size_t sz;
t_bool v210 = FALSE, v26 = FALSE;
DEVICE *dptr;
UNIT *uptr;
REG *rptr;

#define READ_S(xx) if (read_line ((xx), CBUFSIZE, rfile) == NULL) \
	{ fclose (rfile); return SCPE_IOERR;  }
#define READ_I(xx) if (fxread (&xx, sizeof (xx), 1, rfile) == 0) \
	{ fclose (rfile); return SCPE_IOERR;  }

GET_SWITCHES (cptr, buf);				/* test for switches */
if (*cptr == 0) return SCPE_2FARG;			/* must be more */
if ((rfile = FOPEN (cptr, "rb")) == NULL) return SCPE_OPENERR;
READ_S (buf);						/* [V2.5+] read version */
if (strcmp (buf, save_vercur) == 0) v210 = v26 = TRUE;	/* version 2.10? */	
else if (strcmp (buf, save_ver26) == 0) v26 = TRUE;	/* version 2.6? */
else if (strcmp (buf, save_ver25) != 0) {		/* version 2.5? */
	printf ("Invalid file version: %s\n", buf);	/* no, unknown */
	fclose (rfile);
	return SCPE_INCOMP;  }
READ_S (buf);						/* read sim name */
if (strcmp (buf, sim_name)) {				/* name match? */
	printf ("Wrong system type: %s\n", buf);
	fclose (rfile);
	return SCPE_INCOMP;  }
READ_I (sim_time);					/* sim time */
if (v26) { READ_I (sim_rtime); }			/* [V2.6+] sim rel time */

for ( ;; ) {						/* device loop */
	READ_S (buf);					/* read device name */
	if (buf[0] == 0) break;				/* last? */
	if ((dptr = find_dev (buf)) == NULL) {		/* locate device */
	    printf ("Invalid device name: %s\n", buf);
	    fclose (rfile);
	    return SCPE_INCOMP;  }
	if (v210) {					/* [V2.10+] */
	    READ_I (flg);				/* cont flags */
	    dptr->flags = (dptr->flags & ~DEV_RFLAGS) |	/* restore */
	         (flg & DEV_RFLAGS);  }
	for ( ;; ) {					/* unit loop */
	    READ_I (unitno);				/* unit number */
	    if (unitno < 0) break;			/* end units? */
	    if (unitno >= dptr->numunits) {		/* too big? */
		printf ("Invalid unit number: %s%d\n", dptr->name, unitno);
		fclose (rfile);
		return SCPE_INCOMP;  }
	    READ_I (time);				/* event time */
	    uptr = (dptr->units) + unitno;
	    sim_cancel (uptr);
	    if (time > 0) sim_activate (uptr, time - 1);
	    READ_I (uptr->u3);				/* device specific */
	    READ_I (uptr->u4);
	    if (v210) {					/* [V2.10+] */
	        READ_I (flg);				/* unit flags */
		uptr->flags = (uptr->flags & ~UNIT_RFLAGS) |
		    (flg & UNIT_RFLAGS);  }		/* restore */ 
	    READ_S (buf);				/* attached file */
	    if (buf[0] != 0) {				/* any file? */
		uptr->flags = uptr->flags & ~UNIT_DIS;
		if (v210 && (flg & UNIT_RO))		/* saved flgs & RO? */
		    sim_switches = SWMASK ('R');	/* RO attach */
		else sim_switches = 0;			/* no, normal att */
		if (dptr->attach != NULL) r = dptr->attach (uptr, buf);
		else r = attach_unit (uptr, buf);
		if (r != SCPE_OK) return r;  }
	    READ_I (high);				/* memory capacity */
	    if (high > 0) {				/* [V2.5+] any memory? */
		if (((uptr->flags & (UNIT_FIX + UNIT_ATTABLE)) != UNIT_FIX) ||
		     (high > uptr->capac) || (dptr->deposit == NULL)) {
		    printf ("Invalid memory bound: %s%d = %u\n",
			dptr->name, unitno, high);
		    fclose (rfile);
		    return SCPE_INCOMP;  }
		sz = SZ_D (dptr);			/* allocate buffer */
		if ((mbuf = calloc (SRBSIZ, sz)) == NULL) {
		    fclose (rfile);
		    return SCPE_MEM;  }
		for (k = 0; k < high; ) {		/* loop thru mem */
		    READ_I (blkcnt);			/* block count */
		    if (blkcnt < 0) limit = -blkcnt;	/* compressed? */
		    else limit = fxread (mbuf, sz, blkcnt, rfile);
		    if (limit <= 0) {			/* invalid or err? */
			fclose (rfile);
			return SCPE_IOERR;  }					
		    for (j = 0; j < limit; j++, k = k + (dptr->aincr)) {
			if (blkcnt < 0) val = 0;	/* compressed? */
			else SZ_LOAD (sz, val, mbuf, j);/* saved value */
			r = dptr->deposit (val, k, uptr, 0);
			if (r != SCPE_OK) return r;
			}				/* end for j */
		    }					/* end for k */
		free (mbuf);				/* dealloc buffer */
		}					/* end if high */
	    }						/* end unit loop */
	for ( ;; ) {					/* register loop */
	    READ_S (buf);				/* read reg name */
	    if (buf[0] == 0) break;			/* last? */
	    if (v210) { READ_I (depth); }		/* [V2.10+] depth */
	    if ((rptr = find_reg (buf, NULL, dptr)) == NULL) {
		printf ("Invalid register name: %s %s\n", dptr->name, buf);
		if (v210) {				/* [V2.10]+ */
		    for (i = 0; i < depth; i++) {	/* skip values */
			READ_I (val);  }  }
		else {
		    READ_I (val);			/* must be one val */
		    if (!restore_skip_val (rfile))	/* grope for next reg */
			return SCPE_INCOMP;  }		/* err or eof? */
		continue;  }				/* pray! */
	    if (v210 && (depth != rptr->depth))		/* [V2.10+] mismatch? */
		printf ("Register depth mismatch: %s %s, file = %d, sim = %d\n",
		    dptr->name, buf, depth, rptr->depth);
	    else depth = rptr->depth;			/* depth validated */
	    mask = width_mask[rptr->width];		/* get mask */
	    for (i = 0; i < depth; i++) {		/* loop thru values */
		READ_I (val);				/* read value */
		if (val > mask)				/* value ok? */
		    printf ("Invalid register value: %s %s\n", dptr->name, buf);
		else if (i < rptr->depth)		/* in range? */
		    put_rval (rptr, i, val);  }  }
	}						/* end device loop */
fclose (rfile);
return SCPE_OK;
}

/* Restore skip values (pre V2.10)

   To find the next register name, sift through bytes looking for valid
   upper case ASCII characters terminated by \n */

t_bool restore_skip_val (FILE *rfile)
{
t_bool instr = FALSE;
char c;
int32 ic, pos;

if ((ic = fgetc (rfile)) == EOF) return FALSE;		/* one char */
fseek (rfile, ftell (rfile) - 1, SEEK_SET);		/* back up over */
if ((ic & 0377) == '\n') return TRUE;			/* immediate nl? */
while ((ic = fgetc (rfile)) != EOF) {			/* get char */
	c = ic & 0377;					/* cut to 8b */
	if (isalnum (c) || (c == '*') || (c == '_')) {	/* valid reg char? */
	    if (!instr) pos = ftell (rfile) - 1;	/* new? save pos */
	    instr = TRUE;  }
	else {						/* not ASCII */
	    if ((c == '\n') && instr) {			/* nl & in string? */
		fseek (rfile, pos, SEEK_SET);		/* rewind file */
		return TRUE;  }
	    instr = FALSE;  }				/* not in string */
	}						/* end while */
return FALSE;						/* error */ 
}

/* Run, go, cont, step commands

   ru[n] [new PC]	reset and start simulation
   go [new PC]		start simulation
   co[nt]		start simulation
   s[tep] [step limit]	start simulation for 'limit' instructions
   b[oot] device	bootstrap from device and start simulation
*/

t_stat run_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
int32 i, j, step, unitno;
t_stat r;
DEVICE *dptr;
UNIT *uptr;
void int_handler (int signal);

GET_SWITCHES (cptr, gbuf);				/* test for switches */
step = 0;
if (((flag == RU_RUN) || (flag == RU_GO)) && (*cptr != 0)) {	/* run or go */
	cptr = get_glyph (cptr, gbuf, 0);		/* get next glyph */
	if ((r = dep_reg (0, gbuf, sim_PC, 0)) != SCPE_OK) return r;  }

if (flag == RU_STEP) {					/* step */
	if (*cptr == 0) step = 1;
	else {	cptr = get_glyph (cptr, gbuf, 0);
		step = (int32) get_uint (gbuf, 10, INT_MAX, &r);
		if ((r != SCPE_OK) || (step == 0)) return SCPE_ARG;  }  }

if (flag == RU_BOOT) {					/* boot */
	if (*cptr == 0) return SCPE_2FARG;		/* must be more */
	cptr = get_glyph (cptr, gbuf, 0);		/* get next glyph */
	dptr = find_unit (gbuf, &uptr);			/* locate unit */
	if (dptr == NULL) return SCPE_NXDEV;		/* found dev? */
	if (uptr == NULL) return SCPE_NXUN;		/* valid unit? */
	if (dptr->boot == NULL) return SCPE_NOFNC;	/* can it boot? */
	if (uptr->flags & UNIT_DIS) return SCPE_UDIS;	/* disabled? */
	if ((uptr->flags & UNIT_ATTABLE) &&
	    !(uptr->flags & UNIT_ATT)) return SCPE_UNATT;
	unitno = uptr - dptr->units;			/* recover unit# */
	if ((r = dptr->boot (unitno, dptr)) != SCPE_OK) return r;  }

if (*cptr != 0) return SCPE_2MARG;			/* now eol? */

if ((flag == RU_RUN) || (flag == RU_BOOT)) {		/* run or boot */
	sim_interval = 0;				/* reset queue */
	sim_time = sim_rtime = 0;
	noqueue_time = 0;
	sim_clock_queue = NULL;
	if ((r = reset_all (0)) != SCPE_OK) return r;  }
for (i = 1; (dptr = sim_devices[i]) != NULL; i++) {
	for (j = 0; j < dptr->numunits; j++) {
		uptr = (dptr->units) + j;
		if ((uptr->flags & (UNIT_ATT + UNIT_SEQ)) ==
		    (UNIT_ATT + UNIT_SEQ))
			fseek (uptr->fileref, uptr->pos, SEEK_SET);  }  }
stop_cpu = 0;
if (signal (SIGINT, int_handler) == SIG_ERR) {		/* set WRU */
	return SCPE_SIGERR;  }
if (ttrunstate () != SCPE_OK) {				/* set console mode */
	ttcmdstate ();
	return SCPE_TTYERR;  }
if ((r = sim_check_console (30)) != SCPE_OK) {		/* check console, error? */
	ttcmdstate ();
	return r;  }
if (step) sim_activate (&step_unit, step);		/* set step timer */
sim_is_running = 1;					/* flag running */
r = sim_instr();

sim_is_running = 0;					/* flag idle */
ttcmdstate ();						/* restore console */
signal (SIGINT, SIG_DFL);				/* cancel WRU */
sim_cancel (&step_unit);				/* cancel step timer */
if (sim_clock_queue != NULL) {				/* update sim time */
	UPDATE_SIM_TIME (sim_clock_queue->time);  }
else {	UPDATE_SIM_TIME (noqueue_time);  }
#if defined (VMS)
printf ("\n");
#endif
fprint_stopped (stdout, r);				/* print msg */
if (sim_log) fprint_stopped (sim_log, r);		/* log if enabled */
return SCPE_OK;
}

/* Print stopped message */

void fprint_stopped (FILE *stream, t_stat v)
{
int32 i;
t_stat r;
t_addr k;
t_value pcval;
DEVICE *dptr;

if (v >= SCPE_BASE) fprintf (stream, "\n%s, %s: ",
	scp_error_messages[v - SCPE_BASE], sim_PC->name);
else fprintf (stream, "\n%s, %s: ", sim_stop_messages[v], sim_PC->name);
pcval = get_rval (sim_PC, 0);
fprint_val (stream, pcval, sim_PC->radix, sim_PC->width,
	sim_PC->flags & REG_FMT);
if (((dptr = sim_devices[0]) != NULL) && (dptr->examine != NULL)) {
    for (i = 0; i < sim_emax; i++) sim_eval[i] = 0;
    for (i = 0, k = (t_addr) pcval; i < sim_emax; i++, k = k + dptr->aincr) {
	if ((r = dptr->examine (&sim_eval[i], k, dptr->units,
		SWMASK ('V'))) != SCPE_OK) break;  }
    if ((r == SCPE_OK) || (i > 0)) {
	fprintf (stream, " (");
	if (fprint_sym (stream, (t_addr) pcval, sim_eval, NULL, SWMASK('M')) > 0)
	    fprint_val (stream, sim_eval[0], dptr->dradix,
		dptr->dwidth, PV_RZRO);
	fprintf (stream, ")");  }  }
fprintf (stream, "\n");
return;
}

/* Unit service for step timeout, originally scheduled by STEP n command

   Return step timeout SCP code, will cause simulation to stop
*/

t_stat step_svc (UNIT *uptr)
{
return SCPE_STEP;
}

/* Signal handler for ^C signal

   Set stop simulation flag
*/

void int_handler (int sig)
{
stop_cpu = 1;
return;
}

/* Examine/deposit commands

   ex[amine] [modifiers] list		examine
   de[posit] [modifiers] list val	deposit
   ie[xamine] [modifiers] list		interactive examine
   id[eposit] [modifiers] list		interactive deposit

   modifiers
	@filename			output file
	-letter(s)			switches
	devname'n			device name and unit number
	[{&|^}value]{=|==|!|!=|>|>=|<|<=} value	search specification

   list					list of addresses and registers
	addr[:addr|-addr]		address range
	ALL				all addresses
	register[:register|-register]	register range
	STATE				all registers
*/

t_stat exdep_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE], *gptr, *tptr;
int32 t;
t_bool exd2f;
t_addr low, high;
t_stat reason;
DEVICE *dptr, *tdptr;
UNIT *uptr, *tuptr;
REG *lowr, *highr;
SCHTAB stab, *schptr;
FILE *ofile;
t_stat exdep_addr_loop (FILE *ofile, SCHTAB *schptr, int32 flag, char *ptr,
	t_addr low, t_addr high, DEVICE *dptr, UNIT *uptr);
t_stat exdep_reg_loop (FILE *ofile, SCHTAB *schptr, int32 flag, char *ptr,
	REG *lowr, REG *highr, t_addr lows, t_addr highs);

ofile = NULL;						/* no output file */
exd2f = FALSE;
sim_switches = 0;					/* no switches */
schptr = NULL;						/* no search */
stab.logic = SCH_OR;					/* default search params */
stab.bool = SCH_GE;
stab.mask = stab.comp = 0;
dptr = sim_devices[0];					/* default device, unit */
uptr = dptr->units;
for (;;) {						/* loop through modifiers */
	if (*cptr == 0) return SCPE_2FARG;		/* must be more */
	if (*cptr == '@') {				/* output file spec? */
		if (flag != EX_E) return SCPE_ARG;	/* examine only */
		if (exd2f) {				/* already got one? */
			fclose (ofile);			/* one per customer */
			return SCPE_ARG;  }
		cptr = get_glyph_nc (cptr + 1, gbuf, 0);
		ofile = FOPEN (gbuf, "a");		/* open for append */
		if (ofile == NULL) return SCPE_OPENERR;
		exd2f = TRUE;
		continue;  }				/* look for more */
	cptr = get_glyph (cptr, gbuf, 0);
	if ((t = get_switches (gbuf)) != 0) {		/* try for switches */
		if (t < 0) return SCPE_INVSW;		/* err if bad switch */
		sim_switches = sim_switches | t;  }	/* or in new switches */
	else if (get_search (gbuf, dptr, &stab) != NULL) {	/* try for search */
		schptr = &stab;  }			/* set search */
	else if (((tdptr = find_unit (gbuf, &tuptr)) != NULL) &&
		(tuptr != NULL)) {			/* try for unit */
		dptr = tdptr;				/* set as default */
		uptr = tuptr;  }
	else break;  }					/* not rec, break out */
if (uptr == NULL) return SCPE_NXUN;			/* got a unit? */
if ((*cptr == 0) == (flag == 0)) return SCPE_ARG;	/* eol if needed? */

if (ofile == NULL) ofile = stdout;			/* no file? stdout */
for (gptr = gbuf, reason = SCPE_OK;
	(*gptr != 0) && (reason == SCPE_OK); gptr = tptr) {
	tdptr = dptr;					/* working dptr */
	if (strncmp (gptr, "STATE", strlen ("STATE")) == 0) {
		tptr = gptr + strlen ("STATE");
		if (*tptr && (*tptr++ != ',')) return SCPE_ARG;
		if ((lowr = dptr->registers) == NULL) return SCPE_NXREG;
		for (highr = lowr; highr->name != NULL; highr++) ;
		sim_switches = sim_switches | SWHIDE;
		reason = exdep_reg_loop (ofile, schptr, flag, cptr,
			lowr, --highr, 0, 0);
		continue;  }

	if ((lowr = find_reg (gptr, &tptr, tdptr)) ||
	    (lowr = find_reg_glob (gptr, &tptr, &tdptr))) {
		low = high = 0;
		if ((*tptr == '-') || (*tptr == ':')) {
			highr = find_reg (tptr + 1, &tptr, tdptr);
			if (highr == NULL) return SCPE_NXREG;  }
		else {	highr = lowr;
			if (*tptr == '[') {
				if (lowr->depth <= 1) return SCPE_ARG;
				tptr = get_range (tptr + 1, &low, &high,
					10, lowr->depth - 1, ']');
				if (tptr == NULL) return SCPE_ARG;  }  }
		if (*tptr && (*tptr++ != ',')) return SCPE_ARG;
		reason = exdep_reg_loop (ofile, schptr, flag, cptr,
			lowr, highr, low, high);
		continue;  }

	tptr = get_range (gptr, &low, &high, dptr->aradix,
		(((uptr->capac == 0) || (flag == EX_E))? 0:
		uptr->capac - dptr->aincr), 0);
	if (tptr == NULL) return SCPE_ARG;
	if (*tptr && (*tptr++ != ',')) return SCPE_ARG;
	reason = exdep_addr_loop (ofile, schptr, flag, cptr, low, high,
		dptr, uptr);
	}						/* end for */
if (exd2f) fclose (ofile);				/* close output file */
return reason;
}

/* Loop controllers for examine/deposit

   exdep_reg_loop	examine/deposit range of registers
   exdep_addr_loop	examine/deposit range of addresses
*/

t_stat exdep_reg_loop (FILE *ofile, SCHTAB *schptr, int32 flag, char *cptr, 
	REG *lowr, REG *highr, t_addr lows, t_addr highs)
{
t_stat reason;
t_addr idx;
t_value val;
REG *rptr;

if ((lowr == NULL) || (highr == NULL)) return SCPE_IERR;
if (lowr > highr) return SCPE_ARG;
for (rptr = lowr; rptr <= highr; rptr++) {
	if ((sim_switches & SWHIDE) &&
	    (rptr->flags & REG_HIDDEN)) continue;
	for (idx = lows; idx <= highs; idx++) {
		if (idx >= (t_addr) rptr->depth) return SCPE_SUB;
		val = get_rval (rptr, idx);
		if (schptr && !test_search (val, schptr)) continue;
		if (flag != EX_D) {
			reason = ex_reg (ofile, val, flag, rptr, idx);
			if (reason != SCPE_OK) return reason;
			if (sim_log && (ofile == stdout))
				ex_reg (sim_log, val, flag, rptr, idx);  }
		if (flag != EX_E) {
			reason = dep_reg (flag, cptr, rptr, idx);
			if (reason != SCPE_OK) return reason;  }  }  }
return SCPE_OK;
}

t_stat exdep_addr_loop (FILE *ofile, SCHTAB *schptr, int32 flag, char *cptr,
	t_addr low, t_addr high, DEVICE *dptr, UNIT *uptr)
{
t_addr i, mask;
t_stat reason;

if (uptr->flags & UNIT_DIS) return SCPE_UDIS;		/* disabled? */
mask = (t_addr) width_mask[dptr->awidth];
if ((low > mask) || (high > mask) || (low > high)) return SCPE_ARG;
for (i = low; i <= high; i = i + (dptr->aincr)) {
	reason = get_aval (i, dptr, uptr);		/* get data */
	if (reason != SCPE_OK) return reason;		/* return if error */
	if (schptr && !test_search (sim_eval[0], schptr)) continue;
	if (flag != EX_D) {
		reason = ex_addr (ofile, flag, i, dptr, uptr);
		if (reason > SCPE_OK) return reason;
		if (sim_log && (ofile == stdout))
			ex_addr (sim_log, flag, i, dptr, uptr);  }
	if (flag != EX_E) {
		reason = dep_addr (flag, cptr, i, dptr, uptr, reason);
		if (reason > SCPE_OK) return reason;  }
	if (reason < SCPE_OK) i = i - (reason * dptr->aincr);  }
return SCPE_OK;
}

/* Examine register routine

   Inputs:
	ofile	=	output stream
	val	=	current register value
	flag	=	type of ex/mod command (ex, iex, idep)
	rptr	=	pointer to register descriptor
	idx	=	index
   Outputs:
	return	=	error status
*/

t_stat ex_reg (FILE *ofile, t_value val, int32 flag, REG *rptr, t_addr idx)
{
int32 rdx;

if (rptr == NULL) return SCPE_IERR;
if (rptr->depth > 1) fprintf (ofile, "%s[%d]:	", rptr->name, idx);
else fprintf (ofile, "%s:	", rptr->name);
if (!(flag & EX_E)) return SCPE_OK;
GET_RADIX (rdx, rptr->radix);
fprint_val (ofile, val, rdx, rptr->width, rptr->flags & REG_FMT);
if (flag & EX_I) fprintf (ofile, "	");
else fprintf (ofile, "\n");
return SCPE_OK;
}

/* Get register value

   Inputs:
	rptr	=	pointer to register descriptor
	idx	=	index
   Outputs:
	return	=	register value
*/

t_value get_rval (REG *rptr, int32 idx)
{
size_t sz;
t_value val;
UNIT *uptr;

sz = SZ_R (rptr);
if ((rptr->depth > 1) && (rptr->flags & REG_CIRC)) {
	idx = idx + rptr->qptr;
	if (idx >= rptr->depth) idx = idx - rptr->depth;  }
if ((rptr->depth > 1) && (rptr->flags & REG_UNIT)) {
	uptr = ((UNIT *) rptr->loc) + idx;
	val = *((uint32 *) uptr);  }
else if ((rptr->depth > 1) && (sz == sizeof (uint8)))
	val = *(((uint8 *) rptr->loc) + idx);
else if ((rptr->depth > 1) && (sz == sizeof (uint16)))
	val = *(((uint16 *) rptr->loc) + idx);
#if !defined (t_int64)
else val = *(((uint32 *) rptr->loc) + idx);
#else
else if (sz <= sizeof (uint32))
	 val = *(((uint32 *) rptr->loc) + idx);
else val = *(((t_uint64 *) rptr->loc) + idx);
#endif
val = (val >> rptr->offset) & width_mask[rptr->width];
return val;
}

/* Deposit register routine

   Inputs:
	flag	=	type of deposit (normal/interactive)
	cptr	=	pointer to input string
	rptr	=	pointer to register descriptor
	idx	=	index
   Outputs:
	return	=	error status
*/

t_stat dep_reg (int32 flag, char *cptr, REG *rptr, t_addr idx)
{
t_stat r;
t_value val, mask;
int32 rdx;
char gbuf[CBUFSIZE];

if ((cptr == NULL) || (rptr == NULL)) return SCPE_IERR;
if (rptr->flags & REG_RO) return SCPE_RO;
if (flag & EX_I) {
	cptr = read_line (gbuf, CBUFSIZE, stdin);
	if (sim_log) fprintf (sim_log, (cptr? "%s\n": "\n"), cptr);
	if (cptr == NULL) return 1;			/* force exit */
	if (*cptr == 0) return SCPE_OK;	 }		/* success */
mask = width_mask[rptr->width];
GET_RADIX (rdx, rptr->radix);
val = get_uint (cptr, rdx, mask, &r);
if (r != SCPE_OK) return SCPE_ARG;
if ((rptr->flags & REG_NZ) && (val == 0)) return SCPE_ARG;
put_rval (rptr, idx, val);
return SCPE_OK;
}

/* Put register value

   Inputs:
	rptr	=	pointer to register descriptor
	idx	=	index
	val	=	new value
	mask	=	mask
   Outputs:
	none
*/

void put_rval (REG *rptr, int32 idx, t_value val)
{
size_t sz;
t_value mask;
UNIT *uptr;

#define PUT_RVAL(sz,rp,id,v,m) \
	*(((sz *) rp->loc) + id) = \
		(*(((sz *) rp->loc) + id) & \
		~((m) << (rp)->offset)) | ((v) << (rp)->offset)

if (rptr == sim_PC) sim_brk_npc ();
sz = SZ_R (rptr);
mask = width_mask[rptr->width];
if ((rptr->depth > 1) && (rptr->flags & REG_CIRC)) {
	idx = idx + rptr->qptr;
	if (idx >= rptr->depth) idx = idx - rptr->depth;  }
if ((rptr->depth > 1) && (rptr->flags & REG_UNIT)) {
	uptr = ((UNIT *) rptr->loc) + idx;
	*((uint32 *) uptr) =
		(*((uint32 *) uptr) &
		~(((uint32) mask) << rptr->offset)) | 
		(((uint32) val) << rptr->offset);  }
else if ((rptr->depth > 1) && (sz == sizeof (uint8)))
	PUT_RVAL (uint8, rptr, idx, (uint32) val, (uint32) mask);
else if ((rptr->depth > 1) && (sz == sizeof (uint16)))
	PUT_RVAL (uint16, rptr, idx, (uint32) val, (uint32) mask);
#if !defined (t_int64)
else PUT_RVAL (uint32, rptr, idx, val, mask);
#else
else if (sz <= sizeof (uint32))
	PUT_RVAL (uint32, rptr, idx, (int32) val, (uint32) mask);
else PUT_RVAL (t_uint64, rptr, idx, val, mask);
#endif
return;
}

/* Examine address routine

   Inputs: (sim_eval is an implicit argument)
	ofile	=	output stream
	flag	=	type of ex/mod command (ex, iex, idep)
	addr	=	address to examine
	dptr	=	pointer to device
	uptr	=	pointer to unit
   Outputs:
	return	=	if >= 0, error status
			if < 0, number of extra words retired
*/

t_stat ex_addr (FILE *ofile, int32 flag, t_addr addr, DEVICE *dptr, UNIT *uptr)
{
t_stat reason;
int32 rdx;

fprint_val (ofile, addr, dptr->aradix, dptr->awidth, PV_LEFT);
fprintf (ofile, ":	");
if (!(flag & EX_E)) return SCPE_OK;

GET_RADIX (rdx, dptr->dradix);
if ((reason = fprint_sym (ofile, addr, sim_eval, uptr, sim_switches)) > 0)
    reason = fprint_val (ofile, sim_eval[0], rdx, dptr->dwidth, PV_RZRO);
if (flag & EX_I) fprintf (ofile, "	");
else fprintf (ofile, "\n");
return reason;
}

/* Get address routine

   Inputs:
	flag	=	type of ex/mod command (ex, iex, idep)
	addr	=	address to examine
	dptr	=	pointer to device
	uptr	=	pointer to unit
   Outputs: (sim_eval is an implicit output)
	return	=	error status
*/

t_stat get_aval (t_addr addr, DEVICE *dptr, UNIT *uptr)
{
int32 i;
t_value mask;
t_addr j, loc;
size_t sz;
t_stat reason = SCPE_OK;

if ((dptr == NULL) || (uptr == NULL)) return SCPE_IERR;
mask = width_mask[dptr->dwidth];
for (i = 0; i < sim_emax; i++) sim_eval[i] = 0;
for (i = 0, j = addr; i < sim_emax; i++, j = j + dptr->aincr) {
	if (dptr->examine != NULL) {
		reason = dptr->examine (&sim_eval[i], j, uptr, sim_switches);
		if (reason != SCPE_OK) break;  }
	else {	if (!(uptr->flags & UNIT_ATT)) return SCPE_UNATT;
		if ((uptr->flags & UNIT_FIX) && (j >= uptr->capac)) {
			reason = SCPE_NXM;
			break;  }
		sz = SZ_D (dptr);
		loc = j / dptr->aincr;
		if (uptr->flags & UNIT_BUF) {
			SZ_LOAD (sz, sim_eval[i], uptr->filebuf, loc);  }
		else {	fseek (uptr->fileref, sz * loc, SEEK_SET);
			fxread (&sim_eval[i], sz, 1, uptr->fileref);
			if ((feof (uptr->fileref)) &&
			   !(uptr->flags & UNIT_FIX)) {
				reason = SCPE_EOF;
				break;  }
		 	else if (ferror (uptr->fileref)) {
				clearerr (uptr->fileref);
				reason = SCPE_IOERR;
				break;  }  }  }
	sim_eval[i] = sim_eval[i] & mask;  }
if ((reason != SCPE_OK) && (i == 0)) return reason;
return SCPE_OK;
}

/* Deposit address routine

   Inputs:
	flag	=	type of deposit (normal/interactive)
	cptr	=	pointer to input string
	addr	=	address to examine
	dptr	=	pointer to device
	uptr	=	pointer to unit
	dfltinc	=	value to return on cr input
   Outputs:
	return	=	if >= 0, error status
			if < 0, number of extra words retired
*/

t_stat dep_addr (int32 flag, char *cptr, t_addr addr, DEVICE *dptr,
	UNIT *uptr, int32 dfltinc)
{
int32 i, count, rdx;
t_addr j, loc;
t_stat r, reason;
t_value mask;
size_t sz;
char gbuf[CBUFSIZE];

if (dptr == NULL) return SCPE_IERR;
if (flag & EX_I) {
	cptr = read_line (gbuf, CBUFSIZE, stdin);
	if (sim_log) fprintf (sim_log, (cptr? "%s\n": "\n"), cptr);
	if (cptr == NULL) return 1;			/* force exit */
	if (*cptr == 0) return dfltinc;	 }		/* success */
if (uptr->flags & UNIT_RO) return SCPE_RO;		/* read only? */
mask = width_mask[dptr->dwidth];

GET_RADIX (rdx, dptr->dradix);
if ((reason = parse_sym (cptr, addr, uptr, sim_eval, sim_switches)) > 0) {
	sim_eval[0] = get_uint (cptr, rdx, mask, &reason);
	if (reason != SCPE_OK) return reason;  }
count = 1 - reason;

for (i = 0, j = addr; i < count; i++, j = j + dptr->aincr) {
	sim_eval[i] = sim_eval[i] & mask;
	if (dptr->deposit != NULL) {
		r = dptr->deposit (sim_eval[i], j, uptr, sim_switches);
		if (r != SCPE_OK) return r;  }
	else {	if (!(uptr->flags & UNIT_ATT)) return SCPE_UNATT;
		if ((uptr->flags & UNIT_FIX) && (j >= uptr->capac))
			return SCPE_NXM;
		sz = SZ_D (dptr);
		loc = j / dptr->aincr;
		if (uptr->flags & UNIT_BUF) {
			SZ_STORE (sz, sim_eval[i], uptr->filebuf, loc);
			if (loc >= uptr->hwmark) uptr->hwmark = loc + 1;  }
		else {	fseek (uptr->fileref, sz * loc, SEEK_SET);
			fxwrite (sim_eval, sz, 1, uptr->fileref);
			if (ferror (uptr->fileref)) {
				clearerr (uptr->fileref);
				return SCPE_IOERR;  }  }  }  }
return reason;
}

/* String processing routines

   read_line		read line

   Inputs:
	cptr	=	pointer to buffer
	size	=	maximum size
	stream	=	pointer to input stream
   Outputs:
	optr	=	pointer to first non-blank character
			NULL if EOF
*/

char *read_line (char *cptr, int32 size, FILE *stream)
{
char *tptr;

cptr = fgets (cptr, size, stream);			/* get cmd line */
if (cptr == NULL) {
	clearerr (stream);				/* clear error */
	return NULL;  }					/* ignore EOF */
for (tptr = cptr; tptr < (cptr + size); tptr++)		/* remove cr or nl */
	if ((*tptr == '\n') || (*tptr == '\r')) *tptr = 0; 
while (isspace (*cptr)) cptr++;				/* absorb spaces */
if (*cptr == ';') *cptr = 0;				/* ignore comment */
return cptr;
}

/* get_glyph		get next glyph (force upper case)
   get_glyph_nc		get next glyph (no conversion)
   get_glyph_gen	get next glyph (general case)

   Inputs:
	iptr	=	pointer to input string
	optr	=	pointer to output string
	mchar	=	optional end of glyph character
	flag	=	TRUE for convert to upper case (_gen only)
   Outputs
	result	=	pointer to next character in input string
*/

char *get_glyph_gen (char *iptr, char *optr, char mchar, t_bool uc)
{
while ((isspace (*iptr) == 0) && (*iptr != 0) && (*iptr != mchar)) {
	if (islower (*iptr) && uc) *optr = toupper (*iptr);
	else *optr = *iptr;
	iptr++; optr++;  }
*optr = 0;
if (mchar && (*iptr == mchar)) iptr++;			/* skip terminator */
while (isspace (*iptr)) iptr++;				/* absorb spaces */
return iptr;
}

char *get_glyph (char *iptr, char *optr, char mchar)
{
return get_glyph_gen (iptr, optr, mchar, TRUE);
}

char *get_glyph_nc (char *iptr, char *optr, char mchar)
{
return get_glyph_gen (iptr, optr, mchar, FALSE);
}

/* get_yn		yes/no question

   Inputs:
	cptr	=	pointer to question
	deflt	=	default answer
   Outputs:
	result	=	true if yes, false if no
*/

t_stat get_yn (char *ques, t_stat deflt)
{
char cbuf[CBUFSIZE], *cptr;

printf ("%s ", ques);
cptr = read_line (cbuf, CBUFSIZE, stdin);
if ((cptr == NULL) || (*cptr == 0)) return deflt;
if ((*cptr == 'Y') || (*cptr == 'y')) return TRUE;
return FALSE;
}

/* get_uint		unsigned number

   Inputs:
	cptr	=	pointer to input string
	radix	=	input radix
	max	=	maximum acceptable value
	*status	=	pointer to error status
   Outputs:
	val	=	value
*/

t_value get_uint (char *cptr, int32 radix, t_value max, t_stat *status)
{
t_value val;
char *tptr;

val = strtotv (cptr, &tptr, radix);
if ((cptr == tptr) || (val > max) || (*tptr != 0)) *status = SCPE_ARG;
else *status = SCPE_OK;
return val;
}

/* get_range		range specification

   Inputs:
	cptr	=	pointer to input string
	*lo	=	pointer to low result
	*hi	=	pointer to high result
	aradix	=	address radix
	max	=	default high value
	term	=	terminating character, 0 if none
   Outputs:
	tptr	=	input pointer after processing
			NULL if error
*/

char *get_range (char *cptr, t_addr *lo, t_addr *hi, int32 rdx,
	t_addr max, char term)
{
char *tptr;
t_addr hb;

*lo = *hi = hb = 0;
if (max && strncmp (cptr, "ALL", strlen ("ALL")) == 0) { /* ALL? */
	tptr = cptr + strlen ("ALL");
	*hi = max;  }
else {	errno = 0;
	*lo = strtoul (cptr, &tptr, rdx);		/* get low */
	if (errno || (cptr == tptr)) return NULL;	/* error? */
	if ((*tptr == '-') || (*tptr == ':') || (*tptr == '/')) {
		if (*tptr == '/') hb = *lo;		/* relative? */
		cptr = tptr + 1;
		errno = 0;
		*hi = hb + strtoul (cptr, &tptr, rdx);	/* get high */
		if (errno || (cptr == tptr)) return NULL;
		if (*lo > *hi) return NULL;  }
	else *hi = *lo;  }
if (term && (*tptr++ != term)) return NULL;
return tptr;
}

/* Find_device		find device matching input string

   Inputs:
	cptr	=	pointer to input string
   Outputs:
	result	=	pointer to device
*/

DEVICE *find_dev (char *cptr)
{
int32 i;
DEVICE *dptr;

for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
	if (strcmp (cptr, dptr->name) == 0) return dptr;  }
return NULL;
}

/* Find_unit		find unit matching input string

   Inputs:
	cptr	=	pointer to input string
	uptr	=	pointer to unit pointer
   Outputs:
	result	=	pointer to device (null if no dev)
	*iptr	=	pointer to unit (null if nx unit)
*/

DEVICE *find_unit (char *cptr, UNIT **uptr)
{
int32 i, u;
char *tptr;
t_stat r;
DEVICE *dptr;

if (uptr == NULL) return NULL;				/* arg error? */
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {	/* exact match? */
	if (strcmp (cptr, dptr->name) == 0) {
		if (qdisable (dptr)) return NULL;	/* disabled? */
		*uptr = dptr->units;			/* unit 0 */
		return dptr;  }  }

for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {	/* base + unit#? */
	if ((dptr->numunits == 0) ||			/* no units? */
	    (strncmp (cptr, dptr->name, strlen (dptr->name)) != 0)) continue;
	tptr = cptr + strlen (dptr->name);		/* skip devname */
	if (!isdigit (*tptr)) continue;			/* number next? */
	if (qdisable (dptr)) return NULL;		/* disabled? */
 	u = (int32) get_uint (tptr, 10, dptr->numunits - 1, &r);
	if (r != SCPE_OK) *uptr = NULL;			/* error? */
	else *uptr = dptr->units + u;
	return dptr;  }	
return NULL;
}

/* Find_dev_from_unit	find device for unit

   Inputs:
	uptr	=	pointer to unit
   Outputs:
	result	=	pointer to device
*/

DEVICE *find_dev_from_unit (UNIT *uptr)
{
DEVICE *dptr;
int32 i, j;

if (uptr == NULL) return NULL;
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
	for (j = 0; j < dptr->numunits; j++) {
		if (uptr == (dptr->units + j)) return dptr;  }  }
return NULL;
}

/* Test for disabled device */

t_bool qdisable (DEVICE *dptr)
{
return (dptr->flags & DEV_DIS? TRUE: FALSE);
}

/* find_reg_glob	find globally unique register

   Inputs:
	cptr	=	pointer to input string
	optr	=	pointer to output pointer (can be null)
	gdptr	=	pointer to global device
   Outputs:
	result	=	pointer to register, NULL if error
	*optr	=	pointer to next character in input string
	*gdptr	=	pointer to device where found
*/

REG *find_reg_glob (char *cptr, char **optr, DEVICE **gdptr)
{
int32 i;
DEVICE *dptr;
REG *rptr, *srptr = NULL;

for (i = 0; (dptr = sim_devices[i]) != 0; i++) {	/* all dev */
	if (rptr = find_reg (cptr, optr, dptr)) {	/* found? */
		if (srptr) return NULL;			/* ambig? err */
		srptr = rptr;				/* save reg */
		*gdptr = dptr;  }  }			/* save unit */
return srptr;
}

/* find_reg		find register matching input string

   Inputs:
	cptr	=	pointer to input string
	optr	=	pointer to output pointer (can be null)
	dptr	=	pointer to device
   Outputs:
	result	=	pointer to register, NULL if error
	*optr	=	pointer to next character in input string
*/

REG *find_reg (char *cptr, char **optr, DEVICE *dptr)
{
char *tptr;
REG *rptr;
uint32 slnt;

if ((cptr == NULL) || (dptr == NULL) ||
	(dptr->registers == NULL)) return NULL;
tptr = cptr;
do { tptr++; }
	while (isalnum (*tptr) || (*tptr == '*') || (*tptr == '_'));
slnt = tptr - cptr;
for (rptr = dptr->registers; rptr->name != NULL; rptr++) {
	if ((slnt == strlen (rptr->name)) &&
	    (strncmp (cptr, rptr->name, slnt) == 0)) {
		if (optr != NULL) *optr = tptr;
		return rptr;  }  }
return NULL;
}

/* get_switches		get switches from input string

   Inputs:
	cptr	=	pointer to input string
   Outputs:
	sw	=	switch bit mask
			0 if no switches, -1 if error
*/

int32 get_switches (char *cptr)
{
int32 sw;

if (*cptr != '-') return 0;
sw = 0;
for (cptr++; (isspace (*cptr) == 0) && (*cptr != 0); cptr++) {
	if (isalpha (*cptr) == 0) return -1;
	sw = sw | SWMASK (*cptr);  }
return sw;
}

t_bool match_ext (char *fnam, char *ext)
{
char *fptr, *eptr;

if ((fnam == NULL) || (ext == NULL)) return FALSE;
fptr = strrchr (fnam, '.');
if (fptr == NULL) return FALSE;
for (fptr++, eptr = ext; *fptr; fptr++, eptr++) {
	if (toupper (*fptr) != toupper (*eptr)) return FALSE;  }
if (*eptr) return FALSE;
return TRUE;
}

/* Get search specification

   Inputs:
	cptr	=	pointer to input string
	dptr	=	pointer to device
	schptr =	pointer to search table
   Outputs:
	return =	NULL if error
			schptr if valid search specification
*/

SCHTAB *get_search (char *cptr, DEVICE *dptr, SCHTAB *schptr)
{
int32 c, logop, cmpop;
t_value logval, cmpval;
char *sptr, *tptr;
const char logstr[] = "|&^", cmpstr[] = "=!><";

if (*cptr == 0) return NULL;				/* check for clause */
for (logop = cmpop = -1; c = *cptr++; ) {		/* loop thru clauses */
	if (sptr = strchr (logstr, c)) {		/* check for mask */
		logop = sptr - logstr;
		logval = strtotv (cptr, &tptr, dptr->dradix);
		if (cptr == tptr) return NULL;
		cptr = tptr;  }
	else if (sptr = strchr (cmpstr, c)) {		/* check for bool */
		cmpop = sptr - cmpstr;
		if (*cptr == '=') {
			cmpop = cmpop + strlen (cmpstr);
			cptr++;  }
		cmpval = strtotv (cptr, &tptr, dptr->dradix);
		if (cptr == tptr) return NULL;
		cptr = tptr;  }
	else return NULL;  }				/* end while */
if (logop >= 0) {
	schptr->logic = logop;
	schptr->mask = logval;  }
if (cmpop >= 0) {
	schptr->bool = cmpop;
	schptr->comp = cmpval;  }
return schptr;
}

/* Test value against search specification

   Inputs:
	val	=	value to test
	schptr =	pointer to search table
   Outputs:
	return =	1 if value passes search criteria, 0 if not
*/

int32 test_search (t_value val, SCHTAB *schptr)
{
if (schptr == NULL) return 0;
switch (schptr->logic) {				/* case on logical */
case SCH_OR:
	val = val | schptr->mask;
	break;
case SCH_AND:
	val = val & schptr->mask;
	break;
case SCH_XOR:
	val = val ^ schptr->mask;
	break;  }
switch (schptr->bool) {					/* case on comparison */
case SCH_E: case SCH_EE:
	return (val == schptr->comp);
case SCH_N: case SCH_NE:
	return (val != schptr->comp);
case SCH_G:
	return (val > schptr->comp);
case SCH_GE:
	return (val >= schptr->comp);
case SCH_L:
	return (val < schptr->comp);
case SCH_LE:
	return (val <= schptr->comp);  }
return 0;
}

/* Radix independent input/output package

   strtotv - general radix input routine

   Inputs:
	inptr	=	string to convert
	endptr	=	pointer to first unconverted character
	radix	=	radix for input
   Outputs:
	value	=	converted value

   On an error, the endptr will equal the inptr.
*/

t_value strtotv (char *inptr, char **endptr, int32 radix)
{
int32 nodigit;
t_value val;
int32 c, digit;

*endptr = inptr;					/* assume fails */
if ((radix < 2) || (radix > 36)) return 0;
while (isspace (*inptr)) inptr++;			/* bypass white space */
val = 0;
nodigit = 1;
for (c = *inptr; isalnum(c); c = *++inptr) {		/* loop through char */
	if (islower (c)) c = toupper (c);
	if (isdigit (c)) digit = c - (int) '0';		/* digit? */
	else digit = c + 10 - (int) 'A';		/* no, letter */
	if (digit >= radix) return 0;			/* valid in radix? */
	val = (val * radix) + digit;			/* add to value */
	nodigit = 0;  }
if (nodigit) return 0;					/* no digits? */
*endptr = inptr;					/* result pointer */
return val;
}

/* fprint_val - general radix printing routine

   Inputs:
	stream	=	stream designator
	val	=	value to print
	radix	=	radix to print
	width	=	width to print
	format	=	leading zeroes format
   Outputs:
	status	=	error status
*/

t_stat fprint_val (FILE *stream, t_value val, int32 radix,
	int32 width, int32 format)
{
#define MAX_WIDTH ((int) (CHAR_BIT * sizeof (t_value)))
t_value owtest, wtest;
int32 d, digit, ndigits;
char dbuf[MAX_WIDTH + 1];

for (d = 0; d < MAX_WIDTH; d++) dbuf[d] = (format == PV_RZRO)? '0': ' ';
dbuf[MAX_WIDTH] = 0;
d = MAX_WIDTH;
do {	d = d - 1;
	digit = (int32) (val % (unsigned) radix);
	val = val / (unsigned) radix;
	dbuf[d] = (digit <= 9)? '0' + digit: 'A' + (digit - 10);
   } while ((d > 0) && (val != 0));

if (format != PV_LEFT) {
	wtest = owtest = radix;
	ndigits = 1;
	while ((wtest < width_mask[width]) && (wtest >= owtest)) {
		owtest = wtest;
		wtest = wtest * radix;
		ndigits = ndigits + 1;  }
	if ((MAX_WIDTH - ndigits) < d) d = MAX_WIDTH - ndigits;  }
if (fputs (&dbuf[d], stream) == EOF) return SCPE_IOERR;
return SCPE_OK;
}

/* Event queue package

	sim_activate		add entry to event queue
	sim_cancel		remove entry from event queue
	sim_process_event	process entries on event queue
	sim_is_active		see if entry is on event queue
	sim_atime		return absolute time for an entry
	sim_gtime		return global time
	sim_qcount		return event queue entry count

   Asynchronous events are set up by queueing a unit data structure
   to the event queue with a timeout (in simulator units, relative
   to the current time).  Each simulator 'times' these events by
   counting down interval counter sim_interval.  When this reaches
   zero the simulator calls sim_process_event to process the event
   and to see if further events need to be processed, or sim_interval
   reset to count the next one.

   The event queue is maintained in clock order; entry timeouts are
   RELATIVE to the time in the previous entry.

   sim_process_event - process event

   Inputs:
	none
   Outputs:
	reason		reason code returned by any event processor,
			or 0 (SCPE_OK) if no exceptions
*/

t_stat sim_process_event (void)
{
UNIT *uptr;
t_stat reason;

if (stop_cpu) return SCPE_STOP;				/* stop CPU? */
if (sim_clock_queue == NULL) {				/* queue empty? */
	UPDATE_SIM_TIME (noqueue_time);			/* update sim time */
	sim_interval = noqueue_time = NOQUEUE_WAIT;	/* flag queue empty */
	return SCPE_OK;  }
UPDATE_SIM_TIME (sim_clock_queue->time);		/* update sim time */
do {	uptr = sim_clock_queue;				/* get first */
	sim_clock_queue = uptr->next;			/* remove first */
	uptr->next = NULL;				/* hygiene */
	uptr->time = 0;
	if (sim_clock_queue != NULL) sim_interval = sim_clock_queue->time;
	else sim_interval = noqueue_time = NOQUEUE_WAIT;
	if (uptr->action != NULL) reason = uptr->action (uptr);
	else reason = SCPE_OK;
   } while ((reason == SCPE_OK) && (sim_interval == 0));

/* Empty queue forces sim_interval != 0 */

return reason;
}

/* sim_activate - activate (queue) event

   Inputs:
	uptr	=	pointer to unit
	event_time =	relative timeout
   Outputs:
	reason	=	result (SCPE_OK if ok)
*/

t_stat sim_activate (UNIT *uptr, int32 event_time)
{
UNIT *cptr, *prvptr;
int32 accum;

if (event_time < 0) return SCPE_IERR;
if (sim_is_active (uptr)) return SCPE_OK;		/* already active? */
if (sim_clock_queue == NULL) { UPDATE_SIM_TIME (noqueue_time);  }
else  {	UPDATE_SIM_TIME (sim_clock_queue->time);  }	/* update sim time */

prvptr = NULL;
accum = 0;
for (cptr = sim_clock_queue; cptr != NULL; cptr = cptr->next) {
	if (event_time < (accum + cptr->time)) break;
	accum = accum + cptr->time;
	prvptr = cptr;  }
if (prvptr == NULL) {					/* insert at head */
	cptr = uptr->next = sim_clock_queue;
	sim_clock_queue = uptr;  }
else {	cptr = uptr->next = prvptr->next;		/* insert at prvptr */
	prvptr->next = uptr;  }
uptr->time = event_time - accum;
if (cptr != NULL) cptr->time = cptr->time - uptr->time;
sim_interval = sim_clock_queue->time;
return SCPE_OK;
}

/* sim_cancel - cancel (dequeue) event

   Inputs:
	uptr	=	pointer to unit
   Outputs:
	reason	=	result (SCPE_OK if ok)

*/

t_stat sim_cancel (UNIT *uptr)
{
UNIT *cptr, *nptr;

if (sim_clock_queue == NULL) return SCPE_OK;
UPDATE_SIM_TIME (sim_clock_queue->time);		/* update sim time */
nptr = NULL;
if (sim_clock_queue == uptr) nptr = sim_clock_queue = uptr->next;
else {	for (cptr = sim_clock_queue; cptr != NULL; cptr = cptr->next) {
		if (cptr->next == uptr) {
			nptr = cptr->next = uptr->next;
			break;  }  }  }			/* end queue scan */
if (nptr != NULL) nptr->time = nptr->time + uptr->time;
uptr->next = NULL;					/* hygiene */
uptr->time = 0;
if (sim_clock_queue != NULL) sim_interval = sim_clock_queue->time;
else sim_interval = noqueue_time = NOQUEUE_WAIT;
return SCPE_OK;
}

/* sim_is_active - test for entry in queue, return activation time

   Inputs:
	uptr	=	pointer to unit
   Outputs:
	result =	absolute activation time + 1, 0 if inactive
*/

int32 sim_is_active (UNIT *uptr)
{
UNIT *cptr;
int32 accum;

accum = 0;
for (cptr = sim_clock_queue; cptr != NULL; cptr = cptr->next) {
	accum = accum + cptr->time;
	if (cptr == uptr) return accum + 1;  }
return 0;
}

/* sim_gtime - return global time
   sim_grtime - return global time with rollover

   Inputs: none
   Outputs:
	time	=	global time
*/

double sim_gtime (void)
{
if (sim_clock_queue == NULL) { UPDATE_SIM_TIME (noqueue_time);  }
else  {	UPDATE_SIM_TIME (sim_clock_queue->time);  }
return sim_time;
}

uint32 sim_grtime (void)
{
if (sim_clock_queue == NULL) { UPDATE_SIM_TIME (noqueue_time);  }
else  {	UPDATE_SIM_TIME (sim_clock_queue->time);  }
return sim_rtime;
}

/* sim_qcount - return queue entry count

   Inputs: none
   Outputs:
	count	=	number of entries on the queue
*/

int32 sim_qcount (void)
{
int32 cnt;
UNIT *uptr;

cnt = 0;
for (uptr = sim_clock_queue; uptr != NULL; uptr = uptr->next) cnt++;
return cnt;
}

/* Endian independent binary I/O package

   For consistency, all binary data read and written by the simulator
   is stored in little endian data order.  That is, in a multi-byte
   data item, the bytes are written out right to left, low order byte
   to high order byte.  On a big endian host, data is read and written
   from high byte to low byte.  Consequently, data written on a little
   endian system must be byte reversed to be usable on a big endian
   system, and vice versa.

   These routines are analogs of the standard C runtime routines
   fread and fwrite.  If the host is little endian, or the data items
   are size char, then the calls are passed directly to fread or
   fwrite.  Otherwise, these routines perform the necessary byte swaps
   using an intermediate buffer.
*/

size_t fxread (void *bptr, size_t size, size_t count, FILE *fptr)
{
size_t c, j, nelem, nbuf, lcnt, total;
int32 i, k;
unsigned char *sptr, *dptr;

if (sim_end || (size == sizeof (char)))
	return fread (bptr, size, count, fptr);
if ((size == 0) || (count == 0)) return 0;
nelem = FLIP_SIZE / size;				/* elements in buffer */
nbuf = count / nelem;					/* number buffers */
lcnt = count % nelem;					/* count in last buf */
if (lcnt) nbuf = nbuf + 1;
else lcnt = nelem;
total = 0;
dptr = bptr;						/* init output ptr */
for (i = nbuf; i > 0; i--) {
	c = fread (sim_flip, size, (i == 1? lcnt: nelem), fptr);
	if (c == 0) return total;
	total = total + c;
	for (j = 0, sptr = sim_flip; j < c; j++) {
		for (k = size - 1; k >= 0; k--) *(dptr + k) = *sptr++;
		dptr = dptr + size;  }  }
return total;
}

size_t fxwrite (void *bptr, size_t size, size_t count, FILE *fptr)
{
size_t c, j, nelem, nbuf, lcnt, total;
int32 i, k;
unsigned char *sptr, *dptr;

if (sim_end || (size == sizeof (char)))
	return fwrite (bptr, size, count, fptr);
if ((size == 0) || (count == 0)) return 0;
nelem = FLIP_SIZE / size;				/* elements in buffer */
nbuf = count / nelem;					/* number buffers */
lcnt = count % nelem;					/* count in last buf */
if (lcnt) nbuf = nbuf + 1;
else lcnt = nelem;
total = 0;
sptr = bptr;						/* init input ptr */
for (i = nbuf; i > 0; i--) {
	c = (i == 1)? lcnt: nelem;
	for (j = 0, dptr = sim_flip; j < c; j++) {
		for (k = size - 1; k >= 0; k--) *(dptr + k) = *sptr++;
		dptr = dptr + size;  }
	c = fwrite (sim_flip, size, c, fptr);
	if (c == 0) return total;
	total = total + c;  }
return total;
}

/* OS independent clock calibration package

   sim_rtc_init	initialize calibration
   sim_rtc_calb	calibrate clock
*/

static int32 rtc_ticks[SIM_NTIMERS] = { 0 };		/* ticks */
static uint32 rtc_rtime[SIM_NTIMERS] = { 0 };		/* real time */
static uint32 rtc_vtime[SIM_NTIMERS] = { 0 };		/* virtual time */
static uint32 rtc_nxintv[SIM_NTIMERS] = { 0 };		/* next interval */
static int32 rtc_based[SIM_NTIMERS] = { 0 };		/* base delay */
static int32 rtc_currd[SIM_NTIMERS] = { 0 };		/* current delay */
extern t_bool rtc_avail;

int32 sim_rtcn_init (int32 time, int32 tmr)
{
if ((tmr < 0) || (tmr >= SIM_NTIMERS)) return time;
rtc_rtime[tmr] = sim_os_msec ();
rtc_vtime[tmr] = rtc_rtime[tmr];
rtc_nxintv[tmr] = 1000;
rtc_ticks[tmr] = 0;
rtc_based[tmr] = time;
rtc_currd[tmr] = time;
return time;
}

int32 sim_rtcn_calb (int32 ticksper, int32 tmr)
{
uint32 new_rtime, delta_rtime;
int32 delta_vtime;

if ((tmr < 0) || (tmr >= SIM_NTIMERS)) return 10000;
rtc_ticks[tmr] = rtc_ticks[tmr] + 1;			/* count ticks */
if (rtc_ticks[tmr] < ticksper) return rtc_currd[tmr];	/* 1 sec yet? */
rtc_ticks[tmr] = 0;					/* reset ticks */
if (!rtc_avail) return rtc_currd[tmr];			/* no timer? */
new_rtime = sim_os_msec ();				/* wall time */
if (new_rtime < rtc_rtime[tmr]) {			/* time running backwards? */
	rtc_rtime[tmr] = new_rtime;			/* reset wall time */
	return rtc_currd[tmr];  }			/* can't calibrate */
delta_rtime = new_rtime - rtc_rtime[tmr];		/* elapsed wtime */
rtc_rtime[tmr] = new_rtime;				/* adv wall time */
if ((delta_rtime == 0) || (delta_rtime > 30000))	/* gap 0 or too big? */
	return rtc_currd[tmr];				/* can't calibr */
rtc_based[tmr] = (int32) (((double) rtc_based[tmr] * (double) rtc_nxintv[tmr]) /
	((double) delta_rtime));			/* new base rate */
rtc_vtime[tmr] = rtc_vtime[tmr] + 1000;			/* adv sim time */
delta_vtime = rtc_vtime[tmr] - rtc_rtime[tmr];		/* gap */
if (delta_vtime > SIM_TMAX) delta_vtime = SIM_TMAX;	/* limit gap */
else if (delta_vtime < -SIM_TMAX) delta_vtime = -SIM_TMAX;
rtc_nxintv[tmr] = 1000 + delta_vtime;			/* next wtime */
rtc_currd[tmr] = (int32) (((double) rtc_based[tmr] * (double) rtc_nxintv[tmr]) /
	1000.0);					/* next delay */
return rtc_currd[tmr];
}

/* Prior interfaces - default to timer 0 */

int32 sim_rtc_init (int32 time)
{
return sim_rtcn_init (time, 0);
}

int32 sim_rtc_calb (int32 ticksper)
{
return sim_rtcn_calb (ticksper, 0);
}

/* Breakpoint package.  This module replaces the VM-implemented one
   instruction breakpoint capability.

   Breakpoints are stored in table sim_brk_tab, which is ordered by address for
   efficient binary searching.  A breakpoint consists of a four entry structure:

	addr			address of the breakpoint
	type			types of breakpoints set on the address
				a bit mask representing letters A-Z
	cnt			number of iterations before breakp is taken
	action			pointer to list of commands to be executed
				when break is taken

   sim_brk_summ is a summary of the types of breakpoints that are currently set (it
   is the bitwise OR of all the type fields).  A simulator need only check for
   a breakpoint of type X if bit SWMASK('X') is set in sim_brk_sum.

   The package contains the following public routines:

	sim_brk_init		initialize
	sim_brk_pchg		PC change
	sim_brk_set		set breakpoint
	sim_brk_clr		clear breakpoint
	sim_brk_clrall		clear all breakpoints
	sim_brk_show		show breakpoint
	sim_brk_showall		show all breakpoints
	sim_brk_test		test for breakpoint
	sim_brk_npc		PC has been changed

   Initialize breakpoint system.  If simulator has filled in sim_brk_types,
   allocate an initial breakpoint table, otherwise, allocate a minimal table.
*/

t_stat sim_brk_init (void)
{
if (sim_brk_types) sim_brk_lnt = SIM_BRK_INILNT;
else sim_brk_lnt = 1;
sim_brk_tab = calloc (sim_brk_lnt, sizeof (BRKTAB));
if (sim_brk_tab == NULL) return SCPE_MEM;
sim_brk_ent = sim_brk_ins = 0;
return SCPE_OK;
}

/* Search for a breakpoint in the sorted breakpoint table */

BRKTAB *sim_brk_fnd (t_addr loc)
{
int32 lo, hi, p;
BRKTAB *bp;

if (sim_brk_ent == 0) {					/* table empty? */
	sim_brk_ins = 0;				/* insrt at head */
	return NULL;  }					/* sch fails */
lo = 0;							/* initial bounds */
hi = sim_brk_ent - 1;
do {	p = (lo + hi) >> 1;				/* probe */
	bp = sim_brk_tab + p;				/* table addr */
	if (loc == bp->addr) return bp;			/* match? */
	else if (loc < bp->addr) hi = p - 1;		/* go down? p is upper */
	else lo = p + 1;  }				/* go up? p is lower */
while (lo <= hi);
if (loc < bp->addr) sim_brk_ins = p;			/* insrt before or */
else sim_brk_ins = p + 1;				/* after last sch */
return NULL;
}

/* Insert a breakpoint */

BRKTAB *sim_brk_new (t_addr loc)
{
BRKTAB *bp;

if (sim_brk_ins < 0) return NULL;
if (sim_brk_ent >= sim_brk_lnt) return NULL;
if (sim_brk_ins != sim_brk_ent) {			/* move needed? */
	for (bp = sim_brk_tab + sim_brk_ent;
	     bp > sim_brk_tab + sim_brk_ins; bp--)
		*bp = *(bp - 1);  }
bp = sim_brk_tab + sim_brk_ins;
bp->addr = loc;
bp->typ = 0;
bp->cnt = 0;
bp->act = NULL;
sim_brk_ent = sim_brk_ent + 1;
return bp;
}

/* Set a breakpoint of type sw */

t_stat sim_brk_set (t_addr loc, int32 sw, int32 ncnt)
{
BRKTAB *bp;

if (sw == 0) sw = sim_brk_dflt;
if ((sim_brk_types & sw) == 0) return SCPE_NOFNC;
bp = sim_brk_fnd (loc);
if (!bp) bp = sim_brk_new (loc);
if (!bp) return SCPE_MEM;
bp->typ = sw;
bp->cnt = ncnt;
sim_brk_summ = sim_brk_summ | sw;
return SCPE_OK;
}

/* Clear a breakpoint */

t_stat sim_brk_clr (t_addr loc, int32 sw)
{
BRKTAB *bp = sim_brk_fnd (loc);

if (!bp) return SCPE_OK;
if (sw == 0) sw = SIM_BRK_ALLTYP;
bp->typ = bp->typ & ~sw;
if (bp->typ) return SCPE_OK;
for ( ; bp < (sim_brk_tab + sim_brk_ent - 1); bp++)
	*bp = *(bp + 1);
sim_brk_ent = sim_brk_ent - 1;
sim_brk_summ = 0;
for (bp = sim_brk_tab; bp < (sim_brk_tab + sim_brk_ent); bp++)
	sim_brk_summ = sim_brk_summ | bp->typ;
return SCPE_OK;
}

/* Clear all breakpoints */

t_stat sim_brk_clrall (int32 sw)
{
BRKTAB *bp;

if (sw == 0) sw = SIM_BRK_ALLTYP;
if ((sim_brk_summ & ~sw) == 0) sim_brk_ent = sim_brk_summ = 0;
else {	for (bp = sim_brk_tab; bp < (sim_brk_tab + sim_brk_ent); bp++) {
	if (bp->typ & sw) sim_brk_clr (bp->addr, sw);  }  }
return SCPE_OK;
}

/* Show a breakpoint */

t_stat sim_brk_show (FILE *st, t_addr loc, int32 sw)
{
BRKTAB *bp = sim_brk_fnd (loc);
DEVICE *dptr;
int32 i, any;

if (sw == 0) sw = SIM_BRK_ALLTYP;
if (!bp || (!(bp->typ & sw))) return SCPE_OK;
dptr = sim_devices[0];
if (dptr == NULL) return SCPE_OK;
fprint_val (st, loc, dptr->aradix, dptr->awidth, PV_LEFT);
fprintf (st, ":\t");
for (i = any = 0; i < 26; i++) {
	if ((bp->typ >> i) & 1) {
		if (any) fprintf (st, ", ");
		fputc (i + 'A', st);
		any = 1;  }  }
if (bp->cnt > 0) fprintf (st, " [%d]", bp->cnt);
fprintf (st, "\n");
return SCPE_OK;
}

/* Show all breakpoints */

t_stat sim_brk_showall (FILE *st, int32 sw)
{
BRKTAB *bp;

if (sw == 0) sw = SIM_BRK_ALLTYP;
for (bp = sim_brk_tab; bp < (sim_brk_tab + sim_brk_ent); bp++) {
	if (bp->typ & sw) sim_brk_show (st, bp->addr, sw);  }
return SCPE_OK;
}

/* Test for breakpoint */

t_bool sim_brk_test (t_addr loc, int32 btyp)
{
BRKTAB *bp;

if ((bp = sim_brk_fnd (loc)) &&
    (btyp & bp->typ) &&
    (!sim_brk_pend || (loc != sim_brk_ploc)) &&
    (--(bp->cnt) <= 0)) {
	bp->cnt = 0;
	sim_brk_ploc = loc;
	sim_brk_pend = TRUE;
	return TRUE;  }
sim_brk_pend = FALSE;
return FALSE;
}

/* New PC */

void sim_brk_npc (void)
{
sim_brk_pend = FALSE;
return;
}

/* Telnet console package.

   The console terminal can be attached to the controlling window
   or to a Telnet connection.  If attached to a Telnet connection,
   the console is described by internal terminal multiplexor
   sim_con_tmxr and internal terminal line description sim_con_ldsc.
*/

/* Set console to Telnet port */

t_stat set_telnet (int32 flg, char *cptr)
{
if (*cptr == 0) return SCPE_2FARG;			/* too few arguments? */
if (sim_con_tmxr.master) return SCPE_ALATT;		/* already open? */
return tmxr_open_master (&sim_con_tmxr, cptr);		/* open master socket */
}

/* Close console Telnet port */

t_stat set_notelnet (int32 flag, char *cptr)
{
if (cptr && (*cptr != 0)) return SCPE_2MARG;		/* too many arguments? */
if (sim_con_tmxr.master == 0) return SCPE_OK;		/* ignore if already closed */
return tmxr_close_master (&sim_con_tmxr);		/* close master socket */
}

/* Show console Telnet status */

t_stat show_telnet (FILE *st, int32 flag, char *cptr)
{
if (cptr && (*cptr != 0)) return SCPE_2MARG;
if (sim_con_tmxr.master == 0)
	fprintf (st, "Connected to console window\n");
else if (sim_con_ldsc.conn == 0)
	fprintf (st, "Listening on port %d\n", sim_con_tmxr.port);
else {	fprintf (st, "Listening on port %d, connected to socket %d\n",
		sim_con_tmxr.port, sim_con_ldsc.conn);
	tmxr_fconns (st, &sim_con_ldsc, -1);
	tmxr_fstats (st, &sim_con_ldsc, -1);  }
return SCPE_OK;
}

/* Check connection before executing */

t_stat sim_check_console (int32 sec)
{
int32 c, i;

if (sim_con_tmxr.master == 0) return SCPE_OK;		/* not Telnet? done */
if (sim_con_ldsc.conn) {				/* connected? */
	tmxr_poll_rx (&sim_con_tmxr);			/* poll (check disconn) */
	if (sim_con_ldsc.conn) return SCPE_OK;  }	/* still connected? */
for (i = 0; i < sec; i++) {				/* loop */
	if (tmxr_poll_conn (&sim_con_tmxr) >= 0) {	/* poll connect */
		sim_con_ldsc.rcve = 1;			/* rcv enabled */
		if (i) {				/* if delayed */
			printf ("Running\n");		/* print transition */
			fflush (stdout);  }
		return SCPE_OK;  }			/* ready to proceed */
	c = sim_os_poll_kbd ();				/* check for stop char */
	if ((c == SCPE_STOP) || stop_cpu) return SCPE_STOP;
	if ((i % 10) == 0) {				/* Status every 10 sec */
		printf ("Waiting for console Telnet connnection\n");
		fflush (stdout);  }
	sim_os_sleep (1);				/* wait 1 second */
	}
return SCPE_TTMO;					/* timed out */
}

/* Poll for character */

t_stat sim_poll_kbd (void)
{
int32 c;

c = sim_os_poll_kbd ();					/* get character */
if ((c == SCPE_STOP) || (sim_con_tmxr.master == 0))	/* ^E or not Telnet? */
	return c;					/* in-window */
if (sim_con_ldsc.conn == 0) return SCPE_LOST;		/* no Telnet conn? */
tmxr_poll_rx (&sim_con_tmxr);				/* poll for input */
if (c = tmxr_getc_ln (&sim_con_ldsc))			/* any char? */ 
	return (c & 0377) | SCPE_KFLAG;
return SCPE_OK;
}

/* Output character */

t_stat sim_putchar (int32 c)
{
if (sim_con_tmxr.master == 0)				/* not Telnet? */
	return sim_os_putchar (c);			/* in-window version */
if (sim_con_ldsc.conn == 0) return SCPE_LOST;		/* no Telnet conn? */
tmxr_putc_ln (&sim_con_ldsc, c);			/* output char */
tmxr_poll_tx (&sim_con_tmxr);				/* poll xmt */
return SCPE_OK;
}
