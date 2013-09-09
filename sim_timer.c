/* sim_timer.c: simulator timer library

   Copyright (c) 1993-2010, Robert M Supnik

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

   21-Oct-11    MP      Fixed throttling in several ways:
                         - Sleep for the observed clock tick size while throttling
                         - Recompute the throttling wait once every 10 seconds
                           to account for varying instruction mixes during
                           different phases of a simulator execution or to 
                           accommodate the presence of other load on the host 
                           system.
                         - Each of the pre-existing throttling modes (Kcps, 
                           Mcps, and %) all compute the appropriate throttling 
                           interval dynamically.  These dynamic computations
                           assume that 100% of the host CPU is dedicated to 
                           the current simulator during this computation.
                           This assumption may not always be true and under 
                           certain conditions may never provide a way to 
                           correctly determine the appropriate throttling 
                           wait.  An additional throttling mode has been added
                           which allows the simulator operator to explicitly
                           state the desired throttling wait parameters.
                           These are specified by: 
                                  SET THROT insts/delay
                           where 'insts' is the number of instructions to 
                           execute before sleeping for 'delay' milliseconds.
   22-Apr-11    MP      Fixed Asynch I/O support to reasonably account cycles
                        when an idle wait is terminated by an external event
   05-Jan-11    MP      Added Asynch I/O support
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

   sim_timer_init -         initialize timing system
   sim_rtc_init -           initialize calibration
   sim_rtc_calb -           calibrate clock
   sim_timer_init -         initialize timing system
   sim_idle -               virtual machine idle
   sim_os_msec  -           return elapsed time in msec
   sim_os_sleep -           sleep specified number of seconds
   sim_os_ms_sleep -        sleep specified number of milliseconds
   sim_idle_ms_sleep -      sleep specified number of milliseconds
                            or until awakened by an asynchronous
                            event
   sim_timespec_diff        subtract two timespec values
   sim_timer_activate_after schedule unit for specific time


   The calibration, idle, and throttle routines are OS-independent; the _os_
   routines are not.
*/

#define NOT_MUX_USING_CODE /* sim_tmxr library provider or agnostic */

#include "sim_defs.h"
#include <ctype.h>
#include <math.h>

t_bool sim_idle_enab = FALSE;                       /* global flag */
volatile t_bool sim_idle_wait = FALSE;              /* global flag */

static int32 sim_calb_tmr = -1;                     /* the system calibrated timer */

static uint32 sim_idle_rate_ms = 0;
static uint32 sim_os_sleep_min_ms = 0;
static uint32 sim_idle_stable = SIM_IDLE_STDFLT;
static t_bool sim_idle_idled = FALSE;
static uint32 sim_throt_ms_start = 0;
static uint32 sim_throt_ms_stop = 0;
static uint32 sim_throt_type = 0;
static uint32 sim_throt_val = 0;
static uint32 sim_throt_state = 0;
static uint32 sim_throt_sleep_time = 0;
static int32 sim_throt_wait = 0;
UNIT *sim_clock_unit = NULL;
t_bool sim_asynch_timer = 
#if defined (SIM_ASYNCH_CLOCKS)
                                 TRUE;
#else
                                 FALSE;
#endif

t_stat sim_throt_svc (UNIT *uptr);

UNIT sim_throt_unit = { UDATA (&sim_throt_svc, 0, 0) };

#define DBG_IDL       TIMER_DBG_IDLE        /* idling */
#define DBG_QUE       TIMER_DBG_QUEUE       /* queue activities */
#define DBG_TRC       0x004                 /* tracing */
#define DBG_CAL       0x008                 /* calibration activities */
#define DBG_TIM       0x010                 /* timer thread activities */
DEBTAB sim_timer_debug[] = {
  {"TRACE",   DBG_TRC},
  {"IDLE",    DBG_IDL},
  {"QUEUE",   DBG_QUE},
  {"CALIB",   DBG_CAL},
  {"TIME",    DBG_TIM},
  {0}
};

#if defined(SIM_ASYNCH_IO)
uint32 sim_idle_ms_sleep (unsigned int msec)
{
uint32 start_time = sim_os_msec();
struct timespec done_time;
t_bool timedout = FALSE;

clock_gettime(CLOCK_REALTIME, &done_time);
done_time.tv_sec += (msec/1000);
done_time.tv_nsec += 1000000*(msec%1000);
if (done_time.tv_nsec > 1000000000) {
  done_time.tv_sec += done_time.tv_nsec/1000000000;
  done_time.tv_nsec = done_time.tv_nsec%1000000000;
  }
pthread_mutex_lock (&sim_asynch_lock);
sim_idle_wait = TRUE;
if (!pthread_cond_timedwait (&sim_asynch_wake, &sim_asynch_lock, &done_time))
  sim_asynch_check = 0;                 /* force check of asynch queue now */
else
  timedout = TRUE;
sim_idle_wait = FALSE;
pthread_mutex_unlock (&sim_asynch_lock);
if (!timedout) {
    AIO_UPDATE_QUEUE;
    }
return sim_os_msec() - start_time;
}
#define SIM_IDLE_MS_SLEEP sim_idle_ms_sleep
#else
#define SIM_IDLE_MS_SLEEP sim_os_ms_sleep
#endif

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

uint32 sim_os_msec (void)
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
sim_os_sleep_min_ms = 10;                               /* VAX/VMS is 10ms */
#else
sim_os_sleep_min_ms = 1;                                /* Alpha/VMS is 1ms */
#endif
return sim_os_sleep_min_ms;
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

#ifdef NEED_CLOCK_GETTIME
int clock_gettime(int clk_id, struct timespec *tp)
{
uint32 secs, ns, tod[2], unixbase[2] = {0xd53e8000, 0x019db1de};

if (clk_id != CLOCK_REALTIME)
  return -1;

sys$gettim (tod);                                       /* time 0.1usec */
lib$subx(tod, unixbase, tod);                           /* convert to unix base */
lib$ediv(&10000000, tod, &secs, &ns);                   /* isolate seconds & 100ns parts */
tp->tv_sec = secs;
tp->tv_nsec = ns*100;
return 0;
}
#endif /* CLOCK_REALTIME */

#elif defined (_WIN32)

/* Win32 routines */

#include <windows.h>

const t_bool rtc_avail = TRUE;

uint32 sim_os_msec (void)
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
sim_os_sleep_min_ms = timers.wPeriodMin;
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
return sim_os_sleep_min_ms;                             /* sim_idle_rate_ms */
}

uint32 sim_os_ms_sleep (unsigned int msec)
{
uint32 stime = sim_os_msec();

Sleep (msec);
return sim_os_msec () - stime;
}

#if defined(NEED_CLOCK_GETTIME)
int clock_gettime(int clk_id, struct timespec *tp)
{
t_uint64 now, unixbase;

if (clk_id != CLOCK_REALTIME)
    return -1;
unixbase = 116444736;
unixbase *= 1000000000;
GetSystemTimeAsFileTime((FILETIME*)&now);
now -= unixbase;
tp->tv_sec = (long)(now/10000000);
tp->tv_nsec = (now%10000000)*100;
return 0;
}
#endif

#elif defined (__OS2__)

/* OS/2 routines, from Bruce Ray */

const t_bool rtc_avail = FALSE;

uint32 sim_os_msec (void)
{
return 0;
}

void sim_os_sleep (unsigned int sec)
{
return;
}

uint32 sim_os_ms_sleep_init (void)
{
return 0;
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
return sim_os_sleep_min_ms = 1;
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

#if defined(NEED_CLOCK_GETTIME)
int clock_gettime(int clk_id, struct timespec *tp)
{
struct timeval cur;
struct timezone foo;

if (clk_id != CLOCK_REALTIME)
  return -1;
gettimeofday (&cur, &foo);
tp->tv_sec = cur.tv_sec;
tp->tv_nsec = cur.tv_usec*1000;
return 0;
}
#endif

#else

/* UNIX routines */

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#define NANOS_PER_MILLI     1000000
#define MILLIS_PER_SEC      1000
#define sleep1Samples       100

const t_bool rtc_avail = TRUE;

uint32 sim_os_msec (void)
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

SIM_IDLE_MS_SLEEP (1);                  /* Start sampling on a tick boundary */
for (i = 0, tot = 0; i < sleep1Samples; i++) {
    t1 = sim_os_msec ();
    SIM_IDLE_MS_SLEEP (1);
    t2 = sim_os_msec ();
    tot += (t2 - t1);
    }
tim = (tot + (sleep1Samples - 1)) / sleep1Samples;
sim_os_sleep_min_ms = tim;
if (tim > SIM_IDLE_MAX)
    tim = 0;
return tim;
}
#if !defined(_POSIX_SOURCE)
#ifdef NEED_CLOCK_GETTIME
typedef int clockid_t;
int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
struct timeval cur;
struct timezone foo;

if (clk_id != CLOCK_REALTIME)
  return -1;
gettimeofday (&cur, &foo);
tp->tv_sec = cur.tv_sec;
tp->tv_nsec = cur.tv_usec*1000;
return 0;
}
#endif /* CLOCK_REALTIME */
#endif /* !defined(_POSIX_SOURCE) && defined(SIM_ASYNCH_IO) */

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

/* diff = min - sub */
void
sim_timespec_diff (struct timespec *diff, struct timespec *min, struct timespec *sub)
{
/* move the minuend value to the difference and operate there. */
*diff = *min;
/* Borrow as needed for the nsec value */
while (sub->tv_nsec > diff->tv_nsec) {
    --diff->tv_sec;
    diff->tv_nsec += 1000000000;
    }
diff->tv_nsec -= sub->tv_nsec;
diff->tv_sec -= sub->tv_sec;
/* Normalize the result */
while (diff->tv_nsec > 1000000000) {
    ++diff->tv_sec;
    diff->tv_nsec -= 1000000000;
    }
}

#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_CLOCKS)
static int sim_timespec_compare (struct timespec *a, struct timespec *b)
{
while (a->tv_nsec > 1000000000) {
    a->tv_nsec -= 1000000000;
    ++a->tv_sec;
    }
while (b->tv_nsec > 1000000000) {
    b->tv_nsec -= 1000000000;
    ++b->tv_sec;
    }
if (a->tv_sec < b->tv_sec)
    return -1;
if (a->tv_sec > b->tv_sec)
    return 1;
if (a->tv_nsec < b->tv_nsec)
    return -1;
if (a->tv_nsec > b->tv_nsec)
    return 1;
else
    return 0;
}
#endif /* defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_CLOCKS) */

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
static double rtc_clock_skew_max[SIM_NTIMERS] = { 0 };  /* asynchronous max skew */

void sim_rtcn_init_all (void)
{
uint32 i;

for (i = 0; i < SIM_NTIMERS; i++) {
    if (rtc_initd[i] != 0) sim_rtcn_init (rtc_initd[i], i);
    }
return;
}

int32 sim_rtcn_init (int32 time, int32 tmr)
{
sim_debug (DBG_CAL, &sim_timer_dev, "sim_rtcn_init(time=%d, tmr=%d)\n", time, tmr);
if (time == 0)
    time = 1;
if ((tmr < 0) || (tmr >= SIM_NTIMERS))
    return time;
rtc_rtime[tmr] = sim_os_msec ();
rtc_vtime[tmr] = rtc_rtime[tmr];
rtc_nxintv[tmr] = 1000;
rtc_ticks[tmr] = 0;
rtc_hz[tmr] = 0;
rtc_based[tmr] = time;
rtc_currd[tmr] = time;
rtc_initd[tmr] = time;
rtc_elapsed[tmr] = 0;
rtc_calibrations[tmr] = 0;
if (sim_calb_tmr == -1)                 /* save first initialized clock as the system timer */
    sim_calb_tmr  = tmr;
return time;
}

int32 sim_rtcn_calb (int32 ticksper, int32 tmr)
{
uint32 new_rtime, delta_rtime;
int32 delta_vtime;
double new_gtime;
int32 new_currd;

if ((tmr < 0) || (tmr >= SIM_NTIMERS))
    return 10000;
rtc_hz[tmr] = ticksper;
rtc_ticks[tmr] = rtc_ticks[tmr] + 1;                    /* count ticks */
if (rtc_ticks[tmr] < ticksper) {                        /* 1 sec yet? */
    return rtc_currd[tmr];
    }
rtc_ticks[tmr] = 0;                                     /* reset ticks */
rtc_elapsed[tmr] = rtc_elapsed[tmr] + 1;                /* count sec */
if (!rtc_avail) {                                       /* no timer? */
    return rtc_currd[tmr];
    }
new_rtime = sim_os_msec ();                             /* wall time */
sim_debug (DBG_TRC, &sim_timer_dev, "sim_rtcn_calb(ticksper=%d, tmr=%d) rtime=%d\n", ticksper, tmr, new_rtime);
if (sim_idle_idled) {
    rtc_rtime[tmr] = new_rtime;                         /* save wall time */
    rtc_vtime[tmr] = rtc_vtime[tmr] + 1000;             /* adv sim time */
    rtc_gtime[tmr] = sim_gtime();                       /* save instruction time */
    sim_idle_idled = FALSE;                             /* reset idled flag */
    sim_debug (DBG_CAL, &sim_timer_dev, "skipping calibration due to idling - result: %d\n", rtc_currd[tmr]);
    return rtc_currd[tmr];                              /* avoid calibrating idle checks */
    }
if (new_rtime < rtc_rtime[tmr]) {                       /* time running backwards? */
    rtc_rtime[tmr] = new_rtime;                         /* reset wall time */
    sim_debug (DBG_CAL, &sim_timer_dev, "time running backwards - result: %d\n", rtc_currd[tmr]);
    return rtc_currd[tmr];                              /* can't calibrate */
    }
++rtc_calibrations[tmr];                                /* count calibrations */
delta_rtime = new_rtime - rtc_rtime[tmr];               /* elapsed wtime */
rtc_rtime[tmr] = new_rtime;                             /* adv wall time */
rtc_vtime[tmr] = rtc_vtime[tmr] + 1000;                 /* adv sim time */
if (delta_rtime > 30000) {                              /* gap too big? */
    rtc_currd[tmr] = rtc_initd[tmr];
    rtc_gtime[tmr] = sim_gtime();                       /* save instruction time */
    sim_debug (DBG_CAL, &sim_timer_dev, "gap too big: delta = %d - result: %d\n", delta_rtime, rtc_initd[tmr]);
    return rtc_initd[tmr];                              /* can't calibr */
    }
new_gtime = sim_gtime();
if (sim_asynch_enabled && sim_asynch_timer) {
    if (rtc_elapsed[tmr] > sim_idle_stable) {
        /* An asynchronous clock, merely needs to divide the number of */
        /* instructions actually executed by the clock rate. */
        new_currd = (int32)((new_gtime - rtc_gtime[tmr])/ticksper);
        /* avoid excessive swings in the calibrated result */
        if (new_currd > 10*rtc_currd[tmr])              /* don't swing big too fast */
            new_currd = 10*rtc_currd[tmr];
        else
            if (new_currd < rtc_currd[tmr]/10)          /* don't swing small too fast */
                new_currd = rtc_currd[tmr]/10;
        rtc_currd[tmr] = new_currd;
        rtc_gtime[tmr] = new_gtime;                     /* save instruction time */
        if (rtc_currd[tmr] == 127) {
            sim_debug (DBG_CAL, &sim_timer_dev, "asynch calibration small: %d\n", rtc_currd[tmr]);
            }
        sim_debug (DBG_CAL, &sim_timer_dev, "asynch calibration result: %d\n", rtc_currd[tmr]);
        return rtc_currd[tmr];                          /* calibrated result */
        }
    else {
        rtc_currd[tmr] = rtc_initd[tmr];
        rtc_gtime[tmr] = new_gtime;                     /* save instruction time */
        sim_debug (DBG_CAL, &sim_timer_dev, "asynch not stable calibration result: %d\n", rtc_initd[tmr]);
        return rtc_initd[tmr];                          /* initial result until stable */
        }
    }
rtc_gtime[tmr] = new_gtime;                             /* save instruction time */
/* This self regulating algorithm depends directly on the assumption */
/* that this routine is called back after processing the number of */
/* instructions which was returned the last time it was called. */
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
sim_debug (DBG_CAL, &sim_timer_dev, "calibrated result: %d\n", rtc_currd[tmr]);
AIO_SET_INTERRUPT_LATENCY(rtc_currd[tmr]*ticksper);     /* set interrrupt latency */
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
sim_debug (DBG_TRC, &sim_timer_dev, "sim_timer_init()\n");
sim_register_internal_device (&sim_timer_dev);
sim_idle_enab = FALSE;                                  /* init idle off */
sim_idle_rate_ms = sim_os_ms_sleep_init ();             /* get OS timer rate */
return (sim_idle_rate_ms != 0);
}

/* sim_timer_idle_capable - tell if the host is Idle capable and what the host OS tick size is */

uint32 sim_timer_idle_capable (uint32 *host_tick_ms)
{
if (host_tick_ms)
    *host_tick_ms = sim_os_sleep_min_ms;
return sim_idle_rate_ms;
}

/* sim_show_timers - show running timer information */

t_stat sim_show_timers (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, char* desc)
{
int tmr;

if (sim_clock_unit)
    fprintf (st, "%s clock device is %s\n", sim_name, sim_uname(sim_clock_unit));
else
    fprintf (st, "%s clock device is not specified, co-scheduling is unavailable\n", sim_name);
for (tmr=0; tmr<SIM_NTIMERS; ++tmr) {
    if (0 == rtc_initd[tmr])
        continue;
    
    fprintf (st, "%s%sTimer %d:\n", (sim_asynch_enabled && sim_asynch_timer) ? "Asynchronous " : "", rtc_hz[tmr] ? "Calibrated " : "Uncalibrated ", tmr);
    if (rtc_hz[tmr]) {
        fprintf (st, "  Running at:              %dhz\n", rtc_hz[tmr]);
        fprintf (st, "  Ticks in current second: %d\n",   rtc_ticks[tmr]);
        }
    fprintf (st, "  Seconds Running:         %u\n",   rtc_elapsed[tmr]);
    fprintf (st, "  Calibrations:            %u\n",   rtc_calibrations[tmr]);
    fprintf (st, "  Instruction Time:        %.0f\n", rtc_gtime[tmr]);
    if (!(sim_asynch_enabled && sim_asynch_timer)) {
        fprintf (st, "  Real Time:               %u\n",   rtc_rtime[tmr]);
        fprintf (st, "  Virtual Time:            %u\n",   rtc_vtime[tmr]);
        fprintf (st, "  Next Interval:           %u\n",   rtc_nxintv[tmr]);
        fprintf (st, "  Base Tick Delay:         %d\n",   rtc_based[tmr]);
        fprintf (st, "  Initial Insts Per Tick:  %d\n",   rtc_initd[tmr]);
        }
    fprintf (st, "  Current Insts Per Tick:  %d\n",   rtc_currd[tmr]);
    if (rtc_clock_skew_max[tmr] != 0.0)
        fprintf (st, "  Peak Clock Skew:         %.0fms\n",   rtc_clock_skew_max[tmr]);
    }
return SCPE_OK;
}

REG sim_timer_reg[] = {
    { DRDATAD (TICKS_PER_SEC,    rtc_hz[0],              32, "Ticks Per Second"), PV_RSPC|REG_RO},
    { DRDATAD (INSTS_PER_TICK,   rtc_currd[0],           32, "Instructions Per Tick"), PV_RSPC|REG_RO},
    { FLDATAD (IDLE_ENAB,        sim_idle_enab,           0, "Idle Enabled"), REG_RO},
    { DRDATAD (IDLE_RATE_MS,     sim_idle_rate_ms,       32, "Idle Rate Milliseconds"), PV_RSPC|REG_RO},
    { DRDATAD (OS_SLEEP_MIN_MS,  sim_os_sleep_min_ms,    32, "Minimum Sleep Resolution"), PV_RSPC|REG_RO},
    { DRDATAD (IDLE_STABLE,      sim_idle_stable,        32, "Idle Stable"), PV_RSPC},
    { FLDATAD (IDLE_IDLED,       sim_idle_idled,          0, ""), REG_RO},
    { DRDATAD (TMR,              sim_calb_tmr,           32, ""), PV_RSPC|REG_RO},
    { DRDATAD (THROT_MS_START,   sim_throt_ms_start,     32, ""), PV_RSPC|REG_RO},
    { DRDATAD (THROT_MS_STOP,    sim_throt_ms_stop,      32, ""), PV_RSPC|REG_RO},
    { DRDATAD (THROT_TYPE,       sim_throt_type,         32, ""), PV_RSPC|REG_RO},
    { DRDATAD (THROT_VAL,        sim_throt_val,          32, ""), PV_RSPC|REG_RO},
    { DRDATAD (THROT_STATE,      sim_throt_state,        32, ""), PV_RSPC|REG_RO},
    { DRDATAD (THROT_SLEEP_TIME, sim_throt_sleep_time,   32, ""), PV_RSPC|REG_RO},
    { DRDATAD (THROT_WAIT,       sim_throt_wait,         32, ""), PV_RSPC|REG_RO},
    { NULL }
    };

/* Clear, Set and show asynch */

/* Clear asynch */

t_stat sim_timer_clr_async (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (sim_asynch_timer) {
    sim_asynch_timer = FALSE;
    sim_timer_change_asynch ();
    }
return SCPE_OK;
}

t_stat sim_timer_set_async (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (!sim_asynch_timer) {
    sim_asynch_timer = TRUE;
    sim_timer_change_asynch ();
    }
return SCPE_OK;
}

t_stat sim_timer_show_async (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, "%s", (sim_asynch_enabled && sim_asynch_timer) ? "Asynchronous" : "Synchronous");
return SCPE_OK;
}

MTAB sim_timer_mod[] = {
#if defined (SIM_ASYNCH_IO) && defined (SIM_ASYNCH_CLOCKS)
  { MTAB_VDV,          MTAB_VDV, "ASYNC", "ASYNC",   &sim_timer_set_async, &sim_timer_show_async, NULL, "Enables/Displays Asynchronous Timer operation mode" },
  { MTAB_VDV,                 0,    NULL, "NOASYNC", &sim_timer_clr_async, NULL,                  NULL, "Disables Asynchronous Timer operation" },
#endif
  { 0 },
};

DEVICE sim_timer_dev = {
    "TIMER", &sim_throt_unit, sim_timer_reg, sim_timer_mod, 
    1, 0, 0, 0, 0, 0, 
    NULL, NULL, NULL, NULL, NULL, NULL, 
    NULL, DEV_DEBUG, 0, sim_timer_debug};


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

if ((!sim_idle_enab)                             ||     /* idling disabled */
    ((sim_clock_queue == QUEUE_LIST_END) &&             /* or clock queue empty? */
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_CLOCKS)
     (!(sim_asynch_enabled && sim_asynch_timer)))||     /*     and not asynch? */
#else
     (TRUE))                                     ||
#endif
    ((sim_clock_queue != QUEUE_LIST_END) && 
     ((sim_clock_queue->flags & UNIT_IDLE) == 0))||     /* or event not idle-able? */
    (rtc_elapsed[tmr] < sim_idle_stable)) {             /* or timer not stable? */
    if (sin_cyc)
        sim_interval = sim_interval - 1;
    return FALSE;
    }
/*
   When a simulator is in an instruction path (or under other conditions 
   which would indicate idling), the countdown of sim_interval will not 
   be happening at a pace which is consistent with the rate it happens 
   when not in the ‘idle capable’ state.  The consequence of this is that 
   the clock calibration may produce calibrated results which vary much 
   more than they do when not in the idle able state.  Sim_idle also uses 
   the calibrated tick size to approximate an adjustment to sim_interval
   to reflect the number of instructions which would have executed during 
   the actual idle time, so consistent calibrated numbers produce better 
   adjustments. 
   
   To negate this effect, we set a flag (sim_idle_idled) here and the 
   sim_rtcn_calb routine checks this flag before performing an actual 
   calibration and skips calibration if the flag was set and then clears 
   the flag.  Thus recalibration only happens if things didn’t idle.

   we also check check sim_idle_enab above so that all simulators can avoid
   directly checking sim_idle_enab before calling sim_idle so that all of 
   the bookkeeping on sim_idle_idled is done here in sim_timer where it 
   means something, while not idling when it isn’t enabled.  
   */
//sim_idle_idled = TRUE;                                  /* record idle attempt */
sim_debug (DBG_TRC, &sim_timer_dev, "sim_idle(tmr=%d, sin_cyc=%d)\n", tmr, sin_cyc);
if (cyc_ms == 0)                                        /* not computed yet? */
    cyc_ms = (rtc_currd[tmr] * rtc_hz[tmr]) / 1000;     /* cycles per msec */
if ((sim_idle_rate_ms == 0) || (cyc_ms == 0)) {         /* not possible? */
    if (sin_cyc)
        sim_interval = sim_interval - 1;
    sim_debug (DBG_IDL, &sim_timer_dev, "not possible %d - %d\n", sim_idle_rate_ms, cyc_ms);
    return FALSE;
    }
w_ms = (uint32) sim_interval / cyc_ms;                  /* ms to wait */
w_idle = w_ms / sim_idle_rate_ms;                       /* intervals to wait */
if (w_idle == 0) {                                      /* none? */
    if (sin_cyc)
        sim_interval = sim_interval - 1;
    sim_debug (DBG_IDL, &sim_timer_dev, "no wait\n");
    return FALSE;
    }
if (sim_clock_queue == QUEUE_LIST_END)
    sim_debug (DBG_IDL, &sim_timer_dev, "sleeping for %d ms - pending event in %d instructions\n", w_ms, sim_interval);
else
    sim_debug (DBG_IDL, &sim_timer_dev, "sleeping for %d ms - pending event on %s in %d instructions\n", w_ms, sim_uname(sim_clock_queue), sim_interval);
act_ms = SIM_IDLE_MS_SLEEP (w_ms);                      /* wait */
act_cyc = act_ms * cyc_ms;
if (act_ms < w_ms)                                      /* awakened early? */
    act_cyc += (cyc_ms * sim_idle_rate_ms) / 2;         /* account for half an interval's worth of cycles */
if (sim_interval > act_cyc)
    sim_interval = sim_interval - act_cyc;              /* count down sim_interval */
else sim_interval = 0;                                  /* or fire immediately */
if (sim_clock_queue == QUEUE_LIST_END)
    sim_debug (DBG_IDL, &sim_timer_dev, "slept for %d ms - pending event in %d instructions\n", act_ms, sim_interval);
else
    sim_debug (DBG_IDL, &sim_timer_dev, "slept for %d ms - pending event on %s in %d instructions\n", act_ms, sim_uname(sim_clock_queue), sim_interval);
return TRUE;
}

/* Set idling - implicitly disables throttling */

t_stat sim_set_idle (UNIT *uptr, int32 val, char *cptr, void *desc)
{
t_stat r;
uint32 v;

if (sim_idle_rate_ms == 0) {
    printf ("Idling is not available, Minimum OS sleep time is %dms\n", sim_os_sleep_min_ms);
    if (sim_log)
        fprintf (sim_log, "Idling is not available, Minimum OS sleep time is %dms\n", sim_os_sleep_min_ms);
    return SCPE_NOFNC;
    }
if ((val != 0) && (sim_idle_rate_ms > (uint32) val)) {
    printf ("Idling is not available, Minimum OS sleep time is %dms, Requied minimum OS sleep is %dms\n", sim_os_sleep_min_ms, val);
    if (sim_log)
        fprintf (sim_log, "Idling is not available, Minimum OS sleep time is %dms, Requied minimum OS sleep is %dms\n", sim_os_sleep_min_ms, val);
    return SCPE_NOFNC;
    }
if (cptr) {
    v = (uint32) get_uint (cptr, 10, SIM_IDLE_STMAX, &r);
    if ((r != SCPE_OK) || (v < SIM_IDLE_STMIN))
        return SCPE_ARG;
    sim_idle_stable = v;
    }
sim_idle_enab = TRUE;
if (sim_throt_type != SIM_THROT_NONE) {
    sim_set_throt (0, NULL);
    printf ("Throttling disabled\n");
    if (sim_log)
        fprintf (sim_log, "Throttling disabled\n");
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
    fprintf (st, "idle enabled");
else
    fprintf (st, "idle disabled");
if (sim_switches & SWMASK ('D'))
    fprintf (st, ", stability wait = %ds, minimum sleep resolution = %dms", sim_idle_stable, sim_os_sleep_min_ms);
return SCPE_OK;
}

/* Throttling package */

t_stat sim_set_throt (int32 arg, char *cptr)
{
char *tptr, c;
t_value val, val2 = 0;

if (arg == 0) {
    if ((cptr != 0) && (*cptr != 0))
        return SCPE_ARG;
    sim_throt_type = SIM_THROT_NONE;
    sim_throt_cancel ();
    }
else if (sim_idle_rate_ms == 0) {
    printf ("Throttling is not available, Minimum OS sleep time is %dms\n", sim_os_sleep_min_ms);
    if (sim_log)
        fprintf (sim_log, "Throttling is not available, Minimum OS sleep time is %dms\n", sim_os_sleep_min_ms);
    return SCPE_NOFNC;
    }
else {
    val = strtotv (cptr, &tptr, 10);
    if (cptr == tptr)
        return SCPE_ARG;
    sim_throt_sleep_time = sim_idle_rate_ms;
    c = toupper (*tptr++);
    if (c == '/')
        val2 = strtotv (tptr, &tptr, 10);
    if ((*tptr != 0) || (val == 0))
        return SCPE_ARG;
    if (c == 'M') 
        sim_throt_type = SIM_THROT_MCYC;
    else if (c == 'K')
        sim_throt_type = SIM_THROT_KCYC;
    else if ((c == '%') && (val > 0) && (val < 100))
        sim_throt_type = SIM_THROT_PCT;
    else if ((c == '/') && (val2 != 0)) {
        sim_throt_type = SIM_THROT_SPC;
        }
    else return SCPE_ARG;
    if (sim_idle_enab) {
        printf ("Idling disabled\n");
        if (sim_log)
            fprintf (sim_log, "Idling disabled\n");
        sim_clr_idle (NULL, 0, NULL, NULL);
        }
    sim_throt_val = (uint32) val;
    if (sim_throt_type == SIM_THROT_SPC) {
        if (val2 >= sim_idle_rate_ms)
            sim_throt_sleep_time = (uint32) val2;
        else {
            sim_throt_sleep_time = (uint32) (val2 * sim_idle_rate_ms);
            sim_throt_val = (uint32) (val * sim_idle_rate_ms);
            }
        }
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

    case SIM_THROT_SPC:
        fprintf (st, "Throttle = %d ms every %d cycles\n", sim_throt_sleep_time, sim_throt_val);
        break;

    default:
        fprintf (st, "Throttling disabled\n");
        break;
        }

    if (sim_switches & SWMASK ('D')) {
        if (sim_throt_type != 0)
            fprintf (st, "Throttle interval = %d cycles\n", sim_throt_wait);
        }
    }
if (sim_switches & SWMASK ('D'))
    fprintf (st, "minimum sleep resolution = %d ms\n", sim_os_sleep_min_ms);
return SCPE_OK;
}

void sim_throt_sched (void)
{
sim_throt_state = 0;
if (sim_throt_type)
    sim_activate (&sim_throt_unit, SIM_THROT_WINIT);
}

void sim_throt_cancel (void)
{
sim_cancel (&sim_throt_unit);
}

/* Throttle service

   Throttle service has three distinct states used while dynamically
   determining a throttling interval:

       0    take initial measurement
       1    take final measurement, calculate wait values
       2    periodic waits to slow down the CPU
*/

t_stat sim_throt_svc (UNIT *uptr)
{
uint32 delta_ms;
double a_cps, d_cps;

if (sim_throt_type == SIM_THROT_SPC) {                  /* Non dynamic? */
    sim_throt_state = 2;                                /* force state */
    sim_throt_wait = sim_throt_val;
    }
switch (sim_throt_state) {

    case 0:                                             /* take initial reading */
        sim_throt_ms_start = sim_os_msec ();
        sim_throt_wait = SIM_THROT_WST;
        sim_throt_state = 1;                            /* next state */
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
                sim_throt_sched ();                     /* start over */
                return SCPE_OK;
                }
            sim_throt_wait = (int32)                    /* time between waits */
                ((a_cps * d_cps * ((double) sim_idle_rate_ms)) /
                 (1000.0 * (a_cps - d_cps)));
            if (sim_throt_wait < SIM_THROT_WMIN) {      /* not long enough? */
                sim_throt_sched ();                     /* start over */
                return SCPE_OK;
                }
            sim_throt_ms_start = sim_throt_ms_stop;
            sim_throt_state = 2;
//            fprintf (stderr, "Throttle values a_cps = %f, d_cps = %f, wait = %d\n",
//                a_cps, d_cps, sim_throt_wait);
            }
        break;

    case 2:                                             /* throttling */
        SIM_IDLE_MS_SLEEP (sim_throt_sleep_time);
        delta_ms = sim_os_msec () - sim_throt_ms_start;
        if ((sim_throt_type != SIM_THROT_SPC) &&        /* when dynamic throttling */
            (delta_ms >= 10000)) {                      /* recompute every 10 sec */
            sim_throt_ms_start = sim_os_msec ();
            sim_throt_wait = SIM_THROT_WST;
            sim_throt_state = 1;                        /* next state */
            }
        break;
        }

sim_activate (uptr, sim_throt_wait);                    /* reschedule */
return SCPE_OK;
}

#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_CLOCKS)

static double _timespec_to_double (struct timespec *time)
{
return ((double)time->tv_sec)+(double)(time->tv_nsec)/1000000000.0;
}

static void _double_to_timespec (struct timespec *time, double dtime)
{
time->tv_sec = (time_t)floor(dtime);
time->tv_nsec = (long)((dtime-floor(dtime))*1000000000.0);
}

double sim_timenow_double (void)
{
struct timespec now;

clock_gettime(CLOCK_REALTIME, &now);
return _timespec_to_double (&now);
}

extern int32 sim_is_running;
extern UNIT * volatile sim_wallclock_queue;
extern UNIT * volatile sim_wallclock_entry;

pthread_t           sim_timer_thread;           /* Wall Clock Timing Thread Id */
pthread_cond_t      sim_timer_startup_cond;
t_bool              sim_timer_thread_running = FALSE;
t_bool              sim_timer_event_canceled = FALSE;

static void *
_timer_thread(void *arg)
{
int sched_policy;
struct sched_param sched_priority;

/* Boost Priority for this I/O thread vs the CPU instruction execution 
   thread which, in general, won't be readily yielding the processor when 
   this thread needs to run */
pthread_getschedparam (pthread_self(), &sched_policy, &sched_priority);
++sched_priority.sched_priority;
pthread_setschedparam (pthread_self(), sched_policy, &sched_priority);

sim_debug (DBG_TIM, &sim_timer_dev, "_timer_thread() - starting\n");

pthread_mutex_lock (&sim_timer_lock);
pthread_cond_signal (&sim_timer_startup_cond);   /* Signal we're ready to go */
while (sim_asynch_enabled && sim_asynch_timer && sim_is_running) {
    struct timespec start_time, stop_time;
    struct timespec due_time;
    double wait_usec;
    int32 inst_delay;
    double inst_per_sec;
    UNIT *uptr;

    if (sim_wallclock_entry) {                          /* something to insert in queue? */
        UNIT *cptr, *prvptr;

        sim_debug (DBG_TIM, &sim_timer_dev, "_timer_thread() - timing %s for %d usec\n", 
                   sim_uname(sim_wallclock_entry), sim_wallclock_entry->time);

        uptr = sim_wallclock_entry;
        sim_wallclock_entry = NULL;

        prvptr = NULL;
        for (cptr = sim_wallclock_queue; cptr != QUEUE_LIST_END; cptr = cptr->a_next) {
            if (uptr->a_due_time < cptr->a_due_time)
                break;
            prvptr = cptr;
            }
        if (prvptr == NULL) {                           /* insert at head */
            cptr = uptr->a_next = sim_wallclock_queue;
            sim_wallclock_queue = uptr;
            }
        else {
            cptr = uptr->a_next = prvptr->a_next;       /* insert at prvptr */
            prvptr->a_next = uptr;
            }
        }

    /* determine wait time */
    if (sim_wallclock_queue != QUEUE_LIST_END) {
        /* due time adjusted by 1/2 a minimal sleep interval */
        /* the goal being to let the last fractional part of the due time */
        /* be done by counting instructions */
        _double_to_timespec (&due_time, sim_wallclock_queue->a_due_time-(((double)sim_idle_rate_ms)*0.0005));
        }
    else {
        due_time.tv_sec = 0x7FFFFFFF;                   /* Sometime when 32 bit time_t wraps */
        due_time.tv_nsec = 0;
        }
    clock_gettime(CLOCK_REALTIME, &start_time);
    wait_usec = floor(1000000.0*(_timespec_to_double (&due_time) - _timespec_to_double (&start_time)));
    if (sim_wallclock_queue == QUEUE_LIST_END)
        sim_debug (DBG_TIM, &sim_timer_dev, "_timer_thread() - waiting forever\n");
    else
        sim_debug (DBG_TIM, &sim_timer_dev, "_timer_thread() - waiting for %.0f usecs until %.6f for %s\n", wait_usec, sim_wallclock_queue->a_due_time, sim_uname(sim_wallclock_queue));
    if ((wait_usec <= 0.0) || 
        (0 != pthread_cond_timedwait (&sim_timer_wake, &sim_timer_lock, &due_time))) {
        if (sim_wallclock_queue == QUEUE_LIST_END)      /* queue empty? */
            continue;                                   /* wait again */
        inst_per_sec = sim_timer_inst_per_sec ();

        uptr = sim_wallclock_queue;
        sim_wallclock_queue = uptr->a_next;
        uptr->a_next = NULL;                            /* hygiene */

        clock_gettime(CLOCK_REALTIME, &stop_time);
        if (1 != sim_timespec_compare (&due_time, &stop_time)) {
            inst_delay = 0;
            uptr->a_last_fired_time = _timespec_to_double(&stop_time);
            }
        else {
            inst_delay = (int32)(inst_per_sec*(_timespec_to_double(&due_time)-_timespec_to_double(&stop_time)));
            uptr->a_last_fired_time = uptr->a_due_time;
            }
        sim_debug (DBG_TIM, &sim_timer_dev, "_timer_thread() - slept %.0fms - activating(%s,%d)\n", 
                   1000.0*(_timespec_to_double (&stop_time)-_timespec_to_double (&start_time)), sim_uname(uptr), inst_delay);
        if (sim_clock_unit == uptr) {
            /*
             * Some devices may depend on executing during the same instruction or immediately 
             * after the clock tick event.  To satisfy this, we link the clock unit to the head
             * of the clock coschedule queue and then insert that list in the asynch event 
             * queue in a single operation
             */
            uptr->a_next = sim_clock_cosched_queue;
            sim_clock_cosched_queue = QUEUE_LIST_END;
            AIO_ACTIVATE_LIST(sim_activate, uptr, inst_delay);
            }
        else
            sim_activate (uptr, inst_delay);
        }
    else {/* Something wants to adjust the queue since the wait condition was signaled */
        if (sim_timer_event_canceled)
            sim_timer_event_canceled = FALSE;           /* reset flag and continue */
        }
    }
pthread_mutex_unlock (&sim_timer_lock);

sim_debug (DBG_TIM, &sim_timer_dev, "_timer_thread() - exiting\n");

return NULL;
}

#endif /* defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_CLOCKS) */

void sim_start_timer_services (void)
{
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_CLOCKS)
pthread_mutex_lock (&sim_timer_lock);
if (sim_asynch_enabled && sim_asynch_timer) {
    pthread_attr_t attr;
    UNIT *cptr;
    double delta_due_time = 0;

    /* when restarting after being manually stopped the due times for all */
    /* timer events needs to slide so they fire in the future. (clock ticks */
    /* don't accumulate when the simulator is stopped) */
    for (cptr = sim_wallclock_queue; cptr != QUEUE_LIST_END; cptr = cptr->a_next) {
        if (cptr == sim_wallclock_queue) { /* Handle first entry */
            struct timespec now;
            double due_time;

            clock_gettime(CLOCK_REALTIME, &now);
            due_time = _timespec_to_double(&now) + ((double)(cptr->a_usec_delay)/1000000.0);
            delta_due_time = due_time - cptr->a_due_time;
            }
        cptr->a_due_time += delta_due_time;
        }
    sim_debug (DBG_TRC, &sim_timer_dev, "sim_start_timer_services() - starting\n");
    pthread_cond_init (&sim_timer_startup_cond, NULL);
    pthread_attr_init (&attr);
    pthread_attr_setscope (&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_create (&sim_timer_thread, &attr, _timer_thread, NULL);
    pthread_attr_destroy( &attr);
    pthread_cond_wait (&sim_timer_startup_cond, &sim_timer_lock); /* Wait for thread to stabilize */
    pthread_cond_destroy (&sim_timer_startup_cond);
    sim_timer_thread_running = TRUE;
    }
pthread_mutex_unlock (&sim_timer_lock);
#endif
}

void sim_stop_timer_services (void)
{
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_CLOCKS)
pthread_mutex_lock (&sim_timer_lock);
if (sim_timer_thread_running) {
    sim_debug (DBG_TRC, &sim_timer_dev, "sim_stop_timer_services() - stopping\n");
    pthread_cond_signal (&sim_timer_wake);
    pthread_mutex_unlock (&sim_timer_lock);
    pthread_join (sim_timer_thread, NULL);
    sim_timer_thread_running = FALSE;
    }
else
    pthread_mutex_unlock (&sim_timer_lock);
#endif
}

t_stat sim_timer_change_asynch (void)
{
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_CLOCKS)
if (sim_asynch_enabled && sim_asynch_timer)
    sim_start_timer_services ();
else {
    UNIT *uptr;
    int32 accum = 0;

    sim_stop_timer_services ();
    while (1) {
        uptr = sim_wallclock_queue;
        if (uptr == QUEUE_LIST_END)
            break;
        sim_wallclock_queue = uptr->a_next;
        accum += uptr->time;
        uptr->a_next = NULL;
        uptr->a_due_time = 0;
        uptr->a_usec_delay = 0;
        sim_activate_after (uptr, accum);
        }
    }
#endif
return SCPE_OK;
}

/* Instruction Execution rate. */
/*  returns a double since it is mostly used in double expressions and
    to avoid overflow if/when strange timing delays might produce unexpected results */

double sim_timer_inst_per_sec (void)
{
double inst_per_sec = SIM_INITIAL_IPS;

if (sim_calb_tmr == -1)
    return inst_per_sec;
inst_per_sec = ((double)rtc_currd[sim_calb_tmr])*rtc_hz[sim_calb_tmr];
if (0 == inst_per_sec)
    inst_per_sec = SIM_INITIAL_IPS;
return inst_per_sec;
}

t_stat sim_timer_activate_after (UNIT *uptr, int32 usec_delay)
{
int32 inst_delay;
double inst_per_sec;

AIO_VALIDATE;
if (sim_is_active (uptr))                               /* already active? */
    return SCPE_OK;
inst_per_sec = sim_timer_inst_per_sec ();
inst_delay = (int32)((inst_per_sec*usec_delay)/1000000.0);
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_CLOCKS)
if ((sim_calb_tmr == -1) ||                             /* if No timer initialized */
    (inst_delay < rtc_currd[sim_calb_tmr]) ||           /*    or sooner than next clock tick? */
    (rtc_elapsed[sim_calb_tmr] < sim_idle_stable) ||    /*    or not idle stable yet */
    (!(sim_asynch_enabled && sim_asynch_timer))) {      /*    or asynch disabled */
    sim_debug (DBG_TIM, &sim_timer_dev, "sim_timer_activate_after() - activating %s after %d instructions\n", 
               sim_uname(uptr), inst_delay);
    return _sim_activate (uptr, inst_delay);            /* queue it now */
    }
if (1) {
    struct timespec now;
    double d_now;

    clock_gettime (CLOCK_REALTIME, &now);
    d_now = _timespec_to_double (&now);
    /* Determine if this is a clock tick like invocation 
       or an ocaisional measured device delay */
    if ((uptr->a_usec_delay == usec_delay) &&
        (uptr->a_due_time != 0.0)          &&
        (1)) {
        double d_delay = ((double)usec_delay)/1000000.0;

        uptr->a_due_time += d_delay;
        if (uptr->a_due_time < (d_now + d_delay*0.1)) { /* Accumulate lost time */
            uptr->a_skew += (d_now + d_delay*0.1) - uptr->a_due_time;
            uptr->a_due_time = d_now + d_delay/10.0;
            if (uptr->a_skew > 30.0) { /* Gap too big? */
                uptr->a_usec_delay = usec_delay;
                uptr->a_skew = uptr->a_last_fired_time = 0.0;
                uptr->a_due_time = d_now + (double)(usec_delay)/1000000.0;
                }
            if (uptr->a_skew > rtc_clock_skew_max[sim_calb_tmr])
                rtc_clock_skew_max[sim_calb_tmr] = uptr->a_skew;
            }
        else {
            if (uptr->a_skew > 0.0) { /* Lost time to make up? */
                if (uptr->a_skew > d_delay*0.9) {
                    uptr->a_skew -= d_delay*0.9;
                    uptr->a_due_time -= d_delay*0.9;
                    }
                else {
                    uptr->a_due_time -= uptr->a_skew;
                    uptr->a_skew = 0.0;
                    }
                }
            }
        }
    else {
        uptr->a_usec_delay = usec_delay;
        uptr->a_skew = uptr->a_last_fired_time = 0.0;
        uptr->a_due_time = d_now + (double)(usec_delay)/1000000.0;
        }
    uptr->time = usec_delay;

    sim_debug (DBG_TIM, &sim_timer_dev, "sim_timer_activate_after() - queue addition %s at %.6f\n", 
               sim_uname(uptr), uptr->a_due_time);
    }
pthread_mutex_lock (&sim_timer_lock);
while (sim_wallclock_entry) {
    sim_debug (DBG_TIM, &sim_timer_dev, "sim_timer_activate_after() - queue insert entry %s busy waiting for 1ms\n", 
               sim_uname(sim_wallclock_entry));
    pthread_mutex_unlock (&sim_timer_lock);
    sim_os_ms_sleep (1);
    pthread_mutex_lock (&sim_timer_lock);
    }
sim_wallclock_entry = uptr;
pthread_mutex_unlock (&sim_timer_lock);
pthread_cond_signal (&sim_timer_wake);                  /* wake the timer thread to deal with it */
return SCPE_OK;
#else
return _sim_activate (uptr, inst_delay);                /* queue it now */
#endif
}

/* Clock coscheduling routines */

t_stat sim_register_clock_unit (UNIT *uptr)
{
sim_clock_unit = uptr;
return SCPE_OK;
}

t_stat sim_clock_coschedule (UNIT *uptr, int32 interval)
{
if (NULL == sim_clock_unit)
    return sim_activate (uptr, interval);
else
    if (sim_asynch_enabled && sim_asynch_timer) {
        if (!sim_is_active (uptr)) {               /* already active? */
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_CLOCKS)
            if ((sim_calb_tmr != -1) &&
                (rtc_elapsed[sim_calb_tmr ] >= sim_idle_stable))  {
                sim_debug (DBG_TIM, &sim_timer_dev, "sim_clock_coschedule() - queueing %s for clock co-schedule\n", sim_uname (uptr));
                pthread_mutex_lock (&sim_timer_lock);
                uptr->a_next = sim_clock_cosched_queue;
                sim_clock_cosched_queue = uptr;
                pthread_mutex_unlock (&sim_timer_lock);
                return SCPE_OK;
                }
            else {
#else
            if (1) {
#endif
                int32 t;

                t = sim_activate_time (sim_clock_unit);
                return sim_activate (uptr, t? t - 1: interval);
                }
            }
        sim_debug (DBG_TIM, &sim_timer_dev, "sim_clock_coschedule() - %s is already active\n", sim_uname (uptr));
        return SCPE_OK;
        }
    else {
        int32 t;

        t = sim_activate_time (sim_clock_unit);
        return sim_activate (uptr, t? t - 1: interval);
        }
}

