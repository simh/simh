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
   sim_timer_activate_time  determine activation time
   sim_timer_activate_time_usecs determine activation time in usecs
   sim_rom_read_with_delay  delay for default or specified delay
   sim_get_rom_delay_factor get current or initialize 1usec delay factor
   sim_set_rom_delay_factor set specific delay factor


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

int32 sim_vm_initial_ips = SIM_INITIAL_IPS;

static int32 sim_precalibrate_ips = SIM_INITIAL_IPS;
static int32 sim_calb_tmr = -1;                     /* the system calibrated timer */
static int32 sim_calb_tmr_last = -1;                /* shadow value when at sim> prompt */
static double sim_inst_per_sec_last = 0;            /* shadow value when at sim> prompt */
static uint32 sim_stop_time = 0;                    /* time when sim_stop_timer_services was called */
double sim_time_at_sim_prompt =  0;                 /* time spent processing commands from sim> prompt */

static uint32 sim_idle_rate_ms = 0;                 /* Minimum Sleep time */
static uint32 sim_os_sleep_min_ms = 0;
static uint32 sim_os_sleep_inc_ms = 0;
static uint32 sim_os_clock_resoluton_ms = 0;
static uint32 sim_os_tick_hz = 0;
static uint32 sim_idle_stable = SIM_IDLE_STDFLT;
static uint32 sim_idle_calib_pct = 100;
static double sim_timer_stop_time = 0;
static uint32 sim_rom_delay = 0;
static uint32 sim_throt_ms_start = 0;
static uint32 sim_throt_ms_stop = 0;
static uint32 sim_throt_type = 0;
static uint32 sim_throt_val = 0;
static uint32 sim_throt_drift_pct = SIM_THROT_DRIFT_PCT_DFLT;
static uint32 sim_throt_state = SIM_THROT_STATE_INIT;
static double sim_throt_cps;
static double sim_throt_peak_cps;
static double sim_throt_inst_start;
static uint32 sim_throt_sleep_time = 0;
static int32 sim_throt_wait = 0;
static uint32 sim_throt_delay = 3;
#define CLK_TPS 100
#define CLK_INIT (sim_precalibrate_ips/CLK_TPS)
static int32 sim_int_clk_tps;

typedef struct RTC {
    UNIT *clock_unit;               /* registered ticking clock unit */
    UNIT *timer_unit;               /* points to related clock assist unit (sim_timer_units) */
    UNIT *clock_cosched_queue;
    int32 cosched_interval;
    uint32 ticks;                   /* ticks */
    uint32 hz;                      /* tick rate */
    uint32 last_hz;                 /* prior tick rate */
    uint32 rtime;                   /* real time (usecs) */
    uint32 vtime;                   /* virtual time (usecs) */
    double gtime;                   /* instruction time */
    uint32 nxintv;                  /* next interval */
    int32 based;                    /* base delay */
    int32 currd;                    /* current delay */
    int32 initd;                    /* initial delay */
    uint32 elapsed;                 /* seconds since init */
    uint32 calibrations;            /* calibration count */
    double clock_skew_max;          /* asynchronous max skew */
    double clock_tick_size;         /* 1/hz */
    uint32 calib_initializations;   /* Initialization Count */
    double calib_tick_time;         /* ticks time */
    double calib_tick_time_tot;     /* ticks time - total*/
    uint32 calib_ticks_acked;       /* ticks Acked */
    uint32 calib_ticks_acked_tot;   /* ticks Acked - total */
    uint32 clock_ticks;             /* ticks delivered since catchup base */
    uint32 clock_ticks_tot;         /* ticks delivered since catchup base - total */
    double clock_init_base_time;    /* reference time for clock initialization */
    double clock_tick_start_time;   /* reference time when ticking started */
    double clock_catchup_base_time; /* reference time for catchup ticks */
    uint32 clock_catchup_ticks;     /* Record of catchups */
    uint32 clock_catchup_ticks_tot; /* Record of catchups - total */
    uint32 clock_catchup_ticks_curr;/* Record of catchups in this second */
    t_bool clock_catchup_pending;   /* clock tick catchup pending */
    t_bool clock_catchup_eligible;  /* clock tick catchup eligible */
    uint32 clock_time_idled;        /* total time idled */
    uint32 clock_time_idled_last;   /* total time idled as of the previous second */
    uint32 clock_calib_skip_idle;   /* Calibrations skipped due to idling */
    uint32 clock_calib_gap2big;     /* Calibrations skipped Gap Too Big */
    uint32 clock_calib_backwards;   /* Calibrations skipped Clock Running Backwards */
    } RTC;

RTC rtcs[SIM_NTIMERS+1];
UNIT sim_timer_units[SIM_NTIMERS+1];/* Clock assist units                         */
                                    /* one for each timer and one for an internal */
                                    /* clock if no clocks are registered.         */


static t_bool sim_catchup_ticks = TRUE;
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
real_sim_idle_ms_sleep (2);         /* Start sampling on a tick boundary */
for (i = 0, tot = 0; i < sleep1Samples; i++)
    tot += real_sim_idle_ms_sleep (1);
tim = tot / sleep1Samples;          /* Truncated average */
real_sim_os_sleep_min_ms = tim;
real_sim_idle_ms_sleep (2);         /* Start sampling on a tick boundary */
for (i = 0, tot = 0; i < sleep1Samples; i++)
    tot += real_sim_idle_ms_sleep (real_sim_os_sleep_min_ms + 1);
tim = tot / sleep1Samples;          /* Truncated average */
real_sim_os_sleep_inc_ms = tim - real_sim_os_sleep_min_ms;
#endif /* defined(MS_MIN_GRANULARITY) && (MS_MIN_GRANULARITY != 1) */
sim_idle_ms_sleep (2);              /* Start sampling on a tick boundary */
for (i = 0, tot = 0; i < sleep1Samples; i++)
    tot += sim_idle_ms_sleep (1);
tim = tot / sleep1Samples;          /* Truncated average */
sim_os_sleep_min_ms = tim;
sim_idle_ms_sleep (2);              /* Start sampling on a tick boundary */
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
struct timespec start_time, end_time, done_time, delta_time;
uint32 delta_ms;
t_bool timedout = FALSE;

clock_gettime(CLOCK_REALTIME, &start_time);
end_time = start_time;
end_time.tv_sec += (msec/1000);
end_time.tv_nsec += 1000000*(msec%1000);
if (end_time.tv_nsec >= 1000000000) {
  end_time.tv_sec += end_time.tv_nsec/1000000000;
  end_time.tv_nsec = end_time.tv_nsec%1000000000;
  }
pthread_mutex_lock (&sim_asynch_lock);
sim_idle_wait = TRUE;
if (pthread_cond_timedwait (&sim_asynch_wake, &sim_asynch_lock, &end_time))
    timedout = TRUE;
else
    sim_asynch_check = 0;                 /* force check of asynch queue now */
sim_idle_wait = FALSE;
pthread_mutex_unlock (&sim_asynch_lock);
clock_gettime(CLOCK_REALTIME, &done_time);
if (!timedout) {
    AIO_UPDATE_QUEUE;
    }
sim_timespec_diff (&delta_time, &done_time, &start_time);
delta_ms = (uint32)((delta_time.tv_sec * 1000) + ((delta_time.tv_nsec + 500000) / 1000000));
return delta_ms;
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
return timeGetTime ();                      /* use Multi-Media time source */
}

void sim_os_sleep (unsigned int sec)
{
Sleep (sec * 1000);
}

static TIMECAPS timers;

void sim_timer_exit (void)
{
timeEndPeriod (timers.wPeriodMin);
}

uint32 sim_os_ms_sleep_init (void)
{
MMRESULT mm_status;

mm_status = timeGetDevCaps (&timers, sizeof (timers));
if (mm_status != TIMERR_NOERROR) {
    fprintf (stderr, "timeGetDevCaps() returned: 0x%X, Last Error: 0x%X\n", mm_status, (unsigned int)GetLastError());
    return 0;
    }
if (timers.wPeriodMin == 0) {
    fprintf (stderr, "Unreasonable MultiMedia timer minimum value of 0\n");
    return 0;
    }
mm_status = timeBeginPeriod (timers.wPeriodMin);
if (mm_status != TIMERR_NOERROR) {
    fprintf (stderr, "timeBeginPeriod() returned: 0x%X, Last Error: 0x%X\n", mm_status, (unsigned int)GetLastError());
    return 0;
    }
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
while (diff->tv_nsec >= 1000000000) {
    ++diff->tv_sec;
    diff->tv_nsec -= 1000000000;
    }
}

/* Forward declarations */

static double _timespec_to_double (struct timespec *time);
static void _double_to_timespec (struct timespec *time, double dtime);
static t_bool _rtcn_tick_catchup_check (RTC *rtc, int32 time);
static void _rtcn_configure_calibrated_clock (int32 newtmr);
static t_bool _sim_coschedule_cancel (UNIT *uptr);
static t_bool _sim_wallclock_cancel (UNIT *uptr);
static t_bool _sim_wallclock_is_active (UNIT *uptr);
t_stat sim_timer_show_idle_mode (FILE* st, UNIT* uptr, int32 val, CONST void *  desc);


#if defined(SIM_ASYNCH_CLOCKS)
static int sim_timespec_compare (struct timespec *a, struct timespec *b)
{
while (a->tv_nsec >= 1000000000) {
    a->tv_nsec -= 1000000000;
    ++a->tv_sec;
    }
while (b->tv_nsec >= 1000000000) {
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

static uint32 sim_idle_cyc_ms = 0;                          /* Cycles per millisecond while not idling */
static uint32 sim_idle_cyc_sleep = 0;                       /* Cycles per minimum sleep interval */
static double sim_idle_end_time = 0.0;                      /* Time when last idle completed */

UNIT sim_stop_unit;                                     /* Stop unit                         */
UNIT sim_internal_timer_unit;                           /* Internal calibration timer */
int32 sim_internal_timer_time;                          /* Pending internal timer delay */
UNIT sim_throttle_unit;                                 /* one for throttle */

t_stat sim_throt_svc (UNIT *uptr);
t_stat sim_timer_tick_svc (UNIT *uptr);
t_stat sim_timer_stop_svc (UNIT *uptr);


#define DBG_IDL       TIMER_DBG_IDLE        /* idling */
#define DBG_QUE       TIMER_DBG_QUEUE       /* queue activities */
#define DBG_MUX       TIMER_DBG_MUX         /* tmxr queue activities */
#define DBG_TRC       0x008                 /* tracing */
#define DBG_CAL       0x010                 /* calibration activities */
#define DBG_TIM       0x020                 /* timer thread activities */
#define DBG_THR       0x040                 /* throttle activities */
#define DBG_ACK       0x080                 /* interrupt acknowledgement activities */
#define DBG_CHK       0x100                 /* check scheduled activation time*/
#define DBG_INT       0x200                 /* internal timer activities */
#define DBG_GET       0x400                 /* get_time activities */
#define DBG_TIK       0x800                 /* tick activities */
DEBTAB sim_timer_debug[] = {
  {"TRACE",   DBG_TRC, "Trace routine calls"},
  {"IDLE",    DBG_IDL, "Idling activities"},
  {"QUEUE",   DBG_QUE, "Event queuing activities"},
  {"IACK",    DBG_ACK, "interrupt acknowledgement activities"},
  {"CALIB",   DBG_CAL, "Calibration activities"},
  {"TICK",    DBG_TIK, "Calibration tick activities"},
  {"TIME",    DBG_TIM, "Activation and scheduling activities"},
  {"GETTIME", DBG_GET, "get_time activities"},
  {"INTER",   DBG_INT, "Internal timer activities"},
  {"THROT",   DBG_THR, "Throttling activities"},
  {"MUX",     DBG_MUX, "Tmxr scheduling activities"},
  {"CHECK",   DBG_CHK, "Check scheduled activation time"},
  {0}
};

/* Forward device declarations */
extern DEVICE sim_timer_dev;
extern DEVICE sim_throttle_dev;
extern DEVICE sim_stop_dev;


void sim_rtcn_init_all (void)
{
int32 tmr;
RTC *rtc;

for (tmr = 0; tmr <= SIM_NTIMERS; tmr++) {
    rtc = &rtcs[tmr];
    if (rtc->initd != 0)
        sim_rtcn_init (rtc->initd, tmr);
    }
}

int32 sim_rtcn_init (int32 time, int32 tmr)
{
return sim_rtcn_init_unit (NULL, time, tmr);
}

int32 sim_rtcn_init_unit (UNIT *uptr, int32 time, int32 tmr)
{
return sim_rtcn_init_unit_ticks (uptr, time, tmr, 0);
}

int32 sim_rtcn_init_unit_ticks (UNIT *uptr, int32 time, int32 tmr, int32 ticksper)
{
RTC *rtc;
        
if (time == 0)
    time = 1;
if (tmr == SIM_INTERNAL_CLK)
    tmr = SIM_NTIMERS;
else {
    if ((tmr < 0) || (tmr >= SIM_NTIMERS))
        return time;
    }
rtc = &rtcs[tmr];
/*
 * If we'd previously succeeded in calibrating a tick value, then use that
 * delay as a better default to setup when we're re-initialized.
 * Re-initializing happens on any boot.
 */
if (rtc->currd)
    time = rtc->currd;
if (!uptr)
    uptr = rtc->clock_unit;
sim_debug (DBG_CAL, &sim_timer_dev, "sim_rtcn_init_unit(unit=%s, time=%d, tmr=%d)\n", uptr ? sim_uname(uptr) : "", time, tmr);
if (uptr) {
    if (!rtc->clock_unit)
        sim_register_clock_unit_tmr (uptr, tmr);
    }
rtc->gtime = sim_gtime();
rtc->rtime = sim_is_running ? sim_os_msec () : sim_stop_time;
rtc->vtime = rtc->rtime;
rtc->nxintv = 1000;
rtc->ticks = 0;
rtc->last_hz = rtc->hz;
rtc->hz = ticksper;
rtc->based = time;
rtc->currd = time;
rtc->initd = time;
rtc->elapsed = 0;
rtc->calibrations = 0;
rtc->clock_ticks_tot += rtc->clock_ticks;
rtc->clock_ticks = 0;
rtc->calib_tick_time_tot += rtc->calib_tick_time;
rtc->calib_tick_time = 0;
rtc->clock_catchup_pending = FALSE;
rtc->clock_catchup_eligible = FALSE;
rtc->clock_catchup_ticks_tot += rtc->clock_catchup_ticks;
rtc->clock_catchup_ticks = 0;
rtc->clock_catchup_ticks_curr = 0;
rtc->calib_ticks_acked_tot += rtc->calib_ticks_acked;
rtc->calib_ticks_acked = 0;
++rtc->calib_initializations;
rtc->clock_init_base_time = sim_timenow_double ();
_rtcn_configure_calibrated_clock (tmr);
return time;
}

int32 sim_rtcn_calb_tick (int32 tmr)
{
RTC *rtc = &rtcs[tmr];

return sim_rtcn_calb (rtc->hz, tmr);
}

int32 sim_rtcn_calb (uint32 ticksper, int32 tmr)
{
uint32 new_rtime, delta_rtime, last_idle_pct, catchup_ticks_curr;
int32 delta_vtime;
double new_gtime;
int32 new_currd;
int32 itmr;
RTC *rtc;

if (tmr == SIM_INTERNAL_CLK)
    tmr = SIM_NTIMERS;
else {
    if ((tmr < 0) || (tmr >= SIM_NTIMERS))
        return 10000;
    }
rtc = &rtcs[tmr];
if (rtc->hz != ticksper) {                          /* changing tick rate? */
    uint32 prior_hz = rtc->hz;

    if (rtc->hz == 0)
        rtc->clock_tick_start_time = sim_timenow_double ();
    if ((rtc->last_hz != 0) && 
        (rtc->last_hz != ticksper) && 
        (ticksper != 0))
        rtc->currd = (int32)(sim_timer_inst_per_sec () / ticksper);
    rtc->last_hz = rtc->hz;
    rtc->hz = ticksper;
    _rtcn_configure_calibrated_clock (tmr);
    if (ticksper != 0) {
        RTC *crtc = &rtcs[sim_calb_tmr];

        rtc->clock_tick_size = 1.0 / ticksper;
        sim_debug (DBG_CAL, &sim_timer_dev, "sim_rtcn_calb(ticksper=%d,tmr=%d) currd=%d, prior_hz=%d\n", ticksper, tmr, rtc->currd, (int)prior_hz);

        if ((tmr != sim_calb_tmr) && rtc->clock_unit && (ticksper > crtc->hz)) {
            sim_catchup_ticks = TRUE;
            sim_debug (DBG_CAL, &sim_timer_dev, "sim_rtcn_calb(%d) - forcing catchup ticks for %s ticking at %d, host tick rate %ds\n", tmr, sim_uname (rtc->clock_unit), ticksper, sim_os_tick_hz);
            _rtcn_tick_catchup_check (rtc, 0);
            }
        }
    else
        sim_debug (DBG_CAL, &sim_timer_dev, "sim_rtcn_calb(ticksper=%d,tmr=%d) timer stopped currd was %d, prior_hz=%d\n", ticksper, tmr, rtc->currd, (int)prior_hz);
    }
if (ticksper == 0)                                      /* running? */
    return 10000;
if (rtc->clock_unit == NULL) {                      /* Not using TIMER units? */
    rtc->clock_ticks += 1;
    rtc->calib_tick_time += rtc->clock_tick_size;
    }
if (rtc->clock_catchup_pending) {                   /* catchup tick? */
    ++rtc->clock_catchup_ticks;                     /* accumulating which were catchups */
    ++rtc->clock_catchup_ticks_curr;
    rtc->clock_catchup_pending = FALSE;
    }
rtc->ticks += 1;                                    /* count ticks */
if (rtc->ticks < ticksper)                          /* 1 sec yet? */
    return rtc->currd;
catchup_ticks_curr = rtc->clock_catchup_ticks_curr;
rtc->clock_catchup_ticks_curr = 0;
rtc->ticks = 0;                                     /* reset ticks */
rtc->elapsed += 1;                                  /* count sec */
if (!rtc_avail)                                     /* no timer? */
    return rtc->currd;
if (sim_calb_tmr != tmr) {
    rtc->currd = (int32)(sim_timer_inst_per_sec()/ticksper);
    sim_debug (DBG_CAL, &sim_timer_dev, "sim_rtcn_calb(tmr=%d) calibrated against internal system tmr=%d, tickper=%d (result: %d)\n", tmr, sim_calb_tmr, ticksper, rtc->currd);
    return rtc->currd;
    }
new_rtime = sim_os_msec ();                         /* wall time */
if (!sim_signaled_int_char && 
    ((new_rtime - sim_last_poll_kbd_time) > 500)) {
    sim_debug (DBG_CAL, &sim_timer_dev, "sim_rtcn_calb(tmr=%d) gratuitious keyboard poll after %d msecs\n", tmr, (int)(new_rtime - sim_last_poll_kbd_time));
    (void)sim_poll_kbd ();
    }
++rtc->calibrations;                                /* count calibrations */
sim_debug (DBG_TRC, &sim_timer_dev, "sim_rtcn_calb(ticksper=%d, tmr=%d)\n", ticksper, tmr);
if (new_rtime < rtc->rtime) {                       /* time running backwards? */
    /* This happens when the value returned by sim_os_msec wraps (as an uint32) */
    /* Wrapping will happen initially sometime before a simulator has been running */
    /* for 49 days approximately every 49 days thereafter. */
    ++rtc->clock_calib_backwards;                   /* Count statistic */
    sim_debug (DBG_CAL, &sim_timer_dev, "time running backwards - OldTime: %u, NewTime: %u, result: %d\n", rtc->rtime, new_rtime, rtc->currd);
    rtc->vtime = rtc->rtime = new_rtime;            /* reset wall time */
    rtc->nxintv = 1000;
    rtc->based = rtc->currd;
    if (rtc->clock_catchup_eligible) {
        rtc->clock_catchup_base_time = sim_timenow_double();
        rtc->calib_tick_time = 0.0;
        }
    return rtc->currd;                              /* can't calibrate */
    }
delta_rtime = new_rtime - rtc->rtime;               /* elapsed wtime */
rtc->rtime = new_rtime;                             /* adv wall time */
rtc->vtime += 1000;                                 /* adv sim time */
if (delta_rtime > 30000) {                          /* gap too big? */
    /* This simulator process has somehow been suspended for a significant */
    /* amount of time.  This will certainly happen if the host system has  */
    /* slept or hibernated.  It also might happen when a simulator         */
    /* developer stops the simulator at a breakpoint (a process, not simh  */
    /* breakpoint).  To accomodate this, we set the calibration state to   */
    /* ignore what happened and proceed from here.                         */
    ++rtc->clock_calib_gap2big;                     /* Count statistic */
    rtc->vtime = rtc->rtime;                        /* sync virtual and real time */
    rtc->nxintv = 1000;                             /* reset next interval */
    rtc->gtime = sim_gtime();                       /* save instruction time */
    rtc->based = rtc->currd;
    if (rtc->clock_catchup_eligible)
        rtc->calib_tick_time += ((double)delta_rtime / 1000.0);/* advance tick time */
    sim_debug (DBG_CAL, &sim_timer_dev, "gap too big: delta = %d - result: %d\n", delta_rtime, rtc->currd);
    return rtc->currd;                              /* can't calibr */
    }
last_idle_pct = 0;                                  /* normally force calibration */
if (tmr != SIM_NTIMERS) {
    if (delta_rtime != 0)                           /* avoid divide by zero  */
        last_idle_pct = MIN(100, (uint32)(100.0 * (((double)(rtc->clock_time_idled - rtc->clock_time_idled_last)) / ((double)delta_rtime))));
    rtc->clock_time_idled_last = rtc->clock_time_idled;
    if (last_idle_pct > sim_idle_calib_pct) {
        rtc->rtime = new_rtime;                     /* save wall time */
        rtc->vtime += 1000;                         /* adv sim time */
        rtc->gtime = sim_gtime();                   /* save instruction time */
        rtc->based = rtc->currd;
        ++rtc->clock_calib_skip_idle;
        sim_debug (DBG_CAL, &sim_timer_dev, "skipping calibration due to idling (%d%%) - result: %d\n", last_idle_pct, rtc->currd);
        return rtc->currd;                          /* avoid calibrating idle checks */
        }
    }
new_gtime = sim_gtime();
if ((last_idle_pct == 0) && (delta_rtime != 0)) {
    sim_idle_cyc_ms = (uint32)((new_gtime - rtc->gtime) / delta_rtime);
    if ((sim_idle_rate_ms != 0) && (delta_rtime > 1))
        sim_idle_cyc_sleep = (uint32)((new_gtime - rtc->gtime) / (delta_rtime / sim_idle_rate_ms));
    }
if (sim_asynch_timer || (catchup_ticks_curr > 0)) {
    /* An asynchronous clock or when catchup ticks have  */
    /* occurred, we merely needs to divide the number of */
    /* instructions actually executed by the clock rate. */
    new_currd = (int32)((new_gtime - rtc->gtime)/ticksper);
    /* avoid excessive swings in the calibrated result */
    if (new_currd > 10*rtc->currd)              /* don't swing big too fast */
        new_currd = 10*rtc->currd;
    else {
        if (new_currd < rtc->currd/10)          /* don't swing small too fast */
            new_currd = rtc->currd/10;
        }
    rtc->based = rtc->currd = new_currd;
    rtc->gtime = new_gtime;                     /* save instruction time */
    sim_debug (DBG_CAL, &sim_timer_dev, "sim_rtcn_calb(%s tmr=%d, tickper=%d) catchups=%u, idle=%d%% result: %d\n", 
                    sim_asynch_timer ? "asynch" : "catchup", tmr, ticksper, catchup_ticks_curr, last_idle_pct, rtc->currd);
    return rtc->currd;                          /* calibrated result */
    }
rtc->gtime = new_gtime;                         /* save instruction time */
/* This self regulating algorithm depends directly on the assumption */
/* that this routine is called back after processing the number of */
/* instructions which was returned the last time it was called. */
if (delta_rtime == 0)                           /* gap too small? */
    rtc->based = rtc->based * ticksper;         /* slew wide */
else
    rtc->based = (int32) (((double) rtc->based * (double) rtc->nxintv) /
                                ((double) delta_rtime));/* new base rate */
delta_vtime = rtc->vtime - rtc->rtime;          /* gap */
if (delta_vtime > SIM_TMAX)                     /* limit gap */
    delta_vtime = SIM_TMAX;
else {
    if (delta_vtime < -SIM_TMAX)
        delta_vtime = -SIM_TMAX;
    }
rtc->nxintv = 1000 + delta_vtime;                   /* next wtime */
rtc->currd = (int32) (((double) rtc->based * (double) rtc->nxintv) /
    1000.0);                                        /* next delay */
if (rtc->based <= 0)                                /* never negative or zero! */
    rtc->based = 1;
if (rtc->currd <= 0)                                /* never negative or zero! */
    rtc->currd = 1;
sim_debug (DBG_CAL, &sim_timer_dev, "sim_rtcn_calb(tmr=%d, tickper=%d) (delta_rtime=%d, delta_vtime=%d, base=%d, nxintv=%u, catchups=%u, idle=%d%%, result: %d)\n", 
                                    tmr, ticksper, (int)delta_rtime, (int)delta_vtime, rtc->based, rtc->nxintv, catchup_ticks_curr, last_idle_pct, rtc->currd);
/* Adjust calibration for other timers which depend on this timer's calibration */
for (itmr=0; itmr<=SIM_NTIMERS; itmr++) {
    RTC *irtc = &rtcs[itmr];

    if ((itmr != tmr) && (irtc->hz != 0))
        irtc->currd = (rtc->currd * ticksper) / irtc->hz;
    }
AIO_SET_INTERRUPT_LATENCY(rtc->currd * ticksper);   /* set interrrupt latency */
return rtc->currd;
}

/* Prior interfaces - default to timer 0 */

int32 sim_rtc_init (int32 time)
{
return sim_rtcn_init (time, 0);
}

int32 sim_rtc_calb (uint32 ticksper)
{
return sim_rtcn_calb (ticksper, 0);
}

/* sim_timer_init - get minimum sleep time available on this host */

t_bool sim_timer_init (void)
{
int tmr;
uint32 clock_start, clock_last, clock_now;

sim_debug (DBG_TRC, &sim_timer_dev, "sim_timer_init()\n");
/* Clear the event queue before initializing the timer subsystem */
while (sim_clock_queue != QUEUE_LIST_END)
    sim_cancel (sim_clock_queue);
for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    RTC *rtc = &rtcs[tmr];

    rtc->timer_unit = &sim_timer_units[tmr];
    rtc->timer_unit->action = &sim_timer_tick_svc;
    rtc->timer_unit->flags = UNIT_DIS | UNIT_IDLE;
    if (rtc->clock_cosched_queue)
        while (rtc->clock_cosched_queue != QUEUE_LIST_END)
            sim_cancel (rtc->clock_cosched_queue);
    rtc->clock_cosched_queue = QUEUE_LIST_END;
    }
sim_stop_unit.action = &sim_timer_stop_svc;
SIM_INTERNAL_UNIT.flags = UNIT_IDLE;
sim_register_internal_device (&sim_timer_dev);          /* Register Clock Assist device */
sim_register_internal_device (&sim_throttle_dev);       /* Register Throttle Device */
sim_throttle_unit.action = &sim_throt_svc;
sim_register_clock_unit_tmr (&SIM_INTERNAL_UNIT, SIM_INTERNAL_CLK);
sim_idle_enab = FALSE;                                  /* init idle off */
sim_idle_rate_ms = sim_os_ms_sleep_init ();             /* get OS timer rate */
sim_set_rom_delay_factor (sim_get_rom_delay_factor ()); /* initialize ROM delay factor */

sim_stop_time = clock_last = clock_start = sim_os_msec ();
sim_os_clock_resoluton_ms = 1000;
do {
    uint32 clock_diff;
    
    clock_now = sim_os_msec ();
    clock_diff = clock_now - clock_last;
    if ((clock_diff > 0) && (clock_diff < sim_os_clock_resoluton_ms))
        sim_os_clock_resoluton_ms = clock_diff;
    clock_last = clock_now;
    } while (clock_now < clock_start + 100);
if ((sim_idle_rate_ms != 0) && (sim_os_clock_resoluton_ms != 0))
    sim_os_tick_hz = 1000/(sim_os_clock_resoluton_ms * (sim_idle_rate_ms/sim_os_clock_resoluton_ms));
else {
    fprintf (stderr, "Can't properly determine host system clock capabilities.\n");
    fprintf (stderr, "Minimum Host Sleep Time:       %u ms\n", sim_os_sleep_min_ms);
    fprintf (stderr, "Minimum Host Sleep Incr Time:  %u ms\n", sim_os_sleep_inc_ms);
    fprintf (stderr, "Host Clock Resolution:         %u ms\n", sim_os_clock_resoluton_ms);
    }
return ((sim_idle_rate_ms == 0) || (sim_os_clock_resoluton_ms == 0));
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
double inst_per_sec = sim_timer_inst_per_sec ();

fprintf (st, "Minimum Host Sleep Time:        %d ms (%dHz)\n", sim_os_sleep_min_ms, sim_os_tick_hz);
if (sim_os_sleep_min_ms != sim_os_sleep_inc_ms)
    fprintf (st, "Minimum Host Sleep Incr Time:   %d ms\n", sim_os_sleep_inc_ms);
fprintf (st, "Host Clock Resolution:          %d ms\n", sim_os_clock_resoluton_ms);
fprintf (st, "Execution Rate:                 %s %s/sec\n", sim_fmt_numeric (inst_per_sec), sim_vm_interval_units);
if (sim_idle_enab) {
    fprintf (st, "Idling:                         Enabled\n");
    fprintf (st, "Time before Idling starts:      %d seconds\n", sim_idle_stable);
    }
if (sim_throt_type != SIM_THROT_NONE) {
    sim_show_throt (st, NULL, uptr, val, desc);
    }
fprintf (st, "Calibrated Timer:               %s\n", (calb_tmr == -1) ? "Undetermined" : 
                                                     ((calb_tmr == SIM_NTIMERS) ? "Internal Timer" : 
                                                     (rtcs[calb_tmr].clock_unit ? sim_uname(rtcs[calb_tmr].clock_unit) : "")));
if (calb_tmr == SIM_NTIMERS)
    fprintf (st, "Catchup Ticks:                  %s\n", sim_catchup_ticks ? "Enabled" : "Disabled");
fprintf (st, "Pre-Calibration Estimated Rate: %s\n", sim_fmt_numeric ((double)sim_precalibrate_ips));
if (sim_idle_calib_pct == 100)
    fprintf (st, "Calibration:                    Always\n");
else
    fprintf (st, "Calibration:                    Skipped when Idle exceeds %d%%\n", sim_idle_calib_pct);
#if defined(SIM_ASYNCH_CLOCKS)
fprintf (st, "Asynchronous Clocks:            %s\n", sim_asynch_timer ? "Active" : "Available");
#endif
if (sim_time_at_sim_prompt != 0.0) {
    double prompt_time = 0.0;
    if (!sim_is_running)
        prompt_time = ((double)(sim_os_msec () - sim_stop_time)) / 1000.0;
    fprintf (st, "Time at sim> prompt:            %s\n", sim_fmt_secs (sim_time_at_sim_prompt + prompt_time));
    }

fprintf (st, "\n");
for (tmr=clocks=0; tmr<=SIM_NTIMERS; ++tmr) {
    RTC *rtc = &rtcs[tmr];

    if (0 == rtc->initd)
        continue;
    
    if (rtc->clock_unit) {
        ++clocks;
        fprintf (st, "%s clock device is %s%s%s\n", sim_name, 
                                                    (tmr == SIM_NTIMERS) ? "Internal Calibrated Timer(" : "", 
                                                    sim_uname(rtc->clock_unit), 
                                                    (tmr == SIM_NTIMERS) ? ")" : "");
        }

    fprintf (st, "%s%sTimer %d:\n", sim_asynch_timer ? "Asynchronous " : "", rtc->hz ? "Calibrated " : "Uncalibrated ", tmr);
    if (rtc->hz) {
        fprintf (st, "  Running at:                %d Hz\n", rtc->hz);
        fprintf (st, "  Tick Size:                 %s\n", sim_fmt_secs (rtc->clock_tick_size));
        fprintf (st, "  Ticks in current second:   %d\n",   rtc->ticks);
        }
    fprintf (st, "  Seconds Running:           %s (%s)\n",   sim_fmt_numeric ((double)rtc->elapsed), sim_fmt_secs ((double)rtc->elapsed));
    if (tmr == calb_tmr) {
        fprintf (st, "  Calibration Opportunities: %s\n",   sim_fmt_numeric ((double)rtc->calibrations));
        if (sim_idle_calib_pct && (sim_idle_calib_pct != 100))
            fprintf (st, "  Calib Skip when Idle >:    %u%%\n",   sim_idle_calib_pct);
        if (rtc->clock_calib_skip_idle)
            fprintf (st, "  Calibs Skip While Idle:    %s\n",   sim_fmt_numeric ((double)rtc->clock_calib_skip_idle));
        if (rtc->clock_calib_backwards)
            fprintf (st, "  Calibs Skip Backwards:     %s\n",   sim_fmt_numeric ((double)rtc->clock_calib_backwards));
        if (rtc->clock_calib_gap2big)
            fprintf (st, "  Calibs Skip Gap Too Big:   %s\n",   sim_fmt_numeric ((double)rtc->clock_calib_gap2big));
        }
    if (rtc->gtime)
        fprintf (st, "  Instruction Time:          %.0f\n", rtc->gtime);
    if ((!sim_asynch_timer) && (sim_throt_type == SIM_THROT_NONE)) {
        fprintf (st, "  Real Time:                 %u\n",   rtc->rtime);
        fprintf (st, "  Virtual Time:              %u\n",   rtc->vtime);
        fprintf (st, "  Next Interval:             %s\n",   sim_fmt_numeric ((double)rtc->nxintv));
        fprintf (st, "  Base Tick Delay:           %s\n",   sim_fmt_numeric ((double)rtc->based));
        fprintf (st, "  Initial Insts Per Tick:    %s\n",   sim_fmt_numeric ((double)rtc->initd));
        }
    fprintf (st, "  Current Insts Per Tick:    %s\n",   sim_fmt_numeric ((double)rtc->currd));
    fprintf (st, "  Initializations:           %d\n",   rtc->calib_initializations);
    fprintf (st, "  Ticks:                     %s\n", sim_fmt_numeric ((double)(rtc->clock_ticks)));
    if (rtc->clock_ticks_tot+rtc->clock_ticks != rtc->clock_ticks)
        fprintf (st, "  Total Ticks:               %s\n", sim_fmt_numeric ((double)(rtc->clock_ticks_tot+rtc->clock_ticks)));
    if (rtc->clock_skew_max != 0.0)
        fprintf (st, "  Peak Clock Skew:           %s%s\n", sim_fmt_secs (fabs(rtc->clock_skew_max)), (rtc->clock_skew_max < 0) ? " fast" : " slow");
    if (rtc->calib_ticks_acked)
        fprintf (st, "  Ticks Acked:               %s\n",   sim_fmt_numeric ((double)rtc->calib_ticks_acked));
    if (rtc->calib_ticks_acked_tot+rtc->calib_ticks_acked != rtc->calib_ticks_acked)
        fprintf (st, "  Total Ticks Acked:         %s\n",   sim_fmt_numeric ((double)(rtc->calib_ticks_acked_tot+rtc->calib_ticks_acked)));
    if (rtc->calib_tick_time)
        fprintf (st, "  Tick Time:                 %s\n",   sim_fmt_secs (rtc->calib_tick_time));
    if (rtc->calib_tick_time_tot+rtc->calib_tick_time != rtc->calib_tick_time)
        fprintf (st, "  Total Tick Time:           %s\n",   sim_fmt_secs (rtc->calib_tick_time_tot+rtc->calib_tick_time));
    if (rtc->clock_catchup_ticks)
        fprintf (st, "  Catchup Ticks Sched:       %s\n",   sim_fmt_numeric ((double)rtc->clock_catchup_ticks));
    if (rtc->clock_catchup_ticks_curr)
        fprintf (st, "  Catchup Ticks this second: %s\n",   sim_fmt_numeric ((double)rtc->clock_catchup_ticks_curr));
    if (rtc->clock_catchup_ticks_tot+rtc->clock_catchup_ticks != rtc->clock_catchup_ticks)
        fprintf (st, "  Total Catchup Ticks Sched: %s\n",   sim_fmt_numeric ((double)(rtc->clock_catchup_ticks_tot+rtc->clock_catchup_ticks)));
    if (rtc->clock_init_base_time) {
        _double_to_timespec (&now, rtc->clock_init_base_time);
        time_t_now = (time_t)now.tv_sec;
        fprintf (st, "  Initialize Base Time:      %8.8s.%03d\n", 11+ctime(&time_t_now), (int)(now.tv_nsec/1000000));
        }
    if (rtc->clock_tick_start_time) {
        _double_to_timespec (&now, rtc->clock_tick_start_time);
        time_t_now = (time_t)now.tv_sec;
        fprintf (st, "  Tick Start Time:           %8.8s.%03d\n", 11+ctime(&time_t_now), (int)(now.tv_nsec/1000000));
        }
    clock_gettime (CLOCK_REALTIME, &now);
    time_t_now = (time_t)now.tv_sec;
    fprintf (st, "  Wall Clock Time Now:       %8.8s.%03d\n", 11+ctime(&time_t_now), (int)(now.tv_nsec/1000000));
    if (sim_catchup_ticks && rtc->clock_catchup_eligible) {
        _double_to_timespec (&now, rtc->clock_catchup_base_time+rtc->calib_tick_time);
        time_t_now = (time_t)now.tv_sec;
        fprintf (st, "  Catchup Tick Time:         %8.8s.%03d\n", 11+ctime(&time_t_now), (int)(now.tv_nsec/1000000));
        _double_to_timespec (&now, rtc->clock_catchup_base_time);
        time_t_now = (time_t)now.tv_sec;
        fprintf (st, "  Catchup Base Time:         %8.8s.%03d\n", 11+ctime(&time_t_now), (int)(now.tv_nsec/1000000));
        }
    if (rtc->clock_time_idled)
        fprintf (st, "  Total Time Idled:          %s\n",   sim_fmt_secs (rtc->clock_time_idled/1000.0));
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
    struct timespec due;
    time_t time_t_due;

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
            _double_to_timespec (&due, uptr->a_due_time);
            time_t_due = (time_t)due.tv_sec;
            fprintf (st, " after %s due at %8.8s.%06d\n", tim, 11+ctime(&time_t_due), (int)(due.tv_nsec/1000));
            }
        }
    }
#endif /* SIM_ASYNCH_CLOCKS */
for (tmr=0; tmr<=SIM_NTIMERS; ++tmr) {
    RTC *rtc = &rtcs[tmr];

    if (rtc->clock_unit == NULL)
        continue;
    if (rtc->clock_cosched_queue != QUEUE_LIST_END) {
        int32 accum;

        fprintf (st, "%s #%d clock (%s) co-schedule event queue status\n",
                 sim_name, tmr, sim_uname(rtc->clock_unit));
        accum = 0;
        for (uptr = rtc->clock_cosched_queue; uptr != QUEUE_LIST_END; uptr = uptr->next) {
            if ((dptr = find_dev_from_unit (uptr)) != NULL) {
                fprintf (st, "  %s", sim_dname (dptr));
                if (dptr->numunits > 1)
                    fprintf (st, " unit %d", (int32) (uptr - dptr->units));
                }
            else
                fprintf (st, "  Unknown");
            if (accum == 0)
                fprintf (st, " on next tick");
            else
                fprintf (st, " after %d tick%s", accum, (accum > 1) ? "s" : "");
            if (uptr->usecs_remaining)
                fprintf (st, " plus %.0f usecs", uptr->usecs_remaining);
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

REG sim_timer_reg[] = {
    { DRDATAD (IDLE_CYC_MS,      sim_idle_cyc_ms,        32, "Cycles Per Millisecond"), PV_RSPC|REG_RO},
    { DRDATAD (IDLE_CYC_SLEEP,   sim_idle_cyc_sleep,     32, "Cycles Per Minimum Sleep"), PV_RSPC|REG_RO},
    { DRDATAD (IDLE_STABLE,      sim_idle_stable,        32, "IDLE stability delay"), PV_RSPC},
    { DRDATAD (ROM_DELAY,        sim_rom_delay,          32, "ROM memory reference delay"), PV_RSPC|REG_RO},
    { DRDATAD (TICK_RATE_0,      rtcs[0].hz,             32, "Timer 0 Ticks Per Second") },
    { DRDATAD (TICK_SIZE_0,      rtcs[0].currd,          32, "Timer 0 Tick Size") },
    { DRDATAD (TICK_RATE_1,      rtcs[1].hz,             32, "Timer 1 Ticks Per Second") },
    { DRDATAD (TICK_SIZE_1,      rtcs[1].currd,          32, "Timer 1 Tick Size") },
    { DRDATAD (TICK_RATE_2,      rtcs[2].hz,             32, "Timer 2 Ticks Per Second") },
    { DRDATAD (TICK_SIZE_2,      rtcs[2].currd,          32, "Timer 2 Tick Size") },
    { DRDATAD (TICK_RATE_3,      rtcs[3].hz,             32, "Timer 3 Ticks Per Second") },
    { DRDATAD (TICK_SIZE_3,      rtcs[3].currd,          32, "Timer 3 Tick Size") },
    { DRDATAD (TICK_RATE_4,      rtcs[4].hz,             32, "Timer 4 Ticks Per Second") },
    { DRDATAD (TICK_SIZE_4,      rtcs[4].currd,          32, "Timer 4 Tick Size") },
    { DRDATAD (TICK_RATE_5,      rtcs[5].hz,             32, "Timer 5 Ticks Per Second") },
    { DRDATAD (TICK_SIZE_5,      rtcs[5].currd,          32, "Timer 5 Tick Size") },
    { DRDATAD (TICK_RATE_6,      rtcs[6].hz,             32, "Timer 6 Ticks Per Second") },
    { DRDATAD (TICK_SIZE_6,      rtcs[6].currd,          32, "Timer 6 Tick Size") },
    { DRDATAD (TICK_RATE_7,      rtcs[7].hz,             32, "Timer 7 Ticks Per Second") },
    { DRDATAD (TICK_SIZE_7,      rtcs[7].currd,          32, "Timer 7 Tick Size") },
    { DRDATAD (INTERNAL_TICK_RATE,sim_int_clk_tps,       32, "Internal Timer Ticks Per Second") },
    { DRDATAD (INTERNAL_TICK_SIZE,rtcs[SIM_NTIMERS].currd,32, "Internal Timer Tick Size") },
    { NULL }
    };

REG sim_throttle_reg[] = {
    { DRDATAD (THROT_MS_START,   sim_throt_ms_start,     32, "Throttle measurement start time"), PV_RSPC|REG_RO},
    { DRDATAD (THROT_MS_STOP,    sim_throt_ms_stop,      32, "Throttle measurement stop time"), PV_RSPC|REG_RO},
    { DRDATAD (THROT_TYPE,       sim_throt_type,         32, "Throttle type"), PV_RSPC|REG_RO},
    { DRDATAD (THROT_VAL,        sim_throt_val,          32, "Throttle mode value"), PV_RSPC|REG_RO},
    { DRDATAD (THROT_STATE,      sim_throt_state,        32, "Throttle state"), PV_RSPC|REG_RO},
    { DRDATAD (THROT_SLEEP_TIME, sim_throt_sleep_time,   32, "Throttle sleep time"), PV_RSPC|REG_RO},
    { DRDATAD (THROT_WAIT,       sim_throt_wait,         32, "Throttle execution interval before sleep"), PV_RSPC|REG_RO},
    { DRDATAD (THROT_DELAY,      sim_throt_delay,        32, "Seconds before throttling starts"), PV_RSPC},
    { DRDATAD (THROT_DRIFT_PCT,  sim_throt_drift_pct,    32, "Percent of throttle drift before correction"), PV_RSPC},
    { NULL }
    };

/* Clear, Set and show catchup */

/* Set/Clear catchup */

t_stat sim_timer_set_catchup (int32 flag, CONST char *cptr)
{
if (flag) {
    if (!sim_catchup_ticks)
        sim_catchup_ticks = TRUE;
    }
else {
    if (sim_catchup_ticks) {
        sim_catchup_ticks = FALSE;
        }
    }
return SCPE_OK;
}

t_stat sim_timer_show_catchup (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "Calibrated Ticks%s", sim_catchup_ticks ? " with Catchup Ticks" : "");
return SCPE_OK;
}

/* Set idle calibration threshold */

t_stat sim_timer_set_idle_pct (int32 flag, CONST char *cptr)
{
t_stat r = SCPE_OK;

if (cptr == NULL)
    return SCPE_ARG;
if (1) {
    int32 newpct;
    char gbuf[CBUFSIZE];

    cptr = get_glyph (cptr, gbuf, 0);                 /* get argument */
    if (isdigit (gbuf[0]))
        newpct = (int32) get_uint (gbuf, 10, 100, &r);
    else {
        if (MATCH_CMD (gbuf, "ALWAYS") == 0)
            newpct = 100;
        else
            r = SCPE_ARG;
        }
    if ((r != SCPE_OK) || (newpct == (int32)(sim_idle_calib_pct)))
        return r;
    if (newpct == 0)
        return SCPE_ARG;
    sim_idle_calib_pct = (uint32)newpct;
    }
return SCPE_OK;
}

/* Set stop time */

t_stat sim_timer_set_stop (int32 flag, CONST char *cptr)
{
t_stat r;
t_value stop_time;

if (cptr == NULL)
    return SCPE_ARG;
stop_time = get_uint (cptr, 10, T_VALUE_MAX, &r);
if (r != SCPE_OK)
    return r;
if (stop_time <= (t_value)sim_gtime())
    return SCPE_ARG;
sim_register_internal_device (&sim_stop_dev);           /* Register Stop Device */
sim_timer_stop_time = (double)stop_time;
sim_activate_abs (&sim_stop_unit, (int32)(sim_timer_stop_time - sim_gtime()));
return SCPE_OK;
}

/* Set/Clear asynch */

t_stat sim_timer_set_async (int32 flag, CONST char *cptr)
{
if (flag) {
    if (sim_asynch_enabled && (!sim_asynch_timer)) {
        sim_asynch_timer = TRUE;
        sim_timer_change_asynch ();
        }
    }
else {
    if (sim_asynch_timer) {
        sim_asynch_timer = FALSE;
        sim_timer_change_asynch ();
        }
    }
return SCPE_OK;
}

static CTAB set_timer_tab[] = {
#if defined (SIM_ASYNCH_CLOCKS)
    { "ASYNCH",     &sim_timer_set_async, 1 },
    { "NOASYNCH",   &sim_timer_set_async, 0 },
#endif
    { "CATCHUP",    &sim_timer_set_catchup,  1 },
    { "NOCATCHUP",  &sim_timer_set_catchup,  0 },
    { "CALIB",      &sim_timer_set_idle_pct, 0 },
    { "STOP",       &sim_timer_set_stop, 0 },
    { NULL, NULL, 0 }
    };

MTAB sim_timer_mod[] = {
  { 0 },
};

static t_stat sim_timer_clock_reset (DEVICE *dptr);

static const char *sim_timer_description (DEVICE *dptr)
{
return "Clock Assist facilities";
}

static const char *sim_int_timer_description (DEVICE *dptr)
{
return "Internal Timer";
}

static const char *sim_int_stop_description (DEVICE *dptr)
{
return "Stop facility";
}

static const char *sim_throttle_description (DEVICE *dptr)
{
return "Throttle facility";
}


DEVICE sim_timer_dev = {
    "INT-CLOCK", sim_timer_units, sim_timer_reg, sim_timer_mod, 
    SIM_NTIMERS+1, 0, 0, 0, 0, 0, 
    NULL, NULL, &sim_timer_clock_reset, NULL, NULL, NULL, 
    NULL, DEV_DEBUG | DEV_NOSAVE, 0, 
    sim_timer_debug};

DEVICE sim_int_timer_dev = {
    "INT-TIMER", &sim_internal_timer_unit, NULL, NULL, 
    1, 0, 0, 0, 0, 0, 
    NULL, NULL, NULL, NULL, NULL, NULL, 
    NULL, DEV_NOSAVE};

DEVICE sim_stop_dev = {
    "INT-STOP", &sim_stop_unit, NULL, NULL, 
    1, 0, 0, 0, 0, 0, 
    NULL, NULL, NULL, NULL, NULL, NULL, 
    NULL, DEV_NOSAVE, 0, 
    NULL, NULL, NULL, NULL, NULL, NULL,
    sim_int_stop_description};

DEVICE sim_throttle_dev = {
    "INT-THROTTLE", &sim_throttle_unit, sim_throttle_reg, NULL,
    1, 0, 0, 0, 0, 0, 
    NULL, NULL, NULL, NULL, NULL, NULL, 
    NULL, DEV_NOSAVE};

/* SET CLOCK command */

t_stat sim_set_timers (int32 arg, CONST char *cptr)
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
    if ((ctptr = find_ctab (set_timer_tab, gbuf))) {    /* match? */
        r = ctptr->action (ctptr->arg, cvptr);          /* do the rest */
        if (r != SCPE_OK)
            return r;
        }
    else return SCPE_NOPARAM;
    }
return SCPE_OK;
}

/* sim_idle - idle simulator until next event or for specified interval

   Inputs:
        tmr =   calibrated timer to use

   Must solve the linear equation

        ms_to_wait = w * ms_per_wait

   Or
        w = ms_to_wait / ms_per_wait
*/

t_bool sim_idle (uint32 tmr, int sin_cyc)
{
uint32 w_ms, w_idle, act_ms;
int32 act_cyc;
static t_bool in_nowait = FALSE;
double cyc_since_idle;
RTC *rtc = &rtcs[tmr];

if (rtc->hz == 0)                                       /* specified timer is not running? */
    tmr = sim_calb_tmr;                                 /* use calibrated timer instead */
rtc = &rtcs[tmr];
if (rtc->clock_catchup_pending) {                       /* Catchup clock tick pending due to ack? */
    sim_debug (DBG_TIK, &sim_timer_dev, "sim_idle(tmr=%d, sin_cyc=%d) - accelerating pending catch-up tick before idling %s\n", tmr, sin_cyc, sim_uname (rtc->clock_unit));
    sim_activate_abs (&sim_timer_units[tmr], 0);
    sim_interval -= sin_cyc;
    return FALSE;
    }
if (_rtcn_tick_catchup_check (rtc, -1)) {               /* Check for slow clock tick? */
    sim_interval -= sin_cyc;
    return FALSE;
    }
if ((!sim_idle_enab)                             ||     /* idling disabled */
    ((sim_clock_queue == QUEUE_LIST_END) &&             /* or clock queue empty? */
     (!sim_asynch_timer))||                             /*     and not asynch? */
    ((sim_clock_queue != QUEUE_LIST_END) &&             /* or clock queue not empty */
     ((sim_clock_queue->flags & UNIT_IDLE) == 0))||     /*   and event not idle-able? */
    (rtc->elapsed < sim_idle_stable)) {             /* or calibrated timer not stable? */
    sim_debug (DBG_IDL, &sim_timer_dev, "Can't idle: %s - elapsed: %d and %d/%d\n", !sim_idle_enab ? "idle disabled" : 
                                                                             ((rtc->elapsed < sim_idle_stable) ? "not stable" : 
                                                                                                                     ((sim_clock_queue != QUEUE_LIST_END) ? sim_uname (sim_clock_queue) : 
                                                                                                                                                            "")), rtc->elapsed, rtc->ticks, rtc->hz);
    sim_interval -= sin_cyc;
    return FALSE;
    }
/*
   When a simulator is in an instruction path (or under other conditions 
   which would indicate idling), the countdown of sim_interval may not 
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
   second and if it exceeds the percentage defined by sim_idle_calib_pct
   calibration is suppressed. Thus recalibration only happens if things 
   didn't idle too much.

   we also check check sim_idle_enab above so that all simulators can avoid
   directly checking sim_idle_enab before calling sim_idle so that all of 
   the bookkeeping on sim_idle_idled is done here in sim_timer where it 
   means something, while not idling when it isn't enabled.  
   */
sim_debug (DBG_TRC, &sim_timer_dev, "sim_idle(tmr=%d, sin_cyc=%d)\n", tmr, sin_cyc);
if (sim_idle_cyc_ms == 0) {
    sim_idle_cyc_ms = (rtc->currd * rtc->hz) / 1000;/* cycles per msec */
    if (sim_idle_rate_ms != 0)
        sim_idle_cyc_sleep = (rtc->currd * rtc->hz) / (1000 / sim_idle_rate_ms);/* cycles per minimum sleep */
    }
if ((sim_idle_rate_ms == 0) || (sim_idle_cyc_ms == 0)) {/* not possible? */
    sim_interval -= sin_cyc;
    sim_debug (DBG_IDL, &sim_timer_dev, "not possible idle_rate_ms=%d - cyc/ms=%d\n", sim_idle_rate_ms, sim_idle_cyc_ms);
    return FALSE;
    }
w_ms = (uint32) sim_interval / sim_idle_cyc_ms;         /* ms to wait */
/* When the host system has a clock tick which is less frequent than the    */
/* simulated system's clock, idling will cause delays which will miss       */
/* simulated clock ticks.  To accomodate this, and still allow idling, if   */
/* the simulator acknowledges the processing of clock ticks, then catchup   */
/* ticks can be used to make up for missed ticks. */
if (rtc->clock_catchup_eligible)
    w_idle = (sim_interval * 1000) / rtc->currd;        /* 1000 * pending fraction of tick */
else
    w_idle = (w_ms * 1000) / sim_idle_rate_ms;          /* 1000 * intervals to wait */
if ((w_idle < 500) || (w_ms == 0)) {                    /* shorter than 1/2 the interval or */
    sim_interval -= sin_cyc;                            /* minimal sleep time? */
    if (!in_nowait)
        sim_debug (DBG_IDL, &sim_timer_dev, "no wait, too short: %d usecs\n", w_idle);
    in_nowait = TRUE;
    return FALSE;
    }
if (w_ms > 1000)                                        /* too long a wait (runaway calibration) */
    sim_debug (DBG_TIK, &sim_timer_dev, "waiting too long: w_ms=%d usecs, w_idle=%d usecs, sim_interval=%d, rtc->currd=%d\n", w_ms, w_idle, sim_interval, rtc->currd);
in_nowait = FALSE;
if (sim_clock_queue == QUEUE_LIST_END)
    sim_debug (DBG_IDL, &sim_timer_dev, "sleeping for %d ms - pending event in %d %s\n", w_ms, sim_interval, sim_vm_interval_units);
else
    sim_debug (DBG_IDL, &sim_timer_dev, "sleeping for %d ms - pending event on %s in %d %s\n", w_ms, sim_uname(sim_clock_queue), sim_interval, sim_vm_interval_units);
cyc_since_idle = sim_gtime() - sim_idle_end_time;       /* time since prior idle */
act_ms = sim_idle_ms_sleep (w_ms);                      /* wait */
rtc->clock_time_idled += act_ms;
act_cyc = act_ms * sim_idle_cyc_ms;
if (cyc_since_idle > sim_idle_cyc_sleep)
    act_cyc -= sim_idle_cyc_sleep / 2;                  /* account for half an interval's worth of cycles */
else
    act_cyc -= (int32)cyc_since_idle;                   /* acount for cycles executed */
sim_interval = sim_interval - act_cyc;                  /* count down sim_interval to reflect idle period */
sim_idle_end_time = sim_gtime();                        /* save idle completed time */
if (sim_clock_queue == QUEUE_LIST_END)
    sim_debug (DBG_IDL, &sim_timer_dev, "slept for %d ms - pending event in %d %s\n", act_ms, sim_interval, sim_vm_interval_units);
else
    sim_debug (DBG_IDL, &sim_timer_dev, "slept for %d ms - pending event on %s in %d %s\n", act_ms, sim_uname(sim_clock_queue), sim_interval, sim_vm_interval_units);
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
        sim_throt_state = SIM_THROT_STATE_THROTTLE;         /* force state */
        sim_throt_wait = sim_throt_val;
        }
    }
if (sim_throt_type == SIM_THROT_SPC)    /* Set initial value while correct one is determined */
    sim_throt_cps = (int32)((1000.0 * sim_throt_val) / (double)sim_throt_sleep_time);
else
    sim_throt_cps = sim_precalibrate_ips;
return SCPE_OK;
}

t_stat sim_show_throt (FILE *st, DEVICE *dnotused, UNIT *unotused, int32 flag, CONST char *cptr)
{
if (sim_idle_rate_ms == 0)
    fprintf (st, "Throttling:                    Not Available\n");
else {
    switch (sim_throt_type) {

    case SIM_THROT_MCYC:
        fprintf (st, "Throttle:                      %d mega%s\n", sim_throt_val, sim_vm_interval_units);
        if (sim_throt_wait)
            fprintf (st, "Throttling by sleeping for:    %d ms every %d %s\n", sim_throt_sleep_time, sim_throt_wait, sim_vm_interval_units);
        break;

    case SIM_THROT_KCYC:
        fprintf (st, "Throttle:                      %d kilo%s\n", sim_throt_val, sim_vm_interval_units);
        if (sim_throt_wait)
            fprintf (st, "Throttling by sleeping for:    %d ms every %d %s\n", sim_throt_sleep_time, sim_throt_wait, sim_vm_interval_units);
        break;

    case SIM_THROT_PCT:
        if (sim_throt_wait) {
            fprintf (st, "Throttle:                      %d%% of %s %s per second\n", sim_throt_val, sim_fmt_numeric (sim_throt_peak_cps), sim_vm_interval_units);
            fprintf (st, "Throttling by sleeping for:    %d ms every %d %s\n", sim_throt_sleep_time, sim_throt_wait, sim_vm_interval_units);
            }
        else
            fprintf (st, "Throttle:                      %d%%\n", sim_throt_val);
        break;

    case SIM_THROT_SPC:
        fprintf (st, "Throttle:                      %d/%d\n", sim_throt_val, sim_throt_sleep_time);
        fprintf (st, "Throttling by sleeping for:    %d ms every %d %s\n", sim_throt_sleep_time, sim_throt_val, sim_vm_interval_units);
        break;

    default:
        fprintf (st, "Throttling:                    Disabled\n");
        break;
        }
    if (sim_throt_type != SIM_THROT_NONE) {
        if (sim_throt_state != SIM_THROT_STATE_THROTTLE)
            fprintf (st, "Throttle State:                %s - wait: %d\n", (sim_throt_state == SIM_THROT_STATE_INIT) ? "Waiting for Init" : "Timing", sim_throt_wait);
        }
    }
return SCPE_OK;
}

void sim_throt_sched (void)
{
if (sim_throt_type != SIM_THROT_NONE) {
    if (sim_throt_state == SIM_THROT_STATE_THROTTLE) {  /* Previously calibrated? */
        /* Reset recalibration reference times */
        sim_throt_ms_start = sim_os_msec ();
        sim_throt_inst_start = sim_gtime ();
        /* Start with prior calibrated delay */
        sim_activate (&sim_throttle_unit, sim_throt_wait);
        }
    else {
        /* Start calibration initially */
        sim_throt_state = SIM_THROT_STATE_INIT;
        sim_activate (&sim_throttle_unit, SIM_THROT_WINIT);
        }
    }
}

void sim_throt_cancel (void)
{
sim_cancel (&sim_throttle_unit);
}

/* Throttle service

   Throttle service has three distinct states used while dynamically
   determining a throttling interval:

       SIM_THROT_STATE_INIT     take initial measurement
       SIM_THROT_STATE_TIME     take final measurement, calculate wait values
       SIM_THROT_STATE_THROTTLE periodic waits to slow down the CPU
*/
t_stat sim_throt_svc (UNIT *uptr)
{
int32 tmr;
uint32 delta_ms;
double a_cps, d_cps, delta_inst;
RTC *rtc = NULL;

if (sim_calb_tmr != -1)
    rtc = &rtcs[sim_calb_tmr];
switch (sim_throt_state) {

    case SIM_THROT_STATE_INIT:                          /* take initial reading */
        if ((sim_calb_tmr != -1) && (rtc->hz != 0)) {
            if (rtc->calibrations < sim_throt_delay) {
                sim_throt_ms_start = sim_os_msec ();
                sim_throt_inst_start = sim_gtime ();
                sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc(INIT) Deferring until stable (%d more seconds)\n", (int)(sim_throt_delay - rtc->calibrations));
                return sim_activate (uptr, rtc->hz * rtc->currd);
                }
            sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc(INIT) Computing Throttling values based on the last second's execution rate\n");
            sim_throt_state = SIM_THROT_STATE_TIME;
            if (sim_throt_peak_cps < (double)(rtc->hz * rtc->currd)) 
                sim_throt_peak_cps = (double)rtc->hz * rtc->currd;
            return sim_throt_svc (uptr);
            }
        else
            sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc(INIT) Calibrated timer not available. Falling back to legacy method\n");
        sim_idle_ms_sleep (sim_idle_rate_ms);           /* start on a tick boundary to calibrate */
        sim_throt_ms_start = sim_os_msec ();
        sim_throt_inst_start = sim_gtime ();
        if (sim_throt_type != SIM_THROT_SPC) {          /* dynamic? */
            switch (sim_throt_type) {
                case SIM_THROT_PCT:
                    sim_throt_wait = (int32)((sim_throt_peak_cps * sim_throt_val) / 100.0);
                    break;
                case SIM_THROT_KCYC:
                    sim_throt_wait = sim_throt_val * 1000;
                    break;
                case SIM_THROT_MCYC:
                    sim_throt_wait = sim_throt_val * 1000000;
                    break;
                }
            sim_throt_state = SIM_THROT_STATE_TIME;     /* next state */
            }
        else {                                          /* Non dynamic? */
            sim_throt_wait = sim_throt_val;
            sim_throt_state = SIM_THROT_STATE_THROTTLE; /* force state */
            sim_throt_cps = (int32)((1000.0 * sim_throt_val) / (double)sim_throt_sleep_time);
            }
        sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc(INIT) Starting.  Values wait = %d\n", sim_throt_wait);
        break;                                          /* reschedule */

    case SIM_THROT_STATE_TIME:                          /* take final reading */
        sim_throt_ms_stop = sim_os_msec ();
        delta_ms = sim_throt_ms_stop - sim_throt_ms_start;
        delta_inst = sim_gtime () - sim_throt_inst_start;
        if (delta_ms < SIM_THROT_MSMIN) {               /* not enough time? */
            if (delta_inst >= 100000000.0) {            /* too many inst? */
                sim_throt_state = SIM_THROT_STATE_INIT; /* fails in 32b! */
                sim_printf ("Can't throttle.  Host CPU is too fast with a minimum sleep time of %d ms\n", sim_idle_rate_ms);
                sim_set_throt (0, NULL);                /* disable throttling */
                return SCPE_OK;
                }
            sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc() Not enough time.  %d ms executing %.f %s.\n", 
                                (int)delta_ms, delta_inst, sim_vm_interval_units);
            sim_throt_wait = (int32)(delta_inst * SIM_THROT_WMUL);
            sim_throt_inst_start = sim_gtime();
            sim_idle_ms_sleep (sim_idle_rate_ms);       /* start on a tick boundart to calibrate */
            sim_throt_ms_start = sim_os_msec ();
            }
        else {                                          /* long enough */
            a_cps = (((double) delta_inst) * 1000.0) / (double) delta_ms;
            if (sim_throt_type == SIM_THROT_MCYC)       /* calc desired cps */
                d_cps = (double) sim_throt_val * 1000000.0;
            else
                if (sim_throt_type == SIM_THROT_KCYC)
                    d_cps = (double) sim_throt_val * 1000.0;
                else
                    d_cps = (sim_throt_peak_cps * sim_throt_val) / 100.0;
            if (d_cps > a_cps) {
                sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc() CPU too slow.  Values a_cps = %f, d_cps = %f\n", 
                                                    a_cps, d_cps);
                sim_throt_state = SIM_THROT_STATE_INIT;
                sim_printf ("*********** WARNING ***********\n");
                sim_printf ("Host CPU is too slow to simulate %s %s per second\n", sim_fmt_numeric(d_cps), sim_vm_interval_units);
                sim_printf ("Host CPU can only simulate %s %s per second\n", sim_fmt_numeric(sim_throt_peak_cps), sim_vm_interval_units);
                sim_printf ("Throttling disabled.\n");
                sim_set_throt (0, NULL);
                return SCPE_OK;
                }
            while (1) {
                sim_throt_wait = (int32)                /* cycles between sleeps */
                    ((a_cps * d_cps * ((double) sim_throt_sleep_time)) /
                     (1000.0 * (a_cps - d_cps)));
                if (sim_throt_wait >= SIM_THROT_WMIN)   /* long enough? */
                    break;
                sim_throt_sleep_time += sim_os_sleep_inc_ms;
                sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc() Wait too small, increasing sleep time to %d ms.  Values a_cps = %f, d_cps = %f, wait = %d\n", 
                                                    sim_throt_sleep_time, a_cps, d_cps, sim_throt_wait);
                }
            sim_throt_ms_start = sim_throt_ms_stop;
            sim_throt_inst_start = sim_gtime();
            sim_throt_state = SIM_THROT_STATE_THROTTLE;
            sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc() Throttle values a_cps = %f, d_cps = %f, wait = %d, sleep = %d ms\n", 
                                                a_cps, d_cps, sim_throt_wait, sim_throt_sleep_time);
            sim_throt_cps = d_cps;                  /* save the desired rate */
            /* Run through all timers and adjust the calibration for each */
            /* one that is running to reflect the throttle rate */
            for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
                rtc = &rtcs[tmr];
                if (rtc->hz) {                                      /* running? */
                    rtc->currd = (int32)(sim_throt_cps / rtc->hz);/* use throttle calibration */
                    rtc->ticks = rtc->hz - 1;                     /* force clock calibration on next tick */
                    rtc->rtime = sim_throt_ms_start - 1000 + 1000/rtc->hz;/* adjust calibration parameters to reflect throttled rate */
                    rtc->gtime = sim_throt_inst_start - sim_throt_cps + sim_throt_cps/rtc->hz;
                    rtc->nxintv = 1000;
                    rtc->based = rtc->currd;
                    if (rtc->clock_unit)
                        sim_activate_abs (rtc->clock_unit, rtc->currd);/* reschedule next tick */
                    }
                }
            }
        break;

    case SIM_THROT_STATE_THROTTLE:                      /* throttling */
        sim_idle_ms_sleep (sim_throt_sleep_time);
        delta_ms = sim_os_msec () - sim_throt_ms_start;
        if (delta_ms >= 10000) {                        /* recompute every 10 sec */
            double delta_insts = sim_gtime() - sim_throt_inst_start;

            a_cps = (delta_insts * 1000.0) / (double) delta_ms;
            if (sim_throt_type != SIM_THROT_SPC) {      /* when not dynamic throttling */
                if (sim_throt_type == SIM_THROT_MCYC)   /* calc desired cps */
                    d_cps = (double) sim_throt_val * 1000000.0;
                else
                    if (sim_throt_type == SIM_THROT_KCYC)
                        d_cps = (double) sim_throt_val * 1000.0;
                    else
                        d_cps = (sim_throt_peak_cps * sim_throt_val) / 100.0;
                if (fabs(100.0 * (d_cps - a_cps) / d_cps) > (double)sim_throt_drift_pct) {
                    sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc() Recalibrating throttle based on values a_cps = %f, d_cps = %f deviating by %.2f%% from the desired value\n", 
                                                        a_cps, d_cps, fabs(100.0 * (d_cps - a_cps) / d_cps));
                    if ((a_cps > d_cps) &&                      /* too fast? */
                        ((100.0 * (a_cps - d_cps) / d_cps) > (100 - sim_throt_drift_pct))) {
                        sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc() Restarting calibrating throttle going too fast: a_cps = %f, d_cps = %f deviating by %.2f%% from the desired value\n", 
                                                            a_cps, d_cps, fabs(100.0 * (d_cps - a_cps) / d_cps));
                        while (1) {
                            sim_throt_wait = (int32)            /* cycles between sleeps */
                                ((sim_throt_peak_cps * d_cps * ((double) sim_throt_sleep_time)) /
                                 (1000.0 * (sim_throt_peak_cps - d_cps)));
                            if (sim_throt_wait >= SIM_THROT_WMIN)/* long enough? */
                                break;
                            sim_throt_sleep_time += sim_os_sleep_inc_ms;
                            sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc() Wait too small, increasing sleep time to %d ms.  Values a_cps = %f, d_cps = %f, wait = %d\n", 
                                                                sim_throt_sleep_time, sim_throt_peak_cps, d_cps, sim_throt_wait);
                            }
                        }
                    else {                                      /* slow or within reasonable range */
                        sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc() Adjusting wait before sleep interval by %d\n", 
                                                            (int32)(((d_cps - a_cps) * (double)sim_throt_wait) / d_cps));
                        sim_throt_wait += (int32)(((d_cps - a_cps) * (double)sim_throt_wait) / d_cps);
                        }
                    sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc() Throttle values a_cps = %f, d_cps = %f, wait = %d, sleep = %d ms\n", 
                                                        a_cps, d_cps, sim_throt_wait, sim_throt_sleep_time);
                    sim_throt_cps = d_cps;                      /* save the desired rate */
                    sim_throt_ms_start = sim_os_msec ();
                    sim_throt_inst_start = sim_gtime();
                    }
                }
            else {                                      /* record instruction rate */
                sim_throt_cps = (int32)a_cps;
                sim_debug (DBG_THR, &sim_timer_dev, "sim_throt_svc() Recalibrating Special %d/%u Cycles Per Second of %f\n", 
                                                    sim_throt_wait, sim_throt_sleep_time, sim_throt_cps);
                sim_throt_inst_start = sim_gtime();
                sim_throt_ms_start = sim_os_msec ();
                }
            }
        break;
        }

sim_activate (uptr, sim_throt_wait);                    /* reschedule */
return SCPE_OK;
}

/* Clock assist activites */
t_stat sim_timer_tick_svc (UNIT *uptr)
{
int32 tmr = (int32)(uptr-sim_timer_units);
t_stat stat;
RTC *rtc = &rtcs[tmr];

rtc->clock_ticks += 1;
rtc->calib_tick_time += rtc->clock_tick_size;
/*
 * Some devices may depend on executing during the same instruction or 
 * immediately after the clock tick event.  To satisfy this, we directly 
 * run the clock event here and if it completes successfully, schedule any
 * currently coschedule units to run now.  Ticks should never return a 
 * non-success status, while co-schedule activities might, so they are 
 * queued to run from sim_process_event
 */
sim_debug (DBG_QUE, &sim_timer_dev, "sim_timer_tick_svc(tmr=%d) - scheduling %s - cosched interval: %d\n", tmr, sim_uname (rtc->clock_unit), rtc->cosched_interval);
if (rtc->clock_unit->action == NULL)
    return SCPE_IERR;
stat = rtc->clock_unit->action (rtc->clock_unit);
--rtc->cosched_interval;                    /* Countdown ticks */
if (rtc->clock_cosched_queue != QUEUE_LIST_END)
    rtc->clock_cosched_queue->time = rtc->cosched_interval;
if ((stat == SCPE_OK)                               && 
    (rtc->cosched_interval <= 0)                &&
    (rtc->clock_cosched_queue != QUEUE_LIST_END)) {
    UNIT *sptr = rtc->clock_cosched_queue;
    UNIT *cptr = QUEUE_LIST_END;

    if (rtc->clock_catchup_eligible) {      /* calibration started? */
        struct timespec now;
        double skew;

        clock_gettime(CLOCK_REALTIME, &now);
        skew = (_timespec_to_double(&now) - (rtc->calib_tick_time+rtc->clock_catchup_base_time));

        if (fabs(skew) > fabs(rtc->clock_skew_max))
            rtc->clock_skew_max = skew;
        }
    /* Gather any queued events which are scheduled for right now */
    do {
        cptr = rtc->clock_cosched_queue;
        rtc->clock_cosched_queue = cptr->next;
        if (rtc->clock_cosched_queue != QUEUE_LIST_END) {
            rtc->clock_cosched_queue->time += rtc->cosched_interval;
            rtc->cosched_interval = rtc->clock_cosched_queue->time;
            }
        else
            rtc->cosched_interval  = 0;
        } while ((rtc->cosched_interval <= 0) &&
                 (rtc->clock_cosched_queue != QUEUE_LIST_END));
    if (cptr != QUEUE_LIST_END)
        cptr->next = QUEUE_LIST_END;
    /* Now dispatch that list (in order). */
    while (sptr != QUEUE_LIST_END) {
        cptr = sptr;
        sptr = sptr->next;
        cptr->next = NULL;
        cptr->cancel = NULL;
        cptr->time = 0;
        if (cptr->usecs_remaining) {
            sim_debug (DBG_QUE, &sim_timer_dev, "Rescheduling %s after %.0f usecs %s%s\n", sim_uname (cptr), cptr->usecs_remaining, (sptr != QUEUE_LIST_END) ? "- next: " : "", (sptr != QUEUE_LIST_END) ? sim_uname (sptr) : "");
            stat = sim_timer_activate_after (cptr, cptr->usecs_remaining);
            }
        else {
            sim_debug (DBG_QUE, &sim_timer_dev, "Activating %s now %s%s\n", sim_uname (cptr), (sptr != QUEUE_LIST_END) ? "- next: " : "", (sptr != QUEUE_LIST_END) ? sim_uname (sptr) : "");
            stat = _sim_activate (cptr, 0);
            }
        if (stat != SCPE_OK) {
            sim_debug (DBG_QUE, &sim_timer_dev, "Activating %s failed: %s\n", sim_uname (cptr), sim_error_text (stat));
            break;
            }
        }
    }
return stat;
}

t_stat sim_timer_stop_svc (UNIT *uptr)
{
return SCPE_STOP;
}

void sim_rtcn_get_time (struct timespec *now, int tmr)
{
sim_debug (DBG_GET, &sim_timer_dev, "sim_rtcn_get_time(tmr=%d)\n", tmr);
clock_gettime (CLOCK_REALTIME, now);
}

time_t sim_get_time (time_t *now)
{
struct timespec ts_now;

sim_debug (DBG_GET, &sim_timer_dev, "sim_get_time()\n");
sim_rtcn_get_time (&ts_now, 0);
if (now)
    *now = ts_now.tv_sec;
return ts_now.tv_sec;
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
 * When necessary, catch-up ticks are scheduled to run under one 
 * of two conditions:
 *   1) after indicated number of instructions in a call by the simulator
 *      to sim_rtcn_tick_ack.  sim_rtcn_tick_ack exists to provide a 
 *      mechanism to inform the simh timer facilities when the simulated 
 *      system has accepted the most recent clock tick interrupt.
 *   2) immediately when the simulator calls sim_idle
 *
 * catchup ticks are only scheduled (eligible to happen) under these 
 * conditions after at least one tick has been acknowledged.
 *
 * The clock tick UNIT that will be scheduled to run for catchup ticks
 * must be specified with sim_rtcn_init_unit().
 */

/* _rtcn_tick_catchup_check - idle simulator until next event or for specified interval

   Inputs:
        RTC =   calibrated timer to check/schedule
        time =  instruction delay for next tick

   Returns TRUE if a catchup tick has been scheduled
*/

static t_bool _rtcn_tick_catchup_check (RTC *rtc, int32 time)
{
int32 tmr;
t_bool bReturn = FALSE;

if (!sim_catchup_ticks)
    return FALSE;
if (time == -1) {
    for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
        rtc = &rtcs[tmr];
        if ((rtc->hz > 0) && rtc->clock_catchup_eligible)
            {
            double tnow = sim_timenow_double();

            if (tnow > (rtc->clock_catchup_base_time + (rtc->calib_tick_time + rtc->clock_tick_size))) {
                if (!rtc->clock_catchup_pending) {
                    sim_debug (DBG_TIK, &sim_timer_dev, "_rtcn_tick_catchup_check(%d) - scheduling catchup tick %d for %s which is behind %s\n", time, 1 + rtc->ticks, sim_uname (rtc->clock_unit), sim_fmt_secs (tnow - (rtc->clock_catchup_base_time + (rtc->calib_tick_time + rtc->clock_tick_size))));
                    rtc->clock_catchup_pending = TRUE;
                    sim_activate_abs (rtc->timer_unit, 0);
                    bReturn = TRUE;
                    }
                else
                    sim_debug (DBG_TIK, &sim_timer_dev, "_rtcn_tick_catchup_check(%d) - already pending catchup tick %d for %s which is behind %s\n", time, 1 + rtc->ticks, sim_uname (rtc->clock_unit), sim_fmt_secs (tnow - (rtc->clock_catchup_base_time + (rtc->calib_tick_time + rtc->clock_tick_size))));
                }
            }
        }
    }
if ((!rtc->clock_catchup_eligible) &&           /* not eligible yet? */
    (time != -1)) {                             /* called from ack? */
    rtc->clock_catchup_base_time = sim_timenow_double();
    rtc->clock_ticks_tot += rtc->clock_ticks;
    rtc->clock_ticks = 0;
    rtc->calib_tick_time_tot += rtc->calib_tick_time;
    rtc->calib_tick_time = 0.0;
    rtc->clock_catchup_ticks_tot += rtc->clock_catchup_ticks;
    rtc->clock_catchup_ticks = 0;
    rtc->calib_ticks_acked_tot += rtc->calib_ticks_acked;
    rtc->calib_ticks_acked = 0;
    rtc->clock_catchup_eligible = TRUE;
    sim_debug (DBG_QUE, &sim_timer_dev, "_rtcn_tick_catchup_check() - Enabling catchup ticks for %s\n", sim_uname (rtc->clock_unit));
    bReturn = TRUE;
    }
if ((rtc->hz > 0) && 
    rtc->clock_catchup_eligible)
    {
    double tnow = sim_timenow_double();

    if (tnow > (rtc->clock_catchup_base_time + (rtc->calib_tick_time + rtc->clock_tick_size))) {
        if (!rtc->clock_catchup_pending) {
            sim_debug (DBG_TIK, &sim_timer_dev, "_rtcn_tick_catchup_check(%d) - scheduling catchup tick %d for %s which is behind %s\n", time, 1 + rtc->ticks, sim_uname (rtc->clock_unit), sim_fmt_secs (tnow - (rtc->clock_catchup_base_time + (rtc->calib_tick_time + rtc->clock_tick_size))));
            rtc->clock_catchup_pending = TRUE;
            sim_activate_abs (rtc->timer_unit, (time < 0) ? 0 : time);
            }
        else
            sim_debug (DBG_TIK, &sim_timer_dev, "_rtcn_tick_catchup_check(%d) - already pending catchup tick %d for %s which is behind %s\n", time, 1 + rtc->ticks, sim_uname (rtc->clock_unit), sim_fmt_secs (tnow - (rtc->clock_catchup_base_time + (rtc->calib_tick_time + rtc->clock_tick_size))));
        return TRUE;
        }
    }
return bReturn;
}

t_stat sim_rtcn_tick_ack (uint32 time, int32 tmr)
{
RTC *rtc;

if ((tmr < 0) || (tmr > SIM_NTIMERS))
    return SCPE_TIMER;
rtc = &rtcs[tmr];
sim_debug (DBG_ACK, &sim_timer_dev, "sim_rtcn_tick_ack - for %s\n", sim_uname (rtc->clock_unit));
_rtcn_tick_catchup_check (rtc, (int32)time);
++rtc->calib_ticks_acked;
return SCPE_OK;
}


static double _timespec_to_double (struct timespec *time)
{
return ((double)time->tv_sec)+(double)(time->tv_nsec)/1000000000.0;
}

static void _double_to_timespec (struct timespec *time, double dtime)
{
double int_part = floor(dtime);

time->tv_sec = (time_t)int_part;
time->tv_nsec = (long)((dtime - int_part)*1000000000.0);
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
sim_timer_thread_running = TRUE;
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
                   sim_uname(sim_wallclock_entry), sim_fmt_secs (sim_wallclock_entry->a_usec_delay/1000000.0));

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
sim_timer_thread_running = FALSE;
pthread_mutex_unlock (&sim_timer_lock);

sim_debug (DBG_TIM, &sim_timer_dev, "_timer_thread() - exiting\n");

return NULL;
}

#endif /* defined(SIM_ASYNCH_CLOCKS) */

/*
   In the event that there are no active calibrated clock devices, 
   no instruction rate calibration will be performed.  This is more 
   likely on simpler simulators which don't have a full spectrum of 
   standard devices or possibly when a clock device exists but its 
   use is optional.

   Additonally, when a host system has a natural clock tick (
   or minimal sleep time) which is greater than the tick size that 
   a simulator wants to run a clock at, we run this clock at the 
   rate implied by the host system's minimal sleep time or 50Hz.
   
   To solve this we merely run an internal clock at 100Hz.
 */

static t_stat sim_timer_clock_tick_svc (UNIT *uptr)
{
sim_debug(DBG_INT, &sim_timer_dev, "sim_timer_clock_tick_svc()\n");
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
  this requirement, OR when asynch clocks are enabled, the internal clock
  is always run.

  Some simulators have clocks that have dynamically programmable tick 
  rates. Such a clock is only a reliable candidate to be the calibrated 
  clock if it uses a single tick rate rather than changing the tick rate 
  on the fly.  Generally most systems like this, under normal conditions
  don't change their tick rates unless they're running something that is
  examining the behavior of the clock system (like a diagnostic).  Under 
  these conditions this clock is removed from the potential selection as
  "the" calibrated clock all others are relative to and if necessary, an
  internal calibrated clock is selected.
 */
static void _rtcn_configure_calibrated_clock (int32 newtmr)
{
int32 tmr;
RTC *rtc, *crtc;

/* Look for a timer running slower or the same as the host system clock */
sim_int_clk_tps = MIN(CLK_TPS, sim_os_tick_hz);
for (tmr=0; tmr<SIM_NTIMERS; tmr++) {
    rtc = &rtcs[tmr];
    if ((rtc->hz) &&                        /* is calibrated AND */
        (rtc->hz <= (uint32)sim_os_tick_hz) && /* slower than OS tick rate AND */
        (rtc->clock_unit) &&                /* clock has been registered AND */
        ((rtc->last_hz == 0) ||             /* first calibration call OR */
         (rtc->last_hz == rtc->hz)))        /* subsequent calibration call with an unchanged tick rate */
        break;
    }
if (tmr == SIM_NTIMERS) {                   /* None found? */
    if ((tmr != newtmr) && (!sim_is_active (&SIM_INTERNAL_UNIT))) {
        if ((sim_calb_tmr != SIM_NTIMERS) &&/* not internal timer? */
            (sim_calb_tmr != -1)) {         /* previously active? */
            crtc = &rtcs[sim_calb_tmr];
            if (!crtc->hz) {                /* now stopped? */
                sim_debug (DBG_CAL, &sim_timer_dev, "_rtcn_configure_calibrated_clock(newtmr=%d) - Cleaning up stopped timer %s support\n", newtmr, sim_uname(crtc->clock_unit));
                /* Migrate any coscheduled devices to the standard queue */
                /* with appropriate usecs_remaining reflecting their currently */
                /* scheduled firing time.  sim_process_event() will coschedule */
                /* appropriately. */
                /* temporarily restore prior hz to get correct remaining time */
                crtc->hz = crtc->last_hz;
                while (crtc->clock_cosched_queue != QUEUE_LIST_END) {
                    UNIT *uptr = crtc->clock_cosched_queue;
                    double usecs_remaining = sim_timer_activate_time_usecs (uptr) - 1;

                    _sim_coschedule_cancel (uptr);
                    _sim_activate (uptr, 1);
                    uptr->usecs_remaining = usecs_remaining;
                    }
                crtc->hz = 0;                           /* back to 0 */
                if (crtc->clock_unit)
                    sim_cancel (crtc->clock_unit);
                sim_cancel (crtc->timer_unit);
                }
            }
        /* Start the internal timer */
        sim_calb_tmr = SIM_NTIMERS;
        sim_debug (DBG_CAL|DBG_INT, &sim_timer_dev, "_rtcn_configure_calibrated_clock(newtmr=%d) - Starting Internal Calibrated Timer at %dHz\n", newtmr, sim_int_clk_tps);
        SIM_INTERNAL_UNIT.action = &sim_timer_clock_tick_svc;
        SIM_INTERNAL_UNIT.flags = UNIT_IDLE;
        sim_register_internal_device (&sim_int_timer_dev);      /* Register Internal timer device */
        sim_rtcn_init_unit_ticks (&SIM_INTERNAL_UNIT, (int32)((CLK_INIT*CLK_TPS)/sim_int_clk_tps), SIM_INTERNAL_CLK, sim_int_clk_tps);
        SIM_INTERNAL_UNIT.action (&SIM_INTERNAL_UNIT);          /* Force tick to activate timer */
        }
    return;
    }
if ((tmr == newtmr) && 
    (sim_calb_tmr == newtmr))               /* already set? */
    return;
if (sim_calb_tmr == SIM_NTIMERS) {          /* was old the internal timer? */
    sim_debug (DBG_CAL|DBG_INT, &sim_timer_dev, "_rtcn_configure_calibrated_clock(newtmr=%d) - Stopping Internal Calibrated Timer, New Timer = %d (%dHz)\n", newtmr, tmr, rtc->hz);
    rtcs[SIM_NTIMERS].initd = 0;
    rtcs[SIM_NTIMERS].hz = 0;
    sim_register_clock_unit_tmr (NULL, SIM_INTERNAL_CLK);
    sim_cancel (&SIM_INTERNAL_UNIT);
    sim_cancel (&sim_timer_units[SIM_NTIMERS]);
    }
else {
    if (sim_calb_tmr != -1) {
        crtc = &rtcs[sim_calb_tmr];
        if (crtc->hz == 0) {
            /* Migrate any coscheduled devices to the standard queue */
            /* with appropriate usecs_remaining reflecting their currently */
            /* scheduled firing time.  sim_process_event() will coschedule */
            /* appropriately. */
            /* temporarily restore prior hz to get correct remaining time */
            crtc->hz = crtc->last_hz;
            while (crtc->clock_cosched_queue != QUEUE_LIST_END) {
                UNIT *uptr = crtc->clock_cosched_queue;
                double usecs_remaining = sim_timer_activate_time_usecs (uptr) - 1;

                _sim_coschedule_cancel (uptr);
                _sim_activate (uptr, 1);
                uptr->usecs_remaining = usecs_remaining;
                }
            crtc->hz = 0;                          /* back to 0 */
            }
        sim_debug (DBG_CAL|DBG_INT, &sim_timer_dev, "_rtcn_configure_calibrated_clock(newtmr=%d) - Changing Calibrated Timer from %d (%dHz) to %d (%dHz)\n", newtmr, sim_calb_tmr, crtc->last_hz, tmr, rtc->hz);
        }
    sim_calb_tmr = tmr;
    }
sim_calb_tmr = tmr;
}

static t_stat sim_timer_clock_reset (DEVICE *dptr)
{
sim_debug (DBG_TRC, &sim_timer_dev, "sim_timer_clock_reset()\n");
_rtcn_configure_calibrated_clock (sim_calb_tmr);
sim_timer_dev.description = &sim_timer_description;
sim_throttle_dev.description = &sim_throttle_description;
sim_int_timer_dev.description = &sim_int_timer_description;
sim_stop_dev.description = &sim_int_stop_description;
if (sim_switches & SWMASK ('P')) {
    sim_cancel (&SIM_INTERNAL_UNIT);
    sim_calb_tmr = -1;
    }
return SCPE_OK;
}

void sim_start_timer_services (void)
{
int32 tmr;
uint32 sim_prompt_time = (sim_gtime () > 0) ? (sim_os_msec () - sim_stop_time) : 0;
int32 registered_units = 0;

sim_time_at_sim_prompt +=  (((double)sim_prompt_time) / 1000.0);
for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    RTC *rtc = &rtcs[tmr];

    if (rtc->initd) {                /* calibrated clock initialized? */
        rtc->rtime += sim_prompt_time;
        rtc->vtime += sim_prompt_time;
        sim_debug (DBG_CAL, &sim_timer_dev, "sim_start_timer_services(tmr=%d) - adjusting calibration real time by %d ms\n", tmr, (int)sim_prompt_time);
        if (rtc->clock_catchup_eligible)
            rtc->calib_tick_time += (((double)sim_prompt_time) / 1000.0);
        if (rtc->clock_unit)
            ++registered_units;
        }
    }
if (registered_units == 1)
    sim_catchup_ticks = FALSE;
if (sim_calb_tmr == -1) {
    sim_debug (DBG_CAL, &sim_timer_dev, "sim_start_timer_services() - starting from scratch\n");
    _rtcn_configure_calibrated_clock (sim_calb_tmr);
    }
else {
    if (sim_calb_tmr == SIM_NTIMERS) {
        sim_debug (DBG_CAL, &sim_timer_dev, "sim_start_timer_services() - restarting internal timer after %d %s\n", 
                                            sim_internal_timer_time, sim_vm_interval_units);
        sim_activate (&SIM_INTERNAL_UNIT, sim_internal_timer_time);
        }
    }
if (sim_timer_stop_time > sim_gtime())
    sim_activate_abs (&sim_stop_unit, (int32)(sim_timer_stop_time - sim_gtime()));
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
    }
pthread_mutex_unlock (&sim_timer_lock);
#endif
}

void sim_stop_timer_services (void)
{
int tmr;

sim_debug (DBG_TRC, &sim_timer_dev, "sim_stop_timer_services(sim_interval=%d, sim_calb_tmr=%d)\n", sim_interval, sim_calb_tmr);

if (sim_interval < 0)
    sim_interval = 0;               /* No catching up after stopping */

for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    int32 accum;
    RTC *rtc = &rtcs[tmr];

    if (rtc->clock_unit) {
        int32 clock_time = _sim_activate_time (rtc->timer_unit);

        /* Stop clock assist unit and make sure the clock unit has a tick queued */
        if (sim_is_active (rtc->timer_unit)) {
            sim_cancel (rtc->timer_unit);
            sim_debug (DBG_QUE, &sim_timer_dev, "sim_stop_timer_services() - tmr=%d scheduling %s after %d\n", tmr, sim_uname (rtc->clock_unit), clock_time);
            _sim_activate (rtc->clock_unit, clock_time);
            }
        /* Move coscheduled units to the standard event queue */
        /* scheduled to fire at the same time as the related */
        /* clock unit is to fire with excess time reflected in */
        /* the unit usecs_remaining value */
        accum = rtc->cosched_interval;
        while (rtc->clock_cosched_queue != QUEUE_LIST_END) {
            UNIT *cptr = rtc->clock_cosched_queue;
            double usecs_remaining = cptr->usecs_remaining;

            rtc->clock_cosched_queue = cptr->next;
            cptr->next = NULL;
            cptr->cancel = NULL;
            accum += cptr->time;
            cptr->usecs_remaining = 0.0;
            _sim_activate (cptr, clock_time);
            cptr->usecs_remaining = usecs_remaining + floor(1000000.0 * (accum - ((accum > 0) ? 1 : 0)) * rtc->clock_tick_size);
            sim_debug (DBG_QUE, &sim_timer_dev, "sim_stop_timer_services() - tmr=%d scheduling %s after %d and %.0f usecs\n", tmr, sim_uname (cptr), clock_time, cptr->usecs_remaining);
            }
        rtc->cosched_interval = 0;
        }
    }

if (sim_calb_tmr == SIM_NTIMERS)
    sim_internal_timer_time = sim_activate_time (&SIM_INTERNAL_UNIT) - 1;
sim_cancel (&SIM_INTERNAL_UNIT);                    /* Make sure Internal Timer is stopped */
sim_cancel (&sim_timer_units[SIM_NTIMERS]);
sim_calb_tmr_last = sim_calb_tmr;                   /* Save calibrated timer value for display */
sim_inst_per_sec_last = sim_timer_inst_per_sec ();  /* Save execution rate for display */
sim_stop_time = sim_os_msec ();                     /* record when execution stopped */
#if defined(SIM_ASYNCH_CLOCKS)
pthread_mutex_lock (&sim_timer_lock);
if (sim_timer_thread_running) {
    sim_debug (DBG_TRC, &sim_timer_dev, "sim_stop_timer_services() - stopping\n");
    pthread_cond_signal (&sim_timer_wake);
    pthread_mutex_unlock (&sim_timer_lock);
    pthread_join (sim_timer_thread, NULL);
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
double inst_per_sec = sim_inst_per_sec_last;
RTC *rtc;

if (sim_calb_tmr == -1)
    return inst_per_sec;
rtc = &rtcs[sim_calb_tmr];
inst_per_sec = ((double)rtc->currd) * rtc->hz;
if (inst_per_sec == 0.0)
    inst_per_sec = ((double)rtc->currd) * sim_int_clk_tps;
return inst_per_sec;
}

t_stat sim_timer_activate (UNIT *uptr, int32 interval)
{
AIO_VALIDATE(uptr);
return sim_timer_activate_after (uptr, (double)((interval * 1000000.0) / sim_timer_inst_per_sec ()));
}

t_stat sim_timer_activate_after (UNIT *uptr, double usec_delay)
{
UNIT *ouptr = uptr;
int inst_delay, tmr;
double inst_delay_d, inst_per_usec;
t_stat stat;
RTC *crtc;

AIO_VALIDATE(uptr);
/* If this is a clock unit, we need to schedule the related timer unit instead */
for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    RTC *rtc = &rtcs[tmr];

    if (rtc->clock_unit == uptr) {
        uptr = rtc->timer_unit;
        break;
        }
    }
if (sim_is_active (uptr))                               /* already active? */
    return SCPE_OK;
if (usec_delay < 0.0) {
    sim_debug (DBG_QUE, &sim_timer_dev, "sim_timer_activate_after(%s, %.0f usecs) - surprising usec value\n", 
               sim_uname(uptr), usec_delay);
    }
if ((sim_is_running) || (tmr <= SIM_NTIMERS))
    uptr->usecs_remaining = 0.0;
else {                                      /* defer non timer wallclock activations until a calibrated timer is in effect */
    uptr->usecs_remaining = usec_delay;
    usec_delay = 0;
    }
/* 
 * Handle long delays by aligning with the calibrated timer's calibration
 * activities.  Delays which would expire prior to the next calibration
 * are specifically scheduled directly based on the the current instruction
 * execution rate.  Longer delays are coscheduled to fire on the first tick
 * after the next calibration and at that time are either scheduled directly
 * or re-coscheduled for the next calibration time, repeating until the total
 * desired time has elapsed.
 */
inst_per_usec = sim_timer_inst_per_sec () / 1000000.0;
inst_delay_d = floor(inst_per_usec * usec_delay);
inst_delay = (int32)inst_delay_d;
if ((inst_delay == 0) && (usec_delay != 0))
    inst_delay_d = inst_delay = 1;  /* Minimum non-zero delay is 1 instruction */
if (uptr->usecs_remaining != 0.0)   /* No calibrated timer yet, wait one cycle */
    inst_delay_d = inst_delay = 1;  /* Minimum non-zero delay is 1 instruction */
if (sim_calb_tmr != -1) {
    crtc = &rtcs[sim_calb_tmr];
    if (crtc->hz) {                 /* Calibrated Timer available? */
        int32 inst_til_tick = sim_activate_time (crtc->timer_unit) - 1;
        int32 ticks_til_calib = crtc->hz - crtc->ticks;
        double usecs_per_tick = floor (1000000.0 / crtc->hz);
        int32 inst_til_calib = inst_til_tick + ((ticks_til_calib - 1) * crtc->currd);
        uint32 usecs_til_calib = (uint32)ceil(inst_til_calib / inst_per_usec);

        if ((uptr != crtc->timer_unit) &&                   /* Not scheduling calibrated timer */
            (inst_til_tick > 0)) {                          /* and tick not pending? */
            if (inst_delay_d > (double)inst_til_calib) {    /* long wait? */
                stat = sim_clock_coschedule_tmr (uptr, sim_calb_tmr, ticks_til_calib - 1);
                uptr->usecs_remaining = (stat == SCPE_OK) ? usec_delay - usecs_til_calib : 0.0;
                sim_debug (DBG_TIM, &sim_timer_dev, "sim_timer_activate_after(%s, %.0f usecs) - coscheduling with with calibrated timer(%d), ticks=%d, usecs_remaining=%.0f usecs, inst_til_tick=%d, ticks_til_calib=%d, usecs_til_calib=%u\n", 
                           sim_uname(uptr), usec_delay, sim_calb_tmr, ticks_til_calib, uptr->usecs_remaining, inst_til_tick, ticks_til_calib, usecs_til_calib);
                sim_debug (DBG_CHK, &sim_timer_dev, "sim_timer_activate_after(%s, %.0f usecs) - result = %.0f usecs, %.0f usecs\n", 
                           sim_uname(uptr), usec_delay, sim_timer_activate_time_usecs (ouptr), sim_timer_activate_time_usecs (uptr));
                return stat;
                }
            if ((usec_delay > (2 * usecs_per_tick)) &&
                (ticks_til_calib > 1)) {                    /* long wait? */
                double usecs_til_tick = floor (inst_til_tick / inst_per_usec);

                stat = sim_clock_coschedule_tmr (uptr, sim_calb_tmr, 0);
                uptr->usecs_remaining = (stat == SCPE_OK) ? usec_delay - usecs_til_tick : 0.0;
                sim_debug (DBG_TIM, &sim_timer_dev, "sim_timer_activate_after(%s, %.0f usecs) - coscheduling with with calibrated timer(%d), ticks=%d, usecs_remaining=%.0f usecs, inst_til_tick=%d, usecs_til_tick=%.0f\n", 
                           sim_uname(uptr), usec_delay, sim_calb_tmr, 0, uptr->usecs_remaining, inst_til_tick, usecs_til_tick);
                sim_debug (DBG_CHK, &sim_timer_dev, "sim_timer_activate_after(%s, %.0f usecs) - result = %.0f usecs, %.0f usecs\n", 
                           sim_uname(uptr), usec_delay, sim_timer_activate_time_usecs (ouptr), sim_timer_activate_time_usecs (uptr));
                return stat;
                }
            }
        }
    }
/* 
 * We're here to schedule if:
 * No Calibrated Timer, OR
 * Scheduling the Calibrated Timer OR
 * Short delay
 */
/*
 * Bound delay to avoid overflow.
 * Long delays are usually canceled before they expire, however bounding the 
 * delay will cause sim_activate_time to return inconsistent results when 
 * truncation has happened.
 */
if (inst_delay_d > (double)0x7fffffff)
    inst_delay_d = (double)0x7fffffff;              /* Bound delay to avoid overflow.  */
inst_delay = (int32)inst_delay_d;
#if defined(SIM_ASYNCH_CLOCKS)
if ((sim_asynch_timer) &&
    (usec_delay > sim_idle_rate_ms*1000.0)) {
    double d_now = sim_timenow_double ();
    UNIT *cptr, *prvptr;

    uptr->a_usec_delay = usec_delay;
    uptr->a_due_time = d_now + (usec_delay / 1000000.0);
    uptr->a_due_gtime = sim_gtime () + (sim_timer_inst_per_sec () * (usec_delay / 1000000.0));
    uptr->cancel = &_sim_wallclock_cancel;              /* bind cleanup method */
    uptr->a_is_active = &_sim_wallclock_is_active;
    if (tmr <= SIM_NTIMERS) {                            /* Timer Unit? */
        RTC *rtc = &rtcs[tmr];

        rtc->clock_unit->cancel = &_sim_wallclock_cancel;
        rtc->clock_unit->a_is_active = &_sim_wallclock_is_active;
        }

    sim_debug (DBG_TIM, &sim_timer_dev, "sim_timer_activate_after(%s, %.0f usecs) - queueing wallclock addition at %.6f\n", 
               sim_uname(uptr), usec_delay, uptr->a_due_time);

    pthread_mutex_lock (&sim_timer_lock);
    for (cptr = sim_wallclock_queue, prvptr = NULL; cptr != QUEUE_LIST_END; cptr = cptr->a_next) {
        if (uptr->a_due_time < cptr->a_due_time)
            break;
        prvptr = cptr;
        }
    if (prvptr == NULL) {                           /* inserting at head */
        uptr->a_next = QUEUE_LIST_END;              /* Temporarily mark as active */
        if (sim_timer_thread_running) {
            while (sim_wallclock_entry) {               /* wait for any prior entry has been digested */
                sim_debug (DBG_TIM, &sim_timer_dev, "sim_timer_activate_after(%s, %.0f usecs) - queue insert entry %s busy waiting for 1ms\n", 
                           sim_uname(uptr), usec_delay, sim_uname(sim_wallclock_entry));
                pthread_mutex_unlock (&sim_timer_lock);
                sim_os_ms_sleep (1);
                pthread_mutex_lock (&sim_timer_lock);
                }
            }
        sim_wallclock_entry = uptr;
        pthread_mutex_unlock (&sim_timer_lock);
        pthread_cond_signal (&sim_timer_wake);      /* wake the timer thread to deal with it */
        return SCPE_OK;
        }
    else {                                          /* inserting at prvptr */
        uptr->a_next = prvptr->a_next;
        prvptr->a_next = uptr;
        pthread_mutex_unlock (&sim_timer_lock);
        return SCPE_OK;
        }
    }
#endif
stat = _sim_activate (uptr, inst_delay);                /* queue it now */
sim_debug (DBG_TIM, &sim_timer_dev, "sim_timer_activate_after(%s, %.0f usecs) - queue addition at %d - remnant: %.0f\n", 
           sim_uname(uptr), usec_delay, inst_delay, uptr->usecs_remaining);
sim_debug (DBG_CHK, &sim_timer_dev, "sim_timer_activate_after(%s, %.0f usecs) - result = %.0f usecs, %.0f usecs\n", 
           sim_uname(uptr), usec_delay, sim_timer_activate_time_usecs (ouptr), sim_timer_activate_time_usecs (uptr));
return stat;
}

/* Clock coscheduling routines */

t_stat sim_register_clock_unit_tmr (UNIT *uptr, int32 tmr)
{
RTC *rtc;

if (tmr == SIM_INTERNAL_CLK)
    tmr = SIM_NTIMERS;
else {
    if ((tmr < 0) || (tmr > SIM_NTIMERS))
        return SCPE_IERR;
    }
rtc = &rtcs[tmr];
if (NULL == uptr) {                         /* deregistering? */
    /* Migrate any coscheduled devices to the standard queue */
    /* they will fire and subsequently requeue themselves */
    while (rtc->clock_cosched_queue != QUEUE_LIST_END) {
        UNIT *uptr = rtc->clock_cosched_queue;
        double usecs_remaining = sim_timer_activate_time_usecs (uptr);

        _sim_coschedule_cancel (uptr);
        _sim_activate (uptr, 1);
        uptr->usecs_remaining = usecs_remaining;
        }
    if (rtc->clock_unit) {
        sim_cancel (rtc->clock_unit);
        rtc->clock_unit->dynflags &= ~UNIT_TMR_UNIT;
        }
    rtc->clock_unit = NULL;
    sim_cancel (rtc->timer_unit);
    return SCPE_OK;
    }
if (rtc->clock_unit == NULL)
    rtc->clock_cosched_queue = QUEUE_LIST_END;
rtc->clock_unit = uptr;
uptr->dynflags |= UNIT_TMR_UNIT;
rtc->timer_unit->flags = ((tmr == SIM_NTIMERS) ? 0 : UNIT_DIS) | 
                          (rtc->clock_unit ? UNIT_IDLE : 0);
return SCPE_OK;
}

/* Default timer is 0, otherwise use a calibrated one if it exists */
int32 sim_rtcn_calibrated_tmr (void)
{
return ((rtcs[0].currd && rtcs[0].hz) ? 0 : ((sim_calb_tmr != -1) ? sim_calb_tmr : 0));
}

int32 sim_rtcn_tick_size (int32 tmr)
{
RTC *rtc = &rtcs[tmr];

return (rtc->currd) ? rtc->currd : 10000;
}

t_stat sim_register_clock_unit (UNIT *uptr)
{
return sim_register_clock_unit_tmr (uptr, 0);
}

t_stat sim_clock_coschedule (UNIT *uptr, int32 interval)
{
int32 tmr = sim_rtcn_calibrated_tmr ();
int32 ticks = (interval + (sim_rtcn_tick_size (tmr)/2))/sim_rtcn_tick_size (tmr);/* Convert to ticks */

sim_debug (DBG_QUE, &sim_timer_dev, "sim_clock_coschedule(%s, interval=%d, ticks=%d)\n", sim_uname(uptr), interval, ticks);
return sim_clock_coschedule_tmr (uptr, tmr, ticks);
}

t_stat sim_clock_coschedule_abs (UNIT *uptr, int32 interval)
{
sim_debug (DBG_QUE, &sim_timer_dev, "sim_clock_coschedule_abs(%s, interval=%d)\n", sim_uname(uptr), interval);
sim_cancel (uptr);
return sim_clock_coschedule (uptr, interval);
}

/* ticks - 0 means on the next tick, 1 means the second tick, etc.  */

t_stat sim_clock_coschedule_tmr (UNIT *uptr, int32 tmr, int32 ticks)
{
RTC *rtc;

if (ticks < 0)
    return SCPE_ARG;
if (sim_is_active (uptr)) {
    sim_debug (DBG_TIM, &sim_timer_dev, "sim_clock_coschedule_tmr(%s, tmr=%d, ticks=%d) - already active\n", sim_uname (uptr), tmr, ticks);
    return SCPE_OK;
    }
if (tmr == SIM_INTERNAL_CLK)
    tmr = SIM_NTIMERS;
else {
    if ((tmr < 0) || (tmr > SIM_NTIMERS))
        return sim_activate (uptr, MAX(1, ticks) * 10000);
    }
rtc = &rtcs[tmr];
if ((NULL == rtc->clock_unit) || (rtc->hz == 0)) {
    sim_debug (DBG_TIM, &sim_timer_dev, "sim_clock_coschedule_tmr(%s, tmr=%d, ticks=%d) - no clock activating after %d %s\n", sim_uname (uptr), tmr, ticks, ticks * (rtc->currd ? rtc->currd : rtcs[sim_rtcn_calibrated_tmr ()].currd), sim_vm_interval_units);
    return sim_activate (uptr, ticks * (rtc->currd ? rtc->currd : rtcs[sim_rtcn_calibrated_tmr ()].currd));
    }
else {
    UNIT *cptr, *prvptr;
    int32 accum;

    if (rtc->clock_cosched_queue != QUEUE_LIST_END)
        rtc->clock_cosched_queue->time = rtc->cosched_interval;
    prvptr = NULL;
    accum = 0;
    for (cptr = rtc->clock_cosched_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
        if (ticks < (accum + cptr->time))
            break;
        accum += cptr->time;
        prvptr = cptr;
        }
    if (prvptr == NULL) {
        cptr = uptr->next = rtc->clock_cosched_queue;
        rtc->clock_cosched_queue = uptr;
        }
    else {
        cptr = uptr->next = prvptr->next;
        prvptr->next = uptr;
        }
    uptr->time = ticks - accum;
    if (cptr != QUEUE_LIST_END)
        cptr->time = cptr->time - uptr->time;
    uptr->cancel = &_sim_coschedule_cancel;             /* bind cleanup method */
    if (uptr == rtc->clock_cosched_queue)
        rtc->cosched_interval = rtc->clock_cosched_queue->time;
    sim_debug (DBG_QUE, &sim_timer_dev, "sim_clock_coschedule_tmr(%s, tmr=%d, ticks=%d, hz=%d) - queueing for clock co-schedule, interval now: %d\n", sim_uname (uptr), tmr, ticks, rtc->hz, rtc->cosched_interval);
    }
return SCPE_OK;
}

t_stat sim_clock_coschedule_tmr_abs (UNIT *uptr, int32 tmr, int32 ticks)
{
sim_cancel (uptr);
return sim_clock_coschedule_tmr (uptr, tmr, ticks);
}

/* Cancel a unit on the coschedule queue */
static t_bool _sim_coschedule_cancel (UNIT *uptr)
{
AIO_UPDATE_QUEUE;
if (uptr->next) {                           /* On a queue? */
    int tmr;
    UNIT *nptr;

    for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
        RTC *rtc = &rtcs[tmr];

        if (rtc->clock_unit) {
            if (uptr == rtc->clock_cosched_queue) {
                nptr = rtc->clock_cosched_queue = uptr->next;
                uptr->next = NULL;
                }
            else {
                UNIT *cptr;

                for (cptr = rtc->clock_cosched_queue;
                     (cptr != QUEUE_LIST_END);
                     cptr = cptr->next) {
                    if (cptr->next == uptr) {
                        nptr = cptr->next = (uptr)->next;
                        uptr->next = NULL;
                        break;
                        }
                    }
                }
            if (uptr->next == NULL) {           /* found? */
                uptr->cancel = NULL;
                uptr->usecs_remaining = 0;
                if (nptr != QUEUE_LIST_END)
                    nptr->time += uptr->time;
                sim_debug (DBG_QUE, &sim_timer_dev, "Canceled Clock Coscheduled Event for %s\n", sim_uname(uptr));
                return TRUE;
                }
            }
        }
    }
return FALSE;
}

t_bool sim_timer_is_active (UNIT *uptr)
{
int32 tmr;

if (!(uptr->dynflags & UNIT_TMR_UNIT))
    return FALSE;
for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    RTC *rtc = &rtcs[tmr];

    if (rtc->clock_unit == uptr)
        return sim_is_active (&sim_timer_units[tmr]);
    }
return FALSE;
}

t_bool sim_timer_cancel (UNIT *uptr)
{
int32 tmr;

if (!(uptr->dynflags & UNIT_TMR_UNIT))
    return SCPE_IERR;
for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    RTC *rtc = &rtcs[tmr];

    if (rtc->clock_unit == uptr)
        return sim_cancel (&sim_timer_units[tmr]);
    }
return SCPE_IERR;
}

#if defined(SIM_ASYNCH_CLOCKS)
static t_bool _sim_wallclock_cancel (UNIT *uptr)
{
int32 tmr;
t_bool b_return = FALSE;

AIO_UPDATE_QUEUE;
pthread_mutex_lock (&sim_timer_lock);
/* If this is a clock unit, we need to cancel both this and the related timer unit */
for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    RTC *rtc = &rtcs[tmr];

    if (rtc->clock_unit == uptr) {
        uptr = &sim_timer_units[tmr];
        break;
        }
    }
if (uptr->a_next) {
    UNIT *cptr;

    if (uptr == sim_wallclock_entry) {  /* Pending on the queue? */
        sim_wallclock_entry = NULL;
        uptr->a_next = NULL;
        sim_debug (DBG_QUE, &sim_timer_dev, "Canceled Queue Pending Timer Event for %s\n", sim_uname(uptr));
        }
    else {
        if (uptr == sim_wallclock_queue) {
            sim_wallclock_queue = uptr->a_next;
            uptr->a_next = NULL;
            sim_debug (DBG_QUE, &sim_timer_dev, "Canceled Top Timer Event for %s\n", sim_uname(uptr));
            pthread_cond_signal (&sim_timer_wake);
            }
        else {
            for (cptr = sim_wallclock_queue;
                (cptr != QUEUE_LIST_END);
                cptr = cptr->a_next) {
                if (cptr->a_next == (uptr)) {
                    cptr->a_next = (uptr)->a_next;
                    uptr->a_next = NULL;
                    sim_debug (DBG_QUE, &sim_timer_dev, "Canceled Timer Event for %s\n", sim_uname(uptr));
                    break;
                    }
                }
            }
        }
    if (uptr->a_next == NULL) {         /* Was canceled? */
        uptr->a_due_time = uptr->a_due_gtime = uptr->a_usec_delay = 0;
        uptr->cancel = NULL;
        uptr->a_is_active = NULL;
        if (tmr <= SIM_NTIMERS) {                        /* Timer Unit? */
            RTC *rtc = &rtcs[tmr];

            rtc->clock_unit->cancel = NULL;
            rtc->clock_unit->a_is_active = NULL;
            }
        b_return = TRUE;
        }
    }
pthread_mutex_unlock (&sim_timer_lock);
return b_return;
}

static t_bool _sim_wallclock_is_active (UNIT *uptr)
{
int32 tmr;

if (uptr->a_next)
    return TRUE;
/* If this is a clock unit, we need to examine the related timer unit instead */
for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    RTC *rtc = &rtcs[tmr];

    if (rtc->clock_unit == uptr)
        return (sim_timer_units[tmr].a_next != NULL);
    }
return FALSE;
}
#endif /* defined(SIM_ASYNCH_CLOCKS) */

int32 _sim_timer_activate_time (UNIT *uptr)
{
UNIT *cptr;
int32 tmr;

#if defined(SIM_ASYNCH_CLOCKS)
if (uptr->a_is_active == &_sim_wallclock_is_active) {
    double d_result;

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
#endif /* defined(SIM_ASYNCH_CLOCKS) */

if (uptr->cancel == &_sim_coschedule_cancel) {
    for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
        int32 accum = 0;
        RTC *rtc = &rtcs[tmr];


        for (cptr = rtc->clock_cosched_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
            if (cptr == rtc->clock_cosched_queue) {
                if (rtc->cosched_interval > 0)
                    accum += rtc->cosched_interval;
                }
            else
                accum += cptr->time;
            if (cptr == uptr)
                return (rtc->currd * accum) + sim_activate_time (&sim_timer_units[tmr]);
            }
        }
    }
for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    if ((uptr == &sim_timer_units[tmr]) && (uptr->next)){
        return _sim_activate_time (&sim_timer_units[tmr]);
        }
    }
return -1;                                          /* Not found. */    
}

double sim_timer_activate_time_usecs (UNIT *uptr)
{
UNIT *cptr;
int32 tmr;
double result = -1.0;

/* If this is a clock unit, we need to return the related clock assist unit instead */
for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    RTC *rtc = &rtcs[tmr];

    if (rtc->clock_unit == uptr) {
        uptr = &sim_timer_units[tmr];
        break;
        }
    }

if (!sim_is_active (uptr)) {
    sim_debug (DBG_QUE, &sim_timer_dev, "sim_timer_activate_time_usecs(%s) - not active\n", sim_uname (uptr));
    return result;
    }
#if defined(SIM_ASYNCH_CLOCKS)
if (uptr->a_is_active == &_sim_wallclock_is_active) {
    pthread_mutex_lock (&sim_timer_lock);
    if (uptr == sim_wallclock_entry) {
        result = uptr->a_due_gtime - sim_gtime ();
        if (result < 0.0)
            result = 0.0;
        pthread_mutex_unlock (&sim_timer_lock);
        result = uptr->usecs_remaining + (1000000.0 * (result / sim_timer_inst_per_sec ())) + 1;
        sim_debug (DBG_QUE, &sim_timer_dev, "sim_timer_activate_time_usecs(%s) wallclock_entry - %.0f usecs, inst_per_sec=%.0f\n", sim_uname (uptr), result, sim_timer_inst_per_sec ());
        return result;
        }
    for (cptr = sim_wallclock_queue;
         cptr != QUEUE_LIST_END;
         cptr = cptr->a_next)
        if (uptr == cptr) {
            result = uptr->a_due_gtime - sim_gtime ();
            if (result < 0.0)
                result = 0.0;
            pthread_mutex_unlock (&sim_timer_lock);
            result = uptr->usecs_remaining + (1000000.0 * (result / sim_timer_inst_per_sec ())) + 1;
            sim_debug (DBG_QUE, &sim_timer_dev, "sim_timer_activate_time_usecs(%s) wallclock - %.0f usecs, inst_per_sec=%.0f\n", sim_uname (uptr), result, sim_timer_inst_per_sec ());
            return result;
            }
    pthread_mutex_unlock (&sim_timer_lock);
    }
if (uptr->a_next) {
    result = uptr->usecs_remaining + (1000000.0 * (uptr->a_event_time / sim_timer_inst_per_sec ())) + 1;
    sim_debug (DBG_QUE, &sim_timer_dev, "sim_timer_activate_time_usecs(%s) asynch - %.0f usecs, inst_per_sec=%.0f\n", sim_uname (uptr), result, sim_timer_inst_per_sec ());
    return result;
    }
#endif /* defined(SIM_ASYNCH_CLOCKS) */

if (uptr->cancel == &_sim_coschedule_cancel) {
    for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
        int32 accum = 0;
        RTC *rtc = &rtcs[tmr];

        for (cptr = rtc->clock_cosched_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
            if (cptr == rtc->clock_cosched_queue) {
                if (rtc->cosched_interval > 0)
                    accum += rtc->cosched_interval;
                }
            else
                accum += cptr->time;
            if (cptr == uptr) {
                result = uptr->usecs_remaining + ceil(1000000.0 * ((rtc->currd * accum) + sim_activate_time (&sim_timer_units[tmr]) - 1) / sim_timer_inst_per_sec ());
                sim_debug (DBG_QUE, &sim_timer_dev, "sim_timer_activate_time_usecs(%s) coscheduled - %.0f usecs, inst_per_sec=%.0f, tmr=%d, ticksize=%d, ticks=%d, inst_til_tick=%d, usecs_remaining=%.0f\n", sim_uname (uptr), result, sim_timer_inst_per_sec (), tmr, rtc->currd, accum, sim_activate_time (&sim_timer_units[tmr]) - 1, uptr->usecs_remaining);
                return result;
                }
            }
        }
    }
for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    RTC *rtc = &rtcs[tmr];

    if ((uptr == rtc->clock_unit) && (uptr->next)) {
        result = rtc->clock_unit->usecs_remaining + (1000000.0 * (sim_activate_time (&sim_timer_units[tmr]) - 1)) / sim_timer_inst_per_sec ();
        sim_debug (DBG_QUE, &sim_timer_dev, "sim_timer_activate_time_usecs(%s) clock - %.0f usecs, inst_per_sec=%.0f, usecs_remaining=%.0f\n", sim_uname (uptr), result, sim_timer_inst_per_sec (), uptr->usecs_remaining);
        return result;
        }
    if ((uptr == &sim_timer_units[tmr]) && (uptr->next)){
        result = uptr->usecs_remaining + (1000000.0 * (sim_activate_time (uptr) - 1)) / sim_timer_inst_per_sec ();
        sim_debug (DBG_QUE, &sim_timer_dev, "sim_timer_activate_time_usecs(%s) clock - %.0f usecs, inst_per_sec=%.0f, usecs_remaining=%.0f\n", sim_uname (uptr), result, sim_timer_inst_per_sec (), uptr->usecs_remaining);
        return result;
        }
    }
result = uptr->usecs_remaining + (1000000.0 * (sim_activate_time (uptr) - 1)) / sim_timer_inst_per_sec ();
sim_debug (DBG_QUE, &sim_timer_dev, "sim_timer_activate_time_usecs(%s) clock - %.0f usecs, inst_per_sec=%.0f, usecs_remaining=%.0f\n", sim_uname (uptr), result, sim_timer_inst_per_sec (), uptr->usecs_remaining);
return result;                                          /* Not found. */    
}

/* read only memory delayed support

   Some simulation activities need a 'regulated' memory access
   time to meet timing assumptions in the code being executed.

   The default calibration determines a way to limit activities
   to 1Mhz for each call to sim_rom_read_with_delay().  If a 
   simulator needs a different delay factor, the 1 Mhz initial 
   value can be queried with sim_get_rom_delay_factor() and the 
   result can be adjusted as nessary and the operating delay
   can be set with sim_set_rom_delay_factor().
*/

SIM_NOINLINE static int32 _rom_swapb(int32 val)
{
return ((val << 24) & 0xff000000) | (( val << 8) & 0xff0000) |
    ((val >> 8) & 0xff00) | ((val >> 24) & 0xff);
}

static volatile int32 rom_loopval = 0;

SIM_NOINLINE int32 sim_rom_read_with_delay (int32 val)
{
uint32 i, l = sim_rom_delay;

for (i = 0; i < l; i++)
    rom_loopval |= (rom_loopval + val) ^ _rom_swapb (_rom_swapb (rom_loopval + val));
return val + rom_loopval;
}

SIM_NOINLINE uint32 sim_get_rom_delay_factor (void)
{
/* Calibrate the loop delay factor at startup.
   Do this 4 times and use the largest value computed. 
   The goal here is to come up with a delay factor which will throttle
   a 6 byte delay loop running from ROM address space to execute
   1 instruction per usec */

if (sim_rom_delay == 0) {
    uint32 i, ts, te, c = 10000, samples = 0;
    while (1) {
        c = c * 2;
        te = sim_os_msec();
        while (te == (ts = sim_os_msec ()));            /* align on ms tick */

/* This is merely a busy wait with some "work" that won't get optimized
   away by a good compiler. loopval always is zero.  To avoid smart compilers,
   the loopval variable is referenced in the function arguments so that the
   function expression is not loop invariant.  It also must be referenced
   by subsequent code to avoid the whole computation being eliminated. */

        for (i = 0; i < c; i++)
            rom_loopval |= (rom_loopval + ts) ^ _rom_swapb (_rom_swapb (rom_loopval + ts));
        te = sim_os_msec (); 
        if ((te - ts) < 50)                         /* sample big enough? */
            continue;
        if (sim_rom_delay < (rom_loopval + (c / (te - ts) / 1000) + 1))
            sim_rom_delay = rom_loopval + (c / (te - ts) / 1000) + 1;
        if (++samples >= 4)
            break;
        c = c / 2;
        }
    if (sim_rom_delay < 5)
        sim_rom_delay = 5;
    }
return sim_rom_delay;
}

void sim_set_rom_delay_factor (uint32 delay)
{
sim_rom_delay = delay;
}

/* sim_timer_precalibrate_execution_rate
 *
 * The point of this routine is to run a bunch of simulator provided
 * instructions that don't do anything, but run in an effective loop.
 * That loop is run for some 5 million instructions and based on 
 * the time those 5 million instructions take to execute the effective
 * execution rate.  That rate is used to avoid the initial 3 to 5 
 * seconds that normal clock calibration takes.
 *
 */
void sim_timer_precalibrate_execution_rate (void)
{
const char **cmd = sim_clock_precalibrate_commands;
uint32 start, end;
int32 saved_switches = sim_switches;
int32 tmr;
UNIT precalib_unit = { UDATA (&sim_timer_stop_svc, 0, 0) };

if (cmd == NULL)
    return;
sim_run_boot_prep (RU_GO);
while (sim_clock_queue != QUEUE_LIST_END)
    sim_cancel (sim_clock_queue);
while (*cmd)
     exdep_cmd (EX_D, *(cmd++));
sim_switches = saved_switches;
sim_cancel (&SIM_INTERNAL_UNIT);
sim_activate (&precalib_unit, sim_precalibrate_ips);
start = sim_os_msec();
sim_instr();
end = sim_os_msec();
sim_precalibrate_ips = (int32)(1000.0 * (sim_precalibrate_ips / (double)(end - start)));

for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    RTC *rtc = &rtcs[tmr];

    if (rtc->hz)
        rtc->initd = rtc->currd = (int32)(((double)sim_precalibrate_ips) / rtc->hz);
    }
reset_all_p (0);
sim_run_boot_prep (RU_GO);
for (tmr=0; tmr<=SIM_NTIMERS; tmr++) {
    RTC *rtc = &rtcs[tmr];

    if (rtc->calib_initializations)
        rtc->calib_initializations = 1;
    }
sim_inst_per_sec_last = sim_precalibrate_ips;
sim_idle_stable = 0;
}

double 
sim_host_speed_factor (void)
{
if (sim_precalibrate_ips > sim_vm_initial_ips)
    return 1.0;
return (double)sim_vm_initial_ips / (double)sim_precalibrate_ips;
}
