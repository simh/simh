/* sim_timer.c: simulator timer library

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

   02-Jan-04	RMS	Split out from SCP

   This library includes the following routines:

   sim_rtc_init	-	initialize calibration
   sim_rtc_calb	-	calibrate clock
   sim_os_msec	-	return elapsed time in msec
   sim_os_sleep	-	sleep specified number of seconds

   The calibration routines are OS-independent; the _os_ routines are not
*/

#include "sim_defs.h"

/* OS independent clock calibration package */

static int32 rtc_ticks[SIM_NTIMERS] = { 0 };		/* ticks */
static uint32 rtc_rtime[SIM_NTIMERS] = { 0 };		/* real time */
static uint32 rtc_vtime[SIM_NTIMERS] = { 0 };		/* virtual time */
static uint32 rtc_nxintv[SIM_NTIMERS] = { 0 };		/* next interval */
static int32 rtc_based[SIM_NTIMERS] = { 0 };		/* base delay */
static int32 rtc_currd[SIM_NTIMERS] = { 0 };		/* current delay */
static int32 rtc_initd[SIM_NTIMERS] = { 0 };		/* initial delay */
const t_bool rtc_avail;

int32 sim_rtcn_init (int32 time, int32 tmr)
{
if (time == 0) time = 1;
if ((tmr < 0) || (tmr >= SIM_NTIMERS)) return time;
rtc_rtime[tmr] = sim_os_msec ();
rtc_vtime[tmr] = rtc_rtime[tmr];
rtc_nxintv[tmr] = 1000;
rtc_ticks[tmr] = 0;
rtc_based[tmr] = time;
rtc_currd[tmr] = time;
rtc_initd[tmr] = time;
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
rtc_vtime[tmr] = rtc_vtime[tmr] + 1000;			/* adv sim time */
if (delta_rtime > 30000)				/* gap too big? */
	return rtc_initd[tmr];				/* can't calibr */
if (delta_rtime == 0)					/* gap too small? */
	rtc_based[tmr] = rtc_based[tmr] * ticksper;	/* slew wide */
else rtc_based[tmr] = (int32) (((double) rtc_based[tmr] * (double) rtc_nxintv[tmr]) /
	((double) delta_rtime));			/* new base rate */
delta_vtime = rtc_vtime[tmr] - rtc_rtime[tmr];		/* gap */
if (delta_vtime > SIM_TMAX) delta_vtime = SIM_TMAX;	/* limit gap */
else if (delta_vtime < -SIM_TMAX) delta_vtime = -SIM_TMAX;
rtc_nxintv[tmr] = 1000 + delta_vtime;			/* next wtime */
rtc_currd[tmr] = (int32) (((double) rtc_based[tmr] * (double) rtc_nxintv[tmr]) /
	1000.0);					/* next delay */
if (rtc_based[tmr] <= 0) rtc_based[tmr] = 1;		/* never negative or zero! */
if (rtc_currd[tmr] <= 0) rtc_currd[tmr] = 1;		/* never negative or zero! */
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

/* OS-dependent timer and clock routines */

/* VMS */

#if defined (VMS)

#if defined(__VAX)
#define sys$gettim SYS$GETTIM
#endif

#include <starlet.h>
#include <unistd.h>

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

/* Win32 routines */

#elif defined (_WIN32)

#include <windows.h>

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

/* OS/2 routines, from Bruce Ray */

#elif defined (__OS2__)

const t_bool rtc_avail = FALSE;

uint32 sim_os_msec ()
{
return 0;
}

void sim_os_sleep (unsigned int sec)
{
return;
}

/* Metrowerks CodeWarrior Macintosh routines, from Ben Supnik */

#elif defined (__MWERKS__) && defined (macintosh)

#include <Timer.h>
#include <Mactypes.h>
#include <sioux.h>
#include <unistd.h>
#include <siouxglobals.h>

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

#else

/* UNIX routines */

#include <sys/time.h>
#include <unistd.h>

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
