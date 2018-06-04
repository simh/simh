/* sim_timer.c: simulator timer library

   Copyright (c) 1993-2017, Robert M Supnik

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

   22-May-17    RMS     Hacked for V4.0 CONST compatibility
   23-Nov-15    RMS     Fixed calibration lost path to reinitialize timer
   28-Mar-15    RMS     Revised to use sim_printf
   29-Dec-10    MP      Fixed clock resolution determination for Unix platforms
   22-Sep-08    RMS     Added "stability threshold" for idle routine
   27-May-08    RMS     Fixed bug in Linux idle routines (from Walter Mueller)
   18-Jun-07    RMS     Modified idle to exclude counted delays
   22-Mar-07    RMS     Added sim_rtcn_init_all
   17-Oct-06    RMS     Added idle support (based on work by Mark Pizzolato)
                        Added throttle support
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   02-Jan-04    RMS     Split out from SCP

   This library includes the following routines:

   sim_timer_init       initialize timing system
   sim_rtc_init         initialize calibration
   sim_rtc_calb         calibrate clock
   sim_timer_init       initialize timing system
   sim_activate_after   activate for specified number of microseconds
   sim_idle             virtual machine idle
   sim_os_msec          return elapsed time in msec
   sim_os_sleep         sleep specified number of seconds
   sim_os_ms_sleep      sleep specified number of milliseconds

   The calibration, idle, and throttle routines are OS-independent; the _os_
   routines are not.

   The timer library assumes that timer[0] is the master system timer.
*/

#include "sim_defs.h"
#include <ctype.h>

t_bool sim_idle_enab = FALSE;                           /* global flag */

static uint32 sim_idle_rate_ms = 0;
static uint32 sim_idle_stable = SIM_IDLE_STDFLT;
static uint32 sim_throt_ms_start = 0;
static uint32 sim_throt_ms_stop = 0;
static uint32 sim_throt_type = 0;
static uint32 sim_throt_val = 0;
static uint32 sim_throt_state = 0;
static int32 sim_throt_wait = 0;
static UNIT *sim_clock_unit = NULL;
extern UNIT *sim_clock_queue;

t_stat sim_throt_svc (UNIT *uptr);

UNIT sim_throt_unit = { UDATA (&sim_throt_svc, 0, 0) };

/* OS-dependent timer and clock routines */

/* VMS */

#if defined (VMS)

#if defined (__VAX)
#define sys$gettim SYS$GETTIM
#define sys$setimr SYS$SETIMR
#define lib$emul LIB$EMUL
#define sys$waitfr SYS$WAITFR
#define lib$subx LIB$SUBX
#define lib$ediv LIB$EDIV
#endif

#include <starlet.h>
#include <lib$routines.h>
#include <unistd.h>

const t_bool rtc_avail = TRUE;

uint32 sim_os_msec ()
{
uint32 quo, htod, tod[2];
int32 i;

sys$gettim (tod);                                       /* time 0.1usec */

/* To convert to msec, must divide a 64b quantity by 10000.  This is actually done
   by dividing the 96b quantity 0'time by 10000, producing 64b of quotient, the
   high 32b of which are discarded.  This can probably be done by a clever multiply...
*/

quo = htod = 0;
for (i = 0; i < 64; i++) {                              /* 64b quo */
    htod = (htod << 1) | ((tod[1] >> 31) & 1);          /* shift divd */
    tod[1] = (tod[1] << 1) | ((tod[0] >> 31) & 1);
    tod[0] = tod[0] << 1;
    quo = quo << 1;                                     /* shift quo */
    if (htod >= 10000) {                                /* divd work? */
        htod = htod - 10000;                            /* subtract */
        quo = quo | 1;                                  /* set quo bit */
        }
    }
return quo;
}

void sim_os_sleep (unsigned int sec)
{
sleep (sec);
return;
}

uint32 sim_os_ms_sleep_init (void)
{
#if defined (__VAX)
return 10;                                              /* VAX/VMS is 10ms */
#else
return 1;                                               /* Alpha/VMS is 1ms */
#endif
}

uint32 sim_os_ms_sleep (unsigned int msec)
{
uint32 stime = sim_os_msec ();
uint32 qtime[2];
int32 nsfactor = -10000;
static int32 zero = 0;

lib$emul (&msec, &nsfactor, &zero, qtime);
sys$setimr (2, qtime, 0, 0);
sys$waitfr (2);
return sim_os_msec () - stime;
}

/* Win32 routines */

#elif defined (_WIN32)

#include <windows.h>

const t_bool rtc_avail = TRUE;

uint32 sim_os_msec ()
{
if (sim_idle_rate_ms)
    return timeGetTime ();
else return GetTickCount ();
}

void sim_os_sleep (unsigned int sec)
{
Sleep (sec * 1000);
return;
}

void sim_timer_exit (void)
{
timeEndPeriod (sim_idle_rate_ms);
return;
}

uint32 sim_os_ms_sleep_init (void)
{
TIMECAPS timers;

if (timeGetDevCaps (&timers, sizeof (timers)) != TIMERR_NOERROR)
    return 0;
if ((timers.wPeriodMin == 0) || (timers.wPeriodMin > SIM_IDLE_MAX))
    return 0;
if (timeBeginPeriod (timers.wPeriodMin) != TIMERR_NOERROR)
    return 0;
atexit (sim_timer_exit);
Sleep (1);
Sleep (1);
Sleep (1);
Sleep (1);
Sleep (1);
return timers.wPeriodMin;                               /* sim_idle_rate_ms */
}

uint32 sim_os_ms_sleep (unsigned int msec)
{
uint32 stime = sim_os_msec();

Sleep (msec);
return sim_os_msec () - stime;
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

uint32 sim_os_ms_sleep_init (void)
{
return FALSE;
}

uint32 sim_os_ms_sleep (unsigned int msec)
{
return 0;
}

/* Metrowerks CodeWarrior Macintosh routines, from Ben Supnik */

#elif defined (__MWERKS__) && defined (macintosh)

#include <Timer.h>
#include <Mactypes.h>
#include <sioux.h>
#include <unistd.h>
#include <siouxglobals.h>
#define NANOS_PER_MILLI     1000000
#define MILLIS_PER_SEC      1000

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

uint32 sim_os_ms_sleep_init (void)
{
return 1;
}

uint32 sim_os_ms_sleep (unsigned int milliseconds)
{
uint32 stime = sim_os_msec ();
struct timespec treq;

treq.tv_sec = milliseconds / MILLIS_PER_SEC;
treq.tv_nsec = (milliseconds % MILLIS_PER_SEC) * NANOS_PER_MILLI;
(void) nanosleep (&treq, NULL);
return sim_os_msec () - stime;
}

#else

/* UNIX routines */

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#define NANOS_PER_MILLI     1000000
#define MILLIS_PER_SEC      1000
#define sleep1Samples       100

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

uint32 sim_os_ms_sleep_init (void)
{
uint32 i, t1, t2, tot, tim;

for (i = 0, tot = 0; i < sleep1Samples; i++) {
    t1 = sim_os_msec ();
    sim_os_ms_sleep (1);
    t2 = sim_os_msec ();
    tot += (t2 - t1);
    }
tim = (tot + (sleep1Samples - 1)) / sleep1Samples;
if (tim > SIM_IDLE_MAX)
    tim = 0;
return tim;
}

uint32 sim_os_ms_sleep (unsigned int milliseconds)
{
uint32 stime = sim_os_msec ();
struct timespec treq;

treq.tv_sec = milliseconds / MILLIS_PER_SEC;
treq.tv_nsec = (milliseconds % MILLIS_PER_SEC) * NANOS_PER_MILLI;
(void) nanosleep (&treq, NULL);
return sim_os_msec () - stime;
}

#endif

/* OS independent clock calibration package */

static int32 rtc_ticks[SIM_NTIMERS] = { 0 };            /* ticks */
static int32 rtc_hz[SIM_NTIMERS] = { 0 };               /* tick rate */
static uint32 rtc_rtime[SIM_NTIMERS] = { 0 };           /* real time */
static uint32 rtc_vtime[SIM_NTIMERS] = { 0 };           /* virtual time */
static double rtc_gtime[SIM_NTIMERS] = { 0 };           /* instruction time */
static uint32 rtc_nxintv[SIM_NTIMERS] = { 0 };          /* next interval */
static int32 rtc_based[SIM_NTIMERS] = { 0 };            /* base delay */
static int32 rtc_currd[SIM_NTIMERS] = { 0 };            /* current delay */
static int32 rtc_initd[SIM_NTIMERS] = { 0 };            /* initial delay */
static uint32 rtc_elapsed[SIM_NTIMERS] = { 0 };         /* sec since init */
static uint32 rtc_calibrations[SIM_NTIMERS] = { 0 };    /* calibration count */

void sim_rtcn_init_all (void)
{
uint32 i;

for (i = 0; i < SIM_NTIMERS; i++) {
    if (rtc_initd[i] != 0)
        sim_rtcn_init (rtc_initd[i], i);
    }
return;
}

int32 sim_rtcn_init (int32 time, int32 tmr)
{
if (time == 0)
    time = 1;
if ((tmr < 0) || (tmr >= SIM_NTIMERS))
    return time;
rtc_rtime[tmr] = sim_os_msec ();
rtc_vtime[tmr] = rtc_rtime[tmr];
rtc_gtime[tmr] = sim_gtime ();
rtc_nxintv[tmr] = 1000;
rtc_ticks[tmr] = 0;
rtc_hz[tmr] = 0;
rtc_based[tmr] = time;
rtc_currd[tmr] = time;
rtc_initd[tmr] = time;
rtc_elapsed[tmr] = 0;
rtc_calibrations[tmr] = 0;
return time;
}

int32 sim_rtcn_calb (int32 ticksper, int32 tmr)
{
uint32 new_rtime, delta_rtime;
int32 delta_vtime;

if ((tmr < 0) || (tmr >= SIM_NTIMERS))
    return 10000;
rtc_hz[tmr] = ticksper;
rtc_ticks[tmr] = rtc_ticks[tmr] + 1;                    /* count ticks */
if (rtc_ticks[tmr] < ticksper)                          /* 1 sec yet? */
    return rtc_currd[tmr];
rtc_ticks[tmr] = 0;                                     /* reset ticks */
rtc_elapsed[tmr] = rtc_elapsed[tmr] + 1;                /* count sec */
if (!rtc_avail)                                         /* no timer? */
    return rtc_currd[tmr];
new_rtime = sim_os_msec ();                             /* wall time */
if (new_rtime < rtc_rtime[tmr]) {                       /* time running backwards? */
    rtc_rtime[tmr] = new_rtime;                         /* reset wall time */
    return rtc_currd[tmr];                              /* can't calibrate */
    }
++rtc_calibrations[tmr];                                /* count calibrations */
delta_rtime = new_rtime - rtc_rtime[tmr];               /* elapsed wtime */
rtc_rtime[tmr] = new_rtime;                             /* adv wall time */
rtc_vtime[tmr] = rtc_vtime[tmr] + 1000;                 /* adv sim time */
rtc_gtime[tmr] = sim_gtime ();                          /* save inst time */
if (delta_rtime > 30000) {                              /* gap too big? */
    sim_rtcn_init (rtc_initd[tmr], tmr);                /* start over */
    return rtc_currd[tmr];                              /* can't calibr */
    }
if (delta_rtime == 0)                                   /* gap too small? */
    rtc_based[tmr] = rtc_based[tmr] * ticksper;         /* slew wide */
else rtc_based[tmr] = (int32) (((double) rtc_based[tmr] * (double) rtc_nxintv[tmr]) /
    ((double) delta_rtime));                            /* new base rate */
delta_vtime = rtc_vtime[tmr] - rtc_rtime[tmr];          /* gap */
if (delta_vtime > SIM_TMAX)                             /* limit gap */
    delta_vtime = SIM_TMAX;
else if (delta_vtime < -SIM_TMAX)
    delta_vtime = -SIM_TMAX;
rtc_nxintv[tmr] = 1000 + delta_vtime;                   /* next wtime */
rtc_currd[tmr] = (int32) (((double) rtc_based[tmr] * (double) rtc_nxintv[tmr]) /
    1000.0);                                            /* next delay */
if (rtc_based[tmr] <= 0)                                /* never negative or zero! */
    rtc_based[tmr] = 1;
if (rtc_currd[tmr] <= 0)                                /* never negative or zero! */
    rtc_currd[tmr] = 1;
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

/* sim_timer_init - get minimum sleep time available on this host */

t_bool sim_timer_init (void)
{
sim_idle_enab = FALSE;                                  /* init idle off */
sim_idle_rate_ms = sim_os_ms_sleep_init ();             /* get OS timer rate */
return (sim_idle_rate_ms != 0);
}

/* sim_idle - idle simulator until next event or for specified interval

   Inputs:
        tmr =   calibrated timer to use

   Must solve the linear equation

        ms_to_wait = w * ms_per_wait

   Or
        w = ms_to_wait / ms_per_wait
*/

t_bool sim_idle (uint32 tmr, t_bool sin_cyc)
{
static uint32 cyc_ms = 0;
uint32 w_ms, w_idle, act_ms;
int32 act_cyc;

if ((!sim_idle_enab) ||                                 /* idling disabled */
    (sim_clock_queue == NULL) ||                        /* clock queue empty? */
    ((sim_clock_queue->flags & UNIT_IDLE) == 0) ||      /* event not idle-able? */
    (rtc_elapsed[tmr] < sim_idle_stable)) {             /* timer not stable? */
    if (sin_cyc)
        sim_interval = sim_interval - 1;
    return FALSE;
    }
if (cyc_ms == 0)                                        /* not computed yet? */
    cyc_ms = (rtc_currd[tmr] * rtc_hz[tmr]) / 1000;     /* cycles per msec */
if ((sim_idle_rate_ms == 0) || (cyc_ms == 0)) {         /* not possible? */
    if (sin_cyc)
        sim_interval = sim_interval - 1;
    return FALSE;
    }
w_ms = (uint32) sim_interval / cyc_ms;                  /* ms to wait */
w_idle = w_ms / sim_idle_rate_ms;                       /* intervals to wait */
if (w_idle == 0) {                                      /* none? */
    if (sin_cyc)
        sim_interval = sim_interval - 1;
    return FALSE;
    }
act_ms = sim_os_ms_sleep (w_ms);                        /* wait */
act_cyc = act_ms * cyc_ms;
if (sim_interval > act_cyc)
    sim_interval = sim_interval - act_cyc;              /* count down sim_interval */
else sim_interval = 0;                                  /* or fire immediately */
return TRUE;
}

/* Set idling - implicitly disables throttling */

t_stat sim_set_idle (UNIT *uptr, int32 val, char *cptr, void *desc)
{
t_stat r;
uint32 v;

if (sim_idle_rate_ms == 0)
    return SCPE_NOFNC;
if ((val != 0) && (sim_idle_rate_ms > (uint32) val))
    return SCPE_NOFNC;
if (cptr) {
    v = (uint32) get_uint (cptr, 10, SIM_IDLE_STMAX, &r);
    if ((r != SCPE_OK) || (v < SIM_IDLE_STMIN))
        return SCPE_ARG;
    sim_idle_stable = v;
    }
sim_idle_enab = TRUE;
if (sim_throt_type != SIM_THROT_NONE) {
    sim_set_throt (0, NULL);
    sim_printf ("Throttling disabled\n");
    }
return SCPE_OK;
}

/* Clear idling */

t_stat sim_clr_idle (UNIT *uptr, int32 val, char *cptr, void *desc)
{
sim_idle_enab = FALSE;
return SCPE_OK;
}

/* Show idling */

t_stat sim_show_idle (FILE *st, UNIT *uptr, int32 val, void *desc)
{
if (sim_idle_enab)
    fprintf (st, "idle enabled, stability wait = %ds", sim_idle_stable);
else fputs ("idle disabled", st);
return SCPE_OK;
}

/* Throttling package */

t_stat sim_set_throt (int32 arg, char *cptr)
{
char *tptr, c;
t_value val;

if (arg == 0) {
    if ((cptr != 0) && (*cptr != 0))
        return SCPE_ARG;
    sim_throt_type = SIM_THROT_NONE;
    sim_throt_cancel ();
    }
else if (sim_idle_rate_ms == 0)
    return SCPE_NOFNC;
else {
    val = strtotv (cptr, &tptr, 10);
    if (cptr == tptr)
        return SCPE_ARG;
    c = toupper (*tptr++);
    if (*tptr != 0)
        return SCPE_ARG;
    if (c == 'M') 
        sim_throt_type = SIM_THROT_MCYC;
    else if (c == 'K')
        sim_throt_type = SIM_THROT_KCYC;
    else if ((c == '%') && (val > 0) && (val < 100))
        sim_throt_type = SIM_THROT_PCT;
    else return SCPE_ARG;
    if (sim_idle_enab) {
        sim_printf ("Idling disabled\n");
        sim_clr_idle (NULL, 0, NULL, NULL);
        }
    sim_throt_val = (uint32) val;
    }
return SCPE_OK;
}

t_stat sim_show_throt (FILE *st, DEVICE *dnotused, UNIT *unotused, int32 flag, char *cptr)
{
if (sim_idle_rate_ms == 0)
    fprintf (st, "Throttling not available\n");
else {
    switch (sim_throt_type) {

    case SIM_THROT_MCYC:
        fprintf (st, "Throttle = %d megacycles\n", sim_throt_val);
        break;

    case SIM_THROT_KCYC:
        fprintf (st, "Throttle = %d kilocycles\n", sim_throt_val);
        break;

    case SIM_THROT_PCT:
        fprintf (st, "Throttle = %d%%\n", sim_throt_val);
        break;

    default:
        fprintf (st, "Throttling disabled\n");
        break;
        }

    if (sim_switches & SWMASK ('D')) {
        fprintf (st, "Wait rate = %d ms\n", sim_idle_rate_ms);
        if (sim_throt_type != 0)
            fprintf (st, "Throttle interval = %d cycles\n", sim_throt_wait);
        }
    }
return SCPE_OK;
}

void sim_throt_sched (void)
{
sim_throt_state = 0;
if (sim_throt_type)
    sim_activate (&sim_throt_unit, SIM_THROT_WINIT);
return;
}

void sim_throt_cancel (void)
{
sim_cancel (&sim_throt_unit);
}

/* Throttle service

   Throttle service has three distinct states

   0        take initial measurement
   1        take final measurement, calculate wait values
   2        periodic waits to slow down the CPU
*/

t_stat sim_throt_svc (UNIT *uptr)
{
uint32 delta_ms;
double a_cps, d_cps;

switch (sim_throt_state) {

    case 0:                                             /* take initial reading */
        sim_throt_ms_start = sim_os_msec ();
        sim_throt_wait = SIM_THROT_WST;
        sim_throt_state++;                              /* next state */
        break;                                          /* reschedule */

    case 1:                                             /* take final reading */
        sim_throt_ms_stop = sim_os_msec ();
        delta_ms = sim_throt_ms_stop - sim_throt_ms_start;
        if (delta_ms < SIM_THROT_MSMIN) {               /* not enough time? */
            if (sim_throt_wait >= 100000000) {          /* too many inst? */
                sim_throt_state = 0;                    /* fails in 32b! */
                return SCPE_OK;
                }
            sim_throt_wait = sim_throt_wait * SIM_THROT_WMUL;
            sim_throt_ms_start = sim_throt_ms_stop;
            }
        else {                                          /* long enough */
            a_cps = ((double) sim_throt_wait) * 1000.0 / (double) delta_ms;
            if (sim_throt_type == SIM_THROT_MCYC)       /* calc desired cps */
                d_cps = (double) sim_throt_val * 1000000.0;
            else if (sim_throt_type == SIM_THROT_KCYC)
                d_cps = (double) sim_throt_val * 1000.0;
            else d_cps = (a_cps * ((double) sim_throt_val)) / 100.0;
            if (d_cps >= a_cps) {
                sim_throt_state = 0;
                return SCPE_OK;
                }
            sim_throt_wait = (int32)                    /* time between waits */
                ((a_cps * d_cps * ((double) sim_idle_rate_ms)) /
                 (1000.0 * (a_cps - d_cps)));
            if (sim_throt_wait < SIM_THROT_WMIN) {      /* not long enough? */
                sim_throt_state = 0;
                return SCPE_OK;
                }
            sim_throt_state++;
//            fprintf (stderr, "Throttle values a_cps = %f, d_cps = %f, wait = %d\n",
//                a_cps, d_cps, sim_throt_wait);
            }
        break;

    case 2:                                             /* throttling */
        sim_os_ms_sleep (1);
        break;
        }

sim_activate (uptr, sim_throt_wait);                    /* reschedule */
return SCPE_OK;
}

/* v4 compatibility routines */

/* Timer based on current execution rates */

double sim_timer_inst_per_sec (void)
{
double inst_per_sec;

inst_per_sec = ((double)rtc_currd[0]) * rtc_hz[0];
if (inst_per_sec == 0)
    inst_per_sec = 50000.0;
return inst_per_sec;
}

t_stat sim_activate_after (UNIT *uptr, int32 usec_delay)
{
int32 inst_delay;
double inst_per_sec;

if (sim_is_active (uptr))                               /* already active? */
    return SCPE_OK;
inst_per_sec = sim_timer_inst_per_sec ();
inst_delay = (int32)((inst_per_sec * usec_delay) / 1000000.0);
return sim_activate (uptr, inst_delay);                 /* queue it now */
}

/* sim_show_timers - show running timer information */

t_stat sim_show_timers (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, char* desc)
{
int tmr, clocks;

for (tmr = clocks = 0; tmr < SIM_NTIMERS; tmr++) {
    if (rtc_initd[tmr] == 0)
        continue;
    else clocks++;

    if (rtc_hz[tmr]) {
        fprintf (st, "Calibrated Timer %d: \n", tmr);
        fprintf (st, "  Running at:              %dhz\n", rtc_hz[tmr]);
        fprintf (st, "  Ticks in current second: %d\n",   rtc_ticks[tmr]);
        }
    else fprintf (st, "Uncalibrated Timer %d:\n", tmr);
    fprintf (st, "  Seconds Running:         %u\n",   rtc_elapsed[tmr]);
    fprintf (st, "  Calibrations:            %u\n",   rtc_calibrations[tmr]);
    fprintf (st, "  Last Calibration Time:   %.0f\n", rtc_gtime[tmr]);
    fprintf (st, "  Real Time:               %u\n",   rtc_rtime[tmr]);
    fprintf (st, "  Virtual Time:            %u\n",   rtc_vtime[tmr]);
    fprintf (st, "  Next Interval:           %u\n",   rtc_nxintv[tmr]);
    fprintf (st, "  Base Tick Delay:         %d\n",   rtc_based[tmr]);
    fprintf (st, "  Initial Insts per Tick:  %d\n",   rtc_initd[tmr]);
    fprintf (st, "  Current Insts per Tick:  %d\n",   rtc_currd[tmr]);
    }
if (clocks == 0)
    fprintf (st, "No calibrated clock devices\n");
return SCPE_OK;
}

/* Clock coscheduling routines - v4 */

t_stat sim_register_clock_unit (UNIT *uptr)
{
sim_clock_unit = uptr;
return SCPE_OK;
}

t_stat sim_clock_coschedule (UNIT *uptr, int32 interval)
{
if (sim_clock_unit == NULL)
    return sim_activate (uptr, interval);
else {
    int32 t = sim_activate_time (sim_clock_unit);
    return sim_activate (uptr, t? t - 1: interval);
    }
}
