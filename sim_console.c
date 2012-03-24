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
   20-Jan-11    MP      Added support for BREAK key on Windows
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
   01-Feb-02    RMS     Added VAX fix from Robert Alan Byer
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

   This module implements the following routines to support terminal I/O:

   sim_poll_kbd -       poll for keyboard input
   sim_putchar  -       output character to console
   sim_putchar_s -      output character to console, stall if congested
   sim_set_console -    set console parameters
   sim_show_console -   show console parameters
   sim_tt_inpcvt -      convert input character per mode
   sim_tt_outcvt -      convert output character per mode

   sim_ttinit   -       called once to get initial terminal state
   sim_ttrun    -       called to put terminal into run state
   sim_ttcmd    -       called to return terminal to command state
   sim_ttclose  -       called once before the simulator exits
   sim_os_poll_kbd -    poll for keyboard input
   sim_os_putchar -     output character to console

   The first group is OS-independent; the second group is OS-dependent.

   The following routines are exposed but deprecated:

   sim_set_telnet -     set console to Telnet port
   sim_set_notelnet -   close console Telnet port
   sim_show_telnet -    show console status
*/

#include "sim_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

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
TMLN sim_con_ldsc = { 0 };                              /* console line descr */
TMXR sim_con_tmxr = { 1, 0, 0, &sim_con_ldsc };         /* console line mux */

extern volatile int32 stop_cpu;
extern int32 sim_quiet, sim_deb_close;
extern FILE *sim_log, *sim_deb;
extern DEVICE *sim_devices[];

/* Set/show data structures */

static CTAB set_con_tab[] = {
    { "WRU", &sim_set_kmap, KMAP_WRU | KMAP_NZ },
    { "BRK", &sim_set_kmap, KMAP_BRK },
    { "DEL", &sim_set_kmap, KMAP_DEL |KMAP_NZ },
    { "PCHAR", &sim_set_pchar, 0 },
    { "TELNET", &sim_set_telnet, 0 },
    { "NOTELNET", &sim_set_notelnet, 0 },
    { "LOG", &sim_set_logon, 0 },
    { "NOLOG", &sim_set_logoff, 0 },
    { "DEBUG", &sim_set_debon, 0 },
    { "NODEBUG", &sim_set_deboff, 0 },
    { NULL, NULL, 0 }
    };

static SHTAB show_con_tab[] = {
    { "WRU", &sim_show_kmap, KMAP_WRU },
    { "BRK", &sim_show_kmap, KMAP_BRK },
    { "DEL", &sim_show_kmap, KMAP_DEL },
    { "PCHAR", &sim_show_pchar, 0 },
    { "LOG", &sim_show_log, 0 },
    { "TELNET", &sim_show_telnet, 0 },
    { "DEBUG", &sim_show_debug, 0 },
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
    if (cvptr = strchr (gbuf, '='))                     /* = value? */
        *cvptr++ = 0;
    get_glyph (gbuf, gbuf, 0);                          /* modifier to UC */
    if (ctptr = find_ctab (set_con_tab, gbuf)) {        /* match? */
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
    if (shptr = find_shtab (show_con_tab, gbuf))
        shptr->action (st, dptr, uptr, shptr->arg, cptr);
    else return SCPE_NOPARAM;
    }
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

if ((cptr == NULL) || (*cptr == 0))                     /* need arg */
    return SCPE_2FARG;
cptr = get_glyph_nc (cptr, gbuf, 0);                    /* get file name */
if (*cptr != 0)                                         /* now eol? */
    return SCPE_2MARG;
sim_set_logoff (0, NULL);                               /* close cur log */
sim_log = sim_fopen (gbuf, "a");                        /* open log */
if (sim_log == NULL)                                    /* error? */
    return SCPE_OPENERR;
if (!sim_quiet)
    printf ("Logging to file \"%s\"\n", gbuf);
fprintf (sim_log, "Logging to file \"%s\"\n", gbuf);    /* start of log */
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
fprintf (sim_log, "Log file closed\n");                 /* close log */
fclose (sim_log);
sim_log = NULL;
return SCPE_OK;
}

/* Show log status */

t_stat sim_show_log (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
if (sim_log)
    fputs ("Logging enabled\n", st);
else fputs ("Logging disabled\n", st);
return SCPE_OK;
}

/* Set debug routine */

t_stat sim_set_debon (int32 flag, char *cptr)
{
char *tptr, gbuf[CBUFSIZE];

if ((cptr == NULL) || (*cptr == 0))                     /* too few arguments? */
    return SCPE_2FARG;
tptr = get_glyph (cptr, gbuf, 0);                       /* get file name */
if (*tptr != 0)                                         /* now eol? */
    return SCPE_2MARG;
sim_set_deboff (0, NULL);                               /* close cur debug */
if (strcmp (gbuf, "LOG") == 0) {                        /* debug to log? */
    if (sim_log == NULL)                                /* any log? */
        return SCPE_ARG;
    sim_deb = sim_log;
    }
else if (strcmp (gbuf, "STDOUT") == 0)                  /* debug to stdout? */
    sim_deb = stdout;
else if (strcmp (gbuf, "STDERR") == 0)                  /* debug to stderr? */
    sim_deb = stderr;
else {
    cptr = get_glyph_nc (cptr, gbuf, 0);                /* reparse */
    sim_deb = sim_fopen (gbuf, "a");                    /* open debug */
    if (sim_deb == NULL)                                /* error? */
        return SCPE_OPENERR;
    sim_deb_close = 1;                                  /* need close */
    }
if (!sim_quiet)
    printf ("Debug output to \"%s\"\n", gbuf);
if (sim_log)
    fprintf (sim_log, "Debug output to \"%s\"\n", gbuf);
return SCPE_OK;
}

/* Set nodebug routine */

t_stat sim_set_deboff (int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
if (sim_deb == NULL)                                    /* no log? */
    return SCPE_OK;
if (!sim_quiet)
    printf ("Debug output disabled\n");
if (sim_log)
    fprintf (sim_log, "Debug output disabled\n");
if (sim_deb_close)                                      /* close if needed */
    fclose (sim_deb);
sim_deb_close = 0;
sim_deb = NULL;
return SCPE_OK;
}

/* Show debug routine */

t_stat sim_show_debug (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
if (sim_deb)
    fputs ("Debug output enabled\n", st);
else fputs ("Debug output disabled\n", st);
return SCPE_OK;
}

/* Set console to Telnet port */

t_stat sim_set_telnet (int32 flg, char *cptr)
{
if ((cptr == NULL) || (*cptr == 0))                     /* too few arguments? */
    return SCPE_2FARG;
if (sim_con_tmxr.master)                                /* already open? */
    return SCPE_ALATT;
return tmxr_open_master (&sim_con_tmxr, cptr);          /* open master socket */
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
if (sim_con_tmxr.master == 0)
    fprintf (st, "Connected to console window\n");
else if (sim_con_ldsc.conn == 0)
    fprintf (st, "Listening on port %d\n", sim_con_tmxr.port);
else {
    fprintf (st, "Listening on port %d, connected to socket %d\n",
        sim_con_tmxr.port, sim_con_ldsc.conn);
    tmxr_fconns (st, &sim_con_ldsc, -1);
    tmxr_fstats (st, &sim_con_ldsc, -1);
    }
return SCPE_OK;
}

/* Check connection before executing */

t_stat sim_check_console (int32 sec)
{
int32 c, i;

if (sim_con_tmxr.master == 0)                           /* not Telnet? done */
    return SCPE_OK;
if (sim_con_ldsc.conn) {                                /* connected? */
    tmxr_poll_rx (&sim_con_tmxr);                       /* poll (check disconn) */
    if (sim_con_ldsc.conn)                              /* still connected? */
        return SCPE_OK;
    }
for (i = 0; i < sec; i++) {                             /* loop */
    if (tmxr_poll_conn (&sim_con_tmxr) >= 0) {          /* poll connect */
        sim_con_ldsc.rcve = 1;                          /* rcv enabled */
        if (i) {                                        /* if delayed */
            printf ("Running\n");                       /* print transition */
            fflush (stdout);
            }
        return SCPE_OK;                                 /* ready to proceed */
        }
    c = sim_os_poll_kbd ();                             /* check for stop char */
    if ((c == SCPE_STOP) || stop_cpu)
        return SCPE_STOP;
    if ((i % 10) == 0) {                                /* Status every 10 sec */
        printf ("Waiting for console Telnet connection\n");
        fflush (stdout);
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
if ((c == SCPE_STOP) || (sim_con_tmxr.master == 0))     /* ^E or not Telnet? */
    return c;                                           /* in-window */
if (sim_con_ldsc.conn == 0)                              /* no Telnet conn? */
    return SCPE_LOST;
tmxr_poll_rx (&sim_con_tmxr);                           /* poll for input */
if (c = tmxr_getc_ln (&sim_con_ldsc))                   /* any char? */ 
    return (c & (SCPE_BREAK | 0377)) | SCPE_KFLAG;
return SCPE_OK;
}

/* Output character */

t_stat sim_putchar (int32 c)
{
if (sim_log)                                            /* log file? */
    fputc (c, sim_log);
if (sim_con_tmxr.master == 0)                           /* not Telnet? */
    return sim_os_putchar (c);                          /* in-window version */
if (sim_con_ldsc.conn == 0)                             /* no Telnet conn? */
    return SCPE_LOST;
tmxr_putc_ln (&sim_con_ldsc, c);                        /* output char */
tmxr_poll_tx (&sim_con_tmxr);                           /* poll xmt */
return SCPE_OK;
}

t_stat sim_putchar_s (int32 c)
{
t_stat r;

if (sim_log) fputc (c, sim_log);                        /* log file? */
if (sim_con_tmxr.master == 0)                           /* not Telnet? */
    return sim_os_putchar (c);                          /* in-window version */
if (sim_con_ldsc.conn == 0)                             /* no Telnet conn? */
    return SCPE_LOST;
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

/* VMS routines, from Ben Thomas, with fixes from Robert Alan Byer */

#if defined (VMS)

#if defined(__VAX)
#define sys$assign SYS$ASSIGN
#define sys$qiow SYS$QIOW
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

t_stat sim_ttinit (void)
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

t_stat sim_ttrun (void)
{
unsigned int status;
IOSB iosb;

status = sys$qiow (EFN, tty_chan, IO$_SETMODE, &iosb, 0, 0,
    &run_mode, sizeof (run_mode), 0, 0, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL))
    return SCPE_TTIERR;
return SCPE_OK;
}

t_stat sim_ttcmd (void)
{
unsigned int status;
IOSB iosb;

status = sys$qiow (EFN, tty_chan, IO$_SETMODE, &iosb, 0, 0,
    &cmd_mode, sizeof (cmd_mode), 0, 0, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL))
    return SCPE_TTIERR;
return SCPE_OK;
}

t_stat sim_ttclose (void)
{
return sim_ttcmd ();
}

t_stat sim_os_poll_kbd (void)
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

t_stat sim_os_putchar (int32 out)
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

t_stat sim_ttinit (void)
{
SetConsoleCtrlHandler( ControlHandler, TRUE );
std_input = GetStdHandle (STD_INPUT_HANDLE);
std_output = GetStdHandle (STD_OUTPUT_HANDLE);
if ((std_input == INVALID_HANDLE_VALUE) ||
    !GetConsoleMode (std_input, &saved_mode))
    return SCPE_TTYERR;
return SCPE_OK;
}
 
t_stat sim_ttrun (void)
{
if (!GetConsoleMode(std_input, &saved_mode) ||
    !SetConsoleMode(std_input, RAW_MODE))
    return SCPE_TTYERR;
if (sim_log) {
    fflush (sim_log);
    _setmode (_fileno (sim_log), _O_BINARY);
    }
SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
return SCPE_OK;
}

t_stat sim_ttcmd (void)
{
if (sim_log) {
    fflush (sim_log);
    _setmode (_fileno (sim_log), _O_TEXT);
    }
SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_NORMAL);
if (!SetConsoleMode(std_input, saved_mode)) return SCPE_TTYERR;
return SCPE_OK;
}

t_stat sim_ttclose (void)
{
return SCPE_OK;
}

t_stat sim_os_poll_kbd (void)
{
int c = -1;
DWORD nkbevents, nkbevent;
INPUT_RECORD rec;

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

t_stat sim_os_putchar (int32 c)
{
DWORD unused;

if (c != 0177)
    WriteConsoleA(std_output, &c, 1, &unused, NULL);
return SCPE_OK;
}

/* OS/2 routines, from Bruce Ray and Holger Veit */

#elif defined (__OS2__)

#include <conio.h>

t_stat sim_ttinit (void)
{
return SCPE_OK;
}

t_stat sim_ttrun (void)
{
return SCPE_OK;
}

t_stat sim_ttcmd (void)
{
return SCPE_OK;
}

t_stat sim_ttclose (void)
{
return SCPE_OK;
}

t_stat sim_os_poll_kbd (void)
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

t_stat sim_os_putchar (int32 c)
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

extern char sim_name[];
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

t_stat sim_ttinit (void) {
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

t_stat sim_ttrun (void)
{
return SCPE_OK;
}

t_stat sim_ttcmd (void)
{
return SCPE_OK;
}

t_stat sim_ttclose (void)
{
return SCPE_OK;
}

t_stat sim_os_poll_kbd (void)
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

t_stat sim_os_putchar (int32 c)
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

t_stat sim_ttinit (void)
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

t_stat sim_ttrun (void)
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

t_stat sim_ttcmd (void)
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

t_stat sim_ttclose (void)
{
return sim_ttcmd ();
}

t_stat sim_os_poll_kbd (void)
{
int status;
unsigned char buf[1];

status = read (0, buf, 1);
if (status != 1) return SCPE_OK;
if (sim_brk_char && (buf[0] == sim_brk_char))
    return SCPE_BREAK;
else return (buf[0] | SCPE_KFLAG);
}

t_stat sim_os_putchar (int32 out)
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

t_stat sim_ttinit (void)
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

t_stat sim_ttrun (void)
{
if (!isatty (fileno (stdin)))                           /* skip if !tty */
    return SCPE_OK;
runtty.c_cc[VINTR] = sim_int_char;                      /* in case changed */
if (tcsetattr (0, TCSAFLUSH, &runtty) < 0)
    return SCPE_TTIERR;
if (prior_norm) {                                       /* at normal pri? */
    errno =     0;
    nice (10);                                          /* try to lower pri */
    prior_norm = errno;                                 /* if no error, done */
    }
return SCPE_OK;
}

t_stat sim_ttcmd (void)
{
if (!isatty (fileno (stdin)))                           /* skip if !tty */
    return SCPE_OK;
if (!prior_norm) {                                      /* priority down? */
    errno =     0;
    nice (-10);                                         /* try to raise pri */
    prior_norm = (errno == 0);                          /* if no error, done */
    }
if (tcsetattr (0, TCSAFLUSH, &cmdtty) < 0)
    return SCPE_TTIERR;
return SCPE_OK;
}

t_stat sim_ttclose (void)
{
return sim_ttcmd ();
}

t_stat sim_os_poll_kbd (void)
{
int status;
unsigned char buf[1];

status = read (0, buf, 1);
if (status != 1) return SCPE_OK;
if (sim_brk_char && (buf[0] == sim_brk_char))
    return SCPE_BREAK;
else return (buf[0] | SCPE_KFLAG);
}

t_stat sim_os_putchar (int32 out)
{
char c;

c = out;
write (1, &c, 1);
return SCPE_OK;
}

#endif
