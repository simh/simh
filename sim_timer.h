/* sim_timer.h: simulator timer library headers

   Copyright (c) 1993-2008, Robert M Supnik

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

   28-Apr-07    RMS     Added sim_rtc_init_all
   17-Oct-06    RMS     Added idle support
   02-Jan-04    RMS     Split out from SCP
*/

#ifndef SIM_TIMER_H_
#define SIM_TIMER_H_   0

/* Pick up a struct timespec definition if it is available */
#include <time.h>
#if defined(SIM_ASYNCH_IO) || defined(USE_READER_THREAD)
#include <pthread.h>
#endif

#if defined (__APPLE__)
#define HAVE_STRUCT_TIMESPEC 1   /* OSX defined the structure but doesn't tell us */
#endif

/* on HP-UX, CLOCK_REALTIME is enum, not preprocessor define */
#if !defined(CLOCK_REALTIME) && !defined(__hpux)
#define CLOCK_REALTIME 1
#define NEED_CLOCK_GETTIME 1
#if !defined(HAVE_STRUCT_TIMESPEC)
#define HAVE_STRUCT_TIMESPEC 1
#if !defined(_TIMESPEC_DEFINED)
#define _TIMESPEC_DEFINED
struct timespec {
    long   tv_sec;
    long   tv_nsec;
};
#endif /* _TIMESPEC_DEFINED */
#endif /* HAVE_STRUCT_TIMESPEC */
int clock_gettime(int clock_id, struct timespec *tp);
#endif


#define SIM_NTIMERS     8                           /* # timers */
#define SIM_TMAX        500                         /* max timer makeup */

#define SIM_INITIAL_IPS 50000                       /* uncalibrated assumption */
                                                    /* about instructions per second */

#define SIM_IDLE_CAL    10                          /* ms to calibrate */
#define SIM_IDLE_MAX    10                          /* max granularity idle */
#define SIM_IDLE_STMIN  10                          /* min sec for stability */
#define SIM_IDLE_STDFLT 20                          /* dft sec for stability */
#define SIM_IDLE_STMAX  600                         /* max sec for stability */

#define SIM_THROT_WINIT 1000                        /* cycles to skip */
#define SIM_THROT_WST   10000                       /* initial wait */
#define SIM_THROT_WMUL  4                           /* multiplier */
#define SIM_THROT_WMIN  100                         /* min wait */
#define SIM_THROT_MSMIN 10                          /* min for measurement */
#define SIM_THROT_NONE  0                           /* throttle parameters */
#define SIM_THROT_MCYC  1                           /* MegaCycles Per Sec */
#define SIM_THROT_KCYC  2                           /* KiloCycles Per Sec */
#define SIM_THROT_PCT   3                           /* Max Percent of host CPU */
#define SIM_THROT_SPC   4                           /* Specific periodic Delay */

#define TIMER_DBG_IDLE  1                           /* Debug Flag for Idle Debugging */
#define TIMER_DBG_QUEUE 2                           /* Debug Flag for Asynch Queue Debugging */

t_bool sim_timer_init (void);
void sim_timespec_diff (struct timespec *diff, struct timespec *min, struct timespec *sub);
#if defined(SIM_ASYNCH_CLOCKS)
double sim_timenow_double (void);
#endif
int32 sim_rtcn_init (int32 time, int32 tmr);
void sim_rtcn_init_all (void);
int32 sim_rtcn_calb (int32 ticksper, int32 tmr);
int32 sim_rtc_init (int32 time);
int32 sim_rtc_calb (int32 ticksper);
t_stat sim_show_timers (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, char* desc);
t_bool sim_idle (uint32 tmr, t_bool sin_cyc);
t_stat sim_set_throt (int32 arg, char *cptr);
t_stat sim_show_throt (FILE *st, DEVICE *dnotused, UNIT *unotused, int32 flag, char *cptr);
t_stat sim_set_idle (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat sim_clr_idle (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat sim_show_idle (FILE *st, UNIT *uptr, int32 val, void *desc);
void sim_throt_sched (void);
void sim_throt_cancel (void);
uint32 sim_os_msec (void);
void sim_os_sleep (unsigned int sec);
uint32 sim_os_ms_sleep (unsigned int msec);
uint32 sim_os_ms_sleep_init (void);
void sim_start_timer_services (void);
void sim_stop_timer_services (void);
t_stat sim_timer_change_asynch (void);
t_stat sim_timer_activate_after (UNIT *uptr, int32 usec_delay);
t_stat sim_register_clock_unit (UNIT *uptr);
t_stat sim_clock_coschedule (UNIT *uptr, int32 interval);
double sim_timer_inst_per_sec (void);
uint32 sim_timer_idle_capable (uint32 *hoat_tick_ms);

extern t_bool sim_idle_enab;                        /* idle enabled flag */
extern volatile t_bool sim_idle_wait;               /* idle waiting flag */
extern UNIT *sim_clock_unit;
extern t_bool sim_asynch_timer;
extern DEVICE sim_timer_dev;

#endif
    