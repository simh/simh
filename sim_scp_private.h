/* sim_scp_private.h: scp library private definitions

   Copyright (c) 2023, Mark Pizzolato

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
   MARK PIZZOLATO BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Mark Pizzolato shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Mark Pizzolato.

This include file may only be included by code in SCP libraries and should 
never be included directly in any simulator source code modules.

*/

#ifndef SIM_SCP_PRIVATE_H_
#define SIM_SCP_PRIVATE_H_    0

#ifdef  __cplusplus
extern "C" {
#endif

#include "sim_sock.h"

#ifdef USE_REGEX
#undef USE_REGEX
#endif
#if defined(HAVE_PCRE_H)
#include <pcre.h>
#define USE_REGEX 1
#endif


/* Asynch/Threaded I/O support */

#if defined (SIM_ASYNCH_IO)
#include <pthread.h>

#define SIM_ASYNCH_CLOCKS 1

extern pthread_mutex_t sim_asynch_lock;
extern pthread_cond_t sim_asynch_wake;
extern pthread_mutex_t sim_timer_lock;
extern pthread_cond_t sim_timer_wake;
extern t_bool sim_timer_event_canceled;
extern int32 sim_tmxr_poll_count;
extern pthread_cond_t sim_tmxr_poll_cond;
extern pthread_mutex_t sim_tmxr_poll_lock;
extern pthread_t sim_asynch_main_threadid;
extern UNIT * volatile sim_asynch_queue;
extern volatile t_bool sim_idle_wait;
extern int32 sim_asynch_check;
extern int32 sim_asynch_latency;
extern int32 sim_asynch_inst_latency;

/* Thread local storage */
#if defined(thread_local)
#define AIO_TLS thread_local
#elif (__STDC_VERSION__ >= 201112) && !(defined(__STDC_NO_THREADS__))
#define AIO_TLS _Thread_local
#elif defined(__GNUC__) && !defined(__APPLE__) && !defined(__hpux) && !defined(__OpenBSD__) && !defined(_AIX)
#define AIO_TLS __thread
#elif defined(_MSC_VER)
#define AIO_TLS __declspec(thread)
#else
/* Other compiler environment, then don't worry about thread local storage. */
/* It is primarily used only used in debugging messages */
#define AIO_TLS
#endif
#define AIO_QUEUE_CHECK(que, lock)                              \
    do {                                                        \
        UNIT *_cptr;                                            \
        if (lock)                                               \
            pthread_mutex_lock (lock);                          \
        for (_cptr = que;                                       \
            (_cptr != QUEUE_LIST_END);                          \
            _cptr = _cptr->next)                                \
            if (!_cptr->next)                                   \
                SIM_SCP_ABORT ("Queue Corruption detected");    \
        if (lock)                                               \
            pthread_mutex_unlock (lock);                        \
        } while (0)
#define AIO_MAIN_THREAD (pthread_equal ( pthread_self(), sim_asynch_main_threadid ))
#define AIO_LOCK                                                  \
    pthread_mutex_lock(&sim_asynch_lock)
#define AIO_UNLOCK                                                \
    pthread_mutex_unlock(&sim_asynch_lock)
#define AIO_IS_ACTIVE(uptr) (((uptr)->a_is_active ? (uptr)->a_is_active (uptr) : FALSE) || ((uptr)->a_next))

#if defined(__DECC_VER)
#include <builtins>
#if defined(__IA64)
#define USE_AIO_INTRINSICS 1
#endif
#endif
#if defined(_WIN32) || defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4) || defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8)
#define USE_AIO_INTRINSICS 1
#endif
/* Provide a way to test both Intrinsic and Lock based queue manipulations  */
/* when both are available on a particular platform                         */
#if defined(DONT_USE_AIO_INTRINSICS) && defined(USE_AIO_INTRINSICS)
#undef USE_AIO_INTRINSICS
#endif
#ifdef USE_AIO_INTRINSICS
/* This approach uses intrinsics to manage access to the link list head     */
/* sim_asynch_queue.  This implementation is a completely lock free design  */
/* which avoids the potential ABA issues.                                   */
#define AIO_QUEUE_MODE "Lock free asynchronous event queue"
#define AIO_INIT                                                  \
    do {                                                          \
      sim_asynch_main_threadid = pthread_self();                  \
      /* Empty list/list end uses the point value (void *)1.      \
         This allows NULL in an entry's a_next pointer to         \
         indicate that the entry is not currently in any list */  \
      sim_asynch_queue = QUEUE_LIST_END;                          \
      } while (0)
#define AIO_CLEANUP                                               \
    do {                                                          \
      pthread_mutex_destroy(&sim_asynch_lock);                    \
      pthread_cond_destroy(&sim_asynch_wake);                     \
      pthread_mutex_destroy(&sim_timer_lock);                     \
      pthread_cond_destroy(&sim_timer_wake);                      \
      pthread_mutex_destroy(&sim_tmxr_poll_lock);                 \
      pthread_cond_destroy(&sim_tmxr_poll_cond);                  \
      } while (0)
#ifdef _WIN32
#elif defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4) || defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8)
#define InterlockedCompareExchangePointer(Destination, Exchange, Comparand) __sync_val_compare_and_swap(Destination, Comparand, Exchange)
#elif defined(__DECC_VER)
#define InterlockedCompareExchangePointer(Destination, Exchange, Comparand) (void *)((int32)_InterlockedCompareExchange64(Destination, Exchange, Comparand))
#else
#error "Implementation of function InterlockedCompareExchangePointer() is needed to build with USE_AIO_INTRINSICS"
#endif
#define AIO_ILOCK AIO_LOCK
#define AIO_IUNLOCK AIO_UNLOCK
#define AIO_QUEUE_VAL (UNIT *)(InterlockedCompareExchangePointer((void * volatile *)&sim_asynch_queue, (void *)sim_asynch_queue, NULL))
#define AIO_QUEUE_SET(newval, oldval) (UNIT *)(InterlockedCompareExchangePointer((void * volatile *)&sim_asynch_queue, (void *)newval, oldval))
#define AIO_UPDATE_QUEUE sim_aio_update_queue ()
#define AIO_ACTIVATE(caller, uptr, event_time)                                   \
    if (!pthread_equal ( pthread_self(), sim_asynch_main_threadid )) {           \
      sim_aio_activate ((ACTIVATE_API)caller, uptr, event_time);                 \
      return SCPE_OK;                                                            \
    } else (void)0
#else /* !USE_AIO_INTRINSICS */
/* This approach uses a pthread mutex to manage access to the link list     */
/* head sim_asynch_queue.  It will always work, but may be slower than the  */
/* lock free approach when using USE_AIO_INTRINSICS                         */
#define AIO_QUEUE_MODE "Lock based asynchronous event queue"
#define AIO_INIT                                                  \
    do {                                                          \
      pthread_mutexattr_t attr;                                   \
                                                                  \
      pthread_mutexattr_init (&attr);                             \
      pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);  \
      pthread_mutex_init (&sim_asynch_lock, &attr);               \
      pthread_mutexattr_destroy (&attr);                          \
      sim_asynch_main_threadid = pthread_self();                  \
      /* Empty list/list end uses the point value (void *)1.      \
         This allows NULL in an entry's a_next pointer to         \
         indicate that the entry is not currently in any list */  \
      sim_asynch_queue = QUEUE_LIST_END;                          \
      } while (0)
#define AIO_CLEANUP                                               \
    do {                                                          \
      pthread_mutex_destroy(&sim_asynch_lock);                    \
      pthread_cond_destroy(&sim_asynch_wake);                     \
      pthread_mutex_destroy(&sim_timer_lock);                     \
      pthread_cond_destroy(&sim_timer_wake);                      \
      pthread_mutex_destroy(&sim_tmxr_poll_lock);                 \
      pthread_cond_destroy(&sim_tmxr_poll_cond);                  \
      } while (0)
#define AIO_ILOCK AIO_LOCK
#define AIO_IUNLOCK AIO_UNLOCK
#define AIO_QUEUE_VAL sim_asynch_queue
#define AIO_QUEUE_SET(newval, oldval) ((sim_asynch_queue = newval),oldval)
#define AIO_UPDATE_QUEUE sim_aio_update_queue ()
#define AIO_ACTIVATE(caller, uptr, event_time)                         \
    if (!pthread_equal ( pthread_self(), sim_asynch_main_threadid )) { \
      sim_debug (SIM_DBG_AIO_QUEUE, sim_dflt_dev, "Queueing Asynch event for %s after %d instructions\n", sim_uname(uptr), event_time);\
      AIO_LOCK;                                                        \
      if (uptr->a_next) {                       /* already queued? */  \
        uptr->a_activate_call = sim_activate_abs;                      \
      } else {                                                         \
        uptr->a_next = sim_asynch_queue;                               \
        uptr->a_event_time = event_time;                               \
        uptr->a_activate_call = (ACTIVATE_API)&caller;                 \
        sim_asynch_queue = uptr;                                       \
      }                                                                \
      if (sim_idle_wait) {                                             \
        if (sim_deb) {  /* only while debug do lock/unlock overhead */ \
          AIO_UNLOCK;                                                  \
          sim_debug (TIMER_DBG_IDLE, &sim_timer_dev, "waking due to event on %s after %d instructions\n", sim_uname(uptr), event_time);\
          AIO_LOCK;                                                    \
          }                                                            \
        pthread_cond_signal (&sim_asynch_wake);                        \
        }                                                              \
      AIO_UNLOCK;                                                      \
      sim_asynch_check = 0;                                            \
      return SCPE_OK;                                                  \
    } else (void)0
#endif /* USE_AIO_INTRINSICS */
#define AIO_VALIDATE(uptr)                                             \
    if (!pthread_equal ( pthread_self(), sim_asynch_main_threadid )) { \
      SIM_SCP_ABORT ("Improper thread context for operation detected");\
      } else (void)0
#else /* !SIM_ASYNCH_IO */
#define AIO_QUEUE_MODE "Asynchronous I/O is not available"
#define AIO_UPDATE_QUEUE
#define AIO_ACTIVATE(caller, uptr, event_time)
#define AIO_VALIDATE(uptr)
#define AIO_INIT
#define AIO_MAIN_THREAD TRUE
#define AIO_LOCK
#define AIO_UNLOCK
#define AIO_CLEANUP
#define AIO_IS_ACTIVE(uptr) FALSE
#define AIO_TLS
#endif /* SIM_ASYNCH_IO */

/* Private SCP only structures */

/* Expect rule */

struct EXPTAB {
    uint8               *match;                         /* match string */
    uint32              size;                           /* match string size */
    char                *match_pattern;                 /* match pattern for format */
    int32               cnt;                            /* proceed count */
    uint32              after;                          /* delay before halting */
    int32               switches;                       /* flags */
#define EXP_TYP_PERSIST         (SWMASK ('P'))      /* rule persists after match, default is once a rule matches, it is removed */
#define EXP_TYP_CLEARALL        (SWMASK ('C'))      /* clear all rules after matching this rule, default is to once a rule matches, it is removed */
#define EXP_TYP_REGEX           (SWMASK ('R'))      /* rule pattern is a regular expression */
#define EXP_TYP_REGEX_I         (SWMASK ('I'))      /* regular expression pattern matching should be case independent */
#define EXP_TYP_TIME            (SWMASK ('T'))      /* halt delay is in microseconds instead of instructions */
#if defined(USE_REGEX)
    pcre                *regex;                         /* compiled regular expression */
    int                 re_nsub;                        /* regular expression sub expression count */
#endif
    char                *act;                           /* action string */
    };

/* Expect Context */

struct EXPECT {
    DEVICE              *dptr;                          /* Device (for Debug) */
    uint32              dbit;                           /* Debugging Bit */
    EXPTAB              *rules;                         /* match rules */
    int32               size;                           /* count of match rules */
    uint8               *buf;                           /* buffer of output data which has produced */
    uint32              buf_ins;                        /* buffer insertion point for the next output data */
    uint32              buf_size;                       /* buffer size */
    uint32              buf_data;                       /* count of data in buffer */
    };

/* Send Context */

struct SEND {
    uint32              delay;                          /* instruction delay between sent data */
#define SEND_DEFAULT_DELAY  1000                        /* default delay instruction count */
    DEVICE              *dptr;                          /* Device (for Debug) */
    uint32              dbit;                           /* Debugging Bit */
    uint32              after;                          /* instruction delay before sending any data */
    double              next_time;                      /* execution time when next data can be sent */
    uint8               *buffer;                        /* buffer */
    size_t              bufsize;                        /* buffer size */
    int32               insoff;                         /* insert offset */
    int32               extoff;                         /* extra offset */
    };

#ifdef  __cplusplus
}
#endif

#endif
