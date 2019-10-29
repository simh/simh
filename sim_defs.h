/* sim_defs.h: simulator definitions

   Copyright (c) 1993-2016, Robert M Supnik

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

   25-Sep-16    RMS     Removed KBD_WAIT and friends
   08-Mar-16    RMS     Added shutdown invisible switch
   24-Dec-14    JDB     Added T_ADDR_FMT
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

#include "sim_rev.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#if defined(_MSC_VER) && (_MSC_VER < 1900)
#define snprintf _snprintf      /* poor man's snprintf which will work most of the time but has different return value */
#endif
#if defined(__VAX)
extern int sim_vax_snprintf(char *buf, size_t buf_size, const char *fmt, ...);
#define snprintf sim_vax_snprintf
#endif
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>

#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif
#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif


#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <winerror.h>
#undef PACKED                       /* avoid macro name collision */
#undef ERROR                        /* avoid macro name collision */
#undef MEM_MAPPED                   /* avoid macro name collision */
#include <process.h>
#endif

#ifdef USE_REGEX
#undef USE_REGEX
#endif
#if defined(HAVE_PCREPOSIX_H)
#include <pcreposix.h>
#include <pcre.h>
#define USE_REGEX 1
#elif defined(HAVE_REGEX_H)
#include <regex.h>
#define USE_REGEX 1
#endif

#ifdef  __cplusplus
extern "C" {
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

/* SCP API shim.

   The SCP API for version 4.0 introduces a number of "pointer-to-const"
   parameter qualifiers that were not present in the 3.x versions.  To maintain
   compatibility with the earlier versions, the new qualifiers are expressed as
   "CONST" rather than "const".  This allows macro removal of the qualifiers
   when compiling for SIMH 3.x.
*/
#ifndef CONST
#define CONST const
#endif

/* Length specific integer declarations */

/* Handle the special/unusual cases first with everything else leveraging stdints.h */
#if defined (VMS)
#include <ints.h>
#elif defined(_MSC_VER) && (_MSC_VER < 1600)
typedef __int8           int8;
typedef __int16          int16;
typedef __int32          int32;
typedef unsigned __int8  uint8;
typedef unsigned __int16 uint16;
typedef unsigned __int32 uint32;
#else                                                   
/* All modern/standard compiler environments */
/* any other environment needa a special case above */
#include <stdint.h>
typedef int8_t          int8;
typedef int16_t         int16;
typedef int32_t         int32;
typedef uint8_t         uint8;
typedef uint16_t        uint16;
typedef uint32_t        uint32;
#endif                                                  /* end standard integers */

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
#define T_VALUE_MAX     0xffffffffffffffffuLL
#define T_SVALUE_MAX    0x7fffffffffffffffLL
#else                                                   /* 32b data */
typedef int32           t_svalue;
typedef uint32          t_value;
#define T_VALUE_MAX     0xffffffffUL
#define T_SVALUE_MAX    0x7fffffffL
#endif                                                  /* end 64b data */

#if defined (USE_INT64) && defined (USE_ADDR64)         /* 64b address */
typedef t_uint64        t_addr;
#define T_ADDR_W        64
#define T_ADDR_FMT      LL_FMT
#else                                                   /* 32b address */
typedef uint32          t_addr;
#define T_ADDR_W        32
#define T_ADDR_FMT      ""
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
#define LL_TYPE long long
#else
#if defined (__VAX) /* No 64 bit ints on VAX */
#define LL_FMT "l"
#define LL_TYPE long
#else
#define LL_FMT "ll"
#define LL_TYPE long long
#endif
#endif

#if defined (VMS) && (defined (__ia64) || defined (__ALPHA))
#define HAVE_GLOB
#endif

#if defined (__linux) || defined (VMS) || defined (__APPLE__)
#define HAVE_C99_STRFTIME 1
#endif

#if defined (_WIN32)
#define NULL_DEVICE "NUL:"
#elif defined (_VMS)
#define NULL_DEVICE "NL:"
#else
#define NULL_DEVICE "/dev/null"
#endif

/* Stubs for inlining */

#if defined(_MSC_VER)
#define SIM_INLINE _inline
#define SIM_NOINLINE _declspec (noinline)
#elif defined(__GNUC__)
#define SIM_INLINE inline
#define SIM_NOINLINE  __attribute__ ((noinline))
#else
#define SIM_INLINE 
#define SIM_NOINLINE
#endif

/* Storage class modifier for weak link definition for sim_vm_init() */

#if defined(__cplusplus)
#if defined(__GNUC__)
#define WEAK __attribute__((weak))
#elif defined(_MSC_VER)
#define WEAK __declspec(selectany) 
#else
#define WEAK extern 
#endif
#else
#define WEAK 
#endif

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

#define SIM_BKPT_N_SPC  (1 << (32 - SIM_BKPT_V_SPC))    /* max number spaces */
#define SIM_BKPT_V_SPC  (BRK_TYP_MAX + 1)               /* location in arg */

/* Extended switch definitions (bits >= 26) */

#define SIM_SW_HIDE     (1u << 26)                      /* enable hiding */
#define SIM_SW_REST     (1u << 27)                      /* attach/restore */
#define SIM_SW_REG      (1u << 28)                      /* register value */
#define SIM_SW_STOP     (1u << 29)                      /* stop message */
#define SIM_SW_SHUT     (1u << 30)                      /* shutdown */

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
#define SCPE_EXPECT     (SCPE_BASE + 44)                /* expect matched */
#define SCPE_AMBREG     (SCPE_BASE + 45)                /* ambiguous register */
#define SCPE_REMOTE     (SCPE_BASE + 46)                /* remote console command */
#define SCPE_INVEXPR    (SCPE_BASE + 47)                /* invalid expression */
#define SCPE_SIGTERM    (SCPE_BASE + 48)                /* SIGTERM has been received */
#define SCPE_FSSIZE     (SCPE_BASE + 49)                /* File System size larger than disk size */

#define SCPE_MAX_ERR    (SCPE_BASE + 49)                /* Maximum SCPE Error Value */
#define SCPE_KFLAG      0x10000000                      /* tti data flag */
#define SCPE_BREAK      0x20000000                      /* tti break flag */
#define SCPE_NOMESSAGE  0x40000000                      /* message display supression flag */
#define SCPE_BARE_STATUS(stat) ((stat) & ~(SCPE_NOMESSAGE|SCPE_KFLAG|SCPE_BREAK))

/* Print value format codes */

#define PV_RZRO         0                               /* right, zero fill */
#define PV_RSPC         1                               /* right, space fill */
#define PV_RCOMMA       2                               /* right, space fill. Comma separate every 3 */
#define PV_LEFT         3                               /* left justify */
#define PV_RCOMMASIGN   6                               /* right, space fill. Comma separate every 3 treat as signed */
#define PV_LEFTSIGN     7                               /* left justify treat as signed */

/* Default timing parameters */

#define KBD_POLL_WAIT   5000                            /* keyboard poll */
#define SERIAL_IN_WAIT  100                             /* serial in time */
#define SERIAL_OUT_WAIT 100                             /* serial output */
#define NOQUEUE_WAIT    1000000                         /* min check time */

/* Convert switch letter to bit mask */

#define SWMASK(x) (1u << (((int) (x)) - ((int) 'A')))

/* String match - at least one character required */

#define MATCH_CMD(ptr,cmd) ((NULL == (ptr)) || (!*(ptr)) || strncasecmp ((ptr), (cmd), strlen (ptr)))

/* End of Linked List/Queue value                           */
/* Chosen for 2 reasons:                                    */
/*     1 - to not be NULL, this allowing the NULL value to  */
/*         indicate inclusion on a list                     */
/* and                                                      */
/*     2 - to not be a valid/possible pointer (alignment)   */
#define QUEUE_LIST_END ((UNIT *)1)

/* Typedefs for principal structures */

typedef struct DEVICE DEVICE;
typedef struct UNIT UNIT;
typedef struct REG REG;
typedef struct CTAB CTAB;
typedef struct C1TAB C1TAB;
typedef struct SHTAB SHTAB;
typedef struct MTAB MTAB;
typedef struct SCHTAB SCHTAB;
typedef struct BRKTAB BRKTAB;
typedef struct BRKTYPTAB BRKTYPTAB;
typedef struct EXPTAB EXPTAB;
typedef struct EXPECT EXPECT;
typedef struct SEND SEND;
typedef struct DEBTAB DEBTAB;
typedef struct FILEREF FILEREF;
typedef struct MEMFILE MEMFILE;
typedef struct BITFIELD BITFIELD;

typedef t_stat (*ACTIVATE_API)(UNIT *unit, int32 interval);

/* Device data structure */

struct DEVICE {
    const char          *name;                          /* name */
    UNIT                *units;                         /* units */
    REG                 *registers;                     /* registers */
    MTAB                *modifiers;                     /* modifiers */
    uint32              numunits;                       /* #units */
    uint32              aradix;                         /* address radix */
    uint32              awidth;                         /* address width */
    uint32              aincr;                          /* addr increment */
    uint32              dradix;                         /* data radix */
    uint32              dwidth;                         /* data width */
    t_stat              (*examine)(t_value *v, t_addr a, UNIT *up,
                            int32 sw);                  /* examine routine */
    t_stat              (*deposit)(t_value v, t_addr a, UNIT *up,
                            int32 sw);                  /* deposit routine */
    t_stat              (*reset)(DEVICE *dp);           /* reset routine */
    t_stat              (*boot)(int32 u, DEVICE *dp);
                                                        /* boot routine */
    t_stat              (*attach)(UNIT *up, CONST char *cp);
                                                        /* attach routine */
    t_stat              (*detach)(UNIT *up);            /* detach routine */
    void                *ctxt;                          /* context */
    uint32              flags;                          /* flags */
    uint32              dctrl;                          /* debug control */
    DEBTAB              *debflags;                      /* debug flags */
    t_stat              (*msize)(UNIT *up, int32 v, CONST char *cp, void *dp);
                                                        /* mem size routine */
    char                *lname;                         /* logical name */
    t_stat              (*help)(FILE *st, DEVICE *dptr,
                            UNIT *uptr, int32 flag, const char *cptr); 
                                                        /* help */
    t_stat              (*attach_help)(FILE *st, DEVICE *dptr,
                            UNIT *uptr, int32 flag, const char *cptr);
                                                        /* attach help */
    void *help_ctx;                                     /* Context available to help routines */
    const char          *(*description)(DEVICE *dptr);  /* Device Description */
    BRKTYPTAB           *brk_types;                     /* Breakpoint types */
    };

/* Device flags */

#define DEV_V_DIS       0                               /* dev disabled */
#define DEV_V_DISABLE   1                               /* dev disable-able */
#define DEV_V_DYNM      2                               /* mem size dynamic */
#define DEV_V_DEBUG     3                               /* debug capability */
#define DEV_V_TYPE      4                               /* Attach type */
#define DEV_S_TYPE      4                               /* Width of Type Field */
#define DEV_V_SECTORS   8                               /* Unit Capacity is in 512byte sectors */
#define DEV_V_DONTAUTO  9                               /* Do not auto detach already attached units */
#define DEV_V_FLATHELP  10                              /* Use traditional (unstructured) help */
#define DEV_V_NOSAVE    11                              /* Don't save device state */
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
#define DEV_NOSAVE      (1 << DEV_V_NOSAVE)             /* Don't save device state */
#define DEV_NET         0                               /* Deprecated - meaningless */


#define DEV_TYPEMASK    (((1 << DEV_S_TYPE) - 1) << DEV_V_TYPE)
#define DEV_DISK        (1 << DEV_V_TYPE)               /* sim_disk Attach */
#define DEV_TAPE        (2 << DEV_V_TYPE)               /* sim_tape Attach */
#define DEV_MUX         (3 << DEV_V_TYPE)               /* sim_tmxr Attach */
#define DEV_CARD        (4 << DEV_V_TYPE)               /* sim_card Attach */
#define DEV_ETHER       (5 << DEV_V_TYPE)               /* Ethernet Device */
#define DEV_DISPLAY     (6 << DEV_V_TYPE)               /* Display Device */
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

struct UNIT {
    UNIT                *next;                          /* next active */
    t_stat              (*action)(UNIT *up);            /* action routine */
    char                *filename;                      /* open file name */
    FILE                *fileref;                       /* file reference */
    void                *filebuf;                       /* memory buffer */
    uint32              hwmark;                         /* high water mark */
    int32               time;                           /* time out */
    uint32              flags;                          /* flags */
    uint32              dynflags;                       /* dynamic flags */
    t_addr              capac;                          /* capacity */
    t_addr              pos;                            /* file position */
    void                (*io_flush)(UNIT *up);          /* io flush routine */
    uint32              iostarttime;                    /* I/O start time */
    int32               buf;                            /* buffer */
    int32               wait;                           /* wait */
    int32               u3;                             /* device specific */
    int32               u4;                             /* device specific */
    int32               u5;                             /* device specific */
    int32               u6;                             /* device specific */
    void                *up7;                           /* device specific */
    void                *up8;                           /* device specific */
    uint16              us9;                            /* device specific */
    uint16              us10;                           /* device specific */
    void                *tmxr;                          /* TMXR linkage */
    uint32              recsize;                        /* Tape specific info */
    t_addr              tape_eom;                       /* Tape specific info */
    t_bool              (*cancel)(UNIT *);
    double              usecs_remaining;                /* time balance for long delays */
    char                *uname;                         /* Unit name */
    DEVICE              *dptr;                          /* DEVICE linkage (backpointer) */
    uint32              dctrl;                          /* debug control */
#ifdef SIM_ASYNCH_IO
    void                (*a_check_completion)(UNIT *);
    t_bool              (*a_is_active)(UNIT *);
    UNIT                *a_next;                        /* next asynch active */
    int32               a_event_time;
    ACTIVATE_API        a_activate_call;
    /* Asynchronous Polling control */
    /* These fields should only be referenced when holding the sim_tmxr_poll_lock */
    t_bool              a_polling_now;                  /* polling active flag */
    int32               a_poll_waiter_count;            /* count of polling threads */
                                                        /* waiting for this unit */
    /* Asynchronous Timer control */
    double              a_due_time;                     /* due time for timer event */
    double              a_due_gtime;                    /* due time (in instructions) for timer event */
    double              a_usec_delay;                   /* time delay for timer event */
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

#define UNIT_ATTMULT        0000001         /* Allow multiple attach commands */
#define UNIT_TM_POLL        0000002         /* TMXR Polling unit */
#define UNIT_NO_FIO         0000004         /* fileref is NOT a FILE * */
#define UNIT_DISK_CHK       0000010         /* disk data debug checking (sim_disk) */
#define UNIT_TMR_UNIT       0000200         /* Unit registered as a calibrated timer */
#define UNIT_TAPE_MRK       0000400         /* Tape Unit Tapemark */
#define UNIT_TAPE_PNU       0001000         /* Tape Unit Position Not Updated */
#define UNIT_V_DF_TAPE      10              /* Bit offset for Tape Density reservation */
#define UNIT_S_DF_TAPE      3               /* Bits Reserved for Tape Density */
#define UNIT_V_TAPE_FMT     13              /* Bit offset for Tape Format */
#define UNIT_S_TAPE_FMT     3               /* Bits Reserved for Tape Format */
#define UNIT_M_TAPE_FMT     (((1 << UNIT_S_TAPE_FMT) - 1) << UNIT_V_TAPE_FMT)
#define UNIT_V_TAPE_ANSI    16              /* Bit offset for ANSI Tape Type */
#define UNIT_S_TAPE_ANSI    4               /* Bits Reserved for ANSI Tape Type */
#define UNIT_M_TAPE_ANSI    (((1 << UNIT_S_TAPE_ANSI) - 1) << UNIT_V_TAPE_ANSI)

struct BITFIELD {
    const char      *name;                              /* field name */
    uint32          offset;                             /* starting bit */
    uint32          width;                              /* width */
    const char      **valuenames;                       /* map of values to strings */
    const char      *format;                            /* value format string */
    };

/* Register data structure */

struct REG {
    CONST char          *name;                          /* name */
    void                *loc;                           /* location */
    uint32              radix;                          /* radix */
    uint32              width;                          /* width */
    uint32              offset;                         /* starting bit */
    uint32              depth;                          /* save depth */
    const char          *desc;                          /* description */
    BITFIELD            *fields;                        /* bit fields */
    uint32              qptr;                           /* circ q ptr */
    size_t              str_size;                       /* structure size */
    /* NOTE: Flags MUST always be last since it is initialized outside of macro definitions */
    uint32              flags;                          /* flags */
    };

/* Register flags */

#define REG_FMT         00003                           /* see PV_x */
#define REG_RO          00004                           /* read only */
#define REG_HIDDEN      00010                           /* hidden */
#define REG_NZ          00020                           /* must be non-zero */
#define REG_UNIT        00040                           /* in unit struct */
#define REG_STRUCT      00100                           /* in structure array */
#define REG_CIRC        00200                           /* circular array */
#define REG_VMIO        00400                           /* use VM data print/parse */
#define REG_VMAD        01000                           /* use VM addr print/parse */
#define REG_FIT         02000                           /* fit access to size */
#define REG_HRO         (REG_RO | REG_HIDDEN)           /* hidden, read only */

#define REG_V_UF        16                              /* device specific */
#define REG_UFMASK      (~((1u << REG_V_UF) - 1))       /* user flags mask */
#define REG_VMFLAGS     (REG_VMIO | REG_UFMASK)         /* call VM routine if any of these are set */

/* Command tables, base and alternate formats */

struct CTAB {
    const char          *name;                          /* name */
    t_stat              (*action)(int32 flag, CONST char *cptr);
                                                        /* action routine */
    int32               arg;                            /* argument */
    const char          *help;                          /* help string/structured locator */
    const char          *help_base;                     /* structured help base*/
    void                (*message)(const char *unechoed_cmdline, t_stat stat);
                                                        /* message printing routine */
    };

struct C1TAB {
    const char          *name;                          /* name */
    t_stat              (*action)(DEVICE *dptr, UNIT *uptr,
                            int32 flag, CONST char *cptr);/* action routine */
    int32               arg;                            /* argument */
    const char          *help;                          /* help string */
    };

struct SHTAB {
    const char          *name;                          /* name */
    t_stat              (*action)(FILE *st, DEVICE *dptr,
                            UNIT *uptr, int32 flag, CONST char *cptr);
    int32               arg;                            /* argument */
    const char          *help;                          /* help string */
    };

/* Modifier table - only extended entries have disp, reg, or flags */

struct MTAB {
    uint32              mask;                           /* mask */
    uint32              match;                          /* match */
    const char          *pstring;                       /* print string */
    const char          *mstring;                       /* match string */
    t_stat              (*valid)(UNIT *up, int32 v, CONST char *cp, void *dp);
                                                        /* validation routine */
    t_stat              (*disp)(FILE *st, UNIT *up, int32 v, CONST void *dp);
                                                        /* display routine */
    void                *desc;                          /* value descriptor */
                                                        /* pointer to something needed by */
                                                        /* the validation and/or display routines */
    const char          *help;                          /* help string */
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

struct SCHTAB {
    int32               logic;                          /* logical operator */
    int32               boolop;                         /* boolean operator */
    uint32              count;                          /* value count in mask and comp arrays */
    t_value             *mask;                          /* mask for logical */
    t_value             *comp;                          /* comparison for boolean */
    };

/* Breakpoint table */

struct BRKTAB {
    t_addr              addr;                           /* address */
    uint32              typ;                            /* mask of types */
#define BRK_TYP_USR_TYPES       ((1 << ('Z'-'A'+1)) - 1)/* all types A-Z */
#define BRK_TYP_DYN_STEPOVER    (SWMASK ('Z'+1))
#define BRK_TYP_DYN_USR         (SWMASK ('Z'+2))
#define BRK_TYP_DYN_ALL         (BRK_TYP_DYN_USR|BRK_TYP_DYN_STEPOVER) /* Mask of All Dynamic types */
#define BRK_TYP_TEMP            (SWMASK ('Z'+3))        /* Temporary (one-shot) */
#define BRK_TYP_MAX             (('Z'-'A')+3)           /* Maximum breakpoint type */
    int32               cnt;                            /* proceed count */
    char                *act;                           /* action string */
    double              time_fired[SIM_BKPT_N_SPC];     /* instruction count when match occurred */
    BRKTAB *next;                                       /* list with same address value */
    };

/* Breakpoint table */

struct BRKTYPTAB {
    uint32      btyp;                                   /* type mask */
    const char *desc;                                   /* description */
    };
#define BRKTYPE(typ,descrip) {SWMASK(typ), descrip}

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
    regex_t             regex;                          /* compiled regular expression */
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

/* Debug table */

struct DEBTAB {
    const char          *name;                          /* control name */
    uint32              mask;                           /* control bit */
    const char          *desc;                          /* description */
    };

/* Deprecated Debug macros.  Use sim_debug() */

#define DEBUG_PRS(d)    (sim_deb && d.dctrl)
#define DEBUG_PRD(d)    (sim_deb && d->dctrl)
#define DEBUG_PRI(d,m)  (sim_deb && (d.dctrl & (m)))
#define DEBUG_PRJ(d,m)  (sim_deb && ((d)->dctrl & (m)))

/* Open File Reference */
struct FILEREF {
    char                name[CBUFSIZE];                 /* file name */
    FILE                *file;                          /* file handle */
    int32               refcount;                       /* reference count */
    };

struct MEMFILE {
    char                *buf;                           /* buffered data */
    size_t              size;                        /* size */
    size_t              pos;                         /* data used */
    };
/* 
   The following macros exist to help populate structure contents

   They are dependent on the declaration order of the fields 
   of the structures they exist to populate.

 */

#define UDATA(act,fl,cap) NULL,act,NULL,NULL,NULL,0,0,(fl),0,(cap),0,NULL,0,0

/* Internal use ONLY (see below) Generic Register declaration for all fields */
#define _REGDATANF(nm,loc,rdx,wd,off,dep,desc,flds,qptr,siz) \
    nm, (loc), (rdx), (wd), (off), (dep), (desc), (flds), (qptr), (siz)

#if defined (__STDC__) || defined (_WIN32) /* Variants which depend on how macro arguments are convered to strings */
/* Generic Register declaration for all fields.  
   If the register structure is extended, this macro will be retained and a 
   new internal macro will be provided that populates the new register structure */
#define REGDATA(nm,loc,rdx,wd,off,dep,desc,flds,fl,qptr,siz) \
    _REGDATANF(#nm,&(loc),rdx,wd,off,dep,desc,flds,qptr,siz),(fl)
#define REGDATAC(nm,loc,rdx,wd,off,dep,desc,flds,fl,qptr,siz) \
    _REGDATANF(#nm,&(loc),rdx,wd,off,dep,desc,flds,qptr,siz),(fl)
/* Right Justified Octal Register Data */
#define ORDATA(nm,loc,wd) \
    _REGDATANF(#nm,&(loc),8,wd,0,1,NULL,NULL,0,0)
#define ORDATAD(nm,loc,wd,desc) \
    _REGDATANF(#nm,&(loc),8,wd,0,1,desc,NULL,0,0)
#define ORDATADF(nm,loc,wd,desc,flds) \
    _REGDATANF(#nm,&(loc),8,wd,0,1,desc,flds,0,0)
/* Right Justified Decimal Register Data */
#define DRDATA(nm,loc,wd) \
    _REGDATANF(#nm,&(loc),10,wd,0,1,NULL,NULL,0,0)
#define DRDATAD(nm,loc,wd,desc) \
    _REGDATANF(#nm,&(loc),10,wd,0,1,desc,NULL,0,0)
#define DRDATADF(nm,loc,wd,desc,flds) \
    _REGDATANF(#nm,&(loc),10,wd,0,1,desc,flds,0,0)
/* Right Justified Hexadecimal Register Data */
#define HRDATA(nm,loc,wd) \
    _REGDATANF(#nm,&(loc),16,wd,0,1,NULL,NULL,0,0)
#define HRDATAD(nm,loc,wd,desc) \
    _REGDATANF(#nm,&(loc),16,wd,0,1,desc,NULL,0,0)
#define HRDATADF(nm,loc,wd,desc,flds) \
    _REGDATANF(#nm,&(loc),16,wd,0,1,desc,flds,0,0)
/* Right Justified Binary Register Data */
#define BINRDATA(nm,loc,wd) \
    _REGDATANF(#nm,&(loc),2,wd,0,1,NULL,NULL,0,0)
#define BINRDATAD(nm,loc,wd,desc) \
    _REGDATANF(#nm,&(loc),2,wd,0,1,desc,NULL,0,0)
#define BINRDATADF(nm,loc,wd,desc,flds) \
    _REGDATANF(#nm,&(loc),2,wd,0,1,desc,flds,0,0)
/* One-bit binary flag at an arbitrary offset in a 32-bit word Register */
#define FLDATA(nm,loc,pos) \
    _REGDATANF(#nm,&(loc),2,1,pos,1,NULL,NULL,0,0)
#define FLDATAD(nm,loc,pos,desc) \
    _REGDATANF(#nm,&(loc),2,1,pos,1,desc,NULL,0,0)
#define FLDATADF(nm,loc,pos,desc,flds) \
    _REGDATANF(#nm,&(loc),2,1,pos,1,desc,flds,0,0)
/* Arbitrary location and Radix Register */
#define GRDATA(nm,loc,rdx,wd,pos) \
    _REGDATANF(#nm,&(loc),rdx,wd,pos,1,NULL,NULL,0,0)
#define GRDATAD(nm,loc,rdx,wd,pos,desc) \
    _REGDATANF(#nm,&(loc),rdx,wd,pos,1,desc,NULL,0,0)
#define GRDATADF(nm,loc,rdx,wd,pos,desc,flds) \
    _REGDATANF(#nm,&(loc),rdx,wd,pos,1,desc,flds,0,0)
/* Arrayed register whose data is kept in a standard C array Register */
#define BRDATA(nm,loc,rdx,wd,dep) \
    _REGDATANF(#nm,loc,rdx,wd,0,dep,NULL,NULL,0,0)
#define BRDATAD(nm,loc,rdx,wd,dep,desc) \
    _REGDATANF(#nm,loc,rdx,wd,0,dep,desc,NULL,0,0)
#define BRDATADF(nm,loc,rdx,wd,dep,desc,flds) \
    _REGDATANF(#nm,loc,rdx,wd,0,dep,desc,flds,0,0)
/* Arrayed register whose data is part of the UNIT structure */
#define URDATA(nm,loc,rdx,wd,off,dep,fl) \
    _REGDATANF(#nm,&(loc),rdx,wd,off,dep,NULL,NULL,0,0),((fl) | REG_UNIT)
#define URDATAD(nm,loc,rdx,wd,off,dep,fl,desc) \
    _REGDATANF(#nm,&(loc),rdx,wd,off,dep,desc,NULL,0,0),((fl) | REG_UNIT)
#define URDATADF(nm,loc,rdx,wd,off,dep,fl,desc,flds) \
    _REGDATANF(#nm,&(loc),rdx,wd,off,dep,desc,flds,0,0),((fl) | REG_UNIT)
/* Arrayed register whose data is part of an arbitrary structure */
#define STRDATA(nm,loc,rdx,wd,off,dep,siz,fl) \
    _REGDATANF(#nm,&(loc),rdx,wd,off,dep,NULL,NULL,0,siz),((fl) | REG_STRUCT)
#define STRDATAD(nm,loc,rdx,wd,off,dep,siz,fl,desc) \
    _REGDATANF(#nm,&(loc),rdx,wd,off,dep,desc,NULL,0,siz),((fl) | REG_STRUCT)
#define STRDATADF(nm,loc,rdx,wd,off,dep,siz,fl,desc,flds) \
    _REGDATANF(#nm,&(loc),rdx,wd,off,dep,desc,flds,0,siz),((fl) | REG_STRUCT)
#define BIT(nm)              {#nm, 0xffffffff, 1,  NULL, NULL}  /* Single Bit definition */
#define BITNC                {"",  0xffffffff, 1,  NULL, NULL}  /* Don't care Bit definition */
#define BITF(nm,sz)          {#nm, 0xffffffff, sz, NULL, NULL}  /* Bit Field definition */
#define BITNCF(sz)           {"",  0xffffffff, sz, NULL, NULL}  /* Don't care Bit Field definition */
#define BITFFMT(nm,sz,fmt)   {#nm, 0xffffffff, sz, NULL, #fmt}  /* Bit Field definition with Output format */
#define BITFNAM(nm,sz,names) {#nm, 0xffffffff, sz, names,NULL}  /* Bit Field definition with value->name map */
#else /* For non-STD-C compiler which can't stringify macro arguments with # */
/* Generic Register declaration for all fields.  
   If the register structure is extended, this macro will be retained and a 
   new macro will be provided that populates the new register structure */
#define REGDATA(nm,loc,rdx,wd,off,dep,desc,flds,fl,qptr,siz) \
    _REGDATANF("nm",&(loc),rdx,wd,off,dep,desc,flds,qptr,siz),(fl)
#define REGDATAC(nm,loc,rdx,wd,off,dep,desc,flds,fl,qptr,siz) \
    _REGDATANF("nm",&(loc),rdx,wd,off,dep,desc,flds,qptr,siz),(fl)
/* Right Justified Octal Register Data */
#define ORDATA(nm,loc,wd) \
    _REGDATANF("nm",&(loc),8,wd,0,1,NULL,NULL,0,0)
#define ORDATAD(nm,loc,wd,desc) \
    _REGDATANF("nm",&(loc),8,wd,0,1,desc,NULL,0,0)
#define ORDATADF(nm,loc,wd,desc,flds) \
    _REGDATANF("nm",&(loc),8,wd,0,1,desc,flds,0,0)
/* Right Justified Decimal Register Data */
#define DRDATA(nm,loc,wd) \
    _REGDATANF("nm",&(loc),10,wd,0,1,NULL,NULL,0,0)
#define DRDATAD(nm,loc,wd,desc) \
    _REGDATANF("nm",&(loc),10,wd,0,1,desc,NULL,0,0)
#define DRDATADF(nm,loc,wd,desc,flds) \
    _REGDATANF("nm",&(loc),10,wd,0,1,desc,flds,0,0)
/* Right Justified Hexadecimal Register Data */
#define HRDATA(nm,loc,wd) \
    _REGDATANF("nm",&(loc),16,wd,0,1,NULL,NULL,0,0)
#define HRDATAD(nm,loc,wd,desc) \
    _REGDATANF("nm",&(loc),16,wd,0,1,desc,NULL,0,0)
#define HRDATADF(nm,loc,wd,desc,flds) \
    _REGDATANF("nm",&(loc),16,wd,0,1,desc,flds,0,0)
/* Right Justified Binary Register Data */
#define BINRDATA(nm,loc,wd) \
    _REGDATANF("nm",&(loc),2,wd,0,1,NULL,NULL,0,0)
#define BINRDATAD(nm,loc,wd,desc) \
    _REGDATANF("nm",&(loc),2,wd,0,1,desc,NULL,0,0)
#define BINRDATADF(nm,loc,wd,desc,flds) \
    _REGDATANF("nm",&(loc),2,wd,0,1,desc,flds,0,0)
/* One-bit binary flag at an arbitrary offset in a 32-bit word Register */
#define FLDATA(nm,loc,pos) \
    _REGDATANF("nm",&(loc),2,1,pos,1,NULL,NULL,0,0)
#define FLDATAD(nm,loc,pos,desc) \
    _REGDATANF("nm",&(loc),2,1,pos,1,desc,NULL,0,0)
#define FLDATADF(nm,loc,pos,desc,flds) \
    _REGDATANF("nm",&(loc),2,1,pos,1,desc,flds,0,0)
/* Arbitrary location and Radix Register */
#define GRDATA(nm,loc,rdx,wd,pos) \
    _REGDATANF("nm",&(loc),rdx,wd,pos,1,NULL,NULL,0,0)
#define GRDATAD(nm,loc,rdx,wd,pos,desc) \
    _REGDATANF("nm",&(loc),rdx,wd,pos,1,desc,NULL,0,0)
#define GRDATADF(nm,loc,rdx,wd,pos,desc,flds) \
    _REGDATANF("nm",&(loc),rdx,wd,pos,1,desc,flds,0,0)
/* Arrayed register whose data is kept in a standard C array Register */
#define BRDATA(nm,loc,rdx,wd,dep) \
    _REGDATANF("nm",loc,rdx,wd,0,dep,NULL,NULL,0,0)
#define BRDATAD(nm,loc,rdx,wd,dep,desc) \
    _REGDATANF("nm",loc,rdx,wd,0,dep,desc,NULL,0,0)
#define BRDATADF(nm,loc,rdx,wd,dep,desc,flds) \
    _REGDATANF("nm",loc,rdx,wd,0,dep,desc,flds,0,0)
/* Arrayed register whose data is part of the UNIT structure */
#define URDATA(nm,loc,rdx,wd,off,dep,fl) \
    _REGDATANF("nm",&(loc),rdx,wd,off,dep,NULL,NULL,0,0),((fl) | REG_UNIT)
#define URDATAD(nm,loc,rdx,wd,off,dep,fl,desc) \
    _REGDATANF("nm",&(loc),rdx,wd,off,dep,desc,NULL,0,0),((fl) | REG_UNIT)
#define URDATADF(nm,loc,rdx,wd,off,dep,fl,desc,flds) \
    _REGDATANF("nm",&(loc),rdx,wd,off,dep,desc,flds,0,0),((fl) | REG_UNIT)
/* Arrayed register whose data is part of an arbitrary structure */
#define STRDATA(nm,loc,rdx,wd,off,dep,siz,fl) \
    _REGDATANF("nm",&(loc),rdx,wd,off,dep,NULL,NULL,0,siz),((fl) | REG_STRUCT)
#define STRDATAD(nm,loc,rdx,wd,off,dep,siz,fl,desc) \
    _REGDATANF("nm",&(loc),rdx,wd,off,dep,desc,NULL,0,siz),((fl) | REG_STRUCT)
#define STRDATADF(nm,loc,rdx,wd,off,dep,siz,fl,desc,flds) \
    _REGDATANF("nm",&(loc),rdx,wd,off,dep,desc,flds,0,siz),((fl) | REG_STRUCT)
#define BIT(nm)              {"nm", 0xffffffff, 1,  NULL, NULL} /* Single Bit definition */
#define BITNC                {"",   0xffffffff, 1,  NULL, NULL} /* Don't care Bit definition */
#define BITF(nm,sz)          {"nm", 0xffffffff, sz, NULL, NULL} /* Bit Field definition */
#define BITNCF(sz)           {"",   0xffffffff, sz, NULL, NULL} /* Don't care Bit Field definition */
#define BITFFMT(nm,sz,fmt)   {"nm", 0xffffffff, sz, NULL, "fmt"}/* Bit Field definition with Output format */
#define BITFNAM(nm,sz,names) {"nm", 0xffffffff, sz, names,NULL} /* Bit Field definition with value->name map */
#endif
#define ENDBITS {NULL}  /* end of bitfield list */


/* Function prototypes */

#include "scp.h"
#include "sim_console.h"
#include "sim_timer.h"
#include "sim_fio.h"

/* Macro to ALWAYS execute the specified expression and fail if it evaluates to false. */
/* This replaces any references to "assert()" which should never be invoked */
/* with an expression which causes side effects (i.e. must be executed for */
/* the program to work correctly) */
#define ASSURE(_Expression) while (!(_Expression)) {fprintf(stderr, "%s failed at %s line %d\n", #_Expression, __FILE__, __LINE__);  \
                                                    sim_printf("%s failed at %s line %d\n", #_Expression, __FILE__, __LINE__);       \
                                                    abort();}

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
#if defined(__GNUC__) && !defined(__APPLE__) && !defined(__hpux) && !defined(__OpenBSD__) && !defined(_AIX)
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
            if (!_cptr->next) {                                 \
                if (sim_deb) {                                  \
                    sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Queue Corruption detected\n");\
                    fclose(sim_deb);                            \
                    }                                           \
                sim_printf("Queue Corruption detected in %s line %d\n",\
                           __FILE__, __LINE);                   \
                abort();                                        \
                }                                               \
        if (lock)                                               \
            pthread_mutex_unlock (lock);                        \
        } while (0)
#define AIO_MAIN_THREAD (pthread_equal ( pthread_self(), sim_asynch_main_threadid ))
#define AIO_LOCK                                                  \
    pthread_mutex_lock(&sim_asynch_lock)
#define AIO_UNLOCK                                                \
    pthread_mutex_unlock(&sim_asynch_lock)
#define AIO_IS_ACTIVE(uptr) (((uptr)->a_is_active ? (uptr)->a_is_active (uptr) : FALSE) || ((uptr)->a_next))
#if defined(SIM_ASYNCH_MUX)
#define AIO_CANCEL(uptr)                                      \
    if (((uptr)->dynflags & UNIT_TM_POLL) &&                  \
        !((uptr)->next) && !((uptr)->a_next)) {               \
        (uptr)->a_polling_now = FALSE;                        \
        sim_tmxr_poll_count -= (uptr)->a_poll_waiter_count;   \
        (uptr)->a_poll_waiter_count = 0;                      \
        }
#endif /* defined(SIM_ASYNCH_MUX) */
#if !defined(AIO_CANCEL)
#define AIO_CANCEL(uptr)
#endif /* !defined(AIO_CANCEL) */
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
        sim_debug (TIMER_DBG_IDLE, &sim_timer_dev, "waking due to event on %s after %d instructions\n", sim_uname(uptr), event_time);\
        pthread_cond_signal (&sim_asynch_wake);                        \
        }                                                              \
      AIO_UNLOCK;                                                      \
      sim_asynch_check = 0;                                            \
      return SCPE_OK;                                                  \
    } else (void)0
#endif /* USE_AIO_INTRINSICS */
#define AIO_VALIDATE(uptr)                                             \
    if (!pthread_equal ( pthread_self(), sim_asynch_main_threadid )) { \
      sim_printf("Improper thread context for operation on %s in %s line %d\n", \
                   sim_uname(uptr), __FILE__, __LINE__);               \
      abort();                                                         \
      } else (void)0
#define AIO_CHECK_EVENT                                                \
    if (0 > --sim_asynch_check) {                                      \
      AIO_UPDATE_QUEUE;                                                \
      sim_asynch_check = sim_asynch_inst_latency;                      \
      } else (void)0
#define AIO_SET_INTERRUPT_LATENCY(instpersec)                                                   \
    do {                                                                                        \
      sim_asynch_inst_latency = (int32)((((double)(instpersec))*sim_asynch_latency)/1000000000);\
      if (sim_asynch_inst_latency == 0)                                                         \
        sim_asynch_inst_latency = 1;                                                            \
      } while (0)
#else /* !SIM_ASYNCH_IO */
#define AIO_QUEUE_MODE "Asynchronous I/O is not available"
#define AIO_UPDATE_QUEUE
#define AIO_ACTIVATE(caller, uptr, event_time)
#define AIO_VALIDATE(uptr)
#define AIO_CHECK_EVENT
#define AIO_INIT
#define AIO_MAIN_THREAD TRUE
#define AIO_LOCK
#define AIO_UNLOCK
#define AIO_CLEANUP
#define AIO_EVENT_BEGIN(uptr)
#define AIO_EVENT_COMPLETE(uptr, reason)
#define AIO_IS_ACTIVE(uptr) FALSE
#define AIO_CANCEL(uptr)
#define AIO_SET_INTERRUPT_LATENCY(instpersec)
#define AIO_TLS
#endif /* SIM_ASYNCH_IO */

#ifdef  __cplusplus
}
#endif

#endif
