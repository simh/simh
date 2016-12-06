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

#define SIM_INTERNAL_CLK (SIM_NTIMERS+(1<<30))
#define SIM_INTERNAL_UNIT sim_internal_timer_unit
#ifndef MIN
#define MIN(a,b)  (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b)  (((a) > (b)) ? (a) : (b))
#endif

uint32 sim_idle_ms_sleep (unsigned int msec);

/* MS_MIN_GRANULARITY exists here so that timing behavior for hosts systems  */
/* with slow clock ticks can be assessed and tested without actually having  */
/* that slow a clock tick on the development platform                        */
//#define MS_MIN_GRANULARITY 20   /* Uncomment to simulate 20ms host tick size.*/
                                /* some Solaris and BSD hosts come this way  */

#if defined(MS_MIN_GRANULARITY) && (MS_MIN_GRANULARITY != 1)
uint32 real_sim_idle_ms_sleep (unsigned int msec);
uint32 real_sim_os_msec (void);
uint32 real_sim_os_ms_sleep (unsigned int msec);
static uint32 real_sim_os_sleep_min_ms = 0;
static uint32 real_sim_os_sleep_inc_ms = 0;

uint32 sim_idle_ms_sleep (unsigned int msec)
{
uint32 real_start = real_sim_os_msec ();
uint32 start = (real_start / MS_MIN_GRANULARITY) * MS_MIN_GRANULARITY;
uint32 tick_left;

if (msec == 0)
    return 0;
if (real_start == start)
    tick_left = 0;
else
    tick_left = MS_MIN_GRANULARITY - (real_start - start);
if (msec <= tick_left)
    real_sim_idle_ms_sleep (tick_left);
else
    real_sim_idle_ms_sleep (((msec + MS_MIN_GRANULARITY - 1) / MS_MIN_GRANULARITY) * MS_MIN_GRANULARITY);

return (sim_os_msec () - start);
}

uint32 sim_os_msec (void)
{
return (real_sim_os_msec ()/MS_MIN_GRANULARITY)*MS_MIN_GRANULARITY;
}

uint32 sim_os_ms_sleep (unsigned int msec)
{
msec = MS_MIN_GRANULARITY*((msec+MS_MIN_GRANULARITY-1)/MS_MIN_GRANULARITY);

return real_sim_os_ms_sleep (msec);
}

#endif /* defined(MS_MIN_GRANULARITY) && (MS_MIN_GRANULARITY != 1) */

t_bool sim_idle_enab = FALSE;                       /* global flag */
volatile t_bool sim_idle_wait = FALSE;              /* global flag */

static int32 sim_calb_tmr = -1;                     /* the system calibrated timer */
static int32 sim_calb_tmr_last = -1;                /* shadow value when at sim> prompt */
static double sim_inst_per_sec_last = 0;            /* shadow value when at sim> prompt */

static uint32 sim_idle_rate_ms = 0;
static uint32 sim_os_sleep_min_ms = 0;
static uint32 sim_os_sleep_inc_ms = 0;
static uint32 sim_os_clock_resoluton_ms = 0;
static uint32 sim_os_tick_hz = 0;
static uint32 sim_idle_stable = SIM_IDLE_STDFLT;
static uint32 sim_idle_calib_pct = 0;
static uint32 sim_throt_ms_start = 0;
static uint32 sim_throt_ms_stop = 0;
static uint32 sim_throt_type = 0;
static uint32 sim_throt_val = 0;
static uint32 sim_throt_state = 0;
static double sim_throt_cps;
static double sim_throt_inst_start;
static uint32 sim_throt_sleep_time = 0;
static int32 sim_throt_wait = 0;
static UNIT *sim_clock_unit[SIM_NTIMERS+1] = {NULL};
UNIT * volatile sim_clock_cosched_queue[SIM_NTIMERS+1] = {NULL};
static int32 sim_cosched_interval[SIM_NTIMERS+1];
static t_bool sim_catchup_ticks = FALSE;
#if defined (SIM_ASYNCH_CLOCKS) && !defined (SIM_ASYNCH_IO)
#undef SIM_ASYNCH_CLOCKS
#endif
t_bool sim_asynch_timer = FALSE;

#if defined (SIM_ASYNCH_CLOCKS)
UNIT * volatile sim_wallclock_queue = QUEUE_LIST_END;
UNIT * volatile sim_wallclock_entry = NULL;
#endif

#define sleep1Samples       100

static uint32 _compute_minimum_sleep (void)
{
uint32 i, tot, tim;

sim_os_set_thread_priority (PRIORITY_ABOVE_NORMAL);
#if defined(MS_MIN_GRANULARITY) && (MS_MIN_GRANULARITY != 1)
real_sim_idle_ms_sleep (1);         /* Start sampling on a tick boundary */
for (i = 0, tot = 0; i < sleep1Samples; i++)
    tot += real_sim_idle_ms_sleep (1);
tim = tot / sleep1Samples;          /* Truncated average */
real_sim_os_sleep_min_ms = tim;
real_sim_idle_ms_sleep (1);         /* Start sampling on a tick boundary */
for (i = 0, tot = 0; i < sleep1Samples; i++)
    tot += real_sim_idle_ms_sleep (real_sim_os_sleep_min_ms + 1);
tim = tot / sleep1Samples;          /* Truncated average */
real_sim_os_sleep_inc_ms = tim - real_sim_os_sleep_min_ms;
#endif /* defined(MS_MIN_GRANULARITY) && (MS_MIN_GRANULARITY != 1) */
sim_idle_ms_sleep (1);              /* Start sampling on a tick boundary */
for (i = 0, tot = 0; i < sleep1Samples; i++)
    tot += sim_idle_ms_sleep (1);
tim = tot / sleep1Samples;          /* Truncated average */
sim_os_sleep_min_ms = tim;
sim_idle_ms_sleep (1);              /* Start sampling on a tick boundary */
for (i = 0, tot = 0; i < sleep1Samples; i++)
    tot += sim_idle_ms_sleep (sim_os_sleep_min_ms + 1);
tim = tot / sleep1Samples;          /* Truncated average */
sim_os_sleep_inc_ms = tim - sim_os_sleep_min_ms;
sim_os_set_thread_priority (PRIORITY_NORMAL);
return sim_os_sleep_min_ms;
}

#if defined(MS_MIN_GRANULARITY) && (MS_MIN_GRANULARITY != 1)

#define sim_idle_ms_sleep   real_sim_idle_ms_sleep 
#define sim_os_msec         real_sim_os_msec 
#define sim_os_ms_sleep     real_sim_os_ms_sleep

#endif /* defined(MS_MIN_GRANULARITY) && (MS_MIN_GRANULARITY != 1) */

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
#else
uint32 sim_idle_ms_sleep (unsigned int msec)
{
return sim_os_ms_sleep (msec);
}
#endif

/* Mark the need for the sim_os_set_thread_priority routine, */
/* allowing the feature and/or platform dependent code to provide it */
#define NEED_THREAD_PRIORITY

/* If we've got pthreads support then use pthreads mechanisms */
#if defined(USE_READER_THREAD)

#undef NEED_THREAD_PRIORITY

#if defined(_WIN32)
/* On Windows there are several potentially disjoint threading APIs */
/* in use (base win32 pthreads, libSDL provided threading, and direct */
/* calls to beginthreadex), so go directly to the Win32 threading APIs */
/* to manage thread priority */
t_stat sim_os_set_thread_priority (int below_normal_above)
{
const static int val[3] = {THREAD_PRIORITY_BELOW_NORMAL, THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL};

if ((below_normal_above < -1) || (below_normal_above > 1))
    return SCPE_ARG;
SetThreadPriority (GetCurrentThread(), val[1 + below_normal_above]);
return SCPE_OK;
}
#else
/* Native pthreads priority implementation */
t_stat sim_os_set_thread_priority (int below_normal_above)
{
int sched_policy, min_prio, max_prio;
struct sched_param sched_priority;

if ((below_normal_above < -1) || (below_normal_above > 1))
    return SCPE_ARG;

pthread_getschedparam (pthread_self(), &sched_policy, &sched_priority);
min_prio = sched_get_priority_min(sched_policy);
max_prio = sched_get_priority_max(sched_policy);
switch (below_normal_above) {
    case PRIORITY_BELOW_NORMAL:
        sched_priority.sched_priority = min_prio;
        break;
    case PRIORITY_NORMAL:
        sched_priority.sched_priority = (max_prio + min_prio) / 2;
        break;
    case PRIORITY_ABOVE_NORMAL:
        sched_priority.sched_priority = max_prio;
        break;
    }
pthread_setschedparam (pthread_self(), sched_policy, &sched_priority);
return SCPE_OK;
}
#endif
#endif  /* defined(USE_READER_THREAD) */

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
return _compute_minimum_sleep ();
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

const t_bool rtc_avail = TRUE;

uint32 sim_os_msec (void)
{
return timeGetTime ();
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
if (timers.wPeriodMin == 0)
    return 0;
if (timeBeginPeriod (timers.wPeriodMin) != TIMERR_NOERROR)
    return 0;
atexit (sim_timer_exit);
/* return measured actual minimum sleep time */
return _compute_minimum_sleep ();
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
return _compute_minimum_sleep ();
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

if (clk_id != CLOCK_REALTIME)
  return -1;
gettimeofday (&cur, NULL);
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
return _compute_minimum_sleep ();
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

#if defined(NEED_THREAD_PRIORITY)
#undef NEED_THREAD_PRIORITY
#include <sys/time.h>
#include <sys/resource.h>

t_stat sim_os_set_thread_priority (int below_normal_above)
{
if ((below_normal_above < -1) || (below_normal_above > 1))
    return SCPE_ARG;

errno = 0;
switch (below_normal_above) {
    case PRIORITY_BELOW_NORMAL:
        if ((getpriority (PRIO_PROCESS, 0) <= 0) &&     /* at or above normal pri? */
            (errno == 0))
            setpriority (PRIO_PROCESS, 0, 10);
        break;
    case PRIORITY_NORMAL:
        if (getpriority (PRIO_PROCESS, 0) != 0)         /* at or above normal pri? */
            setpriority (PRIO_PROCESS, 0, 0);
        break;
    case PRIORITY_ABOVE_NORMAL:
        if ((getpriority (PRIO_PROCESS, 0) <= 0) &&     /* at or above normal pri? */
            (errno == 0))
            setpriority (PRIO_PROCESS, 0, -10);
        break;
    }
return SCPE_OK;
}
#endif  /* defined(NEED_THREAD_PRIORITY) */

#endif

/* If one hasn't been provided yet, then just stub it */
#if defined(NEED_THREAD_PRIORITY)
t_stat sim_os_set_thread_priority (int below_normal_above)
{
return SCPE_OK;
}
#endif

#if defined(MS_MIN_GRANULARITY) && (MS_MIN_GRANULARITY != 1)
/* Make sure to use the substitute routines */
#undef sim_idle_ms_sleep
#undef sim_os_msec
#undef sim_os_ms_sleep
#endif /* defined(MS_MIN_GRANULARITY) && (MS_MIN_GRANULARITY != 1) */

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

/* Forward declarations */

static double _timespec_to_double (struct timespec *time);
static void _double_to_timespec (struct timespec *time, double dtime);
static t_bool _rtcn_tick_catchup_check (int32 tmr, int32 time);
static void _rtcn_configure_calibrated_clock (int32 newtmr);
static void _sim_coschedule_cancel(UNIT *uptr);
static void _sim_wallclock_cancel (UNIT *uptr);
static t_bool _sim_wallclock_is_active (UNIT *uptr);

#if defined(SIM_ASYNCH_CLOCKS)
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
#endif /* defined(SIM_ASYNCH_CLOCKS) */

/* OS independent clock calibration package */

static int32 rtc_ticks[SIM_NTIMERS+1] = { 0 };            /* ticks */
static uint32 rtc_hz[SIM_NTIMERS+1] = { 0 };              /* tick rate */
static uint32 rtc_rtime[SIM_NTIMERS+1] = { 0 };           /* real time */
static uint32 rtc_vtime[SIM_NTIMERS+1] = { 0 };           /* virtual time */
static double rtc_gtime[SIM_NTIMERS+1] = { 0 };           /* instruction time */
static uint32 rtc_nxintv[SIM_NTIMERS+1] = { 0 };          /* next interval */
static int32 rtc_based[SIM_NTIMERS+1] = { 0 };            /* base delay */
static int32 rtc_currd[SIM_NTIMERS+1] = { 0 };            /* current delay */
static int32 rtc_initd[SIM_NTIMERS+1] = { 0 };            /* initial delay */
static uint32 rtc_elapsed[SIM_NTIMERS+1] = { 0 };         /* sec since init */
static uint32 rtc_calibrations[SIM_NTIMERS+1] = { 0 };    /* calibration count */
static double rtc_clock_skew_max[SIM_NTIMERS+1] = { 0 };  /* asynchronous max skew */
static double rtc_clock_start_gtime[SIM_NTIMERS+1] = { 0 };/* reference instruction time for clock */
static double rtc_clock_tick_size[SIM_NTIMERS+1] = { 0 }; /* 1/hz */
static uint32 rtc_calib_initializations[SIM_NTIMERS+1] = { 0 };/* Initialization Count */
static double rtc_calib_tick_time[SIM_NTIMERS+1] = { 0 }; /* ticks time */
static double rtc_calib_tick_time_tot[SIM_NTIMERS+1] = { 0 };/* ticks time - total*/
static uint32 rtc_calib_ticks_acked[SIM_NTIMERS+1] = { 0 };/* ticks Acked */
static uint32 rtc_calib_ticks_acked_tot[SIM_NTIMERS+1] = { 0 };/* ticks Acked - total */
static uint32 rtc_clock_ticks[SIM_NTIMERS+1] = { 0 };/* ticks delivered since catchup base */
static uint32 rtc_clock_ticks_tot[SIM_NTIMERS+1] = { 0 };/* ticks delivered since catchup base - total */
static double rtc_clock_catchup_base_time[SIM_NTIMERS+1] = { 0 };/* reference time for catchup ticks */
static uint32 rtc_clock_catchup_ticks[SIM_NTIMERS+1] = { 0 };/* Record of catchups */
static uint32 rtc_clock_catchup_ticks_tot[SIM_NTIMERS+1] = { 0 };/* Record of catchups - total */
static t_bool rtc_clock_catchup_pending[SIM_NTIMERS+1] = { 0 };/* clock tick catchup pending */
static t_bool rtc_clock_catchup_eligible[SIM_NTIMERS+1] = { 0 };/* clock tick catchup eligible */
static uint32 rtc_clock_time_idled[SIM_NTIMERS+1] = { 0 };/* total time idled */
static uint32 rtc_clock_time_idled_last[SIM_NTIMERS+1] = { 0 };/* total time idled */
static uint32 rtc_clock_calib_skip_idle[SIM_NTIMERS+1] = { 0 };/* Calibrations skipped due to idling */
static uint32 rtc_clock_calib_gap2big[SIM_NTIMERS+1] = { 0 };/* Calibrations skipped Gap Too Big */
static uint32 rtc_clock_calib_backwards[SIM_NTIMERS+1] = { 0 };/* Calibrations skipped Clock Running Backwards */

UNIT sim_timer_units[SIM_NTIMERS+1];                    /* one for each timer and one for an */
                                                        /* internal clock if no clocks are registered */
UNIT sim_internal_timer_unit;                           /* Internal calibration timer */
UNIT sim_throttle_unit;                                 /* one for throttle */

t_stat sim_throt_svc (UNIT *uptr);
t_stat sim_timer_tick_svc (UNIT *uptr);

#define DBG_IDL       TIMER_DBG_IDLE        /* idling */
#define DBG_QUE       TIMER_DBG_QUEUE       /* queue activities */
#define DBG_MUX       TIMER_DBG_MUX         /* tmxr queue activities */
#define DBG_TRC       0x008                 /* tracing */
#define DBG_CAL       0x010                 /* calibration activities */
#define DBG_TIM       0x020                 /* timer thread activities */
#define DBG_THR       0x040                 /* throttle activities */
#define DBG_ACK       0x080                 /* interrupt acknowledgement activities */
DEBTAB sim_timer_debug[] = {
  {"TRACE",   DBG_TRC, "Trace routine calls"},
  {"IDLE",    DBG_IDL, "Idling activities"},
  {"QUEUE",   DBG_QUE, "Event queuing activities"},
  {"IACK",    DBG_ACK, "interrupt acknowledgement activities"},
  {"CALIB",   DBG_CAL, "Calibration activities"},
  {"TIME",    DBG_TIM, "Activation and scheduling activities"},
  {"THROT",   DBG_THR, "Throttling activities"},
  {"MUX",     DBG_MUX, "Tmxr scheduling activities"},
  {0}
};

/* Forward device declarations */
extern DEVICE sim_timer_dev;
extern DEVICE sim_throttle_dev;


void sim_rtcn_init_all (void)
{
int32 tmr;

for (tmr = 0; tmr <= SIM_NTIMERS; tmr++)
    if (rtc_initd[tmr] != 0)
        sim_rtcn_init (rtc_initd[tmr], tmr);
return;
}

int32 sim_rtcn_init (int32 time, int32 tmr)
{
return sim_rtcn_init_unit (NULL, time, tmr);
}

int32 sim_rtcn_init_unit (UNIT *uptr, int32 time, int32 tmr)
{
if (time == 0)
    time = 1;
if (tmr == SIM_INTERNAL_CLK)
    tmr = SIM_NTIMERS;
else {
    if ((tmr < 0) || (tmr >= SIM_NTIMERS))
        return time;
    }
/*
 * If we'd previously succeeded in calibrating a tick value, then use that
 * delay as a better default to setup when we're re-initialized.
 * Re-initializing happens on any boot or after any breakpoint/continue.
 */
if (rtc_currd[tmr])
    time = rtc_currd[tmr];
if (!uptr)
    uptr = sim_clock_unit[tmr];
sim_debug (DBG_CAL, &sim_timer_dev, "_sim_rtcn_init_unit(unit=%s, time=%d, tmr=%d)\n", sim_uname(uptr), time, tmr);
if (uptr) {
    if (!sim_clock_unit[tmr])
        sim_register_clock_unit_tmr (uptr, tmr);
    }
rtc_clock_start_gtime[tmr] = sim_gtime();
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
rtc_clock_ticks_tot[tmr] += rtc_clock_ticks[tmr];
rtc_clock_ticks[tmr] = 0;
rtc_calib_tick_time_tot[tmr] += rtc_calib_tick_time[tmr];
rtc_calib_tick_time[tmr] = 0;
rtc_clock_catchup_pending[tmr] = FALSE;
rtc_clock_catchup_eligible[tmr] = FALSE;
rtc_clock_catchup_ticks_tot[tmr] += rtc_clock_catchup_ticks[tmr];
rtc_clock_catchup_ticks[tmr] = 0;
rtc_calib_ticks_acked_tot[tmr] += rtc_calib_ticks_acked[tmr];
rtc_calib_ticks_acked[tmr] = 0;
++rtc_calib_initializations[tmr];
_rtcn_configure_calibrated_clock (tmr);
return time;
}

int32 sim_rtcn_calb (int32 ticksper, int32 tmr)
{
uint32 new_rtime, delta_rtime, last_idle_pct;
int32 delta_vtime;
double new_gtime;
int32 new_currd;

if (tmr == SIM_INTERNAL_CLK)
    tmr = SIM_NTIMERS;
else {
    if ((tmr < 0) || (tmr >= SIM_NTIMERS))
        return 10000;
    }
if (rtc_hz[tmr] != ticksper) {                          /* changing tick rate? */
    rtc_hz[tmr] = ticksper;
    rtc_clock_tick_size[tmr] = 1.0/ticksper;
    _rtcn_configure_calibrated_clock (tmr);
    rtc_currd[tmr] = (int32)(sim_timer_inst_per_sec()/ticksper);
    }
if (sim_clock_unit[tmr] == NULL) {                      /* Not using TIMER units? */
    rtc_clock_ticks[tmr] += 1;
    rtc_calib_tick_time[tmr] += rtc_clock_tick_size[tmr];
    }
if (rtc_clock_catchup_pending[tmr]) {                   /* catchup tick? */
    ++rtc_clock_catchup_ticks[tmr];                     /* accumulating which were catchups */
    rtc_clock_catchup_pending[tmr] = FALSE;
    if (!sim_asynch_timer)                              /* non asynch timers? */
        return rtc_currd[tmr];                          /* return now avoiding counting catchup tick in calibration */
    }
rtc_ticks[tmr] = rtc_ticks[tmr] + 1;                    /* count ticks */
if (rtc_ticks[tmr] < ticksper) {                        /* 1 sec yet? */
    return rtc_currd[tmr];
    }
rtc_ticks[tmr] = 0;                                     /* reset ticks */
rtc_elapsed[tmr] = rtc_elapsed[tmr] + 1;                /* count sec */
if (sim_throt_type != SIM_THROT_NONE) {
    rtc_gtime[tmr] = sim_gtime();                       /* save instruction time */
    rtc_currd[tmr] = (int32)(sim_throt_cps / ticksper); /* use throttle calibration */
    ++rtc_calibrations[tmr];                            /* count calibrations */
    sim_debug (DBG_CAL, &sim_timer_dev, "using throttle calibrated value - result: %d\n", rtc_currd[tmr]);
    return rtc_currd[tmr];
    }
if (!rtc_avail) {                                       /* no timer? */
    return rtc_currd[tmr];
    }
if (sim_calb_tmr != tmr) {
    rtc_currd[tmr] = (int32)(sim_timer_inst_per_sec()/ticksper);
    sim_debug (DBG_CAL, &sim_timer_dev, "calibrated calibrated tmr=%d against system tmr=%d, tickper=%d (result: %d)\n", tmr, sim_calb_tmr, ticksper, rtc_currd[tmr]);
    return rtc_currd[tmr];
    }
new_rtime = sim_os_msec ();                             /* wall time */
++rtc_calibrations[tmr];                                /* count calibrations */
sim_debug (DBG_TRC, &sim_timer_dev, "sim_rtcn_calb(ticksper=%d, tmr=%d)\n", ticksper, tmr);
if (new_rtime < rtc_rtime[tmr]) {                       /* time running backwards? */
    /* This happens when the value returned by sim_os_msec wraps (as an uint32) */
    /* Wrapping will happen initially sometime before a simulator has been running */
    /* for 49 days approximately every 49 days thereafter. */
    ++rtc_clock_calib_backwards[tmr];                   /* Count statistic */
    sim_debug (DBG_CAL, &sim_timer_dev, "time running backwards - OldTime: %u, NewTime: %u, result: %d\n", rtc_rtime[tmr], new_rtime, rtc_currd[tmr]);
    rtc_rtime[tmr] = new_rtime;                         /* reset wall time */
    return rtc_currd[tmr];                              /* can't calibrate */
    }
delta_rtime = new_rtime - rtc_rtime[tmr];               /* elapsed wtime */
rtc_rtime[tmr] = new_rtime;                             /* adv wall time */
rtc_vtime[tmr] = rtc_vtime[tmr] + 1000;                 /* adv sim time */
if (delta_rtime > 30000) {                              /* gap too big? */
    /* This simulator process has somehow been suspended for a significant */
    /* amount of time.  This will certainly happen if the host system has  */
    /* slept or hibernated.  It also might happen when a simulator         */
    /* developer stops the simulator at a breakpoint (a process, not simh  */
    /* breakpoint).  To accomodate this, we set the calibration state to   */
    /* ignore what happened and proceed from here.                         */
    ++rtc_clock_calib_gap2big[tmr];                     /* Count statistic */
    rtc_vtime[tmr] = rtc_rtime[tmr];                    /* sync virtual and real time */
    rtc_nxintv[tmr] = 1000;                             /* reset next interval */
    rtc_gtime[tmr] = sim_gtime();                       /* save instruction time */
    sim_debug (DBG_CAL, &sim_timer_dev, "gap too big: delta = %d - result: %d\n", delta_rtime, rtc_currd[tmr]);
    return rtc_currd[tmr];                              /* can't calibr */
    }
if (delta_rtime == 0)                                   /* avoid divide by zero  */
    last_idle_pct = 0;                                  /* force calibration */
else
    last_idle_pct = MIN(100, (uint32)(100.0 * (((double)(rtc_clock_time_idled[tmr] - rtc_clock_time_idled_last[tmr])) / ((double)delta_rtime))));
rtc_clock_time_idled_last[tmr] = rtc_clock_time_idled[tmr];
if (last_idle_pct > (100 - sim_idle_calib_pct)) {
    rtc_rtime[tmr] = new_rtime;                         /* save wall time */
    rtc_vtime[tmr] = rtc_vtime[tmr] + 1000;             /* adv sim time */
    rtc_gtime[tmr] = sim_gtime();                       /* save instruction time */
    ++rtc_clock_calib_skip_idle[tmr];
    sim_debug (DBG_CAL, &sim_timer_dev, "skipping calibration due to idling (%d%%) - result: %d\n", last_idle_pct, rtc_currd[tmr]);
    return rtc_currd[tmr];                              /* avoid calibrating idle checks */
    }
new_gtime = sim_gtime();
if (sim_asynch_timer) {
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
    sim_debug (DBG_CAL, &sim_timer_dev, "asynch calibration result: %d\n", rtc_currd[tmr]);
    return rtc_currd[tmr];                          /* calibrated result */
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
sim_debug (DBG_CAL, &sim_timer_dev, "calibrated tmr=%d, tickper=%d (base=%d, nxintv=%u, result: %d)\n", tmr, ticksper, rtc_based[tmr], rtc_nxintv[tmr], rtc_currd[tmr]);
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
int tmr;
uint32 clock_start, clock_last, clock_now;

sim_debug (DBG_TRC, &sim_timer_dev, "sim_timer_init()\n");
for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    sim_timer_units[tmr].action = &sim_timer_tick_svc;
    sim_timer_units[tmr].flags = UNIT_DIS | UNIT_IDLE;
    }
SIM_INTERNAL_UNIT.flags = UNIT_DIS | UNIT_IDLE;
sim_register_internal_device (&sim_timer_dev);
sim_throttle_unit.action = &sim_throt_svc;
sim_throttle_unit.flags = UNIT_DIS;
sim_register_internal_device (&sim_throttle_dev);
sim_register_clock_unit_tmr (&SIM_INTERNAL_UNIT, SIM_INTERNAL_CLK);
sim_idle_enab = FALSE;                                  /* init idle off */
sim_idle_rate_ms = sim_os_ms_sleep_init ();             /* get OS timer rate */

clock_last = clock_start = sim_os_msec ();
sim_os_clock_resoluton_ms = 1000;
do {
    uint32 clock_diff;
    
    clock_now = sim_os_msec ();
    clock_diff = clock_now - clock_last;
    if ((clock_diff > 0) && (clock_diff < sim_os_clock_resoluton_ms))
        sim_os_clock_resoluton_ms = clock_diff;
    clock_last = clock_now;
    } while (clock_now < clock_start + 100);
sim_os_tick_hz = 1000/(sim_os_clock_resoluton_ms * (sim_idle_rate_ms/sim_os_clock_resoluton_ms));
return (sim_idle_rate_ms != 0);
}

/* sim_timer_idle_capable - tell if the host is Idle capable and what the host OS tick size is */

t_bool sim_timer_idle_capable (uint32 *host_ms_sleep_1, uint32 *host_tick_ms)
{
if (host_tick_ms)
    *host_tick_ms = sim_os_clock_resoluton_ms;
if (host_ms_sleep_1)
    *host_ms_sleep_1 = sim_os_sleep_min_ms;
return (sim_idle_rate_ms != 0);
}

/* sim_show_timers - show running timer information */
t_stat sim_show_timers (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, CONST char* desc)
{
int tmr, clocks;
struct timespec now;
time_t time_t_now;
int32 calb_tmr = (sim_calb_tmr == -1) ? sim_calb_tmr_last : sim_calb_tmr;
double inst_per_sec = (sim_calb_tmr == -1) ? sim_inst_per_sec_last : sim_timer_inst_per_sec ();

fprintf (st, "Minimum Host Sleep Time:       %d ms (%dHz)\n", sim_os_sleep_min_ms, sim_os_tick_hz);
if (sim_os_sleep_min_ms != sim_os_sleep_inc_ms)
    fprintf (st, "Minimum Host Sleep Incr Time:  %d ms\n", sim_os_sleep_inc_ms);
fprintf (st, "Host Clock Resolution:         %d ms\n", sim_os_clock_resoluton_ms);
if (sim_idle_enab)
    fprintf (st, "Time before Idling starts:     %d seconds\n", sim_idle_stable);
fprintf (st, "Execution Rate:                %.0f instructions/sec\n", inst_per_sec);
fprintf (st, "Calibrated Timer:              %s\n", (calb_tmr == -1) ? "Undetermined" : 
                                                    ((calb_tmr == SIM_NTIMERS) ? "Internal Timer" : 
                                                    (sim_clock_unit[calb_tmr] ? sim_uname(sim_clock_unit[calb_tmr]) : "")));
fprintf (st, "\n");
for (tmr=clocks=0; tmr<=SIM_NTIMERS; ++tmr) {
    if (0 == rtc_initd[tmr])
        continue;
    
    if (sim_clock_unit[tmr]) {
        ++clocks;
        fprintf (st, "%s clock device is %s%s%s\n", sim_name, 
                                                    (tmr == SIM_NTIMERS) ? "Internal Calibrated Timer(" : "", 
                                                    sim_uname(sim_clock_unit[tmr]), 
                                                    (tmr == SIM_NTIMERS) ? ")" : "");
        }

    fprintf (st, "%s%sTimer %d:\n", sim_asynch_timer ? "Asynchronous " : "", rtc_hz[tmr] ? "Calibrated " : "Uncalibrated ", tmr);
    if (rtc_hz[tmr]) {
        fprintf (st, "  Running at:                %d Hz\n", rtc_hz[tmr]);
        fprintf (st, "  Tick Size:                 %s\n", sim_fmt_secs (rtc_clock_tick_size[tmr]));
        fprintf (st, "  Ticks in current second:   %d\n",   rtc_ticks[tmr]);
        }
    fprintf (st, "  Seconds Running:           %u (%s)\n",   rtc_elapsed[tmr], sim_fmt_secs ((double)rtc_elapsed[tmr]));
    if (tmr == calb_tmr) {
        fprintf (st, "  Calibration Opportunities: %u\n",   rtc_calibrations[tmr]);
        if (sim_idle_calib_pct)
            fprintf (st, "  Calib Skip Idle Thresh %%:  %u\n",   sim_idle_calib_pct);
        if (rtc_clock_calib_skip_idle[tmr])
            fprintf (st, "  Calibs Skip While Idle:    %u\n",   rtc_clock_calib_skip_idle[tmr]);
        if (rtc_clock_calib_backwards[tmr])
            fprintf (st, "  Calibs Skip Backwards:     %u\n",   rtc_clock_calib_backwards[tmr]);
        if (rtc_clock_calib_gap2big[tmr])
            fprintf (st, "  Calibs Skip Gap Too Big:   %u\n",   rtc_clock_calib_gap2big[tmr]);
        }
    if (rtc_gtime[tmr])
        fprintf (st, "  Instruction Time:          %.0f\n", rtc_gtime[tmr]);
    if ((!sim_asynch_timer) && (sim_throt_type == SIM_THROT_NONE)) {
        fprintf (st, "  Real Time:                 %u\n",   rtc_rtime[tmr]);
        fprintf (st, "  Virtual Time:              %u\n",   rtc_vtime[tmr]);
        fprintf (st, "  Next Interval:             %u\n",   rtc_nxintv[tmr]);
        fprintf (st, "  Base Tick Delay:           %d\n",   rtc_based[tmr]);
        fprintf (st, "  Initial Insts Per Tick:    %d\n",   rtc_initd[tmr]);
        }
    fprintf (st, "  Current Insts Per Tick:    %d\n",   rtc_currd[tmr]);
    fprintf (st, "  Initializations:           %d\n",   rtc_calib_initializations[tmr]);
    fprintf (st, "  Total Ticks:               %u\n", rtc_clock_ticks_tot[tmr]+rtc_clock_ticks[tmr]);
    if (rtc_clock_skew_max[tmr] != 0.0)
        fprintf (st, "  Peak Clock Skew:           %s%s\n", sim_fmt_secs (fabs(rtc_clock_skew_max[tmr])), (rtc_clock_skew_max[tmr] < 0) ? " fast" : " slow");
    if (rtc_calib_ticks_acked[tmr])
        fprintf (st, "  Ticks Acked:               %u\n",   rtc_calib_ticks_acked[tmr]);
    if (rtc_calib_ticks_acked_tot[tmr]+rtc_calib_ticks_acked[tmr] != rtc_calib_ticks_acked[tmr])
        fprintf (st, "  Total Ticks Acked:         %u\n",   rtc_calib_ticks_acked_tot[tmr]+rtc_calib_ticks_acked[tmr]);
    if (rtc_calib_tick_time[tmr])
        fprintf (st, "  Tick Time:                 %s\n",   sim_fmt_secs (rtc_calib_tick_time[tmr]));
    if (rtc_calib_tick_time_tot[tmr]+rtc_calib_tick_time[tmr] != rtc_calib_tick_time[tmr])
        fprintf (st, "  Total Tick Time:           %s\n",   sim_fmt_secs (rtc_calib_tick_time_tot[tmr]+rtc_calib_tick_time[tmr]));
    if (rtc_clock_catchup_ticks[tmr])
        fprintf (st, "  Catchup Ticks Sched:       %u\n",   rtc_clock_catchup_ticks[tmr]);
    if (rtc_clock_catchup_ticks_tot[tmr]+rtc_clock_catchup_ticks[tmr] != rtc_clock_catchup_ticks[tmr])
        fprintf (st, "  Total Catchup Ticks Sched: %u\n",   rtc_clock_catchup_ticks_tot[tmr]+rtc_clock_catchup_ticks[tmr]);
    clock_gettime (CLOCK_REALTIME, &now);
    time_t_now = (time_t)now.tv_sec;
    fprintf (st, "  Wall Clock Time Now:       %8.8s.%03d\n", 11+ctime(&time_t_now), (int)(now.tv_nsec/1000000));
    if (rtc_clock_catchup_eligible[tmr]) {
        _double_to_timespec (&now, rtc_clock_catchup_base_time[tmr]+rtc_calib_tick_time[tmr]);
        time_t_now = (time_t)now.tv_sec;
        fprintf (st, "  Catchup Tick Time:         %8.8s.%03d\n", 11+ctime(&time_t_now), (int)(now.tv_nsec/1000000));
        _double_to_timespec (&now, rtc_clock_catchup_base_time[tmr]);
        time_t_now = (time_t)now.tv_sec;
        fprintf (st, "  Catchup Base Time:         %8.8s.%03d\n", 11+ctime(&time_t_now), (int)(now.tv_nsec/1000000));
        }
    if (rtc_clock_time_idled[tmr])
        fprintf (st, "  Total Time Idled:          %s\n",   sim_fmt_secs (rtc_clock_time_idled[tmr]/1000.0));
    }
if (clocks == 0)
    fprintf (st, "%s clock device is not specified, co-scheduling is unavailable\n", sim_name);
return SCPE_OK;
}

t_stat sim_show_clock_queues (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
int tmr;

#if defined (SIM_ASYNCH_CLOCKS)
pthread_mutex_lock (&sim_timer_lock);
if (sim_asynch_timer) {
    const char *tim;

    if (sim_wallclock_queue == QUEUE_LIST_END)
        fprintf (st, "%s wall clock event queue empty\n", sim_name);
    else {
        fprintf (st, "%s wall clock event queue status\n", sim_name);
        for (uptr = sim_wallclock_queue; uptr != QUEUE_LIST_END; uptr = uptr->a_next) {
            if ((dptr = find_dev_from_unit (uptr)) != NULL) {
                fprintf (st, "  %s", sim_dname (dptr));
                if (dptr->numunits > 1)
                    fprintf (st, " unit %d", (int32) (uptr - dptr->units));
                }
            else
                fprintf (st, "  Unknown");
            tim = sim_fmt_secs(uptr->a_usec_delay/1000000.0);
            fprintf (st, " after %s\n", tim);
            }
        }
    }
#endif /* SIM_ASYNCH_CLOCKS */
for (tmr=0; tmr<=SIM_NTIMERS; ++tmr) {
    if (sim_clock_unit[tmr] == NULL)
        continue;
    if (sim_clock_cosched_queue[tmr] != QUEUE_LIST_END) {
        int32 accum;

        fprintf (st, "%s clock (%s) co-schedule event queue status\n",
                 sim_name, sim_uname(sim_clock_unit[tmr]));
        accum = 0;
        for (uptr = sim_clock_cosched_queue[tmr]; uptr != QUEUE_LIST_END; uptr = uptr->next) {
            if ((dptr = find_dev_from_unit (uptr)) != NULL) {
                fprintf (st, "  %s", sim_dname (dptr));
                if (dptr->numunits > 1)
                    fprintf (st, " unit %d", (int32) (uptr - dptr->units));
                }
            else
                fprintf (st, "  Unknown");
            if (accum > 0)
                fprintf (st, " after %d ticks", accum);
            fprintf (st, "\n");
            accum = accum + uptr->time;
            }
        }
    }
#if defined (SIM_ASYNCH_IO)
pthread_mutex_unlock (&sim_timer_lock);
#endif /* SIM_ASYNCH_IO */
return SCPE_OK;
}

t_stat sim_timer_show_idle_mode (FILE* st, UNIT* uptr, int32 val, CONST void *  desc)
{
if (sim_throt_type != SIM_THROT_NONE)
    return sim_show_throt (st, NULL, uptr, val, desc);
return sim_show_idle (st, uptr, val, desc);
}

REG sim_timer_reg[] = {
    { NULL }
    };

REG sim_throttle_reg[] = {
    { DRDATAD (THROT_MS_START,   sim_throt_ms_start,     32, ""), PV_RSPC|REG_RO},
    { DRDATAD (THROT_MS_STOP,    sim_throt_ms_stop,      32, ""), PV_RSPC|REG_RO},
    { DRDATAD (THROT_TYPE,       sim_throt_type,         32, ""), PV_RSPC|REG_RO},
    { DRDATAD (THROT_VAL,        sim_throt_val,          32, ""), PV_RSPC|REG_RO},
    { DRDATAD (THROT_STATE,      sim_throt_state,        32, ""), PV_RSPC|REG_RO},
    { DRDATAD (THROT_SLEEP_TIME, sim_throt_sleep_time,   32, ""), PV_RSPC|REG_RO},
    { DRDATAD (THROT_WAIT,       sim_throt_wait,         32, ""), PV_RSPC|REG_RO},
    { NULL }
    };

/* Clear, Set and show catchup */

/* Clear catchup */

t_stat sim_timer_clr_catchup (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (sim_catchup_ticks)
    sim_catchup_ticks = FALSE;
return SCPE_OK;
}

t_stat sim_timer_set_catchup (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (!sim_catchup_ticks)
    sim_catchup_ticks = TRUE;
return SCPE_OK;
}

t_stat sim_timer_show_catchup (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "Calibrated Ticks%s", sim_catchup_ticks ? " with Catchup Ticks" : "");
return SCPE_OK;
}

/* Set and show idle calibration threshold */

t_stat sim_timer_set_idle_pct (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
t_stat r;
int32 newpct;

if (cptr == NULL)
    return SCPE_ARG;
newpct = (int32) get_uint (cptr, 10, 100, &r);
if ((r != SCPE_OK) || (newpct == (int32)(sim_idle_calib_pct)))
    return r;
if (newpct == 0)
    return SCPE_ARG;
sim_idle_calib_pct = (uint32)newpct;
return SCPE_OK;
}

t_stat sim_timer_show_idle_pct (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (sim_idle_calib_pct == 0)
    fprintf (st, "Calibration Always");
else
    fprintf (st, "Calibration Skipped when Idle exceeds %d%%", sim_idle_calib_pct);
return SCPE_OK;
}

/* Clear, Set and show asynch */

/* Clear asynch */

t_stat sim_timer_clr_async (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (sim_asynch_timer) {
    sim_asynch_timer = FALSE;
    sim_timer_change_asynch ();
    }
return SCPE_OK;
}

t_stat sim_timer_set_async (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (sim_asynch_enabled && (!sim_asynch_timer)) {
    sim_asynch_timer = TRUE;
    sim_timer_change_asynch ();
    }
return SCPE_OK;
}

t_stat sim_timer_show_async (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "%s", sim_asynch_timer ? "Asynchronous" : "Synchronous");
return SCPE_OK;
}

MTAB sim_timer_mod[] = {
#if defined (SIM_ASYNCH_CLOCKS)
  { MTAB_VDV,          MTAB_VDV, "ASYNCH",   "ASYNCH",    &sim_timer_set_async,    &sim_timer_show_async,     NULL, "Enables/Displays Asynchronous Timer mode" },
  { MTAB_VDV,                 0,    NULL,    "NOASYNCH",  &sim_timer_clr_async,    NULL,                      NULL, "Disables Asynchronous Timer operation" },
#endif
  { MTAB_VDV,          MTAB_VDV, "CATCHUP",  "CATCHUP",   &sim_timer_set_catchup,  &sim_timer_show_catchup,   NULL, "Enables/Displays Clock Tick catchup mode" },
  { MTAB_VDV,                 0, NULL,       "NOCATCHUP", &sim_timer_clr_catchup,  NULL,                      NULL, "Disables Clock Tick catchup mode" },
  { MTAB_VDV|MTAB_VALR,       0, "CALIB",    "CALIB=nn",  &sim_timer_set_idle_pct, &sim_timer_show_idle_pct,  NULL, "Configure/Display Calibration Idle Suppression %" },
  { MTAB_VDV,                 0, "IDLE",     NULL,        NULL,                    &sim_timer_show_idle_mode, NULL, "Display Idle/Throttle mode" },
  { 0 },
};

static t_stat sim_timer_clock_reset (DEVICE *dptr);

DEVICE sim_timer_dev = {
    "TIMER", sim_timer_units, sim_timer_reg, sim_timer_mod, 
    SIM_NTIMERS+1, 0, 0, 0, 0, 0, 
    NULL, NULL, &sim_timer_clock_reset, NULL, NULL, NULL, 
    NULL, DEV_DEBUG | DEV_NOSAVE, 0, sim_timer_debug};

DEVICE sim_throttle_dev = {
    "THROTTLE", &sim_throttle_unit, sim_throttle_reg, NULL, 1};


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
uint32 cyc_ms = 0;
uint32 w_ms, w_idle, act_ms;
int32 act_cyc;

if (rtc_clock_catchup_pending[tmr]) {                   /* Catchup clock tick pending? */
    sim_debug (DBG_CAL, &sim_timer_dev, "sim_idle(tmr=%d, sin_cyc=%d) - accelerating pending catch-up tick before idling %s\n", tmr, sin_cyc, sim_uname (sim_clock_unit[tmr]));
    sim_activate_abs (&sim_timer_units[tmr], 0);
    if (sin_cyc)
        sim_interval = sim_interval - 1;
    return FALSE;
    }
if ((!sim_idle_enab)                             ||     /* idling disabled */
    ((sim_clock_queue == QUEUE_LIST_END) &&             /* or clock queue empty? */
     (!sim_asynch_timer))||                             /*     and not asynch? */
    ((sim_clock_queue != QUEUE_LIST_END) &&             /* or clock queue not empty */
     ((sim_clock_queue->flags & UNIT_IDLE) == 0))||     /*   and event not idle-able? */
    (rtc_elapsed[tmr] < sim_idle_stable)) {             /* or timer not stable? */
    sim_debug (DBG_IDL, &sim_timer_dev, "Can't idle: %s - elapsed: %d.%03d\n", !sim_idle_enab ? "idle disabled" : 
                                                                             ((rtc_elapsed[tmr] < sim_idle_stable) ? "not stable" : 
                                                                                                                     ((sim_clock_queue != QUEUE_LIST_END) ? sim_uname (sim_clock_queue) : 
                                                                                                                                                            "")), rtc_elapsed[tmr], rtc_ticks[tmr]);
    if (sin_cyc)
        sim_interval = sim_interval - 1;
    return FALSE;
    }
if (_rtcn_tick_catchup_check(tmr, 0)) {
    sim_debug (DBG_CAL, &sim_timer_dev, "sim_idle(tmr=%d, sin_cyc=%d) - rescheduling catchup tick for %s\n", tmr, sin_cyc, sim_uname (sim_clock_unit[tmr]));
    if (sin_cyc)
        sim_interval = sim_interval - 1;
    return FALSE;
    }
/*
   When a simulator is in an instruction path (or under other conditions 
   which would indicate idling), the countdown of sim_interval will not 
   be happening at a pace which is consistent with the rate it happens 
   when not in the 'idle capable' state.  The consequence of this is that 
   the clock calibration may produce calibrated results which vary much 
   more than they do when not in the idle able state.  Sim_idle also uses 
   the calibrated tick size to approximate an adjustment to sim_interval
   to reflect the number of instructions which would have executed during 
   the actual idle time, so consistent calibrated numbers produce better 
   adjustments. 
   
   To negate this effect, we accumulate the time actually idled here.
   sim_rtcn_calb compares the accumulated idle time during the most recent 
   second and if it exceeds the percentage defined by and sim_idle_calib_pct
   calibration is suppressed. Thus recalibration only happens if things 
   didn't idle too much.

   we also check check sim_idle_enab above so that all simulators can avoid
   directly checking sim_idle_enab before calling sim_idle so that all of 
   the bookkeeping on sim_idle_idled is done here in sim_timer where it 
   means something, while not idling when it isn't enabled.  
   */
sim_debug (DBG_TRC, &sim_timer_dev, "sim_idle(tmr=%d, sin_cyc=%d)\n", tmr, sin_cyc);
cyc_ms = (rtc_currd[tmr] * rtc_hz[tmr]) / 1000;         /* cycles per msec */
if ((sim_idle_rate_ms == 0) || (cyc_ms == 0)) {         /* not possible? */
    if (sin_cyc)
        sim_interval = sim_interval - 1;
    sim_debug (DBG_IDL, &sim_timer_dev, "not possible idle_rate_ms=%d - cyc/ms=%d\n", sim_idle_rate_ms, cyc_ms);
    return FALSE;
    }
w_ms = (uint32) sim_interval / cyc_ms;                  /* ms to wait */
w_idle = (w_ms * 1000) / sim_idle_rate_ms;              /* intervals to wait * 1000 */
if (w_idle < 500) {                                     /* shorter than 1/2 a minimum sleep? */
    if (sin_cyc)
        sim_interval = sim_interval - 1;
    sim_debug (DBG_IDL, &sim_timer_dev, "no wait\n");
    return FALSE;
    }
if (sim_clock_queue == QUEUE_LIST_END)
    sim_debug (DBG_IDL, &sim_timer_dev, "sleeping for %d ms - pending event in %d instructions\n", w_ms, sim_interval);
else
    sim_debug (DBG_IDL, &sim_timer_dev, "sleeping for %d ms - pending event on %s in %d instructions\n", w_ms, sim_uname(sim_clock_queue), sim_interval);
act_ms = sim_idle_ms_sleep (w_ms);                      /* wait */
rtc_clock_time_idled[tmr] += act_ms;
act_cyc = act_ms * cyc_ms;
if (act_ms < w_ms)                                      /* awakened early? */
    act_cyc += (cyc_ms * sim_idle_rate_ms) / 2;         /* account for half an interval's worth of cycles */
if (sim_interval > act_cyc)
    sim_interval = sim_interval - act_cyc;              /* count down sim_interval */
else
    sim_interval = 0;                                   /* or fire immediately */
if (sim_clock_queue == QUEUE_LIST_END)
    sim_debug (DBG_IDL, &sim_timer_dev, "slept for %d ms - pending event in %d instructions\n", act_ms, sim_interval);
else
    sim_debug (DBG_IDL, &sim_timer_dev, "slept for %d ms - pending event on %s in %d instructions\n", act_ms, sim_uname(sim_clock_queue), sim_interval);
return TRUE;
}

/* Set idling - implicitly disables throttling */

t_stat sim_set_idle (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
t_stat r;
uint32 v;

if (cptr && *cptr) {
    v = (uint32) get_uint (cptr, 10, SIM_IDLE_STMAX, &r);
    if ((r != SCPE_OK) || (v < SIM_IDLE_STMIN))
        return sim_messagef (SCPE_ARG, "Invalid Stability value: %s.  Valid values range from %d to %d.\n", cptr, SIM_IDLE_STMIN, SIM_IDLE_STMAX);
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

t_stat sim_clr_idle (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
sim_idle_enab = FALSE;
return SCPE_OK;
}

/* Show idling */

t_stat sim_show_idle (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
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

t_stat sim_set_throt (int32 arg, CONST char *cptr)
{
CONST char *tptr;
char c;
t_value val, val2 = 0;

if (arg == 0) {
    if ((cptr != NULL) && (*cptr != 0))
        return sim_messagef (SCPE_ARG, "Unexpected NOTHROTTLE argument: %s\n", cptr);
    sim_throt_type = SIM_THROT_NONE;
    sim_throt_cancel ();
    }
else if (sim_idle_rate_ms == 0) {
    return sim_messagef (SCPE_NOFNC, "Throttling is not available, Minimum OS sleep time is %dms\n", sim_os_sleep_min_ms);
    }
else {
    if (*cptr == '\0')
        return sim_messagef (SCPE_ARG, "Missing throttle mode specification\n");
    val = strtotv (cptr, &tptr, 10);
    if (cptr == tptr)
        return sim_messagef (SCPE_ARG, "Invalid throttle specification: %s\n", cptr);
    sim_throt_sleep_time = sim_idle_rate_ms;
    c = (char)toupper (*tptr++);
    if (c == '/') {
        val2 = strtotv (tptr, &tptr, 10);
        if ((*tptr != '\0') || (val == 0))
            return sim_messagef (SCPE_ARG, "Invalid throttle delay specifier: %s\n", cptr);
        }
    if (c == 'M') 
        sim_throt_type = SIM_THROT_MCYC;
    else if (c == 'K')
        sim_throt_type = SIM_THROT_KCYC;
    else if ((c == '%') && (val > 0) && (val < 100))
        sim_throt_type = SIM_THROT_PCT;
    else if ((c == '/') && (val2 != 0))
        sim_throt_type = SIM_THROT_SPC;
    else return sim_messagef (SCPE_ARG, "Invalid throttle specification: %s\n", cptr);
    if (sim_idle_enab) {
        sim_printf ("Idling disabled\n");
        sim_clr_idle (NULL, 0, NULL, NULL);
        }
    sim_throt_val = (uint32) val;
    if (sim_throt_type == SIM_THROT_SPC) {
        if (val2 >= sim_idle_rate_ms)
            sim_throt_sleep_time = (uint32) val2;
        else {
            if ((sim_idle_rate_ms % val2) == 0) {
                sim_throt_sleep_time = sim_idle_rate_ms;
                sim_throt_val = (uint32) (val * (sim_idle_rate_ms / val2));
                }
            else {
                sim_throt_sleep_time = sim_idle_rate_ms;
                sim_throt_val = (uint32) (val * (1 + (sim_idle_rate_ms / val2)));
                }
            }
        }
    }
return SCPE_OK;
}

t_stat sim_show_throt (FILE *st, DEVICE *dnotused, UNIT *unotused, int32 flag, CONST char *cptr)
{
if (sim_idle_rate_ms == 0)
    fprintf (st, "Throttling not available\n");
else {
    switch (sim_throt_type) {

    case SIM_THROT_MCYC:
        fprintf (st, "Throttle = %d megacycles\n", sim_throt_val);
        if (sim_throt_wait)
            fprintf (st, "Throttling achieved by sleeping for %d ms every %d cycles\n", sim_throt_sleep_time, sim_throt_wait);
        break;

    case SIM_THROT_KCYC:
        fprintf (st, "Throttle = %d kilocycles\n", sim_throt_val);
        if (sim_throt_wait)
            fprintf (st, "Throttling achieved by sleeping for %d ms every %d cycles\n", sim_throt_sleep_time, sim_throt_wait);
        break;

    case SIM_THROT_PCT:
        fprintf (st, "Throttle = %d%%\n", sim_throt_val);
        if (sim_throt_wait)
            fprintf (st, "Throttling achieved by sleeping for %d ms every %d cycles\n", sim_throt_sleep_time, sim_throt_wait);
        break;

    case SIM_THROT_SPC:
        fprintf (st, "Throttle = %d ms every %d cycles\n", sim_throt_sleep_time, sim_throt_val);
        break;

    default:
        fprintf (st, "Throttling disabled\n");
        break;
        }
    }
return SCPE_OK;
}

void sim_throt_sched (void)
{
sim_throt_state = 0;
if (sim_throt_type)
    sim_activate (&sim_throttle_unit, SIM_THROT_WINIT);
}

void sim_throt_cancel (void)
{
sim_cancel (&sim_throttle_unit);
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
        sim_throt_inst_start = sim_gtime();
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
            sim_throt_inst_start = sim_gtime();
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
            sim_throt_inst_start = sim_gtime();
            sim_throt_state = 2;
            sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc() Throttle values a_cps = %f, d_cps = %f, wait = %d\n", 
                                                a_cps, d_cps, sim_throt_wait);
            sim_throt_cps = (int32)d_cps;               /* save the desired rate */
            }
        break;

    case 2:                                             /* throttling */
        sim_idle_ms_sleep (sim_throt_sleep_time);
        delta_ms = sim_os_msec () - sim_throt_ms_start;
        if (sim_throt_type != SIM_THROT_SPC) {          /* when not dynamic throttling */
            if (delta_ms >= 10000) {                    /* recompute every 10 sec */
                double delta_insts = sim_gtime() - sim_throt_inst_start;
                a_cps = (delta_insts * 1000.0) / (double) delta_ms;
                if (sim_throt_type == SIM_THROT_MCYC)   /* calc desired cps */
                    d_cps = (double) sim_throt_val * 1000000.0;
                else if (sim_throt_type == SIM_THROT_KCYC)
                    d_cps = (double) sim_throt_val * 1000.0;
                else d_cps = (a_cps * ((double) sim_throt_val)) / 100.0;
                if (fabs(100.0 * (d_cps - a_cps) / a_cps) > (double)SIM_THROT_DRIFT_PCT) {
                    sim_throt_wait = sim_throt_val;
                    sim_throt_state = 1;                /* next state to recalibrate */
                    sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc() Recalibrating throttle based on values a_cps = %f, d_cps = %f\n", 
                                                        a_cps, d_cps);
                    }
                sim_throt_ms_start = sim_os_msec ();
                sim_throt_inst_start = sim_gtime();
                }
            }
        else                                            /* record instruction rate */
            sim_throt_cps = (int32)((1000.0 * sim_throt_val) / (double)delta_ms);
        break;
        }

sim_activate (uptr, sim_throt_wait);                    /* reschedule */
return SCPE_OK;
}

/* Clock assist activites */
t_stat sim_timer_tick_svc (UNIT *uptr)
{
int tmr = (int)(uptr-sim_timer_units);
t_stat stat;

rtc_clock_ticks[tmr] += 1;
rtc_calib_tick_time[tmr] += rtc_clock_tick_size[tmr];
/*
 * Some devices may depend on executing during the same instruction or 
 * immediately after the clock tick event.  To satisfy this, we directly 
 * run the clock event here and if it completes successfully, schedule any
 * currently coschedule units to run now.  Ticks should never return a 
 * non-success status, while co-schedule activities might, so they are 
 * queued to run from sim_process_event
 */
sim_debug (DBG_QUE, &sim_timer_dev, "sim_timer_tick_svc - scheduling %s\n", sim_uname (sim_clock_unit[tmr]));
if (sim_clock_unit[tmr]->action == NULL)
    return SCPE_IERR;
stat = sim_clock_unit[tmr]->action (sim_clock_unit[tmr]);
--sim_cosched_interval[tmr];                    /* Countdown ticks */
if (stat == SCPE_OK) {
    if (rtc_clock_catchup_eligible[tmr]) {      /* calibration started? */
        struct timespec now;
        double skew;

        clock_gettime(CLOCK_REALTIME, &now);
        skew = (_timespec_to_double(&now) - (rtc_calib_tick_time[tmr]+rtc_clock_catchup_base_time[tmr]));

        if (fabs(skew) > fabs(rtc_clock_skew_max[tmr]))
            rtc_clock_skew_max[tmr] = skew;
        }
    while ((sim_clock_cosched_queue[tmr] != QUEUE_LIST_END) &&
           (sim_cosched_interval[tmr] < sim_clock_cosched_queue[tmr]->time)) {
        UNIT *cptr = sim_clock_cosched_queue[tmr];
        sim_clock_cosched_queue[tmr] = cptr->next;
        cptr->next = NULL;
        cptr->cancel = NULL;
        sim_debug (DBG_QUE, &sim_timer_dev, "sim_timer_tick_svc(tmr=%d) - coactivating %s\n", tmr, sim_uname (cptr));
        _sim_activate (cptr, 0);
        }
    if (sim_clock_cosched_queue[tmr] != QUEUE_LIST_END)
        sim_cosched_interval[tmr] = sim_clock_cosched_queue[tmr]->time;
    else
        sim_cosched_interval[tmr]  = 0;
    }
sim_timer_activate_after (uptr, 1000000/rtc_hz[tmr]);
return stat;
}

void sim_rtcn_get_time (struct timespec *now, int tmr)
{
sim_debug (DBG_CAL, &sim_timer_dev, "sim_rtcn_get_time(tmr=%d)\n", tmr);
clock_gettime (CLOCK_REALTIME, now);
}

/* 
 * If the host system has a relatively large clock tick (as compared to
 * the desired simulated hz) ticks will naturally be scheduled late and
 * these delays will accumulate.  The net result will be unreasonably
 * slow ticks being delivered to the simulated system.
 * Additionally, when a simulator is idling and/or throttling, it will
 * deliberately call sim_os_ms_sleep and those sleep operations will be
 * variable and subject to the host system's minimum sleep resolution
 * which can exceed the desired sleep interval and add to the concept
 * of slow tick delivery to the simulated system.
 * We accomodate these problems and make up for lost ticks by injecting
 * catch-up ticks to the simulator.
 *
 * We avoid excessive co-scheduled polling during these catch-up ticks 
 * to minimize what is likely excessive overhead, thus 'coschedule 
 * polling' only occurs on every fourth clock tick when processing 
 * catch-up ticks.
 *
 * When necessary, catch-up ticks are scheduled to run under one 
 * of two conditions:
 *   1) after indicated number of instructions in a call by the simulator
 *      to sim_rtcn_tick_ack.  sim_rtcn_tick_ack exists to provide a 
 *      mechanism to inform the simh timer facilities when the simulated 
 *      system has accepted the most recent clock tick interrupt.
 *   2) immediately when the simulator calls sim_idle
 */

/* _rtcn_tick_catchup_check - idle simulator until next event or for specified interval

   Inputs:
        tmr =   calibrated timer to check/schedule
        time =  instruction delay for next tick

   Returns TRUE if a catchup tick has been scheduled
*/

static t_bool _rtcn_tick_catchup_check (int32 tmr, int32 time)
{
double tnow;

if ((!sim_catchup_ticks) || 
    ((tmr < 0) || (tmr >= SIM_NTIMERS)))
    return FALSE;
tnow = sim_timenow_double();
if (!rtc_clock_catchup_eligible[tmr]) {
    rtc_clock_catchup_base_time[tmr] = tnow;
    rtc_clock_ticks_tot[tmr] += rtc_clock_ticks[tmr];
    rtc_clock_ticks[tmr] = 0;
    rtc_calib_tick_time_tot[tmr] += rtc_calib_tick_time[tmr];
    rtc_calib_tick_time[tmr] = 0.0;
    rtc_clock_catchup_ticks_tot[tmr] += rtc_clock_catchup_ticks[tmr];
    rtc_clock_catchup_ticks[tmr] = 0;
    rtc_calib_ticks_acked_tot[tmr] += rtc_calib_ticks_acked[tmr];
    rtc_calib_ticks_acked[tmr] = 0;
    rtc_clock_catchup_eligible[tmr] = TRUE;
    sim_debug (DBG_QUE, &sim_timer_dev, "_rtcn_tick_catchup_check() - Enabling catchup ticks for %s\n", sim_uname (sim_clock_unit[tmr]));
    return TRUE;
    }
if (rtc_clock_catchup_eligible[tmr] &&
    (tnow > (rtc_clock_catchup_base_time[tmr] + (rtc_calib_tick_time[tmr] + rtc_clock_tick_size[tmr])))) {
    sim_debug (DBG_QUE, &sim_timer_dev, "_rtcn_tick_catchup_check(%d) - scheduling catchup tick for %s which is behind %s\n", time, sim_uname (sim_clock_unit[tmr]), sim_fmt_secs (tnow > (rtc_clock_catchup_base_time[tmr] + (rtc_calib_tick_time[tmr] + rtc_clock_tick_size[tmr]))));
    rtc_clock_catchup_pending[tmr] = TRUE;
    sim_activate_abs (&sim_timer_units[tmr], time);
    return TRUE;
    }
return FALSE;
}

t_stat sim_rtcn_tick_ack (int32 time, int32 tmr)
{
if ((tmr < 0) || (tmr >= SIM_NTIMERS))
    return SCPE_TIMER;
sim_debug (DBG_ACK, &sim_timer_dev, "sim_rtcn_tick_ack - for %s\n", sim_uname (sim_clock_unit[tmr]));
_rtcn_tick_catchup_check (tmr, time);
++rtc_calib_ticks_acked[tmr];
return SCPE_OK;
}


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

clock_gettime (CLOCK_REALTIME, &now);
return _timespec_to_double (&now);
}

#if defined(SIM_ASYNCH_CLOCKS)

pthread_t           sim_timer_thread;           /* Wall Clock Timing Thread Id */
pthread_cond_t      sim_timer_startup_cond;
t_bool              sim_timer_thread_running = FALSE;

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
while (sim_asynch_timer && sim_is_running) {
    struct timespec start_time, stop_time;
    struct timespec due_time;
    double wait_usec;
    int32 inst_delay;
    double inst_per_sec;
    UNIT *uptr, *cptr, *prvptr;

    if (sim_wallclock_entry) {                          /* something to insert in queue? */

        sim_debug (DBG_TIM, &sim_timer_dev, "_timer_thread() - timing %s for %s\n", 
                   sim_uname(sim_wallclock_entry), sim_fmt_secs (sim_wallclock_entry->time/1000000.0));

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
        if (1 != sim_timespec_compare (&due_time, &stop_time))
            inst_delay = 0;
        else
            inst_delay = (int32)(inst_per_sec*(_timespec_to_double(&due_time)-_timespec_to_double(&stop_time)));
        sim_debug (DBG_TIM, &sim_timer_dev, "_timer_thread() - slept %.0fms - activating(%s,%d)\n", 
                   1000.0*(_timespec_to_double (&stop_time)-_timespec_to_double (&start_time)), sim_uname(uptr), inst_delay);
        sim_activate (uptr, inst_delay);
        }
    else {/* Something wants to adjust the queue since the wait condition was signaled */
        }
    }
pthread_mutex_unlock (&sim_timer_lock);

sim_debug (DBG_TIM, &sim_timer_dev, "_timer_thread() - exiting\n");

return NULL;
}

#endif /* defined(SIM_ASYNCH_CLOCKS) */

/*
   In the event that there are no active clock devices, no instruction 
   rate calibration will be performed.  This is more likely on simpler
   simulators which don't have a full spectrum of standard devices or 
   possibly when a clock device exists but its use is optional.

   Additonally, when a host system has a natural clock tick (or minimal 
   sleep time) which is greater than the tick size that a simulator 
   wants to run a clock at, we run this clock at the rate implied by
   the host system's minimal sleep time or 50Hz.
   
   To solve this we merely run an internal clock at 10Hz.
 */

#define CLK_TPS 10
#define CLK_INIT (SIM_INITIAL_IPS/CLK_TPS)
static int32 sim_int_clk_tps;

static t_stat sim_timer_clock_tick_svc (UNIT *uptr)
{
sim_rtcn_calb (sim_int_clk_tps, SIM_INTERNAL_CLK);
sim_activate_after (uptr, 1000000/sim_int_clk_tps);     /* reactivate unit */
return SCPE_OK;
}

/* 
  This routine exists to assure that there is a single reliably calibrated 
  clock properly counting instruction execution relative to time.  The best 
  way to assure reliable calibration is to use a clock which ticks no 
  faster than the host system's clock.  This is optimal so that accurate 
  time measurements are taken.  If the simulated system doesn't have a 
  clock with an appropriate tick rate, an internal clock is run that meets 
  this requirement, 
 */
static void _rtcn_configure_calibrated_clock (int32 newtmr)
{
int32 tmr;

/* Look for a timer running slower than the host system clock */
sim_int_clk_tps = MIN(CLK_TPS, sim_os_tick_hz);
for (tmr=0; tmr<SIM_NTIMERS; tmr++) {
    if ((rtc_hz[tmr]) &&
        (rtc_hz[tmr] <= (uint32)sim_os_tick_hz))
        break;
    }
if (tmr == SIM_NTIMERS) {                   /* None found? */
    if ((tmr != newtmr) && (!sim_is_active (&SIM_INTERNAL_UNIT))) {
        /* Start the internal timer */
        sim_calb_tmr = SIM_NTIMERS;
        sim_debug (DBG_CAL, &sim_timer_dev, "_rtcn_configure_calibrated_clock() - Starting Internal Calibrated Timer at %dHz\n", sim_int_clk_tps);
        SIM_INTERNAL_UNIT.action = &sim_timer_clock_tick_svc;
        SIM_INTERNAL_UNIT.flags = UNIT_DIS | UNIT_IDLE;
        sim_activate_abs (&SIM_INTERNAL_UNIT, 0);
        sim_rtcn_init_unit (&SIM_INTERNAL_UNIT, (CLK_INIT*CLK_TPS)/sim_int_clk_tps, SIM_INTERNAL_CLK);
        }
    return;
    }
if ((tmr == newtmr) && 
    (sim_calb_tmr == newtmr))               /* already set? */
    return;
if (sim_calb_tmr == SIM_NTIMERS) {      /* was old the internal timer? */
    sim_debug (DBG_CAL, &sim_timer_dev, "_rtcn_configure_calibrated_clock() - Stopping Internal Calibrated Timer, New Timer = %d (%dHz)\n", tmr, rtc_hz[tmr]);
    rtc_initd[SIM_NTIMERS] = 0;
    rtc_hz[SIM_NTIMERS] = 0;
    sim_cancel (&SIM_INTERNAL_UNIT);
    /* Migrate any coscheduled devices to the standard queue and they will requeue themselves */
    while (sim_clock_cosched_queue[SIM_NTIMERS] != QUEUE_LIST_END) {
        UNIT *uptr = sim_clock_cosched_queue[SIM_NTIMERS];

        _sim_coschedule_cancel (uptr);
        _sim_activate (uptr, 1);
        }
    }
else {
    sim_debug (DBG_CAL, &sim_timer_dev, "_rtcn_configure_calibrated_clock() - Changing Calibrated Timer from %d (%dHz) to %d (%dHz)\n", sim_calb_tmr, rtc_hz[sim_calb_tmr], tmr, rtc_hz[tmr]);
    sim_calb_tmr = tmr;
    }
sim_calb_tmr = tmr;
}

static t_stat sim_timer_clock_reset (DEVICE *dptr)
{
sim_debug (DBG_TRC, &sim_timer_dev, "sim_timer_clock_reset()\n");
_rtcn_configure_calibrated_clock (sim_calb_tmr);
if (sim_switches & SWMASK ('P')) {
    sim_cancel (&SIM_INTERNAL_UNIT);
    sim_calb_tmr = -1;
    }
return SCPE_OK;
}

void sim_start_timer_services (void)
{
sim_debug (DBG_TRC, &sim_timer_dev, "sim_start_timer_services()\n");
_rtcn_configure_calibrated_clock (sim_calb_tmr);
#if defined(SIM_ASYNCH_CLOCKS)
pthread_mutex_lock (&sim_timer_lock);
if (sim_asynch_timer) {
    pthread_attr_t attr;

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
int tmr;

sim_debug (DBG_TRC, &sim_timer_dev, "sim_stop_timer_services()\n");

for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    int32 accum;

    if (sim_clock_unit[tmr]) {
        /* Stop clock assist unit and make sure the clock unit has a tick queued */
        sim_cancel (&sim_timer_units[tmr]);
        if (rtc_hz[tmr])
            sim_activate (sim_clock_unit[tmr], rtc_currd[tmr]);
        /* Move coscheduled units to the standard event queue */
        accum = 1;
        while (sim_clock_cosched_queue[tmr] != QUEUE_LIST_END) {
            UNIT *cptr = sim_clock_cosched_queue[tmr];

            sim_clock_cosched_queue[tmr] = cptr->next;
            cptr->next = NULL;
            cptr->cancel = NULL;

            accum += cptr->time;
            _sim_activate (cptr, accum*rtc_currd[tmr]);
            }
        }
    }
sim_cancel (&SIM_INTERNAL_UNIT);                    /* Make sure Internal Timer is stopped */
sim_calb_tmr_last = sim_calb_tmr;                   /* Save calibrated timer value for display */
sim_inst_per_sec_last = sim_timer_inst_per_sec ();  /* Save execution rate for display */
sim_calb_tmr = -1;
#if defined(SIM_ASYNCH_CLOCKS)
pthread_mutex_lock (&sim_timer_lock);
if (sim_timer_thread_running) {
    sim_debug (DBG_TRC, &sim_timer_dev, "sim_stop_timer_services() - stopping\n");
    pthread_cond_signal (&sim_timer_wake);
    pthread_mutex_unlock (&sim_timer_lock);
    pthread_join (sim_timer_thread, NULL);
    sim_timer_thread_running = FALSE;
    /* Any wallclock queued events are now migrated to the normal event queue */
    while (sim_wallclock_queue != QUEUE_LIST_END) {
        UNIT *uptr = sim_wallclock_queue;
        double inst_delay_d = uptr->a_due_gtime - sim_gtime ();
        int32 inst_delay;

        uptr->cancel (uptr);
        if (inst_delay_d < 0.0)
            inst_delay_d = 0.0;
        /* Bound delay to avoid overflow.  */
        /* Long delays are usually canceled before they expire */
        if (inst_delay_d > (double)0x7FFFFFFF)
            inst_delay_d = (double)0x7FFFFFFF;
        inst_delay = (int32)inst_delay_d;
        if ((inst_delay == 0) && (inst_delay_d != 0.0))
            inst_delay = 1;     /* Minimum non-zero delay is 1 instruction */
        _sim_activate (uptr, inst_delay);            /* queue it now */
        }
    }
else
    pthread_mutex_unlock (&sim_timer_lock);
#endif
}

t_stat sim_timer_change_asynch (void)
{
#if defined(SIM_ASYNCH_CLOCKS)
if (sim_asynch_enabled && sim_asynch_timer)
    sim_start_timer_services ();
else
    sim_stop_timer_services ();
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
    inst_per_sec = ((double)rtc_currd[sim_calb_tmr])*sim_int_clk_tps;
return inst_per_sec;
}

t_stat sim_timer_activate (UNIT *uptr, int32 interval)
{
AIO_VALIDATE;
return sim_timer_activate_after (uptr, (uint32)((interval * 1000000.0) / sim_timer_inst_per_sec ()));
}

t_stat sim_timer_activate_after (UNIT *uptr, uint32 usec_delay)
{
int inst_delay, tmr;
double inst_delay_d, inst_per_sec;

AIO_VALIDATE;
/* If this is a clock unit, we need to schedule the related timer unit instead */
for (tmr=0; tmr<=SIM_NTIMERS; tmr++)
    if (sim_clock_unit[tmr] == uptr) {
        uptr = &sim_timer_units[tmr];
        break;
        }
if (sim_is_active (uptr))                               /* already active? */
    return SCPE_OK;
inst_per_sec = sim_timer_inst_per_sec ();
inst_delay_d = ((inst_per_sec*usec_delay)/1000000.0);
/* Bound delay to avoid overflow.  */
/* Long delays are usually canceled before they expire */
if (inst_delay_d > (double)0x7fffffff)
    inst_delay_d = (double)0x7fffffff;
inst_delay = (int32)inst_delay_d;
if ((inst_delay == 0) && (usec_delay != 0))
    inst_delay = 1;     /* Minimum non-zero delay is 1 instruction */
#if defined(SIM_ASYNCH_CLOCKS)
if ((sim_calb_tmr == -1) ||                             /* if No timer initialized */
    (inst_delay < rtc_currd[sim_calb_tmr]) ||           /*    or sooner than next clock tick? */
    (rtc_calibrations[sim_calb_tmr] == 0) ||            /*    or haven't calibrated yet */
    (!sim_asynch_timer)) {                              /*    or asynch disabled */
    sim_debug (DBG_TIM, &sim_timer_dev, "sim_timer_activate_after() - activating %s after %d instructions\n", 
               sim_uname(uptr), inst_delay);
    return _sim_activate (uptr, inst_delay);            /* queue it now */
    }
if (1) {
    double d_now = sim_timenow_double ();

    uptr->a_usec_delay = usec_delay;
    uptr->a_due_time = d_now + (double)(usec_delay)/1000000.0;
    uptr->a_due_gtime = sim_gtime () + (sim_timer_inst_per_sec () * (double)(usec_delay)/1000000.0);
    uptr->time = usec_delay;
    uptr->cancel = &_sim_wallclock_cancel;              /* bind cleanup method */
    uptr->a_is_active = &_sim_wallclock_is_active;
    if (tmr < SIM_NTIMERS) {                            /* Timer Unit? */
        sim_clock_unit[tmr]->cancel = &_sim_wallclock_cancel;
        sim_clock_unit[tmr]->a_is_active = &_sim_wallclock_is_active;
        }

    sim_debug (DBG_TIM, &sim_timer_dev, "sim_timer_activate_after() - queue wallclock addition %s at %.6f\n", 
               sim_uname(uptr), uptr->a_due_time);
    }
pthread_mutex_lock (&sim_timer_lock);
uptr->a_next = QUEUE_LIST_END;                          /* Temporarily mark as active */
while (sim_wallclock_entry) {                           /* wait for any prior entry has been digested */
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
sim_debug (DBG_TIM, &sim_timer_dev, "sim_timer_activate_after() - queue addition %s at %d (%d usecs)\n", 
           sim_uname(uptr), inst_delay, usec_delay);
return _sim_activate (uptr, inst_delay);                /* queue it now */
#endif
}

/* Clock coscheduling routines */

t_stat sim_register_clock_unit_tmr (UNIT *uptr, int32 tmr)
{
if (tmr == SIM_INTERNAL_CLK)
    tmr = SIM_NTIMERS;
else {
    if ((tmr < 0) || (tmr >= SIM_NTIMERS))
        return SCPE_IERR;
    }
if (NULL == uptr) {                         /* deregistering? */
    while (sim_clock_cosched_queue[tmr] != QUEUE_LIST_END) {
        UNIT *uptr = sim_clock_cosched_queue[tmr];

        _sim_coschedule_cancel (uptr);
        _sim_activate (uptr, 1);
        }
    sim_clock_unit[tmr] = NULL;
    return SCPE_OK;
    }
if (NULL == sim_clock_unit[tmr])
    sim_clock_cosched_queue[tmr] = QUEUE_LIST_END;
sim_clock_unit[tmr] = uptr;
uptr->dynflags |= UNIT_TMR_UNIT;
sim_timer_units[tmr].flags = UNIT_DIS | (sim_clock_unit[tmr] ? UNIT_IDLE : 0);
return SCPE_OK;
}

/* Default timer is 0, otherwise use a calibrated one if it exists */
static int32 _default_tmr ()
{
return (rtc_currd[0] ? 0 : ((sim_calb_tmr != -1) ? rtc_currd[sim_calb_tmr] : 0));
}

static int32 _tick_size ()
{
return (rtc_currd[_default_tmr ()] ? rtc_currd[_default_tmr ()] : 10000);
}

int32 sim_rtcn_tick_size (int32 tmr)
{
return (rtc_currd[tmr]) ? rtc_currd[tmr] : 10000;
}

t_stat sim_register_clock_unit (UNIT *uptr)
{
return sim_register_clock_unit_tmr (uptr, 0);
}

t_stat sim_clock_coschedule (UNIT *uptr, int32 interval)
{
int32 ticks = (interval + (_tick_size ()/2))/_tick_size ();/* Convert to ticks */

sim_debug (DBG_QUE, &sim_timer_dev, "sim_clock_coschedule(%s, interval=%d, ticks=%d)\n", sim_uname(uptr), interval, ticks);
return sim_clock_coschedule_tmr (uptr, _default_tmr (), ticks);
}

t_stat sim_clock_coschedule_abs (UNIT *uptr, int32 interval)
{
int32 ticks = (interval + (_tick_size ()/2))/_tick_size ();/* Convert to ticks */

sim_debug (DBG_QUE, &sim_timer_dev, "sim_clock_coschedule_abs(%s, interval=%d, ticks=%d)\n", sim_uname(uptr), interval, ticks);
sim_cancel (uptr);
return sim_clock_coschedule_tmr (uptr, _default_tmr (), ticks);
}

t_stat sim_clock_coschedule_tmr (UNIT *uptr, int32 tmr, int32 ticks)
{
if (ticks < 0)
    return SCPE_ARG;
if (sim_is_active (uptr)) {
    sim_debug (DBG_TIM, &sim_timer_dev, "sim_clock_coschedule_tmr(tmr=%d) - %s is already active\n", tmr, sim_uname (uptr));
    return SCPE_OK;
    }
if (tmr == SIM_INTERNAL_CLK)
    tmr = SIM_NTIMERS;
else {
    if ((tmr < 0) || (tmr >= SIM_NTIMERS))
        return sim_activate (uptr, MAX(1, ticks) * 10000);
    }
if (NULL == sim_clock_unit[tmr])
    return sim_activate (uptr, ticks * (rtc_currd[tmr] ? rtc_currd[tmr] : _tick_size ()));
else {
    UNIT *cptr, *prvptr;
    int32 accum;

    sim_debug (DBG_QUE, &sim_timer_dev, "sim_clock_coschedule_tmr(tmr=%d) - queueing %s for clock co-schedule (ticks=%d)\n", tmr, sim_uname (uptr), ticks);
    prvptr = NULL;
    accum = 0;
    for (cptr = sim_clock_cosched_queue[tmr]; cptr != QUEUE_LIST_END; cptr = cptr->next) {
        if (ticks < (accum + cptr->time))
            break;
        accum = accum + cptr->time;
        prvptr = cptr;
        }
    if (prvptr == NULL) {
        cptr = uptr->next = sim_clock_cosched_queue[tmr];
        sim_clock_cosched_queue[tmr] = uptr;
        }
    else {
        cptr = uptr->next = prvptr->next;
        prvptr->next = uptr;
        }
    uptr->time = ticks - accum;
    if (cptr != QUEUE_LIST_END)
        cptr->time = cptr->time - uptr->time;
    uptr->cancel = &_sim_coschedule_cancel;             /* bind cleanup method */
    sim_cosched_interval[tmr] = sim_clock_cosched_queue[tmr]->time;
    }
return SCPE_OK;
}

t_stat sim_clock_coschedule_tmr_abs (UNIT *uptr, int32 tmr, int32 ticks)
{
sim_cancel (uptr);
return sim_clock_coschedule_tmr (uptr, tmr, ticks);
}

/* Cancel a unit on the coschedule queue */
static void _sim_coschedule_cancel (UNIT *uptr)
{
AIO_UPDATE_QUEUE;
if (uptr->next) {                           /* On a queue? */
    int tmr;

    for (tmr=0; tmr<SIM_NTIMERS; tmr++) {
        if (uptr == sim_clock_cosched_queue[tmr]) {
            sim_clock_cosched_queue[tmr] = uptr->next;
            uptr->next = NULL;
            }
        else {
            UNIT *cptr;

            for (cptr = sim_clock_cosched_queue[tmr];
                (cptr != QUEUE_LIST_END);
                cptr = cptr->next)
                if (cptr->next == (uptr)) {
                    cptr->next = (uptr)->next;
                    uptr->next = NULL;
                    break;
                    }
            }
        if (uptr->next == NULL) {           /* found? */
            uptr->cancel = NULL;
            sim_debug (SIM_DBG_EVENT, &sim_timer_dev, "Canceled Clock Coscheduled Event for %s\n", sim_uname(uptr));
            return;
            }
        }
    }
}

#if defined(SIM_ASYNCH_CLOCKS)
static void _sim_wallclock_cancel (UNIT *uptr)
{
int32 tmr;

AIO_UPDATE_QUEUE;
pthread_mutex_lock (&sim_timer_lock);
/* If this is a clock unit, we need to cancel both this and the related timer unit */
for (tmr=0; tmr<SIM_NTIMERS; tmr++)
    if (sim_clock_unit[tmr] == uptr) {
        uptr = &sim_timer_units[tmr];
        break;
        }
if (uptr->a_next) {
    UNIT *cptr;

    if (uptr == sim_wallclock_entry) {  /* Pending on the queue? */
        sim_wallclock_entry = NULL;
        uptr->a_next = NULL;
        }
    else {
        if (uptr == sim_wallclock_queue) {
            sim_wallclock_queue = uptr->a_next;
            uptr->a_next = NULL;
            sim_debug (SIM_DBG_EVENT, &sim_timer_dev, "Canceling Timer Event for %s\n", sim_uname(uptr));
            pthread_cond_signal (&sim_timer_wake);
            }
        else {
            for (cptr = sim_wallclock_queue;
                (cptr != QUEUE_LIST_END);
                cptr = cptr->a_next) {
                if (cptr->a_next == (uptr)) {
                    cptr->a_next = (uptr)->a_next;
                    uptr->a_next = NULL;
                    sim_debug (SIM_DBG_EVENT, &sim_timer_dev, "Canceled Timer Event for %s\n", sim_uname(uptr));
                    break;
                    }
                }
            }
        }
    if (uptr->a_next == NULL) {
        uptr->a_due_time = uptr->a_due_gtime = uptr->a_usec_delay = 0;
        uptr->cancel = NULL;
        uptr->a_is_active = NULL;
        if (tmr < SIM_NTIMERS) {                        /* Timer Unit? */
            sim_clock_unit[tmr]->cancel = NULL;
            sim_clock_unit[tmr]->a_is_active = NULL;
            }
        }
    }
pthread_mutex_unlock (&sim_timer_lock);
}

int32 sim_timer_activate_time (UNIT *uptr)
{
UNIT *cptr;
double d_result;
int32 tmr;

if (uptr->a_is_active == &_sim_wallclock_is_active) {
    pthread_mutex_lock (&sim_timer_lock);
    if (uptr == sim_wallclock_entry) {
        d_result = uptr->a_due_gtime - sim_gtime ();
        if (d_result < 0.0)
            d_result = 0.0;
        if (d_result > (double)0x7FFFFFFE)
            d_result = (double)0x7FFFFFFE;
        pthread_mutex_unlock (&sim_timer_lock);
        return ((int32)d_result) + 1;
        }
    for (cptr = sim_wallclock_queue;
         cptr != QUEUE_LIST_END;
         cptr = cptr->a_next)
        if (uptr == cptr) {
            d_result = uptr->a_due_gtime - sim_gtime ();
            if (d_result < 0.0)
                d_result = 0.0;
            if (d_result > (double)0x7FFFFFFE)
                d_result = (double)0x7FFFFFFE;
            pthread_mutex_unlock (&sim_timer_lock);
            return ((int32)d_result) + 1;
            }
    pthread_mutex_unlock (&sim_timer_lock);
    }
if (uptr->a_next)
    return uptr->a_event_time + 1;
for (tmr=0; tmr<SIM_NTIMERS; tmr++)
    if (sim_clock_unit[tmr] == uptr)
        return sim_activate_time (&sim_timer_units[tmr]);
return -1;                                          /* Not found. */    
}

static t_bool _sim_wallclock_is_active (UNIT *uptr)
{
int32 tmr;

if (uptr->a_next)
    return TRUE;
/* If this is a clock unit, we need to examine the related timer unit instead */
for (tmr=0; tmr<SIM_NTIMERS; tmr++)
    if (sim_clock_unit[tmr] == uptr)
        return (sim_timer_units[tmr].a_next != NULL);
return FALSE;
}

#endif /* defined(SIM_ASYNCH_CLOCKS) */
