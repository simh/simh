/* sim_console.c: simulator console I/O library

   Copyright (c) 1993-2014, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   02-Jan-14    RMS     Added tab stop routines
   18-Mar-12    RMS     Removed unused reference to sim_switches (Dave Bryan)
   07-Dec-11    MP      Added sim_ttisatty to support reasonable behaviour (i.e. 
                        avoid in infinite loop) in the main command input
                        loop when EOF is detected and input is coming from 
                        a file (or a null device: /dev/null or NUL:) This may
                        happen when a simulator is running in a background 
                        process.
   17-Apr-11    MP      Cleaned up to support running in a background/detached
                        process
   20-Jan-11    MP      Fixed support for BREAK key on Windows to account 
                        for/ignore other keyboard Meta characters.
   18-Jan-11    MP      Added log file reference count support
   17-Jan-11    MP      Added support for a "Buffered" behaviors which include:
                        - If Buffering is enabled and Telnet is enabled, a
                          telnet connection is not required for simulator 
                          operation (instruction execution).
                        - If Buffering is enabled, all console output is 
                          written to the buffer at all times (deleting the
                          oldest buffer contents on overflow).
                        - when a connection is established on the console 
                          telnet port, the whole contents of the Buffer is
                          presented on the telnet session and connection 
                          will then proceed as if the connection had always
                          been there.
                        This concept allows a simulator to run in the background
                        and when needed a console session to be established.  
                        The "when needed" case usually will be interested in 
                        what already happened before looking to address what 
                        to do, hence the buffer contents being presented.
   28-Dec-10    MP      Added support for BREAK key on Windows
   30-Sep-06    RMS     Fixed non-printable characters in KSR mode
   22-Jun-06    RMS     Implemented SET/SHOW PCHAR
   31-May-06    JDB     Fixed bug if SET CONSOLE DEBUG with no argument
   22-Nov-05    RMS     Added central input/output conversion support
   05-Nov-04    RMS     Moved SET/SHOW DEBUG under CONSOLE hierarchy
   28-Oct-04    JDB     Fixed SET CONSOLE to allow comma-separated parameters
   20-Aug-04    RMS     Added OS/2 EMX fixes (Holger Veit)
   14-Jul-04    RMS     Revised Windows console code (Dave Bryan)
   28-May-04    RMS     Added SET/SHOW CONSOLE
                RMS     Added break, delete character maps
   02-Jan-04    RMS     Removed timer routines, added Telnet console routines
                RMS     Moved console logging to OS-independent code
   25-Apr-03    RMS     Added long seek support (Mark Pizzolato)
                        Added Unix priority control (Mark Pizzolato)
   24-Sep-02    RMS     Removed VT support, added Telnet console support
                        Added CGI support (Brian Knittel)
                        Added MacOS sleep (Peter Schorn)
   14-Jul-02    RMS     Added Windows priority control (Mark Pizzolato)
   20-May-02    RMS     Added Windows VT support (Fischer Franz)
   01-Feb-02    RMS     Added VAX fix (Robert Alan Byer)
   19-Sep-01    RMS     More MacOS changes
   31-Aug-01    RMS     Changed int64 to t_int64 for Windoze
   20-Jul-01    RMS     Added MacOS support (Louis Chretien, Peter Schorn, Ben Supnik)
   15-May-01    RMS     Added logging support
   05-Mar-01    RMS     Added clock calibration support
   08-Dec-00    BKR     Added OS/2 support (Bruce Ray)
   18-Aug-98    RMS     Added BeOS support
   13-Oct-97    RMS     Added NetBSD terminal support
   25-Jan-97    RMS     Added POSIX terminal I/O support
   02-Jan-97    RMS     Fixed bug in sim_poll_kbd

   This module implements the following routines to support terminal and 
   Remote Console I/O:

   sim_poll_kbd                 poll for keyboard input
   sim_putchar                  output character to console
   sim_putchar_s                output character to console, stall if congested
   sim_set_console              set console parameters
   sim_show_console             show console parameters
   sim_set_remote_console       set remote console parameters
   sim_show_remote_console      show remote console parameters
   sim_set_cons_buff            set console buffered
   sim_set_cons_unbuff          set console unbuffered
   sim_set_cons_log             set console log
   sim_set_cons_nolog           set console nolog
   sim_show_cons_buff           show console buffered
   sim_show_cons_log            show console log
   sim_tt_inpcvt                convert input character per mode
   sim_tt_outcvt                convert output character per mode
   sim_cons_get_send            get console send structure address
   sim_cons_get_expect          get console expect structure address
   sim_show_cons_send_input     show pending input data
   sim_show_cons_expect         show expect rules and state
   sim_ttinit                   called once to get initial terminal state
   sim_ttrun                    called to put terminal into run state
   sim_ttcmd                    called to return terminal to command state
   sim_ttclose                  called once before the simulator exits
   sim_ttisatty                 called to determine if running interactively
   sim_os_poll_kbd              poll for keyboard input
   sim_os_putchar               output character to console
   sim_set_noconsole_port       Enable automatic WRU console polling
   sim_set_stable_registers_state Declare that all registers are always stable


   The first group is OS-independent; the second group is OS-dependent.

   The following routines are exposed but deprecated:

   sim_set_telnet               set console to Telnet port
   sim_set_notelnet             close console Telnet port
   sim_show_telnet              show console status
*/

#include "sim_defs.h"
#include "sim_tmxr.h"
#include "sim_serial.h"
#include "sim_timer.h"
#include <ctype.h>
#include <math.h>

#ifdef __HAIKU__
#define nice(n) ({})
#endif

#ifndef MIN
#define MIN(a,b)  (((a) <= (b)) ? (a) : (b))
#endif

/* Forward Declaraations of Platform specific routines */

static t_stat sim_os_poll_kbd (void);
static t_bool sim_os_poll_kbd_ready (int ms_timeout);
static t_stat sim_os_putchar (int32 out);
static t_stat sim_os_ttinit (void);
static t_stat sim_os_ttrun (void);
static t_stat sim_os_ttcmd (void);
static t_stat sim_os_ttclose (void);
static t_bool sim_os_fd_isatty (int fd);

static t_stat sim_set_rem_telnet (int32 flag, CONST char *cptr);
static t_stat sim_set_rem_bufsize (int32 flag, CONST char *cptr);
static t_stat sim_set_rem_connections (int32 flag, CONST char *cptr);
static t_stat sim_set_rem_timeout (int32 flag, CONST char *cptr);
static t_stat sim_set_rem_master (int32 flag, CONST char *cptr);

/* Deprecated CONSOLE HALT, CONSOLE RESPONSE and CONSOLE DELAY support */
static t_stat sim_set_halt (int32 flag, CONST char *cptr);
static t_stat sim_set_response (int32 flag, CONST char *cptr);
static t_stat sim_set_delay (int32 flag, CONST char *cptr);


#define KMAP_WRU        0
#define KMAP_BRK        1
#define KMAP_DEL        2
#define KMAP_DBGINT     3
#define KMAP_MASK       0377
#define KMAP_NZ         0400

int32 sim_int_char = 005;                               /* interrupt character */
int32 sim_dbg_int_char = 0;                             /* SIGINT char under debugger */
static t_bool sigint_message_issued = FALSE;
int32 sim_brk_char = 000;                               /* break character */
int32 sim_tt_pchar = 0x00002780;
#if defined (_WIN32) || defined (__OS2__) || (defined (__MWERKS__) && defined (macintosh))
int32 sim_del_char = '\b';                              /* delete character */
#else
int32 sim_del_char = 0177;
#endif
t_bool sim_signaled_int_char                            /* WRU character detected by signal while running */
#if defined (_WIN32) || defined (_VMS) || defined (__CYGWIN__) || (defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL))
                             = FALSE;
#else
                             = TRUE;
#endif
uint32 sim_last_poll_kbd_time;                          /* time when sim_poll_kbd was called */
extern TMLN *sim_oline;                                 /* global output socket */
static uint32 sim_con_pos;                              /* console character output count */

static t_stat sim_con_poll_svc (UNIT *uptr);                /* console connection poll routine */
static t_stat sim_con_reset (DEVICE *dptr);                 /* console reset routine */
static t_stat sim_con_attach (UNIT *uptr, CONST char *ptr); /* console attach routine (save,restore) */
static t_stat sim_con_detach (UNIT *uptr);                  /* console detach routine (save,restore) */

UNIT sim_con_units[2] = {{ UDATA (&sim_con_poll_svc, UNIT_ATTABLE, 0)}}; /* console connection unit */
#define sim_con_unit sim_con_units[0]

/* debugging bitmaps */
#define DBG_TRC  TMXR_DBG_TRC                           /* trace routine calls */
#define DBG_XMT  TMXR_DBG_XMT                           /* display Transmitted Data */
#define DBG_RCV  TMXR_DBG_RCV                           /* display Received Data */
#define DBG_RET  TMXR_DBG_RET                           /* display Returned Received Data */
#define DBG_ASY  TMXR_DBG_ASY                           /* asynchronous thread activity */
#define DBG_CON  TMXR_DBG_CON                           /* display connection activity */
#define DBG_EXP  0x00000001                             /* Expect match activity */
#define DBG_SND  0x00000002                             /* Send (Inject) data activity */

static DEBTAB sim_con_debug[] = {
  {"TRC",    DBG_TRC, "routine calls"},
  {"XMT",    DBG_XMT, "Transmitted Data"},
  {"RCV",    DBG_RCV, "Received Data"},
  {"RET",    DBG_RET, "Returned Received Data"},
  {"ASY",    DBG_ASY, "asynchronous activity"},
  {"CON",    DBG_CON, "connection activity"},
  {"EXP",    DBG_EXP, "Expect match activity"},
  {"SND",    DBG_SND, "Send (Inject) data activity"},
  {0}
};

static REG sim_con_reg[] = {
    { ORDATAD (WRU,         sim_int_char,  8, "interrupt character") },
    { ORDATAD (BRK,         sim_brk_char,  8, "break character") },
    { ORDATAD (DEL,         sim_del_char,  8, "delete character ") },
    { ORDATAD (PCHAR,       sim_tt_pchar, 32, "printable character mask") },
    { DRDATAD (CONSOLE_POS, sim_con_pos,  32, "character output count") },
  { 0 },
};

static MTAB sim_con_mod[] = {
  { 0 },
};

static const char *sim_con_telnet_description (DEVICE *dptr)
{
return "Console telnet support";
}

DEVICE sim_con_telnet = {
    "CON-TELNET", sim_con_units, sim_con_reg, sim_con_mod, 
    2, 0, 0, 0, 0, 0, 
    NULL, NULL, sim_con_reset, NULL, sim_con_attach, sim_con_detach, 
    NULL, DEV_DEBUG | DEV_NOSAVE, 0, sim_con_debug,
    NULL, NULL, NULL, NULL, NULL, sim_con_telnet_description};
TMLN sim_con_ldsc = { 0 };                                          /* console line descr */
TMXR sim_con_tmxr = { 1, 0, 0, &sim_con_ldsc, NULL, &sim_con_telnet };/* console line mux */


SEND sim_con_send = {0, &sim_con_telnet, DBG_SND};
EXPECT sim_con_expect = {&sim_con_telnet, DBG_EXP};

static t_bool sim_con_console_port = TRUE;

/* Enable automatic WRU console polling */

t_stat sim_set_noconsole_port (void)
{
sim_con_console_port = FALSE;
return SCPE_OK;
}

static t_bool sim_con_stable_registers = FALSE;

/* Enable automatic WRU console polling */

t_stat sim_set_stable_registers_state (void)
{
sim_con_stable_registers = TRUE;
return SCPE_OK;
}

/* Unit service for console connection polling */

static t_stat sim_con_poll_svc (UNIT *uptr)
{
if ((sim_con_tmxr.master == 0) &&                       /* not Telnet and not serial and not WRU polling? */
    (sim_con_ldsc.serport == 0) &&
    (sim_con_console_port))
    return SCPE_OK;                                     /* done */
if (tmxr_poll_conn (&sim_con_tmxr) >= 0)                /* poll connect */
    sim_con_ldsc.rcve = 1;                              /* rcv enabled */
sim_activate_after(uptr, 1000000);                      /* check again in 1 second */
if (!sim_con_console_port)                              /* WRU poll needed */
    sim_poll_kbd();                                     /* sets global stop_cpu when WRU received */
if (sim_con_ldsc.conn)
    tmxr_send_buffered_data (&sim_con_ldsc);            /* try to flush any buffered data */
return SCPE_OK;
}

static t_stat sim_con_reset (DEVICE *dptr)
{
dptr->units[1].flags = UNIT_DIS;
return sim_con_poll_svc (&dptr->units[0]);              /* establish polling as needed */
}

/* Console Attach/Detach - only used indirectly in restore */

static t_stat sim_con_attach (UNIT *uptr, CONST char *ptr)
{
return tmxr_attach (&sim_con_tmxr, &sim_con_unit, ptr);
}

static t_stat sim_con_detach (UNIT *uptr)
{
return sim_set_notelnet (0, NULL);
}

/* Set/show data structures */

static CTAB set_con_tab[] = {
    { "WRU",     &sim_set_kmap, KMAP_WRU    | KMAP_NZ },
    { "BRK",     &sim_set_kmap, KMAP_BRK },
    { "DEL",     &sim_set_kmap, KMAP_DEL    | KMAP_NZ },
    { "DBGINT",  &sim_set_kmap, KMAP_DBGINT | KMAP_NZ },
    { "PCHAR",   &sim_set_pchar, 0 },
    { "SPEED",   &sim_set_cons_speed, 0 },
    { "TELNET",  &sim_set_telnet, 0 },
    { "NOTELNET", &sim_set_notelnet, 0 },
    { "SERIAL",  &sim_set_serial, 0 },
    { "NOSERIAL", &sim_set_noserial, 0 },
    { "LOG",     &sim_set_logon, 0 },
    { "NOLOG",   &sim_set_logoff, 0 },
    { "DEBUG",   &sim_set_debon, 0 },
    { "NODEBUG", &sim_set_deboff, 0 },
#define CMD_WANTSTR     0100000
    { "HALT", &sim_set_halt, 1 | CMD_WANTSTR },
    { "NOHALT", &sim_set_halt, 0 },
    { "DELAY", &sim_set_delay, 0 },
    { "RESPONSE", &sim_set_response, 1 | CMD_WANTSTR },
    { "NORESPONSE", &sim_set_response, 0 },
    { NULL, NULL, 0 }
    };

static CTAB set_rem_con_tab[] = {
    { "CONNECTIONS", &sim_set_rem_connections, 0 },
    { "TELNET", &sim_set_rem_telnet, 1 },
    { "BUFFERSIZE", &sim_set_rem_bufsize, 1 },
    { "NOTELNET", &sim_set_rem_telnet, 0 },
    { "TIMEOUT", &sim_set_rem_timeout, 0 },
    { "MASTER", &sim_set_rem_master, 1 },
    { "NOMASTER", &sim_set_rem_master, 0 },
    { NULL, NULL, 0 }
    };

static SHTAB show_con_tab[] = {
    { "WRU", &sim_show_kmap, KMAP_WRU },
    { "BRK", &sim_show_kmap, KMAP_BRK },
    { "DEL", &sim_show_kmap, KMAP_DEL },
#if (defined(__GNUC__) && !defined(__OPTIMIZE__) && !defined(_WIN32))       /* Debug build? */
    { "DBGINT", &sim_show_kmap, KMAP_DBGINT },
#endif
    { "PCHAR", &sim_show_pchar, 0 },
    { "SPEED", &sim_show_cons_speed, 0 },
    { "LOG", &sim_show_cons_log, 0 },
    { "TELNET", &sim_show_telnet, 0 },
    { "DEBUG", &sim_show_cons_debug, 0 },
    { "BUFFERED", &sim_show_cons_buff, 0 },
    { "EXPECT", &sim_show_cons_expect, 0 },
    { "HALT", &sim_show_cons_expect, -1 },
    { "INPUT", &sim_show_cons_send_input, 0 },
    { "RESPONSE", &sim_show_cons_send_input, -1 },
    { "DELAY", &sim_show_cons_expect, -1 },
    { NULL, NULL, 0 }
    };

static CTAB set_con_telnet_tab[] = {
    { "LOG", &sim_set_cons_log, 0 },
    { "NOLOG", &sim_set_cons_nolog, 0 },
    { "BUFFERED", &sim_set_cons_buff, 0 },
    { "NOBUFFERED", &sim_set_cons_unbuff, 0 },
    { "UNBUFFERED", &sim_set_cons_unbuff, 0 },
    { NULL, NULL, 0 }
    };

static CTAB set_con_serial_tab[] = {
    { "LOG", &sim_set_cons_log, 0 },
    { "NOLOG", &sim_set_cons_nolog, 0 },
    { NULL, NULL, 0 }
    };

static int32 *cons_kmap[] = {
    &sim_int_char,
    &sim_brk_char,
    &sim_del_char,
    &sim_dbg_int_char
    };

/* Console I/O package.

   The console terminal can be attached to the controlling window
   or to a Telnet connection.  If attached to a Telnet connection,
   the console is described by internal terminal multiplexor
   sim_con_tmxr and internal terminal line description sim_con_ldsc.
*/

/* SET CONSOLE command */

t_stat sim_set_console (int32 flag, CONST char *cptr)
{
char *cvptr, gbuf[CBUFSIZE];
CTAB *ctptr;
t_stat r;

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_2FARG;
while (*cptr != 0) {                                    /* do all mods */
    cptr = get_glyph_nc (cptr, gbuf, ',');              /* get modifier */
    if ((cvptr = strchr (gbuf, '=')))                   /* = value? */
        *cvptr++ = 0;
    get_glyph (gbuf, gbuf, 0);                          /* modifier to UC */
    if ((ctptr = find_ctab (set_con_tab, gbuf))) {      /* match? */
        r = ctptr->action (ctptr->arg, cvptr);          /* do the rest */
        if (r != SCPE_OK)
            return r;
        }
    else return SCPE_NOPARAM;
    }
return SCPE_OK;
}

/* SHOW CONSOLE command */

t_stat sim_show_console (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
SHTAB *shptr;
int32 i;

if (*cptr == 0) {                                       /* show all */
    for (i = 0; show_con_tab[i].name; i++)
        if (show_con_tab[i].arg != -1)
            show_con_tab[i].action (st, dptr, uptr, show_con_tab[i].arg, cptr);
    return SCPE_OK;
    }
while (*cptr != 0) {
    cptr = get_glyph (cptr, gbuf, ',');                 /* get modifier */
    if ((shptr = find_shtab (show_con_tab, gbuf)))
        shptr->action (st, dptr, uptr, shptr->arg, NULL);
    else return SCPE_NOPARAM;
    }
return SCPE_OK;
}

#define MAX_REMOTE_SESSIONS 40                          /* Arbitrary Session Limit */

t_stat sim_rem_con_poll_svc (UNIT *uptr);               /* remote console connection poll routine */
t_stat sim_rem_con_data_svc (UNIT *uptr);               /* remote console connection data routine */
t_stat sim_rem_con_repeat_svc (UNIT *uptr);             /* remote auto repeat command console timing routine */
t_stat sim_rem_con_smp_collect_svc (UNIT *uptr);        /* remote remote register data sampling routine */
t_stat sim_rem_con_reset (DEVICE *dptr);                /* remote console reset routine */
#define rem_con_poll_unit (&sim_remote_console.units[0])
#define rem_con_data_unit (&sim_remote_console.units[1])
#define REM_CON_BASE_UNITS 2
#define rem_con_repeat_units (&sim_remote_console.units[REM_CON_BASE_UNITS])
#define rem_con_smp_smpl_units (&sim_remote_console.units[REM_CON_BASE_UNITS+sim_rem_con_tmxr.lines])

#define DBG_MOD  0x00000004                             /* Remote Console Mode activities */
#define DBG_REP  0x00000008                             /* Remote Console Repeat activities */
#define DBG_SAM  0x00000010                             /* Remote Console Sample activities */
#define DBG_CMD  0x00000020                             /* Remote Console Command activities */

DEBTAB sim_rem_con_debug[] = {
  {"TRC",    DBG_TRC, "routine calls"},
  {"XMT",    DBG_XMT, "Transmitted Data"},
  {"RCV",    DBG_RCV, "Received Data"},
  {"CON",    DBG_CON, "connection activity"},
  {"CMD",    DBG_CMD, "Remote Console Command activity"},
  {"MODE",   DBG_MOD, "Remote Console Mode activity"},
  {"REPEAT", DBG_REP, "Remote Console Repeat activity"},
  {"SAMPLE", DBG_SAM, "Remote Console Sample activity"},
  {0}
};

MTAB sim_rem_con_mod[] = {
  { 0 },
};

static const char *sim_rem_con_description (DEVICE *dptr)
{
return "Remote Console Facility";
}

DEVICE sim_remote_console = {
    "REM-CON", NULL, NULL, sim_rem_con_mod, 
    0, 0, 0, 0, 0, 0, 
    NULL, NULL, sim_rem_con_reset, NULL, NULL, NULL, 
    NULL, DEV_DEBUG | DEV_NOSAVE, 0, sim_rem_con_debug,
    NULL, NULL, NULL, NULL, NULL, sim_rem_con_description};

typedef struct BITSAMPLE BITSAMPLE;
struct BITSAMPLE {
    int             tot;            /* total of all values */
    int             ptr;            /* pointer to next value cell */
    int             depth;          /* number of values */
    int             *vals;          /* values */
    };
typedef struct BITSAMPLE_REG BITSAMPLE_REG;
struct BITSAMPLE_REG {
    REG             *reg;           /* Register to be sampled */
    uint32           idx;           /* Register index */
    t_bool          indirect;       /* Register value points at memory */
    DEVICE          *dptr;          /* Device register is part of */
    UNIT            *uptr;          /* Unit Register is related to */
    uint32          width;          /* number of bits to sample */
    BITSAMPLE       *bits;
    };
typedef struct REMOTE REMOTE;
struct REMOTE {
    int32           buf_size;
    int32           buf_ptr;
    char            *buf;
    char            *act_buf;
    size_t          act_buf_size;
    char            *act;
    t_bool          single_mode;
    uint32          read_timeout;
    int             line;                   /* remote console line number */
    TMLN            *lp;                    /* mux line/socket for remote session */
    UNIT            *uptr;                  /* remote console unit */
    uint32          repeat_interval;        /* usecs between repeat execution */
    t_bool          repeat_pending;         /* repeat delivery pending */
    char            *repeat_action;         /* command(s) to repeatedly execute */
    int             smp_sample_interval;    /* cycles between samples */
    int             smp_sample_dither_pct;  /* dithering of cycles interval */
    uint32          smp_reg_count;          /* sample register count */
    BITSAMPLE_REG   *smp_regs;              /* registers being sampled */
    };
REMOTE *sim_rem_consoles = NULL;

static TMXR sim_rem_con_tmxr = { 0, 0, 0, NULL, NULL, &sim_remote_console };/* remote console line mux */
static uint32 sim_rem_read_timeout = 30;    /* seconds before automatic continue */
static int32 sim_rem_active_number = -1;    /* -1 - not active, >= 0 is index of active console */
int32 sim_rem_cmd_active_line = -1;         /* step in progress on line # */
static CTAB *sim_rem_active_command = NULL; /* active command */
static char *sim_rem_command_buf;           /* active command buffer */
static t_bool sim_log_temp = FALSE;         /* temporary log file active */
static char sim_rem_con_temp_name[PATH_MAX+1];
static t_bool sim_rem_master_mode = FALSE;  /* Master Mode Enabled Flag */
static t_bool sim_rem_master_was_enabled = FALSE; /* Master was Enabled */
static t_bool sim_rem_master_was_connected = FALSE; /* Master Mode has been connected */
static t_offset sim_rem_cmd_log_start = 0;  /* Log File saved position */

static t_stat sim_rem_sample_output (FILE *st, int32 line)
{
REMOTE *rem = &sim_rem_consoles[line];
uint32 reg;

if (rem->smp_reg_count == 0) {
    fprintf (st, "Samples are not being collected\n");
    return SCPE_OK;
    }
for (reg = 0; reg < rem->smp_reg_count; reg++) {
    uint32 bit;

    if (rem->smp_regs[reg].reg->depth > 1)
        fprintf (st, "}%s %s[%d] %s %d:", rem->smp_regs[reg].dptr->name, rem->smp_regs[reg].reg->name, rem->smp_regs[reg].idx, rem->smp_regs[reg].indirect ? " -I" : "", rem->smp_regs[reg].bits[0].depth);
    else
        fprintf (st, "}%s %s%s %d:", rem->smp_regs[reg].dptr->name, rem->smp_regs[reg].reg->name, rem->smp_regs[reg].indirect ? " -I" : "", rem->smp_regs[reg].bits[0].depth);
    for (bit = 0; bit < rem->smp_regs[reg].width; bit++)
        fprintf (st, "%s%d", (bit != 0) ? "," : "", rem->smp_regs[reg].bits[bit].tot);
    fprintf (st, "\n");
    }
return SCPE_OK;
}


/* SET REMOTE CONSOLE command */

t_stat sim_set_remote_console (int32 flag, CONST char *cptr)
{
char *cvptr, gbuf[CBUFSIZE];
CTAB *ctptr;
t_stat r;

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_2FARG;
while (*cptr != 0) {                                    /* do all mods */
    cptr = get_glyph_nc (cptr, gbuf, ',');              /* get modifier */
    if ((cvptr = strchr (gbuf, '=')))                   /* = value? */
        *cvptr++ = 0;
    get_glyph (gbuf, gbuf, 0);                          /* modifier to UC */
    if ((ctptr = find_ctab (set_rem_con_tab, gbuf))) {  /* match? */
        r = ctptr->action (ctptr->arg, cvptr);          /* do the rest */
        if (r != SCPE_OK)
            return r;
        }
    else return SCPE_NOPARAM;
    }
return SCPE_OK;
}

/* SHOW REMOTE CONSOLE command */

t_stat sim_show_remote_console (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
int32 i, connections;
REMOTE *rem;

if (*cptr != 0)
    return SCPE_NOPARAM;
if (sim_rem_active_number >= 0) {
    if (sim_rem_master_mode && (sim_rem_active_number == 0))
        fprintf (st, "Running from Master Mode Remote Console Connection\n");
    else
        fprintf (st, "Running from Remote Console Connection %d\n", sim_rem_active_number);
    }
if (sim_rem_con_tmxr.lines > 1)
    fprintf (st, "Remote Console Input Connections from %d sources are supported concurrently\n", sim_rem_con_tmxr.lines);
if (sim_rem_read_timeout)
    fprintf (st, "Remote Console Input automatically continues after %d seconds\n", sim_rem_read_timeout);
if (!sim_rem_con_tmxr.master)
    fprintf (st, "Remote Console Command input is disabled\n");
else {
    fprintf (st, "Remote Console Command Input listening on TCP port: %s\n", rem_con_poll_unit->filename);
    fprintf (st, "Remote Console Per Command Output buffer size:      %d bytes\n", sim_rem_con_tmxr.buffered);
    }
for (i=connections=0; i<sim_rem_con_tmxr.lines; i++) {
    rem = &sim_rem_consoles[i];
    if (!rem->lp->conn)
        continue;
    ++connections;
    if (connections == 1)
        fprintf (st, "Remote Console Connections:\n");
    tmxr_fconns (st, rem->lp, i);
    if (rem->read_timeout != sim_rem_read_timeout) {
        if (rem->read_timeout)
            fprintf (st, "Remote Console Input on connection %d automatically continues after %d seconds\n", i, rem->read_timeout);
        else
            fprintf (st, "Remote Console Input on connection %d does not continue automatically\n", i);
        }
    if (rem->repeat_action) {
        fprintf (st, "The Command: %s\n", rem->repeat_action);
        fprintf (st, "    is repeated every %s\n", sim_fmt_secs (rem->repeat_interval / 1000000.0));
        }
    if (rem->smp_reg_count) {
        uint32 reg;
        DEVICE *dptr = NULL;

        if (rem->smp_sample_dither_pct)
            fprintf (st, "Register Bit Sampling is occurring every %d %s (dithered %d percent)\n", rem->smp_sample_interval, sim_vm_interval_units, rem->smp_sample_dither_pct);
        else
            fprintf (st, "Register Bit Sampling is occurring every %d %s\n", rem->smp_sample_interval, sim_vm_interval_units);
        fprintf (st, " Registers being sampled are: ");
        for (reg = 0; reg < rem->smp_reg_count; reg++) {
            if (rem->smp_regs[reg].indirect)
                fprintf (st, " indirect ");
            if (dptr != rem->smp_regs[reg].dptr)
                fprintf (st, "%s ", rem->smp_regs[reg].dptr->name);
            if (rem->smp_regs[reg].reg->depth > 1)
                fprintf (st, "%s[%d]%s", rem->smp_regs[reg].reg->name, rem->smp_regs[reg].idx, ((reg + 1) < rem->smp_reg_count) ? ", " : "");
            else
                fprintf (st, "%s%s", rem->smp_regs[reg].reg->name, ((reg + 1) < rem->smp_reg_count) ? ", " : "");
            dptr = rem->smp_regs[reg].dptr;
            }
        fprintf (st, "\n");
        if (sim_switches & SWMASK ('D'))
            sim_rem_sample_output (st, rem->line);
        }
    }
return SCPE_OK;
}

/* Unit service for remote console connection polling */

t_stat sim_rem_con_poll_svc (UNIT *uptr)
{
int32 c;

c = tmxr_poll_conn (&sim_rem_con_tmxr);
if (c >= 0) {                                           /* poll connect */
    REMOTE *rem = &sim_rem_consoles[c];
    TMLN *lp = rem->lp;
    char wru_name[8];

    sim_activate_after(rem_con_data_unit, 1000000);     /* start data poll after 1 second */
    lp->rcve = 1;                                       /* rcv enabled */
    rem->buf_ptr = 0;                                   /* start with empty command buffer */
    rem->single_mode = TRUE;                            /* start in single command mode */
    rem->read_timeout = sim_rem_read_timeout;           /* Start with default timeout */
    if (isprint(sim_int_char&0xFF))
        sprintf(wru_name, "'%c'", sim_int_char&0xFF);
    else
        if (sim_int_char <= 26)
            sprintf(wru_name, "^%c", '@' + (sim_int_char&0xFF));
        else
            sprintf(wru_name, "'\\%03o'", sim_int_char&0xFF);
    tmxr_linemsgf (lp, "%s Remote Console\r\n"
                       "Enter single commands or to enter multiple command mode enter the %s character\r"
                       "%s",
                       sim_name, wru_name, 
                       ((sim_rem_master_mode && (c == 0)) ? "" : "\nSimulator Running..."));
    if (sim_rem_master_mode && (c == 0))                /* Master Mode session? */
        rem->single_mode = FALSE;                       /*  start in multi-command mode */
    tmxr_send_buffered_data (lp);                       /* flush buffered data */
    }
sim_activate_after(uptr, 1000000);                      /* check again in 1 second */
if (sim_con_ldsc.conn)
    tmxr_send_buffered_data (&sim_con_ldsc);            /* try to flush any buffered data */
return SCPE_OK;
}

static t_stat x_continue_cmd (int32 flag, CONST char *cptr)
{
return 1+SCPE_IERR;         /* This routine should never be called */
}

static t_stat x_repeat_cmd (int32 flag, CONST char *cptr)
{
return 2+SCPE_IERR;         /* This routine should never be called */
}

static t_stat x_collect_cmd (int32 flag, CONST char *cptr)
{
return 3+SCPE_IERR;         /* This routine should never be called */
}

static t_stat x_sampleout_cmd (int32 flag, CONST char *cptr)
{
return 4+SCPE_IERR;         /* This routine should never be called */
}

static t_stat x_execute_cmd (int32 flag, CONST char *cptr)
{
return 5+SCPE_IERR;         /* This routine should never be called */
}

static t_stat x_step_cmd (int32 flag, CONST char *cptr)
{
return 6+SCPE_IERR;         /* This routine should never be called */
}

static t_stat x_run_cmd (int32 flag, CONST char *cptr)
{
return 7+SCPE_IERR;         /* This routine should never be called */
}

static t_stat x_help_cmd (int32 flag, CONST char *cptr);

static CTAB allowed_remote_cmds[] = {
    { "EXAMINE",  &exdep_cmd,      EX_E },
    { "DEPOSIT",  &exdep_cmd,      EX_D },
    { "EVALUATE", &eval_cmd,          0 },
    { "ATTACH",   &attach_cmd,        0 },
    { "DETACH",   &detach_cmd,        0 },
    { "ASSIGN",   &assign_cmd,        0 },
    { "DEASSIGN", &deassign_cmd,      0 },
    { "CONTINUE", &x_continue_cmd,    0 },
    { "REPEAT",   &x_repeat_cmd,      0 },
    { "COLLECT",  &x_collect_cmd,     0 },
    { "SAMPLEOUT",&x_sampleout_cmd,   0 },
    { "PWD",      &pwd_cmd,           0 },
    { "SAVE",     &save_cmd,          0 },
    { "DIR",      &dir_cmd,           0 },
    { "LS",       &dir_cmd,           0 },
    { "ECHO",     &echo_cmd,          0 },
    { "ECHOF",    &echof_cmd,         0 },
    { "SET",      &set_cmd,           0 },
    { "SHOW",     &show_cmd,          0 },
    { "HELP",     &x_help_cmd,        0 },
    { NULL,       NULL }
    };

static CTAB allowed_master_remote_cmds[] = {
    { "EXAMINE",  &exdep_cmd,      EX_E },
    { "DEPOSIT",  &exdep_cmd,      EX_D },
    { "EVALUATE", &eval_cmd,          0 },
    { "ATTACH",   &attach_cmd,        0 },
    { "DETACH",   &detach_cmd,        0 },
    { "ASSIGN",   &assign_cmd,        0 },
    { "DEASSIGN", &deassign_cmd,      0 },
    { "CONTINUE", &x_continue_cmd,    0 },
    { "STEP",     &x_step_cmd,        0 },
    { "REPEAT",   &x_repeat_cmd,      0 },
    { "COLLECT",  &x_collect_cmd,     0 },
    { "SAMPLEOUT",&x_sampleout_cmd,   0 },
    { "EXECUTE",  &x_execute_cmd,     0 },
    { "PWD",      &pwd_cmd,           0 },
    { "SAVE",     &save_cmd,          0 },
    { "CD",       &set_default_cmd,   0 },
    { "DIR",      &dir_cmd,           0 },
    { "LS",       &dir_cmd,           0 },
    { "ECHO",     &echo_cmd,          0 },
    { "ECHOF",    &echof_cmd,         0 },
    { "SET",      &set_cmd,           0 },
    { "SHOW",     &show_cmd,          0 },
    { "HELP",     &x_help_cmd,        0 },
    { "EXIT",     &exit_cmd,          0 },
    { "QUIT",     &exit_cmd,          0 },
    { "RUN",      &x_run_cmd,    RU_RUN },
    { "GO",       &x_run_cmd,     RU_GO },
    { "BOOT",     &x_run_cmd,   RU_BOOT },
    { "BREAK",    &brk_cmd,      SSH_ST },
    { "NOBREAK",  &brk_cmd,      SSH_CL },
    { "EXPECT",   &expect_cmd,        1 },
    { "NOEXPECT", &expect_cmd,        0 },
    { "DEBUG",    &debug_cmd,         1 },
    { "NODEBUG",  &debug_cmd,         0 },
    { "SEND",     &send_cmd,          0 },
    { NULL,       NULL }
    };

static CTAB allowed_single_remote_cmds[] = {
    { "ATTACH",   &attach_cmd,        0 },
    { "DETACH",   &detach_cmd,        0 },
    { "EXAMINE",  &exdep_cmd,      EX_E },
    { "EVALUATE", &eval_cmd,          0 },
    { "REPEAT",   &x_repeat_cmd,      0 },
    { "COLLECT",  &x_collect_cmd,     0 },
    { "SAMPLEOUT",&x_sampleout_cmd,   0 },
    { "EXECUTE",  &x_execute_cmd,     0 },
    { "PWD",      &pwd_cmd,           0 },
    { "DIR",      &dir_cmd,           0 },
    { "LS",       &dir_cmd,           0 },
    { "ECHO",     &echo_cmd,          0 },
    { "ECHOF",    &echof_cmd,         0 },
    { "SHOW",     &show_cmd,          0 },
    { "DEBUG",    &debug_cmd,         1 },
    { "NODEBUG",  &debug_cmd,         0 },
    { "HELP",     &x_help_cmd,        0 },
    { NULL,       NULL }
    };

static CTAB remote_only_cmds[] = {
    { "REPEAT",   &x_repeat_cmd,      0 },
    { "COLLECT",  &x_collect_cmd,     0 },
    { "SAMPLEOUT",&x_sampleout_cmd,   0 },
    { "EXECUTE",  &x_execute_cmd,     0 },
    { NULL,       NULL }
    };

static t_stat x_help_cmd (int32 flag, CONST char *cptr)
{
CTAB *cmdp, *cmdph;

if (*cptr) {
    int32 saved_switches = sim_switches;
    t_stat r;

    sim_switches |= SWMASK ('F');
    r = help_cmd (flag, cptr);
    sim_switches = saved_switches;
    return r;
    }
sim_printf ("Help is available for the following Remote Console commands:\r\n");
for (cmdp=allowed_remote_cmds; cmdp->name != NULL; ++cmdp) {
    cmdph = find_cmd (cmdp->name);
    if (cmdph && cmdph->help)
        sim_printf ("    %s\r\n", cmdp->name);
    }
sim_printf ("Enter \"HELP cmd\" for detailed help on a command\r\n");
return SCPE_OK;
}

static t_stat _sim_rem_message (const char *cmd, t_stat stat)
{
CTAB *cmdp = NULL;
t_stat stat_nomessage = stat & SCPE_NOMESSAGE;  /* extract possible message supression flag */

cmdp = find_cmd (cmd);
stat = SCPE_BARE_STATUS(stat);                  /* remove possible flag */
if (!stat_nomessage) {
    if (cmdp && (cmdp->message))                /* special message handler? */
        cmdp->message (NULL, stat);             /* let it deal with display */
    else {
        if (stat >= SCPE_BASE)                  /* error? */
            sim_printf ("%s\r\n", sim_error_text (stat));
        }
    }
return stat;
}

static void _sim_rem_log_out (TMLN *lp)
{
char cbuf[4*CBUFSIZE];
REMOTE *rem = &sim_rem_consoles[(int)(lp - sim_rem_con_tmxr.ldsc)];

if ((!sim_oline) && (sim_log)) {
    fflush (sim_log);
    (void)sim_fseeko (sim_log, sim_rem_cmd_log_start, SEEK_SET);
    cbuf[sizeof(cbuf)-1] = '\0';
    while (fgets (cbuf, sizeof(cbuf)-1, sim_log))
        tmxr_linemsgf (lp, "%s", cbuf);
    }
sim_oline = NULL;
if ((rem->act == NULL) && 
    (!tmxr_input_pending_ln (lp))) {
    int32 unwritten;

    do {
        unwritten = tmxr_send_buffered_data (lp);
        if (unwritten == lp->txbsz)
            sim_os_ms_sleep (100);
        } while (unwritten == lp->txbsz);
    }

}

void sim_remote_process_command (void)
{
char cbuf[4*CBUFSIZE], gbuf[CBUFSIZE], *argv[1] = {NULL};
CONST char *cptr;
int32 saved_switches = sim_switches;
t_stat stat;

strlcpy (cbuf, sim_rem_command_buf, sizeof (cbuf));
while (isspace(cbuf[0]))
    memmove (cbuf, cbuf+1, strlen(cbuf+1)+1);   /* skip leading whitespace */
sim_sub_args (cbuf, sizeof(cbuf), argv);
cptr = cbuf;
cptr = get_glyph (cptr, gbuf, 0);               /* get command glyph */
sim_rem_active_command = find_cmd (gbuf);       /* find command */

if (!sim_processing_event)
    sim_ttcmd ();                               /* restore console */
stat = sim_rem_active_command->action (sim_rem_active_command->arg, cptr);/* execute command */
if (stat != SCPE_OK)
    stat = _sim_rem_message (gbuf, stat);       /* display results */
sim_last_cmd_stat = SCPE_BARE_STATUS(stat);
if (sim_vm_post != NULL)                        /* optionally let the simulator know */
    (*sim_vm_post) (TRUE);                      /* something might have changed */
if (!sim_processing_event) {
    sim_ttrun ();                               /* set console mode */
    sim_cancel (rem_con_data_unit);             /* force immediate activation of sim_rem_con_data_svc */
    sim_activate (rem_con_data_unit, -1);
    }
sim_switches = saved_switches;                  /* restore original switches */
}

/* Clear pending actions */

static char *sim_rem_clract (int32 line)
{
REMOTE *rem = &sim_rem_consoles[line];

tmxr_send_buffered_data (rem->lp);              /* flush any buffered data */
return rem->act = NULL;
}

/* Set up pending actions */

static void sim_rem_setact (int32 line, const char *action)
{
if (action) {
    size_t act_size = strlen (action) + 1;
    REMOTE *rem = &sim_rem_consoles[line];

    if (act_size > rem->act_buf_size) {         /* expand buffer if necessary */
        rem->act_buf = (char *)realloc (rem->act_buf, act_size);
        rem->act_buf_size = act_size;
        }
    strcpy (rem->act_buf, action);              /* populate buffer */
    rem->act = rem->act_buf;                    /* start at beginning of buffer */
    }
else
    sim_rem_clract (line);
}

/* Get next pending action, if any */

static char *sim_rem_getact (int32 line, char *buf, int32 size)
{
char *ep;
size_t lnt;
REMOTE *rem = &sim_rem_consoles[line];

if (rem->act == NULL)                           /* any action? */
    return NULL;
while (sim_isspace (*rem->act))                 /* skip spaces */
    rem->act++;
if (*rem->act == 0)                             /* now empty? */
    return sim_rem_clract (line);
ep = strpbrk (rem->act, ";\"'");                /* search for a semicolon or single or double quote */
if ((ep != NULL) && (*ep != ';')) {             /* if a quoted string is present */
    char quote = *ep++;                         /*   then save the opening quotation mark */

    while (ep [0] != '\0' && ep [0] != quote)   /* while characters remain within the quotes */
        if (ep [0] == '\\' && ep [1] == quote)  /*   if an escaped quote sequence follows */
            ep = ep + 2;                        /*     then skip over the pair */
        else                                    /*   otherwise */
            ep = ep + 1;                        /*     skip the non-quote character */  
    ep = strchr (ep, ';');                      /* the next semicolon is outside the quotes if it exists */
    }

if (ep != NULL) {                               /* if a semicolon is present */
    lnt = ep - rem->act;                        /* cmd length */
    memcpy (buf, rem->act, lnt + 1);            /* copy with ; */
    buf[lnt] = 0;                               /* erase ; */
    rem->act += lnt + 1;                        /* adv ptr */
    }
else {
    strlcpy (buf, rem->act, size);              /* copy action */
    rem->act += strlen (rem->act);              /* adv ptr to end */
    sim_rem_clract (line);
    }
return buf;
}

/* 
    Parse and setup Remote Console REPEAT command:
       REPEAT EVERY nnn USECS Command {; command...}
 */
static t_stat sim_rem_repeat_cmd_setup (int32 line, CONST char **iptr)
{
char gbuf[CBUFSIZE];
int32 val;
t_bool all_stop = FALSE;
t_stat stat = SCPE_OK;
CONST char *cptr = *iptr;
REMOTE *rem = &sim_rem_consoles[line];

sim_debug (DBG_REP, &sim_remote_console, "Repeat Setup: %s\n", cptr);
if (*cptr == 0)         /* required argument? */
    stat = SCPE_2FARG;
else {
    cptr = get_glyph (cptr, gbuf, 0);               /* get next glyph */
    if (MATCH_CMD (gbuf, "EVERY") == 0) {
        cptr = get_glyph (cptr, gbuf, 0);           /* get next glyph */
        val = (int32) get_uint (gbuf, 10, INT_MAX, &stat);
        if ((stat != SCPE_OK) || (val <= 0))        /* error? */
            stat = SCPE_ARG;
        else {
            cptr = get_glyph (cptr, gbuf, 0);       /* get next glyph */
            if ((MATCH_CMD (gbuf, "USECS") != 0) || (*cptr == 0))
                stat = SCPE_ARG;
            else
                rem->repeat_interval = val;
            }
        }
    else {
        if (MATCH_CMD (gbuf, "STOP") == 0) {
            if (*cptr) {                            /* more command arguments? */
                cptr = get_glyph (cptr, gbuf, 0);   /* get next glyph */
                if ((MATCH_CMD (gbuf, "ALL") != 0) ||   /*  */
                    (*cptr != 0)                   ||   /*  */
                    (line != 0))                        /* master line? */
                    stat = SCPE_ARG;
                else
                    all_stop = TRUE;
                }
            else
                rem->repeat_interval = 0;
            }
        else
            stat = SCPE_ARG;
        }
    }
if (stat == SCPE_OK) {
    if (all_stop) {
        for (line = 0; line < sim_rem_con_tmxr.lines; line++) {
            rem = &sim_rem_consoles[line];
            free (rem->repeat_action);
            rem->repeat_action = NULL;
            sim_cancel (rem->uptr);
            rem->repeat_pending = FALSE;
            sim_rem_clract (line);
            }
        }
    else {
        if (rem->repeat_interval != 0) {
            rem->repeat_action = (char *)realloc (rem->repeat_action, 1 + strlen (cptr));
            strcpy (rem->repeat_action, cptr);
            cptr += strlen (cptr);
            stat = sim_activate_after (rem->uptr, rem->repeat_interval);
            }
        else {
            free (rem->repeat_action);
            rem->repeat_action = NULL;
            sim_cancel (rem->uptr);
            }
        rem->repeat_pending = FALSE;
        sim_rem_clract (line);
        }
    }
*iptr = cptr;
return stat;
}


/* 
    Parse and setup Remote Console REPEAT command:
       COLLECT nnn SAMPLES EVERY nnn CYCLES reg{,reg...}
 */
static t_stat sim_rem_collect_cmd_setup (int32 line, CONST char **iptr)
{
char gbuf[CBUFSIZE];
int32 samples, cycles, dither_pct;
t_bool all_stop = FALSE;
t_stat stat = SCPE_OK;
CONST char *cptr = *iptr;
REMOTE *rem = &sim_rem_consoles[line];

sim_debug (DBG_SAM, &sim_remote_console, "Collect Setup: %s\n", cptr);
if (*cptr == 0)         /* required argument? */
    return SCPE_2FARG;
cptr = get_glyph (cptr, gbuf, 0);               /* get next glyph */
samples = (int32) get_uint (gbuf, 10, INT_MAX, &stat);
if ((stat != SCPE_OK) || (samples <= 0)) {      /* error? */
    if (MATCH_CMD (gbuf, "STOP") == 0) {
        stat = SCPE_OK;
        if (*cptr) {                            /* more command arguments? */
            cptr = get_glyph (cptr, gbuf, 0);   /* get next glyph */
            if ((MATCH_CMD (gbuf, "ALL") != 0) ||   /*  */
                (*cptr != 0)                   ||   /*  */
                (line != 0))                        /* master line? */
                stat = SCPE_ARG;
            else
                all_stop = TRUE;
            }
        if (stat == SCPE_OK) {
            for (line = all_stop ? 0 : rem->line; line < (all_stop ? sim_rem_con_tmxr.lines : (rem->line + 1)); line++) {
                uint32 i, j;

                rem = &sim_rem_consoles[line];
                for (i = 0; i< rem->smp_reg_count; i++) {
                    for (j = 0; j < rem->smp_regs[i].width; j++)
                        free (rem->smp_regs[i].bits[j].vals);
                    free (rem->smp_regs[i].bits);
                    }
                free (rem->smp_regs);
                rem->smp_regs = NULL;
                rem->smp_reg_count = 0;
                sim_cancel (&rem_con_smp_smpl_units[rem->line]);
                rem->smp_sample_interval = 0;
                }
            }
        }
    else
        stat = sim_messagef (SCPE_ARG, "Expected value or STOP found: %s\n", gbuf);
    }
else {
    const char *tptr;
    int32 event_time = rem->smp_sample_interval;

    cptr = get_glyph (cptr, gbuf, 0);               /* get next glyph */
    if (MATCH_CMD (gbuf, "SAMPLES") != 0) {
        *iptr = cptr;
        return sim_messagef (SCPE_ARG, "Expected SAMPLES found: %s\n", gbuf);
        }
    cptr = get_glyph (cptr, gbuf, 0);               /* get next glyph */
    if (MATCH_CMD (gbuf, "EVERY") != 0) {
        *iptr = cptr;
        return sim_messagef (SCPE_ARG, "Expected EVERY found: %s\n", gbuf);
        }
    cptr = get_glyph (cptr, gbuf, 0);               /* get next glyph */
    cycles = (int32) get_uint (gbuf, 10, INT_MAX, &stat);
    if ((stat != SCPE_OK) || (cycles <= 0)) {       /* error? */
        *iptr = cptr;
        return sim_messagef (SCPE_ARG, "Expected value found: %s\n", gbuf);
        }
    cptr = get_glyph (cptr, gbuf, 0);               /* get next glyph */
    if ((MATCH_CMD (gbuf, "CYCLES") != 0) || (*cptr == 0)) {
        *iptr = cptr;
        return sim_messagef (SCPE_ARG, "Expected CYCLES found: %s\n", gbuf);
        }
    cptr = get_glyph (cptr, gbuf, 0);               /* get next glyph */
    if ((MATCH_CMD (gbuf, "DITHER") != 0) || (*cptr == 0)) {
        *iptr = cptr;
        return sim_messagef (SCPE_ARG, "Expected DITHER found: %s\n", gbuf);
        }
    cptr = get_glyph (cptr, gbuf, 0);               /* get next glyph */
    dither_pct = (int32) get_uint (gbuf, 10, INT_MAX, &stat);
    if ((stat != SCPE_OK) ||                        /* error? */
        (dither_pct < 0) || (dither_pct > 25)) {
        *iptr = cptr;
        return sim_messagef (SCPE_ARG, "Expected value found: %s\n", gbuf);
        }
    cptr = get_glyph (cptr, gbuf, 0);               /* get next glyph */
    if ((MATCH_CMD (gbuf, "PERCENT") != 0) || (*cptr == 0)) {
        *iptr = cptr;
        return sim_messagef (SCPE_ARG, "Expected PERCENT found: %s\n", gbuf);
        }
    tptr = strcpy (gbuf, "STOP");                   /* Start from a clean slate */
    sim_rem_collect_cmd_setup (rem->line, &tptr);
    rem->smp_sample_interval = cycles;
    rem->smp_reg_count = 0;
    while (cptr && *cptr) {
        const char *comma = strchr (cptr, ',');
        char tbuf[2*CBUFSIZE];
        uint32 bit, width;
        REG *reg;
        uint32 idx;
        int32 saved_switches = sim_switches;
        t_bool indirect = FALSE;
        BITSAMPLE_REG *smp_regs;

        if (comma) {
            strncpy (tbuf, cptr, comma - cptr);
            tbuf[comma - cptr] = '\0';
            cptr = comma + 1;
            }
        else {
            strcpy (tbuf, cptr);
            cptr += strlen (cptr);
            }
        tptr = tbuf;
        if (strchr (tbuf, ' ')) {
            sim_switches = 0;
            tptr = get_sim_opt (CMD_OPT_SW|CMD_OPT_DFT, tbuf, &stat); /* get switches and device */
            indirect = ((sim_switches & SWMASK('I')) != 0);
            sim_switches = saved_switches;
            }
        if (stat != SCPE_OK)
            break;
        tptr = get_glyph (tptr, gbuf, 0);     /* get next glyph */
        reg = find_reg (gbuf, &tptr, sim_dfdev);
        if (reg == NULL) {
            stat = sim_messagef (SCPE_NXREG, "Nonexistent Register: %s\n", gbuf);
            break;
            }
        if (*tptr == '[') {                             /* subscript? */
            const char *tgptr = ++tptr;

            if (reg->depth <= 1) {                      /* array register? */
                stat = sim_messagef (SCPE_SUB, "Not Array Register: %s\n", reg->name);
                break;
                }
            idx = (uint32) strtotv (tgptr, &tptr, 10);  /* convert index */
            if ((tgptr == tptr) || (*tptr++ != ']')) {
                stat = sim_messagef (SCPE_SUB, "Missing or Invalid Register Subscript: %s[%s\n", reg->name, tgptr);
                break;
                }
            if (idx >= reg->depth) {                    /* validate subscript */
                stat = sim_messagef (SCPE_SUB, "Invalid Register Subscript: %s[%d]\n", reg->name, idx);
                break;
                }
            }
        else
            idx = 0;                                    /* not array */
        smp_regs = (BITSAMPLE_REG *)realloc (rem->smp_regs, (rem->smp_reg_count + 1) * sizeof(*smp_regs));
        if (smp_regs == NULL) {
            stat = SCPE_MEM;
            break;
            }
        rem->smp_regs = smp_regs;
        smp_regs[rem->smp_reg_count].reg = reg;
        smp_regs[rem->smp_reg_count].idx = idx;
        smp_regs[rem->smp_reg_count].dptr = sim_dfdev;
        smp_regs[rem->smp_reg_count].uptr = sim_dfunit;
        smp_regs[rem->smp_reg_count].indirect = indirect;
        width = indirect ? sim_dfdev->dwidth : reg->width;
        smp_regs[rem->smp_reg_count].width = width;
        smp_regs[rem->smp_reg_count].bits = (BITSAMPLE *)calloc (width, sizeof (*smp_regs[rem->smp_reg_count - 1].bits));
        if (smp_regs[rem->smp_reg_count].bits == NULL) {
            stat = SCPE_MEM;
            break;
            }
        rem->smp_reg_count += 1;
        for (bit = 0; bit < width; bit++) {
            smp_regs[rem->smp_reg_count - 1].bits[bit].depth = samples;
            smp_regs[rem->smp_reg_count - 1].bits[bit].vals = (int *)calloc (samples, sizeof (int));
            if (smp_regs[rem->smp_reg_count - 1].bits[bit].vals == NULL) {
                stat = SCPE_MEM;
                break;
                }
            }
        if (stat != SCPE_OK)
            break;
        }
    if (stat != SCPE_OK) {                      /* Error? */
        *iptr = cptr;
        cptr = strcpy (gbuf, "STOP");
        sim_rem_collect_cmd_setup (line, &cptr);/* Cleanup mess */
        return stat;
        }
    if (rem->smp_sample_dither_pct)
        event_time = (((rand() % (2 * rem->smp_sample_dither_pct)) - rem->smp_sample_dither_pct) * event_time) / 100;
    sim_activate (&rem_con_smp_smpl_units[rem->line], event_time);
    }
*iptr = cptr;
return stat;
}

t_stat sim_rem_con_repeat_svc (UNIT *uptr)
{
int line = uptr - rem_con_repeat_units;
REMOTE *rem = &sim_rem_consoles[line];

sim_debug (DBG_REP, &sim_remote_console, "sim_rem_con_repeat_svc(line=%d) - interval=%d usecs\n", line, rem->repeat_interval);
if (rem->repeat_interval) {
    rem->repeat_pending = TRUE;
    sim_activate_after (uptr, rem->repeat_interval);        /* reschedule */
    sim_activate_abs (rem_con_data_unit, -1);               /* wake up to process */
    }
return SCPE_OK;
}

static void sim_rem_record_reg_bit (BITSAMPLE *bit, int val)
{
bit->tot -= bit->vals[bit->ptr];    /* remove retired value */
bit->tot += val;                    /* accumulate new value */
bit->vals[bit->ptr] = val;          /* save new value */
++bit->ptr;                         /* increment next pointer */
if (bit->ptr >= bit->depth)         /* if too big */
    bit->ptr = 0;                   /* wrap around */
}

static void sim_rem_set_reg_bit (BITSAMPLE *bit, int val)
{
int i;

bit->tot = bit->depth * val;        /* compute total */
for (i = 0; i < bit->depth; i++)    /* set all value bits */
    bit->vals[i] = val;
}

static void sim_rem_collect_reg_bits (BITSAMPLE_REG *reg)
{
uint32 i;
t_value val = get_rval (reg->reg, reg->idx);

if (reg->indirect)
    val = get_aval ((t_addr)val, reg->dptr, reg->uptr);
val = val >> reg->reg->offset;
for (i = 0; i < reg->width; i++) {
    if (sim_is_running)
        sim_rem_record_reg_bit (&reg->bits[i], val&1);
    else
        sim_rem_set_reg_bit (&reg->bits[i], val&1);
    val = val >> 1;
    }
}

static void sim_rem_collect_registers (REMOTE *rem)
{
uint32 i;

for (i = 0; i < rem->smp_reg_count; i++)
    sim_rem_collect_reg_bits (&rem->smp_regs[i]);
}

static void sim_rem_collect_all_registers (void)
{
int32 line;

for (line = 0; line < sim_rem_con_tmxr.lines; line++)
    sim_rem_collect_registers (&sim_rem_consoles[line]);
}

t_stat sim_rem_con_smp_collect_svc (UNIT *uptr)
{
int line = uptr - rem_con_smp_smpl_units;
REMOTE *rem = &sim_rem_consoles[line];

sim_debug (DBG_SAM, &sim_remote_console, "sim_rem_con_smp_collect_svc(line=%d) - interval=%d, dither=%d%%\n", line, rem->smp_sample_interval, rem->smp_sample_dither_pct);
if (rem->smp_sample_interval && (rem->smp_reg_count != 0)) {
    int32 event_time = rem->smp_sample_interval;

    if (rem->smp_sample_dither_pct)
        event_time = (((rand() % (2 * rem->smp_sample_dither_pct)) - rem->smp_sample_dither_pct) * event_time) / 100;
    sim_rem_collect_registers (rem);
    sim_activate (uptr, event_time);                    /* reschedule */
    }
return SCPE_OK;
}

/* Unit service for remote console data polling */

t_stat sim_rem_con_data_svc (UNIT *uptr)
{
int32 i, j, c = 0;
t_stat stat = SCPE_OK;
t_bool active_command = FALSE;
int32 steps = 0;
t_bool was_active_command = (sim_rem_cmd_active_line != -1);
t_bool got_command;
t_bool close_session = FALSE;
TMLN *lp;
char cbuf[4*CBUFSIZE], gbuf[CBUFSIZE], *argv[1] = {NULL};
CONST char *cptr;
CTAB *cmdp = NULL;
CTAB *basecmdp = NULL;
uint32 read_start_time = 0;

tmxr_poll_rx (&sim_rem_con_tmxr);                      /* poll input */
for (i=(was_active_command ? sim_rem_cmd_active_line : 0); 
     (i < sim_rem_con_tmxr.lines) && (!active_command); 
     i++) {
    REMOTE *rem = &sim_rem_consoles[i];
    t_bool master_session = (sim_rem_master_mode && (i == 0));

    lp = rem->lp;
    if (!lp->conn) {
        if (rem->repeat_interval) {                 /* was repeated enabled? */
            cptr = strcpy (gbuf, "STOP");
            sim_rem_repeat_cmd_setup (i, &cptr);    /* make sure it is now disabled */
            }
        if (rem->smp_reg_count) {                   /* were bit samples being collected? */
            cptr = strcpy (gbuf, "STOP");
            sim_rem_collect_cmd_setup (i, &cptr);   /* make sure it is now disabled */
            }
        continue;
        }
    if (master_session && !sim_rem_master_was_connected) {
        tmxr_linemsgf (lp, "\nMaster Mode Session\r\n");
        tmxr_send_buffered_data (lp);               /* flush any buffered data */
        }
    sim_rem_master_was_connected |= master_session; /* Remember if master ever connected */
    stat = SCPE_OK;
    if ((was_active_command) ||
        (master_session && !rem->single_mode)) {
        sim_debug (DBG_MOD, &sim_remote_console, "Session: %d %s %s\n", i, was_active_command ? "Was Active" : "", (master_session && !rem->single_mode) ? "master_session && !single_mode" : "");
        if (was_active_command) {
            sim_rem_cmd_active_line = -1;           /* Done with active command */
            if (!sim_rem_active_command) {          /* STEP command? */
                stat = SCPE_STEP;
                if (sim_con_stable_registers || !sim_rem_master_mode)
                    _sim_rem_message ("STEP", stat);/* produce a STEP complete message */
                }
            _sim_rem_log_out (lp);
            sim_rem_active_command = NULL;          /* Restart loop to process available input */
            was_active_command = FALSE;
            i = -1;
            continue;
            }
        else {
            sim_is_running = FALSE;
            sim_rem_collect_all_registers ();
            sim_stop_timer_services ();
            sim_flush_buffered_files ();
            if (rem->act == NULL) {
                for (j=0; j < sim_rem_con_tmxr.lines; j++) {
                    TMLN *lpj = &sim_rem_con_tmxr.ldsc[j];
                    if ((i == j) || (!lpj->conn))
                        continue;
                    tmxr_linemsgf (lpj, "\nRemote Master Console(%s) Entering Commands\n", lp->ipad);
                    tmxr_send_buffered_data (lpj);     /* flush any buffered data */
                    }
                }
            }
        }
    else {
        if (((!rem->repeat_pending) && (rem->act == NULL)) ||   /* Repeat isn't pending AND no prior commands still active */
            (rem->buf_ptr != 0) ||                              /* OR Not at beginning of line */
            (tmxr_input_pending_ln (lp))) {                     /* OR input available to read */
            c = tmxr_getc_ln (lp);
            if (!(TMXR_VALID & c))
                continue;
            c = c & ~TMXR_VALID;
            if (rem->single_mode) {
                if (c == sim_int_char) {            /* ^E (the interrupt character) must start continue mode console interaction */
                    rem->single_mode = FALSE;       /* enter multi command mode */
                    sim_is_running = FALSE;
                    sim_rem_collect_all_registers ();
                    sim_stop_timer_services ();
                    sim_flush_buffered_files ();
                    stat = SCPE_STOP;
                    _sim_rem_message ("RUN", stat);
                    _sim_rem_log_out (lp);
                    for (j=0; j < sim_rem_con_tmxr.lines; j++) {
                        TMLN *lpj = &sim_rem_con_tmxr.ldsc[j];
                        if ((i == j) || (!lpj->conn))
                            continue;
                        tmxr_linemsgf (lpj, "\nRemote Console %d(%s) Entering Commands\n", i, lp->ipad);
                        tmxr_send_buffered_data (lpj);  /* flush any buffered data */
                        }
                    lp = &sim_rem_con_tmxr.ldsc[i];
                    if (!master_session)
                        tmxr_linemsg (lp, "\r\nSimulator paused.\r\n");
                    if (!master_session && rem->read_timeout) {
                        tmxr_linemsgf (lp, "Simulation will resume automatically if input is not received in %d seconds\n", rem->read_timeout);
                        tmxr_linemsgf (lp, "\r\n");
                        tmxr_send_buffered_data (lp);   /* flush any buffered data */
                        }
                    }
                else {
                    if ((rem->buf_ptr == 0) &&          /* At beginning of input line */
                        ((c == '\n') ||                 /* Ignore bare LF between commands (Microsoft Telnet bug) */
                         (c == '\r')))                  /* Ignore empty commands */
                        continue;
                    if ((c == '\004') || (c == '\032')) {/* EOF character (^D or ^Z) ? */
                        tmxr_linemsgf (lp, "\r\nGoodbye\r\n");
                        tmxr_send_buffered_data (lp);   /* flush any buffered data */
                        tmxr_reset_ln (lp);
                        continue;
                        }
                    if (rem->buf_ptr == 0) {
                        /* we just picked up the first character on a command line */
                        if (!master_session)
                            tmxr_linemsgf (lp, "\r\n%s", sim_prompt);
                        else
                            tmxr_linemsgf (lp, "\r\n%s", sim_is_running ? "SIM> " : "sim> ");
                        sim_debug (DBG_XMT, &sim_remote_console, "Prompt Written: %s\n", sim_is_running ? "SIM> " : "sim> ");
                        if ((rem->act == NULL) && (!tmxr_input_pending_ln (lp)))
                            tmxr_send_buffered_data (lp);/* flush any buffered data */
                        }
                    }
                }
            }
        }
    got_command = FALSE;
    while (1) {
        if (stat == SCPE_EXIT)
            return stat|SCPE_NOMESSAGE;
        if ((!rem->single_mode) && (rem->act == NULL)) {
            read_start_time = sim_os_msec();
            if (master_session)
                tmxr_linemsg (lp, "sim> ");
            else
                tmxr_linemsg (lp, sim_prompt);
            tmxr_send_buffered_data (lp);               /* flush any buffered data */
            }
        do {
            if (rem->buf_ptr == 0) {
                if (sim_rem_getact (i, rem->buf, rem->buf_size)) {
                    if (!master_session)
                        tmxr_linemsgf (lp, "%s%s\n", sim_prompt, rem->buf);
                    else
                        tmxr_linemsgf (lp, "%s%s\n", "SIM> ", rem->buf);
                    rem->buf_ptr = strlen (rem->buf);
                    got_command = TRUE;
                    break;
                    }
                if ((rem->repeat_pending) &&            /* New repeat pending */
                    (rem->act == NULL) &&               /* AND no prior still active */
                    (!tmxr_input_pending_ln (lp))) {    /* AND no session input pending */
                    rem->repeat_pending = FALSE;
                    sim_rem_setact (rem-sim_rem_consoles, rem->repeat_action);
                    sim_rem_getact (rem-sim_rem_consoles, rem->buf, rem->buf_size);
                    if (!master_session)
                        tmxr_linemsgf (lp, "%s%s\n", sim_prompt, rem->buf);
                    else
                        tmxr_linemsgf (lp, "%s%s\n", "SIM> ", rem->buf);
                    rem->buf_ptr = strlen (rem->buf);
                    got_command = TRUE;
                    break;
                    }
                }
            if (!rem->single_mode) {
                c = tmxr_getc_ln (lp);
                if (!(TMXR_VALID & c)) {
                    tmxr_send_buffered_data (lp);       /* flush any buffered data */
                    if (!master_session && 
                        rem->read_timeout &&
                        ((sim_os_msec() - read_start_time)/1000 >= rem->read_timeout)) {
                        while (rem->buf_ptr > 0) {      /* Erase current input line */
                            tmxr_linemsg (lp, "\b \b");
                            --rem->buf_ptr;
                            }
                        if (rem->buf_ptr+80 >= rem->buf_size) {
                            rem->buf_size += 1024;
                            rem->buf = (char *)realloc (rem->buf, rem->buf_size);
                            }
                        strcpy (rem->buf, "CONTINUE         ! Automatic continue due to timeout");
                        tmxr_linemsgf (lp, "%s\n", rem->buf);
                        got_command = TRUE;
                        break;
                        }
                    sim_os_ms_sleep (50);
                    tmxr_poll_rx (&sim_rem_con_tmxr);   /* poll input */
                    if (!lp->conn) {                    /* if connection lost? */
                        rem->single_mode = TRUE;        /* No longer multi-command more */
                        break;                          /* done waiting */
                        }
                    continue;
                    }
                read_start_time = sim_os_msec();
                c = c & ~TMXR_VALID;
                }
            switch (c) {
                case 0:     /* no data */
                    break;
                case '\b':  /* Backspace */
                case 127:   /* Rubout */
                    if (rem->buf_ptr > 0) {
                        tmxr_linemsg (lp, "\b \b");
                        --rem->buf_ptr;
                        }
                    break;
                case 27:   /* escape */
                case 21:   /* ^U */
                    while (rem->buf_ptr > 0) {
                        tmxr_linemsg (lp, "\b \b");
                        --rem->buf_ptr;
                        }
                    break;
                case '\n':
                    if (rem->buf_ptr == 0)
                        break;
                    /* fall through */
                case '\r':
                    tmxr_linemsg (lp, "\r\n");
                    if (rem->buf_ptr+1 >= rem->buf_size) {
                        rem->buf_size += 1024;
                        rem->buf = (char *)realloc (rem->buf, rem->buf_size);
                        }
                    rem->buf[rem->buf_ptr++] = '\0';
                    sim_debug (DBG_RCV, &sim_remote_console, "Got Command (%d bytes still in buffer): %s\n", tmxr_input_pending_ln (lp), rem->buf);
                    got_command = TRUE;
                    break;
                case '\004': /* EOF (^D) */
                case '\032': /* EOF (^Z) */
                    while (rem->buf_ptr > 0) {          /* Erase current input line */
                        tmxr_linemsg (lp, "\b \b");
                        --rem->buf_ptr;
                        }
                    if (!rem->single_mode) {
                        if (rem->buf_ptr+80 >= rem->buf_size) {
                            rem->buf_size += 1024;
                            rem->buf = (char *)realloc (rem->buf, rem->buf_size);
                            }
                        strcpy (rem->buf, "CONTINUE         ! Automatic continue before close");
                        tmxr_linemsgf (lp, "%s\n", rem->buf);
                        got_command = TRUE;
                        }
                    close_session = TRUE;
                    break;
                default:
                    tmxr_putc_ln (lp, c);
                    if (rem->buf_ptr+2 >= rem->buf_size) {
                        rem->buf_size += 1024;
                        rem->buf = (char *)realloc (rem->buf, rem->buf_size);
                        }
                    rem->buf[rem->buf_ptr++] = (char)c;
                    rem->buf[rem->buf_ptr] = '\0';
                    if (((size_t)rem->buf_ptr) >= sizeof(cbuf))
                        got_command = TRUE;             /* command too long */
                    break;
                }
            c = 0;
            if ((!got_command) &&                   /* No Command yet */
                (rem->single_mode) &&               /* AND single command mode */
                (tmxr_input_pending_ln (lp)) &&     /* AND something ready to read */
                (rem->act == NULL)) {               /* AND no prior still active */
                c = tmxr_getc_ln (lp);
                c = c & ~TMXR_VALID;
                }
            } while ((!got_command) && ((!rem->single_mode) || c));
        if ((rem->act == NULL) && (!tmxr_input_pending_ln (lp)))
            tmxr_send_buffered_data (lp);               /* flush any buffered data */
        if ((rem->single_mode) && !got_command) {
            break;
            }
        if (!sim_rem_master_mode)
            sim_printf ("Remote Console Command from %s> %s\r\n", lp->ipad, rem->buf);
        got_command = FALSE;
        if (strlen(rem->buf) >= sizeof(cbuf)) {
            sim_printf ("\r\nLine too long. Ignored.  Continuing Simulator execution\r\n");
            tmxr_linemsgf (lp, "\nLine too long. Ignored.  Continuing Simulator execution\n");
            tmxr_send_buffered_data (lp);               /* try to flush any buffered data */
            break;
            }
        strcpy (cbuf, rem->buf);
        rem->buf_ptr = 0;
        rem->buf[rem->buf_ptr] = '\0';
        while (isspace(cbuf[0]))
            memmove (cbuf, cbuf+1, strlen(cbuf+1)+1);   /* skip leading whitespace */
        if (cbuf[0] == '\0') {
            if (rem->single_mode) {
                rem->single_mode = FALSE;
                break;
                }
            else
                continue;
            }
        strcpy (sim_rem_command_buf, cbuf);
        sim_sub_args (cbuf, sizeof(cbuf), argv);
        cptr = cbuf;
        cptr = get_glyph (cptr, gbuf, 0);               /* get command glyph */
        sim_switches = 0;                               /* init switches */
        sim_rem_active_number = i;
        if (!sim_log) {                                 /* Not currently logging? */
            int32 save_quiet = sim_quiet;

            sim_quiet = 1;
            sprintf (sim_rem_con_temp_name, "sim_remote_console_%d.temporary_log", (int)getpid());
            sim_set_logon (0, sim_rem_con_temp_name);
            sim_quiet = save_quiet;
            sim_log_temp = TRUE;
            }
        sim_rem_cmd_log_start = sim_ftell (sim_log);
        basecmdp = find_cmd (gbuf);                     /* validate basic command */
        if (basecmdp == NULL)
            basecmdp = find_ctab (remote_only_cmds, gbuf);/* validate basic command */
        if (basecmdp == NULL) {
            if ((gbuf[0] == ';') || (gbuf[0] == '#')) { /* ignore comment */
                sim_rem_cmd_active_line = i;
                was_active_command = TRUE;
                sim_rem_active_command = &allowed_single_remote_cmds[0];/* Dummy */
                i = i - 1;
                break;
                }
            else
                stat = SCPE_UNK;
            }
        else {
            if ((cmdp = find_ctab (rem->single_mode ? allowed_single_remote_cmds : (master_session ? allowed_master_remote_cmds : allowed_remote_cmds), gbuf))) {/* lookup command */
                sim_debug (DBG_CMD, &sim_remote_console, "gbuf='%s', basecmd='%s', cmd='%s'\n", gbuf, basecmdp->name, cmdp->name);
                if (cmdp->action == &x_continue_cmd) {
                    sim_debug (DBG_CMD, &sim_remote_console, "continue_cmd executing\n");
                    stat = SCPE_OK;
                    }
                else {
                    if (cmdp->action == &exit_cmd)
                        return SCPE_EXIT;
                    if (cmdp->action == &x_step_cmd) {
                        sim_debug (DBG_CMD, &sim_remote_console, "step_cmd executing\n");
                        steps = 1;                      /* default of 1 instruction */
                        stat = SCPE_OK;
                        if (*cptr != 0) {               /* argument? */
                            cptr = get_glyph (cptr, gbuf, 0);/* get next glyph */
                            if (*cptr != 0)            /* should be end */
                                stat = SCPE_2MARG;
                            else {
                                steps = (int32) get_uint (gbuf, 10, INT_MAX, &stat);
                                if ((stat != SCPE_OK) || (steps <= 0)) /* error? */
                                    stat = SCPE_ARG;
                                }
                            }
                        if (stat != SCPE_OK)
                            cmdp = NULL;
                        }
                    else {
                        if (cmdp->action == &x_run_cmd) {
                            sim_debug (DBG_CMD, &sim_remote_console, "run_cmd executing\n");
                            if (sim_con_stable_registers && /* can we process command now? */
                                sim_rem_master_mode)
                                sim_oline = lp;             /* specify output socket */
                            sim_switches |= SIM_SW_HIDE;    /* Request Setup only */
                            stat = basecmdp->action (cmdp->arg, cptr);
                            sim_switches &= ~SIM_SW_HIDE;   /* Done with Setup only mode */
                            if (stat == SCPE_OK) {
                                /* switch to CONTINUE after x_run_cmd() did RUN setup */
                                cmdp = find_ctab (allowed_master_remote_cmds, "CONTINUE");
                                }
                            }
                        else {
                            if (cmdp->action == &x_sampleout_cmd) {
                                sim_debug (DBG_CMD, &sim_remote_console, "sampleout_cmd executing\n");
                                sim_oline = lp;                     /* specify output socket */
                                stat = sim_rem_sample_output (NULL, i);
                                }
                            else {
                                if (cmdp->action == &x_repeat_cmd) {
                                    sim_debug (DBG_CMD, &sim_remote_console, "repeat_cmd executing\n");
                                    stat = sim_rem_repeat_cmd_setup (i, &cptr);
                                    }
                                else {
                                    if (cmdp->action == &x_execute_cmd) {
                                        sim_debug (DBG_CMD, &sim_remote_console, "execute_cmd executing\n");
                                        if (rem->act)
                                            stat = SCPE_IERR;
                                        else {
                                            sim_rem_setact (rem-sim_rem_consoles, cptr);
                                            stat = SCPE_OK;
                                            }
                                        }
                                    else {
                                        if (cmdp->action == &x_collect_cmd) {
                                            sim_debug (DBG_CMD, &sim_remote_console, "collect_cmd executing\n");
                                            stat = sim_rem_collect_cmd_setup (i, &cptr);
                                            }
                                        else {
                                            if ((sim_con_stable_registers &&    /* can we process command now? */
                                                 sim_rem_master_mode) ||
                                                (cmdp->action == &x_help_cmd)) {
                                                sim_debug (DBG_CMD, &sim_remote_console, "Processing Command directly\n");
                                                sim_oline = lp;         /* specify output socket */
                                                if (cmdp->action == &x_help_cmd)
                                                    x_help_cmd (0, cptr);
                                                else
                                                    sim_remote_process_command ();
                                                stat = SCPE_OK;         /* any message has already been emitted */
                                                }
                                            else {
                                                sim_debug (DBG_CMD, &sim_remote_console, "Processing Command via SCPE_REMOTE\n");
                                                stat = SCPE_REMOTE;     /* force processing outside of sim_instr() */
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            else
                stat = SCPE_INVREM;
            }
        sim_rem_active_number = -1;
        if ((stat != SCPE_OK) && (stat != SCPE_REMOTE))
            stat = _sim_rem_message (gbuf, stat);
        _sim_rem_log_out (lp);
        if (master_session && !sim_rem_master_mode) {
            rem->single_mode = TRUE;
            return SCPE_STOP;
            }
        if (cmdp && (cmdp->action == &x_continue_cmd)) {
            sim_rem_cmd_active_line = -1;                   /* Not active_command */
            if (sim_log_temp &&                             /* If we setup a temporary log, clean it now  */
                (!sim_rem_master_mode)) {
                int32 save_quiet = sim_quiet;

                sim_quiet = 1;
                sim_set_logoff (0, NULL);
                sim_quiet = save_quiet;
                (void)remove (sim_rem_con_temp_name);
                sim_log_temp = FALSE;
                }
            else {
                fflush (sim_log);
                sim_rem_cmd_log_start = sim_ftell (sim_log);
                }
            if (!rem->single_mode) {
                tmxr_linemsg (lp, "Simulator Running...");
                tmxr_send_buffered_data (lp);
                for (j=0; j < sim_rem_con_tmxr.lines; j++) {
                    TMLN *lpj = &sim_rem_con_tmxr.ldsc[j];
                    if ((i == j) || (!lpj->conn))
                        continue;
                    tmxr_linemsg (lpj, "Simulator Running...");
                    tmxr_send_buffered_data (lpj);
                    }
                sim_is_running = TRUE;
                sim_start_timer_services ();
                }
            if (cmdp && (cmdp->action == &x_continue_cmd))
                rem->single_mode = TRUE;
            else {
                if (!rem->single_mode) {
                    if (master_session)
                        tmxr_linemsgf (lp, "%s", "sim> ");
                    else
                        tmxr_linemsgf (lp, "%s", sim_prompt);
                    tmxr_send_buffered_data (lp);
                    }
                }
            break;
            }
        if ((cmdp && (cmdp->action == &x_step_cmd)) ||
            (stat == SCPE_REMOTE)) {
            sim_rem_cmd_active_line = i;
            break;
            }
        }
    if (close_session) {
        tmxr_linemsgf (lp, "\r\nGoodbye\r\n");
        tmxr_send_buffered_data (lp);                       /* flush any buffered data */
        tmxr_reset_ln (lp);
        rem->single_mode = FALSE;
        }
    }
if (sim_rem_master_was_connected &&                         /* Master mode ever connected? */
    !sim_rem_con_tmxr.ldsc[0].sock)                         /* Master Connection lost? */
    return sim_messagef (SCPE_EXIT, "Master Session Disconnect");/* simulator has been 'unplugged' */
if (sim_rem_cmd_active_line != -1) {
    if (steps) {
        if (!sim_con_stable_registers && sim_rem_master_mode) {
            sim_step = steps;
            sim_sched_step ();
            }
        else
            sim_activate(uptr, steps);                      /* check again after 'steps' instructions */
        }
    else
        return SCPE_REMOTE;                                 /* force sim_instr() to exit to process command */
    }
else
    sim_activate_after(uptr, 100000);                       /* check again in 100 milliaeconds */
if (sim_rem_master_was_enabled && !sim_rem_master_mode) {   /* Transitioning out of master mode? */
    lp = &sim_rem_con_tmxr.ldsc[0];
    tmxr_linemsgf (lp, "Non Master Mode Session...");       /* report transition */
    tmxr_send_buffered_data (lp);                           /* flush any buffered data */
    return SCPE_STOP|SCPE_NOMESSAGE;                        /* Unwind to the normal input path */
    }
else
    return SCPE_OK;                                         /* keep going */
}

t_stat sim_rem_con_reset (DEVICE *dptr)
{
if (sim_rem_con_tmxr.lines) {
    int32 i;

    sim_debug (DBG_REP, &sim_remote_console, "sim_rem_con_reset(lines=%d)\n", sim_rem_con_tmxr.lines);
    for (i=0; i<sim_rem_con_tmxr.lines; i++) {
        REMOTE *rem = &sim_rem_consoles[i];

        if (!sim_rem_con_tmxr.ldsc[i].conn)
            continue;
        sim_debug (DBG_REP, &sim_remote_console, "sim_rem_con_reset(line=%d, usecs=%d)\n", i, rem->repeat_interval);
        if (rem->repeat_interval)
            sim_activate_after (&rem_con_repeat_units[rem->line], rem->repeat_interval);    /* schedule */
        if (rem->smp_reg_count)
            sim_activate (&rem_con_smp_smpl_units[rem->line], rem->smp_sample_interval);    /* schedule */
        }
    sim_activate_after (rem_con_data_unit, 100000);         /* continue polling for open sessions */
    return sim_rem_con_poll_svc (rem_con_poll_unit);        /* establish polling for new sessions */
    }
return SCPE_OK;
}

static t_stat sim_set_rem_telnet (int32 flag, CONST char *cptr)
{
t_stat r;

if (flag) {
    r = sim_parse_addr (cptr, NULL, 0, NULL, NULL, 0, NULL, NULL);
    if (r == SCPE_OK) {
        if (sim_rem_con_tmxr.master)                        /* already open? */
            sim_set_rem_telnet (0, NULL);                   /* close first */
        if (sim_rem_con_tmxr.lines == 0)                    /* if no connection limit set */
            sim_set_rem_connections (0, "1");               /* use 1 */
        sim_rem_con_tmxr.buffered = 8192;                   /* Use big enough buffers */
        sim_register_internal_device (&sim_remote_console);
        r = tmxr_attach (&sim_rem_con_tmxr, rem_con_poll_unit, cptr);/* open master socket */
        if (r == SCPE_OK)
            sim_activate_after(rem_con_poll_unit, 1000000);/* check for connection in 1 second */
        return r;
        }
    return SCPE_NOPARAM;
    }
else {
    if (sim_rem_con_tmxr.master) {
        int32 i;

        tmxr_detach (&sim_rem_con_tmxr, rem_con_poll_unit);
        for (i=0; i<sim_rem_con_tmxr.lines; i++) {
            REMOTE *rem = &sim_rem_consoles[i];
            free (rem->buf);
            rem->buf = NULL;
            rem->buf_size = 0;
            rem->buf_ptr = 0;
            rem->single_mode = TRUE;
            }
        }
    }
return SCPE_OK;
}

static t_stat sim_set_rem_connections (int32 flag, CONST char *cptr)
{
int32 lines;
REMOTE *rem;
t_stat r;
int32 i;

if (cptr == NULL)
    return SCPE_ARG;
lines = (int32) get_uint (cptr, 10, MAX_REMOTE_SESSIONS, &r);
if (r != SCPE_OK)
    return r;
if (sim_rem_con_tmxr.master)
    return SCPE_ALATT;
if (sim_rem_con_tmxr.lines) {
    sim_cancel (rem_con_poll_unit);
    sim_cancel (rem_con_data_unit);
    }
for (i=0; i<sim_rem_con_tmxr.lines; i++) {
    rem = &sim_rem_consoles[i];
    free (rem->buf);
    free (rem->act_buf);
    free (rem->act);
    free (rem->repeat_action);
    sim_cancel (&rem_con_repeat_units[i]);
    sim_cancel (&rem_con_smp_smpl_units[i]);
    }
sim_rem_con_tmxr.lines = lines;
sim_rem_con_tmxr.ldsc = (TMLN *)realloc (sim_rem_con_tmxr.ldsc, sizeof(*sim_rem_con_tmxr.ldsc)*lines);
memset (sim_rem_con_tmxr.ldsc, 0, sizeof(*sim_rem_con_tmxr.ldsc)*lines);
sim_remote_console.units = (UNIT *)realloc (sim_remote_console.units, sizeof(*sim_remote_console.units)*((2 * lines) + REM_CON_BASE_UNITS));
memset (sim_remote_console.units, 0, sizeof(*sim_remote_console.units)*((2 * lines) + REM_CON_BASE_UNITS));
sim_remote_console.numunits = (2 * lines) + REM_CON_BASE_UNITS;
rem_con_poll_unit->action = &sim_rem_con_poll_svc;/* remote console connection polling unit */
rem_con_poll_unit->flags |= UNIT_IDLE;
rem_con_data_unit->action = &sim_rem_con_data_svc;/* console data handling unit */
rem_con_data_unit->flags |= UNIT_IDLE|UNIT_DIS;
sim_rem_consoles = (REMOTE *)realloc (sim_rem_consoles, sizeof(*sim_rem_consoles)*lines);
memset (sim_rem_consoles, 0, sizeof(*sim_rem_consoles)*lines);
sim_rem_command_buf = (char *)realloc (sim_rem_command_buf, 4*CBUFSIZE+1);
memset (sim_rem_command_buf, 0, 4*CBUFSIZE+1);
for (i=0; i<lines; i++) {
    rem_con_repeat_units[i].flags = UNIT_DIS;
    rem_con_repeat_units[i].action = &sim_rem_con_repeat_svc;
    rem_con_smp_smpl_units[i].flags = UNIT_DIS;
    rem_con_smp_smpl_units[i].action = &sim_rem_con_smp_collect_svc;
    rem = &sim_rem_consoles[i];
    rem->line = i;
    rem->lp = &sim_rem_con_tmxr.ldsc[i];
    rem->uptr = &rem_con_repeat_units[i];
    }
return SCPE_OK;
}

static t_stat sim_set_rem_timeout (int32 flag, CONST char *cptr)
{
int32 timeout;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
timeout = (int32) get_uint (cptr, 10, 3600, &r);
if (r != SCPE_OK)
    return r;
if (sim_rem_active_number >= 0)
    sim_rem_consoles[sim_rem_active_number].read_timeout = timeout;
else
    sim_rem_read_timeout = timeout;
return SCPE_OK;
}

static t_stat sim_set_rem_bufsize (int32 flag, CONST char *cptr)
{
char cmdbuf[CBUFSIZE];
int32 bufsize;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
bufsize = (int32) get_uint (cptr, 10, 32768, &r);
if (r != SCPE_OK)
    return r;
if (bufsize < 1400)
    return sim_messagef (SCPE_ARG, "%d is too small.  Minimum size is 1400\n", bufsize);
sprintf(cmdbuf, "BUFFERED=%d", bufsize);
return tmxr_open_master (&sim_rem_con_tmxr, cmdbuf);        /* open master socket */
}

t_bool sim_is_remote_console_master_line (void *lp)
{
return sim_rem_master_mode &&                                           /* master mode */
       (((TMLN *)lp) >= sim_rem_con_tmxr.ldsc) &&                       /* And it is one of the Remote Console Lines */
       (((TMLN *)lp) < sim_rem_con_tmxr.ldsc + sim_rem_con_tmxr.lines);
}

/* Enable or disable Remote Console master mode */

/* In master mode, commands are subsequently processed from the
   primary/initial (master mode) remote console session.  Commands
   are processed from that source until that source disables master
   mode or the simulator exits 
 */

static t_stat sim_set_rem_master (int32 flag, CONST char *cptr)
{
t_stat stat = SCPE_OK;

if (cptr && *cptr)
    return SCPE_2MARG;

if (sim_rem_active_number > 0)
    return sim_messagef (SCPE_INVREM, "Can't change Remote Console mode from Remote Console\n");

if (sim_rem_con_tmxr.master || (!flag))                     /* Remote Console Enabled? */
    sim_rem_master_mode = flag;
else
    return sim_messagef (SCPE_INVREM, "Can't enable Remote Console Master mode with Remote Console disabled\n");

if (sim_rem_master_mode) {
    t_stat stat_nomessage = 0;

    sim_messagef (SCPE_OK, "Command input starting on Master Remote Console Session\n");
    stat = sim_run_boot_prep (0);
    sim_rem_master_was_enabled = TRUE;
    sim_last_cmd_stat = SCPE_OK;
    while (sim_rem_master_mode) {
        char *brk_action;

        sim_rem_consoles[0].single_mode = FALSE;
        sim_cancel (rem_con_data_unit);
        sim_activate (rem_con_data_unit, -1);
        stat = run_cmd (RU_GO, "");
        if (stat != SCPE_TTMO) {
            stat_nomessage = stat & SCPE_NOMESSAGE;         /* extract possible message supression flag */
            stat = _sim_rem_message ("RUN", stat);
            }
        brk_action = sim_brk_replace_act (NULL);
        sim_debug (DBG_MOD, &sim_remote_console, "Master Session Returned: Status - %d Active_Line: %d, Mode: %s, Active Cmd: %s%s%s\n", stat, sim_rem_cmd_active_line, sim_rem_consoles[0].single_mode ? "Single" : "^E Stopped", sim_rem_active_command ? sim_rem_active_command->name : "", brk_action ? " Break Action: " : "", brk_action ? brk_action : "");
        if (stat == SCPE_EXIT)
            sim_rem_master_mode = FALSE;
        if (brk_action) {
            free (sim_rem_consoles[0].act);
            sim_rem_consoles[0].act = brk_action;
            }
        sim_rem_cmd_active_line = 0;                    /* Make it look like */
        sim_rem_consoles[0].single_mode = FALSE;
        sim_cancel_step ();
        if (stat != SCPE_STEP)
            sim_rem_active_command = &allowed_single_remote_cmds[0];/* Dummy */
        else
            sim_activate_abs (rem_con_data_unit, 0);    /* force step completion processing */
        sim_last_cmd_stat = SCPE_BARE_STATUS(stat);     /* make exit status available to remote console */
        }
    sim_rem_master_was_enabled = FALSE;
    sim_rem_master_was_connected = FALSE;
    if (sim_log_temp) {                                     /* If we setup a temporary log, clean it now  */
        int32 save_quiet = sim_quiet;

        sim_quiet = 1;
        sim_set_logoff (0, NULL);
        sim_quiet = save_quiet;
        (void)remove (sim_rem_con_temp_name);
        sim_log_temp = FALSE;
        }
    stat |= stat_nomessage;
    }
else {
    sim_rem_consoles[0].single_mode = TRUE;                 /* Force remote session into single command mode */
    }
return stat;
}

/* Set keyboard map */

t_stat sim_set_kmap (int32 flag, CONST char *cptr)
{
DEVICE *dptr = sim_devices[0];
int32 val, rdx;
t_stat r;

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_2FARG;
if (dptr->dradix == 16)
    rdx = 16;
else
    rdx = 8;
val = (int32) get_uint (cptr, rdx, 0177, &r);
if ((r != SCPE_OK) ||
    ((val == 0) && (flag & KMAP_NZ)))
    return SCPE_ARG;
*(cons_kmap[flag & KMAP_MASK]) = val;
sigint_message_issued = FALSE;
return SCPE_OK;
}

/* Show keyboard map */

t_stat sim_show_kmap (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
int32 kmap_char = *(cons_kmap[flag & KMAP_MASK]);

if (sim_devices[0]->dradix == 16)
    fprintf (st, "%s = 0x%X", show_con_tab[flag].name, kmap_char);
else
    fprintf (st, "%s = 0%o", show_con_tab[flag].name, kmap_char);
if (isprint(kmap_char&0xFF))
    fprintf (st, " = '%c'\n", kmap_char&0xFF);
else
    if (kmap_char <= 26)
        fprintf (st, " = ^%c\n", '@' + (kmap_char&0xFF));
    else
        fprintf (st, "\n");
return SCPE_OK;
}

/* Set printable characters */

t_stat sim_set_pchar (int32 flag, CONST char *cptr)
{
DEVICE *dptr = sim_devices[0];
uint32 val, rdx;
t_stat r;

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_2FARG;
if (dptr->dradix == 16) rdx = 16;
else rdx = 8;
val = (uint32) get_uint (cptr, rdx, 0xFFFFFFFF, &r);
if ((r != SCPE_OK) ||
    ((val & 0x00002400) == 0))
    return SCPE_ARG;
sim_tt_pchar = val;
return SCPE_OK;
}

/* Show printable characters */

t_stat sim_show_pchar (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
if (sim_devices[0]->dradix == 16)
    fprintf (st, "pchar mask = %X", sim_tt_pchar);
else
    fprintf (st, "pchar mask = %o", sim_tt_pchar);
if (sim_tt_pchar) {
    static const char *pchars[] = {"NUL(^@)", "SOH(^A)", "STX(^B)", "ETX(^C)", "EOT(^D)", "ENQ(^E)", "ACK(^F)", "BEL(^G)", 
                                   "BS(^H)" , "HT(^I)",  "LF(^J)",  "VT(^K)",  "FF(^L)",  "CR(^M)",  "SO(^N)",  "SI(^O)",
                                   "DLE(^P)", "DC1(^Q)", "DC2(^R)", "DC3(^S)", "DC4(^T)", "NAK(^U)", "SYN(^V)", "ETB(^W)",
                                   "CAN(^X)", "EM(^Y)",  "SUB(^Z)", "ESC",     "FS",      "GS",      "RS",      "US"};
    int i;
    t_bool found = FALSE;

    fprintf (st, " {");
    for (i=31; i>=0; i--)
        if (sim_tt_pchar & (1 << i)) {
            fprintf (st, "%s%s", found ? "," : "", pchars[i]);
            found = TRUE;
            }
    fprintf (st, "}");
    }
fprintf (st, "\n");
return SCPE_OK;
}

/* Set input speed (bps) */

t_stat sim_set_cons_speed (int32 flag, CONST char *cptr)
{
return tmxr_set_line_speed (&sim_con_ldsc, cptr);
}

t_stat sim_show_cons_speed (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
if (sim_con_ldsc.rxbps) {
    fprintf (st, "Speed = %d", sim_con_ldsc.rxbps);
    if (sim_con_ldsc.bpsfactor != 1.0)
        fprintf (st, "*%.0f", sim_con_ldsc.bpsfactor);
    fprintf (st, " bps\n");
    }
return SCPE_OK;
}

/* Set log routine */

t_stat sim_set_logon (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
t_stat r;
time_t now;

if ((cptr == NULL) || (*cptr == 0))                     /* need arg */
    return SCPE_2FARG;
cptr = get_glyph_nc (cptr, gbuf, 0);                    /* get file name */
if (*cptr != 0)                                         /* now eol? */
    return SCPE_2MARG;
sim_set_logoff (0, NULL);                               /* close cur log */
r = sim_open_logfile (gbuf, (sim_switches & SWMASK ('B')) == SWMASK ('B'), 
                            &sim_log, &sim_log_ref);    /* open log */
if (r != SCPE_OK)                                       /* error? */
    return r;
if ((!sim_quiet) && (!(sim_switches & SWMASK ('Q'))))
    fprintf (stdout, "Logging to file \"%s\"\n", 
             sim_logfile_name (sim_log, sim_log_ref));
fprintf (sim_log, "Logging to file \"%s\"\n", 
             sim_logfile_name (sim_log, sim_log_ref));  /* start of log */
time(&now);
if ((!sim_quiet) && (!(sim_switches & SWMASK ('Q'))))
    fprintf (sim_log, "Logging to file \"%s\" at %s", sim_logfile_name (sim_log, sim_log_ref), ctime(&now));
return SCPE_OK;
}

/* Set nolog routine */

t_stat sim_set_logoff (int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
if (sim_log == NULL)                                    /* no log? */
    return SCPE_OK;
if ((!sim_quiet) && (!(sim_switches & SWMASK ('Q'))))
    fprintf (stdout, "Log file closed\n");
fprintf (sim_log, "Log file closed\n");
sim_close_logfile (&sim_log_ref);                       /* close log */
sim_log = NULL;
return SCPE_OK;
}

/* Show log status */

t_stat sim_show_log (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
if (sim_log)
    fprintf (st, "Logging enabled to \"%s\"\n", 
                 sim_logfile_name (sim_log, sim_log_ref));
else
    fprintf (st, "Logging disabled\n");
return SCPE_OK;
}

/* Set debug switches */

int32 sim_set_deb_switches (int32 switches)
{
int32 old_deb_switches = sim_deb_switches;

sim_deb_switches = switches & 
                   (SWMASK ('R') | SWMASK ('P') | 
                    SWMASK ('T') | SWMASK ('A') | 
                    SWMASK ('F') | SWMASK ('N') |
                    SWMASK ('B') | SWMASK ('E') |
                    SWMASK ('D') );                 /* save debug switches */
return old_deb_switches;
}

/* Set debug routine */

t_stat sim_set_debon (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
t_stat r;
time_t now;
size_t buffer_size = 0;

if ((cptr == NULL) || (*cptr == 0))                     /* need arg */
    return SCPE_2FARG;
if (sim_switches & SWMASK ('B')) {
    cptr = get_glyph_nc (cptr, gbuf, 0);                /* buffer size */
    buffer_size = (size_t)strtoul (gbuf, NULL, 10);
    if ((buffer_size == 0) || (buffer_size > 1024))
        return sim_messagef (SCPE_ARG, "Invalid debug memory buffersize %u MB\n", (unsigned int)buffer_size);
    }
cptr = get_glyph_nc (cptr, gbuf, 0);                    /* get file name */
if (*cptr != 0)                                         /* now eol? */
    return SCPE_2MARG;
r = sim_open_logfile (gbuf, FALSE, &sim_deb, &sim_deb_ref);

if (r != SCPE_OK)
    return r;

sim_set_deb_switches (sim_switches);

if (sim_deb_switches & SWMASK ('R')) {
    struct tm loc_tm, gmt_tm;
    time_t time_t_now;

    sim_rtcn_get_time(&sim_deb_basetime, 0);
    time_t_now = (time_t)sim_deb_basetime.tv_sec;
    /* Adjust the relative timebase to reflect the localtime GMT offset */
    loc_tm = *localtime (&time_t_now);
    gmt_tm = *gmtime (&time_t_now);
    sim_deb_basetime.tv_sec -= mktime (&gmt_tm) - mktime (&loc_tm);
    if (!(sim_deb_switches & (SWMASK ('A') | SWMASK ('T'))))
        sim_deb_switches |= SWMASK ('T');
    }
sim_messagef (SCPE_OK, "Debug output to \"%s\"\n", sim_logfile_name (sim_deb, sim_deb_ref));
if (sim_deb_switches & SWMASK ('P'))
    sim_messagef (SCPE_OK, "   Debug messages contain current PC value\n");
if (sim_deb_switches & SWMASK ('T'))
    sim_messagef (SCPE_OK, "   Debug messages display time of day as hh:mm:ss.msec%s\n", sim_deb_switches & SWMASK ('R') ? " relative to the start of debugging" : "");
if (sim_deb_switches & SWMASK ('A'))
    sim_messagef (SCPE_OK, "   Debug messages display time of day as seconds.msec%s\n", sim_deb_switches & SWMASK ('R') ? " relative to the start of debugging" : "");
if (sim_deb_switches & SWMASK ('F'))
    sim_messagef (SCPE_OK, "   Debug messages will not be filtered to summarize duplicate lines\n");
if (sim_deb_switches & SWMASK ('E'))
    sim_messagef (SCPE_OK, "   Debug messages containing blob data in EBCDIC will display in readable form\n");
if (sim_deb_switches & SWMASK ('B'))
    sim_messagef (SCPE_OK, "   Debug messages will be written to a %u MB circular memory buffer\n", 
                                (unsigned int)buffer_size);
time(&now);
if (!sim_quiet) {
    fprintf (sim_deb, "Debug output to \"%s\" at %s", sim_logfile_name (sim_deb, sim_deb_ref), ctime(&now));
    show_version (sim_deb, NULL, NULL, 0, NULL);
    }
if (sim_deb_switches & SWMASK ('N'))
    sim_deb_switches &= ~SWMASK ('N');          /* Only process the -N flag initially */

if (sim_deb_switches & SWMASK ('B')) {
    sim_deb_buffer_size = (size_t)(1024 * 1024 * buffer_size);
    sim_deb_buffer = (char *)realloc (sim_deb_buffer, sim_deb_buffer_size);
    sim_debug_buffer_offset = sim_debug_buffer_inuse = 0;
    memset (sim_deb_buffer, 0, sim_deb_buffer_size);
    }

return SCPE_OK;
}

/* Set nodebug routine */

t_stat sim_set_deboff (int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
if (sim_deb == NULL)                                    /* no debug? */
    return SCPE_OK;
if (sim_deb_switches & SWMASK ('B')) {
    size_t offset = (sim_debug_buffer_inuse == sim_deb_buffer_size) ? sim_debug_buffer_offset : 0;
    const char *bufmsg = "Circular Buffer Contents follow here:\n\n";

    fwrite (bufmsg, 1, strlen (bufmsg), sim_deb);

    while (sim_debug_buffer_inuse > 0) {
        size_t write_size = MIN (sim_deb_buffer_size - offset, sim_debug_buffer_inuse);

        fwrite (sim_deb_buffer + offset, 1, write_size, sim_deb);
        sim_debug_buffer_inuse -= write_size;
        offset += write_size;
        if (offset == sim_deb_buffer_size)
            offset = 0;
        }
    free (sim_deb_buffer);
    sim_deb_buffer = NULL;
    sim_deb_buffer_size = sim_debug_buffer_offset = sim_debug_buffer_inuse = 0;
    }
sim_close_logfile (&sim_deb_ref);
sim_deb = NULL;
sim_deb_switches = 0;
return sim_messagef (SCPE_OK, "Debug output disabled\n");
}

/* Show debug routine */

t_stat sim_show_debug (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
int32 i;

if (cptr && (*cptr != 0))
    return SCPE_2MARG;
if (sim_deb) {
    fprintf (st, "Debug output enabled to \"%s\"\n", 
                 sim_logfile_name (sim_deb, sim_deb_ref));
    if (sim_deb_switches & SWMASK ('P'))
        fprintf (st, "   Debug messages contain current PC value\n");
    if (sim_deb_switches & SWMASK ('T'))
        fprintf (st, "   Debug messages display time of day as hh:mm:ss.msec%s\n", sim_deb_switches & SWMASK ('R') ? " relative to the start of debugging" : "");
    if (sim_deb_switches & SWMASK ('A'))
        fprintf (st, "   Debug messages display time of day as seconds.msec%s\n", sim_deb_switches & SWMASK ('R') ? " relative to the start of debugging" : "");
    if (sim_deb_switches & SWMASK ('F'))
        fprintf (st, "   Debug messages are not being filtered to summarize duplicate lines\n");
    if (sim_deb_switches & SWMASK ('E'))
        fprintf (st, "   Debug messages containing blob data in EBCDIC will display in readable form\n");
    for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
        t_bool unit_debug = FALSE;
        uint32 unit;

        for (unit = 0; unit < dptr->numunits; unit++)
            if (dptr->units[unit].dctrl) {
                unit_debug = TRUE;
                break;
                }
        if (!(dptr->flags & DEV_DIS) &&
            ((dptr->flags & DEV_DEBUG) || (dptr->debflags)) &&
            ((dptr->dctrl) || unit_debug)) {
            fprintf (st, "Device: %-6s ", dptr->name);
            show_dev_debug (st, dptr, NULL, 0, NULL);
            }
        }
    for (i = 0; sim_internal_device_count && (dptr = sim_internal_devices[i]); ++i) {
        t_bool unit_debug = FALSE;
        uint32 unit;

        for (unit = 0; unit < dptr->numunits; unit++)
            if (dptr->units[unit].dctrl) {
                unit_debug = TRUE;
                break;
                }
        if (!(dptr->flags & DEV_DIS) &&
            ((dptr->flags & DEV_DEBUG) || (dptr->debflags)) &&
            ((dptr->dctrl) || unit_debug)) {
            fprintf (st, "Device: %-6s ", dptr->name);
            show_dev_debug (st, dptr, NULL, 0, NULL);
            }
        }
    }
else
    fprintf (st, "Debug output disabled\n");
return SCPE_OK;
}

/* SET CONSOLE command */

/* Set console to Telnet port (and parameters) */

t_stat sim_set_telnet (int32 flag, CONST char *cptr)
{
char *cvptr, gbuf[CBUFSIZE];
CTAB *ctptr;
t_stat r;

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_2FARG;
while (*cptr != 0) {                                    /* do all mods */
    cptr = get_glyph_nc (cptr, gbuf, ',');              /* get modifier */
    if ((cvptr = strchr (gbuf, '=')))                   /* = value? */
        *cvptr++ = 0;
    get_glyph (gbuf, gbuf, 0);                          /* modifier to UC */
    if ((ctptr = find_ctab (set_con_telnet_tab, gbuf))) { /* match? */
        r = ctptr->action (ctptr->arg, cvptr);          /* do the rest */
        if (r != SCPE_OK)
            return r;
        }
    else {
        if (sim_con_tmxr.master)                        /* already open? */
            sim_set_notelnet (0, NULL);                 /* close first */
        r = tmxr_attach (&sim_con_tmxr, &sim_con_unit, gbuf);/* open master socket */
        if (r == SCPE_OK)
            sim_activate_after(&sim_con_unit, 1000000); /* check for connection in 1 second */
        else
            return r;
        }
    }
return SCPE_OK;
}

/* Close console Telnet port */

t_stat sim_set_notelnet (int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))                               /* too many arguments? */
    return SCPE_2MARG;
if (sim_con_tmxr.master == 0)                           /* ignore if already closed */
    return SCPE_OK;
return tmxr_close_master (&sim_con_tmxr);               /* close master socket */
}

/* Show console Telnet status */

t_stat sim_show_telnet (FILE *st, DEVICE *dunused, UNIT *uunused, int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
if ((sim_con_tmxr.master == 0) && 
    (sim_con_ldsc.serport == 0))
    fprintf (st, "Connected to console window\n");
else {
    if (sim_con_ldsc.serport) {
        fprintf (st, "Connected to ");
        tmxr_fconns (st, &sim_con_ldsc, -1);
        }
    else 
        if (sim_con_ldsc.sock == 0)
            fprintf (st, "Listening on port %s\n", sim_con_tmxr.port);
        else {
            fprintf (st, "Listening on port %s, connection from %s\n",
                sim_con_tmxr.port, sim_con_ldsc.ipad);
            tmxr_fconns (st, &sim_con_ldsc, -1);
            }
    tmxr_fstats (st, &sim_con_ldsc, -1);
    }
return SCPE_OK;
}

/* Set console to Buffering  */

t_stat sim_set_cons_buff (int32 flg, CONST char *cptr)
{
char cmdbuf[CBUFSIZE];

sprintf(cmdbuf, "BUFFERED%c%s", cptr ? '=' : '\0', cptr ? cptr : "");
return tmxr_open_master (&sim_con_tmxr, cmdbuf);      /* open master socket */
}

/* Set console to NoBuffering */

t_stat sim_set_cons_unbuff (int32 flg, CONST char *cptr)
{
char cmdbuf[CBUFSIZE];

sprintf(cmdbuf, "UNBUFFERED%c%s", cptr ? '=' : '\0', cptr ? cptr : "");
return tmxr_open_master (&sim_con_tmxr, cmdbuf);      /* open master socket */
}

/* Set console to Logging */

t_stat sim_set_cons_log (int32 flg, CONST char *cptr)
{
char cmdbuf[CBUFSIZE];

sprintf(cmdbuf, "LOG%c%s", cptr ? '=' : '\0', cptr ? cptr : "");
return tmxr_open_master (&sim_con_tmxr, cmdbuf);      /* open master socket */
}

/* Set console to NoLogging */

t_stat sim_set_cons_nolog (int32 flg, CONST char *cptr)
{
char cmdbuf[CBUFSIZE];

sprintf(cmdbuf, "NOLOG%c%s", cptr ? '=' : '\0', cptr ? cptr : "");
return tmxr_open_master (&sim_con_tmxr, cmdbuf);      /* open master socket */
}

t_stat sim_show_cons_log (FILE *st, DEVICE *dunused, UNIT *uunused, int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
if (sim_con_tmxr.ldsc->txlog)
    fprintf (st, "Log File being written to %s\n", sim_con_tmxr.ldsc->txlogname);
else
    fprintf (st, "No Logging\n");
return SCPE_OK;
}

t_stat sim_show_cons_buff (FILE *st, DEVICE *dunused, UNIT *uunused, int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
if (!sim_con_tmxr.ldsc->txbfd)
    fprintf (st, "Unbuffered\n");
else
    fprintf (st, "Buffer Size = %d\n", sim_con_tmxr.ldsc->txbsz);
return SCPE_OK;
}

/* Set console Debug Mode */

t_stat sim_set_cons_debug (int32 flg, CONST char *cptr)
{
return set_dev_debug (&sim_con_telnet, &sim_con_unit, flg, cptr);
}

t_stat sim_show_cons_debug (FILE *st, DEVICE *dunused, UNIT *uunused, int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
return show_dev_debug (st, &sim_con_telnet, &sim_con_unit, flag, cptr);
}

/* Set console to Serial port (and parameters) */

t_stat sim_set_serial (int32 flag, CONST char *cptr)
{
char *cvptr, gbuf[CBUFSIZE], ubuf[CBUFSIZE];
CTAB *ctptr;
t_stat r;

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_2FARG;
while (*cptr != 0) {                                    /* do all mods */
    cptr = get_glyph_nc (cptr, gbuf, ',');              /* get modifier */
    if ((cvptr = strchr (gbuf, '=')))                   /* = value? */
        *cvptr++ = 0;
    get_glyph (gbuf, ubuf, 0);                          /* modifier to UC */
    if ((ctptr = find_ctab (set_con_serial_tab, ubuf))) { /* match? */
        r = ctptr->action (ctptr->arg, cvptr);          /* do the rest */
        if (r != SCPE_OK)
            return r;
        }
    else {
        SERHANDLE serport = sim_open_serial (gbuf, NULL, &r);
        if (serport != INVALID_HANDLE) {
            sim_close_serial (serport);
            if (r == SCPE_OK) {
                char cbuf[CBUFSIZE+10];

                if ((sim_con_tmxr.master) ||            /* already open? */
                    (sim_con_ldsc.serport))
                    sim_set_noserial (0, NULL);         /* close first */
                sprintf(cbuf, "Connect=%s", gbuf);
                r = tmxr_attach (&sim_con_tmxr, &sim_con_unit, cbuf);/* open master socket */
                sim_con_ldsc.rcve = 1;                  /* rcv enabled */
                if (r == SCPE_OK)
                    sim_activate_after(&sim_con_unit, 1000000); /* check for connection in 1 second */
                return r;
                }
            }
        return SCPE_ARG;
        }
    }
return SCPE_OK;
}

/* Close console Serial port */

t_stat sim_set_noserial (int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))                               /* too many arguments? */
    return SCPE_2MARG;
if (sim_con_ldsc.serport == 0)                          /* ignore if already closed */
    return SCPE_OK;
return tmxr_close_master (&sim_con_tmxr);               /* close master socket */
}

/* Show the console expect rules and state */

t_stat sim_show_cons_expect (FILE *st, DEVICE *dunused, UNIT *uunused, int32 flag, CONST char *cptr)
{
fprintf (st, "Console Expect processing:\n");
return sim_exp_show (st, &sim_con_expect, cptr);
}

/* Log File Open/Close/Show Support */

/* Open log file */

t_stat sim_open_logfile (const char *filename, t_bool binary, FILE **pf, FILEREF **pref)
{
char gbuf[CBUFSIZE];
const char *tptr;

if ((filename == NULL) || (*filename == 0))             /* too few arguments? */
    return SCPE_2FARG;
tptr = get_glyph (filename, gbuf, 0);
if (*tptr != 0)                                         /* now eol? */
    return SCPE_2MARG;
sim_close_logfile (pref);
*pf = NULL;
if (strcmp (gbuf, "LOG") == 0) {                        /* output to log? */
    if (sim_log == NULL)                                /* any log? */
        return SCPE_ARG;
    *pf = sim_log;
    *pref = sim_log_ref;
    if (*pref)
        ++(*pref)->refcount;
    }
else if (strcmp (gbuf, "DEBUG") == 0) {                 /* output to debug? */
    if (sim_deb == NULL)                                /* any debug? */
        return SCPE_ARG;
    *pf = sim_deb;
    *pref = sim_deb_ref;
    if (*pref)
        ++(*pref)->refcount;
    }
else if (strcmp (gbuf, "STDOUT") == 0) {                /* output to stdout? */
    *pf = stdout;
    *pref = NULL;
    }
else if (strcmp (gbuf, "STDERR") == 0) {                /* output to stderr? */
    *pf = stderr;
    *pref = NULL;
    }
else {
    *pref = (FILEREF *)calloc (1, sizeof(**pref));
    if (!*pref)
        return SCPE_MEM;
    get_glyph_nc (filename, gbuf, 0);                   /* reparse */
    strlcpy ((*pref)->name, gbuf, sizeof((*pref)->name));
    if (sim_switches & SWMASK ('N'))                    /* if a new log file is requested */
        *pf = sim_fopen (gbuf, (binary ? "w+b" : "w+"));/*   then open an empty file */
    else                                                /* otherwise */
        *pf = sim_fopen (gbuf, (binary ? "a+b" : "a+"));/*   append to an existing file */
    if (*pf == NULL) {                                  /* error? */
        free (*pref);
        *pref = NULL;
        return SCPE_OPENERR;
        }
    setvbuf (*pf, NULL, _IOFBF, 65536);
    (*pref)->file = *pf;
    (*pref)->refcount = 1;                               /* need close */
    }
return SCPE_OK;
}

/* Close log file */

t_stat sim_close_logfile (FILEREF **pref)
{
if (NULL == *pref)
    return SCPE_OK;
(*pref)->refcount = (*pref)->refcount  - 1;
if ((*pref)->refcount > 0) {
    *pref = NULL;
    return SCPE_OK;
    }
fclose ((*pref)->file);
free (*pref);
*pref = NULL;
return SCPE_OK;
}

/* Show logfile support routine */

const char *sim_logfile_name (FILE *st, FILEREF *ref)
{
if (!st)
    return "";
if (st == stdout)
    return "STDOUT";
if (st == stderr)
    return "STDERR";
if (!ref)
    return "";
return ref->name;
}

/* Check connection before executing 
   (including a remote console which may be required in master mode) */

t_stat sim_check_console (int32 sec)
{
int32 c, trys = 0;

if (sim_rem_master_mode) {
    for (;trys < sec; ++trys) {
        sim_rem_con_poll_svc (rem_con_poll_unit);
        if (sim_rem_con_tmxr.ldsc[0].conn)
            break;
        if ((trys % 10) == 0) {                         /* Status every 10 sec */
            sim_messagef (SCPE_OK, "Waiting for Remote Console connection\r\n");
            fflush (stdout);
            if (sim_log)                                /* log file? */
                fflush (sim_log);
            }
        sim_os_sleep (1);                               /* wait 1 second */
        }
    if ((sim_rem_con_tmxr.ldsc[0].conn) &&
        (!sim_con_ldsc.serport) &&
        (sim_con_tmxr.master == 0) &&
        (sim_con_console_port)) {
        tmxr_linemsgf (&sim_rem_con_tmxr.ldsc[0], "\r\nConsole port must be Telnet or Serial with Master Remote Console\r\n");
        tmxr_linemsgf (&sim_rem_con_tmxr.ldsc[0], "Goodbye\r\n");
        while (tmxr_send_buffered_data (&sim_rem_con_tmxr.ldsc[0]))
            sim_os_ms_sleep (100);
        sim_os_ms_sleep (100);
        tmxr_reset_ln (&sim_rem_con_tmxr.ldsc[0]);
        return sim_messagef (SCPE_EXIT, "Console port must be Telnet or Serial with Master Remote Console\r\n");
        }
    }
if (trys == sec) {
    return SCPE_TTMO;                                   /* timed out */
    }
if (sim_con_ldsc.serport)
    if (tmxr_poll_conn (&sim_con_tmxr) >= 0) 
        sim_con_ldsc.rcve = 1;                          /* rcv enabled */
if ((sim_con_tmxr.master == 0) ||                       /* serial console or not Telnet? done */
    (sim_con_ldsc.serport))
    return SCPE_OK;
if (sim_con_ldsc.conn || sim_con_ldsc.txbfd) {          /* connected or buffered ? */
    tmxr_poll_rx (&sim_con_tmxr);                       /* poll (check disconn) */
    if (sim_con_ldsc.conn || sim_con_ldsc.txbfd) {      /* still connected? */
        if (!sim_con_ldsc.conn) {
            sim_messagef (SCPE_OK, "Running with Buffered Console\r\n"); /* print transition */
            fflush (stdout);
            if (sim_log)                                /* log file? */
                fflush (sim_log);
            }
        return SCPE_OK;
        }
    }
for (; trys < sec; trys++) {                            /* loop */
    if (tmxr_poll_conn (&sim_con_tmxr) >= 0) {          /* poll connect */
        sim_con_ldsc.rcve = 1;                          /* rcv enabled */
        if (trys) {                                     /* if delayed */
            sim_messagef (SCPE_OK, "Running\r\n");                 /* print transition */
            fflush (stdout);
            if (sim_log)                                /* log file? */
                fflush (sim_log);
            }
        return SCPE_OK;                                 /* ready to proceed */
        }
    c = sim_os_poll_kbd ();                             /* check for stop char */
    if ((c == SCPE_STOP) || stop_cpu)
        return SCPE_STOP;
    if ((trys % 10) == 0) {                             /* Status every 10 sec */
        sim_messagef (SCPE_OK, "Waiting for console Telnet connection\r\n");
        fflush (stdout);
        if (sim_log)                                    /* log file? */
            fflush (sim_log);
        }
    sim_os_sleep (1);                                   /* wait 1 second */
    }
return SCPE_TTMO;                                       /* timed out */
}

/* Get Send object address for console */

SEND *sim_cons_get_send (void)
{
return &sim_con_send;
}

/* Get Expect object address for console */

EXPECT *sim_cons_get_expect (void)
{
return &sim_con_expect;
}

/* Display console Queued input data status */

t_stat sim_show_cons_send_input (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
fprintf (st, "Console Send processing:\n");
return sim_show_send_input (st, &sim_con_send);
}

/* Poll for character */

t_stat sim_poll_kbd (void)
{
t_stat c;

sim_last_poll_kbd_time = sim_os_msec ();                    /* record when this poll happened */
if (sim_send_poll_data (&sim_con_send, &c))                 /* injected input characters available? */
    return c;
if (!sim_rem_master_mode) {
    if ((sim_con_ldsc.rxbps) &&                             /* rate limiting && */
        (sim_gtime () < sim_con_ldsc.rxnexttime))           /* too soon? */
        return SCPE_OK;                                     /* not yet */
    if (sim_ttisatty ())
        c = sim_os_poll_kbd ();                             /* get character */
    else
        c = SCPE_OK;
    if (c == SCPE_STOP) {                                   /* ^E */
        stop_cpu = TRUE;                                    /* Force a stop (which is picked up by sim_process_event */
        return SCPE_OK;
        }
    if ((sim_con_tmxr.master == 0) &&                       /* not Telnet? */
        (sim_con_ldsc.serport == 0)) {                      /* and not serial? */
        if (c && sim_con_ldsc.rxbps)                        /* got something && rate limiting? */
            sim_con_ldsc.rxnexttime =                       /* compute next input time */
                floor (sim_gtime () + ((sim_con_ldsc.rxdeltausecs * sim_timer_inst_per_sec ()) / USECS_PER_SECOND));
        if (c)
            sim_debug (DBG_RCV, &sim_con_telnet, "sim_poll_kbd() returning: '%c' (0x%02X)\n", sim_isprint (c & 0xFF) ? c & 0xFF : '.', c);
        return c;                                           /* in-window */
        }
    if (!sim_con_ldsc.conn) {                               /* no telnet or serial connection? */
        if (!sim_con_ldsc.txbfd)                            /* unbuffered? */
            return SCPE_LOST;                               /* connection lost */
        if (tmxr_poll_conn (&sim_con_tmxr) >= 0)            /* poll connect */
            sim_con_ldsc.rcve = 1;                          /* rcv enabled */
        else                                                /* fall through to poll reception */
            return SCPE_OK;                                 /* unconnected and buffered - nothing to receive */
        }
    }
tmxr_poll_rx (&sim_con_tmxr);                               /* poll for input */
if ((c = (t_stat)tmxr_getc_ln (&sim_con_ldsc)))             /* any char? */ 
    return (c & (SCPE_BREAK | 0377)) | SCPE_KFLAG;
return SCPE_OK;
}

/* Output character */

t_stat sim_putchar (int32 c)
{
sim_exp_check (&sim_con_expect, c);
if ((sim_con_tmxr.master == 0) &&                       /* not Telnet? */
    (sim_con_ldsc.serport == 0)) {                      /* and not serial port */
    ++sim_con_pos;                                      /* bookkeeping */
    if (sim_log)                                        /* log file? */
        fputc (c, sim_log);
    sim_debug (DBG_XMT, &sim_con_telnet, "sim_putchar('%c' (0x%02X)\n", sim_isprint (c) ? c : '.', c);
    return sim_os_putchar (c);                          /* in-window version */
    }
if (!sim_con_ldsc.conn) {                               /* no Telnet or serial connection? */
    if (!sim_con_ldsc.txbfd)                            /* unbuffered? */
        return SCPE_LOST;                               /* connection lost */
    if (tmxr_poll_conn (&sim_con_tmxr) >= 0)            /* poll connect */
        sim_con_ldsc.rcve = 1;                          /* rcv enabled */
    }
tmxr_putc_ln (&sim_con_ldsc, c);                        /* output char */
++sim_con_pos;                                          /* bookkeeping */
tmxr_poll_tx (&sim_con_tmxr);                           /* poll xmt */
return SCPE_OK;
}

t_stat sim_putchar_s (int32 c)
{
t_stat r;

sim_exp_check (&sim_con_expect, c);
if ((sim_con_tmxr.master == 0) &&                       /* not Telnet? */
    (sim_con_ldsc.serport == 0)) {                      /* and not serial port */
    ++sim_con_pos;                                      /* bookkeeping */
    if (sim_log)                                        /* log file? */
        fputc (c, sim_log);
    sim_debug (DBG_XMT, &sim_con_telnet, "sim_putchar('%c' (0x%02X)\n", sim_isprint (c) ? c : '.', c);
    return sim_os_putchar (c);                          /* in-window version */
    }
if (!sim_con_ldsc.conn) {                               /* no Telnet or serial connection? */
    if (!sim_con_ldsc.txbfd)                            /* non-buffered Telnet connection? */
        return SCPE_LOST;                               /* lost */
    if (tmxr_poll_conn (&sim_con_tmxr) >= 0)            /* poll connect */
        sim_con_ldsc.rcve = 1;                          /* rcv enabled */
    }
r = tmxr_putc_ln (&sim_con_ldsc, c);                    /* Telnet output */
if (r == SCPE_OK)
    ++sim_con_pos;                                      /* bookkeeping */
tmxr_poll_tx (&sim_con_tmxr);                           /* poll xmt */
return r;                                               /* return status */
}

/* Input character processing */

int32 sim_tt_inpcvt (int32 c, uint32 mode)
{
uint32 md = mode & TTUF_M_MODE;

if (md != TTUF_MODE_8B) {
    uint32 par_mode = (mode >> TTUF_W_MODE) & TTUF_M_PAR;
    static int32 nibble_even_parity = 0x699600;     /* bit array indicating the even parity for each index (offset by 8) */

    c = c & 0177;
    if (md == TTUF_MODE_UC) {
        if (islower (c))
            c = toupper (c);
        if (mode & TTUF_KSR)
            c = c | 0200;                           /* Force MARK parity */
        }
    switch (par_mode) {
        case TTUF_PAR_EVEN:
            c |= (((nibble_even_parity >> ((c & 0xF) + 1)) ^ (nibble_even_parity >> (((c >> 4) & 0xF) + 1))) & 0x80);
            break;
        case TTUF_PAR_ODD:
            c |= ((~((nibble_even_parity >> ((c & 0xF) + 1)) ^ (nibble_even_parity >> (((c >> 4) & 0xF) + 1)))) & 0x80);
            break;
        case TTUF_PAR_MARK:
            c = c | 0x80;
            break;
        }
    }
else
    c = c & 0377;
return c;
}

/* Output character processing */

int32 sim_tt_outcvt (int32 c, uint32 mode)
{
uint32 md = mode & TTUF_M_MODE;

if (md != TTUF_MODE_8B) {
    c = c & 0177;
    if (md == TTUF_MODE_UC) {
        if (islower (c))
            c = toupper (c);
        if ((mode & TTUF_KSR) && (c >= 0140))
            return -1;
        }
    if (((md == TTUF_MODE_UC) || (md == TTUF_MODE_7P)) &&
        ((c == 0177) ||
         ((c < 040) && !((sim_tt_pchar >> c) & 1))))
        return -1;
    }
else c = c & 0377;
return c;
}

/* Tab stop array handling

   *desc points to a uint8 array of length val

   Columns with tabs set are non-zero; columns without tabs are 0 */

t_stat sim_tt_settabs (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint8 *temptabs, *tabs = (uint8 *) desc;
int32 i, d;
t_stat r;
char gbuf[CBUFSIZE];

if ((cptr == NULL) || (tabs == NULL) || (val <= 1))
    return SCPE_IERR;
if (*cptr == 0)
    return SCPE_2FARG;
if ((temptabs = (uint8 *)malloc (val)) == NULL)
    return SCPE_MEM;
for (i = 0; i < val; i++)
    temptabs[i] = 0;
do {
    cptr = get_glyph (cptr, gbuf, ';');
    d = (int32)get_uint (gbuf, 10, val, &r);
    if ((r != SCPE_OK) || (d == 0)) {
        free (temptabs);
        return SCPE_ARG;
        }
    temptabs[d - 1] = 1;
    } while (*cptr != 0);
for (i = 0; i < val; i++)
    tabs[i] = temptabs[i];
free (temptabs);
return SCPE_OK;
}

t_stat sim_tt_showtabs (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
const uint8 *tabs = (const uint8 *) desc;
int32 i, any;

if ((st == NULL) || (val == 0) || (desc == NULL))
    return SCPE_IERR;
for (i = any = 0; i < val; i++) {
    if (tabs[i] != 0) {
        fprintf (st, (any? ";%d": "%d"), i + 1);
        any = 1;
        }
    }
fprintf (st, (any? "\n": "no tabs set\n"));
return SCPE_OK;
}

t_stat sim_tt_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 par_mode = (TT_GET_MODE (uptr->flags) >> TTUF_W_MODE) & TTUF_M_PAR;

uptr->flags = uptr->flags & ~((TTUF_M_MODE << TTUF_V_MODE) | (TTUF_M_PAR << TTUF_V_PAR) | TTUF_KSR);
uptr->flags |= val;
if (val != TT_MODE_8B)
    uptr->flags |= (par_mode << TTUF_V_PAR);
return SCPE_OK;
}

t_stat sim_tt_set_parity (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uptr->flags = uptr->flags & ~(TTUF_M_MODE | TTUF_M_PAR);
uptr->flags |= TT_MODE_7B | val;
return SCPE_OK;
}

t_stat sim_tt_show_modepar (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
uint32 md = (TT_GET_MODE (uptr->flags) & TTUF_M_MODE);
static const char *modes[] = {"7b", "8b", "UC", "7p"};
uint32 par_mode = (TT_GET_MODE (uptr->flags) >> TTUF_W_MODE) & TTUF_M_PAR;
static const char *parity[] = {"SPACE", "MARK", "EVEN", "ODD"};

if ((md == TTUF_MODE_UC) && (par_mode == TTUF_PAR_MARK))
    fprintf (st, "KSR (UC, MARK parity)");
else
    fprintf (st, "%s", modes[md]);
if ((md != TTUF_MODE_8B) && 
    ((md != TTUF_MODE_UC) || (par_mode != TTUF_PAR_MARK))) {
    if (par_mode != 0)
        fprintf (st, ", %s parity", parity[par_mode]);
    }
return SCPE_OK;
}


#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
extern pthread_mutex_t     sim_tmxr_poll_lock;
extern pthread_cond_t      sim_tmxr_poll_cond;
extern int32               sim_tmxr_poll_count;
extern t_bool              sim_tmxr_poll_running;

pthread_t           sim_console_poll_thread;       /* Keyboard Polling Thread Id */
t_bool              sim_console_poll_running = FALSE;
pthread_cond_t      sim_console_startup_cond;

static void *
_console_poll(void *arg)
{
int wait_count = 0;
DEVICE *d;

/* Boost Priority for this I/O thread vs the CPU instruction execution 
   thread which, in general, won't be readily yielding the processor when 
   this thread needs to run */
sim_os_set_thread_priority (PRIORITY_ABOVE_NORMAL);

sim_debug (DBG_ASY, &sim_con_telnet, "_console_poll() - starting\n");

pthread_mutex_lock (&sim_tmxr_poll_lock);
pthread_cond_signal (&sim_console_startup_cond);   /* Signal we're ready to go */
while (sim_asynch_enabled) {

    if (!sim_is_running) {
        if (wait_count) {
            sim_debug (DBG_ASY, d, "_console_poll() - Removing interest in %s. Other interest: %d\n", d->name, sim_con_ldsc.uptr->a_poll_waiter_count);
            --sim_con_ldsc.uptr->a_poll_waiter_count;
            --sim_tmxr_poll_count;
            }
        break;
        }

    /* If we started something, let it finish before polling again */
    if (wait_count) {
        sim_debug (DBG_ASY, &sim_con_telnet, "_console_poll() - waiting for %d units\n", wait_count);
        pthread_cond_wait (&sim_tmxr_poll_cond, &sim_tmxr_poll_lock);
        sim_debug (DBG_ASY, &sim_con_telnet, "_console_poll() - continuing with after wait\n");
        }

    pthread_mutex_unlock (&sim_tmxr_poll_lock);
    wait_count = 0;
    if (sim_os_poll_kbd_ready (1000)) {
        sim_debug (DBG_ASY, &sim_con_telnet, "_console_poll() - Keyboard Data available\n");
        pthread_mutex_lock (&sim_tmxr_poll_lock);
        ++wait_count;
        if (!sim_con_ldsc.uptr->a_polling_now) {
            sim_con_ldsc.uptr->a_polling_now = TRUE;
            sim_con_ldsc.uptr->a_poll_waiter_count = 1;
            d = find_dev_from_unit(sim_con_ldsc.uptr);
            sim_debug (DBG_ASY, &sim_con_telnet, "_console_poll() - Activating %s\n", d->name);
            pthread_mutex_unlock (&sim_tmxr_poll_lock);
            _sim_activate (sim_con_ldsc.uptr, 0);
            pthread_mutex_lock (&sim_tmxr_poll_lock);
            }
        else {
            d = find_dev_from_unit(sim_con_ldsc.uptr);
            sim_debug (DBG_ASY, &sim_con_telnet, "_console_poll() - Already Activated %s %d times\n", d->name, sim_con_ldsc.uptr->a_poll_waiter_count);
            ++sim_con_ldsc.uptr->a_poll_waiter_count;
            }
        }
    else
        pthread_mutex_lock (&sim_tmxr_poll_lock);

    sim_tmxr_poll_count += wait_count;
    }
pthread_mutex_unlock (&sim_tmxr_poll_lock);

sim_debug (DBG_ASY, &sim_con_telnet, "_console_poll() - exiting\n");

return NULL;
}


#endif /* defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX) */


t_stat sim_ttinit (void)
{
sim_con_tmxr.ldsc->mp = &sim_con_tmxr;
sim_register_internal_device (&sim_con_telnet);
tmxr_startup ();
return sim_os_ttinit ();
}

t_stat sim_ttrun (void)
{
if (!sim_con_tmxr.ldsc->uptr) {                         /* If simulator didn't declare its input polling unit */
    sim_con_unit.dynflags &= ~UNIT_TM_POLL;             /* we can't poll asynchronously */
    sim_con_unit.dynflags |= TMUF_NOASYNCH;             /* disable asynchronous behavior */
    }
else {
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
    if (sim_asynch_enabled) {
        sim_con_tmxr.ldsc->uptr->dynflags |= UNIT_TM_POLL;/* flag console input device as a polling unit */
        sim_con_unit.dynflags |= UNIT_TM_POLL;         /* flag as polling unit */
        }
#endif
    }
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
pthread_mutex_lock (&sim_tmxr_poll_lock);
if (sim_asynch_enabled) {
    pthread_attr_t attr;

    pthread_cond_init (&sim_console_startup_cond, NULL);
    pthread_attr_init (&attr);
    pthread_attr_setscope (&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_create (&sim_console_poll_thread, &attr, _console_poll, NULL);
    pthread_attr_destroy( &attr);
    pthread_cond_wait (&sim_console_startup_cond, &sim_tmxr_poll_lock); /* Wait for thread to stabilize */
    pthread_cond_destroy (&sim_console_startup_cond);
    sim_console_poll_running = TRUE;
    }
pthread_mutex_unlock (&sim_tmxr_poll_lock);
#endif
tmxr_start_poll ();
return sim_os_ttrun ();
}

t_stat sim_ttcmd (void)
{
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
pthread_mutex_lock (&sim_tmxr_poll_lock);
if (sim_console_poll_running) {
    pthread_cond_signal (&sim_tmxr_poll_cond);
    pthread_mutex_unlock (&sim_tmxr_poll_lock);
    pthread_join (sim_console_poll_thread, NULL);
    sim_console_poll_running = FALSE;
    }
else
    pthread_mutex_unlock (&sim_tmxr_poll_lock);
#endif
tmxr_stop_poll ();
return sim_os_ttcmd ();
}

t_stat sim_ttclose (void)
{
t_stat r1 = tmxr_shutdown ();
t_stat r2 = sim_os_ttclose ();

if (r1 != SCPE_OK)
    return r1;
return r2;
}

t_bool sim_ttisatty (void)
{
static int answer = -1;

if (answer == -1)
    answer = sim_os_fd_isatty (0);
return (t_bool)answer;
}

t_bool sim_fd_isatty (int fd)
{
return sim_os_fd_isatty (fd);
}

/* Platform specific routine definitions */

/* VMS routines, from Ben Thomas, with fixes from Robert Alan Byer */

#if defined (VMS)

#if defined(__VAX)
#define sys$assign SYS$ASSIGN
#define sys$qiow SYS$QIOW
#define sys$dassgn SYS$DASSGN
#endif

#include <descrip.h>
#include <ttdef.h>
#include <tt2def.h>
#include <iodef.h>
#include <ssdef.h>
#include <starlet.h>
#include <unistd.h>

#define EFN 0
uint32 tty_chan = 0;
int buffered_character = 0;

typedef struct {
    unsigned short sense_count;
    unsigned char sense_first_char;
    unsigned char sense_reserved;
    unsigned int stat;
    unsigned int stat2; } SENSE_BUF;

typedef struct {
    unsigned short status;
    unsigned short count;
    unsigned int dev_status; } IOSB;

SENSE_BUF cmd_mode = { 0 };
SENSE_BUF run_mode = { 0 };

static t_stat sim_os_ttinit (void)
{
unsigned int status;
IOSB iosb;
$DESCRIPTOR (terminal_device, "tt");

sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_ttinit()\n");

status = sys$assign (&terminal_device, &tty_chan, 0, 0);
if (status != SS$_NORMAL)
    return SCPE_TTIERR;
status = sys$qiow (EFN, tty_chan, IO$_SENSEMODE, &iosb, 0, 0,
    &cmd_mode, sizeof (cmd_mode), 0, 0, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL))
    return SCPE_TTIERR;
run_mode = cmd_mode;
run_mode.stat = cmd_mode.stat | TT$M_NOECHO & ~(TT$M_HOSTSYNC | TT$M_TTSYNC);
run_mode.stat2 = cmd_mode.stat2 | TT2$M_PASTHRU;
return SCPE_OK;
}

static t_stat sim_os_ttrun (void)
{
unsigned int status;
IOSB iosb;

sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_ttrun()\n");

status = sys$qiow (EFN, tty_chan, IO$_SETMODE, &iosb, 0, 0,
    &run_mode, sizeof (run_mode), 0, 0, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL))
    return SCPE_TTIERR;
return SCPE_OK;
}

static t_stat sim_os_ttcmd (void)
{
unsigned int status;
IOSB iosb;

sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_ttcmd() - BSDTTY\n");

status = sys$qiow (EFN, tty_chan, IO$_SETMODE, &iosb, 0, 0,
    &cmd_mode, sizeof (cmd_mode), 0, 0, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL))
    return SCPE_TTIERR;
return SCPE_OK;
}

static t_stat sim_os_ttclose (void)
{
sim_ttcmd ();
sys$dassgn (tty_chan);
return SCPE_OK;
}

static t_bool sim_os_fd_isatty (int fd)
{
return isatty (fd);
}

static t_stat sim_os_poll_kbd_data (void)
{
unsigned int status, term[2];
unsigned char buf[4];
IOSB iosb;
SENSE_BUF sense;

term[0] = 0; term[1] = 0;
status = sys$qiow (EFN, tty_chan, IO$_SENSEMODE | IO$M_TYPEAHDCNT, &iosb,
    0, 0, &sense, 8, 0, term, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL))
    return SCPE_TTIERR;
if (sense.sense_count == 0) return SCPE_OK;
term[0] = 0; term[1] = 0;
status = sys$qiow (EFN, tty_chan,
    IO$_READLBLK | IO$M_NOECHO | IO$M_NOFILTR | IO$M_TIMED | IO$M_TRMNOECHO,
    &iosb, 0, 0, buf, 1, 0, term, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL))
    return SCPE_OK;
if (buf[0] == sim_int_char)
    return SCPE_STOP;
if (sim_brk_char && (buf[0] == sim_brk_char))
    return SCPE_BREAK;
return (buf[0] | SCPE_KFLAG);
}

static t_stat sim_os_poll_kbd (void)
{
t_stat response;

sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_poll_kbd()\n");

if (response = buffered_character) {
    buffered_character = 0;
    return response;
    }
return sim_os_poll_kbd_data ();
}

static t_bool sim_os_poll_kbd_ready (int ms_timeout)
{
unsigned int status, term[2];
unsigned char buf[4];
IOSB iosb;

term[0] = 0; term[1] = 0;
status = sys$qiow (EFN, tty_chan,
    IO$_READLBLK | IO$M_NOECHO | IO$M_NOFILTR | IO$M_TIMED | IO$M_TRMNOECHO,
    &iosb, 0, 0, buf, 1, (ms_timeout+999)/1000, term, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL))
    return FALSE;
if (buf[0] == sim_int_char)
    buffered_character = SCPE_STOP;
else
    if (sim_brk_char && (buf[0] == sim_brk_char))
        buffered_character = SCPE_BREAK;
    else
        buffered_character = (buf[0] | SCPE_KFLAG);
return TRUE;
}


static t_stat sim_os_putchar (int32 out)
{
unsigned int status;
char c;
IOSB iosb;

c = out;
status = sys$qiow (EFN, tty_chan, IO$_WRITELBLK | IO$M_NOFORMAT,
    &iosb, 0, 0, &c, 1, 0, 0, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL))
    return SCPE_TTOERR;
return SCPE_OK;
}

/* Win32 routines */

#elif defined (_WIN32)

#include <fcntl.h>
#include <io.h>
#define RAW_MODE 0
static HANDLE std_input;
static HANDLE std_output;
static HANDLE std_error;
static DWORD saved_input_mode;
static DWORD saved_output_mode;
static DWORD saved_error_mode;
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

/* Note: This routine catches all the potential events which some aspect 
         of the windows system can generate.  The CTRL_C_EVENT won't be 
         generated by a  user typing in a console session since that 
         session is in RAW mode.  In general, Ctrl-C on a simulator's
         console terminal is a useful character to be passed to the 
         simulator.  This code does nothing to disable or affect that. */

#include <signal.h>

static BOOL WINAPI
ControlHandler(DWORD dwCtrlType)
    {
    DWORD Mode;
    extern void int_handler (int sig);

    switch (dwCtrlType)
        {
        case CTRL_BREAK_EVENT:      // Use CTRL-Break or CTRL-C to simulate 
        case CTRL_C_EVENT:          // SERVICE_CONTROL_STOP in debug mode
            int_handler(SIGINT);
            return TRUE;
        case CTRL_CLOSE_EVENT:      // Window is Closing
        case CTRL_LOGOFF_EVENT:     // User is logging off
            if (!GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &Mode))
                return TRUE;        // Not our User, so ignore
            /* fall through */
        case CTRL_SHUTDOWN_EVENT:   // System is shutting down
            int_handler(SIGTERM);
            return TRUE;
        }
    return FALSE;
    }

static t_stat sim_os_ttinit (void)
{
sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_ttinit()\n");

SetConsoleCtrlHandler( ControlHandler, TRUE );
std_input = GetStdHandle (STD_INPUT_HANDLE);
std_output = GetStdHandle (STD_OUTPUT_HANDLE);
std_error = GetStdHandle (STD_ERROR_HANDLE);
if ((std_input) &&                                      /* Not Background process? */
    (std_input != INVALID_HANDLE_VALUE))
    GetConsoleMode (std_input, &saved_input_mode);      /* Save Input Mode */
if ((std_output) &&                                     /* Not Background process? */
    (std_output != INVALID_HANDLE_VALUE))
    GetConsoleMode (std_output, &saved_output_mode);    /* Save Output Mode */
if ((std_error) &&                                      /* Not Background process? */
    (std_error != INVALID_HANDLE_VALUE))
    GetConsoleMode (std_error, &saved_error_mode);      /* Save Output Mode */
return SCPE_OK;
}

static t_stat sim_os_ttrun (void)
{
sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_ttrun()\n");

if ((sim_ttisatty ()) &&
    (std_input) &&                                      /* If Not Background process? */
    (std_input != INVALID_HANDLE_VALUE)) {
    if (!GetConsoleMode(std_input, &saved_input_mode))
        return sim_messagef (SCPE_TTYERR, "GetConsoleMode() error: 0x%X\n", (unsigned int)GetLastError ());
    if ((!SetConsoleMode(std_input, ENABLE_VIRTUAL_TERMINAL_INPUT)) &&
        (!SetConsoleMode(std_input, RAW_MODE)))
        return sim_messagef (SCPE_TTYERR, "SetConsoleMode() error: 0x%X\n", (unsigned int)GetLastError ());
    }
if ((std_output) &&                                     /* If Not Background process? */
    (std_output != INVALID_HANDLE_VALUE)) {
    if (GetConsoleMode(std_output, &saved_output_mode))
        if (!SetConsoleMode(std_output, ENABLE_VIRTUAL_TERMINAL_PROCESSING|ENABLE_PROCESSED_OUTPUT|ENABLE_WRAP_AT_EOL_OUTPUT))
            SetConsoleMode(std_output, ENABLE_PROCESSED_OUTPUT|ENABLE_WRAP_AT_EOL_OUTPUT);
    }
if (sim_log) {
    fflush (sim_log);
    _setmode (_fileno (sim_log), _O_BINARY);
    }
sim_os_set_thread_priority (PRIORITY_BELOW_NORMAL);
return SCPE_OK;
}

static t_stat sim_os_ttcmd (void)
{
sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_ttcmd() - BSDTTY\n");

if (sim_log) {
    fflush (sim_log);
    _setmode (_fileno (sim_log), _O_TEXT);
    }
sim_os_set_thread_priority (PRIORITY_NORMAL);
if ((sim_ttisatty ()) &&
    (std_input) &&                                      /* If Not Background process? */
    (std_input != INVALID_HANDLE_VALUE) &&
    (!SetConsoleMode(std_input, saved_input_mode)))     /* Restore Normal mode */
    return SCPE_TTYERR;
if ((std_output) &&                                     /* If Not Background process? */
    (std_output != INVALID_HANDLE_VALUE) &&
    (!SetConsoleMode(std_output, saved_output_mode)))   /* Restore Normal mode */
    return SCPE_TTYERR;
return SCPE_OK;
}

static t_stat sim_os_ttclose (void)
{
return SCPE_OK;
}

static t_bool sim_os_fd_isatty (int fd)
{
DWORD Mode;
HANDLE handle;

switch (fd) {
    case 0:
        handle = std_input;
        break;
    case 1:
        handle = std_output;
        break;
    case 2:
        handle = std_error;
        break;
    default:
        handle = NULL;
    }

return (handle) && (handle != INVALID_HANDLE_VALUE) && GetConsoleMode (handle, &Mode);
}

static t_stat sim_os_poll_kbd (void)
{
int c = -1;
DWORD nkbevents, nkbevent;
INPUT_RECORD rec;

sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_poll_kbd()\n");

if ((std_input == NULL) ||                              /* No keyboard for */
    (std_input == INVALID_HANDLE_VALUE))                /* background processes */
    return SCPE_OK;
if (!GetNumberOfConsoleInputEvents(std_input, &nkbevents))
    return SCPE_TTYERR;
while (c == -1) {
    if (0 == nkbevents)
        return SCPE_OK;
    if (!ReadConsoleInput(std_input, &rec, 1, &nkbevent))
        return SCPE_TTYERR;
    if (0 == nkbevent)
        return SCPE_OK;
    --nkbevents;
    if (rec.EventType == KEY_EVENT) {
        if (rec.Event.KeyEvent.bKeyDown) {
            if (0 == rec.Event.KeyEvent.uChar.UnicodeChar) {     /* Special Character/Keys? */
                if (rec.Event.KeyEvent.wVirtualKeyCode == VK_PAUSE) /* Pause/Break Key */
                    c = sim_brk_char | SCPE_BREAK;
                else
                    if (rec.Event.KeyEvent.wVirtualKeyCode == '2')  /* ^@ */
                        c = 0;                                      /* return NUL */
            } else
                c = rec.Event.KeyEvent.uChar.AsciiChar;
            }
      }
    }
if ((c & 0177) == sim_del_char)
    c = 0177;
if ((c & 0177) == sim_int_char)
    return SCPE_STOP;
if ((sim_brk_char && ((c & 0177) == sim_brk_char)) || (c & SCPE_BREAK))
    return SCPE_BREAK;
return c | SCPE_KFLAG;
}

static t_bool sim_os_poll_kbd_ready (int ms_timeout)
{
sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_poll_kbd_ready()\n");
if ((std_input == NULL) ||                              /* No keyboard for */
    (std_input == INVALID_HANDLE_VALUE)) {              /* background processes */
    Sleep (ms_timeout);
    return FALSE;
    }
return (WAIT_OBJECT_0 == WaitForSingleObject (std_input, ms_timeout));
}


#define BELL_CHAR           7       /* Bell Character */
#define BELL_INTERVAL_MS    500     /* No more than 2 Bell Characters Per Second */
#define ESC_CHAR            033     /* Escape Character */
#define CSI_CHAR            0233    /* Control Sequence Introducer */
#define NUL_CHAR            0000    /* NUL character */
#define ESC_HOLD_USEC_DELAY 8000    /* Escape hold interval */
#define ESC_HOLD_MAX        32      /* Maximum Escape hold buffer */

static uint8 out_buf[ESC_HOLD_MAX]; /* Buffered characters pending output */
static int32 out_ptr = 0;

static t_stat sim_out_hold_svc (UNIT *uptr)
{
DWORD unused;

WriteConsoleA(std_output, out_buf, out_ptr, &unused, NULL);
out_ptr = 0;
return SCPE_OK;
}

#define out_hold_unit sim_con_units[1]

static t_stat sim_os_putchar (int32 c)
{
DWORD unused;
uint32 now;
static uint32 last_bell_time;

if (c != 0177) {
    switch (c) {
        case BELL_CHAR:
            now = sim_os_msec ();
            if ((now - last_bell_time) > BELL_INTERVAL_MS) {
                WriteConsoleA(std_output, &c, 1, &unused, NULL);
                last_bell_time = now;
                }
            break;
        case NUL_CHAR:
            break;
        case CSI_CHAR:
        case ESC_CHAR:
            if (out_ptr) {
                WriteConsoleA(std_output, out_buf, out_ptr, &unused, NULL);
                out_ptr = 0;
                sim_cancel (&out_hold_unit);
                }
            out_buf[out_ptr++] = (uint8)c;
            sim_activate_after (&out_hold_unit, ESC_HOLD_USEC_DELAY);
            out_hold_unit.action = &sim_out_hold_svc;
            break;
        default:
            if (out_ptr) {
                if (out_ptr >= ESC_HOLD_MAX) {              /* Stop buffering if full */
                    WriteConsoleA(std_output, out_buf, out_ptr, &unused, NULL);
                    out_ptr = 0;
                    WriteConsoleA(std_output, &c, 1, &unused, NULL);
                    }
                else
                    out_buf[out_ptr++] = (uint8)c;
                }
            else
                WriteConsoleA(std_output, &c, 1, &unused, NULL);
        }
    }
return SCPE_OK;
}

/* OS/2 routines, from Bruce Ray and Holger Veit */

#elif defined (__OS2__)

#include <conio.h>

static t_stat sim_os_ttinit (void)
{
return SCPE_OK;
}

static t_stat sim_os_ttrun (void)
{
return SCPE_OK;
}

static t_stat sim_os_ttcmd (void)
{
return SCPE_OK;
}

static t_stat sim_os_ttclose (void)
{
return SCPE_OK;
}

static t_bool sim_os_fd_isatty (int fd)
{
return 1;
}

static t_stat sim_os_poll_kbd (void)
{
int c;

sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_poll_kbd()\n");

#if defined (__EMX__)
switch (c = _read_kbd(0,0,0)) {                         /* EMX has _read_kbd */

    case -1:                                            /* no char*/
        return SCPE_OK;

    case 0:                                             /* char pending */
        c = _read_kbd(0,1,0);
        break;

    default:                                            /* got char */
        break;
        }
#else
if (!kbhit ())
    return SCPE_OK;
c = getch();
#endif
if ((c & 0177) == sim_del_char)
    c = 0177;
if ((c & 0177) == sim_int_char)
    return SCPE_STOP;
if (sim_brk_char && ((c & 0177) == sim_brk_char))
    return SCPE_BREAK;
return c | SCPE_KFLAG;
}

static t_bool sim_os_poll_kbd_ready (int ms_timeout)   /* Don't know how to do this on this platform */
{
sim_os_ms_sleep (MIN(20,ms_timeout));           /* Wait a little */
return TRUE;                                    /* force a poll */
}

static t_stat sim_os_putchar (int32 c)
{
if (c != 0177) {
#if defined (__EMX__)
    putchar (c);
#else
    putch (c);
#endif
    fflush (stdout);
    }
return SCPE_OK;
}

/* Metrowerks CodeWarrior Macintosh routines, from Louis Chretien and
   Peter Schorn */

#elif defined (__MWERKS__) && defined (macintosh)

#include <console.h>
#include <Mactypes.h>
#include <string.h>
#include <sioux.h>
#include <unistd.h>
#include <siouxglobals.h>
#include <Traps.h>
#include <LowMem.h>

/* function prototypes */

Boolean SIOUXIsAppWindow(WindowPtr window);
void SIOUXDoMenuChoice(long menuValue);
void SIOUXUpdateMenuItems(void);
void SIOUXUpdateScrollbar(void);
int ps_kbhit(void);
int ps_getch(void);

extern pSIOUXWin SIOUXTextWindow;
static CursHandle iBeamCursorH = NULL;                  /* contains the iBeamCursor */

static void updateCursor(void) {
    WindowPtr window;
    window = FrontWindow();
    if (SIOUXIsAppWindow(window)) {
        GrafPtr savePort;
        Point localMouse;
        GetPort(&savePort);
        SetPort(window);
#if TARGET_API_MAC_CARBON
        GetGlobalMouse(&localMouse);
#else
        localMouse = LMGetMouseLocation();
#endif
        GlobalToLocal(&localMouse);
        if (PtInRect(localMouse, &(*SIOUXTextWindow->edit)->viewRect) && iBeamCursorH) {
            SetCursor(*iBeamCursorH);
        }
        else {
            SetCursor(&qd.arrow);
        }
        TEIdle(SIOUXTextWindow->edit);
        SetPort(savePort);
    }
    else {
        SetCursor(&qd.arrow);
        TEIdle(SIOUXTextWindow->edit);
    }
    return;
}

int ps_kbhit(void) {
    EventRecord event;
    int c;
    updateCursor();
    SIOUXUpdateScrollbar();
    while (GetNextEvent(updateMask | osMask | mDownMask | mUpMask | activMask |
             highLevelEventMask | diskEvt, &event)) {
        SIOUXHandleOneEvent(&event);
    }
    if (SIOUXQuitting) {
        exit(1);
    }
    if (EventAvail(keyDownMask,&event)) {
        c = event.message&charCodeMask;
        if ((event.modifiers & cmdKey) && (c > 0x20)) {
            GetNextEvent(keyDownMask, &event);
            SIOUXHandleOneEvent(&event);
            if (SIOUXQuitting) {
                exit(1);
            }
            return false;
        }
        return true;
    }
    else {
        return false;
    }
}

int ps_getch(void) {
    int c;
    EventRecord event;
    fflush(stdout);
    updateCursor();
    while(!GetNextEvent(keyDownMask,&event)) {
        if (GetNextEvent(updateMask | osMask | mDownMask | mUpMask | activMask |
             highLevelEventMask | diskEvt, &event)) {
            SIOUXUpdateScrollbar();
            SIOUXHandleOneEvent(&event);
        }
    }
    if (SIOUXQuitting) {
        exit(1);
    }
    c = event.message&charCodeMask;
    if ((event.modifiers & cmdKey) && (c > 0x20)) {
        SIOUXUpdateMenuItems();
        SIOUXDoMenuChoice(MenuKey(c));
    }
    if (SIOUXQuitting) {
        exit(1);
    }
   return c;
}

/* Note that this only works if the call to sim_ttinit comes before any output to the console */

static t_stat sim_os_ttinit (void) 
{
    int i;

    sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_ttinit()\n");

    /* this blank will later be replaced by the number of characters */
    char title[50] = " ";
    unsigned char ptitle[50];
    SIOUXSettings.autocloseonquit       = TRUE;
    SIOUXSettings.asktosaveonclose = FALSE;
    SIOUXSettings.showstatusline = FALSE;
    SIOUXSettings.columns = 80;
    SIOUXSettings.rows = 40;
    SIOUXSettings.toppixel = 42;
    SIOUXSettings.leftpixel     = 6;
    iBeamCursorH = GetCursor(iBeamCursor);
    strlcat(title, sim_name, sizeof(title));
    strlcat(title, " Simulator", sizeof(title));
    title[0] = strlen(title) - 1;                       /* Pascal string done */
    for (i = 0; i <= title[0]; i++) {                   /* copy to unsigned char */
        ptitle[i] = title[i];
        }
    SIOUXSetTitle(ptitle);
    return SCPE_OK;
}

static t_stat sim_os_ttrun (void)
{
return SCPE_OK;
}

static t_stat sim_os_ttcmd (void)
{
return SCPE_OK;
}

static t_stat sim_os_ttclose (void)
{
return SCPE_OK;
}

static t_bool sim_os_fd_isatty (int fd)
{
return 1;
}

static t_stat sim_os_poll_kbd (void)
{
int c;

sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_poll_kbd()\n");

if (!ps_kbhit ())
    return SCPE_OK;
c = ps_getch();
if ((c & 0177) == sim_del_char)
    c = 0177;
if ((c & 0177) == sim_int_char)
    return SCPE_STOP;
if (sim_brk_char && ((c & 0177) == sim_brk_char))
    return SCPE_BREAK;
return c | SCPE_KFLAG;
}

static t_bool sim_os_poll_kbd_ready (int ms_timeout)   /* Don't know how to do this on this platform */
{
sim_os_ms_sleep (MIN(20,ms_timeout));           /* Wait a little */
return TRUE;                                    /* force a poll */
}

static t_stat sim_os_putchar (int32 c)
{
if (c != 0177) {
    putchar (c);
    fflush (stdout);
    }
return SCPE_OK;
}

/* BSD UNIX routines */

#elif defined (BSDTTY)

#include <sgtty.h>
#include <fcntl.h>
#include <unistd.h>

#if (!defined(O_NONBLOCK)) && defined(O_NDELAY)
#define O_NONBLOCK O_NDELAY
#else
#if !defined(O_NONBLOCK)
#define O_NONBLOCK FNDELAY
#endif
#endif

struct sgttyb cmdtty,runtty;                            /* V6/V7 stty data */
struct tchars cmdtchars,runtchars;                      /* V7 editing */
struct ltchars cmdltchars,runltchars;                   /* 4.2 BSD editing */
int cmdfl,runfl;                                        /* TTY flags */

static t_stat sim_os_ttinit (void)
{
sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_ttinit() - BSDTTY\n");

cmdfl = fcntl (fileno (stdin), F_GETFL, 0);             /* get old flags  and status */
runfl = cmdfl | O_NONBLOCK;
if (ioctl (0, TIOCGETP, &cmdtty) < 0)
    return SCPE_TTIERR;
if (ioctl (0, TIOCGETC, &cmdtchars) < 0)
    return SCPE_TTIERR;
if (ioctl (0, TIOCGLTC, &cmdltchars) < 0)
    return SCPE_TTIERR;
runtty = cmdtty;                                        /* initial run state */
runtty.sg_flags = cmdtty.sg_flags & ~(ECHO|CRMOD) | CBREAK;
runtchars.t_intrc = sim_int_char;                       /* interrupt */
runtchars.t_quitc = 0xFF;                               /* no quit */
runtchars.t_startc = 0xFF;                              /* no host sync */
runtchars.t_stopc = 0xFF;
runtchars.t_eofc = 0xFF;
runtchars.t_brkc = 0xFF;
runltchars.t_suspc = 0xFF;                              /* no specials of any kind */
runltchars.t_dsuspc = 0xFF;
runltchars.t_rprntc = 0xFF;
runltchars.t_flushc = 0xFF;
runltchars.t_werasc = 0xFF;
runltchars.t_lnextc = 0xFF;
return SCPE_OK;                                         /* return success */
}

static t_stat sim_os_ttrun (void)
{
sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_ttrun() - BSDTTY\n");

#if (defined(__GNUC__) && !defined(__OPTIMIZE__))       /* Debug build? */
if (sim_dbg_int_char == 0)
    sim_dbg_int_char = sim_int_char + 1;
runtchars.t_intrc = sim_dbg_int_char;                   /* let debugger get SIGINT with next highest char */
if (!sigint_message_issued) {
    char sigint_name[8];

    if (isprint(sim_dbg_int_char&0xFF))
        sprintf(sigint_name, "'%c'", sim_dbg_int_char&0xFF);
    else
        if (sim_dbg_int_char <= 26)
            sprintf(sigint_name, "^%c", '@' + (sim_dbg_int_char&0xFF));
        else
            sprintf(sigint_name, "'\\%03o'", sim_dbg_int_char&0xFF);
    sigint_message_issued = TRUE;
    sim_messagef (SCPE_OK, "SIGINT will be delivered to your debugger when the %s character is entered\n", sigint_name);
    }
#else
runtchars.t_intrc = sim_int_char;                       /* in case changed */
#endif
fcntl (0, F_SETFL, runfl);                              /* non-block mode */
if (ioctl (0, TIOCSETP, &runtty) < 0)
    return SCPE_TTIERR;
if (ioctl (0, TIOCSETC, &runtchars) < 0)
    return SCPE_TTIERR;
if (ioctl (0, TIOCSLTC, &runltchars) < 0)
    return SCPE_TTIERR;
sim_os_set_thread_priority (PRIORITY_BELOW_NORMAL)l     /* lower priority */
return SCPE_OK;
}

static t_stat sim_os_ttcmd (void)
{
sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_ttcmd() - BSDTTY\n");

sim_os_set_thread_priority (PRIORITY_NORMAL);           /* restore priority */
fcntl (0, F_SETFL, cmdfl);                              /* block mode */
if (ioctl (0, TIOCSETP, &cmdtty) < 0)
    return SCPE_TTIERR;
if (ioctl (0, TIOCSETC, &cmdtchars) < 0)
    return SCPE_TTIERR;
if (ioctl (0, TIOCSLTC, &cmdltchars) < 0)
    return SCPE_TTIERR;
return SCPE_OK;
}

static t_stat sim_os_ttclose (void)
{
return sim_ttcmd ();
}

static t_bool sim_os_fd_isatty (int fd)
{
return isatty (fd);
}

static t_stat sim_os_poll_kbd (void)
{
int status;
unsigned char buf[1];

sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_poll_kbd() - BSDTTY\n");

status = read (0, buf, 1);
if (status != 1)
    return SCPE_OK;
if (sim_brk_char && (buf[0] == sim_brk_char))
    return SCPE_BREAK;
if (sim_int_char && (buf[0] == sim_int_char))
    return SCPE_STOP;
return (buf[0] | SCPE_KFLAG);
}

static t_bool sim_os_poll_kbd_ready (int ms_timeout)
{
fd_set readfds;
struct timeval timeout;

if (!isatty (0)) {                           /* skip if !tty */
    sim_os_ms_sleep (ms_timeout);
    return FALSE;
    }
FD_ZERO (&readfds);
FD_SET (0, &readfds);
timeout.tv_sec = (ms_timeout*1000)/1000000;
timeout.tv_usec = (ms_timeout*1000)%1000000;
return (1 == select (1, &readfds, NULL, NULL, &timeout));
}

static t_stat sim_os_putchar (int32 out)
{
char c;

c = out;
write (1, &c, 1);
return SCPE_OK;
}

/* POSIX UNIX routines, from Leendert Van Doorn */

#else

#if !defined (__ANDROID_API__) || (__ANDROID_API__ < 26)
#define TCSETATTR_ACTION TCSAFLUSH
#else
#define TCSETATTR_ACTION TCSANOW
#endif

#include <termios.h>
#include <unistd.h>

struct termios cmdtty, runtty;
int cmdfl,runfl;                                        /* TTY flags */

static t_stat sim_os_ttinit (void)
{
sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_ttinit()\n");

cmdfl = fcntl (fileno (stdin), F_GETFL, 0);             /* get old flags  and status */
/* 
 * make sure systems with broken termios (that don't honor
 * VMIN=0 and VTIME=0) actually implement non blocking reads.  
 * This will have no negative effect on other systems since 
 * this is turned on and off depending on whether simulation 
 * is running or not.
 */
runfl = cmdfl | O_NONBLOCK;
if (!isatty (fileno (stdin)))                           /* skip if !tty */
    return SCPE_OK;
if (tcgetattr (0, &cmdtty) < 0)                         /* get old flags */
    return SCPE_TTIERR;
runtty = cmdtty;
runtty.c_lflag = runtty.c_lflag & ~(ECHO | ICANON);     /* no echo or edit */
runtty.c_oflag = runtty.c_oflag & ~OPOST;               /* no output edit */
runtty.c_iflag = runtty.c_iflag & ~ICRNL;               /* no cr conversion */
#if defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)
runtty.c_cc[VINTR] = 0;                                 /* OS X doesn't deliver SIGINT to main thread when enabled */
#else
runtty.c_cc[VINTR] = sim_int_char;                      /* interrupt */
#endif
runtty.c_cc[VQUIT] = 0;                                 /* no quit */
runtty.c_cc[VERASE] = 0;
runtty.c_cc[VKILL] = 0;
runtty.c_cc[VEOF] = 0;
runtty.c_cc[VEOL] = 0;
runtty.c_cc[VSTART] = 0;                                /* no host sync */
runtty.c_cc[VSUSP] = 0;
runtty.c_cc[VSTOP] = 0;
#if defined (VREPRINT)
runtty.c_cc[VREPRINT] = 0;                              /* no specials */
#endif
#if defined (VDISCARD)
runtty.c_cc[VDISCARD] = 0;
#endif
#if defined (VWERASE)
runtty.c_cc[VWERASE] = 0;
#endif
#if defined (VLNEXT)
runtty.c_cc[VLNEXT] = 0;
#endif
runtty.c_cc[VMIN] = 0;                                  /* no waiting */
runtty.c_cc[VTIME] = 0;
#if defined (VDSUSP)
runtty.c_cc[VDSUSP] = 0;
#endif
#if defined (VSTATUS)
runtty.c_cc[VSTATUS] = 0;
#endif
return SCPE_OK;
}

static t_stat sim_os_ttrun (void)
{
sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_ttrun()\n");

if (!isatty (fileno (stdin)))                           /* skip if !tty */
    return SCPE_OK;
(void)fcntl (fileno (stdin), F_SETFL, runfl);           /* non-block mode */
#if defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)
runtty.c_cc[VINTR] = 0;                                 /* OS X doesn't deliver SIGINT to main thread when enabled */
#else
runtty.c_cc[VINTR] = sim_int_char;                      /* in case changed */
#endif
#if (defined(__GNUC__) && !defined(__OPTIMIZE__))       /* Debug build? */
if (sim_dbg_int_char == 0)
    sim_dbg_int_char = sim_int_char + 1;
runtty.c_cc[VINTR] = sim_dbg_int_char;                  /* let debugger get SIGINT with next highest char */
if (!sigint_message_issued) {
    char sigint_name[8];

    if (isprint(sim_dbg_int_char&0xFF))
        sprintf(sigint_name, "'%c'", sim_dbg_int_char&0xFF);
    else
        if (sim_dbg_int_char <= 26)
            sprintf(sigint_name, "^%c", '@' + (sim_dbg_int_char&0xFF));
        else
            sprintf(sigint_name, "'\\%03o'", sim_dbg_int_char&0xFF);
    sigint_message_issued = TRUE;
    sim_messagef (SCPE_OK, "SIGINT will be delivered to your debugger when the %s character is entered\n", sigint_name);
    }
#endif
if (tcsetattr (fileno(stdin), TCSETATTR_ACTION, &runtty) < 0)
    return SCPE_TTIERR;
sim_os_set_thread_priority (PRIORITY_BELOW_NORMAL);     /* try to lower pri */
return SCPE_OK;
}

static t_stat sim_os_ttcmd (void)
{
sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_ttcmd() - BSDTTY\n");

if (!isatty (fileno (stdin)))                           /* skip if !tty */
    return SCPE_OK;
sim_os_set_thread_priority (PRIORITY_NORMAL);           /* try to raise pri */
(void)fcntl (0, F_SETFL, cmdfl);                        /* block mode */
if (tcsetattr (fileno(stdin), TCSETATTR_ACTION, &cmdtty) < 0)
    return SCPE_TTIERR;
return SCPE_OK;
}

static t_stat sim_os_ttclose (void)
{
return sim_ttcmd ();
}

static t_bool sim_os_fd_isatty (int fd)
{
return isatty (fd);
}

static t_stat sim_os_poll_kbd (void)
{
int status;
unsigned char buf[1];

sim_debug (DBG_TRC, &sim_con_telnet, "sim_os_poll_kbd()\n");

status = read (0, buf, 1);
if (status != 1)
    return SCPE_OK;
if (sim_brk_char && (buf[0] == sim_brk_char))
    return SCPE_BREAK;
if (sim_int_char && (buf[0] == sim_int_char))
    return SCPE_STOP;
return (buf[0] | SCPE_KFLAG);
}

static t_bool sim_os_poll_kbd_ready (int ms_timeout)
{
fd_set readfds;
struct timeval timeout;

if (!sim_ttisatty()) {                      /* skip if !tty */
    sim_os_ms_sleep (ms_timeout);
    return FALSE;
    }
FD_ZERO (&readfds);
FD_SET (0, &readfds);
timeout.tv_sec = (ms_timeout*1000)/1000000;
timeout.tv_usec = (ms_timeout*1000)%1000000;
return (1 == select (1, &readfds, NULL, NULL, &timeout));
}

static t_stat sim_os_putchar (int32 out)
{
char c;

c = out;
if (write (1, &c, 1)) {};
return SCPE_OK;
}

#endif

/* Decode a string.

   A string containing encoded control characters is decoded into the equivalent
   character string.  Escape targets @, A-Z, and [\]^_ form control characters
   000-037.
*/
#define ESCAPE_CHAR '~'

static void decode (char *decoded, const char *encoded)
{
char c;

while ((c = *decoded++ = *encoded++))                   /* copy the character */
    if (c == ESCAPE_CHAR) {                             /* does it start an escape? */
        if ((isalpha (*encoded)) ||                     /* is next character "A-Z" or "a-z"? */
            (*encoded == '@') ||                        /*   or "@"? */
            ((*encoded >= '[') && (*encoded <= '_')))   /*   or "[\]^_"? */

            *(decoded - 1) = *encoded++ & 037;          /* convert back to control character */
        else {
            if ((*encoded == '\0') ||                   /* single escape character at EOL? */
                 (*encoded++ != ESCAPE_CHAR))           /*   or not followed by another escape? */
                decoded--;                              /* drop the encoding */
            }
        }
return;
}

/* Set console halt */

static t_stat sim_set_halt (int32 flag, CONST char *cptr)
{
if (flag == 0)                                              /* no halt? */
    sim_exp_clrall (&sim_con_expect);                       /* disable halt checks */
else {
    char *mbuf;
    char *mbuf2;

    if (cptr == NULL || *cptr == 0)                         /* no match string? */
        return SCPE_2FARG;                                  /* need an argument */

    sim_exp_clrall (&sim_con_expect);                       /* make sure that none currently exist */

    mbuf = (char *)malloc (1 + strlen (cptr));
    decode (mbuf, cptr);                                    /* save decoded match string */

    mbuf2 = (char *)malloc (3 + strlen(cptr));
    sprintf (mbuf2, "%s%s%s", (sim_switches & SWMASK ('A')) ? "\n" : "",
                              mbuf, 
                              (sim_switches & SWMASK ('I')) ? "" : "\n");
    free (mbuf);
    mbuf = sim_encode_quoted_string ((uint8 *)mbuf2, strlen (mbuf2));
    sim_switches = EXP_TYP_PERSIST;
    sim_set_expect (&sim_con_expect, mbuf);
    free (mbuf);
    free (mbuf2);
    }

return SCPE_OK;
}


/* Set console response */

static t_stat sim_set_response (int32 flag, CONST char *cptr)
{
if (flag == 0)                                          /* no response? */
    sim_send_clear (&sim_con_send);
else {
    uint8 *rbuf;

    if (cptr == NULL || *cptr == 0)
        return SCPE_2FARG;                              /* need arg */

    rbuf = (uint8 *)malloc (1 + strlen(cptr));

    decode ((char *)rbuf, cptr);                        /* decode string */
    sim_send_input (&sim_con_send, rbuf, strlen((char *)rbuf), 0, 0); /* queue it for output */
    free (rbuf);
    }

return SCPE_OK;
}

/* Set console delay */

static t_stat sim_set_delay (int32 flag, CONST char *cptr)
{
int32 val;
t_stat r;

if (cptr == NULL || *cptr == 0)                         /* no argument string? */
    return SCPE_2FARG;                                  /* need an argument */

val = (int32) get_uint (cptr, 10, INT_MAX, &r);         /* parse the argument */
if (r == SCPE_OK) {                                     /* parse OK? */
    char gbuf[CBUFSIZE];

    snprintf (gbuf, sizeof (gbuf), "HALTAFTER=%d", val);
    expect_cmd (1, gbuf);
    }

return r;
}
