/* sim_console.c: simulator console I/O library

   Copyright (c) 1993-2012, Robert M Supnik

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
   20-Jul-01    RMS     Added Macintosh support (Louis Chretien, Peter Schorn, Ben Supnik)
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

   sim_ttinit                   called once to get initial terminal state
   sim_ttrun                    called to put terminal into run state
   sim_ttcmd                    called to return terminal to command state
   sim_ttclose                  called once before the simulator exits
   sim_ttisatty                 called to determine if running interactively
   sim_os_poll_kbd              poll for keyboard input
   sim_os_putchar               output character to console

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

/* Forward Declaraations of Platform specific routines */

static t_stat sim_os_poll_kbd (void);
static t_bool sim_os_poll_kbd_ready (int ms_timeout);
static t_stat sim_os_putchar (int32 out);
static t_stat sim_os_ttinit (void);
static t_stat sim_os_ttrun (void);
static t_stat sim_os_ttcmd (void);
static t_stat sim_os_ttclose (void);
static t_bool sim_os_ttisatty (void);

static t_stat sim_set_rem_telnet (int32 flag, char *cptr);
static t_stat sim_set_rem_connections (int32 flag, char *cptr);
static t_stat sim_set_rem_timeout (int32 flag, char *cptr);

#define KMAP_WRU        0
#define KMAP_BRK        1
#define KMAP_DEL        2
#define KMAP_MASK       0377
#define KMAP_NZ         0400

int32 sim_int_char = 005;                               /* interrupt character */
int32 sim_brk_char = 000;                               /* break character */
int32 sim_tt_pchar = 0x00002780;
#if defined (_WIN32) || defined (__OS2__) || (defined (__MWERKS__) && defined (macintosh))
int32 sim_del_char = '\b';                              /* delete character */
#else
int32 sim_del_char = 0177;
#endif
static t_stat sim_con_poll_svc (UNIT *uptr);                       /* console connection poll routine */
static t_stat sim_con_reset (DEVICE *dptr);                        /* console reset routine */
UNIT sim_con_unit = { UDATA (&sim_con_poll_svc, 0, 0)  };   /* console connection unit */
/* debugging bitmaps */
#define DBG_TRC  TMXR_DBG_TRC                           /* trace routine calls */
#define DBG_XMT  TMXR_DBG_XMT                           /* display Transmitted Data */
#define DBG_RCV  TMXR_DBG_RCV                           /* display Received Data */
#define DBG_ASY  TMXR_DBG_ASY                           /* asynchronous thread activity */

static DEBTAB sim_con_debug[] = {
  {"TRC",    DBG_TRC},
  {"XMT",    DBG_XMT},
  {"RCV",    DBG_RCV},
  {"ASY",    DBG_ASY},
  {0}
};

static MTAB sim_con_mod[] = {
  { 0 },
};

DEVICE sim_con_telnet = {
    "CON-TEL", &sim_con_unit, NULL, sim_con_mod, 
    1, 0, 0, 0, 0, 0, 
    NULL, NULL, sim_con_reset, NULL, NULL, NULL, 
    NULL, DEV_DEBUG, 0, sim_con_debug};
TMLN sim_con_ldsc = { 0 };                                          /* console line descr */
TMXR sim_con_tmxr = { 1, 0, 0, &sim_con_ldsc, NULL, &sim_con_telnet };/* console line mux */

/* Unit service for console connection polling */

static t_stat sim_con_poll_svc (UNIT *uptr)
{
if ((sim_con_tmxr.master == 0) &&                       /* not Telnet and not serial? */
    (sim_con_ldsc.serport == 0))
    return SCPE_OK;                                     /* done */
if (tmxr_poll_conn (&sim_con_tmxr) >= 0)                /* poll connect */
    sim_con_ldsc.rcve = 1;                              /* rcv enabled */
sim_activate_after(uptr, 1000000);                      /* check again in 1 second */
if (sim_con_ldsc.conn)
    tmxr_send_buffered_data (&sim_con_ldsc);            /* try to flush any buffered data */
return SCPE_OK;
}

static t_stat sim_con_reset (DEVICE *dptr)
{
return sim_con_poll_svc (&dptr->units[0]);              /* establish polling as needed */
}


/* Set/show data structures */

static CTAB set_con_tab[] = {
    { "WRU", &sim_set_kmap, KMAP_WRU | KMAP_NZ },
    { "BRK", &sim_set_kmap, KMAP_BRK },
    { "DEL", &sim_set_kmap, KMAP_DEL |KMAP_NZ },
    { "PCHAR", &sim_set_pchar, 0 },
    { "TELNET", &sim_set_telnet, 0 },
    { "NOTELNET", &sim_set_notelnet, 0 },
    { "SERIAL", &sim_set_serial, 0 },
    { "NOSERIAL", &sim_set_noserial, 0 },
    { "LOG", &sim_set_logon, 0 },
    { "NOLOG", &sim_set_logoff, 0 },
    { "DEBUG", &sim_set_debon, 0 },
    { "NODEBUG", &sim_set_deboff, 0 },
    { NULL, NULL, 0 }
    };

static CTAB set_rem_con_tab[] = {
    { "CONNECTIONS", &sim_set_rem_connections, 0 },
    { "TELNET", &sim_set_rem_telnet, 1 },
    { "NOTELNET", &sim_set_rem_telnet, 0 },
    { "TIMEOUT", &sim_set_rem_timeout, 0 },
    { NULL, NULL, 0 }
    };

static SHTAB show_con_tab[] = {
    { "WRU", &sim_show_kmap, KMAP_WRU },
    { "BRK", &sim_show_kmap, KMAP_BRK },
    { "DEL", &sim_show_kmap, KMAP_DEL },
    { "PCHAR", &sim_show_pchar, 0 },
    { "LOG", &sim_show_cons_log, 0 },
    { "TELNET", &sim_show_telnet, 0 },
    { "DEBUG", &sim_show_cons_debug, 0 },
    { "BUFFERED", &sim_show_cons_buff, 0 },
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
    &sim_del_char
    };

/* Console I/O package.

   The console terminal can be attached to the controlling window
   or to a Telnet connection.  If attached to a Telnet connection,
   the console is described by internal terminal multiplexor
   sim_con_tmxr and internal terminal line description sim_con_ldsc.
*/

/* SET CONSOLE command */

t_stat sim_set_console (int32 flag, char *cptr)
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

t_stat sim_show_console (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
SHTAB *shptr;
int32 i;

if (*cptr == 0) {                                       /* show all */
    for (i = 0; show_con_tab[i].name; i++)
        show_con_tab[i].action (st, dptr, uptr, show_con_tab[i].arg, cptr);
    return SCPE_OK;
    }
while (*cptr != 0) {
    cptr = get_glyph (cptr, gbuf, ',');                 /* get modifier */
    if ((shptr = find_shtab (show_con_tab, gbuf)))
        shptr->action (st, dptr, uptr, shptr->arg, cptr);
    else return SCPE_NOPARAM;
    }
return SCPE_OK;
}

t_stat sim_rem_con_poll_svc (UNIT *uptr);                /* remote console connection poll routine */
t_stat sim_rem_con_data_svc (UNIT *uptr);                /* remote console connection data routine */
t_stat sim_rem_con_reset (DEVICE *dptr);                 /* remote console reset routine */
UNIT sim_rem_con_unit[2] = {
    { UDATA (&sim_rem_con_poll_svc, 0, 0)  },           /* remote console connection polling unit */
    { UDATA (&sim_rem_con_data_svc, 0, 0)  }};          /* console data handling unit */

DEBTAB sim_rem_con_debug[] = {
  {"TRC",    DBG_TRC},
  {"XMT",    DBG_XMT},
  {"RCV",    DBG_RCV},
  {0}
};

MTAB sim_rem_con_mod[] = {
  { 0 },
};

DEVICE sim_remote_console = {
    "REM-CON", sim_rem_con_unit, NULL, sim_rem_con_mod, 
    2, 0, 0, 0, 0, 0, 
    NULL, NULL, sim_rem_con_reset, NULL, NULL, NULL, 
    NULL, DEV_DEBUG, 0, sim_rem_con_debug};
#define MAX_REMOTE_SESSIONS 40          /* Arbitrary Session Limit */
static int32 *sim_rem_buf_size = NULL;
static int32 *sim_rem_buf_ptr = NULL;
static char **sim_rem_buf = NULL;
static t_bool *sim_rem_single_mode = NULL;  /* per line command mode (single command or must continue) */
static TMXR sim_rem_con_tmxr = { 0, 0, 0, NULL, NULL, &sim_remote_console };/* remote console line mux */
static uint32 sim_rem_read_timeout = 30;    /* seconds before automatic continue */
static int32 sim_rem_step_line = -1;        /* step in progress on line # */
static t_bool sim_log_temp = FALSE;         /* temporary log file active */
static char sim_rem_con_temp_name[PATH_MAX+1];

/* SET REMOTE CONSOLE command */

t_stat sim_set_remote_console (int32 flag, char *cptr)
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

t_stat sim_show_remote_console (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
int32 i, connections;
TMLN *lp;

if (*cptr != 0)
    return SCPE_NOPARAM;
if (sim_rem_con_tmxr.lines > 1)
    fprintf (st, "Remote Console Input Connections from %d sources are supported concurrently\n", sim_rem_con_tmxr.lines);
if (sim_rem_read_timeout)
    fprintf (st, "Remote Console Input automatically continues after %d seconds\n", sim_rem_read_timeout);
if (!sim_rem_con_tmxr.master)
    fprintf (st, "Remote Console Command input is disabled\n");
else
    fprintf (st, "Remote Console Command Input listening on TCP port: %s\n", sim_rem_con_unit[0].filename);
for (i=connections=0; i<sim_rem_con_tmxr.lines; i++) {
    lp = &sim_rem_con_tmxr.ldsc[i];
    if (!lp->conn)
        continue;
    ++connections;
    if (connections == 1)
        fprintf (st, "Remote Console Connections:\n");
    tmxr_fconns (st, lp, i);
    }
return SCPE_OK;
}

/* Unit service for remote console connection polling */

t_stat sim_rem_con_poll_svc (UNIT *uptr)
{
int32 c;

c = tmxr_poll_conn (&sim_rem_con_tmxr);
if (c >= 0) {                                           /* poll connect */
    TMLN *lp = &sim_rem_con_tmxr.ldsc[c];
    char wru_name[8];

    sim_activate_after(uptr+1, 1000000);                /* start data poll after 100ms */
    lp->rcve = 1;                                       /* rcv enabled */
    sim_rem_buf_ptr[c] = 0;                             /* start with empty command buffer */
    if (isprint(sim_int_char&0xFF))
        sprintf(wru_name, "'%c'", sim_int_char&0xFF);
    else
        if (sim_int_char <= 26)
            sprintf(wru_name, "^%c", '@' + (sim_int_char&0xFF));
        else
            sprintf(wru_name, "'\\%03o'", sim_int_char&0xFF);
    tmxr_linemsgf (lp, "%s Remote Console\r\n"
                       "Enter single commands or to enter multiple command mode enter the %s character\r\n"
                       "Simulator Running...", sim_name, wru_name);
    tmxr_send_buffered_data (lp);                       /* flush buffered data */
    }
sim_activate_after(uptr, 1000000);                      /* check again in 1 second */
if (sim_con_ldsc.conn)
    tmxr_send_buffered_data (&sim_con_ldsc);            /* try to flush any buffered data */
return SCPE_OK;
}

static t_stat x_continue_cmd (int32 flag, char *cptr)
{
return SCPE_IERR;           /* This routine should never be called */
}

static t_stat x_step_cmd (int32 flag, char *cptr)
{
return SCPE_IERR;           /* This routine should never be called */
}

static t_stat x_help_cmd (int32 flag, char *cptr);

#define EX_D            0                               /* deposit */
#define EX_E            1                               /* examine */
#define EX_I            2                               /* interactive */
static CTAB allowed_remote_cmds[] = {
    { "EXAMINE",  &exdep_cmd,      EX_E },
    { "IEXAMINE", &exdep_cmd, EX_E+EX_I },
    { "DEPOSIT",  &exdep_cmd,      EX_D },
    { "EVALUATE", &eval_cmd,          0 },
    { "ATTACH",   &attach_cmd,        0 },
    { "DETACH",   &detach_cmd,        0 },
    { "ASSIGN",   &assign_cmd,        0 },
    { "DEASSIGN", &deassign_cmd,      0 },
    { "CONTINUE", &x_continue_cmd,    0 },
    { "STEP",     &x_step_cmd,        0 },
    { "PWD",      &pwd_cmd,           0 },
    { "SAVE",     &save_cmd,          0 },
    { "DIR",      &dir_cmd,           0 },
    { "LS",       &dir_cmd,           0 },
    { "ECHO",     &echo_cmd,          0 },
    { "SET",      &set_cmd,           0 },
    { "SHOW",     &show_cmd,          0 },
    { "HELP",     &x_help_cmd,        0 },
    { NULL,       NULL }
    };

static CTAB allowed_single_remote_cmds[] = {
    { "ATTACH",   &attach_cmd,        0 },
    { "DETACH",   &detach_cmd,        0 },
    { "PWD",      &pwd_cmd,           0 },
    { "DIR",      &dir_cmd,           0 },
    { "LS",       &dir_cmd,           0 },
    { "ECHO",     &echo_cmd,          0 },
    { "SHOW",     &show_cmd,          0 },
    { "HELP",     &x_help_cmd,        0 },
    { NULL,       NULL }
    };

static t_stat x_help_cmd (int32 flag, char *cptr)
{
CTAB *cmdp, *cmdph;

if (*cptr)
    return help_cmd (flag, cptr);
printf ("Remote Console Commands:\r\n");
if (sim_log)
    fprintf (sim_log, "Remote Console Commands:\r\n");
for (cmdp=allowed_remote_cmds; cmdp->name != NULL; ++cmdp) {
    cmdph = find_cmd (cmdp->name);
    if (cmdph && cmdph->help) {
        printf ("%s", cmdph->help);
        if (sim_log)
            fprintf (sim_log, "%s", cmdph->help);
        }
    }
return SCPE_OK;
}

/* Unit service for remote console data polling */

t_stat sim_rem_con_data_svc (UNIT *uptr)
{
int32 i, j, c;
t_stat stat, stat_nomessage;
t_bool stepping = FALSE;
int32 steps = 1;
t_bool was_stepping = (sim_rem_step_line != -1);
t_bool got_command;
t_bool close_session = FALSE;
TMLN *lp;
char cbuf[4*CBUFSIZE], gbuf[CBUFSIZE], *cptr, *argv[1] = {NULL};
CTAB *cmdp;
uint32 read_start_time;
t_offset cmd_log_start;

tmxr_poll_rx (&sim_rem_con_tmxr);                      /* poll input */
for (i=(was_stepping ? sim_rem_step_line : 0); 
     (i < sim_rem_con_tmxr.lines) && (!stepping); 
     i++) {
    lp = &sim_rem_con_tmxr.ldsc[i];
    if (!lp->conn)
        continue;
    if (was_stepping) {
        sim_rem_step_line = -1;                     /* Done with step */
        stat = SCPE_STEP;
        cmdp = find_cmd ("STEP");
        stat_nomessage = stat & SCPE_NOMESSAGE;     /* extract possible message supression flag */
        stat = SCPE_BARE_STATUS(stat);              /* remove possible flag */
        if (!stat_nomessage) {                      /* displaying message status? */
            fflush (sim_log);
            cmd_log_start = sim_ftell (sim_log);
            if (cmdp && (cmdp->message))            /* special message handler? */
                cmdp->message (NULL, stat);         /* let it deal with display */
            else
                if (stat >= SCPE_BASE) {            /* error? */
                    printf ("%s\r\n", sim_error_text (stat));
                    if (sim_log)
                        fprintf (sim_log, "%s\n", sim_error_text (stat));
                    }
            fflush (sim_log);
            sim_fseeko (sim_log, cmd_log_start, SEEK_SET);
            cbuf[sizeof(cbuf)-1] = '\0';
            while (fgets (cbuf, sizeof(cbuf)-1, sim_log)) {
                tmxr_linemsgf (lp, "%s", cbuf);
                tmxr_send_buffered_data (lp);
                }
            }
        }
    else {
        c = tmxr_getc_ln (lp);
        if (!(TMXR_VALID & c))
            continue;
        c = c & ~TMXR_VALID;
        if (!sim_rem_single_mode[i]) {
            if (c == sim_int_char) {                /* ^E (the interrupt character) must start continue mode console interaction */
                sim_is_running = 0;
                sim_stop_timer_services ();
                for (j=0; j < sim_rem_con_tmxr.lines; j++) {
                    lp = &sim_rem_con_tmxr.ldsc[j];
                    if ((i == j) || (!lp->conn))
                        continue;
                    tmxr_linemsgf (lp, "\nRemote Console(%s) Entering Commands\n", lp->ipad);
                    tmxr_send_buffered_data (lp);           /* flush any buffered data */
                    }
                lp = &sim_rem_con_tmxr.ldsc[i];
                tmxr_linemsg (lp, "\r\nSimulator paused.\r\n");
                if (sim_rem_read_timeout)
                    tmxr_linemsgf (lp, "Simulation will resume automatically if input is not received in %d seconds\n", sim_rem_read_timeout);
                }
            else {
                if ((c == '\004') || (c == '\032')) { /* EOF character (^D or ^Z) ? */
                    tmxr_linemsgf (lp, "\r\nGoodbye\r\n");
                    tmxr_send_buffered_data (lp);           /* flush any buffered data */
                    tmxr_reset_ln (lp);
                    continue;
                    }
                sim_rem_single_mode[i] = TRUE;
                tmxr_linemsgf (lp, "\r\n%s", sim_prompt);
                tmxr_send_buffered_data (lp);           /* flush any buffered data */
                }
            }
        }
    got_command = FALSE;
    while (1) {
        if (!sim_rem_single_mode[i]) {
            read_start_time = sim_os_msec();
            tmxr_linemsg (lp, sim_prompt);
            tmxr_send_buffered_data (lp);           /* flush any buffered data */
            }
        do {
            if (!sim_rem_single_mode[i]) {
                c = tmxr_getc_ln (lp);
                if (!(TMXR_VALID & c)) {
                    tmxr_send_buffered_data (lp);   /* flush any buffered data */
                    if (sim_rem_read_timeout &&
                        ((sim_os_msec() - read_start_time)/1000 >= sim_rem_read_timeout)) {
                        while (sim_rem_buf_ptr[i] > 0) { /* Erase current input line */
                            tmxr_linemsg (lp, "\b \b");
                            --sim_rem_buf_ptr[i];
                            }
                        if (sim_rem_buf_ptr[i]+80 >= sim_rem_buf_size[i]) {
                            sim_rem_buf_size[i] += 1024;
                            sim_rem_buf[i] = realloc (sim_rem_buf[i], sim_rem_buf_size[i]);
                            }
                        strcpy (sim_rem_buf[i], "CONTINUE         ! Automatic continue due to timeout");
                        tmxr_linemsgf (lp, "%s\n", sim_rem_buf[i]);
                        got_command = TRUE;
                        break;
                        }
                    sim_os_ms_sleep (100);
                    tmxr_poll_rx (&sim_rem_con_tmxr);/* poll input */
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
                    if (sim_rem_buf_ptr[i] > 0) {
                        tmxr_linemsg (lp, "\b \b");
                        --sim_rem_buf_ptr[i];
                        }
                    break;
                case 27:   /* escape */
                case 21:   /* ^U */
                    while (sim_rem_buf_ptr[i] > 0) {
                        tmxr_linemsg (lp, "\b \b");
                        --sim_rem_buf_ptr[i];
                        }
                    break;
                case '\n':
                    if (sim_rem_buf_ptr[i] == 0)
                        break;
                case '\r':
                    tmxr_linemsg (lp, "\r\n");
                    if (sim_rem_buf_ptr[i]+1 >= sim_rem_buf_size[i]) {
                        sim_rem_buf_size[i] += 1024;
                        sim_rem_buf[i] = realloc (sim_rem_buf[i], sim_rem_buf_size[i]);
                        }
                    sim_rem_buf[i][sim_rem_buf_ptr[i]++] = '\0';
                    got_command = TRUE;
                    break;
                case '\004': /* EOF (^D) */
                case '\032': /* EOF (^Z) */
                    while (sim_rem_buf_ptr[i] > 0) { /* Erase current input line */
                        tmxr_linemsg (lp, "\b \b");
                        --sim_rem_buf_ptr[i];
                        }
                    if (!sim_rem_single_mode[i]) {
                        if (sim_rem_buf_ptr[i]+80 >= sim_rem_buf_size[i]) {
                            sim_rem_buf_size[i] += 1024;
                            sim_rem_buf[i] = realloc (sim_rem_buf[i], sim_rem_buf_size[i]);
                            }
                        strcpy (sim_rem_buf[i], "CONTINUE         ! Automatic continue before close");
                        tmxr_linemsgf (lp, "%s\n", sim_rem_buf[i]);
                        got_command = TRUE;
                        }
                    close_session = TRUE;
                    break;
                default:
                    tmxr_putc_ln (lp, c);
                    if (sim_rem_buf_ptr[i]+2 >= sim_rem_buf_size[i]) {
                        sim_rem_buf_size[i] += 1024;
                        sim_rem_buf[i] = realloc (sim_rem_buf[i], sim_rem_buf_size[i]);
                        }
                    sim_rem_buf[i][sim_rem_buf_ptr[i]++] = c;
                    sim_rem_buf[i][sim_rem_buf_ptr[i]] = '\0';
                    if (sim_rem_buf_ptr[i] >= sizeof(cbuf))
                        got_command = TRUE;                 /* command too long */
                    break;
                }
            } while ((!got_command) && (!sim_rem_single_mode[i]));
        tmxr_send_buffered_data (lp);           /* flush any buffered data */
        if ((sim_rem_single_mode[i]) && !got_command) {
            break;
            }
        printf ("Remote Console Command from %s> %s\r\n", lp->ipad, sim_rem_buf[i]);
        if (sim_log)
            fprintf (sim_log, "Remote Console Command from %s> %s\n", lp->ipad, sim_rem_buf[i]);
        got_command = FALSE;
        if (strlen(sim_rem_buf[i]) >= sizeof(cbuf)) {
            printf ("\nLine too long. Ignored.  Continuing Simulator execution\n");
            if (sim_log)
                fprintf (sim_log, "\r\nLine too long. Ignored.  Continuing Simulator execution\r\n");
            tmxr_linemsgf (lp, "\nLine too long. Ignored.  Continuing Simulator execution\n");
            tmxr_send_buffered_data (lp);       /* try to flush any buffered data */
            break;
            }
        strcpy (cbuf, sim_rem_buf[i]);
        sim_rem_buf_ptr[i] = 0;
        sim_rem_buf[i][sim_rem_buf_ptr[i]] = '\0';
        if (cbuf[0] == '\0') {
            if (sim_rem_single_mode[i]) {
                sim_rem_single_mode[i] = FALSE;
                break;
                }
            else
                continue;
            }
        sim_sub_args (cbuf, sizeof(cbuf), argv);
        cptr = cbuf;
        cptr = get_glyph (cptr, gbuf, 0);                   /* get command glyph */
        sim_switches = 0;                                   /* init switches */
        if (!sim_log) {                                     /* Not currently logging? */
            int32 save_quiet = sim_quiet;

            sim_quiet = 1;
            sprintf (sim_rem_con_temp_name, "sim_remote_console_%d.temporary_log", (int)getpid());
            sim_set_logon (0, sim_rem_con_temp_name);
            sim_quiet = save_quiet;
            sim_log_temp = TRUE;
            }
        cmd_log_start = sim_ftell (sim_log);
        if (!find_cmd (gbuf))                               /* validate command */
            stat = SCPE_UNK;
        else {
            if ((cmdp = find_ctab (sim_rem_single_mode[i] ? allowed_single_remote_cmds : allowed_remote_cmds, gbuf))) {/* lookup command */
                if (cmdp->action == &x_continue_cmd)
                    stat = SCPE_OK;
                else {
                    if (cmdp->action == &x_step_cmd) {
                        steps = 1;                          /* default of 1 instruction */
                        stat = SCPE_OK;
                        if (*cptr != 0) {                   /* argument? */
                             cptr = get_glyph (cptr, gbuf, 0);/* get next glyph */
                             if (*cptr != 0)                /* should be end */
                                 stat = SCPE_2MARG;
                             else {
                                 steps = (int32) get_uint (gbuf, 10, INT_MAX, &stat);
                                 if ((stat != SCPE_OK) || (steps <= 0)) /* error? */
                                     stat = SCPE_ARG;
                                 }
                             }
                        if (stat == SCPE_OK)
                            stepping = TRUE;
                        else
                            cmdp = NULL;
                        }
                    else
                        stat = cmdp->action (cmdp->arg, cptr);
                    }
                }
            else
                stat = SCPE_INVREM;
            }
        stat_nomessage = stat & SCPE_NOMESSAGE;             /* extract possible message supression flag */
        stat = SCPE_BARE_STATUS(stat);                      /* remove possible flag */
        if (!stat_nomessage) {                              /* displaying message status? */
            if (cmdp && (cmdp->message))                    /* special message handler? */
                cmdp->message (NULL, stat);                 /* let it deal with display */
            else
                if (stat >= SCPE_BASE) {                    /* error? */
                    printf ("%s\r\n", sim_error_text (stat));
                    if (sim_log)
                        fprintf (sim_log, "%s\n", sim_error_text (stat));
                    }
            }
        fflush (sim_log);
        sim_fseeko (sim_log, cmd_log_start, SEEK_SET);
        cbuf[sizeof(cbuf)-1] = '\0';
        while (fgets (cbuf, sizeof(cbuf)-1, sim_log)) {
            int32 unwritten;

            tmxr_linemsgf (lp, "%s", cbuf);
            do {
                unwritten = tmxr_send_buffered_data (lp);
                if (unwritten == lp->txbsz)
                    sim_os_ms_sleep (100);
                } while (unwritten == lp->txbsz);
            }
        if ((cmdp && (cmdp->action == &x_continue_cmd)) ||
            (sim_rem_single_mode[i])) {
            sim_rem_step_line = -1;                 /* Not stepping */
            if (sim_log_temp) {                     /* If we setup a temporary log, clean it now  */
                int32 save_quiet = sim_quiet;

                sim_quiet = 1;
                sim_set_logoff (0, NULL);
                sim_quiet = save_quiet;
                remove (sim_rem_con_temp_name);
                sim_log_temp = FALSE;
                }
            if (!sim_rem_single_mode[i]) {
                tmxr_linemsg (lp, "Simulator Running...");
                tmxr_send_buffered_data (lp);
                for (j=0; j < sim_rem_con_tmxr.lines; j++) {
                    lp = &sim_rem_con_tmxr.ldsc[j];
                    if ((i == j) || (!lp->conn))
                        continue;
                    tmxr_linemsg (lp, "Simulator Running...");
                    tmxr_send_buffered_data (lp);
                    }
                sim_is_running = 1;
                sim_start_timer_services ();
                }
            sim_rem_single_mode[i] = FALSE;
            break;
            }
        if (cmdp && (cmdp->action == &x_step_cmd)) {
            sim_rem_step_line = i;
            break;
            }
        }
    if (close_session) {
        tmxr_linemsgf (lp, "\r\nGoodbye\r\n");
        tmxr_send_buffered_data (lp);           /* flush any buffered data */
        tmxr_reset_ln (lp);
        }
    }
if (stepping)
    sim_activate(uptr, steps);                  /* check again after 'steps' instructions */
else
    sim_activate_after(uptr, 100000);           /* check again in 100 milliaeconds */
return SCPE_OK;
}

t_stat sim_rem_con_reset (DEVICE *dptr)
{
if (sim_rem_con_tmxr.lines)
    return sim_rem_con_poll_svc (&dptr->units[0]);          /* establish polling as needed */
return SCPE_OK;
}

static t_stat sim_set_rem_telnet (int32 flag, char *cptr)
{
t_stat r;

if (flag) {
    r = sim_parse_addr (cptr, NULL, 0, NULL, NULL, 0, NULL, NULL);
    if (r == SCPE_OK) {
        if (sim_rem_con_tmxr.master)                    /* already open? */
            sim_set_rem_telnet (0, NULL);               /* close first */
        if (sim_rem_con_tmxr.lines == 0)                /* Ir no connection limit set */
            sim_set_rem_connections (0, "1");           /* use 1 */
        sim_register_internal_device (&sim_remote_console);
        r = tmxr_attach (&sim_rem_con_tmxr, &sim_rem_con_unit[0], cptr);/* open master socket */
        if (r == SCPE_OK)
            sim_activate_after(&sim_rem_con_unit[0], 1000000); /* check for connection in 1 second */
        return r;
        }
    return SCPE_NOPARAM;
    }
else {
    if (sim_rem_con_tmxr.master) {
        int32 i;

        tmxr_detach (&sim_rem_con_tmxr, &sim_rem_con_unit[0]);
        for (i=0; i<sim_rem_con_tmxr.lines; i++) {
            free (sim_rem_buf[i]);
            sim_rem_buf[i] = NULL;
            sim_rem_buf_size[i] = 0;
            sim_rem_buf_ptr[i] = 0;
            sim_rem_single_mode[i] = FALSE;
            }
        }
    }
return SCPE_OK;
}

static t_stat sim_set_rem_connections (int32 flag, char *cptr)
{
int32 lines;
t_stat r;
int32 i;

if (cptr == NULL)
    return SCPE_ARG;
lines = (int32) get_uint (cptr, 10, MAX_REMOTE_SESSIONS, &r);
if (r != SCPE_OK)
    return r;
if (sim_rem_con_tmxr.master)
    return SCPE_ARG;
for (i=0; i<sim_rem_con_tmxr.lines; i++)
    free (sim_rem_buf[i]);
sim_rem_con_tmxr.lines = lines;
sim_rem_con_tmxr.ldsc = realloc (sim_rem_con_tmxr.ldsc, sizeof(*sim_rem_con_tmxr.ldsc)*lines);
memset (sim_rem_con_tmxr.ldsc, 0, sizeof(*sim_rem_con_tmxr.ldsc)*lines);
sim_rem_buf = realloc (sim_rem_buf, sizeof(*sim_rem_buf)*lines);
memset (sim_rem_buf, 0, sizeof(*sim_rem_buf)*lines);
sim_rem_buf_size = realloc (sim_rem_buf_size, sizeof(*sim_rem_buf_size)*lines);
memset (sim_rem_buf_size, 0, sizeof(*sim_rem_buf_size)*lines);
sim_rem_buf_ptr = realloc (sim_rem_buf_ptr, sizeof(*sim_rem_buf_ptr)*lines);
memset (sim_rem_buf_ptr, 0, sizeof(*sim_rem_buf_ptr)*lines);
sim_rem_single_mode = realloc (sim_rem_single_mode, sizeof(*sim_rem_single_mode)*lines);
memset (sim_rem_single_mode, 0, sizeof(*sim_rem_single_mode)*lines);
return SCPE_OK;
}

static t_stat sim_set_rem_timeout (int32 flag, char *cptr)
{
int32 timeout;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
timeout = (int32) get_uint (cptr, 10, 3600, &r);
if (r != SCPE_OK)
    return r;
sim_rem_read_timeout = timeout;
return SCPE_OK;
}

/* Set keyboard map */

t_stat sim_set_kmap (int32 flag, char *cptr)
{
DEVICE *dptr = sim_devices[0];
int32 val, rdx;
t_stat r;

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_2FARG;
if (dptr->dradix == 16) rdx = 16;
else rdx = 8;
val = (int32) get_uint (cptr, rdx, 0177, &r);
if ((r != SCPE_OK) ||
    ((val == 0) && (flag & KMAP_NZ)))
    return SCPE_ARG;
*(cons_kmap[flag & KMAP_MASK]) = val;
return SCPE_OK;
}

/* Show keyboard map */

t_stat sim_show_kmap (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
if (sim_devices[0]->dradix == 16)
    fprintf (st, "%s = %X\n", show_con_tab[flag].name, *(cons_kmap[flag & KMAP_MASK]));
else fprintf (st, "%s = %o\n", show_con_tab[flag].name, *(cons_kmap[flag & KMAP_MASK]));
return SCPE_OK;
}

/* Set printable characters */

t_stat sim_set_pchar (int32 flag, char *cptr)
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

t_stat sim_show_pchar (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
if (sim_devices[0]->dradix == 16)
    fprintf (st, "pchar mask = %X\n", sim_tt_pchar);
else fprintf (st, "pchar mask = %o\n", sim_tt_pchar);
return SCPE_OK;
}

/* Set log routine */

t_stat sim_set_logon (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
t_stat r;

if ((cptr == NULL) || (*cptr == 0))                     /* need arg */
    return SCPE_2FARG;
cptr = get_glyph_nc (cptr, gbuf, 0);                    /* get file name */
if (*cptr != 0)                                         /* now eol? */
    return SCPE_2MARG;
sim_set_logoff (0, NULL);                               /* close cur log */
r = sim_open_logfile (gbuf, FALSE, &sim_log, &sim_log_ref); /* open log */
if (r != SCPE_OK)                                       /* error? */
    return r;
if (!sim_quiet)
    printf ("Logging to file \"%s\"\n", 
             sim_logfile_name (sim_log, sim_log_ref));
fprintf (sim_log, "Logging to file \"%s\"\n", 
             sim_logfile_name (sim_log, sim_log_ref));  /* start of log */
return SCPE_OK;
}

/* Set nolog routine */

t_stat sim_set_logoff (int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
if (sim_log == NULL)                                    /* no log? */
    return SCPE_OK;
if (!sim_quiet)
    printf ("Log file closed\n");
fprintf (sim_log, "Log file closed\n");
sim_close_logfile (&sim_log_ref);                       /* close log */
sim_log = NULL;
return SCPE_OK;
}

/* Show log status */

t_stat sim_show_log (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
if (sim_log)
    fprintf (st, "Logging enabled to \"%s\"\n", 
                 sim_logfile_name (sim_log, sim_log_ref));
else fprintf (st, "Logging disabled\n");
return SCPE_OK;
}

/* Set debug routine */

t_stat sim_set_debon (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
t_stat r;

if ((cptr == NULL) || (*cptr == 0))                     /* need arg */
    return SCPE_2FARG;
cptr = get_glyph_nc (cptr, gbuf, 0);                    /* get file name */
if (*cptr != 0)                                         /* now eol? */
    return SCPE_2MARG;
r = sim_open_logfile (gbuf, FALSE, &sim_deb, &sim_deb_ref);

if (r != SCPE_OK)
    return r;
if (!sim_quiet)
    printf ("Debug output to \"%s\"\n", 
            sim_logfile_name (sim_deb, sim_deb_ref));
if (sim_log)
    fprintf (sim_log, "Debug output to \"%s\"\n", 
                      sim_logfile_name (sim_deb, sim_deb_ref));
return SCPE_OK;
}

/* Set nodebug routine */

t_stat sim_set_deboff (int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
if (sim_deb == NULL)                                    /* no log? */
    return SCPE_OK;
sim_close_logfile (&sim_deb_ref);
sim_deb = NULL;
if (!sim_quiet)
    printf ("Debug output disabled\n");
if (sim_log)
    fprintf (sim_log, "Debug output disabled\n");
return SCPE_OK;
}

/* Show debug routine */

t_stat sim_show_debug (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
if (sim_deb)
    fprintf (st, "Debug output enabled to \"%s\"\n", 
                 sim_logfile_name (sim_deb, sim_deb_ref));
else fprintf (st, "Debug output disabled\n");
return SCPE_OK;
}

/* SET CONSOLE command */

/* Set console to Telnet port (and parameters) */

t_stat sim_set_telnet (int32 flag, char *cptr)
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
        r = ctptr->action (ctptr->arg, cvptr);      /* do the rest */
        if (r != SCPE_OK)
            return r;
        }
    else {
        r = sim_parse_addr (gbuf, NULL, 0, NULL, NULL, 0, NULL, NULL);
        if (r == SCPE_OK) {
            if (sim_con_tmxr.master)                        /* already open? */
                sim_set_notelnet (0, NULL);                 /* close first */
            r = tmxr_attach (&sim_con_tmxr, &sim_con_unit, gbuf);/* open master socket */
            if (r == SCPE_OK)
                sim_activate_after(&sim_con_unit, 1000000); /* check for connection in 1 second */
            return r;
            }
        return SCPE_NOPARAM;
        }
    }
return SCPE_OK;
}

/* Close console Telnet port */

t_stat sim_set_notelnet (int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))                               /* too many arguments? */
    return SCPE_2MARG;
if (sim_con_tmxr.master == 0)                           /* ignore if already closed */
    return SCPE_OK;
return tmxr_close_master (&sim_con_tmxr);               /* close master socket */
}

/* Show console Telnet status */

t_stat sim_show_telnet (FILE *st, DEVICE *dunused, UNIT *uunused, int32 flag, char *cptr)
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

t_stat sim_set_cons_buff (int32 flg, char *cptr)
{
char cmdbuf[CBUFSIZE];

sprintf(cmdbuf, "BUFFERED%c%s", cptr ? '=' : '\0', cptr ? cptr : "");
return tmxr_open_master (&sim_con_tmxr, cmdbuf);      /* open master socket */
}

/* Set console to NoBuffering */

t_stat sim_set_cons_unbuff (int32 flg, char *cptr)
{
char cmdbuf[CBUFSIZE];

sprintf(cmdbuf, "UNBUFFERED%c%s", cptr ? '=' : '\0', cptr ? cptr : "");
return tmxr_open_master (&sim_con_tmxr, cmdbuf);      /* open master socket */
}

/* Set console to Logging */

t_stat sim_set_cons_log (int32 flg, char *cptr)
{
char cmdbuf[CBUFSIZE];

sprintf(cmdbuf, "LOG%c%s", cptr ? '=' : '\0', cptr ? cptr : "");
return tmxr_open_master (&sim_con_tmxr, cmdbuf);      /* open master socket */
}

/* Set console to NoLogging */

t_stat sim_set_cons_nolog (int32 flg, char *cptr)
{
char cmdbuf[CBUFSIZE];

sprintf(cmdbuf, "NOLOG%c%s", cptr ? '=' : '\0', cptr ? cptr : "");
return tmxr_open_master (&sim_con_tmxr, cmdbuf);      /* open master socket */
}

t_stat sim_show_cons_log (FILE *st, DEVICE *dunused, UNIT *uunused, int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
if (sim_con_tmxr.ldsc->txlog)
    fprintf (st, "Log File being written to %s\n", sim_con_tmxr.ldsc->txlogname);
else
    fprintf (st, "No Logging\n");
return SCPE_OK;
}

t_stat sim_show_cons_buff (FILE *st, DEVICE *dunused, UNIT *uunused, int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
if (!sim_con_tmxr.buffered)
    fprintf (st, "Unbuffered\n");
else
    fprintf (st, "Buffer Size = %d\n", sim_con_tmxr.buffered);
return SCPE_OK;
}

/* Set console Debug Mode */

t_stat sim_set_cons_debug (int32 flg, char *cptr)
{
return set_dev_debug (&sim_con_telnet, &sim_con_unit, flg, cptr);
}

t_stat sim_show_cons_debug (FILE *st, DEVICE *dunused, UNIT *uunused, int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
return show_dev_debug (st, &sim_con_telnet, &sim_con_unit, flag, cptr);
}

/* Set console to Serial port (and parameters) */

t_stat sim_set_serial (int32 flag, char *cptr)
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
                char cbuf[CBUFSIZE];
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

t_stat sim_set_noserial (int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))                               /* too many arguments? */
    return SCPE_2MARG;
if (sim_con_ldsc.serport == 0)                  /* ignore if already closed */
    return SCPE_OK;
return tmxr_close_master (&sim_con_tmxr);               /* close master socket */
}

/* Log File Open/Close/Show Support */

/* Open log file */

t_stat sim_open_logfile (char *filename, t_bool binary, FILE **pf, FILEREF **pref)
{
char *tptr, gbuf[CBUFSIZE];

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
    *pref = calloc (1, sizeof(**pref));
    if (!*pref)
        return SCPE_MEM;
    get_glyph_nc (filename, gbuf, 0);                   /* reparse */
    strncpy ((*pref)->name, gbuf, sizeof((*pref)->name)-1);
    *pf = sim_fopen (gbuf, (binary ? "a+b" : "a+"));    /* open file */
    if (*pf == NULL) {                                  /* error? */
        free (*pref);
        *pref = NULL;
        return SCPE_OPENERR;
        }
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
if ((*pref)->refcount > 0)
    return SCPE_OK;
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

/* Check connection before executing */

t_stat sim_check_console (int32 sec)
{
int32 c, i;

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
            printf ("Running with Buffered Console\r\n"); /* print transition */
            fflush (stdout);
            if (sim_log) {                              /* log file? */
                fprintf (sim_log, "Running with Buffered Console\n");
                fflush (sim_log);
                }
            }
        return SCPE_OK;
        }
    }
for (i = 0; i < sec; i++) {                             /* loop */
    if (tmxr_poll_conn (&sim_con_tmxr) >= 0) {          /* poll connect */
        sim_con_ldsc.rcve = 1;                          /* rcv enabled */
        if (i) {                                        /* if delayed */
            printf ("Running\r\n");                       /* print transition */
            fflush (stdout);
            if (sim_log) {                              /* log file? */
                fprintf (sim_log, "Running\n");
                fflush (sim_log);
                }
            }
        return SCPE_OK;                                 /* ready to proceed */
        }
    c = sim_os_poll_kbd ();                             /* check for stop char */
    if ((c == SCPE_STOP) || stop_cpu)
        return SCPE_STOP;
    if ((i % 10) == 0) {                                /* Status every 10 sec */
        printf ("Waiting for console Telnet connection\r\n");
        fflush (stdout);
        if (sim_log) {                              /* log file? */
            fprintf (sim_log, "Waiting for console Telnet connection\n");
            fflush (sim_log);
            }
        }
    sim_os_sleep (1);                                   /* wait 1 second */
    }
return SCPE_TTMO;                                       /* timed out */
}

/* Poll for character */

t_stat sim_poll_kbd (void)
{
int32 c;

c = sim_os_poll_kbd ();                                 /* get character */
if ((c == SCPE_STOP) ||                                 /* ^E or not Telnet? */
    ((sim_con_tmxr.master == 0) &&                      /*       and not serial? */
     (sim_con_ldsc.serport == 0)))
    return c;                                           /* in-window */
if (!sim_con_ldsc.conn) {                               /* no telnet or serial connection? */
    if (!sim_con_ldsc.txbfd)                            /* unbuffered? */
        return SCPE_LOST;                               /* connection lost */
    if (tmxr_poll_conn (&sim_con_tmxr) >= 0)            /* poll connect */
        sim_con_ldsc.rcve = 1;                          /* rcv enabled */
    else                                                /* fall through to poll reception */
        return SCPE_OK;                                 /* unconnected and buffered - nothing to receive */
    }
tmxr_poll_rx (&sim_con_tmxr);                           /* poll for input */
if ((c = tmxr_getc_ln (&sim_con_ldsc)))                 /* any char? */ 
    return (c & (SCPE_BREAK | 0377)) | SCPE_KFLAG;
return SCPE_OK;
}

/* Output character */

t_stat sim_putchar (int32 c)
{
if ((sim_con_tmxr.master == 0) &&                       /* not Telnet? */
    (sim_con_ldsc.serport == 0)) {                      /* and not serial port */
    if (sim_log)                                        /* log file? */
        fputc (c, sim_log);
    return sim_os_putchar (c);                          /* in-window version */
    }
if (sim_log && !sim_con_ldsc.txlog)                     /* log file, but no line log? */
    fputc (c, sim_log);
if (!sim_con_ldsc.conn) {                               /* no Telnet or serial connection? */
    if (!sim_con_ldsc.txbfd)                            /* unbuffered? */
        return SCPE_LOST;                               /* connection lost */
    if (tmxr_poll_conn (&sim_con_tmxr) >= 0)            /* poll connect */
        sim_con_ldsc.rcve = 1;                          /* rcv enabled */
    }
tmxr_putc_ln (&sim_con_ldsc, c);                        /* output char */
tmxr_poll_tx (&sim_con_tmxr);                           /* poll xmt */
return SCPE_OK;
}

t_stat sim_putchar_s (int32 c)
{
t_stat r;

if ((sim_con_tmxr.master == 0) &&                       /* not Telnet? */
    (sim_con_ldsc.serport == 0)) {                      /* and not serial port */
    if (sim_log)                                        /* log file? */
        fputc (c, sim_log);
    return sim_os_putchar (c);                          /* in-window version */
    }
if (sim_log && !sim_con_ldsc.txlog)                     /* log file, but no line log? */
    fputc (c, sim_log);
if (!sim_con_ldsc.conn) {                               /* no Telnet or serial connection? */
    if (!sim_con_ldsc.txbfd)                            /* non-buffered Telnet connection? */
        return SCPE_LOST;                               /* lost */
    if (tmxr_poll_conn (&sim_con_tmxr) >= 0)            /* poll connect */
        sim_con_ldsc.rcve = 1;                          /* rcv enabled */
    }
if (sim_con_ldsc.xmte == 0)                             /* xmt disabled? */
    r = SCPE_STALL;
else r = tmxr_putc_ln (&sim_con_ldsc, c);               /* no, Telnet output */
tmxr_poll_tx (&sim_con_tmxr);                           /* poll xmt */
return r;                                               /* return status */
}

/* Input character processing */

int32 sim_tt_inpcvt (int32 c, uint32 mode)
{
uint32 md = mode & TTUF_M_MODE;

if (md != TTUF_MODE_8B) {
    c = c & 0177;
    if (md == TTUF_MODE_UC) {
        if (islower (c))
            c = toupper (c);
        if (mode & TTUF_KSR)
            c = c | 0200;
        }
    }
else c = c & 0377;
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


#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
extern pthread_mutex_t     sim_tmxr_poll_lock;
extern pthread_cond_t      sim_tmxr_poll_cond;
extern int32               sim_tmxr_poll_count;
extern t_bool              sim_tmxr_poll_running;
extern int32 sim_is_running;

pthread_t           sim_console_poll_thread;       /* Keyboard Polling Thread Id */
t_bool              sim_console_poll_running = FALSE;
pthread_cond_t      sim_console_startup_cond;

static void *
_console_poll(void *arg)
{
int sched_policy;
struct sched_param sched_priority;
int wait_count = 0;
DEVICE *d;

/* Boost Priority for this I/O thread vs the CPU instruction execution 
   thread which, in general, won't be readily yielding the processor when 
   this thread needs to run */
pthread_getschedparam (pthread_self(), &sched_policy, &sched_priority);
++sched_priority.sched_priority;
pthread_setschedparam (pthread_self(), sched_policy, &sched_priority);

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
tmxr_shutdown ();
return sim_os_ttclose ();
}

t_bool sim_ttisatty (void)
{
return sim_os_ttisatty ();
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

static t_bool sim_os_ttisatty (void)
{
return isatty (fileno (stdin));
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
if (buf[0] == sim_int_char) return SCPE_STOP;
if (sim_brk_char && (buf[0] == sim_brk_char))
    return SCPE_BREAK;
return (buf[0] | SCPE_KFLAG);
}

static t_stat sim_os_poll_kbd (void)
{
t_stat response;

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
#include <windows.h>
#define RAW_MODE 0
static HANDLE std_input;
static HANDLE std_output;
static DWORD saved_mode;

/* Note: This routine catches all the potential events which some aspect 
         of the windows system can generate.  The CTRL_C_EVENT won't be 
         generated by a  user typing in a console session since that 
         session is in RAW mode.  In general, Ctrl-C on a simulator's
         console terminal is a useful character to be passed to the 
         simulator.  This code does nothing to disable or affect that. */

static BOOL WINAPI
ControlHandler(DWORD dwCtrlType)
    {
    DWORD Mode;
    extern void int_handler (int sig);

    switch (dwCtrlType)
        {
        case CTRL_BREAK_EVENT:      // Use CTRL-Break or CTRL-C to simulate 
        case CTRL_C_EVENT:          // SERVICE_CONTROL_STOP in debug mode
            int_handler(0);
            return TRUE;
        case CTRL_CLOSE_EVENT:      // Window is Closing
        case CTRL_LOGOFF_EVENT:     // User is logging off
            if (!GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &Mode))
                return TRUE;        // Not our User, so ignore
        case CTRL_SHUTDOWN_EVENT:   // System is shutting down
            int_handler(0);
            return TRUE;
        }
    return FALSE;
    }

static t_stat sim_os_ttinit (void)
{
SetConsoleCtrlHandler( ControlHandler, TRUE );
std_input = GetStdHandle (STD_INPUT_HANDLE);
std_output = GetStdHandle (STD_OUTPUT_HANDLE);
if ((std_input) &&                                      /* Not Background process? */
    (std_input != INVALID_HANDLE_VALUE))
    GetConsoleMode (std_input, &saved_mode);            /* Save Mode */
return SCPE_OK;
}

static t_stat sim_os_ttrun (void)
{
if ((std_input) &&                                      /* If Not Background process? */
    (std_input != INVALID_HANDLE_VALUE) &&
    (!GetConsoleMode(std_input, &saved_mode) ||         /* Set mode to RAW */
     !SetConsoleMode(std_input, RAW_MODE)))
    return SCPE_TTYERR;
if (sim_log) {
    fflush (sim_log);
    _setmode (_fileno (sim_log), _O_BINARY);
    }
SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
return SCPE_OK;
}

static t_stat sim_os_ttcmd (void)
{
if (sim_log) {
    fflush (sim_log);
    _setmode (_fileno (sim_log), _O_TEXT);
    }
SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_NORMAL);
if ((std_input) &&                                      /* If Not Background process? */
    (std_input != INVALID_HANDLE_VALUE) &&
    (!SetConsoleMode(std_input, saved_mode)))           /* Restore Normal mode */
    return SCPE_TTYERR;
return SCPE_OK;
}

static t_stat sim_os_ttclose (void)
{
return SCPE_OK;
}

static t_bool sim_os_ttisatty (void)
{
DWORD Mode;

return (std_input) && (std_input != INVALID_HANDLE_VALUE) && GetConsoleMode (std_input, &Mode);
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

static t_stat sim_os_putchar (int32 c)
{
DWORD unused;

if (c != 0177)
    WriteConsoleA(std_output, &c, 1, &unused, NULL);
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

static t_bool sim_os_ttisatty (void)
{
return 1;
}

static t_stat sim_os_poll_kbd (void)
{
int c;

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

static t_stat sim_os_ttinit (void) {
    int i;
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
    strcat(title, sim_name);
    strcat(title, " Simulator");
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

static t_bool sim_os_ttisatty (void)
{
return 1;
}

static t_stat sim_os_poll_kbd (void)
{
int c;

if (!ps_kbhit ())
    return SCPE_OK;
c = ps_getch();
if ((c & 0177) == sim_del_char)
    c = 0177;
if ((c & 0177) == sim_int_char) return SCPE_STOP;
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

struct sgttyb cmdtty,runtty;                            /* V6/V7 stty data */
struct tchars cmdtchars,runtchars;                      /* V7 editing */
struct ltchars cmdltchars,runltchars;                   /* 4.2 BSD editing */
int cmdfl,runfl;                                        /* TTY flags */

static t_stat sim_os_ttinit (void)
{
cmdfl = fcntl (0, F_GETFL, 0);                          /* get old flags  and status */
runfl = cmdfl | FNDELAY;
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
runtchars.t_intrc = sim_int_char;                       /* in case changed */
fcntl (0, F_SETFL, runfl);                              /* non-block mode */
if (ioctl (0, TIOCSETP, &runtty) < 0)
    return SCPE_TTIERR;
if (ioctl (0, TIOCSETC, &runtchars) < 0)
    return SCPE_TTIERR;
if (ioctl (0, TIOCSLTC, &runltchars) < 0)
    return SCPE_TTIERR;
nice (10);                                              /* lower priority */
return SCPE_OK;
}

static t_stat sim_os_ttcmd (void)
{
nice (-10);                                             /* restore priority */
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

static t_bool sim_os_ttisatty (void)
{
return isatty (0);
}

static t_stat sim_os_poll_kbd (void)
{
int status;
unsigned char buf[1];

status = read (0, buf, 1);
if (status != 1) return SCPE_OK;
if (sim_brk_char && (buf[0] == sim_brk_char))
    return SCPE_BREAK;
else return (buf[0] | SCPE_KFLAG);
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

#include <termios.h>
#include <unistd.h>

struct termios cmdtty, runtty;
static int prior_norm = 1;

static t_stat sim_os_ttinit (void)
{
if (!isatty (fileno (stdin)))                           /* skip if !tty */
    return SCPE_OK;
if (tcgetattr (0, &cmdtty) < 0)                         /* get old flags */
    return SCPE_TTIERR;
runtty = cmdtty;
runtty.c_lflag = runtty.c_lflag & ~(ECHO | ICANON);     /* no echo or edit */
runtty.c_oflag = runtty.c_oflag & ~OPOST;               /* no output edit */
runtty.c_iflag = runtty.c_iflag & ~ICRNL;               /* no cr conversion */
runtty.c_cc[VINTR] = sim_int_char;                      /* interrupt */
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
if (!isatty (fileno (stdin)))                           /* skip if !tty */
    return SCPE_OK;
runtty.c_cc[VINTR] = sim_int_char;                      /* in case changed */
if (tcsetattr (0, TCSAFLUSH, &runtty) < 0)
    return SCPE_TTIERR;
if (prior_norm) {                                       /* at normal pri? */
    errno =     0;
    (void)nice (10);                                    /* try to lower pri */
    prior_norm = errno;                                 /* if no error, done */
    }
return SCPE_OK;
}

static t_stat sim_os_ttcmd (void)
{
if (!isatty (fileno (stdin)))                           /* skip if !tty */
    return SCPE_OK;
if (!prior_norm) {                                      /* priority down? */
    errno =     0;
    (void)nice (-10);                                   /* try to raise pri */
    prior_norm = (errno == 0);                          /* if no error, done */
    }
if (tcsetattr (0, TCSAFLUSH, &cmdtty) < 0)
    return SCPE_TTIERR;
return SCPE_OK;
}

static t_stat sim_os_ttclose (void)
{
return sim_ttcmd ();
}

static t_bool sim_os_ttisatty (void)
{
return isatty (fileno (stdin));
}

static t_stat sim_os_poll_kbd (void)
{
int status;
unsigned char buf[1];

status = read (0, buf, 1);
if (status != 1) return SCPE_OK;
if (sim_brk_char && (buf[0] == sim_brk_char))
    return SCPE_BREAK;
else return (buf[0] | SCPE_KFLAG);
}

static t_bool sim_os_poll_kbd_ready (int ms_timeout)
{
fd_set readfds;
struct timeval timeout;

if (!sim_os_ttisatty()) {                   /* skip if !tty */
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
(void)write (1, &c, 1);
return SCPE_OK;
}

#endif
