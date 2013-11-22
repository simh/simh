/* sim_defs.h: simulator definitions

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

   05-Jan-11    MP      Added Asynch I/O support
   18-Jan-11    MP      Added log file reference count support
   21-Jul-08    RMS     Removed inlining support
   28-May-08    RMS     Added inlining support
   28-Jun-07    RMS     Added IA64 VMS support (from Norm Lastovica)
   18-Jun-07    RMS     Added UNIT_IDLE flag
   18-Mar-07    RMS     Added UNIT_TEXT flag
   07-Mar-07    JDB     Added DEBUG_PRJ macro
   18-Oct-06    RMS     Added limit check for clock synchronized keyboard waits
   13-Jul-06    RMS     Guarantee CBUFSIZE is at least 256
   07-Jan-06    RMS     Added support for breakpoint spaces
                        Added REG_FIT flag
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   11-Mar-05    RMS     Moved 64b data type definitions outside USE_INT64
   07-Feb-05    RMS     Added assertion fail stop
   05-Nov-04    RMS     Added support for SHOW opt=val
   20-Oct-04    RMS     Converted all base types to typedefs
   21-Sep-04    RMS     Added switch to flag stop message printout
   06-Feb-04    RMS     Moved device and unit user flags fields (V3.2)
                RMS     Added REG_VMAD
   29-Dec-03    RMS     Added output stall status
   15-Jun-03    RMS     Added register flag REG_VMIO
   23-Apr-03    RMS     Revised for 32b/64b t_addr
   14-Mar-03    RMS     Lengthened default serial output wait
   31-Mar-03    RMS     Added u5, u6 fields
   18-Mar-03    RMS     Added logical name support
                        Moved magtape definitions to sim_tape.h
                        Moved breakpoint definitions from scp.c
   03-Mar-03    RMS     Added sim_fsize
   08-Feb-03    RMS     Changed sim_os_sleep to void, added match_ext
   05-Jan-03    RMS     Added hidden switch definitions, device dyn memory support,
                        parameters for function pointers, case sensitive SET support
   22-Dec-02    RMS     Added break flag
   08-Oct-02    RMS     Increased simulator error code space
                        Added Telnet errors
                        Added end of medium support
                        Added help messages to CTAB
                        Added flag and context fields to DEVICE
                        Added restore flag masks
                        Revised 64b definitions
   02-May-02    RMS     Removed log status codes
   22-Apr-02    RMS     Added magtape record length error
   30-Dec-01    RMS     Generalized timer package, added circular arrays
   07-Dec-01    RMS     Added breakpoint package
   01-Dec-01    RMS     Added read-only unit support, extended SET/SHOW features,
                        improved error messages
   24-Nov-01    RMS     Added unit-based registers
   27-Sep-01    RMS     Added queue count prototype
   17-Sep-01    RMS     Removed multiple console support
   07-Sep-01    RMS     Removed conditional externs on function prototypes
   31-Aug-01    RMS     Changed int64 to t_int64 for Windoze
   17-Jul-01    RMS     Added additional function prototypes
   27-May-01    RMS     Added multiple console support
   15-May-01    RMS     Increased string buffer size
   25-Feb-01    RMS     Revisions for V2.6
   15-Oct-00    RMS     Editorial revisions for V2.5
   11-Jul-99    RMS     Added unsigned int data types
   14-Apr-99    RMS     Converted t_addr to unsigned
   04-Oct-98    RMS     Additional definitions for V2.4

   The interface between the simulator control package (SCP) and the
   simulator consists of the following routines and data structures

        sim_name                simulator name string
        sim_devices[]           array of pointers to simulated devices
        sim_PC                  pointer to saved PC register descriptor
        sim_interval            simulator interval to next event
        sim_stop_messages[]     array of pointers to stop messages
        sim_instr()             instruction execution routine
        sim_load()              binary loader routine
        sim_emax                maximum number of words in an instruction

   In addition, the simulator must supply routines to print and parse
   architecture specific formats

        print_sym               print symbolic output
        parse_sym               parse symbolic input
*/

#ifndef SIM_DEFS_H_
#define SIM_DEFS_H_    0

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#ifdef _WIN32
#include <winsock2.h>
#undef PACKED                       /* avoid macro name collision */
#undef ERROR                        /* avoid macro name collision */
#undef MEM_MAPPED                   /* avoid macro name collision */
#include <process.h>
#endif

/* avoid macro names collisions */
#ifdef MAX
#undef MAX
#endif
#ifdef MIN
#undef MIN
#endif
#ifdef PMASK
#undef PMASK
#endif
#ifdef RS
#undef RS
#endif
#ifdef PAGESIZE
#undef PAGESIZE
#endif


#ifndef TRUE
#define TRUE            1
#define FALSE           0
#endif

/* Length specific integer declarations */

#if defined (VMS)
#include <ints.h>
#else
typedef signed char     int8;
typedef signed short    int16;
typedef signed int      int32;
typedef unsigned char   uint8;
typedef unsigned short  uint16;
typedef unsigned int    uint32;
#endif
typedef int             t_stat;                         /* status */
typedef int             t_bool;                         /* boolean */

/* 64b integers */

#if defined (__GNUC__)                                  /* GCC */
typedef signed long long        t_int64;
typedef unsigned long long      t_uint64;
#elif defined (_WIN32)                                  /* Windows */
typedef signed __int64          t_int64;
typedef unsigned __int64        t_uint64;
#elif (defined (__ALPHA) || defined (__ia64)) && defined (VMS) /* 64b VMS */
typedef signed __int64          t_int64;
typedef unsigned __int64        t_uint64;
#elif defined (__ALPHA) && defined (__unix__)           /* Alpha UNIX */
typedef signed long             t_int64;
typedef unsigned long           t_uint64;
#else                                                   /* default */
#define t_int64                 signed long long
#define t_uint64                unsigned long long
#endif                                                  /* end 64b */
#ifndef INT64_C
#define INT64_C(x)      x ## LL
#endif

#if defined (USE_INT64)                                 /* 64b data */
typedef t_int64         t_svalue;                       /* signed value */
typedef t_uint64        t_value;                        /* value */
#else                                                   /* 32b data */
typedef int32           t_svalue;
typedef uint32          t_value;
#endif                                                  /* end 64b data */

#if defined (USE_INT64) && defined (USE_ADDR64)         /* 64b address */
typedef t_uint64        t_addr;
#define T_ADDR_W        64
#else                                                   /* 32b address */
typedef uint32          t_addr;
#define T_ADDR_W        32
#endif                                                  /* end 64b address */

#if defined (_WIN32)
#define vsnprintf _vsnprintf
#endif
#if defined (__DECC) && defined (__VMS) && (defined (__VAX) || (__CRTL_VER <= 70311000))
#define NO_vsnprintf
#endif
#if defined( NO_vsnprintf)
#define STACKBUFSIZE 16384
#else
#define STACKBUFSIZE 2048
#endif

#if defined (_WIN32) /* Actually, a GCC issue */
#define LL_FMT "I64"
#else
#define LL_FMT "ll"
#endif

#if defined (VMS) && (defined (__ia64) || defined (__ALPHA))
#define HAVE_GLOB
#endif

/* Stubs for inlining */

#define SIM_INLINE

/* System independent definitions */

#define FLIP_SIZE       (1 << 16)                       /* flip buf size */
#if !defined (PATH_MAX)                                 /* usually in limits */
#define PATH_MAX        512
#endif
#if (PATH_MAX >= 128)
#define CBUFSIZE        (128 + PATH_MAX)                /* string buf size */
#else
#define CBUFSIZE        256
#endif

/* Breakpoint spaces definitions */

#define SIM_BKPT_N_SPC  64                              /* max number spaces */
#define SIM_BKPT_V_SPC  26                              /* location in arg */

/* Extended switch definitions (bits >= 26) */

#define SIM_SW_HIDE     (1u << 26)                      /* enable hiding */
#define SIM_SW_REST     (1u << 27)                      /* attach/restore */
#define SIM_SW_REG      (1u << 28)                      /* register value */
#define SIM_SW_STOP     (1u << 29)                      /* stop message */

/* Simulator status codes

   0                    ok
   1 - (SCPE_BASE - 1)  simulator specific
   SCPE_BASE - n        general
*/

#define SCPE_OK         0                               /* normal return */
#define SCPE_BASE       64                              /* base for messages */
#define SCPE_NXM        (SCPE_BASE + 0)                 /* nxm */
#define SCPE_UNATT      (SCPE_BASE + 1)                 /* no file */
#define SCPE_IOERR      (SCPE_BASE + 2)                 /* I/O error */
#define SCPE_CSUM       (SCPE_BASE + 3)                 /* loader cksum */
#define SCPE_FMT        (SCPE_BASE + 4)                 /* loader format */
#define SCPE_NOATT      (SCPE_BASE + 5)                 /* not attachable */
#define SCPE_OPENERR    (SCPE_BASE + 6)                 /* open error */
#define SCPE_MEM        (SCPE_BASE + 7)                 /* alloc error */
#define SCPE_ARG        (SCPE_BASE + 8)                 /* argument error */
#define SCPE_STEP       (SCPE_BASE + 9)                 /* step expired */
#define SCPE_UNK        (SCPE_BASE + 10)                /* unknown command */
#define SCPE_RO         (SCPE_BASE + 11)                /* read only */
#define SCPE_INCOMP     (SCPE_BASE + 12)                /* incomplete */
#define SCPE_STOP       (SCPE_BASE + 13)                /* sim stopped */
#define SCPE_EXIT       (SCPE_BASE + 14)                /* sim exit */
#define SCPE_TTIERR     (SCPE_BASE + 15)                /* console tti err */
#define SCPE_TTOERR     (SCPE_BASE + 16)                /* console tto err */
#define SCPE_EOF        (SCPE_BASE + 17)                /* end of file */
#define SCPE_REL        (SCPE_BASE + 18)                /* relocation error */
#define SCPE_NOPARAM    (SCPE_BASE + 19)                /* no parameters */
#define SCPE_ALATT      (SCPE_BASE + 20)                /* already attached */
#define SCPE_TIMER      (SCPE_BASE + 21)                /* hwre timer err */
#define SCPE_SIGERR     (SCPE_BASE + 22)                /* signal err */
#define SCPE_TTYERR     (SCPE_BASE + 23)                /* tty setup err */
#define SCPE_SUB        (SCPE_BASE + 24)                /* subscript err */
#define SCPE_NOFNC      (SCPE_BASE + 25)                /* func not imp */
#define SCPE_UDIS       (SCPE_BASE + 26)                /* unit disabled */
#define SCPE_NORO       (SCPE_BASE + 27)                /* rd only not ok */
#define SCPE_INVSW      (SCPE_BASE + 28)                /* invalid switch */
#define SCPE_MISVAL     (SCPE_BASE + 29)                /* missing value */
#define SCPE_2FARG      (SCPE_BASE + 30)                /* too few arguments */
#define SCPE_2MARG      (SCPE_BASE + 31)                /* too many arguments */
#define SCPE_NXDEV      (SCPE_BASE + 32)                /* nx device */
#define SCPE_NXUN       (SCPE_BASE + 33)                /* nx unit */
#define SCPE_NXREG      (SCPE_BASE + 34)                /* nx register */
#define SCPE_NXPAR      (SCPE_BASE + 35)                /* nx parameter */
#define SCPE_NEST       (SCPE_BASE + 36)                /* nested DO */
#define SCPE_IERR       (SCPE_BASE + 37)                /* internal error */
#define SCPE_MTRLNT     (SCPE_BASE + 38)                /* tape rec lnt error */
#define SCPE_LOST       (SCPE_BASE + 39)                /* Telnet conn lost */
#define SCPE_TTMO       (SCPE_BASE + 40)                /* Telnet conn timeout */
#define SCPE_STALL      (SCPE_BASE + 41)                /* Telnet conn stall */
#define SCPE_AFAIL      (SCPE_BASE + 42)                /* assert failed */
#define SCPE_INVREM     (SCPE_BASE + 43)                /* invalid remote console command */

#define SCPE_MAX_ERR    (SCPE_BASE + 44)                /* Maximum SCPE Error Value */
#define SCPE_KFLAG      0x1000                          /* tti data flag */
#define SCPE_BREAK      0x2000                          /* tti break flag */
#define SCPE_NOMESSAGE  0x10000000                      /* message display supression flag */
#define SCPE_BARE_STATUS(stat) ((stat) & ~(SCPE_NOMESSAGE|SCPE_KFLAG|SCPE_BREAK))

/* Print value format codes */

#define PV_RZRO         0                               /* right, zero fill */
#define PV_RSPC         1                               /* right, space fill */
#define PV_RCOMMA       2                               /* right, space fill. Comma separate every 3 */
#define PV_LEFT         3                               /* left justify */

/* Default timing parameters */

#define KBD_POLL_WAIT   5000                            /* keyboard poll */
#define KBD_MAX_WAIT    500000
#define SERIAL_IN_WAIT  100                             /* serial in time */
#define SERIAL_OUT_WAIT 100                             /* serial output */
#define NOQUEUE_WAIT    1000000                         /* min check time */
#define KBD_LIM_WAIT(x) (((x) > KBD_MAX_WAIT)? KBD_MAX_WAIT: (x))
#define KBD_WAIT(w,s)   ((w)? w: KBD_LIM_WAIT (s))

/* Convert switch letter to bit mask */

#define SWMASK(x) (1u << (((int) (x)) - ((int) 'A')))

/* String match - at least one character required */

#define MATCH_CMD(ptr,cmd) ((NULL == (ptr)) || (!*(ptr)) || strncmp ((ptr), (cmd), strlen (ptr)))

/* End of Linked List/Queue value                           */
/* Chosen for 2 reasons:                                    */
/*     1 - to not be NULL, this allowing the NULL value to  */
/*         indicate inclusion on a list                     */
/* and                                                      */
/*     2 - to not be a valid/possible pointer (alignment)   */
#define QUEUE_LIST_END ((UNIT *)1)

/* Device data structure */

struct sim_device {
    char                *name;                          /* name */
    struct sim_unit     *units;                         /* units */
    struct sim_reg      *registers;                     /* registers */
    struct sim_mtab     *modifiers;                     /* modifiers */
    uint32              numunits;                       /* #units */
    uint32              aradix;                         /* address radix */
    uint32              awidth;                         /* address width */
    uint32              aincr;                          /* addr increment */
    uint32              dradix;                         /* data radix */
    uint32              dwidth;                         /* data width */
    t_stat              (*examine)(t_value *v, t_addr a, struct sim_unit *up,
                            int32 sw);                  /* examine routine */
    t_stat              (*deposit)(t_value v, t_addr a, struct sim_unit *up,
                            int32 sw);                  /* deposit routine */
    t_stat              (*reset)(struct sim_device *dp);/* reset routine */
    t_stat              (*boot)(int32 u, struct sim_device *dp);
                                                        /* boot routine */
    t_stat              (*attach)(struct sim_unit *up, char *cp);
                                                        /* attach routine */
    t_stat              (*detach)(struct sim_unit *up); /* detach routine */
    void                *ctxt;                          /* context */
    uint32              flags;                          /* flags */
    uint32              dctrl;                          /* debug control */
    struct sim_debtab   *debflags;                      /* debug flags */
    t_stat              (*msize)(struct sim_unit *up, int32 v, char *cp, void *dp);
                                                        /* mem size routine */
    char                *lname;                         /* logical name */
    t_stat              (*help)(FILE *st, struct sim_device *dptr,
                            struct sim_unit *uptr, int32 flag, char *cptr); 
                                                        /* help */
    t_stat              (*attach_help)(FILE *st, struct sim_device *dptr,
                            struct sim_unit *uptr, int32 flag, char *cptr);
                                                        /* attach help */
    void *help_ctx;                                     /* Context available to help routines */
    char                *(*description)(struct sim_device *dptr);
                                                        /* Device Description */
    };

/* Device flags */

#define DEV_V_DIS       0                               /* dev disabled */
#define DEV_V_DISABLE   1                               /* dev disable-able */
#define DEV_V_DYNM      2                               /* mem size dynamic */
#define DEV_V_DEBUG     3                               /* debug capability */
#define DEV_V_TYPE      4                               /* Attach type */
#define DEV_S_TYPE      3                               /* Width of Type Field */
#define DEV_V_SECTORS   7                               /* Unit Capacity is in 512byte sectors */
#define DEV_V_DONTAUTO  8                               /* Do not auto detach already attached units */
#define DEV_V_FLATHELP  9                               /* Use traditional (unstructured) help */
#define DEV_V_UF_31     12                              /* user flags, V3.1 */
#define DEV_V_UF        16                              /* user flags */
#define DEV_V_RSV       31                              /* reserved */

#define DEV_DIS         (1 << DEV_V_DIS)                /* device is currently disabled */
#define DEV_DISABLE     (1 << DEV_V_DISABLE)            /* device can be set enabled or disabled */
#define DEV_DYNM        (1 << DEV_V_DYNM)               /* device requires call on msize routine to change memory size */
#define DEV_DEBUG       (1 << DEV_V_DEBUG)              /* device supports SET DEBUG command */
#define DEV_SECTORS     (1 << DEV_V_SECTORS)            /* capacity is 512 byte sectors */
#define DEV_DONTAUTO    (1 << DEV_V_DONTAUTO)           /* Do not auto detach already attached units */
#define DEV_FLATHELP    (1 << DEV_V_FLATHELP)           /* Use traditional (unstructured) help */
#define DEV_NET         0                               /* Deprecated - meaningless */


#define DEV_TYPEMASK    (((1 << DEV_S_TYPE) - 1) << DEV_V_TYPE)
#define DEV_DISK        (1 << DEV_V_TYPE)               /* sim_disk Attach */
#define DEV_TAPE        (2 << DEV_V_TYPE)               /* sim_tape Attach */
#define DEV_MUX         (3 << DEV_V_TYPE)               /* sim_tmxr Attach */
#define DEV_ETHER       (4 << DEV_V_TYPE)               /* Ethernet Device */
#define DEV_DISPLAY     (5 << DEV_V_TYPE)               /* Display Device */
#define DEV_TYPE(dptr)  ((dptr)->flags & DEV_TYPEMASK)

#define DEV_UFMASK_31   (((1u << DEV_V_RSV) - 1) & ~((1u << DEV_V_UF_31) - 1))
#define DEV_UFMASK      (((1u << DEV_V_RSV) - 1) & ~((1u << DEV_V_UF) - 1))
#define DEV_RFLAGS      (DEV_UFMASK|DEV_DIS)            /* restored flags */

/* Unit data structure

   Parts of the unit structure are device specific, that is, they are
   not referenced by the simulator control package and can be freely
   used by device simulators.  Fields starting with 'buf', and flags
   starting with 'UF', are device specific.  The definitions given here
   are for a typical sequential device.
*/

struct sim_unit {
    struct sim_unit     *next;                          /* next active */
    t_stat              (*action)(struct sim_unit *up); /* action routine */
    char                *filename;                      /* open file name */
    FILE                *fileref;                       /* file reference */
    void                *filebuf;                       /* memory buffer */
    uint32              hwmark;                         /* high water mark */
    int32               time;                           /* time out */
    uint32              flags;                          /* flags */
    uint32              dynflags;                       /* dynamic flags */
    t_addr              capac;                          /* capacity */
    t_addr              pos;                            /* file position */
    void                (*io_flush)(struct sim_unit *up);/* io flush routine */
    uint32              iostarttime;                    /* I/O start time */
    int32               buf;                            /* buffer */
    int32               wait;                           /* wait */
    int32               u3;                             /* device specific */
    int32               u4;                             /* device specific */
    int32               u5;                             /* device specific */
    int32               u6;                             /* device specific */
    void                *up7;                           /* device specific */
    void                *up8;                           /* device specific */
#ifdef SIM_ASYNCH_IO
    void                (*a_check_completion)(struct sim_unit *);
    t_bool              (*a_is_active)(struct sim_unit *);
    void                (*a_cancel)(struct sim_unit *);
    struct sim_unit     *a_next;                        /* next asynch active */
    int32               a_event_time;
    t_stat              (*a_activate_call)(struct sim_unit *, int32);
    /* Asynchronous Polling control */
    /* These fields should only be referenced when holding the sim_tmxr_poll_lock */
    t_bool              a_polling_now;                  /* polling active flag */
    int32               a_poll_waiter_count;            /* count of polling threads */
                                                        /* waiting for this unit */
    /* Asynchronous Timer control */
    double              a_due_time;                     /* due time for timer event */
    double              a_skew;                         /* accumulated skew being corrected */
    double              a_last_fired_time;              /* time last event fired */
    int32               a_usec_delay;                   /* time delay for timer event */
#endif
    };

/* Unit flags */

#define UNIT_V_UF_31    12              /* dev spec, V3.1 */
#define UNIT_V_UF       16              /* device specific */
#define UNIT_V_RSV      31              /* reserved!! */

#define UNIT_ATTABLE    0000001         /* attachable */
#define UNIT_RO         0000002         /* read only */
#define UNIT_FIX        0000004         /* fixed capacity */
#define UNIT_SEQ        0000010         /* sequential */
#define UNIT_ATT        0000020         /* attached */
#define UNIT_BINK       0000040         /* K = power of 2 */
#define UNIT_BUFABLE    0000100         /* bufferable */
#define UNIT_MUSTBUF    0000200         /* must buffer */
#define UNIT_BUF        0000400         /* buffered */
#define UNIT_ROABLE     0001000         /* read only ok */
#define UNIT_DISABLE    0002000         /* disable-able */
#define UNIT_DIS        0004000         /* disabled */
#define UNIT_IDLE       0040000         /* idle eligible */

/* Unused/meaningless flags */
#define UNIT_TEXT       0000000         /* text mode - no effect */

#define UNIT_UFMASK_31  (((1u << UNIT_V_RSV) - 1) & ~((1u << UNIT_V_UF_31) - 1))
#define UNIT_UFMASK     (((1u << UNIT_V_RSV) - 1) & ~((1u << UNIT_V_UF) - 1))
#define UNIT_RFLAGS     (UNIT_UFMASK|UNIT_DIS)          /* restored flags */

/* Unit dynamic flags (dynflags) */

/* These flags are only set dynamically */

#define UNIT_ATTMULT    0000001         /* Allow multiple attach commands */
#define UNIT_TM_POLL    0000002         /* TMXR Polling unit */
#define UNIT_NO_FIO     0000004         /* fileref is NOT a FILE * */

struct sim_bitfield {
    char            *name;                              /* field name */
    uint32          offset;                             /* starting bit */
    uint32          width;                              /* width */
    const char      **valuenames;                       /* map of values to strings */
    const char      *format;                            /* value format string */
    };

/* Register data structure */

struct sim_reg {
    char                *name;                          /* name */
    void                *loc;                           /* location */
    uint32              radix;                          /* radix */
    uint32              width;                          /* width */
    uint32              offset;                         /* starting bit */
    uint32              depth;                          /* save depth */
    char                *desc;                          /* description */
    struct sim_bitfield *fields;                        /* bit fields */
    uint32              flags;                          /* flags */
    uint32              qptr;                           /* circ q ptr */
    };

#define REG_FMT         00003                           /* see PV_x */
#define REG_RO          00004                           /* read only */
#define REG_HIDDEN      00010                           /* hidden */
#define REG_NZ          00020                           /* must be non-zero */
#define REG_UNIT        00040                           /* in unit struct */
#define REG_CIRC        00100                           /* circular array */
#define REG_VMIO        00200                           /* use VM data print/parse */
#define REG_VMAD        00400                           /* use VM addr print/parse */
#define REG_FIT         01000                           /* fit access to size */
#define REG_HRO         (REG_RO | REG_HIDDEN)           /* hidden, read only */

/* Command tables, base and alternate formats */

struct sim_ctab {
    char                *name;                          /* name */
    t_stat              (*action)(int32 flag, char *cptr);
                                                        /* action routine */
    int32               arg;                            /* argument */
    char                *help;                          /* help string */
    void                (*message)(const char *unechoed_cmdline, t_stat stat);
                                                        /* message printing routine */
    };

struct sim_c1tab {
    char                *name;                          /* name */
    t_stat              (*action)(struct sim_device *dptr, struct sim_unit *uptr,
                            int32 flag, char *cptr);    /* action routine */
    int32               arg;                            /* argument */
    char                *help;                          /* help string */
    };

struct sim_shtab {
    char                *name;                          /* name */
    t_stat              (*action)(FILE *st, struct sim_device *dptr,
                            struct sim_unit *uptr, int32 flag, char *cptr);
    int32               arg;                            /* argument */
    char                *help;                          /* help string */
    };

/* Modifier table - only extended entries have disp, reg, or flags */

struct sim_mtab {
    uint32              mask;                           /* mask */
    uint32              match;                          /* match */
    char                *pstring;                       /* print string */
    char                *mstring;                       /* match string */
    t_stat              (*valid)(struct sim_unit *up, int32 v, char *cp, void *dp);
                                                        /* validation routine */
    t_stat              (*disp)(FILE *st, struct sim_unit *up, int32 v, void *dp);
                                                        /* display routine */
    void                *desc;                          /* value descriptor */
                                                        /* REG * if MTAB_VAL */
                                                        /* int * if not */
    char                *help;                          /* help string */
    };


/* mtab mask flag bits */
/* NOTE: MTAB_VALR and MTAB_VALO are only used to display help */
#define MTAB_XTD        (1u << UNIT_V_RSV)              /* ext entry flag */
#define MTAB_VDV        (0001 | MTAB_XTD)               /* valid for dev */
#define MTAB_VUN        (0002 | MTAB_XTD)               /* valid for unit */
#define MTAB_VALR       (0004 | MTAB_XTD)               /* takes a value (required) */
#define MTAB_VALO       (0010 | MTAB_XTD)               /* takes a value (optional) */
#define MTAB_NMO        (0020 | MTAB_XTD)               /* only if named */
#define MTAB_NC         (0040 | MTAB_XTD)               /* no UC conversion */
#define MTAB_QUOTE      (0100 | MTAB_XTD)               /* quoted string */
#define MTAB_SHP        (0200 | MTAB_XTD)               /* show takes parameter */
#define MODMASK(mptr,flag) (((mptr)->mask & (uint32)(flag)) == (uint32)(flag))/* flag mask test */

/* Search table */

struct sim_schtab {
    int32               logic;                          /* logical operator */
    int32               boolop;                         /* boolean operator */
    t_value             mask;                           /* mask for logical */
    t_value             comp;                           /* comparison for boolean */
    };

/* Breakpoint table */

struct sim_brktab {
    t_addr              addr;                           /* address */
    int32               typ;                            /* mask of types */
    int32               cnt;                            /* proceed count */
    char                *act;                           /* action string */
    };

/* Debug table */

struct sim_debtab {
    char                *name;                          /* control name */
    uint32              mask;                           /* control bit */
    };

#define DEBUG_PRS(d)    (sim_deb && d.dctrl)
#define DEBUG_PRD(d)    (sim_deb && d->dctrl)
#define DEBUG_PRI(d,m)  (sim_deb && (d.dctrl & (m)))
#define DEBUG_PRJ(d,m)  (sim_deb && (d->dctrl & (m)))

#define SIM_DBG_EVENT       0x10000
#define SIM_DBG_ACTIVATE    0x20000
#define SIM_DBG_AIO_QUEUE   0x40000

/* File Reference */
struct sim_fileref {
    char                name[CBUFSIZE];                 /* file name */
    FILE                *file;                          /* file handle */
    int32               refcount;                       /* reference count */
    };

/* The following macros define structure contents */

#define UDATA(act,fl,cap) NULL,act,NULL,NULL,NULL,0,0,(fl),0,(cap),0,NULL,0,0

#if defined (__STDC__) || defined (_WIN32)
/* Right Justified Octal Register Data */
#define ORDATA(nm,loc,wd) #nm, &(loc), 8, (wd), 0, 1, NULL, NULL
/* Right Justified Decimal Register Data */
#define DRDATA(nm,loc,wd) #nm, &(loc), 10, (wd), 0, 1, NULL, NULL
/* Right Justified Hexadecimal Register Data */
#define HRDATA(nm,loc,wd) #nm, &(loc), 16, (wd), 0, 1, NULL, NULL
/* One-bit binary flag at an arbitrary offset in a 32-bit word Register */
#define FLDATA(nm,loc,pos) #nm, &(loc), 2, 1, (pos), 1, NULL, NULL
/* Arbitrary location and Radix Register */
#define GRDATA(nm,loc,rdx,wd,pos) #nm, &(loc), (rdx), (wd), (pos), 1, NULL, NULL
/* Arrayed register whose data is kept in a standard C array Register */
#define BRDATA(nm,loc,rdx,wd,dep) #nm, (loc), (rdx), (wd), 0, (dep), NULL, NULL
/* Arrayed register whose data is part of the UNIT structure */
#define URDATA(nm,loc,rdx,wd,off,dep,fl) \
    #nm, &(loc), (rdx), (wd), (off), (dep), NULL, NULL, ((fl) | REG_UNIT)
/* Same as above, but with additional description initializer */
#define ORDATAD(nm,loc,wd,desc) #nm, &(loc), 8, (wd), 0, 1, (desc), NULL
#define DRDATAD(nm,loc,wd,desc) #nm, &(loc), 10, (wd), 0, 1, (desc), NULL
#define HRDATAD(nm,loc,wd,desc) #nm, &(loc), 16, (wd), 0, 1, (desc), NULL
#define FLDATAD(nm,loc,pos,desc) #nm, &(loc), 2, 1, (pos), 1, (desc), NULL
#define GRDATAD(nm,loc,rdx,wd,pos,desc) #nm, &(loc), (rdx), (wd), (pos), 1, (desc), NULL
#define BRDATAD(nm,loc,rdx,wd,dep,desc) #nm, (loc), (rdx), (wd), 0, (dep), (desc), NULL
#define URDATAD(nm,loc,rdx,wd,off,dep,fl,desc) \
    #nm, &(loc), (rdx), (wd), (off), (dep), (desc), NULL, ((fl) | REG_UNIT)
/* Same as above, but with additional description initializer, and bitfields */
#define ORDATADF(nm,loc,wd,desc,flds) #nm, &(loc), 8, (wd), 0, 1, (desc), (flds)
#define DRDATADF(nm,loc,wd,desc,flds) #nm, &(loc), 10, (wd), 0, 1, (desc), (flds)
#define HRDATADF(nm,loc,wd,desc,flds) #nm, &(loc), 16, (wd), 0, 1, (desc), (flds)
#define FLDATADF(nm,loc,pos,desc,flds) #nm, &(loc), 2, 1, (pos), 1, (desc), (flds)
#define GRDATADF(nm,loc,rdx,wd,pos,desc,flds) #nm, &(loc), (rdx), (wd), (pos), 1, (desc), (flds)
#define BRDATADF(nm,loc,rdx,wd,dep,desc,flds) #nm, (loc), (rdx), (wd), 0, (dep), (desc), (flds)
#define URDATADF(nm,loc,rdx,wd,off,dep,fl,desc,flds) \
    #nm, &(loc), (rdx), (wd), (off), (dep), (desc), (flds), ((fl) | REG_UNIT)
#define BIT(nm)              {#nm, 0xffffffff, 1}             /* Single Bit definition */
#define BITNC                {"",  0xffffffff, 1}             /* Don't care Bit definition */
#define BITF(nm,sz)          {#nm, 0xffffffff, sz}            /* Bit Field definition */
#define BITNCF(sz)           {"",  0xffffffff, sz}            /* Don't care Bit Field definition */
#define BITFFMT(nm,sz,fmt)   {#nm, 0xffffffff, sz, NULL, #fmt}/* Bit Field definition with Output format */
#define BITFNAM(nm,sz,names) {#nm, 0xffffffff, sz, names}     /* Bit Field definition with value->name map */
#else
#define ORDATA(nm,loc,wd) "nm", &(loc), 8, (wd), 0, 1, NULL, NULL
#define DRDATA(nm,loc,wd) "nm", &(loc), 10, (wd), 0, 1, NULL, NULL
#define HRDATA(nm,loc,wd) "nm", &(loc), 16, (wd), 0, 1, NULL, NULL
#define FLDATA(nm,loc,pos) "nm", &(loc), 2, 1, (pos), 1, NULL, NULL
#define GRDATA(nm,loc,rdx,wd,pos) "nm", &(loc), (rdx), (wd), (pos), 1, NULL, NULL
#define BRDATA(nm,loc,rdx,wd,dep) "nm", (loc), (rdx), (wd), 0, (dep), NULL, NULL
#define URDATA(nm,loc,rdx,wd,off,dep,fl) \
    "nm", &(loc), (rdx), (wd), (off), (dep), NULL, NULL, ((fl) | REG_UNIT)
#define ORDATAD(nm,loc,wd,desc) "nm", &(loc), 8, (wd), 0, 1, (desc), NULL
#define DRDATAD(nm,loc,wd,desc) "nm", &(loc), 10, (wd), 0, 1, (desc), NULL
#define HRDATAD(nm,loc,wd,desc) "nm", &(loc), 16, (wd), 0, 1, (desc), NULL
#define FLDATAD(nm,loc,pos,desc) "nm", &(loc), 2, 1, (pos), 1, (desc), NULL
#define GRDATAD(nm,loc,rdx,wd,pos,desc) "nm", &(loc), (rdx), (wd), (pos), 1, (desc), NULL
#define BRDATAD(nm,loc,rdx,wd,dep,desc) "nm", (loc), (rdx), (wd), 0, (dep), (desc), NULL
#define URDATAD(nm,loc,rdx,wd,off,dep,fl,desc) \
    "nm", &(loc), (rdx), (wd), (off), (dep), (desc), NULL, ((fl) | REG_UNIT)
#define ORDATADF(nm,loc,wd,desc,flds) "nm", &(loc), 8, (wd), 0, 1, (desc), (flds)
#define DRDATADF(nm,loc,wd,desc,flds) "nm", &(loc), 10, (wd), 0, 1, (desc), (flds)
#define HRDATADF(nm,loc,wd,desc,flds) "nm", &(loc), 16, (wd), 0, 1, (desc), (flds)
#define FLDATADF(nm,loc,pos,desc,flds) "nm", &(loc), 2, 1, (pos), 1, (desc), (flds)
#define GRDATADF(nm,loc,rdx,wd,pos,desc,flds) "nm", &(loc), (rdx), (wd), (pos), 1, (desc), (flds)
#define BRDATADF(nm,loc,rdx,wd,dep,desc,flds) "nm", (loc), (rdx), (wd), 0, (dep), (desc), (flds)
#define URDATADF(nm,loc,rdx,wd,off,dep,fl,desc,flds) \
    "nm", &(loc), (rdx), (wd), (off), (dep), (desc), (flds), ((fl) | REG_UNIT)
#define BIT(nm)              {"nm", 0xffffffff, 1}              /* Single Bit definition */
#define BITNC                {"",   0xffffffff, 1}              /* Don't care Bit definition */
#define BITF(nm,sz)          {"nm", 0xffffffff, sz}             /* Bit Field definition */
#define BITNCF(sz)           {"",   0xffffffff, sz}             /* Don't care Bit Field definition */
#define BITFFMT(nm,sz,fmt)   {"nm", 0xffffffff, sz, NULL, "fmt"}/* Bit Field definition with Output format */
#define BITFNAM(nm,sz,names) {"nm", 0xffffffff, sz, names}      /* Bit Field definition with value->name map */
#endif
#define ENDBITS {NULL}  /* end of bitfield list */

/* Typedefs for principal structures */

typedef struct sim_device DEVICE;
typedef struct sim_unit UNIT;
typedef struct sim_reg REG;
typedef struct sim_ctab CTAB;
typedef struct sim_c1tab C1TAB;
typedef struct sim_shtab SHTAB;
typedef struct sim_mtab MTAB;
typedef struct sim_schtab SCHTAB;
typedef struct sim_brktab BRKTAB;
typedef struct sim_debtab DEBTAB;
typedef struct sim_fileref FILEREF;
typedef struct sim_bitfield BITFIELD;

/* Function prototypes */

#include "scp.h"
#include "sim_console.h"
#include "sim_timer.h"
#include "sim_fio.h"

/* Asynch/Threaded I/O support */

#if defined (SIM_ASYNCH_IO)
#include <pthread.h>

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
extern UNIT * volatile sim_wallclock_queue;
extern UNIT * volatile sim_wallclock_entry;
extern UNIT * volatile sim_clock_cosched_queue;
extern volatile t_bool sim_idle_wait;
extern int32 sim_asynch_check;
extern int32 sim_asynch_latency;
extern int32 sim_asynch_inst_latency;

/* Thread local storage */
#if defined(__GNUC__) && !defined(__APPLE__) && !defined(__hpux) && !defined(__OpenBSD__) && !defined(_AIX)
#define AIO_TLS __thread
#elif defined(_MSC_VER)
#define AIO_TLS __declspec(thread)
#else
/* Other compiler environment, then don't worry about thread local storage. */
/* It is primarily used only used in debugging messages */
#define AIO_TLS
#endif
#define AIO_QUEUE_CHECK(que, lock)                                  \
    if (1) {                                                        \
            UNIT *_cptr;                                            \
            if (lock)                                               \
                pthread_mutex_lock (lock);                          \
            for (_cptr = que;                                       \
                (_cptr != QUEUE_LIST_END);                          \
                _cptr = _cptr->next)                                \
                if (!_cptr->next) {                                 \
                    if (sim_deb) {                                  \
                        sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Queue Corruption detected\n");\
                        fclose(sim_deb);                            \
                        }                                           \
                    abort();                                        \
                    }                                               \
            if (lock)                                               \
                pthread_mutex_unlock (lock);                        \
        } else (void)0
#define AIO_MAIN_THREAD (pthread_equal ( pthread_self(), sim_asynch_main_threadid ))
#define AIO_LOCK                                                  \
    pthread_mutex_lock(&sim_asynch_lock)
#define AIO_UNLOCK                                                \
    pthread_mutex_unlock(&sim_asynch_lock)
#define AIO_IS_ACTIVE(uptr) (((uptr)->a_is_active ? (uptr)->a_is_active (uptr) : FALSE) || ((uptr)->a_next))
#if !defined(SIM_ASYNCH_MUX) && !defined(SIM_ASYNCH_CLOCKS)
#define AIO_CANCEL(uptr)                                          \
    if ((uptr)->a_cancel)                                         \
        (uptr)->a_cancel (uptr);                                  \
    else                                                          \
        (void)0
#endif /* !defined(SIM_ASYNCH_MUX) && !defined(SIM_ASYNCH_CLOCKS) */
#if !defined(SIM_ASYNCH_MUX) && defined(SIM_ASYNCH_CLOCKS)
#define AIO_CANCEL(uptr)                                          \
    if ((uptr)->a_cancel)                                         \
        (uptr)->a_cancel (uptr);                                  \
    else {                                                        \
        AIO_UPDATE_QUEUE;                                         \
        if ((uptr)->a_next) {                                     \
            UNIT *cptr;                                           \
            pthread_mutex_lock (&sim_timer_lock);                 \
            if ((uptr) == sim_wallclock_queue) {                  \
                sim_wallclock_queue = (uptr)->a_next;             \
                (uptr)->a_next = NULL;                            \
                sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Canceling Timer Event for %s\n", sim_uname(uptr));\
                sim_timer_event_canceled = TRUE;                  \
                pthread_cond_signal (&sim_timer_wake);            \
                }                                                 \
            else                                                  \
                for (cptr = sim_wallclock_queue;                  \
                    (cptr != QUEUE_LIST_END);                     \
                    cptr = cptr->a_next)                          \
                    if (cptr->a_next == (uptr)) {                 \
                        cptr->a_next = (uptr)->a_next;            \
                        (uptr)->a_next = NULL;                    \
                        sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Canceling Timer Event for %s\n", sim_uname(uptr));\
                        break;                                    \
                        }                                         \
            if ((uptr)->a_next == NULL)                           \
                (uptr)->a_due_time = (uptr)->a_usec_delay = 0;    \
            else {                                                \
                if ((uptr) == sim_clock_cosched_queue) {          \
                    sim_clock_cosched_queue = (uptr)->a_next;     \
                    (uptr)->a_next = NULL;                        \
                    }                                             \
                else                                              \
                    for (cptr = sim_clock_cosched_queue;          \
                        (cptr != QUEUE_LIST_END);                 \
                        cptr = cptr->a_next)                      \
                        if (cptr->a_next == (uptr)) {             \
                            cptr->a_next = (uptr)->a_next;        \
                            (uptr)->a_next = NULL;                \
                            break;                                \
                            }                                     \
                if ((uptr)->a_next == NULL) {                     \
                    sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Canceling Clock Coscheduling Event for %s\n", sim_uname(uptr));\
                    }                                             \
                }                                                 \
            while (sim_timer_event_canceled) {                    \
                pthread_mutex_unlock (&sim_timer_lock);           \
                sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Waiting for Timer Event cancelation for %s\n", sim_uname(uptr));\
                sim_os_ms_sleep (0);                              \
                pthread_mutex_lock (&sim_timer_lock);             \
                }                                                 \
            pthread_mutex_unlock (&sim_timer_lock);               \
            }                                                     \
        }
#endif
#if defined(SIM_ASYNCH_MUX) && !defined(SIM_ASYNCH_CLOCKS)
#define AIO_CANCEL(uptr)                                          \
    if ((uptr)->a_cancel)                                         \
        (uptr)->a_cancel (uptr);                                  \
    else {                                                        \
        if (((uptr)->dynflags & UNIT_TM_POLL) &&                  \
            !((uptr)->next) && !((uptr)->a_next)) {               \
            (uptr)->a_polling_now = FALSE;                        \
            sim_tmxr_poll_count -= (uptr)->a_poll_waiter_count;   \
            (uptr)->a_poll_waiter_count = 0;                      \
            }                                                     \
        }
#endif /* defined(SIM_ASYNCH_MUX) && !defined(SIM_ASYNCH_CLOCKS) */
#if defined(SIM_ASYNCH_MUX) && defined(SIM_ASYNCH_CLOCKS)
#define AIO_CANCEL(uptr)                                          \
    if ((uptr)->a_cancel)                                         \
        (uptr)->a_cancel (uptr);                                  \
    else {                                                        \
        AIO_UPDATE_QUEUE;                                         \
        if (((uptr)->dynflags & UNIT_TM_POLL) &&                  \
            !((uptr)->next) && !((uptr)->a_next)) {               \
            (uptr)->a_polling_now = FALSE;                        \
            sim_tmxr_poll_count -= (uptr)->a_poll_waiter_count;   \
            (uptr)->a_poll_waiter_count = 0;                      \
            }                                                     \
        if ((uptr)->a_next) {                                     \
            UNIT *cptr;                                           \
            pthread_mutex_lock (&sim_timer_lock);                 \
            if ((uptr) == sim_wallclock_queue) {                  \
                sim_wallclock_queue = (uptr)->a_next;             \
                (uptr)->a_next = NULL;                            \
                sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Canceling Timer Event for %s\n", sim_uname(uptr));\
                sim_timer_event_canceled = TRUE;                  \
                pthread_cond_signal (&sim_timer_wake);            \
                }                                                 \
            else                                                  \
                for (cptr = sim_wallclock_queue;                  \
                    (cptr != QUEUE_LIST_END);                     \
                    cptr = cptr->a_next)                          \
                    if (cptr->a_next == (uptr)) {                 \
                        cptr->a_next = (uptr)->a_next;            \
                        (uptr)->a_next = NULL;                    \
                        sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Canceling Timer Event for %s\n", sim_uname(uptr));\
                        break;                                    \
                        }                                         \
            if ((uptr)->a_next == NULL)                           \
                (uptr)->a_due_time = (uptr)->a_usec_delay = 0;    \
            else {                                                \
                if ((uptr) == sim_clock_cosched_queue) {          \
                    sim_clock_cosched_queue = (uptr)->a_next;     \
                    (uptr)->a_next = NULL;                        \
                    }                                             \
                else                                              \
                    for (cptr = sim_clock_cosched_queue;          \
                        (cptr != QUEUE_LIST_END);                 \
                        cptr = cptr->a_next)                      \
                        if (cptr->a_next == (uptr)) {             \
                            cptr->a_next = (uptr)->a_next;        \
                            (uptr)->a_next = NULL;                \
                            break;                                \
                            }                                     \
                if ((uptr)->a_next == NULL) {                     \
                    sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Canceling Clock Coscheduling Event for %s\n", sim_uname(uptr));\
                    }                                             \
                }                                                 \
            while (sim_timer_event_canceled) {                    \
                pthread_mutex_unlock (&sim_timer_lock);           \
                sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Waiting for Timer Event cancelation for %s\n", sim_uname(uptr));\
                sim_os_ms_sleep (0);                              \
                pthread_mutex_lock (&sim_timer_lock);             \
                }                                                 \
            pthread_mutex_unlock (&sim_timer_lock);               \
            }                                                     \
        }
#endif
#if defined(SIM_ASYNCH_CLOCKS)
#define AIO_RETURN_TIME(uptr)                                     \
    if (1) {                                                      \
        pthread_mutex_lock (&sim_timer_lock);                     \
        for (cptr = sim_wallclock_queue;                          \
             cptr != QUEUE_LIST_END;                              \
             cptr = cptr->a_next)                                 \
            if ((uptr) == cptr) {                                 \
                double inst_per_sec = sim_timer_inst_per_sec ();  \
                int32 result;                                     \
                                                                  \
                result = (int32)(((uptr)->a_due_time - sim_timenow_double())*inst_per_sec);\
                if (result < 0)                                   \
                    result = 0;                                   \
                pthread_mutex_unlock (&sim_timer_lock);           \
                return result + 1;                                \
                }                                                 \
        pthread_mutex_unlock (&sim_timer_lock);                   \
        if ((uptr)->a_next) /* On asynch queue? */                \
            return (uptr)->a_event_time + 1;                      \
        }                                                         \
    else                                                          \
        (void)0
#else
#define AIO_RETURN_TIME(uptr) (void)0
#endif
#define AIO_EVENT_BEGIN(uptr)                                     \
    do {                                                          \
        int __was_poll = uptr->dynflags & UNIT_TM_POLL
#define AIO_EVENT_COMPLETE(uptr, reason)                          \
        if (__was_poll) {                                         \
            pthread_mutex_lock (&sim_tmxr_poll_lock);             \
            uptr->a_polling_now = FALSE;                          \
            if (uptr->a_poll_waiter_count) {                      \
                sim_tmxr_poll_count -= uptr->a_poll_waiter_count; \
                uptr->a_poll_waiter_count = 0;                    \
                if (0 == sim_tmxr_poll_count)                     \
                    pthread_cond_broadcast (&sim_tmxr_poll_cond); \
                }                                                 \
            pthread_mutex_unlock (&sim_tmxr_poll_lock);           \
            }                                                     \
        AIO_UPDATE_QUEUE;                                         \
        } while (0)

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
#define AIO_QUEUE_MODE "Lock free asynchronous event queue access"
#define AIO_INIT                                                  \
    if (1) {                                                      \
      sim_asynch_main_threadid = pthread_self();                  \
      /* Empty list/list end uses the point value (void *)1.      \
         This allows NULL in an entry's a_next pointer to         \
         indicate that the entry is not currently in any list */  \
      sim_asynch_queue = QUEUE_LIST_END;                          \
      sim_wallclock_queue = QUEUE_LIST_END;                       \
      sim_wallclock_entry = NULL;                                 \
      sim_clock_cosched_queue = QUEUE_LIST_END;                   \
      }                                                           \
    else                                                          \
      (void)0
#define AIO_CLEANUP                                               \
    if (1) {                                                      \
      pthread_mutex_destroy(&sim_asynch_lock);                    \
      pthread_cond_destroy(&sim_asynch_wake);                     \
      pthread_mutex_destroy(&sim_timer_lock);                     \
      pthread_cond_destroy(&sim_timer_wake);                      \
      pthread_mutex_destroy(&sim_tmxr_poll_lock);                 \
      pthread_cond_destroy(&sim_tmxr_poll_cond);                  \
      }                                                           \
    else                                                          \
      (void)0
#ifdef _WIN32
#elif defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4) || defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8)
#define InterlockedCompareExchangePointer(Destination, Exchange, Comparand) __sync_val_compare_and_swap(Destination, Comparand, Exchange)
#elif defined(__DECC_VER)
#define InterlockedCompareExchangePointer(Destination, Exchange, Comparand) (void *)((int32)_InterlockedCompareExchange64(Destination, Exchange, Comparand))
#else
#error "Implementation of function InterlockedCompareExchangePointer() is needed to build with USE_AIO_INTRINSICS"
#endif
#define AIO_QUEUE_VAL (UNIT *)(InterlockedCompareExchangePointer(&sim_asynch_queue, sim_asynch_queue, NULL))
#define AIO_QUEUE_SET(val, queue) (UNIT *)(InterlockedCompareExchangePointer(&sim_asynch_queue, val, queue))
#define AIO_UPDATE_QUEUE                                                         \
    if (AIO_QUEUE_VAL != QUEUE_LIST_END) { /* List !Empty */                     \
      UNIT *q, *uptr;                                                            \
      int32 a_event_time;                                                        \
      do                                                                         \
        q = AIO_QUEUE_VAL;                                                       \
        while (q != AIO_QUEUE_SET(QUEUE_LIST_END, q));                           \
      while (q != QUEUE_LIST_END) {   /* List !Empty */                          \
        sim_debug (SIM_DBG_AIO_QUEUE, sim_dflt_dev, "Migrating Asynch event for %s after %d instructions\n", sim_uname(q), q->a_event_time);\
        uptr = q;                                                                \
        q = q->a_next;                                                           \
        uptr->a_next = NULL;        /* hygiene */                                \
        if (uptr->a_activate_call != &sim_activate_notbefore) {                  \
          a_event_time = uptr->a_event_time-((sim_asynch_inst_latency+1)/2);     \
          if (a_event_time < 0)                                                  \
            a_event_time = 0;                                                    \
          }                                                                      \
        else                                                                     \
          a_event_time = uptr->a_event_time;                                     \
        uptr->a_activate_call (uptr, a_event_time);                              \
        if (uptr->a_check_completion) {                                          \
          sim_debug (SIM_DBG_AIO_QUEUE, sim_dflt_dev, "Calling Completion Check for asynch event on %s\n", sim_uname(uptr));\
          uptr->a_check_completion (uptr);                                       \
          }                                                                      \
      }                                                                          \
    } else (void)0
#define AIO_ACTIVATE(caller, uptr, event_time)                                   \
    if (!pthread_equal ( pthread_self(), sim_asynch_main_threadid )) {           \
      UNIT *ouptr = (uptr);                                                      \
      sim_debug (SIM_DBG_AIO_QUEUE, sim_dflt_dev, "Queueing Asynch event for %s after %d instructions\n", sim_uname(ouptr), event_time);\
      if (ouptr->a_next) {                                                       \
        ouptr->a_activate_call = sim_activate_abs;                               \
      } else {                                                                   \
        UNIT *q, *qe;                                                            \
        ouptr->a_event_time = event_time;                                        \
        ouptr->a_activate_call = caller;                                         \
        ouptr->a_next = QUEUE_LIST_END;                 /* Mark as on list */    \
        do {                                                                     \
          do                                                                     \
            q = AIO_QUEUE_VAL;                                                   \
            while (q != AIO_QUEUE_SET(QUEUE_LIST_END, q));/* Grab current list */\
          for (qe = ouptr; qe->a_next != QUEUE_LIST_END; qe = qe->a_next);       \
          qe->a_next = q;                               /* append current list */\
          do                                                                     \
            q = AIO_QUEUE_VAL;                                                   \
            while (q != AIO_QUEUE_SET(ouptr, q));                                \
          ouptr = q;                                                             \
          } while (ouptr != QUEUE_LIST_END);                                     \
      }                                                                          \
      sim_asynch_check = 0;                             /* try to force check */ \
      if (sim_idle_wait) {                                                       \
        sim_debug (TIMER_DBG_IDLE, &sim_timer_dev, "waking due to event on %s after %d instructions\n", sim_uname(ouptr), event_time);\
        pthread_cond_signal (&sim_asynch_wake);                                  \
        }                                                                        \
      return SCPE_OK;                                                            \
    } else (void)0
#define AIO_ACTIVATE_LIST(caller, list, event_time)                              \
    if (list) {                                                                  \
      UNIT *ouptr, *q, *qe;                                                      \
      sim_debug (SIM_DBG_AIO_QUEUE, sim_dflt_dev, "Queueing Asynch events for %s after %d instructions\n", sim_uname(list), event_time);\
      for (qe=(list); qe->a_next != QUEUE_LIST_END;) {                           \
          qe->a_event_time = event_time;                                         \
          qe->a_activate_call = caller;                                          \
          qe = qe->a_next;                                                       \
          }                                                                      \
      qe->a_event_time = event_time;                                             \
      qe->a_activate_call = caller;                                              \
      ouptr = (list);                                                            \
      do {                                                                       \
        do                                                                       \
          q = AIO_QUEUE_VAL;                                                     \
          while (q != AIO_QUEUE_SET(QUEUE_LIST_END, q));/* Grab current list */  \
        for (qe = ouptr; qe->a_next != QUEUE_LIST_END; qe = qe->a_next);         \
        qe->a_next = q;                               /* append current list */  \
        do                                                                       \
          q = AIO_QUEUE_VAL;                                                     \
          while (q != AIO_QUEUE_SET(ouptr, q));                                  \
        ouptr = q;                                                               \
        } while (ouptr != QUEUE_LIST_END);                                       \
      sim_asynch_check = 0;                             /* try to force check */ \
      if (sim_idle_wait) {                                                       \
        sim_debug (TIMER_DBG_IDLE, &sim_timer_dev, "waking due to event on %s after %d instructions\n", sim_uname(ouptr), event_time);\
        pthread_cond_signal (&sim_asynch_wake);                                  \
        }                                                                        \
      } else (void)0
#else /* !USE_AIO_INTRINSICS */
/* This approach uses a pthread mutex to manage access to the link list     */
/* head sim_asynch_queue.  It will always work, but may be slower than the  */
/* lock free approach when using USE_AIO_INTRINSICS                         */
#define AIO_QUEUE_MODE "Lock based asynchronous event queue access"
#define AIO_INIT                                                  \
    if (1) {                                                      \
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
      sim_wallclock_queue = QUEUE_LIST_END;                       \
      sim_wallclock_entry = NULL;                                 \
      sim_clock_cosched_queue = QUEUE_LIST_END;                   \
      }                                                           \
    else                                                          \
      (void)0
#define AIO_CLEANUP                                               \
    if (1) {                                                      \
      pthread_mutex_destroy(&sim_asynch_lock);                    \
      pthread_cond_destroy(&sim_asynch_wake);                     \
      pthread_mutex_destroy(&sim_timer_lock);                     \
      pthread_cond_destroy(&sim_timer_wake);                      \
      pthread_mutex_destroy(&sim_tmxr_poll_lock);                 \
      pthread_cond_destroy(&sim_tmxr_poll_cond);                  \
      }                                                           \
    else                                                          \
      (void)0
#define AIO_UPDATE_QUEUE                                                         \
    if (1) {                                                                     \
      UNIT *uptr;                                                                \
      AIO_LOCK;                                                                  \
      while (sim_asynch_queue != QUEUE_LIST_END) { /* List !Empty */             \
        int32 a_event_time;                                                      \
        uptr = sim_asynch_queue;                                                 \
        sim_debug (SIM_DBG_AIO_QUEUE, sim_dflt_dev, "Migrating Asynch event for %s after %d instructions\n", sim_uname(uptr), uptr->a_event_time);\
        sim_asynch_queue = uptr->a_next;                                         \
        uptr->a_next = NULL;            /* hygiene */                            \
        if (uptr->a_activate_call != &sim_activate_notbefore) {                  \
          a_event_time = uptr->a_event_time-((sim_asynch_inst_latency+1)/2);     \
          if (a_event_time < 0)                                                  \
            a_event_time = 0;                                                    \
          }                                                                      \
        else                                                                     \
          a_event_time = uptr->a_event_time;                                     \
        AIO_UNLOCK;                                                              \
        uptr->a_activate_call (uptr, a_event_time);                              \
        if (uptr->a_check_completion) {                                          \
          sim_debug (SIM_DBG_AIO_QUEUE, sim_dflt_dev, "Calling Completion Check for asynch event on %s\n", sim_uname(uptr));\
          uptr->a_check_completion (uptr);                                       \
          }                                                                      \
        AIO_LOCK;                                                                \
      }                                                                          \
      AIO_UNLOCK;                                                                \
    } else (void)0
#define AIO_ACTIVATE(caller, uptr, event_time)                         \
    if (!pthread_equal ( pthread_self(), sim_asynch_main_threadid )) { \
      sim_debug (SIM_DBG_AIO_QUEUE, sim_dflt_dev, "Queueing Asynch event for %s after %d instructions\n", sim_uname(uptr), event_time);\
      AIO_LOCK;                                                        \
      if (uptr->a_next) {                       /* already queued? */  \
        uptr->a_activate_call = sim_activate_abs;                      \
      } else {                                                         \
        uptr->a_next = sim_asynch_queue;                               \
        uptr->a_event_time = event_time;                               \
        uptr->a_activate_call = caller;                                \
        sim_asynch_queue = uptr;                                       \
      }                                                                \
      if (sim_idle_wait) {                                             \
        sim_debug (TIMER_DBG_IDLE, &sim_timer_dev, "waking due to event on %s after %d instructions\n", sim_uname(uptr), event_time);\
        pthread_cond_signal (&sim_asynch_wake);                        \
        }                                                              \
      AIO_UNLOCK;                                                      \
      sim_asynch_check = 0;                                            \
      return SCPE_OK;                                                  \
    } else (void)0
#define AIO_ACTIVATE_LIST(caller, list, event_time)                              \
    if (list) {                                                                  \
      UNIT *qe;                                                                  \
      sim_debug (SIM_DBG_AIO_QUEUE, sim_dflt_dev, "Queueing Asynch events for %s after %d instructions\n", sim_uname(list), event_time);\
      for (qe=list; qe->a_next != QUEUE_LIST_END;) {                             \
          qe->a_event_time = event_time;                                         \
          qe->a_activate_call = caller;                                          \
          qe = qe->a_next;                                                       \
          }                                                                      \
      qe->a_event_time = event_time;                                             \
      qe->a_activate_call = caller;                                              \
      AIO_LOCK;                                                                  \
      qe->a_next = sim_asynch_queue;                                             \
      sim_asynch_queue = list;                                                   \
      sim_asynch_check = 0;                             /* try to force check */ \
      if (sim_idle_wait) {                                                       \
        sim_debug (TIMER_DBG_IDLE, &sim_timer_dev, "waking due to event on %s after %d instructions\n", sim_uname(list), event_time);\
        pthread_cond_signal (&sim_asynch_wake);                                  \
        }                                                                        \
      AIO_UNLOCK;                                                                \
      } else (void)0
#endif /* USE_AIO_INTRINSICS */
#define AIO_VALIDATE if (!pthread_equal ( pthread_self(), sim_asynch_main_threadid )) abort()
#define AIO_CHECK_EVENT                                                \
    if (0 > --sim_asynch_check) {                                      \
      AIO_UPDATE_QUEUE;                                                \
      sim_asynch_check = sim_asynch_inst_latency;                      \
    } else (void)0
#define AIO_SET_INTERRUPT_LATENCY(instpersec)                                                   \
    if (1) {                                                                                    \
      sim_asynch_inst_latency = (int32)((((double)(instpersec))*sim_asynch_latency)/1000000000);\
      if (sim_asynch_inst_latency == 0)                                                         \
        sim_asynch_inst_latency = 1;                                                            \
    } else (void)0
#else /* !SIM_ASYNCH_IO */
#define AIO_QUEUE_MODE "Asynchronous I/O is not available"
#define AIO_UPDATE_QUEUE
#define AIO_ACTIVATE(caller, uptr, event_time)
#define AIO_VALIDATE
#define AIO_CHECK_EVENT
#define AIO_INIT
#define AIO_MAIN_THREAD TRUE
#define AIO_LOCK
#define AIO_UNLOCK
#define AIO_CLEANUP
#define AIO_RETURN_TIME(uptr)
#define AIO_EVENT_BEGIN(uptr)
#define AIO_EVENT_COMPLETE(uptr, reason)
#define AIO_IS_ACTIVE(uptr) FALSE
#define AIO_CANCEL(uptr)
#define AIO_SET_INTERRUPT_LATENCY(instpersec)
#define AIO_TLS
#endif /* SIM_ASYNCH_IO */

#endif
