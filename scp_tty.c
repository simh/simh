/* scp_tty.c: operating system-dependent I/O routines

   Copyright (c) 1993-2003, Robert M Supnik

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

   ttinit	-	called once to get initial terminal state
   ttrunstate	-	called to put terminal into run state
   ttcmdstate	-	called to return terminal to command state
   ttclose	-	called once before the simulator exits
   sim_os_poll_kbd -	poll for keyboard input
   sim_os_putchar -	output character to terminal

   This module implements the following routines to support clock calibration:

   sim_os_msec	-	return elapsed time in msec
   sim_os_sleep	-	sleep specified number of seconds

   Versions are included for VMS, Windows, OS/2, Macintosh, BSD UNIX, and POSIX UNIX.
   The POSIX UNIX version works with LINUX.
*/

#include "sim_defs.h"
int32 sim_int_char = 005;				/* interrupt character */
extern FILE *sim_log;

/* VMS routines, from Ben Thomas, with fixes from Robert Alan Byer */

#if defined (VMS)
#define _SIM_IO_TTY_ 0
#if defined(__VAX)
#define sys$assign SYS$ASSIGN
#define sys$qiow SYS$QIOW
#define sys$gettim SYS$GETTIM
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

t_stat ttinit (void)
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

t_stat ttrunstate (void)
{
unsigned int status;
IOSB iosb;

status = sys$qiow (EFN, tty_chan, IO$_SETMODE, &iosb, 0, 0,
	&run_mode, sizeof (run_mode), 0, 0, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL)) return SCPE_TTIERR;
return SCPE_OK;
}

t_stat ttcmdstate (void)
{
unsigned int status;
IOSB iosb;

status = sys$qiow (EFN, tty_chan, IO$_SETMODE, &iosb, 0, 0,
	&cmd_mode, sizeof (cmd_mode), 0, 0, 0, 0);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL)) return SCPE_TTIERR;
return SCPE_OK;
}

t_stat ttclose (void)
{
return ttcmdstate ();
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
if (sim_log) fputc (c, sim_log);
if ((status != SS$_NORMAL) || (iosb.status != SS$_NORMAL)) return SCPE_TTOERR;
return SCPE_OK;
}

const t_bool rtc_avail = TRUE;

uint32 sim_os_msec ()
{
uint32 quo, htod, tod[2];
int32 i;

sys$gettim (tod);					/* time 0.1usec */

/* To convert to msec, must divide a 64b quantity by 10000.  This is actually done
   by dividing the 96b quantity 0'time by 10000, producing 64b of quotient, the
   high 32b of which are discarded.  This can probably be done by a clever multiply...
*/

quo = htod = 0;
for (i = 0; i < 64; i++) {				/* 64b quo */
	htod = (htod << 1) | ((tod[1] >> 31) & 1);	/* shift divd */
	tod[1] = (tod[1] << 1) | ((tod[0] >> 31) & 1);
	tod[0] = tod[0] << 1;
	quo = quo << 1;					/* shift quo */
	if (htod >= 10000) {				/* divd work? */
	    htod = htod - 10000;			/* subtract */
	    quo = quo | 1;  }  }			/* set quo bit */
return quo;
}

void sim_os_sleep (unsigned int sec)
{
sleep (sec);
return;
}

#endif

/* Win32 routines */

#if defined (_WIN32)
#define _SIM_IO_TTY_ 0
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

t_stat ttinit (void)
{
return SCPE_OK;
}

t_stat ttrunstate (void)
{
if (signal (SIGINT, win_handler) == SIG_ERR) return SCPE_SIGERR;
SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
return SCPE_OK;
}

t_stat ttcmdstate (void)
{
SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_NORMAL);
return SCPE_OK;
}

t_stat ttclose (void)
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
if (!kbhit ()) return SCPE_OK;
c = _getch ();
if ((c & 0177) == '\b') c = 0177;
if ((c & 0177) == sim_int_char) return SCPE_STOP;
return c | SCPE_KFLAG;
}

t_stat sim_os_putchar (int32 c)
{
if (c != 0177) {
	_putch (c);
	if (sim_log) fputc (c, sim_log);  }
return SCPE_OK;
}

const t_bool rtc_avail = TRUE;

uint32 sim_os_msec ()
{
return GetTickCount ();
}

void sim_os_sleep (unsigned int sec)
{
Sleep (sec * 1000);
return;
}

#endif

/* OS/2 routines, from Bruce Ray */

#if defined (__OS2__)
#define _SIM_IO_TTY_ 0
#include <conio.h>

t_stat ttinit (void)
{
return SCPE_OK;
}

t_stat ttrunstate (void)
{
return SCPE_OK;
}

t_stat ttcmdstate (void)
{
return SCPE_OK;
}

t_stat ttclose (void)
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
	fflush (stdout) ;
	if (sim_log) fputc (c, sim_log);  }
return SCPE_OK;
}

const t_bool rtc_avail = FALSE;

uint32 sim_os_msec ()
{
return 0;
}

#endif

/* Metrowerks CodeWarrior Macintosh routines, from Louis Chretien,
   Peter Schorn, and Ben Supnik
*/

#if defined (__MWERKS__) && defined (macintosh)
#define _SIM_IO_TTY_ 0

#include <Timer.h>
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
t_stat ttinit (void);
t_stat ttrunstate (void);
t_stat ttcmdstate (void);
t_stat ttclose (void);
uint32 sim_os_msec (void);
t_stat sim_os_poll_kbd (void);
t_stat sim_os_putchar (int32 c);

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

t_stat ttinit (void) {
/* Note that this only works if the call to ttinit comes before any output to the console */
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

t_stat ttrunstate (void)
{
return SCPE_OK;
}

t_stat ttcmdstate (void)
{
return SCPE_OK;
}

t_stat ttclose (void)
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
	fflush (stdout) ;
	if (sim_log) fputc (c, sim_log);  }
return SCPE_OK;
}

const t_bool rtc_avail = TRUE;

uint32 sim_os_msec (void)
{
unsigned long long micros;
UnsignedWide macMicros;
unsigned long millis;

Microseconds (&macMicros);
micros = *((unsigned long long *) &macMicros);
millis = micros / 1000LL;
return (uint32) millis;
}

void sim_os_sleep (unsigned int sec)
{
sleep (sec);
return;
}

#endif

/* BSD UNIX routines */

#if defined (BSDTTY)
#define _SIM_IO_TTY_ 0
#include <sgtty.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

struct sgttyb cmdtty,runtty;			/* V6/V7 stty data */
struct tchars cmdtchars,runtchars;		/* V7 editing */
struct ltchars cmdltchars,runltchars;		/* 4.2 BSD editing */
int cmdfl,runfl;				/* TTY flags */

t_stat ttinit (void)
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

t_stat ttrunstate (void)
{
runtchars.t_intrc = sim_int_char;		/* in case changed */
fcntl (0, F_SETFL, runfl);			/* non-block mode */
if (ioctl (0, TIOCSETP, &runtty) < 0) return SCPE_TTIERR;
if (ioctl (0, TIOCSETC, &runtchars) < 0) return SCPE_TTIERR;
if (ioctl (0, TIOCSLTC, &runltchars) < 0) return SCPE_TTIERR;
nice (10);					/* lower priority */
return SCPE_OK;
}

t_stat ttcmdstate (void)
{
nice (-10);					/* restore priority */
fcntl (0, F_SETFL, cmdfl);			/* block mode */
if (ioctl (0, TIOCSETP, &cmdtty) < 0) return SCPE_TTIERR;
if (ioctl (0, TIOCSETC, &cmdtchars) < 0) return SCPE_TTIERR;
if (ioctl (0, TIOCSLTC, &cmdltchars) < 0) return SCPE_TTIERR;
return SCPE_OK;
}

t_stat ttclose (void)
{
return ttcmdstate ();
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
if (sim_log) fputc (c, sim_log);
return SCPE_OK;
}

const t_bool rtc_avail = TRUE;

uint32 sim_os_msec ()
{
struct timeval cur;
struct timezone foo;
uint32 msec;

gettimeofday (&cur, &foo);
msec = (((uint32) cur.tv_sec) * 1000) + (((uint32) cur.tv_usec) / 1000);
return msec;
}

void sim_os_sleep (unsigned int sec)
{
sleep (sec);
return;
}

#endif

/* POSIX UNIX routines, from Leendert Van Doorn */

#if !defined (_SIM_IO_TTY_)
#include <termios.h>
#include <sys/time.h>
#include <unistd.h>

struct termios cmdtty, runtty;
static int prior_norm = 1;

t_stat ttinit (void)
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

t_stat ttrunstate (void)
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

t_stat ttcmdstate (void)
{
if (!isatty (fileno (stdin))) return SCPE_OK;		/* skip if !tty */
if (!prior_norm) {					/* priority down? */
	errno =	0;
	nice (-10);					/* try to raise pri*/
	prior_norm = (errno == 0); }			/* if no error, done */
if (tcsetattr (0, TCSAFLUSH, &cmdtty) < 0) return SCPE_TTIERR;
return SCPE_OK;
}

t_stat ttclose (void)
{
return ttcmdstate ();
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
if (sim_log) fputc (c, sim_log);
return SCPE_OK;
}

const t_bool rtc_avail = TRUE;

uint32 sim_os_msec ()
{
struct timeval cur;
uint32 msec;

gettimeofday (&cur, NULL);
msec = (((uint32) cur.tv_sec) * 1000) + (((uint32) cur.tv_usec) / 1000);
return msec;
}

void sim_os_sleep (unsigned int sec)
{
sleep (sec);
return;
}

#endif

/* Long seek routines */

#if defined (USE_INT64) && defined (USE_ADDR64)

/* Alpha VMS */

#if defined (__ALPHA) && defined (VMS)			/* Alpha VMS */
#define _SIM_IO_FSEEK_EXT_	0

static t_int64 fpos_t_to_int64 (fpos_t *pos)
{
unsigned short *w = (unsigned short *) pos;		/* endian dep! */
t_int64 result;

result = w[1];
result <<= 16;
result += w[0];
result <<= 9;
result += w[2];
return result;
}

static void int64_to_fpos_t (t_int64 ipos, fpos_t *pos, size_t mbc)
{
unsigned short *w = (unsigned short *) pos;
int bufsize = mbc << 9;

w[3] = 0;
w[2] = (unsigned short) (ipos % bufsize);
ipos -= w[2];
ipos >>= 9;
w[0] = (unsigned short) ipos;
ipos >>= 16;
w[1] = (unsigned short) ipos;
if ((w[2] == 0) && (w[0] || w[1])) {
	w[2] = bufsize;
	w[0] -= mbc;  }
}

int fseek_ext (FILE *st, t_addr offset, int whence)
{
t_addr fileaddr;
fpos_t filepos;

switch (whence) {
	case SEEK_SET:
	    fileaddr = offset;
	    break;
	case SEEK_CUR:
	    if (fgetpos (st, &filepos)) return (-1);
	    fileaddr = fpos_t_to_int64 (&filepos);
	    fileaddr = fileaddr + offset;
	    break;
	default:
	    errno = EINVAL;
	    return (-1);  }
int64_to_fpos_t (fileaddr, &filepos, 127);
return fsetpos (st, &filepos);
}

#endif

/* Alpha UNIX - natively 64b */

#if defined (__ALPHA) && defined (__unix__)		/* Alpha UNIX */
#define _SIM_IO_FSEEK_EXT_	0

int fseek_ext (FILE *st, t_addr offset, int whence)
{
return fseek (st, offset, whence);
}

#endif

/* Windows */

#if defined (_WIN32)
#define _SIM_IO_FSEEK_EXT_	0

int fseek_ext (FILE *st, t_addr offset, int whence)
{
fpos_t fileaddr;

switch (whence) {
	case SEEK_SET:
	    fileaddr = offset;
	    break;
	case SEEK_CUR:
	    if (fgetpos (st, &fileaddr)) return (-1);
	    fileaddr = fileaddr + offset;
	    break;
	default:
	    errno = EINVAL;
	    return (-1);  }
return fsetpos (st, &fileaddr);
}

#endif							/* end Windows */

#endif							/* end 64b seek defs */

/* Default: no OS-specific routine has been defined */

#if !defined (_SIM_IO_FSEEK_EXT_)
#define _SIM_IO_FSEEK_EXT_	0

int fseek_ext (FILE *st, t_addr xpos, int origin)
{
return fseek (st, (int32) xpos, origin);
}

uint32 sim_taddr_64 = 0;
#else
uint32 sim_taddr_64 = 1;
#endif
