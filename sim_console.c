/* sim_console.c: simulator console I/O library

   Copyright (c) 1993-2004, Robert M Supnik

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

   02-Jan-04	RMS	Removed timer routines, added Telnet console routines
		RMS	Moved console logging to OS-independent code
   25-Apr-03	RMS	Added long seek support from Mark Pizzolato
			Added Unix priority control from Mark Pizzolato
   24-Sep-02	RMS	Removed VT support, added Telnet console support
			Added CGI support (from Brian Knittel)
			Added MacOS sleep (from Peter Schorn)
   14-Jul-02	RMS	Added Windows priority control from Mark Pizzolato
   20-May-02	RMS	Added Windows VT support from Fischer Franz
   01-Feb-02	RMS	Added VAX fix from Robert Alan Byer
   19-Sep-01	RMS	More Mac changes
   31-Aug-01	RMS	Changed int64 to t_int64 for Windoze
   20-Jul-01	RMS	Added Macintosh support (from Louis Chretien, Peter Schorn,
				and Ben Supnik)
   15-May-01	RMS	Added logging support
   05-Mar-01	RMS	Added clock calibration support
   08-Dec-00	BKR	Added OS/2 support (from Bruce Ray)
   18-Aug-98	RMS	Added BeOS support
   13-Oct-97	RMS	Added NetBSD terminal support
   25-Jan-97	RMS	Added POSIX terminal I/O support
   02-Jan-97	RMS	Fixed bug in sim_poll_kbd

   This module implements the following routines to support terminal I/O:

   sim_poll_kbd	-	poll for keyboard input
   sim_putchar	-	output character to console
   sim_putchar_s -	output character to console, stall if congested
   sim_set_telnet -	set console to Telnet port
   sim_set_notelnet -	close console Telnet port
   sim_show_telnet -	show console status

   sim_ttinit	-	called once to get initial terminal state
   sim_ttrun	-	called to put terminal into run state
   sim_ttcmd	-	called to return terminal to command state
   sim_ttclose	-	called once before the simulator exits
   sim_os_poll_kbd -	poll for keyboard input
   sim_os_putchar -	output character to console

   The first group is OS-independent; the second group is OS-dependent.
*/

#include "sim_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

int32 sim_int_char = 005;				/* interrupt character */
TMLN sim_con_ldsc = { 0 };				/* console line descr */
TMXR sim_con_tmxr = { 1, 0, 0, &sim_con_ldsc };		/* console line mux */

extern volatile int32 stop_cpu;
extern FILE *sim_log;

/* Console I/O package.

   The console terminal can be attached to the controlling window
   or to a Telnet connection.  If attached to a Telnet connection,
   the console is described by internal terminal multiplexor
   sim_con_tmxr and internal terminal line description sim_con_ldsc.
*/

/* Set console to Telnet port */

t_stat sim_set_telnet (int32 flg, char *cptr)
{
if (*cptr == 0) return SCPE_2FARG;			/* too few arguments? */
if (sim_con_tmxr.master) return SCPE_ALATT;		/* already open? */
return tmxr_open_master (&sim_con_tmxr, cptr);		/* open master socket */
}

/* Close console Telnet port */

t_stat sim_set_notelnet (int32 flag, char *cptr)
{
if (cptr && (*cptr != 0)) return SCPE_2MARG;		/* too many arguments? */
if (sim_con_tmxr.master == 0) return SCPE_OK;		/* ignore if already closed */
return tmxr_close_master (&sim_con_tmxr);		/* close master socket */
}

/* Show console Telnet status */

t_stat sim_show_telnet (FILE *st, DEVICE *dunused, UNIT *uunused, int32 flag, char *cptr)
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
	    if (i) {					/* if delayed */
		printf ("Running\n");			/* print transition */
		fflush (stdout);  }
	    return SCPE_OK;  }				/* ready to proceed */
	c = sim_os_poll_kbd ();				/* check for stop char */
	if ((c == SCPE_STOP) || stop_cpu) return SCPE_STOP;
	if ((i % 10) == 0) {				/* Status every 10 sec */
	    printf ("Waiting for console Telnet connection\n");
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
	return (c & (SCPE_BREAK | 0377)) | SCPE_KFLAG;
return SCPE_OK;
}

/* Output character */

t_stat sim_putchar (int32 c)
{
if (sim_log) fputc (c, sim_log);			/* log file? */
if (sim_con_tmxr.master == 0)				/* not Telnet? */
	return sim_os_putchar (c);			/* in-window version */
if (sim_con_ldsc.conn == 0) return SCPE_LOST;		/* no Telnet conn? */
tmxr_putc_ln (&sim_con_ldsc, c);			/* output char */
tmxr_poll_tx (&sim_con_tmxr);				/* poll xmt */
return SCPE_OK;
}

t_stat sim_putchar_s (int32 c)
{
t_stat r;

if (sim_log) fputc (c, sim_log);			/* log file? */
if (sim_con_tmxr.master == 0)				/* not Telnet? */
	return sim_os_putchar (c);			/* in-window version */
if (sim_con_ldsc.conn == 0) return SCPE_LOST;		/* no Telnet conn? */
if (sim_con_ldsc.xmte == 0) r = SCPE_STALL;		/* xmt disabled? */
else r = tmxr_putc_ln (&sim_con_ldsc, c);		/* no, Telnet output */
tmxr_poll_tx (&sim_con_tmxr);				/* poll xmt */
return r;						/* return status */
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
unsigned int32 tty_chan = 0;

typedef struct {
	unsigned short sense_count;
	unsigned char sense_first_char;
	unsigned char sense_reserved;
	unsigned int32 stat;
	unsigned int32 stat2; } SENSE_BUF;

typedef struct {
	unsigned int16 status;
	unsigned int16 count;
	unsigned int32 dev_status; } IOSB;

SENSE_BUF cmd_mode = { 0 };
SENSE_BUF run_mode = { 0 };

t_stat sim_ttinit (void)
{
unsigned int32 status;
IOSB iosb;
$DESCRIPTOR (terminal_device, "tt");

status = sys$assign (&terminal_device, &tty_chan, 0, 0);
if (status != SS$_NORMAL) return SCPE_TTIERR;
status = sys$qiow (EFN, tty_chan, IO$_SENSEMODE, &iosb, 0, 0,
	&cmd_mode, sizeof (cmd_mode), 0, 0, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL)) return SCPE_TTIERR;
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
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL)) return SCPE_TTIERR;
return SCPE_OK;
}

t_stat sim_ttcmd (void)
{
unsigned int status;
IOSB iosb;

status = sys$qiow (EFN, tty_chan, IO$_SETMODE, &iosb, 0, 0,
	&cmd_mode, sizeof (cmd_mode), 0, 0, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL)) return SCPE_TTIERR;
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
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL)) return SCPE_TTIERR;
if (sense.sense_count == 0) return SCPE_OK;
term[0] = 0; term[1] = 0;
status = sys$qiow (EFN, tty_chan,
	IO$_READLBLK | IO$M_NOECHO | IO$M_NOFILTR | IO$M_TIMED | IO$M_TRMNOECHO,
	&iosb, 0, 0, buf, 1, 0, term, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL)) return SCPE_OK;
if (buf[0] == sim_int_char) return SCPE_STOP;
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
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL)) return SCPE_TTOERR;
return SCPE_OK;
}

/* Win32 routines */

#elif defined (_WIN32)

#include <conio.h>
#include <io.h>
#include <windows.h>
#include <signal.h>
static volatile int sim_win_ctlc = 0;

void win_handler (int sig)
{
sim_win_ctlc = 1;
return;
}

t_stat sim_ttinit (void)
{
return SCPE_OK;
}

t_stat sim_ttrun (void)
{
if (signal (SIGINT, win_handler) == SIG_ERR) return SCPE_SIGERR;
SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
return SCPE_OK;
}

t_stat sim_ttcmd (void)
{
SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_NORMAL);
return SCPE_OK;
}

t_stat sim_ttclose (void)
{
return SCPE_OK;
}

t_stat sim_os_poll_kbd (void)
{
int c;

if (sim_win_ctlc) {
	sim_win_ctlc = 0;
	signal (SIGINT, win_handler);
	return 003 | SCPE_KFLAG;  }
if (!_kbhit ()) return SCPE_OK;
c = _getch ();
if ((c & 0177) == '\b') c = 0177;
if ((c & 0177) == sim_int_char) return SCPE_STOP;
return c | SCPE_KFLAG;
}

t_stat sim_os_putchar (int32 c)
{
if (c != 0177) _putch (c);
return SCPE_OK;
}

/* OS/2 routines, from Bruce Ray */

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

if (!kbhit ()) return SCPE_OK;
c = getch();
if ((c & 0177) == '\b') c = 0177;
if ((c & 0177) == sim_int_char) return SCPE_STOP;
return c | SCPE_KFLAG;
}

t_stat sim_os_putchar (int32 c)
{
if (c != 0177) {
	putch (c);
	fflush (stdout);  }
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
static CursHandle iBeamCursorH = NULL;			/* contains the iBeamCursor */

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

t_stat sim_ttinit (void) {
/* Note that this only works if the call to sim_ttinit comes before any output to the console */
	int i;
	char title[50] = " "; /* this blank will later be replaced by the number of characters */
	unsigned char ptitle[50];
	SIOUXSettings.autocloseonquit	= TRUE;
	SIOUXSettings.asktosaveonclose = FALSE;
	SIOUXSettings.showstatusline = FALSE;
	SIOUXSettings.columns = 80;
	SIOUXSettings.rows = 40;
	SIOUXSettings.toppixel = 42;
	SIOUXSettings.leftpixel	= 6;
	iBeamCursorH = GetCursor(iBeamCursor);
	strcat(title, sim_name);
	strcat(title, " Simulator");
	title[0] = strlen(title) - 1;			/* Pascal string done */
	for (i = 0; i <= title[0]; i++) {		/* copy to unsigned char */
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

if (!ps_kbhit ()) return SCPE_OK;
c = ps_getch();
if ((c & 0177) == '\b') c = 0177;
if ((c & 0177) == sim_int_char) return SCPE_STOP;
return c | SCPE_KFLAG;
}

t_stat sim_os_putchar (int32 c)
{
if (c != 0177) {
	putchar (c);
	fflush (stdout);  }
return SCPE_OK;
}

/* BSD UNIX routines */

#elif defined (BSDTTY)

#include <sgtty.h>
#include <fcntl.h>
#include <unistd.h>

struct sgttyb cmdtty,runtty;			/* V6/V7 stty data */
struct tchars cmdtchars,runtchars;		/* V7 editing */
struct ltchars cmdltchars,runltchars;		/* 4.2 BSD editing */
int cmdfl,runfl;				/* TTY flags */

t_stat sim_ttinit (void)
{
cmdfl = fcntl (0, F_GETFL, 0);			/* get old flags  and status */
runfl = cmdfl | FNDELAY;
if (ioctl (0, TIOCGETP, &cmdtty) < 0) return SCPE_TTIERR;
if (ioctl (0, TIOCGETC, &cmdtchars) < 0) return SCPE_TTIERR;
if (ioctl (0, TIOCGLTC, &cmdltchars) < 0) return SCPE_TTIERR;
runtty = cmdtty;				/* initial run state */
runtty.sg_flags = cmdtty.sg_flags & ~(ECHO|CRMOD) | CBREAK;
runtchars.t_intrc = sim_int_char;		/* interrupt */
runtchars.t_quitc = 0xFF;			/* no quit */
runtchars.t_startc = 0xFF;			/* no host sync */
runtchars.t_stopc = 0xFF;
runtchars.t_eofc = 0xFF;
runtchars.t_brkc = 0xFF;
runltchars.t_suspc = 0xFF;			/* no specials of any kind */
runltchars.t_dsuspc = 0xFF;
runltchars.t_rprntc = 0xFF;
runltchars.t_flushc = 0xFF;
runltchars.t_werasc = 0xFF;
runltchars.t_lnextc = 0xFF;
return SCPE_OK;					/* return success */
}

t_stat sim_ttrun (void)
{
runtchars.t_intrc = sim_int_char;		/* in case changed */
fcntl (0, F_SETFL, runfl);			/* non-block mode */
if (ioctl (0, TIOCSETP, &runtty) < 0) return SCPE_TTIERR;
if (ioctl (0, TIOCSETC, &runtchars) < 0) return SCPE_TTIERR;
if (ioctl (0, TIOCSLTC, &runltchars) < 0) return SCPE_TTIERR;
nice (10);					/* lower priority */
return SCPE_OK;
}

t_stat sim_ttcmd (void)
{
nice (-10);					/* restore priority */
fcntl (0, F_SETFL, cmdfl);			/* block mode */
if (ioctl (0, TIOCSETP, &cmdtty) < 0) return SCPE_TTIERR;
if (ioctl (0, TIOCSETC, &cmdtchars) < 0) return SCPE_TTIERR;
if (ioctl (0, TIOCSLTC, &cmdltchars) < 0) return SCPE_TTIERR;
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
if (!isatty (fileno (stdin))) return SCPE_OK;		/* skip if !tty */
if (tcgetattr (0, &cmdtty) < 0) return SCPE_TTIERR;	/* get old flags */
runtty = cmdtty;
runtty.c_lflag = runtty.c_lflag & ~(ECHO | ICANON);	/* no echo or edit */
runtty.c_oflag = runtty.c_oflag & ~OPOST;		/* no output edit */
runtty.c_iflag = runtty.c_iflag & ~ICRNL;		/* no cr conversion */
runtty.c_cc[VINTR] = sim_int_char;			/* interrupt */
runtty.c_cc[VQUIT] = 0;					/* no quit */
runtty.c_cc[VERASE] = 0;
runtty.c_cc[VKILL] = 0;
runtty.c_cc[VEOF] = 0;
runtty.c_cc[VEOL] = 0;
runtty.c_cc[VSTART] = 0;				/* no host sync */
runtty.c_cc[VSUSP] = 0;
runtty.c_cc[VSTOP] = 0;
#if defined (VREPRINT)
runtty.c_cc[VREPRINT] = 0;				/* no specials */
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
runtty.c_cc[VMIN] = 0;					/* no waiting */
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
if (!isatty (fileno (stdin))) return SCPE_OK;		/* skip if !tty */
runtty.c_cc[VINTR] = sim_int_char;			/* in case changed */
if (tcsetattr (0, TCSAFLUSH, &runtty) < 0) return SCPE_TTIERR;
if (prior_norm) {					/* at normal pri? */
	errno =	0;
	nice (10);					/* try to lower pri */
	prior_norm = errno; }				/* if no error, done */
return SCPE_OK;
}

t_stat sim_ttcmd (void)
{
if (!isatty (fileno (stdin))) return SCPE_OK;		/* skip if !tty */
if (!prior_norm) {					/* priority down? */
	errno =	0;
	nice (-10);					/* try to raise pri*/
	prior_norm = (errno == 0); }			/* if no error, done */
if (tcsetattr (0, TCSAFLUSH, &cmdtty) < 0) return SCPE_TTIERR;
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
