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

#ifndef _SIM_DEFS_H_
#define _SIM_DEFS_H_    0

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#ifndef TRUE
#define TRUE            1
#define FALSE           0
#endif

/* Length specific integer declarations */

typedef signed char     int8;
typedef signed short    int16;
typedef signed int      int32;
typedef unsigned char   uint8;
typedef unsigned short  uint16;
typedef unsigned int    uint32;         
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
#define SCPE_KFLAG      0010000                         /* tti data flag */
#define SCPE_BREAK      0020000                         /* tti break flag */
#define SCPE_DOFAILED   0040000                         /* fail in DO, not subproc */

/* Print value format codes */

#define PV_RZRO         0                               /* right, zero fill */
#define PV_RSPC         1                               /* right, space fill */
#define PV_LEFT         2                               /* left justify */

/* Default timing parameters */

#define KBD_POLL_WAIT   5000                            /* keyboard poll */
#define KBD_MAX_WAIT    500000
#define SERIAL_IN_WAIT  100                             /* serial in time */
#define SERIAL_OUT_WAIT 100                             /* serial output */
#define NOQUEUE_WAIT    10000                           /* min check time */
#define KBD_LIM_WAIT(x) (((x) > KBD_MAX_WAIT)? KBD_MAX_WAIT: (x))
#define KBD_WAIT(w,s)   ((w)? w: KBD_LIM_WAIT (s))

/* Convert switch letter to bit mask */

#define SWMASK(x) (1u << (((int) (x)) - ((int) 'A')))

/* String match */

#define MATCH_CMD(ptr,cmd) strncmp ((ptr), (cmd), strlen (ptr))

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
    };

/* Device flags */

#define DEV_V_DIS       0                               /* dev disabled */
#define DEV_V_DISABLE   1                               /* dev disable-able */
#define DEV_V_DYNM      2                               /* mem size dynamic */
#define DEV_V_NET       3                               /* network attach */
#define DEV_V_DEBUG     4                               /* debug capability */
#define DEV_V_RAW       5                               /* raw supported */
#define DEV_V_RAWONLY   6                               /* only raw supported */
#define DEV_V_UF_31     12                              /* user flags, V3.1 */
#define DEV_V_UF        16                              /* user flags */
#define DEV_V_RSV       31                              /* reserved */

#define DEV_DIS         (1 << DEV_V_DIS)
#define DEV_DISABLE     (1 << DEV_V_DISABLE)
#define DEV_DYNM        (1 << DEV_V_DYNM)
#define DEV_NET         (1 << DEV_V_NET)
#define DEV_DEBUG       (1 << DEV_V_DEBUG)
#define DEV_RAW         (1 << DEV_V_RAW)
#define DEV_RAWONLY     (1 << DEV_V_RAWONLY)

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
    t_addr              capac;                          /* capacity */
    t_addr              pos;                            /* file position */
    int32               buf;                            /* buffer */
    int32               wait;                           /* wait */
    int32               u3;                             /* device specific */
    int32               u4;                             /* device specific */
    int32               u5;                             /* device specific */
    int32               u6;                             /* device specific */
    };

/* Unit flags */

#define UNIT_V_UF_31    12                              /* dev spec, V3.1 */
#define UNIT_V_UF       16                              /* device specific */
#define UNIT_V_RSV      31                              /* reserved!! */

#define UNIT_ATTABLE    000001                          /* attachable */
#define UNIT_RO         000002                          /* read only */
#define UNIT_FIX        000004                          /* fixed capacity */
#define UNIT_SEQ        000010                          /* sequential */
#define UNIT_ATT        000020                          /* attached */
#define UNIT_BINK       000040                          /* K = power of 2 */
#define UNIT_BUFABLE    000100                          /* bufferable */
#define UNIT_MUSTBUF    000200                          /* must buffer */
#define UNIT_BUF        000400                          /* buffered */
#define UNIT_ROABLE     001000                          /* read only ok */
#define UNIT_DISABLE    002000                          /* disable-able */
#define UNIT_DIS        004000                          /* disabled */
#define UNIT_RAW        010000                          /* raw mode */
#define UNIT_TEXT       020000                          /* text mode */
#define UNIT_IDLE       040000                          /* idle eligible */

#define UNIT_UFMASK_31  (((1u << UNIT_V_RSV) - 1) & ~((1u << UNIT_V_UF_31) - 1))
#define UNIT_UFMASK     (((1u << UNIT_V_RSV) - 1) & ~((1u << UNIT_V_UF) - 1))
#define UNIT_RFLAGS     (UNIT_UFMASK|UNIT_DIS)          /* restored flags */

/* Register data structure */

struct sim_reg {
    char                *name;                          /* name */
    void                *loc;                           /* location */
    uint32              radix;                          /* radix */
    uint32              width;                          /* width */
    uint32              offset;                         /* starting bit */
    uint32              depth;                          /* save depth */
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
    };

#define MTAB_XTD        (1u << UNIT_V_RSV)              /* ext entry flag */
#define MTAB_VDV        001                             /* valid for dev */
#define MTAB_VUN        002                             /* valid for unit */
#define MTAB_VAL        004                             /* takes a value */
#define MTAB_NMO        010                             /* only if named */
#define MTAB_NC         020                             /* no UC conversion */
#define MTAB_SHP        040                             /* show takes parameter */

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

/* The following macros define structure contents */

#define UDATA(act,fl,cap) NULL,act,NULL,NULL,NULL,0,0,(fl),(cap),0,0

#if defined (__STDC__) || defined (_WIN32)
#define ORDATA(nm,loc,wd) #nm, &(loc), 8, (wd), 0, 1
#define DRDATA(nm,loc,wd) #nm, &(loc), 10, (wd), 0, 1
#define HRDATA(nm,loc,wd) #nm, &(loc), 16, (wd), 0, 1
#define FLDATA(nm,loc,pos) #nm, &(loc), 2, 1, (pos), 1
#define GRDATA(nm,loc,rdx,wd,pos) #nm, &(loc), (rdx), (wd), (pos), 1
#define BRDATA(nm,loc,rdx,wd,dep) #nm, (loc), (rdx), (wd), 0, (dep)
#define URDATA(nm,loc,rdx,wd,off,dep,fl) \
    #nm, &(loc), (rdx), (wd), (off), (dep), ((fl) | REG_UNIT)
#else
#define ORDATA(nm,loc,wd) "nm", &(loc), 8, (wd), 0, 1
#define DRDATA(nm,loc,wd) "nm", &(loc), 10, (wd), 0, 1
#define HRDATA(nm,loc,wd) "nm", &(loc), 16, (wd), 0, 1
#define FLDATA(nm,loc,pos) "nm", &(loc), 2, 1, (pos), 1
#define GRDATA(nm,loc,rdx,wd,pos) "nm", &(loc), (rdx), (wd), (pos), 1
#define BRDATA(nm,loc,rdx,wd,dep) "nm", (loc), (rdx), (wd), 0, (dep)
#define URDATA(nm,loc,rdx,wd,off,dep,fl) \
    "nm", &(loc), (rdx), (wd), (off), (dep), ((fl) | REG_UNIT)
#endif

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

/* Function prototypes */

#include "scp.h"
#include "sim_console.h"
#include "sim_timer.h"
#include "sim_fio.h"

#endif
