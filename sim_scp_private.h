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

#if defined(_WIN32)
#define dlopen(X,Y)    LoadLibraryA((X))
#define dlsym(X,Y)     GetProcAddress((HINSTANCE)(X),(Y))
#define dlclose(X)     FreeLibrary((X))
#define SIM_DLOPEN_EXTENSION DLL
#else /* !defined(_WIN32) */
#if defined(SIM_HAVE_DLOPEN)
#include <dlfcn.h>
#define SIM_DLOPEN_EXTENSION SIM_HAVE_DLOPEN
#endif
#endif /* defined(_WIN32) */

#if defined(HAVE_PCRE_H)
#include <pcre.h>
#else /* !defined(HAVE_PCRE_H) */
/* Dynamically loaded PCRE support */
#if !defined(PCRE_DYNAMIC_SETUP)
#define PCRE_DYNAMIC_SETUP
typedef void pcre;
typedef void pcre_extra;
#ifndef PCRE_INFO_CAPTURECOUNT
#define PCRE_INFO_CAPTURECOUNT 2
#define PCRE_ERROR_NOMATCH          (-1)
#endif
#ifndef PCRE_NOTBOL
#define PCRE_NOTBOL             0x00000080  /*    E D J */
#endif
#ifndef PCRE_CASELESS
#define PCRE_CASELESS           0x00000001  /* C1       */
#endif
/* Pointers to useful PCRE functions */
extern pcre *(*pcre_compile) (const char *, int, const char **, int *, const unsigned char *);
extern const char *(*pcre_version) (void);
extern void (*pcre_free) (void *);
extern int (*pcre_fullinfo) (const pcre *, const pcre_extra *, int, void *);
extern int (*pcre_exec) (const pcre *, const pcre_extra *, const char *, int, int, int, int *, int);
#endif /* PCRE_DYNAMIC_SETUP */
#endif /* HAVE_PCRE_H */

/* Dynamically loaded PNG support */
#if defined(PNG_H)  /* This symbol has been defined by png.h since png 1.0.7 in 2000 */
#if !defined(PNG_ROUTINE)
#if PNG_LIBPNG_VER < 10600
#define png_const_structrp png_structp
#define png_structrp png_structp
#define png_const_inforp png_infop
#define png_inforp png_infop
#define png_const_colorp png_colorp
#define png_const_bytep png_bytep
#undef PNG_SETJMP_SUPPORTED
#endif
/* Pointers to useful PNG functions */
#if defined(_WIN32) || defined(ALL_DEPENDENCIES)     /* This would be appropriate anytime libpng is specifically listed at link time */
#define PNG_ROUTINE(_type, _name, _args) _type (*p_##_name) _args = &_name;
#else
#define PNG_ROUTINE(_type, _name, _args) _type (*p_##_name) _args;
#endif
#else /* !defined(PNG_ROUTINE) */
#undef PNG_ROUTINE
static struct PNG_Entry {
    const char *entry_name;
    void **entry_pointer;
    } libpng_entries[] = {
#define DEFINING_PNG_ARRAY 1
#define PNG_ROUTINE(_type, _name, _args) {#_name, (void **)&p_##_name},
#endif
/* Return the user pointer associated with the I/O functions */
PNG_ROUTINE(png_voidp, png_get_io_ptr, (png_const_structrp png_ptr))
/* Allocate and initialize png_ptr struct for reading, and any other memory. */
PNG_ROUTINE(png_structp, png_create_read_struct,
    (png_const_charp user_png_ver, png_voidp error_ptr,
    png_error_ptr error_fn, png_error_ptr warn_fn))
/* Allocate and initialize png_ptr struct for writing, and any other memory */
PNG_ROUTINE(png_structp, png_create_write_struct,
    (png_const_charp user_png_ver, png_voidp error_ptr, png_error_ptr error_fn, png_error_ptr warn_fn))
/* Allocate and initialize the info structure */
PNG_ROUTINE(png_infop, png_create_info_struct, (png_const_structrp png_ptr))
/* Free any memory associated with the png_struct and the png_info_structs */
PNG_ROUTINE(void, png_destroy_read_struct, (png_structpp png_ptr_ptr,
    png_infopp info_ptr_ptr, png_infopp end_info_ptr_ptr))
/* Free any memory associated with the png_struct and the png_info_structs */
PNG_ROUTINE(void, png_destroy_write_struct, (png_structpp png_ptr_ptr, png_infopp info_ptr_ptr))
/* Replace the default data output functions with a user supplied one(s).
 * If buffered output is not used, then output_flush_fn can be set to NULL.
 * If PNG_WRITE_FLUSH_SUPPORTED is not defined at libpng compile time
 * output_flush_fn will be ignored (and thus can be NULL).
 * It is probably a mistake to use NULL for output_flush_fn if
 * write_data_fn is not also NULL unless you have built libpng with
 * PNG_WRITE_FLUSH_SUPPORTED undefined, because in this case libpng's
 * default flush function, which uses the standard *FILE structure, will
 * be used.
 */
PNG_ROUTINE(void, png_set_write_fn, (png_structrp png_ptr, png_voidp io_ptr,
    png_rw_ptr write_data_fn, png_flush_ptr output_flush_fn))
PNG_ROUTINE(void, png_set_PLTE, (png_structrp png_ptr,
    png_inforp info_ptr, png_const_colorp palette, int num_palette))
PNG_ROUTINE(void, png_set_IHDR, (png_const_structrp png_ptr,
    png_inforp info_ptr, png_uint_32 width, png_uint_32 height, int bit_depth,
    int color_type, int interlace_method, int compression_method,
    int filter_method))
/* Use blue, green, red order for pixels. */
PNG_ROUTINE(void, png_set_bgr, (png_structrp png_ptr))
PNG_ROUTINE(void, png_write_info,
    (png_structrp png_ptr, png_const_inforp info_ptr))
/* Write a row of image data */
PNG_ROUTINE(void, png_write_row, (png_structrp png_ptr,
    png_const_bytep row))
/* Write the image data */
PNG_ROUTINE(void, png_write_image, (png_structrp png_ptr, png_bytepp image))
/* Write the end of the PNG file. */
PNG_ROUTINE(void, png_write_end, (png_structrp png_ptr,
    png_inforp info_ptr))
#if defined(PNG_SETJMP_SUPPORTED)
/* This function returns the jmp_buf built in to *png_ptr.  It must be
 * supplied with an appropriate 'longjmp' function to use on that jmp_buf
 * unless the default error function is overridden in which case NULL is
 * acceptable.  The size of the jmp_buf is checked against the actual size
 * allocated by the library - the call will return NULL on a mismatch
 * indicating an ABI mismatch.
 */
PNG_ROUTINE(jmp_buf*, png_set_longjmp_fn, (png_structrp png_ptr,
    png_longjmp_ptr longjmp_fn, size_t jmp_buf_size))
#endif /* PNG_SETJMP_SUPPORTED */
PNG_ROUTINE(png_const_charp, png_get_libpng_ver,
    (png_const_structrp png_ptr))
#if defined(ZLIB_VERSION)
PNG_ROUTINE(png_const_charp, zlibVersion,
    (void))
#endif
#if defined(DEFINING_PNG_ARRAY)
    {0},
    };
#undef DEFINING_PNG_ARRAY
#else /* !defined(DEFINING_PNG_ARRAY) */
#undef SIM_SCP_PRIVATE_H_
#include "sim_scp_private.h" /* recurse to generate libpng_entries array */
#endif /* !defined(DEFINING_PNG_ARRAY) */
#endif /* PNG_H */

/* Asynch/Threaded I/O support */

#if defined (SIM_ASYNCH_IO)
#include <pthread.h>

#define SIM_ASYNCH_CLOCKS 1

extern pthread_mutex_t sim_asynch_lock;
extern pthread_cond_t sim_asynch_wake;
extern pthread_mutex_t sim_timer_lock;
extern pthread_cond_t sim_timer_wake;
extern t_bool sim_timer_event_canceled;
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
      } while (0)
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
#if defined(__IA64) || defined(__ia64)
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
#ifdef _WIN32
#elif defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4) || defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8)
#define InterlockedCompareExchangePointerAcquire(Destination, Exchange, Comparand) __sync_val_compare_and_swap(Destination, Comparand, Exchange)
#define InterlockedCompareExchangePointerRelease(Destination, Exchange, Comparand) __sync_val_compare_and_swap(Destination, Comparand, Exchange)
#elif defined(__DECC_VER)
#define InterlockedCompareExchangePointerAcquire(Destination, Exchange, Comparand) _InterlockedCompareExchange64_acq(Destination, Exchange, Comparand)
#define InterlockedCompareExchangePointerRelease(Destination, Exchange, Comparand) _InterlockedCompareExchange64_rel(Destination, Exchange, Comparand)
#else
#error "Implementation of function InterlockedCompareExchangePointer() is needed to build with USE_AIO_INTRINSICS"
#endif
#define AIO_ILOCK AIO_LOCK
#define AIO_IUNLOCK AIO_UNLOCK
#if defined(_M_IX86) || defined(_M_X64)
#define AIO_QUEUE_VAL ((UNIT *)sim_asynch_queue)
#else /* !defined(_M_IX86) || defined(_M_X64) */
#define AIO_QUEUE_VAL (UNIT *)(InterlockedCompareExchangePointerAcquire((void * volatile *)&sim_asynch_queue, (void *)sim_asynch_queue, NULL))
#endif /* defined(_M_IX86) || defined(_M_X64) */
#define AIO_QUEUE_SET(newval, oldval) ((UNIT *)(InterlockedCompareExchangePointerRelease((void * volatile *)&sim_asynch_queue, (void *)newval, oldval)))
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
#define AIO_ILOCK AIO_LOCK
#define AIO_IUNLOCK AIO_UNLOCK
#define AIO_QUEUE_VAL sim_asynch_queue
#define AIO_QUEUE_SET(newval, oldval) ((sim_asynch_queue = newval),oldval)
#define AIO_UPDATE_QUEUE sim_aio_update_queue ()
#define AIO_ACTIVATE(caller, uptr, event_time)                         \
    if (!pthread_equal ( pthread_self(), sim_asynch_main_threadid )) { \
      sim_debug (SIM_DBG_AIO_QUEUE, sim_dflt_dev, "Lock Based Queueing Asynch event for %s after %d %s\n", sim_uname(uptr), event_time, sim_vm_interval_units);\
      AIO_LOCK;                                                        \
      if (uptr->a_next) {                       /* already queued? */  \
        uptr->a_activate_call = sim_activate_abs;                      \
        uptr->a_event_time = MIN (uptr->a_event_time, event_time);     \
      } else {                                                         \
        uptr->a_next = sim_asynch_queue;                               \
        uptr->a_event_time = event_time;                               \
        uptr->a_activate_call = (ACTIVATE_API)&caller;                 \
        sim_asynch_queue = uptr;                                       \
      }                                                                \
      if (sim_idle_wait) {                                             \
        if (sim_deb) {  /* only while debug do lock/unlock overhead */ \
          AIO_UNLOCK;                                                  \
          sim_debug (TIMER_DBG_IDLE, &sim_timer_dev, "wakeup from idle due to async event on %s after %d %s\n", sim_uname(uptr), event_time, sim_vm_interval_units);\
          AIO_LOCK;                                                    \
          }                                                            \
        pthread_cond_signal (&sim_asynch_wake);                        \
        }                                                              \
      AIO_UNLOCK;                                                      \
      sim_asynch_check = 0;     /* try to force check */               \
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

#if !defined(SIM_SCP_PRIVATE_DONT_REPEAT)
#define SIM_SCP_PRIVATE_DONT_REPEAT

/* Internal SCP declarations */
extern DEVICE sim_scp_dev;
#define SIM_DBG_INIT        0x00200000      /* initialization activities */
#define SIM_DBG_SHUTDOWN    0x00100000      /* shutdown activities */

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
    pcre                *regex;                         /* compiled regular expression */
    int                 re_nsub;                        /* regular expression sub expression count */
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

/* Private SCP only APIs */

t_stat _sim_os_putchar (int32 out);
t_bool _sim_running_as_root (void);


#endif /* defined(SIM_SCP_PRIVATE_DONT_REPEAT) */

#ifdef  __cplusplus
}
#endif

#endif
