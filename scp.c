/* scp.c: simulator control program

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

   08-Mar-16    RMS     Added shutdown flag for detach_all
   20-Mar-12    MP      Fixes to "SHOW <x> SHOW" commands
   06-Jan-12    JDB     Fixed "SHOW DEVICE" with only one enabled unit (Dave Bryan)  
   25-Sep-11    MP      Added the ability for a simulator built with 
                        SIM_ASYNCH_IO to change whether I/O is actually done
                        asynchronously by the new scp command SET ASYNCH and 
                        SET NOASYNCH
   22-Sep-11    MP      Added signal catching of SIGHUP and SIGTERM to cause 
                        simulator STOP.  This allows an externally signalled
                        event (i.e. system shutdown, or logoff) to signal a
                        running simulator of these events and to allow 
                        reasonable actions to be taken.  This will facilitate 
                        running a simulator as a 'service' on *nix platforms, 
                        given a sufficiently flexible simulator .ini file.  
   20-Apr-11    MP      Added expansion of %STATUS% and %TSTATUS% in do command
                        arguments.  STATUS is the numeric value of the last 
                        command error status and TSTATUS is the text message
                        relating to the last command error status
   17-Apr-11    MP      Changed sim_rest to defer attaching devices until after
                        device register contents have been restored since some
                        attach activities may reference register contained info.
   29-Jan-11    MP      Adjusted sim_debug to: 
                          - include the simulator timestamp (sim_gtime)
                            as part of the prefix for each line of output
                          - write complete lines at a time (avoid asynch I/O issues).
   05-Jan-11    MP      Added Asynch I/O support
   22-Jan-11    MP      Added SET ON, SET NOON, ON, GOTO and RETURN command support
   13-Jan-11    MP      Added "SHOW SHOW" and "SHOW <dev> SHOW" commands
   05-Jan-11    RMS     Fixed bug in deposit stride for numeric input (John Dundas)
   23-Dec-10    RMS     Clarified some help messages (Mark Pizzolato)
   08-Nov-10    RMS     Fixed handling of DO with no arguments (Dave Bryan)
   22-May-10    RMS     Added *nix READLINE support (Mark Pizzolato)
   08-Feb-09    RMS     Fixed warnings in help printouts
   29-Dec-08    RMS     Fixed implementation of MTAB_NC
   24-Nov-08    RMS     Revised RESTORE unit logic for consistency
   05-Sep-08    JDB     "detach_all" ignores error status returns if shutting down
   17-Aug-08    RMS     Revert RUN/BOOT to standard, rather than powerup, reset
   25-Jul-08    JDB     DO cmd missing params now default to null string
   29-Jun-08    JDB     DO cmd sub_args now allows "\\" to specify literal backslash
   04-Jun-08    JDB     label the patch delta more clearly
   31-Mar-08    RMS     Fixed bug in local/global register search (Mark Pizzolato)
                        Fixed bug in restore of RO units (Mark Pizzolato)
   06-Feb-08    RMS     Added SET/SHO/NO BR with default argument
   18-Jul-07    RMS     Modified match_ext for VMS ext;version support
   28-Apr-07    RMS     Modified sim_instr invocation to call sim_rtcn_init_all
                        Fixed bug in get_sim_opt
                        Fixed bug in restoration with changed memory size
   08-Mar-07    JDB     Fixed breakpoint actions in DO command file processing
   30-Jan-07    RMS     Fixed bugs in get_ipaddr
   17-Oct-06    RMS     Added idle support
   04-Oct-06    JDB     DO cmd failure now echoes cmd unless -q
   30-Aug-06    JDB     detach_unit returns SCPE_UNATT if not attached
   14-Jul-06    RMS     Added sim_activate_abs
   02-Jun-06    JDB     Fixed do_cmd to exit nested files on assertion failure
                        Added -E switch to do_cmd to exit on any error
   14-Feb-06    RMS     Upgraded save file format to V3.5
   18-Jan-06    RMS     Added fprint_stopped_gen
                        Added breakpoint spaces
                        Fixed unaligned register access (Doug Carman)
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   30-Aug-05    RMS     Revised to trim trailing spaces on file names
   25-Aug-05    RMS     Added variable default device support
   23-Aug-05    RMS     Added Linux line history support
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   01-May-05    RMS     Revised syntax for SET DEBUG (Dave Bryan)
   22-Mar-05    JDB     Modified DO command to allow ten-level nesting
   18-Mar-05    RMS     Moved DETACH tests into detach_unit (Dave Bryan)
                        Revised interface to fprint_sym, fparse_sym
   13-Mar-05    JDB     ASSERT now requires a conditional operator
   07-Feb-05    RMS     Added ASSERT command (Dave Bryan)
   02-Feb-05    RMS     Fixed bug in global register search
   26-Dec-04    RMS     Qualified SAVE examine, RESTORE deposit with SIM_SW_REST
   10-Nov-04    JDB     Fixed logging of errors from cmds in "do" file
   05-Nov-04    RMS     Moved SET/SHOW DEBUG under CONSOLE hierarchy
                        Renamed unit OFFLINE/ONLINE to DISABLED/ENABLED (Dave Bryan)
                        Revised to flush output files after simulation stop (Dave Bryan)
   15-Oct-04    RMS     Fixed HELP to suppress duplicate descriptions
   27-Sep-04    RMS     Fixed comma-separation options in set (David Bryan)
   09-Sep-04    RMS     Added -p option for RESET
   13-Aug-04    RMS     Qualified RESTORE detach with SIM_SW_REST
   17-Jul-04    JDB     DO cmd file open failure retries with ".sim" appended
   17-Jul-04    RMS     Added ECHO command (Dave Bryan)
   12-Jul-04    RMS     Fixed problem ATTACHing to read only files
                        (John Dundas)
   28-May-04    RMS     Added SET/SHOW CONSOLE
   14-Feb-04    RMS     Updated SAVE/RESTORE (V3.2)
                RMS     Added debug print routines (Dave Hittner)
                RMS     Added sim_vm_parse_addr and sim_vm_fprint_addr
                RMS     Added REG_VMAD support
                RMS     Split out libraries
                RMS     Moved logging function to SCP
                RMS     Exposed step counter interface(s)
                RMS     Fixed double logging of SHOW BREAK (Mark Pizzolato)
                RMS     Fixed implementation of REG_VMIO
                RMS     Added SET/SHOW DEBUG, SET/SHOW <device> DEBUG,
                        SHOW <device> MODIFIERS, SHOW <device> RADIX
                RMS     Changed sim_fsize to take uptr argument
   29-Dec-03    RMS     Added Telnet console output stall support
   01-Nov-03    RMS     Cleaned up implicit detach on attach/restore
                        Fixed bug in command line read while logging (Mark Pizzolato)
   01-Sep-03    RMS     Fixed end-of-file problem in dep, idep
                        Fixed error on trailing spaces in dep, idep
   15-Jul-03    RMS     Removed unnecessary test in reset_all
   15-Jun-03    RMS     Added register flag REG_VMIO
   25-Apr-03    RMS     Added extended address support (V3.0)
                        Fixed bug in SAVE (Peter Schorn)
                        Added u5, u6 fields
                        Added logical name support
   03-Mar-03    RMS     Added sim_fsize
   27-Feb-03    RMS     Fixed bug in multiword deposits to files
   08-Feb-03    RMS     Changed sim_os_sleep to void, match_ext to char*
                        Added multiple actions, .ini file support
                        Added multiple switch evaluations per line
   07-Feb-03    RMS     Added VMS support for ! (Mark Pizzolato)
   01-Feb-03    RMS     Added breakpoint table extension, actions
   14-Jan-03    RMS     Added missing function prototypes
   10-Jan-03    RMS     Added attach/restore flag, dynamic memory size support,
                        case sensitive SET options
   22-Dec-02    RMS     Added ! (OS command) feature (Mark Pizzolato)
   17-Dec-02    RMS     Added get_ipaddr
   02-Dec-02    RMS     Added EValuate command
   16-Nov-02    RMS     Fixed bug in register name match algorithm
   13-Oct-02    RMS     Fixed Borland compiler warnings (Hans Pufal)
   05-Oct-02    RMS     Fixed bugs in set_logon, ssh_break (David Hittner)
                        Added support for fixed buffer devices
                        Added support for Telnet console, removed VT support
                        Added help <command>
                        Added VMS file optimizations (Robert Alan Byer)
                        Added quiet mode, DO with parameters, GUI interface,
                           extensible commands (Brian Knittel)
                        Added device enable/disable commands
   14-Jul-02    RMS     Fixed exit bug in do, added -v switch (Brian Knittel)
   17-May-02    RMS     Fixed bug in fxread/fxwrite error usage (found by
                        Norm Lastovic)
   02-May-02    RMS     Added VT emulation interface, changed {NO}LOG to SET {NO}LOG
   22-Apr-02    RMS     Fixed laptop sleep problem in clock calibration, added
                        magtape record length error (Jonathan Engdahl)
   26-Feb-02    RMS     Fixed initialization bugs in do_cmd, get_aval
                        (Brian Knittel)
   10-Feb-02    RMS     Fixed problem in clock calibration
   06-Jan-02    RMS     Moved device enable/disable to simulators
   30-Dec-01    RMS     Generalized timer packaged, added circular arrays
   19-Dec-01    RMS     Fixed DO command bug (John Dundas)
   07-Dec-01    RMS     Implemented breakpoint package
   05-Dec-01    RMS     Fixed bug in universal register logic
   03-Dec-01    RMS     Added read-only units, extended SET/SHOW, universal registers
   24-Nov-01    RMS     Added unit-based registers
   16-Nov-01    RMS     Added DO command
   28-Oct-01    RMS     Added relative range addressing
   08-Oct-01    RMS     Added SHOW VERSION
   30-Sep-01    RMS     Relaxed attach test in BOOT
   27-Sep-01    RMS     Added queue count routine, fixed typo in ex/mod
   17-Sep-01    RMS     Removed multiple console support
   07-Sep-01    RMS     Removed conditional externs on function prototypes
                        Added special modifier print
   31-Aug-01    RMS     Changed int64 to t_int64 for Windoze (V2.7)
   18-Jul-01    RMS     Minor changes for Macintosh port
   12-Jun-01    RMS     Fixed bug in big-endian I/O (Dave Conroy)
   27-May-01    RMS     Added multiple console support
   16-May-01    RMS     Added logging
   15-May-01    RMS     Added features from Tim Litt
   12-May-01    RMS     Fixed missing return in disable_cmd
   25-Mar-01    RMS     Added ENABLE/DISABLE
   14-Mar-01    RMS     Revised LOAD/DUMP interface (again)
   05-Mar-01    RMS     Added clock calibration support
   05-Feb-01    RMS     Fixed bug, DETACH buffered unit with hwmark = 0
   04-Feb-01    RMS     Fixed bug, RESTORE not using device's attach routine
   21-Jan-01    RMS     Added relative time
   22-Dec-00    RMS     Fixed find_device for devices ending in numbers
   08-Dec-00    RMS     V2.5a changes
   30-Oct-00    RMS     Added output file option to examine
   11-Jul-99    RMS     V2.5 changes
   13-Apr-99    RMS     Fixed handling of 32b addresses
   04-Oct-98    RMS     V2.4 changes
   20-Aug-98    RMS     Added radix commands
   05-Jun-98    RMS     Fixed bug in ^D handling for UNIX
   10-Apr-98    RMS     Added switches to all commands
   26-Oct-97    RMS     Added search capability
   25-Jan-97    RMS     Revised data types
   23-Jan-97    RMS     Added bi-endian I/O
   06-Sep-96    RMS     Fixed bug in variable length IEXAMINE
   16-Jun-96    RMS     Changed interface to parse/print_sym
   06-Apr-96    RMS     Added error checking in reset all
   07-Jan-96    RMS     Added register buffers in save/restore
   11-Dec-95    RMS     Fixed ordering bug in save/restore
   22-May-95    RMS     Added symbolic input
   13-Apr-95    RMS     Added symbolic printouts
*/

/* Macros and data structures */

#define NOT_MUX_USING_CODE  /* sim_tmxr library provider or agnostic */

#define IN_SCP_C 1          /* Include from scp.c */

#include "sim_defs.h"
#include "sim_rev.h"
#include "sim_disk.h"
#include "sim_tape.h"
#include "sim_ether.h"
#include "sim_serial.h"
#include "sim_video.h"
#include "sim_sock.h"
#include "sim_frontpanel.h"
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <setjmp.h>

#if defined(HAVE_DLOPEN)                                /* Dynamic Readline support */
#include <dlfcn.h>
#endif

#ifndef MAX
#define MAX(a,b)  (((a) >= (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a,b)  (((a) <= (b)) ? (a) : (b))
#endif

/* search logical and boolean ops */

#define SCH_OR          0                               /* search logicals */
#define SCH_AND         1
#define SCH_XOR         2
#define SCH_E           0                               /* search booleans */
#define SCH_N           1
#define SCH_G           2
#define SCH_L           3
#define SCH_EE          4
#define SCH_NE          5
#define SCH_GE          6
#define SCH_LE          7

#define MAX_DO_NEST_LVL 20                              /* DO cmd nesting level */
#define SRBSIZ          1024                            /* save/restore buffer */
#define SIM_BRK_INILNT  4096                            /* bpt tbl length */
#define SIM_BRK_ALLTYP  0xFFFFFFFB
#define UPDATE_SIM_TIME                                         \
    if (1) {                                                    \
        int32 _x;                                               \
        AIO_LOCK;                                               \
        if (sim_clock_queue == QUEUE_LIST_END)                  \
            _x = noqueue_time;                                  \
        else                                                    \
            _x = sim_clock_queue->time;                         \
        sim_time = sim_time + (_x - sim_interval);              \
        sim_rtime = sim_rtime + ((uint32) (_x - sim_interval)); \
        if (sim_clock_queue == QUEUE_LIST_END)                  \
            noqueue_time = sim_interval;                        \
        else                                                    \
            sim_clock_queue->time = sim_interval;               \
        AIO_UNLOCK;                                             \
        }                                                       \
    else                                                        \
        (void)0                                                 \

#define SZ_D(dp) (size_map[((dp)->dwidth + CHAR_BIT - 1) / CHAR_BIT])
#define SZ_R(rp) \
    (size_map[((rp)->width + (rp)->offset + CHAR_BIT - 1) / CHAR_BIT])
#if defined (USE_INT64)
#define SZ_LOAD(sz,v,mb,j) \
    if (sz == sizeof (uint8)) v = *(((uint8 *) mb) + ((uint32) j)); \
    else if (sz == sizeof (uint16)) v = *(((uint16 *) mb) + ((uint32) j)); \
    else if (sz == sizeof (uint32)) v = *(((uint32 *) mb) + ((uint32) j)); \
    else v = *(((t_uint64 *) mb) + ((uint32) j));
#define SZ_STORE(sz,v,mb,j) \
    if (sz == sizeof (uint8)) *(((uint8 *) mb) + j) = (uint8) v; \
    else if (sz == sizeof (uint16)) *(((uint16 *) mb) + ((uint32) j)) = (uint16) v; \
    else if (sz == sizeof (uint32)) *(((uint32 *) mb) + ((uint32) j)) = (uint32) v; \
    else *(((t_uint64 *) mb) + ((uint32) j)) = v;
#else
#define SZ_LOAD(sz,v,mb,j) \
    if (sz == sizeof (uint8)) v = *(((uint8 *) mb) + ((uint32) j)); \
    else if (sz == sizeof (uint16)) v = *(((uint16 *) mb) + ((uint32) j)); \
    else v = *(((uint32 *) mb) + ((uint32) j));
#define SZ_STORE(sz,v,mb,j) \
    if (sz == sizeof (uint8)) *(((uint8 *) mb) + ((uint32) j)) = (uint8) v; \
    else if (sz == sizeof (uint16)) *(((uint16 *) mb) + ((uint32) j)) = (uint16) v; \
    else *(((uint32 *) mb) + ((uint32) j)) = v;
#endif
#define GET_SWITCHES(cp) \
    if ((cp = get_sim_sw (cp)) == NULL) return SCPE_INVSW
#define GET_RADIX(val,dft) \
    if (sim_switches & SWMASK ('O')) val = 8; \
    else if (sim_switches & SWMASK ('D')) val = 10; \
    else if (sim_switches & SWMASK ('H')) val = 16; \
    else if ((sim_switch_number >= 2) && (sim_switch_number <= 36)) val = sim_switch_number; \
    else val = dft;

/* Asynch I/O support */
#if defined (SIM_ASYNCH_IO)
pthread_mutex_t sim_asynch_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t sim_asynch_wake = PTHREAD_COND_INITIALIZER;

pthread_mutex_t sim_timer_lock     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t sim_timer_wake      = PTHREAD_COND_INITIALIZER;
pthread_mutex_t sim_tmxr_poll_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t sim_tmxr_poll_cond  = PTHREAD_COND_INITIALIZER;
int32 sim_tmxr_poll_count;
pthread_t sim_asynch_main_threadid;
UNIT * volatile sim_asynch_queue;
t_bool sim_asynch_enabled = TRUE;
int32 sim_asynch_check;
int32 sim_asynch_latency = 4000;      /* 4 usec interrupt latency */
int32 sim_asynch_inst_latency = 20;   /* assume 5 mip simulator */

int sim_aio_update_queue (void)
{
int migrated = 0;

AIO_ILOCK;
if (AIO_QUEUE_VAL != QUEUE_LIST_END) {  /* List !Empty */
    UNIT *q, *uptr;
    int32 a_event_time;
    do {                                /* Grab current queue */
        q = AIO_QUEUE_VAL;
        } while (q != AIO_QUEUE_SET(QUEUE_LIST_END, q));
    while (q != QUEUE_LIST_END) {       /* List !Empty */
        sim_debug (SIM_DBG_AIO_QUEUE, sim_dflt_dev, "Migrating Asynch event for %s after %d instructions\n", sim_uname(q), q->a_event_time);
        ++migrated;
        uptr = q;
        q = q->a_next;
        uptr->a_next = NULL;        /* hygiene */
        if (uptr->a_activate_call != &sim_activate_notbefore) {
            a_event_time = uptr->a_event_time-((sim_asynch_inst_latency+1)/2);
            if (a_event_time < 0)
                a_event_time = 0;
            }
        else
            a_event_time = uptr->a_event_time;
        AIO_IUNLOCK;
        uptr->a_activate_call (uptr, a_event_time);
        if (uptr->a_check_completion) {
            sim_debug (SIM_DBG_AIO_QUEUE, sim_dflt_dev, "Calling Completion Check for asynch event on %s\n", sim_uname(uptr));
            uptr->a_check_completion (uptr);
            }
        AIO_ILOCK;
        }
    }
AIO_IUNLOCK;
return migrated;
}

void sim_aio_activate (ACTIVATE_API caller, UNIT *uptr, int32 event_time)
{
AIO_ILOCK;
sim_debug (SIM_DBG_AIO_QUEUE, sim_dflt_dev, "Queueing Asynch event for %s after %d instructions\n", sim_uname(uptr), event_time);
if (uptr->a_next) {
    uptr->a_activate_call = sim_activate_abs;
    }
else {
    UNIT *q;
    uptr->a_event_time = event_time;
    uptr->a_activate_call = caller;
    do {
        q = AIO_QUEUE_VAL;
        uptr->a_next = q;                               /* Mark as on list */
        } while (q != AIO_QUEUE_SET(uptr, q));
    }
AIO_IUNLOCK;
sim_asynch_check = 0;                             /* try to force check */
if (sim_idle_wait) {
    sim_debug (TIMER_DBG_IDLE, &sim_timer_dev, "waking due to event on %s after %d instructions\n", sim_uname(uptr), event_time);
    pthread_cond_signal (&sim_asynch_wake);
    }
}
#else
t_bool sim_asynch_enabled = FALSE;
#endif

/* The per-simulator init routine is a weak global that defaults to NULL
   The other per-simulator pointers can be overrriden by the init routine */

WEAK void (*sim_vm_init) (void);
char* (*sim_vm_read) (char *ptr, int32 size, FILE *stream) = NULL;
void (*sim_vm_post) (t_bool from_scp) = NULL;
CTAB *sim_vm_cmd = NULL;
void (*sim_vm_sprint_addr) (char *buf, DEVICE *dptr, t_addr addr) = NULL;
void (*sim_vm_fprint_addr) (FILE *st, DEVICE *dptr, t_addr addr) = NULL;
t_addr (*sim_vm_parse_addr) (DEVICE *dptr, CONST char *cptr, CONST char **tptr) = NULL;
t_value (*sim_vm_pc_value) (void) = NULL;
t_bool (*sim_vm_is_subroutine_call) (t_addr **ret_addrs) = NULL;
t_bool (*sim_vm_fprint_stopped) (FILE *st, t_stat reason) = NULL;

/* Prototypes */

/* Set and show command processors */

t_stat set_dev_radix (DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat set_dev_enbdis (DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat set_dev_debug (DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat set_unit_enbdis (DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat ssh_break (FILE *st, const char *cptr, int32 flg);
t_stat show_cmd_fi (FILE *ofile, int32 flag, CONST char *cptr);
t_stat show_config (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_queue (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_time (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_mod_names (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_show_commands (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_log_names (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_dev_radix (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_dev_debug (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_dev_logicals (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_dev_modifiers (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_dev_show_commands (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_version (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_default (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_break (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_on (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat sim_show_send (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat sim_show_expect (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_device (FILE *st, DEVICE *dptr, int32 flag);
t_stat show_unit (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag);
t_stat show_all_mods (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flg, int32 *toks);
t_stat show_one_mod (FILE *st, DEVICE *dptr, UNIT *uptr, MTAB *mptr, CONST char *cptr, int32 flag);
t_stat sim_save (FILE *sfile);
t_stat sim_rest (FILE *rfile);

/* Breakpoint package */

t_stat sim_brk_init (void);
t_stat sim_brk_set (t_addr loc, int32 sw, int32 ncnt, CONST char *act);
t_stat sim_brk_clr (t_addr loc, int32 sw);
t_stat sim_brk_clrall (int32 sw);
t_stat sim_brk_show (FILE *st, t_addr loc, int32 sw);
t_stat sim_brk_showall (FILE *st, int32 sw);
CONST char *sim_brk_getact (char *buf, int32 size);
BRKTAB *sim_brk_new (t_addr loc, uint32 btyp);
char *sim_brk_clract (void);

FILE *stdnul;

/* Command support routines */

SCHTAB *get_rsearch (CONST char *cptr, int32 radix, SCHTAB *schptr);
SCHTAB *get_asearch (CONST char *cptr, int32 radix, SCHTAB *schptr);
int32 test_search (t_value *val, SCHTAB *schptr);
static const char *get_glyph_gen (const char *iptr, char *optr, char mchar, t_bool uc, t_bool quote, char escape_char);
typedef enum {
    SW_ERROR,           /* Parse Error */
    SW_BITMASK,         /* Bitmask Value or Not a switch */
    SW_NUMBER           /* Numeric Value */
    } SWITCH_PARSE;
SWITCH_PARSE get_switches (const char *cptr, int32 *sw_val, int32 *sw_number);
void put_rval (REG *rptr, uint32 idx, t_value val);
void fprint_help (FILE *st);
void fprint_stopped (FILE *st, t_stat r);
void fprint_capac (FILE *st, DEVICE *dptr, UNIT *uptr);
void fprint_sep (FILE *st, int32 *tokens);
REG *find_reg_glob (CONST char *ptr, CONST char **optr, DEVICE **gdptr);
REG *find_reg_glob_reason (CONST char *cptr, CONST char **optr, DEVICE **gdptr, t_stat *stat);
const char *sim_eval_expression (const char *cptr, t_svalue *value, t_bool parens_required, t_stat *stat);

/* Forward references */

t_stat scp_attach_unit (DEVICE *dptr, UNIT *uptr, const char *cptr);
t_stat scp_detach_unit (DEVICE *dptr, UNIT *uptr);
t_bool qdisable (DEVICE *dptr);
t_stat attach_err (UNIT *uptr, t_stat stat);
t_stat detach_all (int32 start_device, t_bool shutdown);
t_stat assign_device (DEVICE *dptr, const char *cptr);
t_stat deassign_device (DEVICE *dptr);
t_stat ssh_break_one (FILE *st, int32 flg, t_addr lo, int32 cnt, CONST char *aptr);
t_stat exdep_reg_loop (FILE *ofile, SCHTAB *schptr, int32 flag, CONST char *cptr,
    REG *lowr, REG *highr, uint32 lows, uint32 highs);
t_stat ex_reg (FILE *ofile, t_value val, int32 flag, REG *rptr, uint32 idx);
t_stat dep_reg (int32 flag, CONST char *cptr, REG *rptr, uint32 idx);
t_stat exdep_addr_loop (FILE *ofile, SCHTAB *schptr, int32 flag, const char *cptr,
    t_addr low, t_addr high, DEVICE *dptr, UNIT *uptr);
t_stat ex_addr (FILE *ofile, int32 flag, t_addr addr, DEVICE *dptr, UNIT *uptr);
t_stat dep_addr (int32 flag, const char *cptr, t_addr addr, DEVICE *dptr,
    UNIT *uptr, int32 dfltinc);
void fprint_fields (FILE *stream, t_value before, t_value after, BITFIELD* bitdefs);
t_stat step_svc (UNIT *ptr);
t_stat expect_svc (UNIT *ptr);
t_stat shift_args (char *do_arg[], size_t arg_count);
t_stat set_on (int32 flag, CONST char *cptr);
t_stat set_verify (int32 flag, CONST char *cptr);
t_stat set_message (int32 flag, CONST char *cptr);
t_stat set_quiet (int32 flag, CONST char *cptr);
t_stat set_asynch (int32 flag, CONST char *cptr);
t_stat sim_show_asynch (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat do_cmd_label (int32 flag, CONST char *cptr, CONST char *label);
void int_handler (int signal);
t_stat set_prompt (int32 flag, CONST char *cptr);
t_stat sim_set_asynch (int32 flag, CONST char *cptr);
static const char *get_dbg_verb (uint32 dbits, DEVICE* dptr, UNIT *uptr);

/* Global data */

DEVICE *sim_dflt_dev = NULL;
UNIT *sim_clock_queue = QUEUE_LIST_END;
int32 sim_interval = 0;
int32 sim_switches = 0;
int32 sim_switch_number = 0;
FILE *sim_ofile = NULL;
TMLN *sim_oline = NULL;
MEMFILE *sim_mfile = NULL;
SCHTAB *sim_schrptr = FALSE;
SCHTAB *sim_schaptr = FALSE;
DEVICE *sim_dfdev = NULL;
UNIT *sim_dfunit = NULL;
DEVICE **sim_internal_devices = NULL;
uint32 sim_internal_device_count = 0;
int32 sim_opt_out = 0;
volatile t_bool sim_is_running = FALSE;
t_bool sim_processing_event = FALSE;
uint32 sim_brk_summ = 0;
uint32 sim_brk_types = 0;
BRKTYPTAB *sim_brk_type_desc = NULL;                  /* type descriptions */
uint32 sim_brk_dflt = 0;
uint32 sim_brk_match_type;
t_addr sim_brk_match_addr;
char *sim_brk_act[MAX_DO_NEST_LVL];
char *sim_brk_act_buf[MAX_DO_NEST_LVL];
BRKTAB **sim_brk_tab = NULL;
int32 sim_brk_ent = 0;
int32 sim_brk_lnt = 0;
int32 sim_brk_ins = 0;
int32 sim_quiet = 0;
int32 sim_step = 0;
char *sim_sub_instr = NULL;         /* Copy of pre-substitution buffer contents */
char *sim_sub_instr_buf = NULL;     /* Buffer address that substitutions were saved in */
size_t sim_sub_instr_size = 0;      /* substitution buffer size */
size_t *sim_sub_instr_off = NULL;   /* offsets in substitution buffer where original data started */
static double sim_time;
static uint32 sim_rtime;
static int32 noqueue_time;
volatile t_bool stop_cpu = FALSE;
static unsigned int sim_stop_sleep_ms = 250;
static char **sim_argv;
t_value *sim_eval = NULL;
static t_value sim_last_val;
static t_addr sim_last_addr;
FILE *sim_log = NULL;                                   /* log file */
FILEREF *sim_log_ref = NULL;                            /* log file file reference */
FILE *sim_deb = NULL;                                   /* debug file */
FILEREF *sim_deb_ref = NULL;                            /* debug file file reference */
int32 sim_deb_switches = 0;                             /* debug switches */
struct timespec sim_deb_basetime;                       /* debug timestamp relative base time */
char *sim_prompt = NULL;                                /* prompt string */
static FILE *sim_gotofile;                              /* the currently open do file */
static int32 sim_goto_line[MAX_DO_NEST_LVL+1];          /* the current line number in the currently open do file */
static int32 sim_do_echo = 0;                           /* the echo status of the currently open do file */
static int32 sim_show_message = 1;                      /* the message display status of the currently open do file */
static int32 sim_on_inherit = 0;                        /* the inherit status of on state and conditions when executing do files */
static int32 sim_do_depth = 0;
static t_bool sim_cmd_echoed = FALSE;                   /* Command was emitted already prior to message output */
static char **sim_exp_argv = NULL;
static int32 sim_on_check[MAX_DO_NEST_LVL+1];
static char *sim_on_actions[MAX_DO_NEST_LVL+1][SCPE_MAX_ERR+2];
#define ON_SIGINT_ACTION (SCPE_MAX_ERR+1)
static char sim_do_filename[MAX_DO_NEST_LVL+1][CBUFSIZE];
static const char *sim_do_ocptr[MAX_DO_NEST_LVL+1];
static const char *sim_do_label[MAX_DO_NEST_LVL+1];

t_stat sim_last_cmd_stat;                               /* Command Status */
struct timespec cmd_time;                               /*  */

static SCHTAB sim_stabr;                                /* Register search specifier */
static SCHTAB sim_staba;                                /* Memory search specifier */

static DEBTAB sim_dflt_debug[] = {
    {"EVENT",       SIM_DBG_EVENT,      "Event Dispatching"},
    {"ACTIVATE",    SIM_DBG_ACTIVATE,   "Event Scheduling"},
    {"AIO_QUEUE",   SIM_DBG_AIO_QUEUE,  "Asynchronous Event Queueing"},
  {0}
};

static const char *sim_int_step_description (DEVICE *dptr)
{
return "Step/Next facility";
}

static UNIT sim_step_unit = { UDATA (&step_svc, UNIT_IDLE, 0) };
DEVICE sim_step_dev = {
    "INT-STEP", &sim_step_unit, NULL, NULL, 
    1, 0, 0, 0, 0, 0, 
    NULL, NULL, NULL, NULL, NULL, NULL, 
    NULL, DEV_NOSAVE, 0, 
    NULL, NULL, NULL, NULL, NULL, NULL,
    sim_int_step_description};

static const char *sim_int_expect_description (DEVICE *dptr)
{
return "Expect facility";
}

static UNIT sim_expect_unit = { UDATA (&expect_svc, 0, 0) };
DEVICE sim_expect_dev = {
    "INT-EXPECT", &sim_expect_unit, NULL, NULL, 
    1, 0, 0, 0, 0, 0, 
    NULL, NULL, NULL, NULL, NULL, NULL, 
    NULL, DEV_NOSAVE, 0, 
    NULL, NULL, NULL, NULL, NULL, NULL,
    sim_int_expect_description};

#if defined USE_INT64
static const char *sim_si64 = "64b data";
#else
static const char *sim_si64 = "32b data";
#endif
#if defined USE_ADDR64
static const char *sim_sa64 = "64b addresses";
#else
static const char *sim_sa64 = "32b addresses";
#endif
const char *sim_savename = sim_name;      /* Simulator Name used in SAVE/RESTORE images */

/* Tables and strings */

const char save_vercur[] = "V4.0";
const char save_ver40[] = "V4.0";
const char save_ver35[] = "V3.5";
const char save_ver32[] = "V3.2";
const char save_ver30[] = "V3.0";
const struct scp_error {
    const char *code;
    const char *message;
    } scp_errors[1+SCPE_MAX_ERR-SCPE_BASE] =
        {{"NXM",     "Address space exceeded"},
         {"UNATT",   "Unit not attached"},
         {"IOERR",   "I/O error"},
         {"CSUM",    "Checksum error"},
         {"FMT",     "Format error"},
         {"NOATT",   "Unit not attachable"},
         {"OPENERR", "File open error"},
         {"MEM",     "Memory exhausted"},
         {"ARG",     "Invalid argument"},
         {"STEP",    "Step expired"},
         {"UNK",     "Unknown command"},
         {"RO",      "Read only argument"},
         {"INCOMP",  "Command not completed"},
         {"STOP",    "Simulation stopped"},
         {"EXIT",    "Goodbye"},
         {"TTIERR",  "Console input I/O error"},
         {"TTOERR",  "Console output I/O error"},
         {"EOF",     "End of file"},
         {"REL",     "Relocation error"},
         {"NOPARAM", "No settable parameters"},
         {"ALATT",   "Unit already attached"},
         {"TIMER",   "Hardware timer error"},
         {"SIGERR",  "Signal handler setup error"},
         {"TTYERR",  "Console terminal setup error"},
         {"SUB",     "Subscript out of range"},
         {"NOFNC",   "Command not allowed"},
         {"UDIS",    "Unit disabled"},
         {"NORO",    "Read only operation not allowed"},
         {"INVSW",   "Invalid switch"},
         {"MISVAL",  "Missing value"},
         {"2FARG",   "Too few arguments"},
         {"2MARG",   "Too many arguments"},
         {"NXDEV",   "Non-existent device"},
         {"NXUN",    "Non-existent unit"},
         {"NXREG",   "Non-existent register"},
         {"NXPAR",   "Non-existent parameter"},
         {"NEST",    "Nested DO command limit exceeded"},
         {"IERR",    "Internal error"},
         {"MTRLNT",  "Invalid magtape record length"},
         {"LOST",    "Console Telnet connection lost"},
         {"TTMO",    "Console Telnet connection timed out"},
         {"STALL",   "Console Telnet output stall"},
         {"AFAIL",   "Assertion failed"},
         {"INVREM",  "Invalid remote console command"},
         {"NOTATT",  "Not attached"},
         {"EXPECT",  "Expect matched"},
         {"AMBREG",  "Ambiguous register name"},
         {"REMOTE",  "remote console command"},
         {"INVEXPR", "invalid expression"},
    };

const size_t size_map[] = { sizeof (int8),
    sizeof (int8), sizeof (int16), sizeof (int32), sizeof (int32)
#if defined (USE_INT64)
    , sizeof (t_int64), sizeof (t_int64), sizeof (t_int64), sizeof (t_int64)
#endif
};

const t_value width_mask[] = { 0,
    0x1, 0x3, 0x7, 0xF,
    0x1F, 0x3F, 0x7F, 0xFF,
    0x1FF, 0x3FF, 0x7FF, 0xFFF,
    0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF,
    0x1FFFF, 0x3FFFF, 0x7FFFF, 0xFFFFF,
    0x1FFFFF, 0x3FFFFF, 0x7FFFFF, 0xFFFFFF,
    0x1FFFFFF, 0x3FFFFFF, 0x7FFFFFF, 0xFFFFFFF,
    0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF
#if defined (USE_INT64)
    , 0x1FFFFFFFF, 0x3FFFFFFFF, 0x7FFFFFFFF, 0xFFFFFFFFF,
    0x1FFFFFFFFF, 0x3FFFFFFFFF, 0x7FFFFFFFFF, 0xFFFFFFFFFF,
    0x1FFFFFFFFFF, 0x3FFFFFFFFFF, 0x7FFFFFFFFFF, 0xFFFFFFFFFFF,
    0x1FFFFFFFFFFF, 0x3FFFFFFFFFFF, 0x7FFFFFFFFFFF, 0xFFFFFFFFFFFF,
    0x1FFFFFFFFFFFF, 0x3FFFFFFFFFFFF, 0x7FFFFFFFFFFFF, 0xFFFFFFFFFFFFF,
    0x1FFFFFFFFFFFFF, 0x3FFFFFFFFFFFFF, 0x7FFFFFFFFFFFFF, 0xFFFFFFFFFFFFFF,
    0x1FFFFFFFFFFFFFF, 0x3FFFFFFFFFFFFFF,
    0x7FFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFF,
    0x1FFFFFFFFFFFFFFF, 0x3FFFFFFFFFFFFFFF,
    0x7FFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF
#endif
    };

static const char simh_help[] =
       /***************** 80 character line width template *************************/
      "1Commands\n"
#define HLP_RESET       "*Commands Resetting Devices"
       /***************** 80 character line width template *************************/
      "2Resetting Devices\n"
      " The RESET command (abbreviation RE) resets a device or the entire simulator\n"
      " to a predefined condition.  If switch -p is specified, the device is reset\n"
      " to its power-up state:\n\n"
      "++RESET                  reset all devices\n"
      "++RESET -p               powerup all devices\n"
      "++RESET ALL              reset all devices\n"
      "++RESET <device>         reset specified device\n\n"
      " Typically, RESET stops any in-progress I/O operation, clears any interrupt\n"
      " request, and returns the device to a quiescent state.  It does not clear\n"
      " main memory or affect I/O connections.\n"
#define HLP_EXAMINE     "*Commands Examining_and_Changing_State"
#define HLP_IEXAMINE    "*Commands Examining_and_Changing_State"
#define HLP_DEPOSIT     "*Commands Examining_and_Changing_State"
#define HLP_IDEPOSIT    "*Commands Examining_and_Changing_State"
       /***************** 80 character line width template *************************/
      "2Examining and Changing State\n"
      " There are four commands to examine and change state:\n\n"
      "++EXAMINE (abbreviated E) examines state\n"
      "++DEPOSIT (abbreviated D) changes state\n"
      "++IEXAMINE (interactive examine, abbreviated IE) examines state and allows\n"
      "++++the user to interactively change it\n"
      "++IDEPOSIT (interactive deposit, abbreviated ID) allows the user to\n"
      "++++interactively change state\n\n"
      " All four commands take the form\n\n"
      "++command {modifiers} <object list>\n\n"
      " Deposit must also include a deposit value at the end of the command.\n\n"
      " There are four kinds of modifiers: switches, device/unit name, search\n"
      " specifier, and for EXAMINE, output file.  Switches have been described\n"
      " previously.  A device/unit name identifies the device and unit whose\n"
      " address space is to be examined or modified.  If no device is specified,\n"
      " the CPU (main memory)is selected; if a device but no unit is specified,\n"
      " unit 0 of the device is selected.\n\n"
      " The search specifier provides criteria for testing addresses or registers\n"
      " to see if they should be processed.  A specifier consists of a logical\n"
      " operator, a relational operator, or both, optionally separated by spaces.\n\n"
      "++{<logical op> <value>} <relational op> <value>\n\n"
       /***************** 80 character line width template *************************/
      " where the logical operator is & (and), | (or), or ^ (exclusive or), and the\n"
      " relational operator is = or == (equal), ! or != (not equal), >= (greater\n"
      " than or equal), > (greater than), <= (less than or equal), or < (less than).\n"
      " If a logical operator is specified without a relational operator, it is\n"
      " ignored.  If a relational operator is specified without a logical operator,\n"
      " no logical operation is performed.  All comparisons are unsigned.\n\n"
      " The output file modifier redirects command output to a file instead of the\n"
      " console.  An output file modifier consists of @ followed by a valid file\n"
      " name.\n\n"
      " Modifiers may be specified in any order.  If multiple modifiers of the\n"
      " same type are specified, later modifiers override earlier modifiers.  Note\n"
      " that if the device/unit name comes after the search specifier, the search\n"
      " values will interpreted in the radix of the CPU, rather than of the\n"
      " device/unit.\n\n"
      " The \"object list\" consists of one or more of the following, separated by\n"
      " commas:\n\n"
       /***************** 80 character line width template *************************/
      "++register               the specified register\n"
      "++register[sub1-sub2]    the specified register array locations,\n"
      "++                       starting at location sub1 up to and\n"
      "++                       including location sub2\n"
      "++register[sub1/length]  the specified register array locations,\n"
      "++                       starting at location sub1 up to but\n"
      "++                       not including sub1+length\n"
      "++register[ALL]          all locations in the specified register\n"
      "++                       array\n"
      "++register1-register2    all the registers starting at register1\n"
      "++                       up to and including register2\n"
      "++address                the specified location\n"
      "++address1-address2      all locations starting at address1 up to\n"
      "++                       and including address2\n"
      "++address/length         all location starting at address up to\n"
      "++                       but not including address+length\n"
      "++STATE                  all registers in the device\n"
      "++ALL                    all locations in the unit\n"
      "++$                      the last value displayed by an EXAMINE command\n"
      "                         interpreted as an address\n"
      "3Switches\n"
      " Switches can be used to control the format of display information:\n\n"
       /***************** 80 character line width template *************************/
      "++-a                 display as ASCII\n"
      "++-c                 display as character string\n"
      "++-m                 display as instruction mnemonics\n"
      "++-o or -8           display as octal\n"
      "++-d or -10          display as decimal\n"
      "++-h or -16          display as hexadecimal\n"
      "++-2                 display as binary\n\n"
      " The simulators typically accept symbolic input (see documentation with each\n"
      " simulator).\n\n"
      "3Examples\n"
      " Examples:\n\n"
      "++ex 1000-1100           examine 1000 to 1100\n"
      "++de PC 1040             set PC to 1040\n"
      "++ie 40-50               interactively examine 40:50\n"
      "++ie >1000 40-50         interactively examine the subset\n"
      "++                       of locations 40:50 that are >1000\n"
      "++ex rx0 50060           examine 50060, RX unit 0\n"
      "++ex rx sbuf[3-6]        examine SBUF[3] to SBUF[6] in RX\n"
      "++de all 0               set main memory to 0\n"
      "++de &77>0 0             set all addresses whose low order\n"
      "++                       bits are non-zero to 0\n"
      "++ex -m @memdump.txt 0-7777  dump memory to file\n\n"
      " Note: to terminate an interactive command, simply type a bad value\n"
      "       (eg, XYZ) when input is requested.\n"
#define HLP_EVALUATE    "*Commands Evaluating_Instructions"
       /***************** 80 character line width template *************************/
      "2Evaluating Instructions\n"
      " The EVAL command evaluates a symbolic instruction and returns the equivalent\n"
      " numeric value.  This is useful for obtaining numeric arguments for a search\n"
      " command:\n\n"
      "++EVAL <expression>\n\n"
      " Examples:\n\n"
      "+On the VAX simulator:\n"
      "++sim> eval addl2 r2,r3\n"
      "++0:      005352C0\n"
      "++sim> eval addl2 #ff,6(r0)\n"
      "++0:      00FF8FC0\n"
      "++4:      06A00000\n"
      "++sim> eval 'AB\n"
      "++0:      00004241\n\n"
      "+On the PDP-8:\n"
      "++sim> eval tad 60\n"
      "++0:      1060\n"
      "++sim> eval tad 300\n"
      "++tad 300\n"
      "++Can't be parsed as an instruction or data\n\n"
      " 'tad 300' fails, because with an implicit PC of 0, location 300 can't be\n"
      " reached with direct addressing.\n"
       /***************** 80 character line width template *************************/
      "2Loading and Saving Programs\n"
#define HLP_LOAD        "*Commands Loading_and_Saving_Programs LOAD"
      "3LOAD\n"
      " The LOAD command (abbreviation LO) loads a file in binary loader format:\n\n"
      "++LOAD <filename> {implementation options}\n\n"
      " The types of formats supported are implementation specific.  Options (such\n"
      " as load within range) are also implementation specific.\n\n"
#define HLP_DUMP        "*Commands Loading_and_Saving_Programs DUMP"
      "3DUMP\n"
      " The DUMP command (abbreviation DU) dumps memory in binary loader format:\n\n"
      "++DUMP <filename> {implementation options}\n\n"
      " The types of formats supported are implementation specific.  Options (such\n"
      " as dump within range) are also implementation specific.\n"
       /***************** 80 character line width template *************************/
      "2Saving and Restoring State\n"
#define HLP_SAVE        "*Commands Saving_and_Restoring_State SAVE"
      "3SAVE\n"
      " The SAVE command (abbreviation SA) save the complete state of the simulator\n"
      " to a file.  This includes the contents of main memory and all registers,\n"
      " and the I/O connections of devices:\n\n"
      "++SAVE <filename>\n\n"
#define HLP_RESTORE     "*Commands Saving_and_Restoring_State RESTORE"
      "3RESTORE\n"
      " The RESTORE command (abbreviation REST, alternately GET) restores a\n"
      " previously saved simulator state:\n\n"
      "++RESTORE <filename>\n"
      "4Switches\n"
      " Switches can influence the output and behavior of the RESTORE command\n\n"
      "++-Q      Suppresses version warning messages\n"
      "++-D      Suppress detaching and attaching devices during a restore\n"
      "++-F      Overrides the related file timestamp validation check\n"
      "\n"
      "4Notes:\n"
      " 1) SAVE file format compresses zeroes to minimize file size.\n"
      " 2) The simulator can't restore active incoming telnet sessions to\n"
      " multiplexer devices, but the listening ports will be restored across a\n"
      " save/restore.\n"
       /***************** 80 character line width template *************************/
      "2Running A Simulated Program\n"
#define HLP_RUN         "*Commands Running_A_Simulated_Program RUN"
      "3RUN {start_pc_addr} {UNTIL stop_pc_addr|\"output-string\"}\n"
      " The RUN command (abbreviated RU) resets all devices, deposits its argument\n"
      " (if given) in the PC, and starts execution.  If no argument is given,\n"
      " execution starts at the current PC.\n\n"
      " The optional UNTIL argument specifies a stop criteria for execution.\n"
      " There are two forms of execution stop criteria:\n"
      "+1. A temporary breakpoint (which exists only until it is encountered).\n"
      "+2. A string which will stop execution when the simulator has output\n"
      "++the indicated string.\n"
#define HLP_GO          "*Commands Running_A_Simulated_Program GO"
      "3GO {start_pc_addr} {UNTIL stop_pc_addr|\"output-string\"}\n"
      " The GO command does not reset devices, deposits its argument (if given)\n"
      " in the PC, and starts execution.  If no argument is given, execution\n"
      " starts at the current PC.\n\n"
      " The optional UNTIL argument specifies a stop criteria for execution.\n"
      " There are two forms of execution stop criteria:\n"
      "+1. A temporary breakpoint (which exists only until it is encountered).\n"
      "+2. A string which will stop execution when the simulator has output\n"
      "++the indicated string.\n"
#define HLP_CONTINUE    "*Commands Running_A_Simulated_Program CONTINUE"
      "3CONTINUE\n"
      " The CONT command (abbreviated CO) does not reset devices and resumes\n"
      " execution at the current PC.\n"
#define HLP_STEP        "*Commands Running_A_Simulated_Program STEP"
      "3STEP\n"
      " The STEP command (abbreviated S) resumes execution at the current PC for\n"
      " the number of instructions given by its argument.  If no argument is\n"
      " supplied, one instruction is executed.\n"
      "4Switches\n"
      " If the STEP command is invoked with the -T switch, the step command will\n"
      " cause execution to run for microseconds rather than instructions.\n"
#define HLP_NEXT        "*Commands Running_A_Simulated_Program NEXT"
      "3NEXT\n"
      " The NEXT command (abbreviated N) resumes execution at the current PC for\n"
      " one instruction, attempting to execute through a subroutine calls.\n"
      " If the next instruction to be executed is not a subroutine call,\n"
      " one instruction is executed.\n"
#define HLP_BOOT        "*Commands Running_A_Simulated_Program BOOT"
      "3BOOT\n"
      " The BOOT command (abbreviated BO) resets all devices and bootstraps the\n"
      " device and unit given by its argument.  If no unit is supplied, unit 0 is\n"
      " bootstrapped.  The specified unit must be attached.\n"
       /***************** 80 character line width template *************************/
      "2Stopping The Simulator\n"
      " Programs run until the simulator detects an error or stop condition, or\n"
      " until the user forces a stop condition.\n"
      "3Simulator Detected Stop Conditions\n"
      " These simulator-detected conditions stop simulation:\n\n"
      "++-  HALT instruction.  If a HALT instruction is decoded, simulation stops.\n"
      "++-  Breakpoint.  The simulator may support breakpoints (see below).\n"
      "++-  I/O error.  If an I/O error occurs during simulation of an I/O\n"
      "+++operation, and the device stop-on-I/O-error flag is set, simulation\n"
      "+++usually stops.\n\n"
      "++-  Processor condition.  Certain processor conditions can stop\n"
      "+++simulation; these are described with the individual simulators.\n"
      "3User Specified Stop Conditions\n"
      " Typing the interrupt character stops simulation.  The interrupt character\n"
      " is defined by the WRU (where are you) console option and is initially set\n"
      " to 005 (^E).\n\n"
       /***************** 80 character line width template *************************/
#define HLP_BREAK       "*Commands Stopping_The_Simulator User_Specified_Stop_Conditions BREAK"
#define HLP_NOBREAK     "*Commands Stopping_The_Simulator User_Specified_Stop_Conditions BREAK"
      "4Breakpoints\n"
      " A simulator may offer breakpoint capability.  A simulator may define\n"
      " breakpoints of different types, identified by letter (for example, E for\n"
      " execution, R for read, W for write, etc).  At the moment, most simulators\n"
      " support only E (execution) breakpoints.\n\n"
      " Associated with a breakpoint are a count and, optionally, one or more\n"
      " actions.  Each time the breakpoint is taken, the associated count is\n"
      " decremented.  If the count is less than or equal to 0, the breakpoint\n"
      " occurs; otherwise, it is deferred.  When the breakpoint occurs, the\n"
      " optional actions are automatically executed.\n\n"
      " A breakpoint is set by the BREAK or the SET BREAK commands:\n\n"
      "++BREAK {-types} {<addr range>{[count]},{addr range...}}{;action;action...}\n"
      "++SET BREAK {-types} {<addr range>{[count]},{addr range...}}{;action;action...}\n\n"
      " If no type is specified, the simulator-specific default breakpoint type\n"
      " (usually E for execution) is used.  If no address range is specified, the\n"
      " current PC is used.  As with EXAMINE and DEPOSIT, an address range may be a\n"
      " single address, a range of addresses low-high, or a relative range of\n"
      " address/length.\n"
       /***************** 80 character line width template *************************/
      "5Displaying Breakpoints\n"
      " Currently set breakpoints can be displayed with the SHOW BREAK command:\n\n"
      "++SHOW {-C} {-types} BREAK {ALL|<addr range>{,<addr range>...}}\n\n"
      " Locations with breakpoints of the specified type are displayed.\n\n"
      " The -C switch displays the selected breakpoint(s) formatted as commands\n"
      " which may be subsequently used to establish the same breakpoint(s).\n\n"
      "5Removing Breakpoints\n"
      " Breakpoints can be cleared by the NOBREAK or the SET NOBREAK commands.\n"
      "5Examples\n"
      "++BREAK                      set E break at current PC\n"
      "++BREAK -e 200               set E break at 200\n"
      "++BREAK 2000/2[2]            set E breaks at 2000,2001 with count = 2\n"
      "++BREAK 100;EX AC;D MQ 0     set E break at 100 with actions EX AC and\n"
      "+++++++++D MQ 0\n"
      "++BREAK 100;                 delete action on break at 100\n\n"
       /***************** 80 character line width template *************************/
#define HLP_DEBUG       "*Commands Stopping_The_Simulator User_Specified_Stop_Conditions DEBUG"
#define HLP_NODEBUG     "*Commands Stopping_The_Simulator User_Specified_Stop_Conditions DEBUG"
      "4Debug\n"
      " The DEBUG snd NODEBUG commands are aliases for the \"SET DEBUG\" and\n"
      " \"SET NODEBUG\" commands.  Additionally, support is provided that is\n"
      " equivalent to the \"SET <dev> DEBUG=opt1{;opt2}\" and\n"
      " \"SET <dev> NODEBUG=opt1{;opt2}\" commands.\n\n"
       /***************** 80 character line width template *************************/
      "2Connecting and Disconnecting Devices\n"
      " Except for main memory and network devices, units are simulated as\n"
      " unstructured binary disk files in the host file system.  Before using a\n"
      " simulated unit, the user must specify the file to be accessed by that unit.\n"
#define HLP_ATTACH      "*Commands Connecting_and_Disconnecting_Devices ATTACH"
      "3ATTACH\n"
      " The ATTACH (abbreviation AT) command associates a unit and a file:\n"
      "++ATTACH <unit> <filename>\n\n"
      " Some devices have more detailed or specific help available with:\n\n"
      "++HELP <device> ATTACH\n\n"
      "4Switches\n"
      "5-n\n"
      " If the -n switch is specified when an attach is executed, a new file is\n"
      " created, and an appropriate message is printed.\n"
      "5-e\n"
      " If the file does not exist, and the -e switch was not specified, a new\n"
      " file is created, and an appropriate message is printed.  If the -e switch\n"
      " was specified, a new file is not created, and an error message is printed.\n"
      "5-r\n"
      " If the -r switch is specified, or the file is write protected, ATTACH tries\n"
      " to open the file read only.  If the file does not exist, or the unit does\n"
      " not support read only operation, an error occurs.  Input-only devices, such\n"
      " as paper-tape readers, and devices with write lock switches, such as disks\n"
      " and tapes, support read only operation; other devices do not.  If a file is\n"
      " attached read only, its contents can be examined but not modified.\n"
      "5-q\n"
      " If the -q switch is specified when creating a new file (-n) or opening one\n"
      " read only (-r), any messages announcing these facts will be suppressed.\n"
      "5-f\n"
      " For simulated magnetic tapes, the ATTACH command can specify the format of\n"
      " the attached tape image file:\n\n"
      "++ATTACH -f <tape_unit> <format> <filename>\n\n"
      " The currently supported tape image file formats are:\n\n"
      "++SIMH                   SIMH simulator format\n"
      "++E11                    E11 simulator format\n"
      "++TPC                    TPC format\n"
      "++P7B                    Pierce simulator 7-track format\n\n"
       /***************** 80 character line width template *************************/
      " For some simulated disk devices, the ATTACH command can specify the format\n"
      " of the attached disk image file:\n\n"
      "++ATTACH -f <disk_unit> <format> <filename>\n\n"
      " The currently supported disk image file formats are:\n\n"
      "++SIMH                   SIMH simulator format\n"
      "++VHD                    Virtual Disk format\n"
      "++RAW                    platform specific access to physical disk or\n"
      "++                       CDROM drives\n"
      " The disk format can also be set with the SET command prior to ATTACH:\n\n"
      "++SET <disk_unit> FORMAT=<format>\n"
      "++ATT <disk_unit> <filename>\n\n"
       /***************** 80 character line width template *************************/
      " The format of an attached tape or disk file can be displayed with the SHOW\n"
      " command:\n"
      "++SHOW <unit> FORMAT\n"
      " For Telnet-based terminal emulation devices, the ATTACH command associates\n"
      " the master unit with a TCP/IP listening port:\n\n"
      "++ATTACH <unit> <port>\n\n"
      " The port is a decimal number between 1 and 65535 that is not already used\n"
      " other TCP/IP applications.\n"
      " For Ethernet emulators, the ATTACH command associates the simulated Ethernet\n"
      " with a physical Ethernet device:\n\n"
      "++ATTACH <unit> <physical device name>\n"
       /***************** 80 character line width template *************************/
#define HLP_DETACH      "*Commands Connecting_and_Disconnecting_Devices DETACH"
      "3DETACH\n"
      " The DETACH (abbreviation DET) command breaks the association between a unit\n"
      " and a file, port, or network device:\n\n"
      "++DETACH ALL             detach all units\n"
      "++DETACH <unit>          detach specified unit\n"
      " The EXIT command performs an automatic DETACH ALL.\n"
      "2Controlling Simulator Operating Environment\n"
      "3Working Directory\n"
#define HLP_CD          "*Commands Controlling_Simulator_Operating_Environment Working_Directory CD"
      "4CD\n"
      " Set the current working directory:\n"
      "++CD path\n"
      "4SET_DEFAULT\n"
      " Set the current working directory:\n"
      "++SET DEFAULT path\n"
#define HLP_PWD         "*Commands Controlling_Simulator_Operating_Environment Working_Directory PWD"
      "4PWD\n"
      "++PWD\n"
      " Display the current working directory:\n"
      "2Listing Files\n"
#define HLP_DIR         "*Commands Listing_Files DIR"
      "3DIR\n"
      "++DIR {path}                list directory files\n"
#define HLP_LS          "*Commands Listing_Files LS"
      "3LS\n"
      "++LS {path}                 list directory files\n"
      "2Displaying Files\n"
#define HLP_TYPE         "*Commands Displaying_Files TYPE"
      "3TYPE\n"
      "++TYPE file                 display a file contents\n"
#define HLP_CAT          "*Commands Displaying_Files CAT"
      "3CAT\n"
      "++CAT file                  display a file contents\n"
      "2Removing Files\n"
#define HLP_DELETE       "*Commands Removing_Files DEL"
      "3DELETE\n"
      "++DEL{ete} file             deletes a file\n"
#define HLP_RM          "*Commands Removing_Files RM"
      "3RM\n"
      "++RM file                   deletes a file\n"
      "2Copying Files\n"
#define HLP_COPY        "*Commands Copying_Files COPY"
      "3COPY\n"
      "++COPY sfile dfile          copies a file\n"
#define HLP_CP          "*Commands Copying_Files CP"
      "3CP\n"
      "++CP sfile dfile            copies a file\n"
#define HLP_SET         "*Commands SET"
      "2SET\n"
       /***************** 80 character line width template *************************/
#define HLP_SET_CONSOLE "*Commands SET CONSOLE"
      "3Console\n"
      "+SET CONSOLE arg{,arg...}    set console options\n"
      "+SET CONSOLE WRU=value       specify console drop to simh character\n"
      "+SET CONSOLE BRK=value       specify console Break character\n"
      "+SET CONSOLE DEL=value       specify console delete character\n"
      "+SET CONSOLE PCHAR=bitmask   bit mask of printable characters in\n"
      "++++++++                     range [31,0]\n"
      "+SET CONSOLE SPEED=speed{*factor}\n"
      "++++++++                     specify console input data rate\n"
      "+SET CONSOLE TELNET=port     specify console telnet port\n"
      "+SET CONSOLE TELNET=LOG=log_file\n"
      "++++++++                     specify console telnet logging to the\n"
      "++++++++                     specified destination {LOG,STDOUT,STDERR,\n"
      "++++++++                     DEBUG or filename)\n"
      "+SET CONSOLE TELNET=NOLOG    disables console telnet logging\n"
      "+SET CONSOLE TELNET=BUFFERED[=bufsize]\n"
      "++++++++                     specify console telnet buffering\n"
      "+SET CONSOLE TELNET=NOBUFFERED\n"
      "++++++++                     disables console telnet buffering\n"
      "+SET CONSOLE TELNET=UNBUFFERED\n"
      "++++++++                     disables console telnet buffering\n"
      "+SET CONSOLE NOTELNET        disable console telnet\n"
      "+SET CONSOLE SERIAL=serialport[;config]\n"
      "++++++++                     specify console serial port and optionally\n"
      "++++++++                     the port config (i.e. ;9600-8n1)\n"
      "+SET CONSOLE NOSERIAL        disable console serial session\n"
      "+SET CONSOLE SPEED=nn{*fac}  specifies the maximum console port input rate\n"
       /***************** 80 character line width template *************************/
#define HLP_SET_REMOTE "*Commands SET REMOTE"
      "3Remote\n"
      "+SET REMOTE TELNET=port      specify remote console telnet port\n"
      "+SET REMOTE NOTELNET         disables remote console\n"
      "+SET REMOTE BUFFERSIZE=bufsize\n"
      "++++++++                     specify remote console command output buffer\n"
      "++++++++                     size\n"
      "+SET REMOTE CONNECTIONS=n    specify number of concurrent remote\n"
      "++++++++                     console sessions\n"
      "+SET REMOTE TIMEOUT=n        specify number of seconds without input\n"
      "++++++++                     before automatic continue\n"
      "+SET REMOTE MASTER           enable master mode remote console\n"
      "+SET REMOTE NOMASTER         disable remote master mode console\n"
#define HLP_SET_DEFAULT "*Commands SET Working_Directory"
      "3Working Directory\n"
      "+SET DEFAULT <dir>           set the current directory\n"
      "+CD <dir>                    set the current directory\n"
#define HLP_SET_LOG    "*Commands SET Log"
      "3Log\n"
      " Interactions with the simulator session (at the \"sim>\" prompt\n"
      " can be recorded to a log file\n\n"
      "+SET LOG log_file            specify the log destination\n"
      "++++++++                     (STDOUT,DEBUG or filename)\n"
      "+SET NOLOG                   disables any currently active logging\n"
      "4Switches\n"
      " By default, log output is written at the end of the specified log file.\n"
      " A new log file can created if the -N switch is used on the command line.\n"
#define HLP_SET_DEBUG  "*Commands SET Debug"
       /***************** 80 character line width template *************************/
      "3Debug\n"
      "+SET DEBUG debug_file        specify the debug destination\n"
      "++++++++                     (STDOUT,STDERR,LOG or filename)\n"
      "+SET NODEBUG                 disables any currently active debug output\n"
      "4Switches\n"
      " Debug message output contains a timestamp which indicates the number of\n"
      " simulated instructions which have been executed prior to the debug event.\n\n"
      " Debug message output can be enhanced to contain additional, potentially\n"
      " useful information.\n"
      "5-T\n"
      " The -T switch causes debug output to contain a time of day displayed\n"
      " as hh:mm:ss.msec.\n"
      "5-A\n"
      " The -A switch causes debug output to contain a time of day displayed\n"
      " as seconds.msec.\n"
      "5-R\n"
      " The -R switch causes the time of day displayed due to the -T or -A\n"
      " switches to be relative to the start time of debugging.  If neither\n"
      " -T or -A is explicitly specified, -T is implied.\n"
      "5-P\n"
      " The -P switch adds the output of the PC (Program Counter) to each debug\n"
      " message.\n"
      "5-N\n"
      " The -N switch causes a new/empty file to be written to.  The default\n"
      " is to append to an existing debug log file.\n"
      "5-D\n"
      " The -D switch causes data blob output to also display the data as\n"
      " RADIX-50 characters.\n"
      "5-E\n"
      " The -E switch causes data blob output to also display the data as\n"
      " EBCDIC characters.\n"
#define HLP_SET_BREAK  "*Commands SET Breakpoints"
      "3Breakpoints\n"
      "+SET BREAK <list>            set breakpoints\n"
      "+SET NOBREAK <list>          clear breakpoints\n"
       /***************** 80 character line width template *************************/
#define HLP_SET_THROTTLE "*Commands SET Throttle"
      "3Throttle\n"
      " Simulator instruction execution rate can be controlled by specifying\n"
      " one of the following throttle commands:\n\n"
      "+SET THROTTLE xM             execute x million instructions per second\n"
      "+SET THROTTLE xK             execute x thousand instructions per second\n"
      "+SET THROTTLE x%%             occupy x percent of the host capacity\n"
      "++++++++executing instructions\n"
      "+SET THROTTLE x/t            sleep for t milliseconds after executing x\n"
      "++++++++instructions\n\n"
      "+SET NOTHROTTLE              set simulation rate to maximum\n\n"
      " Throttling is only available on host systems that implement a precision\n"
      " real-time delay function.\n\n"
      " xM, xK and x%% modes require the simulator to execute sufficient\n"
      " instructions to actually calibrate the desired execution rate relative\n"
      " to wall clock time.  Very short running programs may complete before\n"
      " calibration completes and therefore before the simulated execution rate\n"
      " can match the desired rate.\n\n"
      " The SET NOTHROTTLE command turns off throttling.  The SHOW THROTTLE\n"
      " command shows the current settings for throttling and the calibration\n"
      " results\n\n"
      " Some simulators implement a different form of host CPU resource management\n"
      " called idling.  Idling suspends simulated execution whenever the program\n"
      " running in the simulator is doing nothing, and runs the simulator at full\n"
      " speed when there is work to do.  Throttling and idling are mutually\n"
      " exclusive.\n"
#define HLP_SET_CLOCKS "*Commands SET Clocks"
      "3Clock\n"
#if defined (SIM_ASYNCH_CLOCKS)
      "+SET CLOCK asynch            enable asynchronous clocks\n"
      "+SET CLOCK noasynch          disable asynchronous clocks\n"
#endif
      "+SET CLOCK nocatchup         disable catchup clock ticks\n"
      "+SET CLOCK catchup           enable catchup clock ticks\n"
      "+SET CLOCK calib=n%%          specify idle calibration skip %%\n"
      "+SET CLOCK stop=n            stop execution after n instructions\n\n"
      " The SET CLOCK STOP command allows execution to have a bound when\n"
      " execution starts with a BOOT, NEXT or CONTINUE command.\n"
#define HLP_SET_ASYNCH "*Commands SET Asynch"
      "3Asynch\n"
      "+SET ASYNCH                  enable asynchronous I/O\n"
      "+SET NOASYNCH                disable asynchronous I/O\n"
#define HLP_SET_ENVIRON "*Commands SET Environment"
      "3Environment\n"
      "4Explicitily Changing a Variable\n"
      "+SET ENVIRONMENT name=val    set environment variable\n"
      "+SET ENVIRONMENT name        clear environment variable\n"
      "4Arithmetic Computations into a Variable\n\n"
      "+SET ENVIRONMENT -A name=expression\n\n"
      " Expression can contain any of these C language operators:\n\n"
      "++ (                  Open Parenthesis\n"
      "++ )                  Close Parenthesis\n"
      "++ -                  Subtraction\n"
      "++ +                  Addition\n"
      "++ *                  Multiplication\n"
      "++ /                  Division\n"
      "++ %%                  Modulus\n"
      "++ &&                 Logical AND\n"
      "++ ||                 Logical OR\n"
      "++ &                  Bitwise AND\n"
      "++ |                  Bitwise Inclusive OR\n"
      "++ ^                  Bitwise Exclusive OR\n"
      "++ >>                 Bitwise Right Shift\n"
      "++ <<                 Bitwise Left Shift\n"
      "++ ==                 Equality\n"
      "++ !=                 Inequality\n"
      "++ <=                 Less than or Equal\n"
      "++ <                  Less than\n"
      "++ >=                 Greater than or Equal\n"
      "++ >                  Greater than\n"
      "++ !                  Logical Negation\n"
      "++ ~                  Bitwise Compliment\n\n"
      " Operator precedence is consistent with C language precedence.\n\n"
      " Expression can contain arbitrary combinations of constant\n"
      " values, simulator registers and environment variables \n"
      "5Examples:\n"
      "++SET ENV -A A=7+2\n"
      "++SET ENV -A A=A-1\n"
      "++ECHO A=%%A%%\n"
      "++A=8\n"
      "4Gathering Input From A User\n"
      " Input from a user can be obtained by:\n\n"
      "+set environment -P \"Prompt String\" name=default\n\n"
      " The -P switch indicates that the user should be prompted\n"
      " with the indicated prompt string and the input provided\n"
      " will be saved in the environment variable 'name'.  If no\n"
      " input is provided, the value specified as 'default' will be\n"
      " used.\n"
#define HLP_SET_ON      "*Commands SET Command_Status_Trap_Dispatching"
      "3Command Status Trap Dispatching\n"
      "+SET ON                      enables error checking after command\n"
      "++++++++                     execution\n"
      "+SET NOON                    disables error checking after command\n"
      "++++++++                     execution\n"
      "+SET ON INHERIT              enables inheritance of ON state and\n"
      "++++++++                     actions into do command files\n"
      "+SET ON NOINHERIT            disables inheritance of ON state and\n"
      "++++++++                     actions into do command files\n"
#define HLP_SET_VERIFY "*Commands SET Command_Execution_Display"
#define HLP_SET_VERIFY "*Commands SET Command_Execution_Display"
      "3Command Execution Display\n"
      "+SET VERIFY                  re-enables display of command file\n"
      "++++++++                     processed commands\n"
      "+SET VERBOSE                 re-enables display of command file\n"
      "++++++++                     processed commands\n"
      "+SET NOVERIFY                disables display of command file processed\n"
      "++++++++                     commands\n"
      "+SET NOVERBOSE               disables display of command file processed\n"
      "++++++++                     commands\n"
#define HLP_SET_MESSAGE "*Commands SET Command_Error_Status_Display"
      "3Command Error Status Display\n"
      "+SET MESSAGE                 re-enables display of command file error\n"
      "++++++++                     messages\n"
      "+SET NOMESSAGE               disables display of command file error\n"
      "++++++++                     messages\n"
#define HLP_SET_QUIET "*Commands SET Command_Output_Display"
      "3Command Output Display\n"
      "+SET QUIET                   disables suppression of some output and\n"
      "++++++++                     messages\n"
      "+SET NOQUIET                 re-enables suppression of some output and\n"
      "++++++++                     messages\n"
#define HLP_SET_PROMPT "*Commands SET Command_Prompt"
      "3Command Prompt\n"
      "+SET PROMPT \"string\"        sets an alternate simulator prompt string\n"
      "3Device and Unit\n"
      "+SET <dev> OCT|DEC|HEX|BIN   set device display radix\n"
      "+SET <dev> ENABLED           enable device\n"
      "+SET <dev> DISABLED          disable device\n"
      "+SET <dev> DEBUG{=arg}       set device debug flags\n"
      "+SET <dev> NODEBUG={arg}     clear device debug flags\n"
      "+SET <dev> arg{,arg...}      set device parameters (see show modifiers)\n"
      "+SET <unit> ENABLED          enable unit\n"
      "+SET <unit> DISABLED         disable unit\n"
      "+SET <unit> arg{,arg...}     set unit parameters (see show modifiers)\n"
      "+HELP <dev> SET              displays the device specific set commands\n"
      "++++++++                     available\n"
       /***************** 80 character line width template *************************/
#define HLP_SHOW        "*Commands SHOW"
      "2SHOW\n"
      "+sh{ow} {-c} br{eak} <list>  show breakpoints\n"
      "+sh{ow} con{figuration}      show configuration\n"
      "+sh{ow} cons{ole} {arg}      show console options\n"
      "+sh{ow} {-ei} dev{ices}      show devices\n"
      "+sh{ow} fea{tures}           show system devices with descriptions\n"
      "+sh{ow} m{odifiers}          show modifiers for all devices\n" 
      "+sh{ow} s{how}               show SHOW commands for all devices\n" 
      "+sh{ow} n{ames}              show logical names\n"
      "+sh{ow} q{ueue}              show event queue\n"
      "+sh{ow} ti{me}               show simulated time\n"
      "+sh{ow} th{rottle}           show simulation rate\n"
      "+sh{ow} a{synch}             show asynchronouse I/O state\n" 
      "+sh{ow} ve{rsion}            show simulator version\n"
      "+sh{ow} def{ault}            show current directory\n" 
      "+sh{ow} re{mote}             show remote console configuration\n" 
      "+sh{ow} <dev> RADIX          show device display radix\n"
      "+sh{ow} <dev> DEBUG          show device debug flags\n"
      "+sh{ow} <dev> MODIFIERS      show device modifiers\n"
      "+sh{ow} <dev> NAMES          show device logical name\n"
      "+sh{ow} <dev> SHOW           show device SHOW commands\n"
      "+sh{ow} <dev> {arg,...}      show device parameters\n"
      "+sh{ow} <unit> {arg,...}     show unit parameters\n"
      "+sh{ow} ethernet             show ethernet devices\n"
      "+sh{ow} serial               show serial devices\n"
      "+sh{ow} multiplexer {dev}    show open multiplexer device info\n"
#if defined(USE_SIM_VIDEO)
      "+sh{ow} video                show video capabilities\n"
#endif
      "+sh{ow} clocks               show calibrated timer information\n"
      "+sh{ow} throttle             show throttle info\n"
      "+sh{ow} on                   show on condition actions\n"
      "+h{elp} <dev> show           displays the device specific show commands\n"
      "++++++++                     available\n"
#define HLP_SHOW_CONFIG         "*Commands SHOW"
#define HLP_SHOW_DEVICES        "*Commands SHOW"
#define HLP_SHOW_FEATURES       "*Commands SHOW"
#define HLP_SHOW_QUEUE          "*Commands SHOW"
#define HLP_SHOW_TIME           "*Commands SHOW"
#define HLP_SHOW_MODIFIERS      "*Commands SHOW"
#define HLP_SHOW_NAMES          "*Commands SHOW"
#define HLP_SHOW_SHOW           "*Commands SHOW"
#define HLP_SHOW_VERSION        "*Commands SHOW"
#define HLP_SHOW_DEFAULT        "*Commands SHOW"
#define HLP_SHOW_CONSOLE        "*Commands SHOW"
#define HLP_SHOW_REMOTE         "*Commands SHOW"
#define HLP_SHOW_BREAK          "*Commands SHOW"
#define HLP_SHOW_LOG            "*Commands SHOW"
#define HLP_SHOW_DEBUG          "*Commands SHOW"
#define HLP_SHOW_THROTTLE       "*Commands SHOW"
#define HLP_SHOW_ASYNCH         "*Commands SHOW"
#define HLP_SHOW_ETHERNET       "*Commands SHOW"
#define HLP_SHOW_SERIAL         "*Commands SHOW"
#define HLP_SHOW_MULTIPLEXER    "*Commands SHOW"
#define HLP_SHOW_VIDEO          "*Commands SHOW"
#define HLP_SHOW_CLOCKS         "*Commands SHOW"
#define HLP_SHOW_ON             "*Commands SHOW"
#define HLP_SHOW_SEND           "*Commands SHOW"
#define HLP_SHOW_EXPECT         "*Commands SHOW"
#define HLP_HELP                "*Commands HELP"
       /***************** 80 character line width template *************************/
      "2HELP\n"
      "+h{elp}                      type this message\n"
      "+h{elp} <command>            type help for command\n" 
      "+h{elp} <dev>                type help for device\n"
      "+h{elp} <dev> registers      type help for device register variables\n"
      "+h{elp} <dev> attach         type help for device specific ATTACH command\n"
      "+h{elp} <dev> set            type help for device specific SET commands\n"
      "+h{elp} <dev> show           type help for device specific SHOW commands\n"
      "+h{elp} <dev> <command>      type help for device specific <command> command\n"
       /***************** 80 character line width template *************************/
      "2Altering The Simulated Configuration\n"
      " In most simulators, the SET <device> DISABLED command removes the\n"
      " specified device from the configuration.  A DISABLED device is invisible\n"
      " to running programs.  The device can still be RESET, but it cannot be\n"
      " ATTAChed, DETACHed, or BOOTed.  SET <device> ENABLED restores a disabled\n"
      " device to a configuration.\n\n"
      " Most multi-unit devices allow units to be enabled or disabled:\n\n"
      "++SET <unit> ENABLED\n"
      "++SET <unit> DISABLED\n\n"
      " When a unit is disabled, it will not be displayed by SHOW DEVICE.\n\n"
#define HLP_ASSIGN      "*Commands Logical_Names"
#define HLP_DEASSIGN    "*Commands Logical_Names"
      "2Logical Names\n"
      " The standard device names can be supplemented with logical names.  Logical\n"
      " names must be unique within a simulator (that is, they cannot be the same\n"
      " as an existing device name).  To assign a logical name to a device:\n\n"
      "++ASSIGN <device> <log-name>      assign log-name to device\n\n"
      " To remove a logical name:\n\n"
      "++DEASSIGN <device>               remove logical name\n\n"
      " To show the current logical name assignment:\n\n"
      "++SHOW <device> NAMES            show logical name, if any\n\n"
      " To show all logical names:\n\n"
      "++SHOW NAMES\n\n"
       /***************** 80 character line width template *************************/
#define HLP_DO          "*Commands Executing_Command_Files"
      "2Executing Command Files\n"
      " The simulator can execute command files with the DO command:\n\n"
      "++DO <filename> {arguments...}       execute commands in file\n\n"
      " The DO command allows command files to contain substitutable arguments.\n"
      " The string %%n, where n is between 1 and 9, is replaced with argument n\n"
      " from the DO command line. The string %%0 is replaced with <filename>.\n"
      " The string %%* is replaced by the whole set of arguments (%%1 ... %%9).\n"
      " The sequences \\%% and \\\\ are replaced with the literal characters %% and \\,\n"
      " respectively.  Arguments with spaces can be enclosed in matching single\n"
      " or double quotation marks.\n\n"
      " DO commands may be nested up to ten invocations deep.\n\n"
      "3Switches\n"
      " If the switch -v is specified, the commands in the file are echoed before\n"
      " they are executed.\n\n"
      " If the switch -e is specified, command processing (including nested command\n"
      " invocations) will be aborted if a command error is encountered.\n"
      " (Simulation stop never abort processing; use ASSERT to catch unexpected\n"
      " stops.)  Without the switch, all errors except ASSERT failures will be\n"
      " ignored, and command processing will continue.\n\n"
      " If the switch -o is specified, the on conditions and actions from the\n"
      " calling command file will be inherited in the command file being invoked.\n"
      " If the switch -q is specified, the quiet mode will be explicitly enabled\n"
      " for the called command file, otherwise quiet mode is inherited from the\n"
      " calling context.\n"
       /***************** 80 character line width template *************************/
      "3Variable_Insertion\n"
      " Built In variables %%DATE%%, %%TIME%%, %%DATETIME%%, %%LDATE%%, %%LTIME%%,\n"
      " %%CTIME%%, %%DATE_YYYY%%, %%DATE_YY%%, %%DATE_YC%%, %%DATE_MM%%, %%DATE_MMM%%,\n"
      " %%DATE_MONTH%%, %%DATE_DD%%, %%DATE_D%%, %%DATE_WYYYY%%, %%DATE_WW%%,\n"
      " %%TIME_HH%%, %%TIME_MM%%, %%TIME_SS%%, %%TIME_MSEC%%, %%STATUS%%, %%TSTATUS%%,\n"
      " %%SIM_VERIFY%%, %%SIM_QUIET%%, %%SIM_MESSAGE%% %%SIM_MESSAGE%%\n"
      " %%SIM_NAME%%, %%SIM_BIN_NAME%%, %%SIM_BIN_PATH%%m %%SIM_OSTYPE%%\n\n"
      "+Token %%0 expands to the command file name.\n"
      "+Token %%n (n being a single digit) expands to the n'th argument\n"
      "+Token %%* expands to the whole set of arguments (%%1 ... %%9)\n\n"
      "+The input sequence \"%%%%\" represents a literal \"%%\".  All other\n"
      "+character combinations are rendered literally.\n\n"
      "+Omitted parameters result in null-string substitutions.\n\n"
      "+Tokens preceeded and followed by %% characters are expanded as environment\n"
      "+variables, and if an environment variable isn't found then it can be one of\n"
      "+several special variables:\n\n"
      "++%%DATE%%              yyyy-mm-dd\n"
      "++%%TIME%%              hh:mm:ss\n"
      "++%%DATETIME%%          yyyy-mm-ddThh:mm:ss\n"
      "++%%LDATE%%             mm/dd/yy (Locale Formatted)\n"
      "++%%LTIME%%             hh:mm:ss am/pm (Locale Formatted)\n"
      "++%%CTIME%%             Www Mmm dd hh:mm:ss yyyy (Locale Formatted)\n"
      "++%%UTIME%%             nnnn (Unix time - seconds since 1/1/1970)\n"
      "++%%DATE_YYYY%%         yyyy        (0000-9999)\n"
      "++%%DATE_YY%%           yy          (00-99)\n"
      "++%%DATE_MM%%           mm          (01-12)\n"
      "++%%DATE_MMM%%          mmm         (JAN-DEC)\n"
      "++%%DATE_MONTH%%        month       (January-December)\n"
      "++%%DATE_DD%%           dd          (01-31)\n"
      "++%%DATE_WW%%           ww          (01-53)     ISO 8601 week number\n"
      "++%%DATE_WYYYY%%        yyyy        (0000-9999) ISO 8601 week year number\n"
      "++%%DATE_D%%            d           (1-7)       ISO 8601 day of week\n"
      "++%%DATE_JJJ%%          jjj         (001-366) day of year\n"
      "++%%DATE_19XX_YY%%      yy          A year prior to 2000 with the same\n"
      "++++++++++   calendar days as the current year\n"
      "++%%DATE_19XX_YYYY%%    yyyy        A year prior to 2000 with the same\n"
      "++++++++++   calendar days as the current year\n"
      "++%%TIME_HH%%           hh          (00-23)\n"
      "++%%TIME_MM%%           mm          (00-59)\n"
      "++%%TIME_SS%%           ss          (00-59)\n"
      "++%%TIME_MSEC%%         msec        (000-999)\n"
      "++%%STATUS%%            Status value from the last command executed\n"
      "++%%TSTATUS%%           The text form of the last status value\n"
      "++%%SIM_VERIFY%%        The Verify/Verbose mode of the current Do command file\n"
      "++%%SIM_VERBOSE%%       The Verify/Verbose mode of the current Do command file\n"
      "++%%SIM_QUIET%%         The Quiet mode of the current Do command file\n"
      "++%%SIM_MESSAGE%%       The message display status of the current Do command file\n"
      "++%%SIM_NAME%%          The name of the current simulator\n"
      "++%%SIM_BIN_NAME%%      The program name of the current simulator\n"
      "++%%SIM_BIN_PATH%%      The program path that invoked the current simulator\n"
      "++%%SIM_OSTYPE%%        The Operating System running the current simulator\n\n"
      "+Environment variable lookups are done first with the precise name between\n"
      "+the %% characters and if that fails, then the name between the %% characters\n"
      "+is upcased and a lookup of that valus is attempted.\n\n"
      "+The first Space delimited token on the line is extracted in uppercase and\n"
      "+then looked up as an environment variable.  If found it the value is\n"
      "+supstituted for the original string before expanding everything else.  If\n"
      "+it is not found, then the original beginning token on the line is left\n"
      "+untouched.\n\n"
      "+Environment variable string substitution:\n\n"
      "++%%XYZ:str1=str2%%\n\n"
      "+would expand the XYZ environment variable, substituting each occurrence\n"
      "+of \"str1\" in the expanded result with \"str2\".  \"str2\" can be the empty\n"
      "+string to effectively delete all occurrences of \"str1\" from the expanded\n"
      "+output.  \"str1\" can begin with an asterisk, in which case it will match\n"
      "+everything from the beginning of the expanded output to the first\n"
      "+occurrence of the remaining portion of str1.\n\n"
      "+May also specify substrings for an expansion.\n\n"
      "++%%XYZ:~10,5%%\n\n"
      "+would expand the XYZ environment variable, and then use only the 5\n"
      "+characters that begin at the 11th (offset 10) character of the expanded\n"
      "+result.  If the length is not specified, then it defaults to the\n"
      "+remainder of the variable value.  If either number (offset or length) is\n"
      "+negative, then the number used is the length of the environment variable\n"
      "+value added to the offset or length specified.\n\n"
      "++%%XYZ:~-10%%\n\n"
      "+would extract the last 10 characters of the XYZ variable.\n\n"
      "++%%XYZ:~0,-2%%\n\n"
      "+would extract all but the last 2 characters of the XYZ variable.\n"
#define HLP_GOTO        "*Commands Executing_Command_Files GOTO"
      "3GOTO\n"
      " Commands in a command file execute in sequence until either an error\n"
      " trap occurs (when a command completes with an error status), or when an\n"
      " explict request is made to start command execution elsewhere with the\n"
      " GOTO command:\n\n"
      "++GOTO <label>\n\n"
      " Labels are lines in a command file which the first non whitespace\n"
      " character is a \":\".  The target of a goto is the first matching label\n"
      " in the current do command file which is encountered.  Since labels\n"
      " don't do anything else besides being the targets of goto's, they could\n"
      " also be used to provide comments in do command files.\n\n"
      "4Examples\n\n"
      "++:: This is a comment\n"
      "++echo Some Message to Output\n"
      "++:Target\n"
      "++:: This is a comment\n"
      "++GOTO Target\n\n"
#define HLP_RETURN      "*Commands Executing_Command_Files RETURN"
       /***************** 80 character line width template *************************/
      "3RETURN\n"
      " The RETURN command causes the current procedure call to be restored to the\n"
      " calling context, possibly returning a specific return status.\n"
      " If no return status is specified, the return status from the last command\n"
      " executed will be returned.  The calling context may have ON traps defined\n"
      " which may redirect command flow in that context.\n\n"
      "++return                   return from command file with last command status\n"
      "++return {-Q} <status>     return from command file with specific status\n\n"
      " The status return can be any numeric value or one of the standard SCPE_\n"
      " condition names.\n\n"
      " The -Q switch on the RETURN command will cause the specified status to\n"
      " be returned, but normal error status message printing to be suppressed.\n"
      "4Condition Names\n"
      " The available standard SCPE_ condition names are\n"
      "5 NXM\n"
      " Address space exceeded\n"
      "5 UNATT\n"
      " Unit not attached\n"
      "5 IOERR\n"
      " I/O error\n"
      "5 CSUM\n"
      " Checksum error\n"
      "5 FMT\n"
      " Format error\n"
      "5 NOATT\n"
      " Unit not attachable\n"
      "5 OPENERR\n"
      " File open error\n"
      "5 MEM\n"
      " Memory exhausted\n"
      "5 ARG\n"
      " Invalid argument\n"
      "5 STEP\n"
      " Step expired\n"
      "5 UNK\n"
      " Unknown command\n"
      "5 RO\n"
      " Read only argument\n"
      "5 INCOMP\n"
      " Command not completed\n"
      "5 STOP\n"
      " Simulation stopped\n"
      "5 EXIT\n"
      " Goodbye\n"
      "5 TTIERR\n"
      " Console input I/O error\n"
      "5 TTOERR\n"
      " Console output I/O error\n"
      "5 EOF\n"
      " End of file\n"
      "5 REL\n"
      " Relocation error\n"
      "5 NOPARAM\n"
      " No settable parameters\n"
      "5 ALATT\n"
      " Unit already attached\n"
      "5 TIMER\n"
      " Hardware timer error\n"
      "5 SIGERR\n"
      " Signal handler setup error\n"
      "5 TTYERR\n"
      " Console terminal setup error\n"
      "5 NOFNC\n"
      " Command not allowed\n"
      "5 UDIS\n"
      " Unit disabled\n"
      "5 NORO\n"
      " Read only operation not allowed\n"
      "5 INVSW\n"
      " Invalid switch\n"
      "5 MISVAL\n"
      " Missing value\n"
      "5 2FARG\n"
      " Too few arguments\n"
      "5 2MARG\n"
      " Too many arguments\n"
      "5 NXDEV\n"
      " Non-existent device\n"
      "5 NXUN\n"
      " Non-existent unit\n"
      "5 NXREG\n"
      " Non-existent register\n"
      "5 NXPAR\n"
      " Non-existent parameter\n"
      "5 NEST\n"
      " Nested DO command limit exceeded\n"
      "5 IERR\n"
      " Internal error\n"
      "5 MTRLNT\n"
      " Invalid magtape record length\n"
      "5 LOST\n"
      " Console Telnet connection lost\n"
      "5 TTMO\n"
      " Console Telnet connection timed out\n"
      "5 STALL\n"
      " Console Telnet output stall\n"
      "5 AFAIL\n"
      " Assertion failed\n"
      "5 INVREM\n"
      " Invalid remote console command\n"
      "5 NOTATT\n"
      " Not attached \n"
      "5 AMBREG\n"
      " Ambiguous register\n"
#define HLP_SHIFT       "*Commands Executing_Command_Files SHIFT"
      "3SHIFT\n"
      "++shift                    shift the command file's positional parameters\n"
#define HLP_CALL        "*Commands Executing_Command_Files CALL"
      "3CALL\n"
      "++call                     transfer control to a labeled subroutine\n"
      "                         a command file.\n"
       /***************** 80 character line width template *************************/
#define HLP_ON          "*Commands Executing_Command_Files Error_Trapping"
      "3Error Trapping\n"
      " Error traps can be taken when any command returns a non success status.\n"
      " Actions to be performed for particular status returns are specified with\n"
      " the ON command.\n"
      "4Enabling Error Traps\n"
      " Error trapping is enabled with:\n\n"
      "++set on                   enable error traps\n"
      "4Disabling Error Traps\n"
      " Error trapping is disabled with:\n\n"
      "++set noon                 disable error traps\n"
      "4ON\n"
      " To set the action(s) to take when a specific error status is returned by\n"
      " a command in the currently running do command file:\n\n"
      "++on <statusvalue> commandtoprocess{; additionalcommandtoprocess}\n\n"
      " To clear the action(s) taken take when a specific error status is returned:\n\n"
      "++on <statusvalue>\n\n"
      " To set the default action(s) to take when any otherwise unspecified error\n"
      " status is returned by a command in the currently running do command file:\n\n"
      "++on error commandtoprocess{; additionalcommandtoprocess}\n\n"
      " To clear the default action(s) taken when any otherwise unspecified error\n"
      " status is returned:\n\n"
      "++on error\n"
      "5Parameters\n"
      " Error traps can be taken for any command which returns a status other\n"
      " than SCPE_STEP, SCPE_OK, and SCPE_EXIT.\n\n"
      " ON Traps can specify any of these status values:\n\n"
      "++NXM, UNATT, IOERR, CSUM, FMT, NOATT, OPENERR, MEM, ARG,\n"
      "++STEP, UNK, RO, INCOMP, STOP, TTIERR, TTOERR, EOF, REL,\n"
      "++NOPARAM, ALATT, TIMER, SIGERR, TTYERR, SUB, NOFNC, UDIS,\n"
      "++NORO, INVSW, MISVAL, 2FARG, 2MARG, NXDEV, NXUN, NXREG,\n"
      "++NXPAR, NEST, IERR, MTRLNT, LOST, TTMO, STALL, AFAIL,\n"
      "++NOTATT, AMBREG\n\n"
      " These values can be indicated by name or by their internal\n"
      " numeric value (not recommended).\n"
      /***************** 80 character line width template *************************/
      "3CONTROL-C Trapping\n"
      " A special ON trap is available to describe action(s) to be taken\n"
      " when CONTROL_C (aka SIGINT) occurs during the execution of\n"
      " simh commands and/or command procedures.\n\n"
      "++on CONTROL_C <action>    perform action(s) after CTRL+C\n"
      "++on CONTROL_C             restore default CTRL+C action\n\n"
      " The default ON CONTROL_C handler will exit nested DO command\n"
      " procedures and return to the sim> prompt.\n\n"
      " Note 1: When a simulator is executing instructions entering CTRL+C\n"
      "+will cause the CNTL+C character to be delivered to the simulator as\n"
      "+input.  The simulator instruction execution can be stopped by entering\n"
      "+the WRU character (usually CTRL+E).  Once instruction execution has\n"
      "+stopped, CTRL+C can be entered and potentially acted on by the\n"
      "+ON CONTROL_C trap handler.\n"
      " Note 2: The ON CONTROL_C trapping is not affected by the SET ON and\n"
      "+SET NOON commands.\n"
#define HLP_PROCEED     "*Commands Executing_Command_Files PROCEED"
#define HLP_IGNORE      "*Commands Executing_Command_Files PROCEED"
       /***************** 80 character line width template *************************/
      "3PROCEED/IGNORE\n"
      " The PROCEED or IGNORE commands do nothing.  They are potentially useful\n"
      " placeholders for an ON action condition which should be explicitly ignored\n"
      "++proceed                  continue command file execution without doing anything\n"
      "++ignore                   continue command file execution without doing anything\n"
      "3DO Command Processing Interactions With ASSERT\n"
      " The command:\n\n"
      "++DO -e commandfile\n\n"
      " is equivalent to starting the invoked command file with:\n\n"
      "++SET ON\n\n"
      " which by itself it equivalent to:\n\n"
      "++SET ON\n"
      "++ON ERROR RETURN\n\n"
      " ASSERT failures have several different actions:\n\n"
      "+*   If error trapping is not enabled then AFAIL causes exit from the\n"
      "++current do command file.\n"
      "+*   If error trapping is enabled and an explicit \"ON AFAIL\" action\n"
      "++is defined, then the specified action is performed.\n"
      "+*   If error trapping is enabled and no \"ON AFAIL\" action is defined,\n"
      "++then an AFAIL causes exit from the current do command file.\n"
#define HLP_ECHO        "*Commands Executing_Command_Files Displaying_Arbitrary_Text ECHO_Command"
       /***************** 80 character line width template *************************/
      "3Displaying Arbitrary Text\n"
      " The ECHO and ECHOF commands are useful ways of annotating command files.\n\n"
      "4ECHO command\n"
      " The ECHO command prints out its arguments on the console (and log)\n"
      " followed by a newline:\n\n"
      "++ECHO <string>      output string to console\n\n"
      " If there is no argument, ECHO prints a blank line on the console.  This\n"
      " may be used to provide spacing in the console display or log.\n"
       /***************** 80 character line width template *************************/
#define HLP_ECHOF       "*Commands Executing_Command_Files Displaying_Arbitrary_Text ECHOF_Command"
       /***************** 80 character line width template *************************/
      "4ECHOF command\n"
      " The ECHOF command prints out its arguments on the console (and log)\n"
      " followed by a newline:\n\n"
       /***************** 80 character line width template *************************/
      "++ECHOF {-n} \"<string>\"|<string>   output string to console\n\n"
      " The ECHOF command can also print output on a specified multiplexer line\n"
      " (and log) followed by a newline:\n\n"
      "++ECHOF {-n} dev:line \"<string>\"|<string>   output string to specified line\n\n"
      " If there is no argument, ECHOF prints a blank line.\n"
      " The string argument may be delimited by quote characters.  Quotes may\n"
      " be either single or double but the opening and closing quote characters\n"
      " must match.  If the string is enclosed in quotes, the string may\n"
      " contain escaped character strings which is interpreted as described\n"
      " in Quoted_String_Data and the resulting string is output.\n\n"
      " A command alias can be used to replace the ECHO command with the ECHOF\n"
      " command:\n\n"
      "++sim> SET ENV ECHO=ECHOF\n"
      "5Switches\n"
      " Switches can be used to influence the behavior of ECHOF commands\n\n"
      "6-n\n"
      " The -n switch indicates that the supplied string should be output\n"
      " without a newline after the string is written.\n"
      "5Quoted String Data\n"
      " String data enclosed in quotes is transformed interpreting character\n"
      " escapes.  The following character escapes are explicitly supported:\n"
      "++\\r  Sends the ASCII Carriage Return character (Decimal value 13)\n"
      "++\\n  Sends the ASCII Linefeed character (Decimal value 10)\n"
      "++\\f  Sends the ASCII Formfeed character (Decimal value 12)\n"
      "++\\t  Sends the ASCII Horizontal Tab character (Decimal value 9)\n"
      "++\\v  Sends the ASCII Vertical Tab character (Decimal value 11)\n"
      "++\\b  Sends the ASCII Backspace character (Decimal value 8)\n"
      "++\\\\  Sends the ASCII Backslash character (Decimal value 92)\n"
      "++\\'  Sends the ASCII Single Quote character (Decimal value 39)\n"
      "++\\\"  Sends the ASCII Double Quote character (Decimal value 34)\n"
      "++\\?  Sends the ASCII Question Mark character (Decimal value 63)\n"
      "++\\e  Sends the ASCII Escape character (Decimal value 27)\n"
      " as well as octal character values of the form:\n"
      "++\\n{n{n}} where each n is an octal digit (0-7)\n"
      " and hext character values of the form:\n"
      "++\\xh{h} where each h is a hex digit (0-9A-Fa-f)\n"
       /***************** 80 character line width template *************************/
#define HLP_SEND        "*Commands Executing_Command_Files Injecting_Console_Input"
       /***************** 80 character line width template *************************/
      "3Injecting Console Input\n"
      " The SEND command provides a way to insert input into the console device of\n"
      " a simulated system as if it was entered by a user.\n\n"
      "++SEND {-t} {after=nn,}{delay=nn,}\"<string>\"\n\n"
      "++NOSEND\n\n"
      "++SHOW SEND\n\n"
      " The string argument must be delimited by quote characters.  Quotes may\n"
      " be either single or double but the opening and closing quote characters\n"
      " must match.  Data in the string may contain escaped character strings.\n\n"
      " The SEND command can also insert input into any serial device on a\n"
      " simulated system as if it was entered by a user.\n\n"
      "++SEND {-t} {<dev>:line} {after=nn,}{delay=nn,}\"<string>\"\n\n"
      "++NOSEND {<dev>:line}\n\n"
      "++SHOW SEND {<dev>:line}\n\n"
      " The NOSEND command removes any undelivered input data which may be\n"
      " pending on the CONSOLE or a specific multiplexer line.\n\n"
      " The SHOW SEND command displays any pending SEND activity for the\n"
      " CONSOLE or a specific multiplexer line.\n"
      "4Delay\n"
      " Specifies an integer (>=0) representing a minimal instruction delay\n"
      " between characters being sent.  The delay parameter can be set by\n"
      " itself with:\n\n"
      "++SEND DELAY=n\n\n"
      " which will set the default delay value for subsequent SEND commands\n"
      " which don't specify an explicit DELAY parameter along with a string\n"
      " If a SEND command is processed and no DELAY value has been specified,\n"
      " the default value of the delay parameter is 1000.\n"
       /***************** 80 character line width template *************************/
      "4After\n"
      " Specifies an integer (>=0) representing a minimal number of instructions\n"
      " which must execute before the first character in the string is sent.\n"
      " The after parameter value can be set by itself with:\n\n"
      "++SEND AFTER=n\n\n"
      " which will set the default after value for subsequent SEND commands\n"
      " which don't specify an explicit AFTER parameter along with a string\n"
      " If a SEND command is processed and no AFTER value has been specified,\n"
      " the default value of the delay parameter is the DELAY parameter value.\n"
      "4Escaping String Data\n"
      " The following character escapes are explicitly supported:\n"
      "++\\r  Sends the ASCII Carriage Return character (Decimal value 13)\n"
      "++\\n  Sends the ASCII Linefeed character (Decimal value 10)\n"
      "++\\f  Sends the ASCII Formfeed character (Decimal value 12)\n"
      "++\\t  Sends the ASCII Horizontal Tab character (Decimal value 9)\n"
      "++\\v  Sends the ASCII Vertical Tab character (Decimal value 11)\n"
      "++\\b  Sends the ASCII Backspace character (Decimal value 8)\n"
      "++\\\\  Sends the ASCII Backslash character (Decimal value 92)\n"
      "++\\'  Sends the ASCII Single Quote character (Decimal value 39)\n"
      "++\\\"  Sends the ASCII Double Quote character (Decimal value 34)\n"
      "++\\?  Sends the ASCII Question Mark character (Decimal value 63)\n"
      "++\\e  Sends the ASCII Escape character (Decimal value 27)\n"
      " as well as octal character values of the form:\n"
      "++\\n{n{n}} where each n is an octal digit (0-7)\n"
      " and hext character values of the form:\n"
      "++\\xh{h} where each h is a hex digit (0-9A-Fa-f)\n"
      "4Switches\n"
      " Switches can be used to influence the behavior of SEND commands\n\n"
      "5-t\n"
      " The -t switch indicates that the Delay and After values are in\n"
      " units of microseconds rather than instructions.\n"
       /***************** 80 character line width template *************************/
#define HLP_EXPECT      "*Commands Executing_Command_Files Reacting_To_Console_Output"
       /***************** 80 character line width template *************************/
      "3Reacting To Console Output\n"
      " The EXPECT command provides a way to stop execution and take actions\n"
      " when specific output has been generated by the simulated system.\n\n"
      "++EXPECT {dev:line} {[count]} {HALTAFTER=n,}\"<string>\" {actioncommand {; actioncommand}...}\n\n"
      "++NOEXPECT {dev:line} \"<string>\"\n\n"
      "++SHOW EXPECT {dev:line}\n\n"
      " The string argument must be delimited by quote characters.  Quotes may\n"
      " be either single or double but the opening and closing quote characters\n"
      " must match.  Data in the string may contain escaped character strings.\n"
      " If a [count] is specified, the rule will match after the match string\n"
      " has matched count times.\n\n"
      " When multiple expect rules are defined with the same match string, they\n"
      " will match in the same order they were defined in.\n\n"
      " When expect rules are defined, they are evaluated agains recently\n"
      " produced output as each character is output to the device.  Since this\n"
      " evaluation processing is done on each output character, rule matching\n"
      " is not specifically line oriented.  If line oriented matching is desired\n"
      " then rules should be defined which contain the simulated system's line\n"
      " ending character sequence (i.e. \"\\r\\n\").\n"
      " Once data has matched any expect rule, that data is no longer eligible\n"
      " to match other expect rules which may already be defined.\n"
      " Data which is output prior to the definition of an expect rule is not\n"
      " eligible to be matched against.\n\n"
      " The NOEXPECT command removes a previously defined EXPECT command for the\n"
      " console or a specific multiplexer line.\n\n"
      " The SHOW EXPECT command displays all of the pending EXPECT state for\n"
      " the console or a specific multiplexer line.\n"
       /***************** 80 character line width template *************************/
      "4Switches\n"
      " Switches can be used to influence the behavior of EXPECT rules\n\n"
      "5-p\n"
      " EXPECT rules default to be one shot activities.  That is a rule is\n"
      " automatically removed when it matches unless it is designated as a\n"
      " persistent rule by using a -p switch when the rule is defined.\n"
      "5-c\n"
      " If an expect rule is defined with the -c switch, it will cause all\n"
      " pending expect rules on the current device to be cleared when the rule\n"
      " matches data in the device output stream.\n"
      "5-r\n"
      " If an expect rule is defined with the -r switch, the string is interpreted\n"
      " as a regular expression applied to the output data stream.  This regular\n"
      " expression may contain parentheses delimited sub-groups.\n\n"
       /***************** 80 character line width template *************************/
#if defined (HAVE_PCREPOSIX_H)
      " The syntax of the regular expressions available are those supported by\n"
      " the Perl Compatible Regular Expression package (aka PCRE).  As the name\n"
      " implies, the syntax is generally the same as Perl regular expressions.\n"
      " See http://perldoc.perl.org/perlre.html for more details\n"
#elif defined (HAVE_REGEX_H)
      " The syntax of the regular expressions available are those supported by\n"
      " your local system's Regular Expression library using the Extended POSIX\n"
      " Regular Expressiona\n"
#else
      " Regular expression support is not currently available on your environment.\n"
      " This simulator could use regular expression support provided by the\n"
      " Perl Compatible Regular Expression (PCRE) package if it was available\n"
      " when you simulator was compiled.\n"
#endif
      "5-i\n"
      " If a regular expression expect rule is defined with the -i switch,\n"
      " character matching for that expression will be case independent.\n"
      " The -i switch is only valid for regular expression expect rules.\n"
      "5-t\n"
      " The -t switch indicates that the value specified by the HaltAfter\n"
      " parameter are in units of microseconds rather than instructions.\n"
      "4Determining Which Output Matched\n"
      " When an expect rule matches data in the output stream, the rule which\n"
      " matched is recorded in the environment variable _EXPECT_MATCH_PATTERN.\n"
      " If the expect rule was a regular expression rule, then the environment\n"
      " variable _EXPECT_MATCH_GROUP_0 is set to the whole string which matched\n"
      " and if the match pattern had any parentheses delimited sub-groups, the\n"
      " environment variables _EXPECT_MATCH_PATTERN_1 thru _EXPECT_MATCH_PATTERN_n\n"
      " are set to the values within the string which matched the respective\n"
      " sub-groups.\n"
       /***************** 80 character line width template *************************/
      "4Escaping String Data\n"
      " The following character escapes are explicitly supported when NOT using\n"
      " regular expression match patterns:\n"
      "++\\r  Expect the ASCII Carriage Return character (Decimal value 13)\n"
      "++\\n  Expect the ASCII Linefeed character (Decimal value 10)\n"
      "++\\f  Expect the ASCII Formfeed character (Decimal value 12)\n"
      "++\\t  Expect the ASCII Horizontal Tab character (Decimal value 9)\n"
      "++\\v  Expect the ASCII Vertical Tab character (Decimal value 11)\n"
      "++\\b  Expect the ASCII Backspace character (Decimal value 8)\n"
      "++\\\\  Expect the ASCII Backslash character (Decimal value 92)\n"
      "++\\'  Expect the ASCII Single Quote character (Decimal value 39)\n"
      "++\\\"  Expect the ASCII Double Quote character (Decimal value 34)\n"
      "++\\?  Expect the ASCII Question Mark character (Decimal value 63)\n"
      "++\\e  Expect the ASCII Escape character (Decimal value 27)\n"
      " as well as octal character values of the form:\n"
      "++\\n{n{n}} where each n is an octal digit (0-7)\n"
      " and hext character values of the form:\n"
      "++\\xh{h} where each h is a hex digit (0-9A-Fa-f)\n"
      "4HaltAfter\n"
      " Specifies the number of instructions which should be executed before\n"
      " simulator instruction execution should stop.  The default is to stop\n"
      " executing instructions immediately (i.e. HALTAFTER=0).\n"
      " The default HaltAfter delay, once set, persists for all expect behaviors\n"
      " for that device.\n"
      " The default HaltAfter parameter value can be set by itself with:\n\n"
      "++EXPECT HALTAFTER=n\n\n"
      " A unique HaltAfter value can be specified with each expect matching rule\n"
      " which if it is not specified then the default value will be used.\n"
      " To avoid potentially unpredictable system hehavior that will happen\n"
      " if multiple expect rules are in effect and a haltafter value is large\n"
      " enough for more than one expect rule to match before an earlier haltafter\n"
      " delay has expired, only a single EXPECT rule can be defined if a non-zero\n"
      " HaltAfter parameter has been set.\n"
      /***************** 80 character line width template *************************/
#define HLP_SLEEP       "*Commands Executing_Command_Files Pausing_Command_Execution"
      "3Pausing Command Execution\n"
      " A simulator command file may wait for a specific period of time with the\n\n"
      "++SLEEP NUMBER[SUFFIX]...\n\n"
      " Pause for NUMBER seconds.  SUFFIX may be 's' for seconds (the default),\n"
      " 'm' for minutes, 'h' for hours or 'd' for days.  NUMBER may be an\n"
      " arbitrary floating point number.  Given two or more arguments, pause\n"
      " for the amount of time specified by the sum of their values.\n"
      " NOTE: A SLEEP command is interruptable with SIGINT (CTRL+C).\n\n"
      /***************** 80 character line width template *************************/
#define HLP_ASSERT      "*Commands Executing_Command_Files Testing_Simulator_State"
#define HLP_IF          "*Commands Executing_Command_Files Testing_Simulator_State"
      "3Testing Simulator State\n"
      " There are two ways for a command file to examine simulator state and\n"
      " then take action based on that state:\n"
      "4ASSERT\n"
      " The ASSERT command tests a simulator state condition and halts command\n"
      " file execution if the condition is false:\n\n"
      "++ASSERT <Simulator State Expressions>\n\n"
      " If the indicated expression evaluates to false, the command completes\n"
      " with an AFAIL condition.  By default, when a command file encounters a\n"
      " command which returns the AFAIL condition, it will exit the running\n"
      " command file with the AFAIL status to the calling command file.  This\n"
      " behavior can be changed with the ON command as well as switches to the\n"
      " invoking DO command.\n\n"
      "5Examples:\n"
      " A command file might be used to bootstrap an operating system that\n"
      " halts after the initial load from disk.  The ASSERT command is then\n"
      " used to confirm that the load completed successfully by examining the\n"
      " CPU's \"A\" register for the expected value:\n\n"
      "++; OS bootstrap command file\n"
      "++;\n"
      "++IF EXIST \"os.disk\" echo os.disk exists\n"
      "++IF NOT EXIST os.disk echo os.disk not existing\n"
      "++ATTACH DS0 os.disk\n"
      "++BOOT DS\n"
      "++; A register contains error code; 0 = good boot\n"
      "++ASSERT A=0\n"
      "++ATTACH MT0 sys.tape\n"
      "++ATTACH MT1 user.tape\n"
      "++RUN\n\n"
       /***************** 80 character line width template *************************/
      " In the example, if the A register is not 0, the \"ASSERT A=0\" command will\n"
      " be echoed, the command file will be aborted with an \"Assertion failed\"\n"
      " message.  Otherwise, the command file will continue to bring up the\n"
      " operating system.\n"
      "4IF\n"
      " The IF command tests a simulator state condition and executes additional\n"
      " commands if the condition is true:\n\n"
      "++IF <Conditional Expressions> commandtoprocess{; additionalcommandtoprocess}...\n\n"
      "5Examples:\n"
      " A command file might be used to bootstrap an operating system that\n"
      " halts after the initial load from disk.  The ASSERT command is then\n"
      " used to confirm that the load completed successfully by examining the\n"
      " CPU's \"A\" register for the expected value:\n\n"
      "++; OS bootstrap command file\n"
      "++;\n"
      "++IF EXIST \"os.disk\" echo os.disk exists\n"
      "++IF NOT EXIST os.disk echo os.disk not existing\n"
      "++ATTACH DS0 os.disk\n"
      "++BOOT DS\n"
      "++; A register contains error code; 0 = good boot\n"
      "++IF NOT A=0 echo Boot failed - Failure Code; EX A; exit AFAIL\n"
      "++ATTACH MT0 sys.tape\n"
      "++ATTACH MT1 user.tape\n"
      "++RUN\n\n"
       /***************** 80 character line width template *************************/
      " In the example, if the A register is not 0, the message \"Boot failed -\n"
      " Failure Code:\" command will be displayed, the contents of the A register\n"
      " will be displayed and the command file will be aborted with an \"Assertion\n"
      " failed\" message.  Otherwise, the command file will continue to bring up\n"
      " the operating system.\n"
      "4Conditional Expressions\n"
      " The IF and ASSERT commands evaluate five different forms of conditional\n"
      " expressions.:\n\n"
      "5C Style Simulator State Expressions\n"

      " Comparisons can optionally be done with complete C style computational\n"
      " expressions which leverage the C operations in the below table and can\n"
      " optionally reference any combination of values that are constants or\n"
      " contained in environment variables or simulator registers.  C style\n"
      " expression evaluation is initiated by enclosing the expression in\n"
      " parenthesis.\n\n"
      " Expression can contain any of these C language operators:\n\n"
      "++ (                  Open Parenthesis\n"
      "++ )                  Close Parenthesis\n"
      "++ -                  Subtraction\n"
      "++ +                  Addition\n"
      "++ *                  Multiplication\n"
      "++ /                  Division\n"
      "++ %%                  Modulus\n"
      "++ &&                 Logical AND\n"
      "++ ||                 Logical OR\n"
      "++ &                  Bitwise AND\n"
      "++ |                  Bitwise Inclusive OR\n"
      "++ ^                  Bitwise Exclusive OR\n"
      "++ >>                 Bitwise Right Shift\n"
      "++ <<                 Bitwise Left Shift\n"
      "++ ==                 Equality\n"
      "++ !=                 Inequality\n"
      "++ <=                 Less than or Equal\n"
      "++ <                  Less than\n"
      "++ >=                 Greater than or Equal\n"
      "++ >                  Greater than\n"
      "++ !                  Logical Negation\n"
      "++ ~                  Bitwise Compliment\n\n"
      " Operator precedence is consistent with C language precedence.\n\n"
      " Expression can contain arbitrary combinations of constant\n"
      " values, simulator registers and environment variables \n"
      "5Simulator State Expressions\n"
      " The values of simulator registers can be evaluated with:\n\n"
      "++{NOT} {<dev>} <reg>|<addr>{<logical-op><value>}<conditional-op><value>\n\n"
      " If <dev> is not specified, CPU is assumed.  <reg> is a register (scalar\n"
      " or subscripted) belonging to the indicated device.  <addr> is an address\n"
      " in the address space of the indicated device.  The <conditional-op>\n"
      " and optional <logical-op> are the same as those used for \"search\n"
      " specifiers\" by the EXAMINE and DEPOSIT commands.  The <value>s are\n"
      " expressed in the radix specified for <reg>, not in the radix for the\n"
      " device when referencing a register and when an address is referenced\n"
      " the device radix is used as the default.\n\n"
      " If the <logical-op> and <value> are specified, the target register value\n"
      " is first altered as indicated.  The result is then compared to the\n"
      " <value> via the <conditional-op>.  If the result is true, the additional\n"
      " command(s) are executed before proceeding to the next line in the command\n"
      " file.  Otherwise, the next command in the command file is processed.\n\n"
      "5String Comparison Expressions\n"
      " String Values can be compared with:\n"
      "++{-i} {NOT} \"<string1>\"|EnVarName1 <compare-op> \"<string2>|EnvVarName2\"\n\n"
      " The -i switch, if present, causes comparisons to be case insensitive.\n"
      " <string1> and <string2> are quoted string values which may have\n"
      " environment variables substituted as desired.\n"
      " Either quoted string may alternatively be an environment variable name.\n"
      " <compare-op> may be one of:\n\n"
      "++==  - equal\n"
      "++EQU - equal\n"
      "++!=  - not equal\n"
      "++NEQ - not equal\n"
      "++<   - less than\n"
      "++LSS - less than\n"
      "++<=  - less than or equal\n"
      "++LEQ - less than or equal\n"
      "++>   - greater than\n"
      "++GTR - greater than\n"
      "++>=  - greater than or equal\n"
      "++GEQ - greater than or equal\n\n"
      " Comparisons are generic.  This means that if both string1 and string2 are\n"
      " comprised of all numeric digits, then the strings are converted to numbers\n"
      " and a numeric comparison is performed. For example: \"+1\" EQU \"1\" will be\n"
      " true.\n"
      "5File Existence Expressions\n"
      " File existence can be determined with:\n\n"
      "++{NOT} EXIST \"<filespec>\"\n\n"
      "++{NOT} EXIST <filespec>\n\n"
      " Specifies a true (false {NOT}) condition if the file exists.\n"
      "5File Comparison Expressions\n"
      " Files can have their contents compared with:\n\n"
      "++-D {NOT} \"<filespec1>\" == \"<filespec2>\" \n\n"
      " Specifies a true (false {NOT}) condition if the indicated files\n"
      " have the same contents.\n\n"
       /***************** 80 character line width template *************************/
#define HLP_EXIT        "*Commands Exiting_The_Simulator"
      "2Exiting The Simulator\n"
      " EXIT (synonyms QUIT and BYE) returns control to the operating system.\n"
       /***************** 80 character line width template *************************/
#define HLP_SCREENSHOT  "*Commands Screenshot_Video_Window"
      "2Screenshot Video Window\n"
      " Simulators with Video devices display the simulated video in a window\n"
      " on the local system.  The contents of that display can be saved in a\n"
      " file with the SCREENSHOT command:\n\n"
      " +SCREENSHOT screenshotfile\n\n"
#if defined(HAVE_LIBPNG)
      " which will create a screen shot file called screenshotfile.png\n"
#else
      " which will create a screen shot file called screenshotfile.bmp\n"
#endif
#define HLP_SPAWN       "*Commands Executing_System_Commands"
      "2Executing System Commands\n"
      " The simulator can execute operating system commands with the ! (spawn)\n"
      " command:\n\n"
      "++!                    execute local command interpreter\n"
      "++! <command>          execute local host command\n"
      " If no operating system command is provided, the simulator attempts to\n"
      " launch the host operating system's command shell.\n"
      " The exit status from the command which was executed is set as the command\n"
      " completion status for the ! command.  This may influence any enabled ON\n"
      " condition traps\n";


static CTAB cmd_table[] = {
    { "RESET",      &reset_cmd,     0,          HLP_RESET },
    { "EXAMINE",    &exdep_cmd,     EX_E,       HLP_EXAMINE },
    { "IEXAMINE",   &exdep_cmd,     EX_E+EX_I,  HLP_IEXAMINE },
    { "DEPOSIT",    &exdep_cmd,     EX_D,       HLP_DEPOSIT },
    { "IDEPOSIT",   &exdep_cmd,     EX_D+EX_I,  HLP_IDEPOSIT },
    { "EVALUATE",   &eval_cmd,      0,          HLP_EVALUATE },
    { "RUN",        &run_cmd,       RU_RUN,     HLP_RUN,        NULL, &run_cmd_message },
    { "GO",         &run_cmd,       RU_GO,      HLP_GO,         NULL, &run_cmd_message },
    { "STEP",       &run_cmd,       RU_STEP,    HLP_STEP,       NULL, &run_cmd_message },
    { "NEXT",       &run_cmd,       RU_NEXT,    HLP_NEXT,       NULL, &run_cmd_message },
    { "CONTINUE",   &run_cmd,       RU_CONT,    HLP_CONTINUE,   NULL, &run_cmd_message },
    { "BOOT",       &run_cmd,       RU_BOOT,    HLP_BOOT,       NULL, &run_cmd_message },
    { "BREAK",      &brk_cmd,       SSH_ST,     HLP_BREAK },
    { "NOBREAK",    &brk_cmd,       SSH_CL,     HLP_NOBREAK },
    { "DEBUG",      &debug_cmd,     1,          HLP_DEBUG},
    { "NODEBUG",    &debug_cmd,     0,          HLP_NODEBUG },
    { "ATTACH",     &attach_cmd,    0,          HLP_ATTACH },
    { "DETACH",     &detach_cmd,    0,          HLP_DETACH },
    { "ASSIGN",     &assign_cmd,    0,          HLP_ASSIGN },
    { "DEASSIGN",   &deassign_cmd,  0,          HLP_DEASSIGN },
    { "SAVE",       &save_cmd,      0,          HLP_SAVE  },
    { "RESTORE",    &restore_cmd,   0,          HLP_RESTORE },
    { "GET",        &restore_cmd,   0,          NULL },
    { "LOAD",       &load_cmd,      0,          HLP_LOAD },
    { "DUMP",       &load_cmd,      1,          HLP_DUMP },
    { "EXIT",       &exit_cmd,      0,          HLP_EXIT },
    { "QUIT",       &exit_cmd,      0,          NULL },
    { "BYE",        &exit_cmd,      0,          NULL },
    { "CD",         &set_default_cmd, 0,        HLP_CD },
    { "PWD",        &pwd_cmd,       0,          HLP_PWD },
    { "DIR",        &dir_cmd,       0,          HLP_DIR },
    { "LS",         &dir_cmd,       0,          HLP_LS },
    { "TYPE",       &type_cmd,      0,          HLP_TYPE },
    { "CAT",        &type_cmd,      0,          HLP_CAT },
    { "DELETE",     &delete_cmd,    0,          HLP_DELETE },
    { "RM",         &delete_cmd,    0,          HLP_RM },
    { "COPY",       &copy_cmd,      0,          HLP_COPY },
    { "CP",         &copy_cmd,      0,          HLP_CP },
    { "SET",        &set_cmd,       0,          HLP_SET },
    { "SHOW",       &show_cmd,      0,          HLP_SHOW },
    { "DO",         &do_cmd,        1,          HLP_DO },
    { "GOTO",       &goto_cmd,      1,          HLP_GOTO },
    { "RETURN",     &return_cmd,    0,          HLP_RETURN },
    { "SHIFT",      &shift_cmd,     0,          HLP_SHIFT },
    { "CALL",       &call_cmd,      0,          HLP_CALL },
    { "ON",         &on_cmd,        0,          HLP_ON },
    { "IF",         &assert_cmd,    0,          HLP_IF },
    { "PROCEED",    &noop_cmd,      0,          HLP_PROCEED },
    { "IGNORE",     &noop_cmd,      0,          HLP_IGNORE },
    { "ECHO",       &echo_cmd,      0,          HLP_ECHO },
    { "ECHOF",      &echof_cmd,     0,          HLP_ECHOF },
    { "ASSERT",     &assert_cmd,    1,          HLP_ASSERT },
    { "SEND",       &send_cmd,      1,          HLP_SEND },
    { "NOSEND",     &send_cmd,      0,          HLP_SEND },
    { "EXPECT",     &expect_cmd,    1,          HLP_EXPECT },
    { "NOEXPECT",   &expect_cmd,    0,          HLP_EXPECT },
    { "SLEEP",      &sleep_cmd,     0,          HLP_SLEEP },
    { "!",          &spawn_cmd,     0,          HLP_SPAWN },
    { "HELP",       &help_cmd,      0,          HLP_HELP },
#if defined(USE_SIM_VIDEO)
    { "SCREENSHOT", &screenshot_cmd,0,          HLP_SCREENSHOT },
#endif
    { NULL, NULL, 0 }
    };

static CTAB set_glob_tab[] = {
    { "CONSOLE",    &sim_set_console,           0, HLP_SET_CONSOLE },
    { "REMOTE",     &sim_set_remote_console,    0, HLP_SET_REMOTE },
    { "BREAK",      &brk_cmd,              SSH_ST, HLP_SET_BREAK },
    { "NOBREAK",    &brk_cmd,              SSH_CL, HLP_SET_BREAK },
    { "DEFAULT",    &set_default_cmd,           1, HLP_SET_DEFAULT },
    { "TELNET",     &sim_set_telnet,            0 },            /* deprecated */
    { "NOTELNET",   &sim_set_notelnet,          0 },            /* deprecated */
    { "LOG",        &sim_set_logon,             0, HLP_SET_LOG  },
    { "NOLOG",      &sim_set_logoff,            0, HLP_SET_LOG  },
    { "DEBUG",      &sim_set_debon,             0, HLP_SET_DEBUG  },
    { "NODEBUG",    &sim_set_deboff,            0, HLP_SET_DEBUG  },
    { "THROTTLE",   &sim_set_throt,             1, HLP_SET_THROTTLE },
    { "NOTHROTTLE", &sim_set_throt,             0, HLP_SET_THROTTLE },
    { "CLOCKS",     &sim_set_timers,            1, HLP_SET_CLOCKS },
    { "ASYNCH",     &sim_set_asynch,            1, HLP_SET_ASYNCH },
    { "NOASYNCH",   &sim_set_asynch,            0, HLP_SET_ASYNCH },
    { "ENVIRONMENT", &sim_set_environment,      1, HLP_SET_ENVIRON },
    { "ON",         &set_on,                    1, HLP_SET_ON },
    { "NOON",       &set_on,                    0, HLP_SET_ON },
    { "VERIFY",     &set_verify,                1, HLP_SET_VERIFY },
    { "VERBOSE",    &set_verify,                1, HLP_SET_VERIFY },
    { "NOVERIFY",   &set_verify,                0, HLP_SET_VERIFY },
    { "NOVERBOSE",  &set_verify,                0, HLP_SET_VERIFY },
    { "MESSAGE",    &set_message,               1, HLP_SET_MESSAGE },
    { "NOMESSAGE",  &set_message,               0, HLP_SET_MESSAGE },
    { "QUIET",      &set_quiet,                 1, HLP_SET_QUIET },
    { "NOQUIET",    &set_quiet,                 0, HLP_SET_QUIET },
    { "PROMPT",     &set_prompt,                0, HLP_SET_PROMPT },
    { NULL,         NULL,                       0 }
    };

static C1TAB set_dev_tab[] = {
    { "OCTAL",      &set_dev_radix,     8 },
    { "DECIMAL",    &set_dev_radix,     10 },
    { "HEX",        &set_dev_radix,     16 },
    { "BINARY",     &set_dev_radix,     2 },
    { "ENABLED",    &set_dev_enbdis,    1 },
    { "DISABLED",   &set_dev_enbdis,    0 },
    { "DEBUG",      &set_dev_debug,     1 },
    { "NODEBUG",    &set_dev_debug,     0 },
    { NULL,         NULL,               0 }
    };

static C1TAB set_unit_tab[] = {
    { "ENABLED",    &set_unit_enbdis,   1 },
    { "DISABLED",   &set_unit_enbdis,   0 },
    { "DEBUG",      &set_dev_debug,     2+1 },
    { "NODEBUG",    &set_dev_debug,     2+0 },
    { NULL,         NULL,               0 }
    };

static SHTAB show_glob_tab[] = {
    { "CONFIGURATION",  &show_config,               0, HLP_SHOW_CONFIG },
    { "DEVICES",        &show_config,               1, HLP_SHOW_DEVICES },
    { "FEATURES",       &show_config,               2, HLP_SHOW_FEATURES },
    { "QUEUE",          &show_queue,                0, HLP_SHOW_QUEUE },
    { "TIME",           &show_time,                 0, HLP_SHOW_TIME },
    { "MODIFIERS",      &show_mod_names,            0, HLP_SHOW_MODIFIERS },
    { "NAMES",          &show_log_names,            0, HLP_SHOW_NAMES },
    { "SHOW",           &show_show_commands,        0, HLP_SHOW_SHOW },
    { "VERSION",        &show_version,              1, HLP_SHOW_VERSION },
    { "DEFAULT",        &show_default,              0, HLP_SHOW_DEFAULT },
    { "CONSOLE",        &sim_show_console,          0, HLP_SHOW_CONSOLE },
    { "REMOTE",         &sim_show_remote_console,   0, HLP_SHOW_REMOTE },
    { "BREAK",          &show_break,                0, HLP_SHOW_BREAK },
    { "LOG",            &sim_show_log,              0, HLP_SHOW_LOG },
    { "TELNET",         &sim_show_telnet,           0 },    /* deprecated */
    { "DEBUG",          &sim_show_debug,            0, HLP_SHOW_DEBUG },
    { "THROTTLE",       &sim_show_throt,            0, HLP_SHOW_THROTTLE },
    { "ASYNCH",         &sim_show_asynch,           0, HLP_SHOW_ASYNCH },
    { "ETHERNET",       &eth_show_devices,          0, HLP_SHOW_ETHERNET },
    { "SERIAL",         &sim_show_serial,           0, HLP_SHOW_SERIAL },
    { "MULTIPLEXER",    &tmxr_show_open_devices,    0, HLP_SHOW_MULTIPLEXER },
    { "MUX",            &tmxr_show_open_devices,    0, HLP_SHOW_MULTIPLEXER },
#if defined(USE_SIM_VIDEO)
    { "VIDEO",          &vid_show,                  0, HLP_SHOW_VIDEO },
#endif
    { "CLOCKS",         &sim_show_timers,           0, HLP_SHOW_CLOCKS },
    { "SEND",           &sim_show_send,             0, HLP_SHOW_SEND },
    { "EXPECT",         &sim_show_expect,           0, HLP_SHOW_EXPECT },
    { "ON",             &show_on,                   0, HLP_SHOW_ON },
    { NULL,             NULL,                       0 }
    };

static SHTAB show_dev_tab[] = {
    { "RADIX",      &show_dev_radix,            0 },
    { "DEBUG",      &show_dev_debug,            0 },
    { "MODIFIERS",  &show_dev_modifiers,        0 },
    { "NAMES",      &show_dev_logicals,         0 },
    { "SHOW",       &show_dev_show_commands,    0 },
    { NULL,         NULL,                       0 }
    };

static SHTAB show_unit_tab[] = {
    { "DEBUG",      &show_dev_debug,            1 },
    { NULL, NULL, 0 }
    };


#if defined(_WIN32) || defined(__hpux)
static
int setenv(const char *envname, const char *envval, int overwrite)
{
char *envstr = (char *)malloc(strlen(envname)+strlen(envval)+2);
int r;

sprintf(envstr, "%s=%s", envname, envval);
#if defined(_WIN32)
r = _putenv(envstr);
free(envstr);
#else
r = putenv(envstr);
#endif
return r;
}

static
int unsetenv(const char *envname)
{
setenv(envname, "", 1);
return 0;
}
#endif

t_stat process_stdin_commands (t_stat stat, char *argv[]);

/* Main command loop */

int main (int argc, char *argv[])
{
char cbuf[4*CBUFSIZE], *cptr, *cptr2;
char nbuf[PATH_MAX + 7];
char **targv = NULL;
int32 i, sw;
t_bool lookswitch;
t_stat stat;

#if defined (__MWERKS__) && defined (macintosh)
argc = ccommand (&argv);
#endif

/* Make sure that argv has at least 10 elements and that it ends in a NULL pointer */
targv = (char **)calloc (1+MAX(10, argc), sizeof(*targv));
for (i=0; i<argc; i++)
    targv[i] = argv[i];
argv = targv;
set_prompt (0, "sim>");                                 /* start with set standard prompt */
*cbuf = 0;                                              /* init arg buffer */
sim_switches = 0;                                       /* init switches */
lookswitch = TRUE;
stdnul = fopen(NULL_DEVICE,"wb");
for (i = 1; i < argc; i++) {                            /* loop thru args */
    if (argv[i] == NULL)                                /* paranoia */
        continue;
    if ((*argv[i] == '-') && lookswitch) {              /* switch? */
        if (get_switches (argv[i], &sw, NULL) == SW_ERROR) {
            fprintf (stderr, "Invalid switch %s\n", argv[i]);
            return 0;
            }
        sim_switches = sim_switches | sw;
        }
    else {
        if ((strlen (argv[i]) + strlen (cbuf) + 3) >= sizeof(cbuf)) {
            fprintf (stderr, "Argument string too long\n");
            return 0;
            }
        if (*cbuf)                                      /* concat args */
            strlcat (cbuf, " ", sizeof (cbuf)); 
        sprintf(&cbuf[strlen(cbuf)], "%s%s%s", strchr(argv[i], ' ') ? "\"" : "", argv[i], strchr(argv[i], ' ') ? "\"" : "");
        lookswitch = FALSE;                             /* no more switches */
        }
    }                                                   /* end for */
sim_quiet = sim_switches & SWMASK ('Q');                /* -q means quiet */
sim_on_inherit = sim_switches & SWMASK ('O');           /* -o means inherit on state */


sim_init_sock ();                                       /* init socket capabilities */
AIO_INIT;                                               /* init Asynch I/O */
if (sim_vm_init != NULL)                                /* call once only */
    (*sim_vm_init)();
sim_finit ();                                           /* init fio package */
setenv ("SIM_NAME", sim_name, 1);                       /* Publish simulator name */
stop_cpu = FALSE;
sim_interval = 0;
sim_time = sim_rtime = 0;
noqueue_time = 0;
sim_clock_queue = QUEUE_LIST_END;
sim_is_running = FALSE;
sim_log = NULL;
if (sim_emax <= 0)
    sim_emax = 1;
if (sim_timer_init ()) {
    fprintf (stderr, "Fatal timer initialization error\n");
    read_line_p ("Hit Return to exit: ", cbuf, sizeof (cbuf) - 1, stdin);
    return 0;
    }
sim_register_internal_device (&sim_expect_dev);
sim_register_internal_device (&sim_step_dev);

if ((stat = sim_ttinit ()) != SCPE_OK) {
    fprintf (stderr, "Fatal terminal initialization error\n%s\n",
        sim_error_text (stat));
    read_line_p ("Hit Return to exit: ", cbuf, sizeof (cbuf) - 1, stdin);
    return 0;
    }
if ((sim_eval = (t_value *) calloc (sim_emax, sizeof (t_value))) == NULL) {
    fprintf (stderr, "Unable to allocate examine buffer\n");
    read_line_p ("Hit Return to exit: ", cbuf, sizeof (cbuf) - 1, stdin);
    return 0;
    };
if (sim_dflt_dev == NULL)                               /* if no default */
    sim_dflt_dev = sim_devices[0];
if ((stat = reset_all_p (0)) != SCPE_OK) {
    fprintf (stderr, "Fatal simulator initialization error\n%s\n",
        sim_error_text (stat));
    read_line_p ("Hit Return to exit: ", cbuf, sizeof (cbuf) - 1, stdin);
    return 0;
    }
if ((stat = sim_brk_init ()) != SCPE_OK) {
    fprintf (stderr, "Fatal breakpoint table initialization error\n%s\n",
        sim_error_text (stat));
    read_line_p ("Hit Return to exit: ", cbuf, sizeof (cbuf) - 1, stdin);
    return 0;
    }
signal (SIGINT, int_handler);
if (!sim_quiet) {
    printf ("\n");
    show_version (stdout, NULL, NULL, 0, NULL);
    }
show_version (stdnul, NULL, NULL, 1, NULL);             /* Quietly set SIM_OSTYPE */
if (((sim_dflt_dev->flags & DEV_DEBUG) == 0) &&         /* default device without debug? */
    (sim_dflt_dev->debflags == NULL)) {
    sim_dflt_dev->flags |= DEV_DEBUG;                   /* connect default event debugging */
    sim_dflt_dev->debflags = sim_dflt_debug;
    }
if (*argv[0]) {                                         /* sim name arg? */
    char *np;                                           /* "path.ini" */

    strncpy (nbuf, argv[0], PATH_MAX + 1);              /* copy sim name */
    if ((np = (char *)match_ext (nbuf, "EXE")))         /* remove .exe */
        *np = 0;
    np = strrchr (nbuf, '/');                           /* stript path and try again in cwd */
    if (np == NULL)
        np = strrchr (nbuf, '\\');                      /* windows path separator */
    if (np == NULL)
        np = strrchr (nbuf, ']');                       /* VMS path separator */
    if (np != NULL)
        setenv ("SIM_BIN_NAME", np+1, 1);               /* Publish simulator binary name */
    setenv ("SIM_BIN_PATH", argv[0], 1);
    }
sim_argv = argv;
cptr = getenv("HOME");
if (cptr == NULL) {
    cptr = getenv("HOMEPATH");
    cptr2 = getenv("HOMEDRIVE");
    }
else
    cptr2 = NULL;
if (cptr && sizeof (nbuf) > strlen (cptr) + strlen ("/simh.ini") + 1) {
    sprintf(nbuf, "\"%s%s%ssimh.ini\"", cptr2 ? cptr2 : "", cptr, strchr (cptr, '/') ? "/" : "\\");
    stat = do_cmd (-1, nbuf) & ~SCPE_NOMESSAGE;         /* simh.ini proc cmd file */
    }
if (stat == SCPE_OPENERR)
    stat = do_cmd (-1, "simh.ini");                     /* simh.ini proc cmd file */
if (*cbuf)                                              /* cmd file arg? */
    stat = do_cmd (0, cbuf);                            /* proc cmd file */
else if (*argv[0]) {                                    /* sim name arg? */
    char *np;                                           /* "path.ini" */
    nbuf[0] = '"';                                      /* starting " */
    strncpy (nbuf + 1, argv[0], PATH_MAX + 1);          /* copy sim name */
    if ((np = (char *)match_ext (nbuf, "EXE")))         /* remove .exe */
        *np = 0;
    strlcat (nbuf, ".ini\"", sizeof (nbuf));            /* add .ini" */
    stat = do_cmd (-1, nbuf) & ~SCPE_NOMESSAGE;         /* proc default cmd file */
    if (stat == SCPE_OPENERR) {                         /* didn't exist/can't open? */
        np = strrchr (nbuf, '/');                       /* stript path and try again in cwd */
        if (np == NULL)
            np = strrchr (nbuf, '\\');                  /* windows path separator */
        if (np == NULL)
            np = strrchr (nbuf, ']');                   /* VMS path separator */
        if (np != NULL) {
            *np = '"';
            stat = do_cmd (-1, np) & ~SCPE_NOMESSAGE;   /* proc default cmd file */
            }
        }
    }

stat = process_stdin_commands (SCPE_BARE_STATUS(stat), argv);

detach_all (0, TRUE);                                   /* close files */
sim_set_deboff (0, NULL);                               /* close debug */
sim_set_logoff (0, NULL);                               /* close log */
sim_set_notelnet (0, NULL);                             /* close Telnet */
vid_close ();                                           /* close video */
sim_ttclose ();                                         /* close console */
AIO_CLEANUP;                                            /* Asynch I/O */
sim_cleanup_sock ();                                    /* cleanup sockets */
fclose (stdnul);                                        /* close bit bucket file handle */
free (targv);                                           /* release any argv copy that was made */
return 0;
}

t_stat process_stdin_commands (t_stat stat, char *argv[])
{
char cbuf[4*CBUFSIZE], gbuf[CBUFSIZE];
CONST char *cptr;
t_stat stat_nomessage;
CTAB *cmdp;

stat = SCPE_BARE_STATUS(stat);                          /* remove possible flag */
while (stat != SCPE_EXIT) {                             /* in case exit */
    if (stop_cpu) {                                     /* SIGINT happened? */
        stop_cpu = FALSE;
        if (!sim_ttisatty()) {
            stat = SCPE_EXIT;
            break;
            }
        if (sim_on_actions[sim_do_depth][ON_SIGINT_ACTION])
            sim_brk_setact (sim_on_actions[sim_do_depth][ON_SIGINT_ACTION]);
        }
    if ((cptr = sim_brk_getact (cbuf, sizeof(cbuf)))) { /* pending action? */
        if (sim_do_echo)
            printf ("%s+ %s\n", sim_prompt, cptr);      /* echo */
        }
    else {
        if (sim_vm_read != NULL) {                      /* sim routine? */
            printf ("%s", sim_prompt);                  /* prompt */
            cptr = (*sim_vm_read) (cbuf, sizeof(cbuf), stdin);
            }
        else
            cptr = read_line_p (sim_prompt, cbuf, sizeof(cbuf), stdin);/* read with prompt*/
        }
    if (cptr == NULL) {                                 /* EOF? or SIGINT? */
        if (sim_ttisatty()) {
            printf ("\n");
            continue;                                   /* ignore tty EOF */
            }
        else
            break;                                      /* otherwise exit */
        }
    if (*cptr == 0)                                     /* ignore blank */
        continue;
    sim_cmd_echoed = TRUE;
    sim_sub_args (cbuf, sizeof(cbuf), argv);
    if (sim_log)                                        /* log cmd */
        fprintf (sim_log, "%s%s\n", sim_prompt, cptr);
    if (sim_deb && (sim_deb != sim_log) && (sim_deb != stdout))
        fprintf (sim_deb, "%s%s\n", sim_prompt, cptr);
    cptr = get_glyph_cmd (cptr, gbuf);                  /* get command glyph */
    sim_switches = 0;                                   /* init switches */
    if ((cmdp = find_cmd (gbuf)))                       /* lookup command */
        stat = cmdp->action (cmdp->arg, cptr);          /* if found, exec */
    else
        stat = SCPE_UNK;
    stat_nomessage = stat & SCPE_NOMESSAGE;             /* extract possible message supression flag */
    stat_nomessage = stat_nomessage || (!sim_show_message);/* Apply global suppression */
    stat = SCPE_BARE_STATUS(stat);                      /* remove possible flag */
    sim_last_cmd_stat = stat;                           /* save command error status */
    if (!stat_nomessage) {                              /* displaying message status? */
        if (cmdp && (cmdp->message))                    /* special message handler? */
            cmdp->message (NULL, stat);                 /* let it deal with display */
        else
            if (stat >= SCPE_BASE)                      /* error? */
                sim_printf ("%s\n", sim_error_text (stat));
        }
    if (sim_vm_post != NULL)
        (*sim_vm_post) (TRUE);
    }                                                   /* end while */
return stat;
}

/* Set prompt routine */

t_stat set_prompt (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE], *gptr;

if ((!cptr) || (*cptr == '\0'))
    return SCPE_ARG;

cptr = get_glyph_nc (cptr, gbuf, '"');                  /* get quote delimited token */
if (gbuf[0] == '\0') {                                  /* Token started with quote */
    gbuf[sizeof (gbuf)-1] = '\0';
    strlcpy (gbuf, cptr, sizeof (gbuf));
    gptr = strchr (gbuf, '"');
    if (gptr)
        *gptr = '\0';
    }
sim_prompt = (char *)realloc (sim_prompt, strlen (gbuf) + 2);   /* nul terminator and trailing blank */
sprintf (sim_prompt, "%s ", gbuf);
return SCPE_OK;
}

/* Find command routine */

CTAB *find_cmd (const char *gbuf)
{
CTAB *cmdp = NULL;

if (sim_vm_cmd)                                         /* try ext commands */
    cmdp = find_ctab (sim_vm_cmd, gbuf);
if (cmdp == NULL)                                       /* try regular cmds */
    cmdp = find_ctab (cmd_table, gbuf);
return cmdp;
}

/* Exit command */

t_stat exit_cmd (int32 flag, CONST char *cptr)
{
return SCPE_EXIT;
}

/* Help command */


/* Used when sorting a list of command names */
static int _cmd_name_compare (const void *pa, const void *pb)
{
CTAB * const *a = (CTAB * const *)pa;
CTAB * const *b = (CTAB * const *)pb;

return strcmp((*a)->name, (*b)->name);
}

void fprint_help (FILE *st)
{
CTAB *cmdp;
CTAB **hlp_cmdp = NULL;
int cmd_cnt = 0;
int cmd_size = 0;
size_t max_cmdname_size = 0;
int i, line_offset;

for (cmdp = sim_vm_cmd; cmdp && (cmdp->name != NULL); cmdp++) {
    if (cmdp->help) {
        if (cmd_cnt >= cmd_size) {
            cmd_size += 20;
            hlp_cmdp = (CTAB **)realloc (hlp_cmdp, sizeof(*hlp_cmdp)*cmd_size);
            }
        hlp_cmdp[cmd_cnt] = cmdp;
        ++cmd_cnt;
        if (strlen(cmdp->name) > max_cmdname_size)
            max_cmdname_size = strlen(cmdp->name);
        }
    }
for (cmdp = cmd_table; cmdp && (cmdp->name != NULL); cmdp++) {
    if (cmdp->help && (!sim_vm_cmd || !find_ctab (sim_vm_cmd, cmdp->name))) {
        if (cmd_cnt >= cmd_size) {
            cmd_size += 20;
            hlp_cmdp = (CTAB **)realloc (hlp_cmdp, sizeof(*hlp_cmdp)*cmd_size);
            }
        hlp_cmdp[cmd_cnt] = cmdp;
        ++cmd_cnt;
        if (strlen (cmdp->name) > max_cmdname_size)
            max_cmdname_size = strlen(cmdp->name);
        }
    }
fprintf (st, "Help is available for the following commands:\n\n    ");
qsort (hlp_cmdp, cmd_cnt, sizeof(*hlp_cmdp), _cmd_name_compare);
line_offset = 4;
for (i=0; i<cmd_cnt; ++i) {
    fputs (hlp_cmdp[i]->name, st);
    line_offset += 5 + max_cmdname_size;
    if (line_offset + max_cmdname_size > 79) {
        line_offset = 4;
        fprintf (st, "\n    ");
        }
    else
        fprintf (st, "%*s", (int)(max_cmdname_size + 5 - strlen (hlp_cmdp[i]->name)), "");
    }
free (hlp_cmdp);
fprintf (st, "\n");
return;
}

static void fprint_header (FILE *st, t_bool *pdone, char *context)
{
if (!*pdone)
    fprintf (st, "%s", context);
*pdone = TRUE;
}

void fprint_reg_help_ex (FILE *st, DEVICE *dptr, t_bool silent)
{
REG *rptr, *trptr;
t_bool found = FALSE;
t_bool all_unique = TRUE;
size_t max_namelen = 0;
DEVICE *tdptr;
CONST char *tptr;
char *namebuf;
char rangebuf[32];

if (dptr->registers)
    for (rptr = dptr->registers; rptr->name != NULL; rptr++) {
        if (rptr->flags & REG_HIDDEN)
            continue;
        if (rptr->depth > 1)
            sprintf (rangebuf, "[%d:%d]", 0, rptr->depth-1);
        else
            strcpy (rangebuf, "");
        if (max_namelen < (strlen(rptr->name) + strlen (rangebuf)))
            max_namelen = strlen(rptr->name) + strlen (rangebuf);
        found = TRUE;
        trptr = find_reg_glob (rptr->name, &tptr, &tdptr);
        if ((trptr == NULL) || (tdptr != dptr))
            all_unique = FALSE;
        }
if (!found) {
    if (!silent)
        fprintf (st, "No register help is available for the %s device\n", dptr->name);
    }
else {
    namebuf = (char *)calloc (max_namelen + 1, sizeof (*namebuf));
    fprintf (st, "\nThe %s device implements these registers:\n\n", dptr->name);
    for (rptr = dptr->registers; rptr->name != NULL; rptr++) {
        if (rptr->flags & REG_HIDDEN)
            continue;
        if (rptr->depth <= 1)
            sprintf (namebuf, "%*s", -((int)max_namelen), rptr->name);
        else {
            sprintf (rangebuf, "[%d:%d]", 0, rptr->depth-1);
            sprintf (namebuf, "%s%*s", rptr->name, (int)(strlen(rptr->name))-((int)max_namelen), rangebuf);
            }
        if (all_unique) {
            fprintf (st, "  %s %4d  %s\n", namebuf, rptr->width, rptr->desc ? rptr->desc : "");
            continue;
            }
        trptr = find_reg_glob (rptr->name, &tptr, &tdptr);
        if ((trptr == NULL) || (tdptr != dptr))
            fprintf (st, "  %s %s %4d  %s\n", dptr->name, namebuf, rptr->width, rptr->desc ? rptr->desc : "");
        else
            fprintf (st, "  %*s %s %4d  %s\n", (int)strlen(dptr->name), "", namebuf, rptr->width, rptr->desc ? rptr->desc : "");
        }
    free (namebuf);
    }
}

void fprint_reg_help (FILE *st, DEVICE *dptr)
{
fprint_reg_help_ex (st, dptr, TRUE);
}

void fprint_attach_help_ex (FILE *st, DEVICE *dptr, t_bool silent)
{
if (dptr->attach_help) {
    fprintf (st, "\n%s device attach commands:\n\n", dptr->name);
    dptr->attach_help (st, dptr, NULL, 0, NULL);
    return;
    }
if (DEV_TYPE(dptr) == DEV_MUX) {
    fprintf (st, "\n%s device attach commands:\n\n", dptr->name);
    tmxr_attach_help (st, dptr, NULL, 0, NULL);
    return;
    }
if (DEV_TYPE(dptr) == DEV_DISK) {
    fprintf (st, "\n%s device attach commands:\n\n", dptr->name);
    sim_disk_attach_help (st, dptr, NULL, 0, NULL);
    return;
    }
if (DEV_TYPE(dptr) == DEV_TAPE) {
    fprintf (st, "\n%s device attach commands:\n\n", dptr->name);
    sim_tape_attach_help (st, dptr, NULL, 0, NULL);
    return;
    }
if (DEV_TYPE(dptr) == DEV_ETHER) {
    fprintf (st, "\n%s device attach commands:\n\n", dptr->name);
    eth_attach_help (st, dptr, NULL, 0, NULL);
    return;
    }
if (!silent) {
    fprintf (st, "No ATTACH help is available for the %s device\n", dptr->name);
    if (dptr->help)
        dptr->help (st, dptr, NULL, 0, NULL);
    }
}

void fprint_set_help_ex (FILE *st, DEVICE *dptr, t_bool silent)
{
MTAB *mptr;
DEBTAB *dep;
t_bool found = FALSE;
t_bool deb_desc_available = FALSE;
char buf[CBUFSIZE], header[CBUFSIZE];

sprintf (header, "\n%s device SET commands:\n\n", dptr->name);
if (dptr->modifiers) {
    for (mptr = dptr->modifiers; mptr->mask != 0; mptr++) {
        if (!MODMASK(mptr,MTAB_VDV) && MODMASK(mptr,MTAB_VUN) && (dptr->numunits != 1))
            continue;                                       /* skip unit only extended modifiers */
        if ((dptr->numunits != 1) && !(mptr->mask & MTAB_XTD))
            continue;                                       /* skip unit only simple modifiers */
        if (mptr->mstring) {
            fprint_header (st, &found, header);
            sprintf (buf, "set %s %s%s", sim_dname (dptr), mptr->mstring, (strchr(mptr->mstring, '=')) ? "" : (MODMASK(mptr,MTAB_VALR) ? "=val" : (MODMASK(mptr,MTAB_VALO) ? "{=val}" : "")));
            if ((strlen (buf) < 30) || (!mptr->help))
                fprintf (st, "%-30s\t%s\n", buf, mptr->help ? mptr->help : "");
            else
                fprintf (st, "%s\n%-30s\t%s\n", buf, "", mptr->help);
            }
        }
    }
if (dptr->flags & DEV_DISABLE) {
    fprint_header (st, &found, header);
    sprintf (buf, "set %s ENABLE", sim_dname (dptr));
    fprintf (st,  "%-30s\tEnables device %s\n", buf, sim_dname (dptr));
    sprintf (buf, "set %s DISABLE", sim_dname (dptr));
    fprintf (st,  "%-30s\tDisables device %s\n", buf, sim_dname (dptr));
    }
if ((dptr->flags & DEV_DEBUG) || (dptr->debflags)) {
    fprint_header (st, &found, header);
    sprintf (buf, "set %s DEBUG", sim_dname (dptr));
    fprintf (st,  "%-30s\tEnables debugging for device %s\n", buf, sim_dname (dptr));
    sprintf (buf, "set %s NODEBUG", sim_dname (dptr));
    fprintf (st,  "%-30s\tDisables debugging for device %s\n", buf, sim_dname (dptr));
    if (dptr->debflags) {
        strcpy (buf, "");
        fprintf (st, "set %s DEBUG=", sim_dname (dptr));
        for (dep = dptr->debflags; dep->name != NULL; dep++) {
            fprintf (st, "%s%s", ((dep == dptr->debflags) ? "" : ";"), dep->name);
            deb_desc_available |= ((dep->desc != NULL) && (dep->desc[0] != '\0'));
            }
        fprintf (st, "\n");
        fprintf (st,  "%-30s\tEnables specific debugging for device %s\n", buf, sim_dname (dptr));
        fprintf (st, "set %s NODEBUG=", sim_dname (dptr));
        for (dep = dptr->debflags; dep->name != NULL; dep++)
            fprintf (st, "%s%s", ((dep == dptr->debflags) ? "" : ";"), dep->name);
        fprintf (st, "\n");
        fprintf (st,  "%-30s\tDisables specific debugging for device %s\n", buf, sim_dname (dptr));
        }
    }
if ((dptr->modifiers) && (dptr->units) && (dptr->numunits != 1)) {
    if (dptr->units->flags & UNIT_DISABLE) {
        fprint_header (st, &found, header);
        sprintf (buf, "set %sn ENABLE", sim_dname (dptr));
        fprintf (st,  "%-30s\tEnables unit %sn\n", buf, sim_dname (dptr));
        sprintf (buf, "set %sn DISABLE", sim_dname (dptr));
        fprintf (st,  "%-30s\tDisables unit %sn\n", buf, sim_dname (dptr));
        }
    if (((dptr->flags & DEV_DEBUG) || (dptr->debflags)) &&
        ((DEV_TYPE(dptr) == DEV_DISK) || (DEV_TYPE(dptr) == DEV_TAPE))) {
        sprintf (buf, "set %sn DEBUG", sim_dname (dptr));
        fprintf (st,  "%-30s\tEnables debugging for device unit %sn\n", buf, sim_dname (dptr));
        sprintf (buf, "set %sn NODEBUG", sim_dname (dptr));
        fprintf (st,  "%-30s\tDisables debugging for device unit %sn\n", buf, sim_dname (dptr));
        if (dptr->debflags) {
            strcpy (buf, "");
            fprintf (st, "set %sn DEBUG=", sim_dname (dptr));
            for (dep = dptr->debflags; dep->name != NULL; dep++)
                fprintf (st, "%s%s", ((dep == dptr->debflags) ? "" : ";"), dep->name);
            fprintf (st, "\n");
            fprintf (st,  "%-30s\tEnables specific debugging for device unit %sn\n", buf, sim_dname (dptr));
            fprintf (st, "set %sn NODEBUG=", sim_dname (dptr));
            for (dep = dptr->debflags; dep->name != NULL; dep++)
                fprintf (st, "%s%s", ((dep == dptr->debflags) ? "" : ";"), dep->name);
            fprintf (st, "\n");
            fprintf (st,  "%-30s\tDisables specific debugging for device unit %sn\n", buf, sim_dname (dptr));
            }

        }
    for (mptr = dptr->modifiers; mptr->mask != 0; mptr++) {
        if ((!MODMASK(mptr,MTAB_VUN)) && MODMASK(mptr,MTAB_XTD))
            continue;                                           /* skip device only modifiers */
        if ((!mptr->valid) && MODMASK(mptr,MTAB_XTD))
            continue;                                           /* skip show only modifiers */
        if (mptr->mstring) {
            fprint_header (st, &found, header);
            sprintf (buf, "set %s%s %s%s", sim_dname (dptr), (dptr->numunits > 1) ? "n" : "0", mptr->mstring, (strchr(mptr->mstring, '=')) ? "" : (MODMASK(mptr,MTAB_VALR) ? "=val" : (MODMASK(mptr,MTAB_VALO) ? "{=val}": "")));
            fprintf (st, "%-30s\t%s\n", buf, (strchr (mptr->mstring, '=')) ? ((strlen (buf) > 30) ? "" : mptr->help) : (mptr->help ? mptr->help : ""));
            if ((strchr (mptr->mstring, '=')) && (strlen (buf) > 30))
                fprintf (st,  "%-30s\t%s\n", "", mptr->help);
            }
        }
    }
if (deb_desc_available) {
    fprintf (st, "\n*%s device DEBUG settings:\n", sim_dname (dptr));
    for (dep = dptr->debflags; dep->name != NULL; dep++)
        fprintf (st, "%4s%-12s%s\n", "", dep->name, dep->desc ? dep->desc : "");
    }
if (!found && !silent)
    fprintf (st, "No SET help is available for the %s device\n", dptr->name);
}

void fprint_set_help (FILE *st, DEVICE *dptr)
    {
    fprint_set_help_ex (st, dptr, TRUE);
    }

void fprint_show_help_ex (FILE *st, DEVICE *dptr, t_bool silent)
{
MTAB *mptr;
t_bool found = FALSE;
char buf[CBUFSIZE], header[CBUFSIZE];

sprintf (header, "\n%s device SHOW commands:\n\n", dptr->name);
if (dptr->modifiers) {
    for (mptr = dptr->modifiers; mptr->mask != 0; mptr++) {
        if (!MODMASK(mptr,MTAB_VDV) && MODMASK(mptr,MTAB_VUN) && (dptr->numunits != 1))
            continue;                                       /* skip unit only extended modifiers */
        if ((dptr->numunits != 1) && !(mptr->mask & MTAB_XTD))
            continue;                                       /* skip unit only simple modifiers */
        if ((!mptr->disp) || (!mptr->pstring) || !(*mptr->pstring))
            continue;
        fprint_header (st, &found, header);
        sprintf (buf, "show %s %s%s", sim_dname (dptr), mptr->pstring, MODMASK(mptr,MTAB_SHP) ? "=arg" : "");
        fprintf (st, "%-30s\t%s\n", buf, mptr->help ? mptr->help : "");
        }
    }
if ((dptr->flags & DEV_DEBUG) || (dptr->debflags)) {
    fprint_header (st, &found, header);
    sprintf (buf, "show %s DEBUG", sim_dname (dptr));
    fprintf (st, "%-30s\tDisplays debugging status for device %s\n", buf, sim_dname (dptr));
    }
if ((dptr->modifiers) && (dptr->units) && (dptr->numunits != 1)) {
    for (mptr = dptr->modifiers; mptr->mask != 0; mptr++) {
        if ((!MODMASK(mptr,MTAB_VUN)) && MODMASK(mptr,MTAB_XTD))
            continue;                                           /* skip device only modifiers */
        if ((!mptr->disp) || (!mptr->pstring))
            continue;
        fprint_header (st, &found, header);
        sprintf (buf, "show %s%s %s%s", sim_dname (dptr), (dptr->numunits > 1) ? "n" : "0", mptr->pstring, MODMASK(mptr,MTAB_SHP) ? "=arg" : "");
        fprintf (st, "%-30s\t%s\n", buf, mptr->help ? mptr->help : "");
        }
    }
if (!found && !silent)
    fprintf (st, "No SHOW help is available for the %s device\n", dptr->name);
}

void fprint_show_help (FILE *st, DEVICE *dptr)
    {
    fprint_show_help_ex (st, dptr, TRUE);
    }

void fprint_brk_help_ex (FILE *st, DEVICE *dptr, t_bool silent)
{
BRKTYPTAB *brkt = dptr->brk_types;
char gbuf[CBUFSIZE];

if (sim_brk_types == 0) {
    if ((dptr != sim_dflt_dev) && (!silent)) {
        fprintf (st, "Breakpoints are not supported in the %s simulator\n", sim_name);
        if (dptr->help)
            dptr->help (st, dptr, NULL, 0, NULL);
        }
    return;
    }
if (brkt == NULL) {
    int i;

    if (dptr == sim_dflt_dev) {
        if (sim_brk_types & ~sim_brk_dflt) {
            fprintf (st, "%s supports the following breakpoint types:\n", sim_dname (dptr));
            for (i=0; i<26; i++) {
                if (sim_brk_types & (1<<i))
                    fprintf (st, "  -%c\n", 'A'+i);
                }
            }
        fprintf (st, "The default breakpoint type is: %s\n", put_switches (gbuf, sizeof(gbuf), sim_brk_dflt));
        }
    return;
    }
fprintf (st, "%s supports the following breakpoint types:\n", sim_dname (dptr));
while (brkt->btyp) {
    fprintf (st, "  %s     %s\n", put_switches (gbuf, sizeof(gbuf), brkt->btyp), brkt->desc);
    ++brkt;
    }
fprintf (st, "The default breakpoint type is: %s\n", put_switches (gbuf, sizeof(gbuf), sim_brk_dflt));
}

t_stat help_dev_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
char gbuf[CBUFSIZE];
CTAB *cmdp;

if (*cptr) {
    const char *gptr = get_glyph (cptr, gbuf, 0);
    if ((cmdp = find_cmd (gbuf))) {
        if (cmdp->action == &exdep_cmd) {
            if (dptr->help) /* Shouldn't this pass cptr so the device knows which command invoked? */
                return dptr->help (st, dptr, uptr, flag, gptr);
            else
                fprintf (st, "No help available for the %s %s command\n", cmdp->name, sim_dname(dptr));
            return SCPE_OK;
            }
        if (cmdp->action == &set_cmd) {
            fprint_set_help_ex (st, dptr, FALSE);
            return SCPE_OK;
            }
        if (cmdp->action == &show_cmd) {
            fprint_show_help_ex (st, dptr, FALSE);
            return SCPE_OK;
            }
        if (cmdp->action == &attach_cmd) {
            fprint_attach_help_ex (st, dptr, FALSE);
            return SCPE_OK;
            }
        if (cmdp->action == &brk_cmd) {
            fprint_brk_help_ex (st, dptr, FALSE);
            return SCPE_OK;
            }
        if (dptr->help)
            return dptr->help (st, dptr, uptr, flag, cptr);
        fprintf (st, "No %s help is available for the %s device\n", cmdp->name, dptr->name);
        return SCPE_OK;
        }
    if (MATCH_CMD (gbuf, "REGISTERS") == 0) {
        fprint_reg_help_ex (st, dptr, FALSE);
        return SCPE_OK;
        }
    if (dptr->help)
        return dptr->help (st, dptr, uptr, flag, cptr);
    fprintf (st, "No %s help is available for the %s device\n", gbuf, dptr->name);
    return SCPE_OK;
    }
if (dptr->help) {
    return dptr->help (st, dptr, uptr, flag, cptr);
    }
if (dptr->description)
    fprintf (st, "%s %s help\n", dptr->description (dptr), dptr->name);
else
    fprintf (st, "%s help\n", dptr->name);
fprint_set_help_ex (st, dptr, TRUE);
fprint_show_help_ex (st, dptr, TRUE);
fprint_attach_help_ex (st, dptr, TRUE);
fprint_reg_help_ex (st, dptr, TRUE);
fprint_brk_help_ex (st, dptr, TRUE);
return SCPE_OK;
}

t_stat help_cmd_output (int32 flag, const char *help, const char *help_base)
{
switch (help[0]) {
    case '*':
        scp_help (stdout, NULL, NULL, flag, help_base ? help_base : simh_help, help+1);
        if (sim_log)
            scp_help (sim_log, NULL, NULL, flag | SCP_HELP_FLAT, help_base ? help_base : simh_help, help+1);
        break;
    default:
        fputs (help, stdout);
        if (sim_log)
            fputs (help, sim_log);
        break;
    }
return SCPE_OK;
}

t_stat help_cmd (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
CTAB *cmdp;

GET_SWITCHES (cptr);
if (sim_switches & SWMASK ('F'))
    flag = flag | SCP_HELP_FLAT;
if (*cptr) {
    cptr = get_glyph (cptr, gbuf, 0);
    if ((cmdp = find_cmd (gbuf))) {
        if (*cptr) {
            if ((cmdp->action == &set_cmd) || (cmdp->action == &show_cmd)) {
                DEVICE *dptr;
                UNIT *uptr;
                t_stat r;

                cptr = get_glyph (cptr, gbuf, 0);
                dptr = find_unit (gbuf, &uptr);
                if (dptr == NULL)
                    dptr = find_dev (gbuf);
                if (dptr != NULL) {
                    r = help_dev_help (stdout, dptr, uptr, flag, (cmdp->action == &set_cmd) ? "SET" : "SHOW");
                    if (sim_log)
                        help_dev_help (sim_log, dptr, uptr, flag | SCP_HELP_FLAT, (cmdp->action == &set_cmd) ? "SET" : "SHOW");
                    return r;
                    }
                if (cmdp->action == &set_cmd) { /* HELP SET xxx (not device or unit) */
                    if ((cmdp = find_ctab (set_glob_tab, gbuf)) &&
                         (cmdp->help))
                        return help_cmd_output (flag, cmdp->help, cmdp->help_base);
                    }
                else { /* HELP SHOW xxx (not device or unit) */
                    SHTAB *shptr = find_shtab (show_glob_tab, gbuf);

                    if ((shptr == NULL) || (shptr->help == NULL) || (*shptr->help == '\0'))
                        return SCPE_ARG;
                    return help_cmd_output (flag, shptr->help, NULL);
                    }
                return SCPE_ARG;
                }
            else
                return SCPE_2MARG;
            }
        if (cmdp->help) {
            if (strcmp (cmdp->name, "HELP") == 0) {
                DEVICE *dptr;
                int i;

                for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
                    if (dptr->help)
                        sim_printf ("h{elp} %-17s display help for device %s\n", dptr->name, dptr->name);
                    if (dptr->attach_help || 
                        (DEV_TYPE(dptr) == DEV_MUX) ||
                        (DEV_TYPE(dptr) == DEV_DISK) ||
                        (DEV_TYPE(dptr) == DEV_TAPE)) {
                        sim_printf ("h{elp} %s ATTACH\t display help for device %s ATTACH command\n", dptr->name, dptr->name);
                        }
                    if (dptr->registers) {
                        if (dptr->registers->name != NULL)
                            sim_printf ("h{elp} %s REGISTERS\t display help for device %s register variables\n", dptr->name, dptr->name);
                        }
                    if (dptr->modifiers) {
                        MTAB *mptr;

                        for (mptr = dptr->modifiers; mptr->pstring != NULL; mptr++) {
                            if (mptr->help) {
                                sim_printf ("h{elp} %s SET\t\t display help for device %s SET commands (modifiers)\n", dptr->name, dptr->name);
                                break;
                                }
                            }
                        }
                    }
                }
            else {
                if (((cmdp->action == &exdep_cmd) || (0 == strcmp(cmdp->name, "BOOT"))) &&
                    sim_dflt_dev->help) {
                        sim_dflt_dev->help (stdout, sim_dflt_dev, sim_dflt_dev->units, 0, cmdp->name);
                        if (sim_log)
                            sim_dflt_dev->help (sim_log, sim_dflt_dev, sim_dflt_dev->units, 0, cmdp->name);
                    }
                }
            help_cmd_output (flag, cmdp->help, cmdp->help_base);
            }
        else { /* no help so it is likely a command alias */
            CTAB *cmdpa;

            for (cmdpa=cmd_table; cmdpa->name != NULL; cmdpa++)
                if ((cmdpa->action == cmdp->action) && (cmdpa->help)) {
                    sim_printf ("%s is an alias for the %s command:\n%s", 
                                cmdp->name, cmdpa->name, cmdpa->help);
                    break;
                    }
            if (cmdpa->name == NULL)                /* not found? */
                sim_printf ("No help available for the %s command\n", cmdp->name);
            }
        }
    else { 
        DEVICE *dptr;
        UNIT *uptr;
        t_stat r;

        dptr = find_unit (gbuf, &uptr);
        if (dptr == NULL) {
            dptr = find_dev (gbuf);
            if (dptr == NULL)
                return SCPE_ARG;
            if (dptr->flags & DEV_DISABLE)
                sim_printf ("Device %s is currently disabled\n", dptr->name);
            }
        r = help_dev_help (stdout, dptr, uptr, flag, cptr);
        if (sim_log)
            help_dev_help (sim_log, dptr, uptr, flag | SCP_HELP_FLAT, cptr);
        return r;
        }
    }
else {
    fprint_help (stdout);
    if (sim_log)
        fprint_help (sim_log);
    }
return SCPE_OK;
}

/* Spawn command */

t_stat spawn_cmd (int32 flag, CONST char *cptr)
{
t_stat status;
if ((cptr == NULL) || (strlen (cptr) == 0))
    cptr = getenv("SHELL");
if ((cptr == NULL) || (strlen (cptr) == 0))
    cptr = getenv("ComSpec");
#if defined (VMS)
if ((cptr == NULL) || (strlen (cptr) == 0))
    cptr = "SPAWN/INPUT=SYS$COMMAND:";
#endif
fflush(stdout);                                         /* flush stdout */
if (sim_log)                                            /* flush log if enabled */
    fflush (sim_log);
if (sim_deb)                                            /* flush debug if enabled */
    fflush (sim_deb);
status = system (cptr);
#if defined (VMS)
printf ("\n");
#endif

return status;
}

/* Screenshot command */

t_stat screenshot_cmd (int32 flag, CONST char *cptr)
{
if ((cptr == NULL) || (strlen (cptr) == 0))
    return SCPE_ARG;
#if defined (USE_SIM_VIDEO)
return vid_screenshot (cptr);
#else
sim_printf ("No video device\n");
return SCPE_UNK|SCPE_NOMESSAGE;
#endif
}

/* Echo command */

t_stat echo_cmd (int32 flag, CONST char *cptr)
{
sim_printf ("%s\n", cptr);
return SCPE_OK;
}

/* EchoF command */

t_stat echof_cmd (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
CONST char *tptr;
TMLN *lp = NULL;
uint8 dbuf[4*CBUFSIZE];
uint32 dsize = 0;
t_stat r;

GET_SWITCHES (cptr);
tptr = get_glyph (cptr, gbuf, ',');
if (sim_isalpha(gbuf[0]) && (strchr (gbuf, ':'))) {
    r = tmxr_locate_line (gbuf, &lp);
    if (r != SCPE_OK)
        return r;
    cptr = tptr;
    }
GET_SWITCHES (cptr);
if ((*cptr == '"') || (*cptr == '\'')) {
    cptr = get_glyph_quoted (cptr, gbuf, 0);
    if (*cptr != '\0')
        return SCPE_2MARG;              /* No more arguments */
    if (SCPE_OK != sim_decode_quoted_string (gbuf, dbuf, &dsize))
        return sim_messagef (SCPE_ARG, "Invalid String\n");
    dbuf[dsize] = 0;
    cptr = (char *)dbuf;
    }
if (lp) {
    tmxr_linemsgf (lp, "%s%s", cptr, (sim_switches & SWMASK('N')) ? "" : "\r\n");
    tmxr_send_buffered_data (lp);
    }
else
    sim_printf ("%s%s", cptr, (sim_switches & SWMASK('N')) ? "" : "\n");
return SCPE_OK;
}

/* Do command

   Syntax: DO {-E} {-V} <filename> {<arguments>...}

   -E causes all command errors to be fatal; without it, only EXIT and ASSERT
   failure will stop a command file.

   -V causes commands to be echoed before execution.

   Note that SCPE_STEP ("Step expired") is considered a note and not an error
   and so does not abort command execution when using -E.

   Inputs:
        flag    =   caller and nesting level indicator
        fcptr   =   filename and optional arguments, space-separated
   Outputs:
        status  =   error status

   The "flag" input value indicates the source of the call, as follows:

        -1      =   initialization file (no error if not found)
         0      =   command line file
         1      =   "DO" command
        >1      =   nested "DO" command
*/

t_stat do_cmd (int32 flag, CONST char *fcptr)
{
return do_cmd_label (flag, fcptr, NULL);
}

static char *do_position(void)
{
static char cbuf[CBUFSIZE];

sprintf (cbuf, "%s%s%s-%d", sim_do_filename[sim_do_depth], sim_do_label[sim_do_depth] ? "::" : "", sim_do_label[sim_do_depth] ? sim_do_label[sim_do_depth] : "", sim_goto_line[sim_do_depth]);
return cbuf;
}

t_stat do_cmd_label (int32 flag, CONST char *fcptr, CONST char *label)
{
char cbuf[4*CBUFSIZE], gbuf[CBUFSIZE], abuf[4*CBUFSIZE], quote, *c, *do_arg[11];
CONST char *cptr;
FILE *fpin;
CTAB *cmdp = NULL;
int32 echo, nargs, errabort, i;
int32 saved_sim_do_echo = sim_do_echo, 
      saved_sim_show_message = sim_show_message,
      saved_sim_on_inherit = sim_on_inherit,
      saved_sim_quiet = sim_quiet;
t_bool staying;
t_stat stat, stat_nomessage;

stat = SCPE_OK;
staying = TRUE;
if (flag > 0)                                           /* need switches? */
    GET_SWITCHES (fcptr);                               /* get switches */
echo = (sim_switches & SWMASK ('V')) || sim_do_echo;    /* -v means echo */
sim_quiet = (sim_switches & SWMASK ('Q')) || sim_quiet; /* -q means quiet */
sim_on_inherit =(sim_switches & SWMASK ('O')) || sim_on_inherit; /* -o means inherit ON condition actions */

errabort = sim_switches & SWMASK ('E');                 /* -e means abort on error */

abuf[sizeof(abuf)-1] = '\0';
strlcpy (abuf, fcptr, sizeof(abuf));
c = abuf;
do_arg[10] = NULL;                                      /* make sure the argument list always ends with a NULL */
for (nargs = 0; nargs < 10; ) {                         /* extract arguments */
    while (sim_isspace (*c))                            /* skip blanks */
        c++;
    if (*c == 0)                                        /* all done? */
        do_arg [nargs++] = NULL;                        /* null argument */
    else {
        if (*c == '\'' || *c == '"')                    /* quoted string? */
            quote = *c++;
        else quote = 0;
        do_arg[nargs++] = c;                            /* save start */
        while (*c && (quote ? (*c != quote) : !sim_isspace (*c)))
            c++;
        if (*c)                                         /* term at quote/spc */
            *c++ = 0;
        }
    }                                                   /* end for */

if (do_arg [0] == NULL)                                 /* need at least 1 */
    return SCPE_2FARG;
if ((fpin = fopen (do_arg[0], "r")) == NULL) {          /* file failed to open? */
    strlcpy (cbuf, do_arg[0], sizeof (cbuf));           /* try again with .sim extension */
    strlcat (cbuf, ".sim", sizeof (cbuf));
    if ((fpin = fopen (cbuf, "r")) == NULL) {           /* failed a second time? */
        if (flag == 0)                                  /* cmd line file? */
             fprintf (stderr, "Can't open file %s\n", do_arg[0]);
        return SCPE_OPENERR;                            /* return failure */
        }
    }
if (flag >= 0) {                                        /* Only bump nesting from command or nested */
    ++sim_do_depth;
    if (sim_on_inherit) {                               /* inherit ON condition actions? */
        sim_on_check[sim_do_depth] = sim_on_check[sim_do_depth-1]; /* inherit On mode */
        for (i=0; i<=SCPE_MAX_ERR; i++) {               /* replicate appropriate on commands */
            if (sim_on_actions[sim_do_depth-1][i]) {
                sim_on_actions[sim_do_depth][i] = (char *)malloc(1+strlen(sim_on_actions[sim_do_depth-1][i]));
                if (NULL == sim_on_actions[sim_do_depth][i]) {
                    while (--i >= 0) {
                        free(sim_on_actions[sim_do_depth][i]);
                        sim_on_actions[sim_do_depth][i] = NULL;
                        }
                    sim_on_check[sim_do_depth] = 0;
                    sim_brk_clract ();                  /* defang breakpoint actions */
                    --sim_do_depth;                     /* unwind nesting */
                    fclose (fpin);
                    return SCPE_MEM;
                    }
                strcpy(sim_on_actions[sim_do_depth][i], sim_on_actions[sim_do_depth-1][i]);
                }
            }
        }
    }

sim_debug (SIM_DBG_DO, sim_dflt_dev, "do_cmd_label(%d, flag=%d, '%s', '%s')\n", sim_do_depth, flag, fcptr, label ? label : "");
strlcpy( sim_do_filename[sim_do_depth], do_arg[0], 
         sizeof (sim_do_filename[sim_do_depth]));       /* stash away do file name for possible use by 'call' command */
sim_do_label[sim_do_depth] = label;                     /* stash away do label for possible use in messages */
sim_goto_line[sim_do_depth] = 0;
if (label) {
    sim_gotofile = fpin;
    sim_do_echo = echo;
    stat = goto_cmd (0, label);
    if (stat != SCPE_OK) {
        strcpy(cbuf, "RETURN SCPE_ARG");
        cptr = get_glyph (cbuf, gbuf, 0);               /* get command glyph */
        cmdp = find_cmd (gbuf);                         /* return the errorStage things to the stat will be returned */
        goto Cleanup_Return;
        }
    }
if (errabort)                                           /* -e flag? */
    set_on (1, NULL);                                   /* equivalent to ON ERROR RETURN */

do {
    if (stop_cpu) {                                     /* SIGINT? */
        if (sim_on_actions[sim_do_depth][ON_SIGINT_ACTION]) {
            stop_cpu = FALSE;
            sim_brk_setact (sim_on_actions[sim_do_depth][ON_SIGINT_ACTION]);/* Use specified action */
            }
        else
            break;                                      /* Exit this command procedure */
        }
    sim_do_ocptr[sim_do_depth] = cptr = sim_brk_getact (cbuf, sizeof(cbuf)); /* get bkpt action */
    if (!sim_do_ocptr[sim_do_depth]) {                  /* no pending action? */
        sim_do_ocptr[sim_do_depth] = cptr = read_line (cbuf, sizeof(cbuf), fpin);/* get cmd line */
        sim_goto_line[sim_do_depth] += 1;
        }
    if (cptr == NULL) {                                 /* EOF? */
        stat = SCPE_OK;                                 /* set good return */
        break;
        }
    sim_debug (SIM_DBG_DO, sim_dflt_dev, "Input Command:    %s\n", cbuf);
    sim_sub_args (cbuf, sizeof(cbuf), do_arg);          /* substitute args */
    sim_debug (SIM_DBG_DO, sim_dflt_dev, "Expanded Command: %s\n", cbuf);
    if (*cptr == 0)                                     /* ignore blank */
        continue;
    if (echo)                                           /* echo if -v */
        sim_printf("%s> %s\n", do_position(), cptr);
    sim_cmd_echoed = echo;
    if (*cptr == ':')                                   /* ignore label */
        continue;
    cptr = get_glyph_cmd (cptr, gbuf);                  /* get command glyph */
    sim_switches = 0;                                   /* init switches */
    sim_gotofile = fpin;
    sim_do_echo = echo;
    if ((cmdp = find_cmd (gbuf))) {                     /* lookup command */
        if (cmdp->action == &return_cmd)                /* RETURN command? */
            break;                                      /*    done! */
        if (cmdp->action == &do_cmd) {                  /* DO command? */
            if (sim_do_depth >= MAX_DO_NEST_LVL)        /* nest too deep? */
                stat = SCPE_NEST;
            else
                stat = do_cmd (sim_do_depth+1, cptr);   /* exec DO cmd */
            }
        else
            if (cmdp->action == &shift_cmd)             /* SHIFT command */
                stat = shift_args(do_arg, sizeof(do_arg)/sizeof(do_arg[0]));
            else
                stat = cmdp->action (cmdp->arg, cptr);  /* exec other cmd */
        }
    else
        stat = SCPE_UNK;                                /* bad cmd given */
    sim_debug (SIM_DBG_DO, sim_dflt_dev, "Command '%s', Result: 0x%X - %s\n", cmdp ? cmdp->name : "", stat, sim_error_text (stat));
    echo = sim_do_echo;                                 /* Allow for SET VERIFY */
    stat_nomessage = stat & SCPE_NOMESSAGE;             /* extract possible message supression flag */
    stat_nomessage = stat_nomessage || (!sim_show_message);/* Apply global suppression */
    stat = SCPE_BARE_STATUS(stat);                      /* remove possible flag */
    if (((stat != SCPE_OK) && (stat != SCPE_EXPECT)) ||
        ((cmdp->action != &return_cmd) &&
         (cmdp->action != &goto_cmd) &&
         (cmdp->action != &on_cmd) &&
         (cmdp->action != &echo_cmd) &&
         (cmdp->action != &echof_cmd) &&
         (cmdp->action != &sleep_cmd)))
        sim_last_cmd_stat = stat;                       /* save command error status */
    switch (stat) {
        case SCPE_AFAIL:
            staying = (sim_on_check[sim_do_depth] &&        /* if trap action defined */
                       sim_on_actions[sim_do_depth][stat]); /* use it, otherwise exit */
            break;
        case SCPE_EXIT:
            staying = FALSE;
            break;
        case SCPE_OK:
        case SCPE_STEP:
            break;
        default:
            break;
        }
    if ((stat >= SCPE_BASE) && (stat != SCPE_EXIT) &&   /* error from cmd? */
        (stat != SCPE_STEP)) {
        if (!echo &&                                    /* report if not echoing */
            !stat_nomessage &&                          /* and not suppressing messages */
            !(cmdp && cmdp->message)) {                 /* and not handling them specially */
            sim_printf("%s> %s\n", do_position(), sim_do_ocptr[sim_do_depth]);
            }
        }
    if (!stat_nomessage) {                              /* report error if not suppressed */
        if (cmdp && cmdp->message)                      /* special message handler */
            cmdp->message ((!echo && !sim_quiet) ? sim_do_ocptr[sim_do_depth] : NULL, stat);
        else
            if (stat >= SCPE_BASE)                      /* report error if not suppressed */
                sim_printf ("%s\n", sim_error_text (stat));
        }
    if (stat == SCPE_EXPECT)                            /* EXPECT status is non actionable */
        stat = SCPE_OK;                                 /* so adjust it to SCPE_OK */
    if (staying &&
        (sim_on_check[sim_do_depth]) && 
        (stat != SCPE_OK) &&
        (stat != SCPE_STEP)) {
        if ((stat <= SCPE_MAX_ERR) && sim_on_actions[sim_do_depth][stat])
            sim_brk_setact (sim_on_actions[sim_do_depth][stat]);
        else
            sim_brk_setact (sim_on_actions[sim_do_depth][0]);
        }
    if (sim_vm_post != NULL)
        (*sim_vm_post) (TRUE);
    } while (staying);
Cleanup_Return:
fclose (fpin);                                          /* close file */
sim_gotofile = NULL;
if (flag >= 0) {
    sim_do_echo = saved_sim_do_echo;                    /* restore echo state we entered with */
    sim_show_message = saved_sim_show_message;          /* restore message display state we entered with */
    sim_on_inherit = saved_sim_on_inherit;              /* restore ON inheritance state we entered with */
    sim_quiet = saved_sim_quiet;                        /* restore quiet mode we entered with */
    }
if ((flag >= 0) || (!sim_on_inherit)) {
    for (i=0; i<=SCPE_MAX_ERR; i++) {                    /* release any on commands */
        free (sim_on_actions[sim_do_depth][i]);
        sim_on_actions[sim_do_depth][i] = NULL;
        }
    sim_on_check[sim_do_depth] = 0;                     /* clear on mode */
    }
sim_debug (SIM_DBG_DO, sim_dflt_dev, "do_cmd_label - exiting - stat:%d (%d, flag=%d, '%s', '%s')\n", stat, sim_do_depth, flag, fcptr, label ? label : "");
if (flag >= 0) {
    sim_brk_clract ();                                  /* defang breakpoint actions */
    --sim_do_depth;                                     /* unwind nesting */
    }
if (cmdp && (cmdp->action == &return_cmd) && (0 != *cptr)) { /* return command with argument? */
    sim_string_to_stat (cptr, &stat);
    sim_last_cmd_stat = stat;                           /* save explicit status as command error status */
    if (sim_switches & SWMASK ('Q'))
        stat |= SCPE_NOMESSAGE;                         /* suppress error message display (in caller) if requested */
    return stat;                                        /* return with explicit return status */
    }
return stat | SCPE_NOMESSAGE;                           /* suppress message since we've already done that here */
}

/* Substitute_args - replace %n tokens in 'instr' with the do command's arguments
                     and other enviroment variables

   Calling sequence
   instr        =       input string
   instr_size   =       sizeof input string buffer
   do_arg[10]   =       arguments

   Token %0 expands to the command file name. 
   Token %n (n being a single digit) expands to the n'th argument
   Tonen %* expands to the whole set of arguments (%1 ... %9)

   The input sequence "%%" represents a literal "%".  All other 
   character combinations are rendered literally.

   Omitted parameters result in null-string substitutions.

   Tokens preceeded and followed by % characters are expanded as environment
   variables, and if one isn't found then can be one of several special 
   variables: 
          %DATE%              yyyy-mm-dd
          %TIME%              hh:mm:ss
          %DATETIME%          yyyy-mm-ddThh:mm:ss
          %STIME%             hh_mm_ss
          %CTIME%             Www Mmm dd hh:mm:ss yyyy
          %UTIME%             nnn (Unix time - seconds since 1/1/1970)
          %STATUS%            Status value from the last command executed
          %TSTATUS%           The text form of the last status value
          %SIM_VERIFY%        The Verify/Verbose mode of the current Do command file
          %SIM_VERBOSE%       The Verify/Verbose mode of the current Do command file
          %SIM_QUIET%         The Quiet mode of the current Do command file
          %SIM_MESSAGE%       The message display status of the current Do command file
   Environment variable lookups are done first with the precise name between 
   the % characters and if that fails, then the name between the % characters
   is upcased and a lookup of that valus is attempted.

   The first Space delimited token on the line is extracted in uppercase and 
   then looked up as an environment variable.  If found it the value is 
   supstituted for the original string before expanding everything else.  If 
   it is not found, then the original beginning token on the line is left 
   untouched.
*/

static const char *
_sim_gen_env_uplowcase (const char *gbuf, char *rbuf, size_t rbuf_size)
{
const char *ap;
char tbuf[CBUFSIZE];

ap = getenv(gbuf);                      /* first try using the literal name */
if (!ap) {
    get_glyph (gbuf, tbuf, 0);          /* now try using the upcased name */
    if (strcmp (gbuf, tbuf))            /* upcase different? */
        ap = getenv(tbuf);              /* lookup the upcase name */
    }
if (ap) {                               /* environment variable found? */
    strlcpy (rbuf, ap, rbuf_size);      /* Return the environment value */
    ap = rbuf;
    }
return ap;
}

/*
Environment variable substitution:

    %XYZ:str1=str2%

would expand the XYZ environment variable, substituting each occurrence
of "str1" in the expanded result with "str2".  "str2" can be the empty
string to effectively delete all occurrences of "str1" from the expanded
output.  "str1" can begin with an asterisk, in which case it will match
everything from the beginning of the expanded output to the first
occurrence of the remaining portion of str1.

May also specify substrings for an expansion.

    %XYZ:~10,5%

would expand the XYZ environment variable, and then use only the 5
characters that begin at the 11th (offset 10) character of the expanded
result.  If the length is not specified, then it defaults to the
remainder of the variable value.  If either number (offset or length) is
negative, then the number used is the length of the environment variable
value added to the offset or length specified.

    %XYZ:~-10%

would extract the last 10 characters of the XYZ variable.

    %XYZ:~0,-2%

would extract all but the last 2 characters of the XYZ variable.

 */

static void _sim_subststr_substr (const char *ops, char *rbuf, size_t rbuf_size)
{
int rbuf_len = (int)strlen (rbuf);
char *tstr = (char *)malloc (1 + rbuf_len);

strcpy (tstr, rbuf);

if (*ops == '~') {      /* Substring? */
    int offset = 0, length = rbuf_len;
    int o, l;

    switch (sscanf (ops + 1, "%d,%d", &o, &l)) {
        case 2:
            if (l < 0)
                length = rbuf_len - MIN(-l, rbuf_len);
            else
                length = l;
            /* fall through */
        case 1:
            if (o < 0)
                offset = rbuf_len - MIN(-o, rbuf_len);
            else
                offset = MIN(o, rbuf_len);
            break;
        case 0:
            offset = 0;
            length = rbuf_len;
            break;
        }
    if (offset + length > rbuf_len)
        length = rbuf_len - offset;
    memcpy (rbuf, tstr + offset, length);
    rbuf[length] = '\0';
    }
else {
    const char *eq;

    if ((eq = strchr (ops, '='))) {     /* Substitute? */
        const char *last = tstr;
        const char *curr = tstr;
        char *match = (char *)malloc (1 + (eq - ops));
        size_t move_size;
        t_bool asterisk_match;

        strlcpy (match, ops, 1 + (eq - ops));
        asterisk_match = (*ops == '*');
        if (asterisk_match)
            memmove (match, match + 1, 1 + strlen (match + 1));
        while ((curr = strstr (last, match))) {
            if (!asterisk_match) {
                move_size = MIN((size_t)(curr - last), rbuf_size);
                memcpy (rbuf, last, move_size);
                rbuf_size -= move_size;
                rbuf += move_size;
                }
            else
                asterisk_match = FALSE;
            move_size = MIN(strlen (eq + 1), rbuf_size);
            memcpy (rbuf, eq + 1, move_size);
            rbuf_size -= move_size;
            rbuf += move_size;
            curr += strlen (match);
            last = curr;
            }
        move_size = MIN(strlen (last), rbuf_size);
        memcpy (rbuf, last, move_size);
        rbuf_size -= move_size;
        rbuf += move_size;
        if (rbuf_size)
            *rbuf = '\0';
        free (match);
        }
    }
free (tstr);
}

static const char *
_sim_get_env_special (const char *gbuf, char *rbuf, size_t rbuf_size)
{
const char *ap;
const char *fixup_needed = strchr (gbuf, ':');
char *tgbuf = NULL;
size_t tgbuf_size = MAX(rbuf_size, 1 + (size_t)(fixup_needed - gbuf));

if (fixup_needed) {
    tgbuf = (char *)calloc (tgbuf_size, 1);
    memcpy (tgbuf, gbuf, (fixup_needed - gbuf));
    gbuf = tgbuf;
    }
ap = _sim_gen_env_uplowcase (gbuf, rbuf, rbuf_size);/* Look for environment variable */
if (!ap) {                              /* no environment variable found? */
    time_t now = (time_t)cmd_time.tv_sec;
    struct tm *tmnow = localtime(&now);

    /* ISO 8601 format date/time info */
    if (!strcmp ("DATE", gbuf)) {
        sprintf (rbuf, "%4d-%02d-%02d", tmnow->tm_year+1900, tmnow->tm_mon+1, tmnow->tm_mday);
        ap = rbuf;
        }
    else if (!strcmp ("TIME", gbuf)) {
        sprintf (rbuf, "%02d:%02d:%02d", tmnow->tm_hour, tmnow->tm_min, tmnow->tm_sec);
        ap = rbuf;
        }
    else if (!strcmp ("DATETIME", gbuf)) {
        sprintf (rbuf, "%04d-%02d-%02dT%02d:%02d:%02d", tmnow->tm_year+1900, tmnow->tm_mon+1, tmnow->tm_mday, tmnow->tm_hour, tmnow->tm_min, tmnow->tm_sec);
        ap = rbuf;
        }
    /* Locale oriented formatted date/time info */
    else if (!strcmp ("LDATE", gbuf)) {
        strftime (rbuf, rbuf_size, "%x", tmnow);
        ap = rbuf;
        }
    else if (!strcmp ("LTIME", gbuf)) {
#if defined(HAVE_C99_STRFTIME)
        strftime (rbuf, rbuf_size, "%r", tmnow);
#else
        strftime (rbuf, rbuf_size, "%p", tmnow);
        if (rbuf[0])
            strftime (rbuf, rbuf_size, "%I:%M:%S %p", tmnow);
        else
            strftime (rbuf, rbuf_size, "%H:%M:%S", tmnow);
#endif
        ap = rbuf;
        }
    else if (!strcmp ("CTIME", gbuf)) {
#if defined(HAVE_C99_STRFTIME)
        strftime (rbuf, rbuf_size, "%c", tmnow);
#else
        strcpy (rbuf, ctime(&now));
        rbuf[strlen (rbuf)-1] = '\0';    /* remove trailing \n */
#endif
        ap = rbuf;
        }
    else if (!strcmp ("UTIME", gbuf)) {
        sprintf (rbuf, "%" LL_FMT "d", (LL_TYPE)now);
        ap = rbuf;
        }
    /* Separate Date/Time info */
    else if (!strcmp ("DATE_YYYY", gbuf)) {/* Year (0000-9999) */
        strftime (rbuf, rbuf_size, "%Y", tmnow);
        ap = rbuf;
        }
    else if (!strcmp ("DATE_YY", gbuf)) {/* Year (00-99) */
        strftime (rbuf, rbuf_size, "%y", tmnow);
        ap = rbuf;
        }
    else if (!strcmp ("DATE_YC", gbuf)) {/* Century (year/100) */
        sprintf (rbuf, "%d", (tmnow->tm_year + 1900)/100);
        ap = rbuf;
        }
    else if ((!strcmp ("DATE_19XX_YY", gbuf)) || /* Year with same calendar */
             (!strcmp ("DATE_19XX_YYYY", gbuf))) {
        int year = tmnow->tm_year + 1900;
        int days = year - 2001;
        int leaps = days/4 - days/100 + days/400;
        int lyear = ((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0));
        int selector = ((days + leaps + 7) % 7) + lyear * 7;
        static int years[] = {90, 91, 97, 98, 99, 94, 89, 
                              96, 80, 92, 76, 88, 72, 84};
        int cal_year = years[selector];

        if (!strcmp ("DATE_19XX_YY", gbuf))
            sprintf (rbuf, "%d", cal_year);        /* 2 digit year */
        else
            sprintf (rbuf, "%d", cal_year + 1900); /* 4 digit year */
        ap = rbuf;
        }
    else if (!strcmp ("DATE_MM", gbuf)) {/* Month number (01-12) */
        strftime (rbuf, rbuf_size, "%m", tmnow);
        ap = rbuf;
        }
    else if (!strcmp ("DATE_MMM", gbuf)) {/* abbreviated Month name */
        strftime (rbuf, rbuf_size, "%b", tmnow);
        ap = rbuf;
        }
    else if (!strcmp ("DATE_MONTH", gbuf)) {/* full Month name */
        strftime (rbuf, rbuf_size, "%B", tmnow);
        ap = rbuf;
        }
    else if (!strcmp ("DATE_DD", gbuf)) {/* Day of Month (01-31) */
        strftime (rbuf, rbuf_size, "%d", tmnow);
        ap = rbuf;
        }
    else if (!strcmp ("DATE_D", gbuf)) { /* ISO 8601 weekday number (1-7) */
        sprintf (rbuf, "%d", (tmnow->tm_wday ? tmnow->tm_wday : 7));
        ap = rbuf;
        }
    else if ((!strcmp ("DATE_WW", gbuf)) ||   /* ISO 8601 week number (01-53) */
             (!strcmp ("DATE_WYYYY", gbuf))) {/* ISO 8601 week year number (0000-9999) */
        int iso_yr = tmnow->tm_year + 1900;
        int iso_wk = (tmnow->tm_yday + 11 - (tmnow->tm_wday ? tmnow->tm_wday : 7))/7;;

        if (iso_wk == 0) {
            iso_yr = iso_yr - 1;
            tmnow->tm_yday += 365 + (((iso_yr % 4) == 0) ? 1 : 0);  /* Adjust for Leap Year (Correct thru 2099) */
            iso_wk = (tmnow->tm_yday + 11 - (tmnow->tm_wday ? tmnow->tm_wday : 7))/7;
            }
        else
            if ((iso_wk == 53) && (((31 - tmnow->tm_mday) + tmnow->tm_wday) < 4)) {
                ++iso_yr;
                iso_wk = 1;
                }
        if (!strcmp ("DATE_WW", gbuf))
            sprintf (rbuf, "%02d", iso_wk);
        else
            sprintf (rbuf, "%04d", iso_yr);
        ap = rbuf;
        }
    else if (!strcmp ("DATE_JJJ", gbuf)) {/* day of year (001-366) */
        strftime (rbuf, rbuf_size, "%j", tmnow);
        ap = rbuf;
        }
    else if (!strcmp ("TIME_HH", gbuf)) {/* Hour of day (00-23) */
        strftime (rbuf, rbuf_size, "%H", tmnow);
        ap = rbuf;
        }
    else if (!strcmp ("TIME_MM", gbuf)) {/* Minute of hour (00-59) */
        strftime (rbuf, rbuf_size, "%M", tmnow);
        ap = rbuf;
        }
    else if (!strcmp ("TIME_SS", gbuf)) {/* Second of minute (00-59) */
        strftime (rbuf, rbuf_size, "%S", tmnow);
        ap = rbuf;
        }
    else if (!strcmp ("TIME_MSEC", gbuf)) {/* Milliseconds of Second (000-999) */
        sprintf (rbuf, "%03d", (int)(cmd_time.tv_nsec / 1000000));
        ap = rbuf;
        }
    else if (!strcmp ("STATUS", gbuf)) {
        sprintf (rbuf, "%08X", sim_last_cmd_stat);
        ap = rbuf;
        }
    else if (!strcmp ("TSTATUS", gbuf)) {
        sprintf (rbuf, "%s", sim_error_text (sim_last_cmd_stat));
        ap = rbuf;
        }
    else if (!strcmp ("SIM_VERIFY", gbuf)) {
        sprintf (rbuf, "%s", sim_do_echo ? "-V" : "");
        ap = rbuf;
        }
    else if (!strcmp ("SIM_VERBOSE", gbuf)) {
        sprintf (rbuf, "%s", sim_do_echo ? "-V" : "");
        ap = rbuf;
        }
    else if (!strcmp ("SIM_QUIET", gbuf)) {
        sprintf (rbuf, "%s", sim_quiet ? "-Q" : "");
        ap = rbuf;
        }
    else if (!strcmp ("SIM_MESSAGE", gbuf)) {
        sprintf (rbuf, "%s", sim_show_message ? "" : "-Q");
        ap = rbuf;
        }
    }
if (ap && fixup_needed) {   /* substring/substituted needed? */
    strlcpy (tgbuf, ap, tgbuf_size);
    _sim_subststr_substr (fixup_needed + 1, tgbuf, tgbuf_size);
    strlcpy (rbuf, tgbuf, rbuf_size);
    }
free (tgbuf);
return ap;
}

/* Substitute_args - replace %n tokens in 'instr' with the do command's arguments

   Calling sequence
   instr        =       input string
   tmpbuf       =       temp buffer
   maxstr       =       min (len (instr), len (tmpbuf))
   do_arg[10]   =       arguments

   Token "%0" represents the command file name.

   The input sequence "\%" represents a literal "%", and "\\" represents a
   literal "\".  All other character combinations are rendered literally.

   Omitted parameters result in null-string substitutions.
*/

void sim_sub_args (char *instr, size_t instr_size, char *do_arg[])
{
char gbuf[CBUFSIZE];
char *ip = instr, *op, *oend, *istart, *tmpbuf;
const char *ap;
char rbuf[CBUFSIZE];
int i;
size_t instr_off = 0;
size_t outstr_off = 0;

sim_exp_argv = do_arg;
clock_gettime(CLOCK_REALTIME, &cmd_time);
tmpbuf = (char *)malloc(instr_size);
op = tmpbuf;
oend = tmpbuf + instr_size - 2;
if (instr_size > sim_sub_instr_size) {
    sim_sub_instr = (char *)realloc (sim_sub_instr, instr_size*sizeof(*sim_sub_instr));
    sim_sub_instr_off = (size_t *)realloc (sim_sub_instr_off, instr_size*sizeof(*sim_sub_instr_off));
    sim_sub_instr_size = instr_size;
    }
sim_sub_instr_buf = instr;
strlcpy (sim_sub_instr, instr, instr_size*sizeof(*sim_sub_instr));
while (sim_isspace (*ip)) {                                 /* skip leading spaces */
    sim_sub_instr_off[outstr_off++] = ip - instr;
    *op++ = *ip++;
    }
/* If entire string is within quotes, strip the quotes */
if ((*ip == '"') || (*ip == '\'')) {                        /* start with a quote character? */
    const char *cptr = ip;
    char *tp = op;              /* use remainder of output buffer as temp buffer */

    cptr = get_glyph_quoted (cptr, tp, 0);                  /* get quoted string */
    while (sim_isspace (*cptr))
        ++cptr;                                             /* skip over trailing spaces */
    if (*cptr == '\0') {                                    /* full string was quoted? */
        uint32 dsize;

        if (SCPE_OK == sim_decode_quoted_string (tp, (uint8 *)tp, &dsize)) {
            tp[dsize] = '\0';
            while (sim_isspace (*tp))
                memmove (tp, tp + 1, strlen (tp));
            strlcpy (ip, tp, instr_size - (ip - instr));/* copy quoted contents to input buffer */
            strlcpy (sim_sub_instr + (ip -  instr), tp, instr_size - (ip - instr));
            }
        }
    }
istart = ip;
for (; *ip && (op < oend); ) {
    if ((ip [0] == '%') && (ip [1] == '%')) {           /* literal % insert? */
        sim_sub_instr_off[outstr_off++] = ip - instr;
        ip++;                                           /* skip one */
        *op++ = *ip++;                                  /* copy insert % */
        }
    else 
        if ((*ip == '%') && 
            (sim_isalnum(ip[1]) || (ip[1] == '*') || (ip[1] == '_'))) {/* sub? */
            if ((ip[1] >= '0') && (ip[1] <= ('9'))) {   /* %n = sub */
                ap = do_arg[ip[1] - '0'];
                for (i=0; i<ip[1] - '0'; ++i)           /* make sure we're not past the list end */
                    if (do_arg[i] == NULL) {
                        ap = NULL;
                        break;
                        }
                ip = ip + 2;
                }
            else if (ip[1] == '*') {                    /* %1 ... %9 = sub */
                memset (rbuf, '\0', sizeof(rbuf));
                ap = rbuf;
                for (i=1; i<=9; ++i)
                    if (do_arg[i] == NULL)
                        break;
                    else
                        if ((sizeof(rbuf)-strlen(rbuf)) < (2 + strlen(do_arg[i]))) {
                            if (strchr(do_arg[i], ' ')) { /* need to surround this argument with quotes */
                                char quote = '"';
                                if (strchr(do_arg[i], quote))
                                    quote = '\'';
                                sprintf(&rbuf[strlen(rbuf)], "%s%c%s%c\"", (i != 1) ? " " : "", quote, do_arg[i], quote);
                                }
                            else
                                sprintf(&rbuf[strlen(rbuf)], "%s%s", (i != 1) ? " " : "", do_arg[i]);
                            }
                        else
                            break;
                ip = ip + 2;
                }
            else {                                      /* check environment variable or special variables */
                get_glyph_nc (ip+1, gbuf, '%');         /* get the literal name */
                ap = _sim_get_env_special (gbuf, rbuf, sizeof (rbuf));
                ip += 1 + strlen (gbuf);
                if (*ip == '%') 
                    ++ip;
                }
            if (ap) {                                   /* non-null arg? */
                while (*ap && (op < oend)) {            /* copy the argument */
                    sim_sub_instr_off[outstr_off++] = ip - instr;
                    *op++ = *ap++;
                    }
                }
            }
        else
            if (ip == istart) {                         /* at beginning of input? */
                get_glyph (istart, gbuf, 0);            /* substitute initial token */
                ap = getenv(gbuf);                      /* if it is an environment variable name */
                if (!ap) {                              /* nope? */
                    sim_sub_instr_off[outstr_off++] = ip - instr;
                    *op++ = *ip++;                      /* press on with literal character */
                    continue;
                    }
                while (*ap && (op < oend)) {            /* copy the translation */
                    sim_sub_instr_off[outstr_off++] = ip - instr;
                    *op++ = *ap++;
                    }
                ip += strlen(gbuf);
                }
            else {
                sim_sub_instr_off[outstr_off++] = ip - instr;
                *op++ = *ip++;                          /* literal character */
                }
    }
*op = 0;                                                /* term buffer */
sim_sub_instr_off[outstr_off] = 0;
strcpy (instr, tmpbuf);
free (tmpbuf);
return;
}

t_stat shift_args (char *do_arg[], size_t arg_count)
{
size_t i;

for (i=1; i<arg_count-1; ++i)
    do_arg[i] = do_arg[i+1];
return SCPE_OK;
}

static
int sim_cmp_string (const char *s1, const char *s2)
{
long int v1, v2;
char *ep1, *ep2;

if (sim_switches & SWMASK ('F')) {      /* File Compare? */
    FILE *f1, *f2;
    int c1, c2;
    char *filename1, *filename2;

    filename1 = (char *)malloc (strlen (s1));
    strcpy (filename1, s1 + 1);
    filename1[strlen (filename1) - 1] = '\0';
    filename2 = (char *)malloc (strlen (s2));
    strcpy (filename2, s2 + 1);
    filename2[strlen (filename2) - 1] = '\0';

    f1 = fopen (filename1, "rb");
    f2 = fopen (filename2, "rb");
    free (filename1);
    free (filename2);
    if ((f1 == NULL) && (f2 == NULL))   /* Both can't open? */
        return 0;                       /* Call that equal */
    if (f1 == NULL) {
        fclose (f2);
        return -1;
        }
    if (f2 == NULL) {
        fclose (f1);
        return 1;
        }
    while (((c1 = fgetc (f1)) == (c2 = fgetc (f2))) &&
           (c1 != EOF)) ;
    fclose (f1);
    fclose (f2);
    return c1 - c2;
    }
v1 = strtol(s1+1, &ep1, 0);
v2 = strtol(s2+1, &ep2, 0);
if ((ep1 != s1 + strlen (s1) - 1) ||
    (ep2 != s2 + strlen (s2) - 1))
    return (strlen (s1) == strlen (s2)) ? strncmp (s1 + 1, s2 + 1, strlen (s1) - 2)
                                        : strcmp (s1, s2);
if (v1 == v2)
    return 0;
if (v1 < v2)
    return -1;
return 1;
}

/* Assert command
   If command

   Syntax: ASSERT {NOT} {<dev>} <reg>{<logical-op><value>}<conditional-op><value>
   Syntax: IF {NOT} {<dev>} <reg>{<logical-op><value>}<conditional-op><value> commandtoprocess{; additionalcommandtoprocess}...

       If NOT is specified, the resulting expression value is inverted.
       If <dev> is not specified, sim_dflt_dev (CPU) is assumed.  
       <value> is expressed in the radix specified for <reg>.  
       <logical-op> and <conditional-op> are the same as that
       allowed for examine and deposit search specifications.

   Syntax: ASSERT {-i} {NOT} "<string1>"|EnvVarName1 <compare-op> "<string2>"|EnvVarName2
   Syntax: IF {-i} {NOT} "<string1>"|EnvVarName1 <compare-op> "<string2>"|EnvVarName2 commandtoprocess{; additionalcommandtoprocess}...

       If -i is specified, the comparisons are done in a case insensitive manner.
       If NOT is specified, the resulting expression value is inverted.
       "<string1>" and "<string2>" are quote delimited strings which include 
       expansion references to environment variables in the simulator.
       <compare-op> can be any one of:
            ==  - equal
            EQU - equal
            !=  - not equal
            NEQ - not equal
            <   - less than
            LSS - less than
            <=  - less than or equal
            LEQ - less than or equal
            >   - greater than
            GTR - greater than
            >=  - greater than or equal
            GEQ - greater than or equal
*/
static CONST char *_get_string (CONST char *iptr, char *optr, char mchar)
{
const char *ap;
CONST char *tptr, *gptr;
REG *rptr;

tptr = (CONST char *)get_glyph_gen (iptr, optr, mchar, (sim_switches & SWMASK ('I')), TRUE, '\\');
if ((*optr != '"') && (*optr != '\'')) {
    ap = getenv (optr);
    if (!ap)
        return tptr;
    /* for legacy ASSERT/IF behavior give precidence to REGister names over Environment Variables */
    get_glyph (optr, optr, 0);
    rptr = find_reg (optr, &gptr, sim_dfdev);
    if (rptr)
        return tptr;
    snprintf (optr, CBUFSIZE - 1, "\"%s\"", ap);
    get_glyph_gen (optr, optr, 0, (sim_switches & SWMASK ('I')), TRUE, '\\');
    }
return tptr;
}

t_stat assert_cmd (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE], gbuf2[CBUFSIZE];
CONST char *tptr, *gptr;
REG *rptr;
uint32 idx;
t_stat r;
t_bool Not = FALSE;
t_bool Exist = FALSE;
t_bool result;
t_addr addr;
t_stat reason;

cptr = (CONST char *)get_sim_opt (CMD_OPT_SW|CMD_OPT_DFT, (CONST char *)cptr, &r);  
                                                        /* get sw, default */
sim_stabr.boolop = sim_staba.boolop = -1;               /* no relational op dflt */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
tptr = get_glyph (cptr, gbuf, 0);                       /* get token */
if (!strcmp (gbuf, "NOT")) {                            /* Conditional Inversion? */
    Not = TRUE;                                         /* remember that, and */
    cptr = (CONST char *)tptr;
    tptr = get_glyph (cptr, gbuf, 0);                   /* get next token */
    }
if (!strcmp (gbuf, "EXIST")) {                          /* File Exist Test? */
    Exist = TRUE;                                       /* remember that, and */
    cptr = (CONST char *)tptr;
    }
tptr = _get_string (cptr, gbuf, ' ');                   /* get first string */
if (Exist || (*gbuf == '"') || (*gbuf == '\'')) {       /* quoted string comparison? */
    char quote = *gbuf;
    char op[CBUFSIZE];
    static struct {
        const char *op;
        int aval;
        int bval;
        t_bool invert;
        } *optr, compare_ops[] =
        {
            {"==",   0,  0, FALSE},
            {"EQU",  0,  0, FALSE},
            {"!=",   0,  0, TRUE},
            {"NEQ",  0,  0, TRUE},
            {"<",   -1, -1, FALSE},
            {"LSS", -1, -1, FALSE},
            {"<=",   0, -1, FALSE},
            {"LEQ",  0, -1, FALSE},
            {">",    1,  1, FALSE},
            {"GTR",  1,  1, FALSE},
            {">=",   0,  1, FALSE},
            {"GEQ",  0,  1, FALSE},
            {NULL}};

    if (!*tptr)
        return SCPE_2FARG;
    cptr = tptr;
    while (sim_isspace (*cptr))                         /* skip spaces */
        ++cptr;
    if (!Exist) {
        get_glyph (cptr, op, quote);
        for (optr = compare_ops; optr->op; optr++)
            if (0 == strncmp (op, optr->op, strlen (optr->op)))
                break;
        if (!optr->op)
            return sim_messagef (SCPE_ARG, "Invalid operator: %s\n", op);
        cptr += strlen (optr->op);
        if ((!isspace (*cptr)) && isalpha (optr->op[strlen (optr->op) - 1]) && isalnum (*cptr))
            return sim_messagef (SCPE_ARG, "Invalid operator: %s\n", op);
        while (sim_isspace (*cptr))                     /* skip spaces */
            ++cptr;
        cptr = _get_string (cptr, gbuf2, 0);            /* get second string */
        if (*cptr) {                                    /* more? */
            if (flag)                                   /* ASSERT has no more args */
                return SCPE_2MARG;
            }
        else {
            if (!flag)
                return SCPE_2FARG;                      /* IF needs actions! */
            }
        result = sim_cmp_string (gbuf, gbuf2);
        result = ((result == optr->aval) || (result == optr->bval));
        if (optr->invert)
            result = !result;
        }
    else {
        FILE *f = fopen (gbuf, "r");
        if (f)
            fclose (f);
        result = (f != NULL);
        }
    }
else {
    while (sim_isspace (*cptr))                         /* skip spaces */
        ++cptr;
    if (*cptr == '(') {
        t_svalue value;

        if ((cptr > sim_sub_instr_buf) && ((size_t)(cptr - sim_sub_instr_buf) < sim_sub_instr_size))
            cptr = &sim_sub_instr[sim_sub_instr_off[cptr - sim_sub_instr_buf]]; /* get un-substituted string */
        cptr = sim_eval_expression (cptr, &value, TRUE, &r);
        result = (value != 0);
        }
    else {
        cptr = get_glyph (cptr, gbuf, 0);                   /* get register */
        rptr = find_reg (gbuf, &gptr, sim_dfdev);           /* parse register */
        if (rptr) {                                         /* got register? */
            if (*gptr == '[') {                             /* subscript? */
                if (rptr->depth <= 1)                       /* array register? */
                    return SCPE_ARG;
                idx = (uint32) strtotv (++gptr, &tptr, 10); /* convert index */
                if ((gptr == tptr) || (*tptr++ != ']'))
                    return SCPE_ARG;
                gptr = tptr;                                /* update */
                }
            else idx = 0;                                   /* not array */
            if (idx >= rptr->depth)                         /* validate subscript */
                return SCPE_SUB;
            }
        else {                                              /* not reg, check for memory */
            if (sim_dfdev && sim_vm_parse_addr)             /* get addr */
                addr = sim_vm_parse_addr (sim_dfdev, gbuf, &gptr);
            else
                addr = (t_addr) strtotv (gbuf, &gptr, sim_dfdev ? sim_dfdev->dradix : sim_dflt_dev->dradix);
            if (gbuf == gptr)                               /* not register? */
                return SCPE_NXREG;
            }
        if (*gptr != 0)                                     /* more? must be search */
            get_glyph (gptr, gbuf, 0);
        else {
            if (*cptr == 0)                                 /* must be more */
                return SCPE_2FARG;
            cptr = get_glyph (cptr, gbuf, 0);               /* get search cond */
            }
        if (*cptr) {                                        /* more? */
            if (flag)                                       /* ASSERT has no more args */
                return SCPE_2MARG;
            }
        else {
            if (!flag)                                      
                return SCPE_2FARG;                          /* IF needs actions! */
            }
        if (rptr) {                                         /* Handle register case */
            if (!get_rsearch (gbuf, rptr->radix, &sim_stabr) ||  /* parse condition */
                (sim_stabr.boolop == -1))                   /* relational op reqd */
                return SCPE_MISVAL;
            sim_eval[0] = get_rval (rptr, idx);             /* get register value */
            result = test_search (sim_eval, &sim_stabr);    /* test condition */
            }
        else {                                              /* Handle memory case */
            if (!get_asearch (gbuf, sim_dfdev ? sim_dfdev->dradix : sim_dflt_dev->dradix, &sim_staba) ||  /* parse condition */
                (sim_staba.boolop == -1))                    /* relational op reqd */
                return SCPE_MISVAL;
            reason = get_aval (addr, sim_dfdev, sim_dfunit);/* get data */
            if (reason != SCPE_OK)                          /* return if error */
                return reason;
            result = test_search (sim_eval, &sim_staba);    /* test condition */
            }
        }
    }
if ((cptr > sim_sub_instr_buf) && ((size_t)(cptr - sim_sub_instr_buf) < sim_sub_instr_size))
    cptr = &sim_sub_instr[sim_sub_instr_off[cptr - sim_sub_instr_buf]]; /* get un-substituted string */
if (Not ^ result) {
    if (!flag)
        sim_brk_setact (cptr);                          /* set up IF actions */
    }
else
    if (flag)
        return SCPE_AFAIL;                              /* return assert status */
return SCPE_OK;
}

/* Send command

   Syntax: SEND {After=m},{Delay=n},"string-to-send"

   After  - is an integer (>= 0) representing a number of instruction 
            delay before the initial characters is sent.  The after 
            parameter can is set by itself (with SEND AFTER=n). 
            The value specified then persists across SEND commands, 
            and is the default value used in subsequent SEND commands 
            which don't specify an explicit AFTER parameter.  This default
            value is visible externally via an environment variable.
   Delay  - is an integer (>= 0) representing a number of instruction 
            delay before and between characters being sent.  The
            delay parameter can is set by itself (with SEND DELAY=n) 
            The value specified persists across SEND commands, and is
            the default value used in subsequent SEND commands which
            don't specify an explicit DELAY parameter.  This default
            value is visible externally via an environment variable.
   String - must be quoted.  Quotes may be either single or double but the
            opening anc closing quote characters must match.  Within quotes 
            C style character escapes are allowed.  
            The following character escapes are explicitly supported:
        \r  Sends the ASCII Carriage Return character (Decimal value 13)
        \n  Sends the ASCII Linefeed character (Decimal value 10)
        \f  Sends the ASCII Formfeed character (Decimal value 12)
        \t  Sends the ASCII Horizontal Tab character (Decimal value 9)
        \v  Sends the ASCII Vertical Tab character (Decimal value 11)
        \b  Sends the ASCII Backspace character (Decimal value 8)
        \\  Sends the ASCII Backslash character (Decimal value 92)
        \'  Sends the ASCII Single Quote character (Decimal value 39)
        \"  Sends the ASCII Double Quote character (Decimal value 34)
        \?  Sends the ASCII Question Mark character (Decimal value 63)
        \e  Sends the ASCII Escape character (Decimal value 27)
     as well as octal character values of the form:
        \n{n{n}} where each n is an octal digit (0-7)
     and hext character values of the form:
        \xh{h} where each h is a hex digit (0-9A-Fa-f)
   */

static uint32 get_default_env_parameter (const char *dev_name, const char *param_name, uint32 default_value)
{
char varname[CBUFSIZE];
uint32 val;
char *endptr;
const char *colon = strchr (dev_name, ':');

if (colon)
    snprintf (varname, sizeof(varname), "%s_%*.*s_%s", param_name, (int)(colon-dev_name), (int)(colon-dev_name), dev_name, colon + 1);
else
    snprintf (varname, sizeof(varname), "%s_%s", param_name, dev_name);
if (!getenv (varname))
    val = default_value;
else {
    val = strtoul (getenv (varname), &endptr, 0);
    if (*endptr)
        val = default_value;
    }
return val;
}

static void set_default_env_parameter (const char *dev_name, const char *param_name, uint32 value)
{
char varname[CBUFSIZE];
char valbuf[CBUFSIZE];

const char *colon = strchr (dev_name, ':');

if (colon)
    snprintf (varname, sizeof(varname), "%s_%*.*s_%s", param_name, (int)(colon-dev_name), (int)(colon-dev_name), dev_name, colon + 1);
else
    snprintf (varname, sizeof(varname), "%s_%s", param_name, dev_name);
snprintf (valbuf, sizeof(valbuf), "%u", value);
setenv(varname, valbuf, 1);
}

t_stat send_cmd (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
CONST char *tptr;
uint8 dbuf[CBUFSIZE];
uint32 dsize = 0;
const char *dev_name;
uint32 delay;
t_bool delay_set = FALSE;
uint32 after;
t_bool after_set = FALSE;
t_stat r;
SEND *snd;

GET_SWITCHES (cptr);                                    /* get switches */
tptr = get_glyph (cptr, gbuf, ',');
if (sim_isalpha(gbuf[0]) && (strchr (gbuf, ':'))) {
    r = tmxr_locate_line_send (gbuf, &snd);
    if (r != SCPE_OK)
        return r;
    cptr = tptr;
    tptr = get_glyph (tptr, gbuf, ',');
    }
else
    snd = sim_cons_get_send ();
dev_name = tmxr_send_line_name (snd);
if (!flag)
    return sim_send_clear (snd);
delay = get_default_env_parameter (dev_name, "SIM_SEND_DELAY", SEND_DEFAULT_DELAY);
after = get_default_env_parameter (dev_name, "SIM_SEND_AFTER", delay);
while (*cptr) {
    if ((!strncmp(gbuf, "DELAY=", 6)) && (gbuf[6])) {
        delay = (uint32)get_uint (&gbuf[6], 10, 10000000, &r);
        if (r != SCPE_OK)
            return sim_messagef (SCPE_ARG, "Invalid Delay Value\n");
        cptr = tptr;
        tptr = get_glyph (cptr, gbuf, ',');
        delay_set = TRUE;
        if (!after_set)
            after = delay;
        continue;
        }
    if ((!strncmp(gbuf, "AFTER=", 6)) && (gbuf[6])) {
        after = (uint32)get_uint (&gbuf[6], 10, 10000000, &r);
        if (r != SCPE_OK)
            return sim_messagef (SCPE_ARG, "Invalid After Value\n");
        cptr = tptr;
        tptr = get_glyph (cptr, gbuf, ',');
        after_set = TRUE;
        continue;
        }
    if ((*cptr == '"') || (*cptr == '\''))
        break;
    return SCPE_ARG;
    }
if (!*cptr) {
    if ((!delay_set) && (!after_set))
        return SCPE_2FARG;
    set_default_env_parameter (dev_name, "SIM_SEND_DELAY", delay);
    set_default_env_parameter (dev_name, "SIM_SEND_AFTER", after);
    return SCPE_OK;
    }
if ((*cptr != '"') && (*cptr != '\''))
    return sim_messagef (SCPE_ARG, "String must be quote delimited\n");
cptr = get_glyph_quoted (cptr, gbuf, 0);
if (*cptr != '\0')
    return SCPE_2MARG;                  /* No more arguments */

if (SCPE_OK != sim_decode_quoted_string (gbuf, dbuf, &dsize))
    return sim_messagef (SCPE_ARG, "Invalid String\n");
return sim_send_input (snd, dbuf, dsize, after, delay);
}

t_stat sim_show_send (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
CONST char *tptr;
t_stat r;
SEND *snd;

tptr = get_glyph (cptr, gbuf, ',');
if (sim_isalpha(gbuf[0]) && (strchr (gbuf, ':'))) {
    r = tmxr_locate_line_send (gbuf, &snd);
    if (r != SCPE_OK)
        return r;
    cptr = tptr;
    }
else
    snd = sim_cons_get_send ();
if (*cptr)
    return SCPE_2MARG;
return sim_show_send_input (st, snd);
}

t_stat expect_cmd (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
CONST char *tptr;
t_stat r;
EXPECT *exp;

GET_SWITCHES (cptr);                                    /* get switches */
tptr = get_glyph (cptr, gbuf, ',');
if (sim_isalpha(gbuf[0]) && (strchr (gbuf, ':'))) {
    r = tmxr_locate_line_expect (gbuf, &exp);
    if (r != SCPE_OK)
        return sim_messagef (r, "No such active line: %s\n", gbuf);
    cptr = tptr;
    }
else
    exp = sim_cons_get_expect ();
if (flag)
    return sim_set_expect (exp, cptr);
else
    return sim_set_noexpect (exp, cptr);
}

t_stat sim_show_expect (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
CONST char *tptr;
t_stat r;
EXPECT *exp;

tptr = get_glyph (cptr, gbuf, ',');
if (sim_isalpha(gbuf[0]) && (strchr (gbuf, ':'))) {
    r = tmxr_locate_line_expect (gbuf, &exp);
    if (r != SCPE_OK)
        return r;
    cptr = tptr;
    }
else
    exp = sim_cons_get_expect ();
if (*cptr && (*cptr != '"') && (*cptr != '\''))
    return SCPE_ARG;            /* String must be quote delimited */
tptr = get_glyph_quoted (cptr, gbuf, 0);
if (*tptr != '\0')
    return SCPE_2MARG;          /* No more arguments */
if (*cptr && (cptr[strlen(cptr)-1] != '"') && (cptr[strlen(cptr)-1] != '\''))
    return SCPE_ARG;            /* String must be quote delimited */
return sim_exp_show (st, exp, gbuf);
}


/* Sleep command */

t_stat sleep_cmd (int32 flag, CONST char *cptr)
{
char *tptr;
double wait;

while (*cptr) {
    wait = strtod (cptr, &tptr);
    switch (*tptr) {
        case ' ':
        case '\t':
        case '\0':
            break;
        case 's':
        case 'S':
            ++tptr;
            break;
        case 'm':
        case 'M':
            ++tptr;
            wait *= 60.0;
            break;
        case 'h':
        case 'H':
            ++tptr;
            wait *= (60.0*60.0);
            break;
        case 'd':
        case 'D':
            ++tptr;
            wait *= (24.0*60.0*60.0);
            break;
        default:
            return sim_messagef (SCPE_ARG, "Invalid Sleep unit '%c'\n", *cptr);
        }
    wait *= 1000.0;                             /* Convert to Milliseconds */
    cptr = tptr;
    while ((wait > 1000.0) && (!stop_cpu))
        wait -= sim_os_ms_sleep (1000);
    if ((wait > 0.0) && (!stop_cpu))
        sim_os_ms_sleep ((unsigned)wait);
    }
stop_cpu = FALSE;                   /* Clear in case sleep was interrupted */
return SCPE_OK;
}

/* Goto command */

t_stat goto_cmd (int32 flag, CONST char *fcptr)
{
char cbuf[CBUFSIZE], gbuf[CBUFSIZE], gbuf1[CBUFSIZE];
const char *cptr;
long fpos;
int32 saved_do_echo = sim_do_echo;
int32 saved_goto_line = sim_goto_line[sim_do_depth];

if (NULL == sim_gotofile) return SCPE_UNK;              /* only valid inside of do_cmd */
get_glyph (fcptr, gbuf1, 0);
if ('\0' == gbuf1[0])                                   /* unspecified goto target */
    return sim_messagef (SCPE_ARG, "Missing goto target\n");
fpos = ftell(sim_gotofile);                             /* Save start position */
if (fpos < 0)
    return sim_messagef (SCPE_IERR, "goto ftell error: %s\n", strerror (errno));
rewind(sim_gotofile);                                   /* start search for label */
sim_goto_line[sim_do_depth] = 0;                        /* reset line number */
sim_do_echo = 0;                                        /* Don't echo while searching for label */
while (1) {
    cptr = read_line (cbuf, sizeof(cbuf), sim_gotofile);/* get cmd line */
    if (cptr == NULL) break;                            /* exit on eof */
    sim_goto_line[sim_do_depth] += 1;                   /* record line number */
    if (*cptr == 0) continue;                           /* ignore blank */
    if (*cptr != ':') continue;                         /* ignore non-labels */
    ++cptr;                                             /* skip : */
    while (sim_isspace (*cptr)) ++cptr;                 /* skip blanks */
    get_glyph (cptr, gbuf, 0);                          /* get label glyph */
    if (0 == strcmp(gbuf, gbuf1)) {
        sim_brk_clract ();                              /* goto defangs current actions */
        sim_do_echo = saved_do_echo;                    /* restore echo mode */
        if (sim_do_echo)                                /* echo if -v */
            sim_printf("%s> %s\n", do_position(), cbuf);
        return SCPE_OK;
        }
    }
sim_do_echo = saved_do_echo;                            /* restore echo mode */
sim_goto_line[sim_do_depth] = saved_goto_line;          /* restore start line number */
if (fseek(sim_gotofile, fpos, SEEK_SET))                /* restore start position */
    return sim_messagef (SCPE_IERR, "goto seek error: %s\n", strerror (errno));
return sim_messagef (SCPE_ARG, "goto target '%s' not found\n", gbuf1);
}

/* Return command */
/* The return command is invalid unless encountered in a do_cmd context, */
/* and in that context, it is handled as a special case inside of do_cmd() */
/* and not dispatched here, so if we get here a return has been issued from */
/* interactive input */

t_stat return_cmd (int32 flag, CONST char *fcptr)
{
return SCPE_UNK;                                        /* only valid inside of do_cmd */
}

/* Shift command */
/* The shift command is invalid unless encountered in a do_cmd context, */
/* and in that context, it is handled as a special case inside of do_cmd() */
/* and not dispatched here, so if we get here a shift has been issued from */
/* interactive input (it is not valid interactively since it would have to */
/* mess with the program's argv which is owned by the C runtime library */

t_stat shift_cmd (int32 flag, CONST char *fcptr)
{
return SCPE_UNK;                                        /* only valid inside of do_cmd */
}

/* Call command */
/* The call command is invalid unless encountered in a do_cmd context, */
/* and in that context, it is handled as a special case inside of do_cmd() */
/* and not dispatched here, so if we get here a call has been issued from */
/* interactive input */

t_stat call_cmd (int32 flag, CONST char *fcptr)
{
char cbuf[CBUFSIZE], gbuf[CBUFSIZE];
const char *cptr;

if (NULL == sim_gotofile) return SCPE_UNK;              /* only valid inside of do_cmd */
cptr = get_glyph (fcptr, gbuf, 0);
if ('\0' == gbuf[0]) return SCPE_ARG;                   /* unspecified goto target */
sprintf(cbuf, "%s %s", sim_do_filename[sim_do_depth], cptr);
sim_switches |= SWMASK ('O');                           /* inherit ON state and actions */
return do_cmd_label (flag, cbuf, gbuf);
}

/* On command */

t_stat on_cmd (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
t_stat cond;

cptr = get_glyph (cptr, gbuf, 0);
if ('\0' == gbuf[0]) return SCPE_ARG;                   /* unspecified condition */
if (0 == strcmp("ERROR", gbuf))
    cond = 0;
else {
    if (SCPE_OK != sim_string_to_stat (gbuf, &cond)) {
        if ((MATCH_CMD (gbuf, "CONTROL_C") == 0) || 
            (MATCH_CMD (gbuf, "SIGINT") == 0))
            cond = ON_SIGINT_ACTION;                    /* Special case */
        else
            return sim_messagef (SCPE_ARG, "Invalid argument: %s\n", gbuf);
        }
    }
if (cond == SCPE_OK)
    return sim_messagef (SCPE_ARG, "Invalid argument: %s\n", gbuf);
if ((NULL == cptr) || ('\0' == *cptr)) {                /* Empty Action */
    free(sim_on_actions[sim_do_depth][cond]);           /* Clear existing condition */
    sim_on_actions[sim_do_depth][cond] = NULL; }
else {
    sim_on_actions[sim_do_depth][cond] = 
        (char *)realloc(sim_on_actions[sim_do_depth][cond], 1+strlen(cptr));
    strcpy(sim_on_actions[sim_do_depth][cond], cptr);
    }
return SCPE_OK;
}

/* noop command */
/* The noop command (IGNORE, PROCEED) does nothing */

t_stat noop_cmd (int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
return SCPE_OK;                                         /* we're happy doing nothing */
}

/* Set on/noon routine */

t_stat set_on (int32 flag, CONST char *cptr)
{
if ((flag) && (cptr) && (*cptr)) {                      /* Set ON with arg */
    char gbuf[CBUFSIZE];

    cptr = get_glyph (cptr, gbuf, 0);                   /* get command glyph */
    if (((MATCH_CMD(gbuf,"INHERIT")) &&
         (MATCH_CMD(gbuf,"NOINHERIT"))) ||
        (*cptr))
        return SCPE_2MARG;
    if ((gbuf[0]) && (0 == MATCH_CMD(gbuf,"INHERIT")))
        sim_on_inherit = 1;
    if ((gbuf[0]) && (0 == MATCH_CMD(gbuf,"NOINHERIT")))
        sim_on_inherit = 0;
    return SCPE_OK;
    }
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
sim_on_check[sim_do_depth] = flag;
if ((sim_do_depth != 0) && 
    (NULL == sim_on_actions[sim_do_depth][0])) {        /* default handler set? */
    sim_on_actions[sim_do_depth][0] =                   /* No, so make "RETURN" */
        (char *)malloc(1+strlen("RETURN"));             /* be the default action */
    strcpy(sim_on_actions[sim_do_depth][0], "RETURN");
    }
if ((sim_do_depth != 0) && 
    (NULL == sim_on_actions[sim_do_depth][SCPE_AFAIL])) {/* handler set for AFAIL? */
    sim_on_actions[sim_do_depth][SCPE_AFAIL] =          /* No, so make "RETURN" */
        (char *)malloc(1+strlen("RETURN"));             /* be the action */
    strcpy(sim_on_actions[sim_do_depth][SCPE_AFAIL], "RETURN");
    }
return SCPE_OK;
}

/* Set verify/noverify routine */

t_stat set_verify (int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
if (flag == sim_do_echo)                                /* already set correctly? */
    return SCPE_OK;
sim_do_echo = flag;
return SCPE_OK;
}

/* Set message/nomessage routine */

t_stat set_message (int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
if (flag == sim_show_message)                           /* already set correctly? */
    return SCPE_OK;
sim_show_message = flag;
return SCPE_OK;
}

/* Set quiet/noquiet routine */

t_stat set_quiet (int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
if (flag == sim_quiet)                                  /* already set correctly? */
    return SCPE_OK;
sim_quiet = flag;
return SCPE_OK;
}

/* Set asynch/noasynch routine */

t_stat sim_set_asynch (int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
#ifdef SIM_ASYNCH_IO
if (flag == sim_asynch_enabled)                         /* already set correctly? */
    return SCPE_OK;
sim_asynch_enabled = flag;
tmxr_change_async ();
sim_timer_change_asynch ();
if (1) {
    uint32 i, j;
    DEVICE *dptr;
    UNIT *uptr;

    /* Call unit flush routines to report asynch status change to device layer */
    for (i = 1; (dptr = sim_devices[i]) != NULL; i++) { /* flush attached files */
        for (j = 0; j < dptr->numunits; j++) {          /* if not buffered in mem */
            uptr = dptr->units + j;
            if ((uptr->flags & UNIT_ATT) &&             /* attached, */
                (uptr->io_flush))                       /* unit specific flush routine */
                uptr->io_flush (uptr);
            }
        }
    }
if (!sim_quiet)
    fprintf (stdout, "Asynchronous I/O %sabled\n", sim_asynch_enabled ? "en" : "dis");
if ((!sim_oline) && sim_log)
    fprintf (sim_log, "Asynchronous I/O %sabled\n", sim_asynch_enabled ? "en" : "dis");
return SCPE_OK;
#else
if (!sim_quiet)
    fprintf (stdout, "Asynchronous I/O is not available in this simulator\n");
if ((!sim_oline) && sim_log)
    fprintf (sim_log, "Asynchronous I/O is not available in this simulator\n");
return SCPE_NOFNC;
#endif
}

/* Show asynch routine */

t_stat sim_show_asynch (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
#ifdef SIM_ASYNCH_IO
fprintf (st, "Asynchronous I/O is %sabled, %s\n", (sim_asynch_enabled) ? "en" : "dis", AIO_QUEUE_MODE);
#if defined(SIM_ASYNCH_MUX)
fprintf (st, "Asynchronous Multiplexer support is available\n");
#endif
#if defined(SIM_ASYNCH_CLOCKS)
fprintf (st, "Asynchronous Clock is %sabled\n", (sim_asynch_timer) ? "en" : "dis");
#endif
#else
fprintf (st, "Asynchronous I/O is not available in this simulator\n");
#endif
return SCPE_OK;
}

/* Set environment routine */

t_stat sim_set_environment (int32 flag, CONST char *cptr)
{
char varname[CBUFSIZE], prompt[CBUFSIZE], cbuf[CBUFSIZE];

if ((!cptr) || (*cptr == 0))                            /* now eol? */
    return SCPE_2FARG;
if (sim_switches & SWMASK ('P')) {
    CONST char *deflt = NULL;

    cptr = get_glyph_quoted (cptr, prompt, 0);          /* get prompt */
    if (prompt[0] == '\0')
        return sim_messagef (SCPE_2FARG, "Missing Prompt and Environment Variable Name\n");
    if ((prompt[0] == '"') || (prompt[0] == '\'')) {
        prompt[strlen (prompt) - 1] = '\0';
        memmove (prompt, prompt + 1, strlen (prompt));
        }
    deflt = get_glyph (cptr, varname, '=');             /* get environment variable name */
    if (deflt == NULL)
        deflt = "";
    if (*deflt) {
        strlcat (prompt, " [", sizeof (prompt));
        strlcat (prompt, deflt, sizeof (prompt));
        strlcat (prompt, "] ", sizeof (prompt));
        }
    else
        strlcat (prompt, " ", sizeof (prompt));
    if (sim_rem_cmd_active_line == -1) {
        cptr = read_line_p (prompt, cbuf, sizeof(cbuf), stdin);
        if ((cptr == NULL) || (*cptr == 0))
            cptr = deflt;
        else
            cptr = cbuf;
        }
    else
        cptr = deflt;
    }
else {
    cptr = get_glyph (cptr, varname, '=');              /* get environment variable name */
    strlcpy (cbuf, cptr, sizeof(cbuf));
    sim_trim_endspc (cbuf);
    if (sim_switches & SWMASK ('S')) {                  /* Quote String argument? */
        uint32 str_size;

        cptr = cbuf;
        get_glyph_quoted (cptr, cbuf, 0);
        if (SCPE_OK != sim_decode_quoted_string (cbuf, (uint8 *)cbuf, &str_size)) 
            return sim_messagef (SCPE_ARG, "Invalid quoted string: %s\n", cbuf);
        cbuf[str_size] = '\0';
        }
    else {
        if (sim_switches & SWMASK ('A')) {              /* Arithmentic Expression Evaluation argument? */
            t_svalue val;
            t_stat stat;
            const char *eptr = cptr;

            if ((cptr > sim_sub_instr_buf) && ((size_t)(cptr - sim_sub_instr_buf) < sim_sub_instr_size))
                eptr = &sim_sub_instr[sim_sub_instr_off[cptr - sim_sub_instr_buf]]; /* get un-substituted string */
            cptr = sim_eval_expression (eptr, &val, FALSE, &stat);
            if (stat == SCPE_OK) {
                sprintf (cbuf, "%ld", (long)val);
                cptr = cbuf;
                }
            else
                return stat;
            }
        }
    }
setenv(varname, cptr, 1);
return SCPE_OK;
}

/* Set command */

t_stat set_cmd (int32 flag, CONST char *cptr)
{
uint32 lvl = 0;
t_stat r;
char gbuf[CBUFSIZE], *cvptr;
CONST char *svptr;
DEVICE *dptr;
UNIT *uptr;
MTAB *mptr;
CTAB *gcmdp;
C1TAB *ctbr = NULL, *glbr;

GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
cptr = get_glyph (svptr = cptr, gbuf, 0);               /* get glob/dev/unit */

if ((dptr = find_dev (gbuf))) {                         /* device match? */
    uptr = dptr->units;                                 /* first unit */
    ctbr = set_dev_tab;                                 /* global table */
    lvl = MTAB_VDV;                                     /* device match */
    GET_SWITCHES (cptr);                                /* get more switches */
    }
else if ((dptr = find_unit (gbuf, &uptr))) {            /* unit match? */
    if (uptr == NULL)                                   /* invalid unit */
        return SCPE_NXUN;
    ctbr = set_unit_tab;                                /* global table */
    lvl = MTAB_VUN;                                     /* unit match */
    GET_SWITCHES (cptr);                                /* get more switches */
    }
else if ((gcmdp = find_ctab (set_glob_tab, gbuf))) {    /* global? */
    GET_SWITCHES (cptr);                                /* get more switches */
    return gcmdp->action (gcmdp->arg, cptr);            /* do the rest */
    }
else {
    if (sim_dflt_dev->modifiers) {
        if ((cvptr = strchr (gbuf, '=')))               /* = value? */
            *cvptr++ = 0;
        for (mptr = sim_dflt_dev->modifiers; mptr->mask != 0; mptr++) {
            if (mptr->mstring && (MATCH_CMD (gbuf, mptr->mstring) == 0)) {
                dptr = sim_dflt_dev;
                cptr = svptr;
                while (sim_isspace(*cptr))
                    ++cptr;
                break;
                }
            }
        }
    if (!dptr)
        return SCPE_NXDEV;                              /* no match */
    lvl = MTAB_VDV;                                     /* device match */
    uptr = dptr->units;                                 /* first unit */
    }
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
GET_SWITCHES (cptr);                                    /* get more switches */

while (*cptr != 0) {                                    /* do all mods */
    cptr = get_glyph (svptr = cptr, gbuf, ',');         /* get modifier */
    if ((cvptr = strchr (gbuf, '=')))                   /* = value? */
        *cvptr++ = 0;
    for (mptr = dptr->modifiers; mptr && (mptr->mask != 0); mptr++) {
        if ((mptr->mstring) &&                          /* match string */
            (MATCH_CMD (gbuf, mptr->mstring) == 0)) {   /* matches option? */
            if (mptr->mask & MTAB_XTD) {                /* extended? */
                if (((lvl & mptr->mask) & ~MTAB_XTD) == 0)
                    return SCPE_ARG;
                if ((lvl == MTAB_VUN) && (uptr->flags & UNIT_DIS))
                    return SCPE_UDIS;                   /* unit disabled? */
                if (mptr->valid) {                      /* validation rtn? */
                    if (cvptr && MODMASK(mptr,MTAB_QUOTE)) {
                        get_glyph_quoted (svptr, gbuf, ',');
                        if ((cvptr = strchr (gbuf, '=')))
                            *cvptr++ = 0;
                        }
                    else {
                        if (cvptr && MODMASK(mptr,MTAB_NC)) {
                            get_glyph_nc (svptr, gbuf, ',');
                            if ((cvptr = strchr (gbuf, '=')))
                                *cvptr++ = 0;
                            }
                        }
                    r = mptr->valid (uptr, mptr->match, cvptr, mptr->desc);
                    if (r != SCPE_OK)
                        return r;
                    }
                else if (!mptr->desc)                   /* value desc? */
                    break;
//                else if (mptr->mask & MTAB_VAL) {     /* take a value? */
//                    if (!cvptr) return SCPE_MISVAL;   /* none? error */
//                    r = dep_reg (0, cvptr, (REG *) mptr->desc, 0);
//                    if (r != SCPE_OK) return r;
//                    }
                else if (cvptr)                         /* = value? */
                    return SCPE_ARG;
                else *((int32 *) mptr->desc) = mptr->match;
                }                                       /* end if xtd */
            else {                                      /* old style */
                if (cvptr)                              /* = value? */
                    return SCPE_ARG;
                if (uptr->flags & UNIT_DIS)             /* disabled? */
                    return SCPE_UDIS;
                if ((mptr->valid) &&                    /* invalid? */
                    ((r = mptr->valid (uptr, mptr->match, cvptr, mptr->desc)) != SCPE_OK))
                    return r;
                uptr->flags = (uptr->flags & ~(mptr->mask)) |
                    (mptr->match & mptr->mask);         /* set new value */
                }                                       /* end else xtd */
            break;                                      /* terminate for */
            }                                           /* end if match */
        }                                               /* end for */
    if (!mptr || (mptr->mask == 0)) {                   /* no match? */
        if ((glbr = find_c1tab (ctbr, gbuf))) {         /* global match? */
            r = glbr->action (dptr, uptr, glbr->arg, cvptr);    /* do global */
            if (r != SCPE_OK)
                return r;
            }
        else if (!dptr->modifiers)                      /* no modifiers? */
            return SCPE_NOPARAM;
        else return SCPE_NXPAR;
        }                                               /* end if no mat */
    }                                                   /* end while */
return SCPE_OK;                                         /* done all */
}

/* Match CTAB/CTAB1 name */

CTAB *find_ctab (CTAB *tab, const char *gbuf)
{
if (!tab)
    return NULL;
for (; tab->name != NULL; tab++) {
    if (MATCH_CMD (gbuf, tab->name) == 0)
        return tab;
    }
return NULL;
}

C1TAB *find_c1tab (C1TAB *tab, const char *gbuf)
{
if (!tab)
    return NULL;
for (; tab->name != NULL; tab++) {
    if (MATCH_CMD (gbuf, tab->name) == 0)
        return tab;
    }
return NULL;
}

/* Set device data radix routine */

t_stat set_dev_radix (DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
if (cptr)
    return SCPE_ARG;
dptr->dradix = flag & 037;
return SCPE_OK;
}

/* Set device enabled/disabled routine */

t_stat set_dev_enbdis (DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
UNIT *up;
uint32 i;

if (cptr)
    return SCPE_ARG;
if ((dptr->flags & DEV_DISABLE) == 0)                   /* allowed? */
    return SCPE_NOFNC;
if (flag) {                                             /* enable? */
    if ((dptr->flags & DEV_DIS) == 0)                   /* already enb? ok */
        return SCPE_OK;
    dptr->flags = dptr->flags & ~DEV_DIS;               /* no, enable */
    }
else {
    if (dptr->flags & DEV_DIS)                          /* already dsb? ok */
        return SCPE_OK;
    for (i = 0; i < dptr->numunits; i++) {              /* check units */
        up = (dptr->units) + i;                         /* att or active? */
        if ((up->flags & UNIT_ATT) || sim_is_active (up))
            return SCPE_NOFNC;                          /* can't do it */
        }
    dptr->flags = dptr->flags | DEV_DIS;                /* disable */
    }
if (dptr->reset)                                        /* reset device */
    return dptr->reset (dptr);
else return SCPE_OK;
}

/* Set unit enabled/disabled routine */

t_stat set_unit_enbdis (DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
if (cptr)
    return SCPE_ARG;
if (!(uptr->flags & UNIT_DISABLE))                      /* allowed? */
    return SCPE_NOFNC;
if (flag)                                               /* enb? enable */
    uptr->flags = uptr->flags & ~UNIT_DIS;
else {
    if ((uptr->flags & UNIT_ATT) ||                     /* dsb */
        sim_is_active (uptr))                           /* more tests */
        return SCPE_NOFNC;
    uptr->flags = uptr->flags | UNIT_DIS;               /* disable */
    }
return SCPE_OK;
}

/* Set device/unit debug enabled/disabled routine */

t_stat set_dev_debug (DEVICE *dptr, UNIT *uptr, int32 flags, CONST char *cptr)
{
int32 flag = flags & 1;
t_bool uflag = ((flags & 2) != 0);
char gbuf[CBUFSIZE];
DEBTAB *dep;

if ((dptr->flags & DEV_DEBUG) == 0)
    return SCPE_NOFNC;
if (cptr == NULL) {                                     /* no arguments? */
    if (uflag)
        uptr->dctrl = flag ? (dptr->debflags ? flag : 0xFFFFFFFF) : 0;/* disable/enable w/o table */
    else
        dptr->dctrl = flag ? (dptr->debflags ? flag : 0xFFFFFFFF) : 0;/* disable/enable w/o table */
    if (flag && dptr->debflags) {                       /* enable with table? */
        for (dep = dptr->debflags; dep->name != NULL; dep++)
            if (uflag)
                uptr->dctrl = uptr->dctrl | dep->mask;      /* set all */
            else
                dptr->dctrl = dptr->dctrl | dep->mask;      /* set all */
        }
    return SCPE_OK;
    }
if (dptr->debflags == NULL)                             /* must have table */
    return SCPE_ARG;
while (*cptr) {
    cptr = get_glyph (cptr, gbuf, ';');                 /* get debug flag */
    for (dep = dptr->debflags; dep->name != NULL; dep++) {
        if (strcmp (dep->name, gbuf) == 0) {            /* match? */
            if (flag)
                if (uflag)
                    uptr->dctrl = uptr->dctrl | dep->mask;
                else
                    dptr->dctrl = dptr->dctrl | dep->mask;
            else
                if (uflag)
                    uptr->dctrl = uptr->dctrl & ~dep->mask;
                else
                    dptr->dctrl = dptr->dctrl & ~dep->mask;
            break;
            }
        }                                               /* end for */
    if (dep->mask == 0)                                 /* no match? */
        return SCPE_ARG;
    }                                                   /* end while */
return SCPE_OK;
}

/* Show command */

t_stat show_cmd (int32 flag, CONST char *cptr)
{
t_stat r;

cptr = get_sim_opt (CMD_OPT_SW|CMD_OPT_OF, cptr, &r);
                                                        /* get sw, ofile */
if (!cptr)                                              /* error? */
    return r;
if (sim_ofile) {                                        /* output file? */
    r = show_cmd_fi (sim_ofile, flag, cptr);            /* do show */
    fclose (sim_ofile);
    }
else {
    r = show_cmd_fi (stdout, flag, cptr);               /* no, stdout, log */
    if ((!sim_oline) && (sim_log && (sim_log != stdout)))
        show_cmd_fi (sim_log, flag, cptr);
    if ((!sim_oline) && (sim_deb && (sim_deb != stdout) && (sim_deb != sim_log)))
        show_cmd_fi (sim_deb, flag, cptr);
    }
return r;
}

t_stat show_cmd_fi (FILE *ofile, int32 flag, CONST char *cptr)
{
uint32 lvl = 0xFFFFFFFF;
char gbuf[CBUFSIZE], *cvptr;
CONST char *svptr;
DEVICE *dptr;
UNIT *uptr;
MTAB *mptr;
SHTAB *shtb = NULL, *shptr;

GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
cptr = get_glyph (svptr = cptr, gbuf, 0);               /* get next glyph */

if ((dptr = find_dev (gbuf))) {                         /* device match? */
    uptr = dptr->units;                                 /* first unit */
    shtb = show_dev_tab;                                /* global table */
    lvl = MTAB_VDV;                                     /* device match */
    GET_SWITCHES (cptr);                                /* get more switches */
    }
else if ((dptr = find_unit (gbuf, &uptr))) {            /* unit match? */
    if (uptr == NULL)                                   /* invalid unit */
        return SCPE_NXUN;
    if (uptr->flags & UNIT_DIS)                         /* disabled? */
        return SCPE_UDIS;
    shtb = show_unit_tab;                               /* global table */
    lvl = MTAB_VUN;                                     /* unit match */
    GET_SWITCHES (cptr);                                /* get more switches */
    }
else if ((shptr = find_shtab (show_glob_tab, gbuf))) {  /* global? */
    GET_SWITCHES (cptr);                                /* get more switches */
    return shptr->action (ofile, NULL, NULL, shptr->arg, cptr);
    }
else {
    if (sim_dflt_dev->modifiers) {
        if ((cvptr = strchr (gbuf, '=')))               /* = value? */
            *cvptr++ = 0;
        for (mptr = sim_dflt_dev->modifiers; mptr->mask != 0; mptr++) {
            if ((((mptr->mask & MTAB_VDV) == MTAB_VDV) &&
                 (mptr->pstring && (MATCH_CMD (gbuf, mptr->pstring) == 0))) ||
                (!(mptr->mask & MTAB_VDV) && (mptr->mstring && (MATCH_CMD (gbuf, mptr->mstring) == 0)))) {
                dptr = sim_dflt_dev;
                lvl = MTAB_VDV;                         /* device match */
                cptr = svptr;
                while (sim_isspace(*cptr))
                    ++cptr;
                break;
                }
            }
        }
    if (!dptr) {
        if ((shptr = find_shtab (show_dev_tab, gbuf)))  /* global match? */
            return shptr->action (ofile, sim_dflt_dev, uptr, shptr->arg, cptr);
        else
            return SCPE_NXDEV;                          /* no match */
        }
    }

if (*cptr == 0) {                                       /* now eol? */
    return (lvl == MTAB_VDV)?
        show_device (ofile, dptr, 0):
        show_unit (ofile, dptr, uptr, -1);
    }
GET_SWITCHES (cptr);                                    /* get more switches */

while (*cptr != 0) {                                    /* do all mods */
    cptr = get_glyph (cptr, gbuf, ',');                 /* get modifier */
    if ((cvptr = strchr (gbuf, '=')))                   /* = value? */
        *cvptr++ = 0;
    for (mptr = dptr->modifiers; mptr && (mptr->mask != 0); mptr++) {
        if (((mptr->mask & MTAB_XTD)?                   /* right level? */
            ((mptr->mask & lvl) == lvl): (MTAB_VUN & lvl)) &&
            ((mptr->disp && mptr->pstring &&            /* named disp? */
            (MATCH_CMD (gbuf, mptr->pstring) == 0))
 //           ||
 //           ((mptr->mask & MTAB_VAL) &&                 /* named value? */
 //           mptr->mstring &&
 //           (MATCH_CMD (gbuf, mptr->mstring) == 0)))
            )) {
            if (cvptr && !(mptr->mask & MTAB_SHP))
                return SCPE_ARG;
            show_one_mod (ofile, dptr, uptr, mptr, cvptr, 1);
            break;
            }                                           /* end if */
        }                                               /* end for */
    if (!mptr || (mptr->mask == 0)) {                   /* no match? */
        if (shtb && (shptr = find_shtab (shtb, gbuf))) {/* global match? */
            t_stat r;

            r = shptr->action (ofile, dptr, uptr, shptr->arg, cptr);
            if (r != SCPE_OK)
                return r;
            }
        else if (!dptr->modifiers)                      /* no modifiers? */
            return SCPE_NOPARAM;
        else
            return SCPE_NXPAR;
        }                                               /* end if */
    }                                                   /* end while */
return SCPE_OK;
}

SHTAB *find_shtab (SHTAB *tab, const char *gbuf)
{
if (!tab)
    return NULL;
for (; tab->name != NULL; tab++) {
    if (MATCH_CMD (gbuf, tab->name) == 0)
        return tab;
    }
return NULL;
}

/* Show device and unit */

t_stat show_device (FILE *st, DEVICE *dptr, int32 flag)
{
uint32 j, udbl, ucnt;
UNIT *uptr;
int32 toks = 0;

fprintf (st, "%s", sim_dname (dptr));                   /* print dev name */
if ((flag == 2) && dptr->description) {
    fprintf (st, "\t%s\n", dptr->description(dptr));
    }
else {
    if ((sim_switches & SWMASK ('D')) && dptr->description)
        fprintf (st, "\t%s\n", dptr->description(dptr));
    }
if (qdisable (dptr)) {                                  /* disabled? */
    fprintf (st, "\tdisabled\n");
    return SCPE_OK;
    }
for (j = ucnt = udbl = 0; j < dptr->numunits; j++) {    /* count units */
    uptr = dptr->units + j;
    if (!(uptr->flags & UNIT_DIS))                      /* count enabled units */
        ucnt++;
    else if (uptr->flags & UNIT_DISABLE)
        udbl++;                                         /* count user-disabled */
    }
show_all_mods (st, dptr, dptr->units, MTAB_VDV, &toks); /* show dev mods */
if (dptr->numunits == 0) {
    if (toks)
        fprintf (st, "\n");
    }
else {
    if (ucnt == 0) {
        fprint_sep (st, &toks);
        fprintf (st, "all units disabled\n");
        }
    else if ((ucnt > 1) || (udbl > 0)) {
        fprint_sep (st, &toks);
        fprintf (st, "%d units\n", ucnt + udbl);
        }
    else
        if ((flag != 2) || !dptr->description || toks) 
            fprintf (st, "\n");
    toks = 0;
    }
if (flag)                                               /* dev only? */
    return SCPE_OK;
for (j = 0; j < dptr->numunits; j++) {                  /* loop thru units */
    uptr = dptr->units + j;
    if ((uptr->flags & UNIT_DIS) == 0)
        show_unit (st, dptr, uptr, ucnt + udbl);
    }
return SCPE_OK;
}

void fprint_sep (FILE *st, int32 *tokens)
{
fprintf (st, (*tokens > 0) ? ", " : "\t");
*tokens += 1;
}

t_stat show_unit (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag)
{
int32 u = (int32)(uptr - dptr->units);
int32 toks = 0;

if (flag > 1)
    fprintf (st, "  %s%d", sim_dname (dptr), u);
else if (flag < 0)
    fprintf (st, "%s%d", sim_dname (dptr), u);
if (uptr->flags & UNIT_FIX) {
    fprint_sep (st, &toks);
    fprint_capac (st, dptr, uptr);
    }
if (uptr->flags & UNIT_ATT) {
    fprint_sep (st, &toks);
    fprintf (st, "attached to %s", uptr->filename);
    if (uptr->flags & UNIT_RO)
        fprintf (st, ", read only");
    }
else {
    if (uptr->flags & UNIT_ATTABLE) {
        fprint_sep (st, &toks);
        fprintf (st, "not attached");
        }
    }
show_all_mods (st, dptr, uptr, MTAB_VUN, &toks);        /* show unit mods */ 
if (toks || (flag < 0) || (flag > 1))
    fprintf (st, "\n");
return SCPE_OK;
}

const char *sprint_capac (DEVICE *dptr, UNIT *uptr)
{
static char capac_buf[((CHAR_BIT * sizeof (t_value) * 4 + 3)/3) + 8];
t_addr kval = (uptr->flags & UNIT_BINK)? 1024: 1000;
t_addr mval;
t_addr psize = uptr->capac;
const char *scale, *width;

if (sim_switches & SWMASK ('B'))
    kval = 1024;
mval = kval * kval;
if (dptr->flags & DEV_SECTORS)
    psize = psize * 512;
if ((dptr->dwidth / dptr->aincr) > 8)
    width = "W";
else 
    width = "B";
if (psize < (kval * 10))
    scale = "";
else if (psize < (mval * 10)) {
    scale = "K";
    psize = psize / kval;
    }
else {
    scale = "M";
    psize = psize / mval;
    }
sprint_val (capac_buf, (t_value) psize, 10, T_ADDR_W, PV_LEFT);
sprintf (&capac_buf[strlen (capac_buf)], "%s%s", scale, width);
return capac_buf;
}

void fprint_capac (FILE *st, DEVICE *dptr, UNIT *uptr)
{
fprintf (st, "%s", sprint_capac (dptr, uptr));
}

/* Show <global name> processors  */

t_stat show_version (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
int32 vmaj = SIM_MAJOR, vmin = SIM_MINOR, vpat = SIM_PATCH, vdelt = SIM_DELTA;
const char *cpp = "";
const char *build = "";
const char *arch = "";

if (cptr && (*cptr != 0))
    return SCPE_2MARG;
fprintf (st, "%s simulator V%d.%d-%d", sim_name, vmaj, vmin, vpat);
if (vdelt)
    fprintf (st, " delta %d", vdelt);
#if defined (SIM_VERSION_MODE)
fprintf (st, " %s", SIM_VERSION_MODE);
#endif
if (flag) {
    t_bool idle_capable;
    uint32 os_ms_sleep_1, os_tick_size;
    char os_type[128] = "Unknown";

    fprintf (st, "\n    Simulator Framework Capabilities:");
    fprintf (st, "\n        %s", sim_si64);
    fprintf (st, "\n        %s", sim_sa64);
    fprintf (st, "\n        %s", eth_capabilities());
    idle_capable = sim_timer_idle_capable (&os_ms_sleep_1, &os_tick_size);
    fprintf (st, "\n        Idle/Throttling support is %savailable", idle_capable ? "" : "NOT ");
    if (sim_disk_vhd_support())
        fprintf (st, "\n        Virtual Hard Disk (VHD) support");
    if (sim_disk_raw_support())
        fprintf (st, "\n        RAW disk and CD/DVD ROM support");
#if defined (SIM_ASYNCH_IO)
    fprintf (st, "\n        Asynchronous I/O support (%s)", AIO_QUEUE_MODE);
#endif
#if defined (SIM_ASYNCH_MUX)
    fprintf (st, "\n        Asynchronous Multiplexer support");
#endif
#if defined (SIM_ASYNCH_CLOCKS)
    fprintf (st, "\n        Asynchronous Clock support");
#endif
#if defined (SIM_FRONTPANEL_VERSION)
    fprintf (st, "\n        FrontPanel API Version %d", SIM_FRONTPANEL_VERSION);
#endif
    fprintf (st, "\n    Host Platform:");
#if defined (__GNUC__) && defined (__VERSION__)
    fprintf (st, "\n        Compiler: GCC %s", __VERSION__);
#elif defined (__clang_version__)
    fprintf (st, "\n        Compiler: clang %s", __clang_version__);
#elif defined (_MSC_FULL_VER) && defined (_MSC_BUILD)
    fprintf (st, "\n        Compiler: Microsoft Visual C++ %d.%02d.%05d.%02d", _MSC_FULL_VER/10000000, (_MSC_FULL_VER/100000)%100, _MSC_FULL_VER%100000, _MSC_BUILD);
#if defined(_DEBUG)
    build = " (Debug Build)";
#else
    build = " (Release Build)";
#endif
#elif defined (__DECC_VER)
    fprintf (st, "\n        Compiler: DEC C %c%d.%d-%03d", ("T SV")[((__DECC_VER/10000)%10)-6], __DECC_VER/10000000, (__DECC_VER/100000)%100, __DECC_VER%10000);
#elif defined (SIM_COMPILER)
#define S_xstr(a) S_str(a)
#define S_str(a) #a
    fprintf (st, "\n        Compiler: %s", S_xstr(SIM_COMPILER));
#undef S_str
#undef S_xstr
#endif
#if defined(__GNUC__)
#if defined(__OPTIMIZE__)
    build = " (Release Build)";
#else
    build = " (Debug Build)";
#endif
#endif
#if defined(_M_X64) || defined(_M_AMD64) || defined(__amd64__) || defined(__x86_64__)
    arch = " arch: x64";
#elif defined(_M_IX86) || defined(__i386)
    arch = " arch: x86";
#elif defined(_M_ARM64) || defined(__aarch64_)
    arch = " arch: ARM64";
#elif defined(_M_ARM) || defined(__arm__)
    arch = " arch: ARM";
#elif defined(__ia64__) || defined(_M_IA64) || defined(__itanium__)
    arch = " arch: IA-64";
#endif
#if defined (__DATE__) && defined (__TIME__)
#ifdef  __cplusplus
    cpp = "C++";
#else
    cpp = "C";
#endif
#if !defined (SIM_BUILD_OS)
    fprintf (st, "\n        Simulator Compiled as %s%s%s on %s at %s", cpp, arch, build, __DATE__, __TIME__);
#else
#define S_xstr(a) S_str(a)
#define S_str(a) #a
    fprintf (st, "\n        Simulator Compiled as %s%s%s on %s at %s %s", cpp, arch, build, __DATE__, __TIME__, S_xstr(SIM_BUILD_OS));
#undef S_str
#undef S_xstr
#endif
#endif
    fprintf (st, "\n        Memory Access: %s Endian", sim_end ? "Little" : "Big");
    fprintf (st, "\n        Memory Pointer Size: %d bits", (int)sizeof(dptr)*8);
    fprintf (st, "\n        %s", sim_toffset_64 ? "Large File (>2GB) support" : "No Large File support");
    fprintf (st, "\n        SDL Video support: %s", vid_version());
#if defined (HAVE_PCREPOSIX_H)
    fprintf (st, "\n        PCRE RegEx (Version %s) support for EXPECT commands", pcre_version());
#elif defined (HAVE_REGEX_H)
    fprintf (st, "\n        RegEx support for EXPECT commands");
#else
    fprintf (st, "\n        No RegEx support for EXPECT commands");
#endif
    fprintf (st, "\n        OS clock resolution: %dms", os_tick_size);
    fprintf (st, "\n        Time taken by msleep(1): %dms", os_ms_sleep_1);
#if defined(__VMS)
    if (1) {
        char *arch = 
#if defined(__ia64)
            "I64";
#elif defined(__ALPHA)
            "Alpha";
#else
            "VAX";
#endif
        strlcpy (os_type, "OpenVMS ", sizeof (os_type));
        strlcat (os_type, arch, sizeof (os_type));
        fprintf (st, "\n        OS: OpenVMS %s %s", arch, __VMS_VERSION);
        }
#elif defined(_WIN32)
    if (1) {
        char *proc_id = getenv ("PROCESSOR_IDENTIFIER");
        char *arch = getenv ("PROCESSOR_ARCHITECTURE");
        char *procs = getenv ("NUMBER_OF_PROCESSORS");
        char *proc_level = getenv ("PROCESSOR_LEVEL");
        char *proc_rev = getenv ("PROCESSOR_REVISION");
        char *proc_arch3264 = getenv ("PROCESSOR_ARCHITEW6432");
        char osversion[PATH_MAX+1] = "";
        FILE *f;

        if ((f = _popen ("ver", "r"))) {
            memset (osversion, 0, sizeof(osversion));
            do {
                if (NULL == fgets (osversion, sizeof(osversion)-1, f))
                    break;
                sim_trim_endspc (osversion);
                } while (osversion[0] == '\0');
            _pclose (f);
            }
        fprintf (st, "\n        OS: %s", osversion);
        fprintf (st, "\n        Architecture: %s%s%s, Processors: %s", arch, proc_arch3264 ? " on " : "", proc_arch3264 ? proc_arch3264  : "", procs);
        fprintf (st, "\n        Processor Id: %s, Level: %s, Revision: %s", proc_id ? proc_id : "", proc_level ? proc_level : "", proc_rev ? proc_rev : "");
        strlcpy (os_type, "Windows", sizeof (os_type));
        }
#else
    if (1) {
        char osversion[2*PATH_MAX+1] = "";
        FILE *f;
        
        if ((f = popen ("uname -a", "r"))) {
            memset (osversion, 0, sizeof (osversion));
            do {
                if (NULL == fgets (osversion, sizeof (osversion)-1, f))
                    break;
                sim_trim_endspc (osversion);
                } while (osversion[0] == '\0');
            pclose (f);
            }
        fprintf (st, "\n        OS: %s", osversion);
        if ((f = popen ("uname", "r"))) {
            memset (os_type, 0, sizeof (os_type));
            do {
                if (NULL == fgets (os_type, sizeof (os_type)-1, f))
                    break;
                sim_trim_endspc (os_type);
                } while (os_type[0] == '\0');
            pclose (f);
            }
        }
#endif
    if ((!strcmp (os_type, "Unknown")) && (getenv ("OSTYPE")))
        strlcpy (os_type, getenv ("OSTYPE"), sizeof (os_type));
    setenv ("SIM_OSTYPE", os_type, 1);
    }
#if defined(SIM_GIT_COMMIT_ID)
#define S_xstr(a) S_str(a)
#define S_str(a) #a
fprintf (st, "%sgit commit id: %8.8s", flag ? "\n        " : "        ", S_xstr(SIM_GIT_COMMIT_ID));
#if defined(SIM_GIT_COMMIT_TIME)
if (flag)
    fprintf (st, "%sgit commit time: %s", "\n        ", S_xstr(SIM_GIT_COMMIT_TIME));
#endif
#undef S_str
#undef S_xstr
#endif
#if defined(SIM_BUILD)
#define S_xstr(a) S_str(a)
#define S_str(a) #a
fprintf (st, "%sBuild: %s", flag ? "\n        " : "        ", S_xstr(SIM_BUILD));
#undef S_str
#undef S_xstr
#endif
fprintf (st, "\n");
return SCPE_OK;
}

t_stat show_config (FILE *st, DEVICE *dnotused, UNIT *unotused, int32 flag, CONST char *cptr)
{
int32 i;
DEVICE *dptr;
t_bool only_enabled = (sim_switches & SWMASK ('E'));

if (cptr && (*cptr != 0))
    return SCPE_2MARG;
fprintf (st, "%s simulator configuration%s\n\n", sim_name, only_enabled ? " (enabled devices)" : "");
for (i = 0; (dptr = sim_devices[i]) != NULL; i++)
    if (!only_enabled || !qdisable (dptr))
        show_device (st, dptr, flag);
if (sim_switches & SWMASK ('I')) {
    fprintf (st, "\nInternal Devices%s\n\n", only_enabled ? " (enabled devices)" : "");
    for (i = 0; sim_internal_device_count && (dptr = sim_internal_devices[i]); ++i)
        if (!only_enabled || !qdisable (dptr))
            show_device (st, dptr, flag);
    }
return SCPE_OK;
}

t_stat show_log_names (FILE *st, DEVICE *dnotused, UNIT *unotused, int32 flag, CONST char *cptr)
{
int32 i;
DEVICE *dptr;

if (cptr && (*cptr != 0))
    return SCPE_2MARG;
for (i = 0; (dptr = sim_devices[i]) != NULL; i++)
    show_dev_logicals (st, dptr, NULL, 1, cptr);
return SCPE_OK;
}

t_stat show_dev_logicals (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
if (dptr->lname)
    fprintf (st, "%s -> %s\n", dptr->lname, dptr->name);
else if (!flag)
    fputs ("no logical name assigned\n", st);
return SCPE_OK;
}

t_stat show_queue (FILE *st, DEVICE *dnotused, UNIT *unotused, int32 flag, CONST char *cptr)
{
DEVICE *dptr;
UNIT *uptr;
int32 accum;
MEMFILE buf;

memset (&buf, 0, sizeof (buf));
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
if (sim_clock_queue == QUEUE_LIST_END)
    fprintf (st, "%s event queue empty, time = %.0f, executing %s instructios/sec\n",
             sim_name, sim_time, sim_fmt_numeric (sim_timer_inst_per_sec ()));
else {
    const char *tim;

    fprintf (st, "%s event queue status, time = %.0f, executing %s instructions/sec\n",
             sim_name, sim_time, sim_fmt_numeric (sim_timer_inst_per_sec ()));
    accum = 0;
    for (uptr = sim_clock_queue; uptr != QUEUE_LIST_END; uptr = uptr->next) {
        if (uptr == &sim_step_unit)
            fprintf (st, "  Step timer");
        else
            if (uptr == &sim_expect_unit)
                fprintf (st, "  Expect fired");
            else
                if ((dptr = find_dev_from_unit (uptr)) != NULL) {
                    fprintf (st, "  %s", sim_dname (dptr));
                    if (dptr->numunits > 1)
                        fprintf (st, " unit %d", (int32) (uptr - dptr->units));
                    }
                else
                    fprintf (st, "  Unknown");
        tim = sim_fmt_secs(((accum + uptr->time) / sim_timer_inst_per_sec ()) + (uptr->usecs_remaining / 1000000.0));
        if (uptr->usecs_remaining)
            fprintf (st, " at %d plus %.0f usecs%s%s%s%s\n", accum + uptr->time, uptr->usecs_remaining,
                                            (*tim) ? " (" : "", tim, (*tim) ? " total)" : "",
                                            (uptr->flags & UNIT_IDLE) ? " (Idle capable)" : "");
        else
            fprintf (st, " at %d%s%s%s%s\n", accum + uptr->time, 
                                            (*tim) ? " (" : "", tim, (*tim) ? ")" : "",
                                            (uptr->flags & UNIT_IDLE) ? " (Idle capable)" : "");
        accum = accum + uptr->time;
        }
    }
sim_show_clock_queues (st, dnotused, unotused, flag, cptr);
#if defined (SIM_ASYNCH_IO)
pthread_mutex_lock (&sim_asynch_lock);
sim_mfile = &buf;
fprintf (st, "asynchronous pending event queue\n");
if (sim_asynch_queue == QUEUE_LIST_END)
    fprintf (st, "  Empty\n");
else {
    for (uptr = sim_asynch_queue; uptr != QUEUE_LIST_END; uptr = uptr->a_next) {
        if ((dptr = find_dev_from_unit (uptr)) != NULL) {
            fprintf (st, "  %s", sim_dname (dptr));
            if (dptr->numunits > 1) fprintf (st, " unit %d",
                (int32) (uptr - dptr->units));
            }
        else fprintf (st, "  Unknown");
        fprintf (st, " event delay %d\n", uptr->a_event_time);
        }
    }
fprintf (st, "asynch latency: %d nanoseconds\n", sim_asynch_latency);
fprintf (st, "asynch instruction latency: %d instructions\n", sim_asynch_inst_latency);
pthread_mutex_unlock (&sim_asynch_lock);
sim_mfile = NULL;
fprintf (st, "%*.*s", (int)buf.pos, (int)buf.pos, buf.buf);
free (buf.buf);
#endif /* SIM_ASYNCH_IO */
return SCPE_OK;
}

t_stat show_time (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
fprintf (st, "Time:\t%.0f\n", sim_gtime());
return SCPE_OK;
}

t_stat show_break (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
t_stat r;

if (cptr && (*cptr != 0))
    r = ssh_break (st, cptr, 1);  /* more? */
else 
    r = sim_brk_showall (st, sim_switches);
return r;
}

t_stat show_dev_radix (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
fprintf (st, "Radix=%d\n", dptr->dradix);
return SCPE_OK;
}

t_stat show_dev_debug (FILE *st, DEVICE *dptr, UNIT *uptr, int32 uflag, CONST char *cptr)
{
DEBTAB *dep;
uint32 unit;
int32 any = 0;

if (uflag) {
    if ((dptr->flags & DEV_DEBUG) || (dptr->debflags)) {
        if (!uptr->dctrl)
            return SCPE_OK;
        if (dptr->debflags == NULL)
            fprintf (st, "%s: Debugging enabled\n", sim_uname (uptr));
        else {
            uint32 dctrl = uptr->dctrl;

            for (dep = dptr->debflags; (dctrl != 0) && (dep->name != NULL); dep++) {
                if ((dctrl & dep->mask) == dep->mask) {
                    dctrl &= ~dep->mask;
                    if (any)
                        fputc (';', st);
                    else
                        fprintf (st, "%s: Debug=", sim_uname (uptr));
                    fputs (dep->name, st);
                    any = 1;
                    }
                }
            if (any)
                fputc ('\n', st);
            }
        }
    return SCPE_OK;
    }
if ((dptr->flags & DEV_DEBUG) || (dptr->debflags)) {
    if (dptr->dctrl == 0)
        fputs ("Debugging disabled", st);
    else if (dptr->debflags == NULL)
        fputs ("Debugging enabled", st);
    else {
        uint32 dctrl = dptr->dctrl;

        fputs ("Debug=", st);
        for (dep = dptr->debflags; (dctrl != 0) && (dep->name != NULL); dep++) {
            if ((dctrl & dep->mask) == dep->mask) {
                dctrl &= ~dep->mask;
                if (any)
                    fputc (';', st);
                fputs (dep->name, st);
                any = 1;
                }
            }
        }
    fputc ('\n', st);
    for (unit = 0; unit < dptr->numunits; unit++)
        show_dev_debug (st, dptr, &dptr->units[unit], 1, NULL);
    return SCPE_OK;
    }
else return SCPE_NOFNC;
}

/* Show On actions */

t_stat show_on (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
int32 lvl, i;

if (cptr && (*cptr != 0)) return SCPE_2MARG;            /* now eol? */
for (lvl=sim_do_depth; lvl >= 0; --lvl) {
    if (lvl > 0)
        fprintf(st, "On Processing at Do Nest Level: %d", lvl);
    else
        fprintf(st, "On Processing for input commands");
    fprintf(st, " is %s\n", (sim_on_check[lvl]) ? "enabled" : "disabled");
    for (i=1; i<SCPE_BASE; ++i) {
        if (sim_on_actions[lvl][i])
            fprintf(st, "    on %6d    %s\n", i, sim_on_actions[lvl][i]); }
    for (i=SCPE_BASE; i<=SCPE_MAX_ERR; ++i) {
        if (sim_on_actions[lvl][i])
            fprintf(st, "    on %-6s    %s\n", scp_errors[i-SCPE_BASE].code, sim_on_actions[lvl][i]); }
    if (sim_on_actions[lvl][0])
        fprintf(st, "    on ERROR     %s\n", sim_on_actions[lvl][0]);
    if (sim_on_actions[lvl][ON_SIGINT_ACTION]) {
        fprintf(st, "CONTROL+C/SIGINT Handling:\n");
        fprintf(st, "    on CONTROL_C %s\n", sim_on_actions[lvl][ON_SIGINT_ACTION]);
        }
    fprintf(st, "\n");
    }
if (sim_on_inherit)
    fprintf(st, "on state and actions are inherited by nested do commands and subroutines\n");
return SCPE_OK;
}

/* Show modifiers */

t_stat show_mod_names (FILE *st, DEVICE *dnotused, UNIT *unotused, int32 flag, CONST char *cptr)
{
int32 i;
DEVICE *dptr;

if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
for (i = 0; (dptr = sim_devices[i]) != NULL; i++)
    show_dev_modifiers (st, dptr, NULL, flag, cptr);
for (i = 0; sim_internal_device_count && (dptr = sim_internal_devices[i]); ++i)
    show_dev_modifiers (st, dptr, NULL, flag, cptr);
return SCPE_OK;
}

t_stat show_dev_modifiers (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
fprint_set_help (st, dptr);
return SCPE_OK;
}

t_stat show_all_mods (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, int32 *toks)
{
MTAB *mptr;
t_stat r = SCPE_OK;

if (dptr->modifiers == NULL)
    return SCPE_OK;
for (mptr = dptr->modifiers; mptr->mask != 0; mptr++) {
    if (mptr->pstring && 
        ((mptr->mask & MTAB_XTD)?
            (MODMASK(mptr,flag) && !MODMASK(mptr,MTAB_NMO)): 
            ((MTAB_VUN == (uint32)flag) && ((uptr->flags & mptr->mask) == mptr->match)))) {
        if (*toks > 2) {
            fprintf (st, "\n");
            *toks = 0;
            }
        if (r == SCPE_OK)
            fprint_sep (st, toks);
        r = show_one_mod (st, dptr, uptr, mptr, NULL, 0);
        }
    }
return SCPE_OK;
}

t_stat show_one_mod (FILE *st, DEVICE *dptr, UNIT *uptr, MTAB *mptr,
    CONST char *cptr, int32 flag)
{
t_stat r = SCPE_OK;
//t_value val;

if (mptr->disp)
    r = mptr->disp (st, uptr, mptr->match, (CONST void *)(cptr? cptr: mptr->desc));
//else if ((mptr->mask & MTAB_XTD) && (mptr->mask & MTAB_VAL)) {
//    REG *rptr = (REG *) mptr->desc;
//    fprintf (st, "%s=", mptr->pstring);
//    val = get_rval (rptr, 0);
//    fprint_val (st, val, rptr->radix, rptr->width,
//        rptr->flags & REG_FMT);
//    }
else fputs (mptr->pstring, st);
if ((r == SCPE_OK) && (flag && !((mptr->mask & MTAB_XTD) && MODMASK(mptr,MTAB_NMO))))
    fputc ('\n', st);
return r;
}

/* Show show commands */

t_stat show_show_commands (FILE *st, DEVICE *dnotused, UNIT *unotused, int32 flag, CONST char *cptr)
{
int32 i;
DEVICE *dptr;

if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) 
    show_dev_show_commands (st, dptr, NULL, flag, cptr);
for (i = 0; sim_internal_device_count && (dptr = sim_internal_devices[i]); ++i)
    show_dev_show_commands (st, dptr, NULL, flag, cptr);
return SCPE_OK;
}

t_stat show_dev_show_commands (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
fprint_show_help (st, dptr);
return SCPE_OK;
}

/* Show/change the current working directiory commands */

t_stat show_default (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
char buffer[PATH_MAX];
char *wd = getcwd(buffer, PATH_MAX);
fprintf (st, "%s\n", wd);
return SCPE_OK;
}

t_stat set_default_cmd (int32 flg, CONST char *cptr)
{
char gbuf[4*CBUFSIZE];

if (sim_is_running)
    return SCPE_INVREM;
if ((!cptr) || (*cptr == 0))
    return SCPE_2FARG;
gbuf[sizeof(gbuf)-1] = '\0';
strlcpy (gbuf, cptr, sizeof(gbuf));
sim_trim_endspc(gbuf);
if (chdir(gbuf) != 0)
    return sim_messagef(SCPE_IOERR, "Unable to directory change to: %s\n", gbuf);
return SCPE_OK;
}

t_stat pwd_cmd (int32 flg, CONST char *cptr)
{
return show_cmd (0, "DEFAULT");
}

#if defined (_WIN32)

t_stat sim_dir_scan (const char *cptr, DIR_ENTRY_CALLBACK entry, void *context)
{
HANDLE hFind;
WIN32_FIND_DATAA File;
struct stat filestat;
char WildName[PATH_MAX + 1];

strlcpy (WildName, cptr, sizeof(WildName));
cptr = WildName;
sim_trim_endspc (WildName);
if ((hFind =  FindFirstFileA (cptr, &File)) != INVALID_HANDLE_VALUE) {
    t_int64 FileSize;
    char DirName[PATH_MAX + 1], FileName[PATH_MAX + 1];
    char *c;
    const char *backslash = strchr (cptr, '\\');
    const char *slash = strchr (cptr, '/');
    const char *pathsep = (backslash && slash) ? MIN (backslash, slash) : (backslash ? backslash : slash);

    GetFullPathNameA(cptr, sizeof(DirName), DirName, (char **)&c);
    c = strrchr (DirName, '\\');
    *c = '\0';                                  /* Truncate to just directory path */
    if (!pathsep || (!strcmp (slash, "/*")))    /* Separator wasn't mentioned? */
        pathsep = "\\";                         /* Default to Windows backslash */
    if (*pathsep == '/') {                      /* If slash separator? */
        while ((c = strchr (DirName, '\\')))
            *c = '/';                           /* Convert backslash to slash */
        }
    sprintf (&DirName[strlen (DirName)], "%c", *pathsep);
    do {
        FileSize = (((t_int64)(File.nFileSizeHigh)) << 32) | File.nFileSizeLow;
        sprintf (FileName, "%s%s", DirName, File.cFileName);
        stat (FileName, &filestat);
        entry (DirName, File.cFileName, FileSize, &filestat, context);
        } while (FindNextFile (hFind, &File));
    FindClose (hFind);
    }
else
    return SCPE_ARG;
return SCPE_OK;
}

#else /* !defined (_WIN32) */

#if defined (HAVE_GLOB)
#include <glob.h>
#else /* !defined (HAVE_GLOB) */
#include <dirent.h>
#if defined (HAVE_FNMATCH)
#include <fnmatch.h>
#endif
#endif /* defined (HAVE_GLOB) */

t_stat sim_dir_scan (const char *cptr, DIR_ENTRY_CALLBACK entry, void *context)
{
#if defined (HAVE_GLOB)
glob_t  paths;
#else
DIR *dir;
#endif
struct stat filestat;
char *c;
char DirName[PATH_MAX + 1], WholeName[PATH_MAX + 1], WildName[PATH_MAX + 1];

memset (DirName, 0, sizeof(DirName));
memset (WholeName, 0, sizeof(WholeName));
strlcpy (WildName, cptr, sizeof(WildName));
cptr = WildName;
sim_trim_endspc (WildName);
if ((*cptr != '/') || (0 == memcmp (cptr, "./", 2)) || (0 == memcmp (cptr, "../", 3))) {
#if defined (VMS)
    getcwd (WholeName, sizeof (WholeName)-1, 0);
#else
    getcwd (WholeName, sizeof (WholeName)-1);
#endif
    strlcat (WholeName, "/", sizeof (WholeName));
    strlcat (WholeName, cptr, sizeof (WholeName));
    sim_trim_endspc (WholeName);
    }
else
    strlcpy (WholeName, cptr, sizeof (WholeName));
while ((c = strstr (WholeName, "/./")))
    memmove (c + 1, c + 3, 1 + strlen (c + 3));
while ((c = strstr (WholeName, "//")))
    memmove (c + 1, c + 2, 1 + strlen (c + 2));
while ((c = strstr (WholeName, "/../"))) {
    char *c1;
    c1 = c - 1;
    while ((c1 >= WholeName) && (*c1 != '/'))
        c1 = c1 - 1;
    memmove (c1, c + 3, 1 + strlen (c + 3));
    while (0 == memcmp (WholeName, "/../", 4))
        memmove (WholeName, WholeName+3, 1 + strlen (WholeName+3));
    }
c = strrchr (WholeName, '/');
if (c) {
    memmove (DirName, WholeName, 1+c-WholeName);
    DirName[1+c-WholeName] = '\0';
    }
else {
#if defined (VMS)
    getcwd (WholeName, sizeof (WholeName)-1, 0);
#else
    getcwd (WholeName, sizeof (WholeName)-1);
#endif
    }
cptr = WholeName;
#if defined (HAVE_GLOB)
memset (&paths, 0, sizeof (paths));
if (0 == glob (cptr, 0, NULL, &paths)) {
#else
dir = opendir(DirName[0] ? DirName : "/.");
if (dir) {
    struct dirent *ent;
#endif
    t_offset FileSize;
    char FileName[PATH_MAX + 1];
    const char *MatchName = 1 + strrchr (cptr, '/');
    char *p_name;
    struct tm *local;
#if defined (HAVE_GLOB)
    size_t i;
#endif

#if defined (HAVE_GLOB)
    for (i=0; i<paths.gl_pathc; i++) {
        sprintf (FileName, "%s", paths.gl_pathv[i]);
#else
    while ((ent = readdir (dir))) {
#if defined (HAVE_FNMATCH)
        if (fnmatch(MatchName, ent->d_name, 0))
            continue;
#else
        /* only match exact name without fnmatch support */
        if (strcmp(MatchName, ent->d_name) != 0)
            continue;
#endif
        sprintf (FileName, "%s%s", DirName, ent->d_name);
#endif
        p_name = FileName + strlen (DirName);
        memset (&filestat, 0, sizeof (filestat));
        (void)stat (FileName, &filestat);
        FileSize = (t_offset)((filestat.st_mode & S_IFDIR) ? 0 : sim_fsize_name_ex (FileName));
        entry (DirName, p_name, FileSize, &filestat, context);
        }
#if defined (HAVE_GLOB)
    globfree (&paths);
#else
    closedir (dir);
#endif
    }
else
    return SCPE_ARG;
return SCPE_OK;
}
#endif /* !defined(_WIN32) */

typedef struct {
    char LastDir[PATH_MAX + 1];
    t_offset TotalBytes;
    int TotalDirs;
    int TotalFiles;
    int DirChanges;
    int DirCount;
    int FileCount;
    t_offset ByteCount;
    } DIR_CTX;

static void sim_dir_entry (const char *directory, 
                        const char *filename,
                        t_offset FileSize,
                        const struct stat *filestat,
                        void *context)
{
DIR_CTX *ctx = (DIR_CTX *)context;
struct tm *local;

if ((directory == NULL) || (filename == NULL)) {
    if (ctx->DirChanges > 1)
        sim_printf ("     Total Files Listed:\n");
    if (ctx->DirChanges > 0) {
        sim_printf ("%16d File(s) ", ctx->TotalFiles);
        sim_print_val ((t_value) ctx->TotalBytes, 10, 17, PV_RCOMMA);
        sim_printf (" bytes\n");
        sim_printf ("%16d Dir(s)\n", ctx->TotalDirs);
        }
    return;
    }
if (strcmp (ctx->LastDir, directory)) {
    if (ctx->DirCount || ctx->FileCount) {
        sim_printf ("%16d File(s) ", ctx->FileCount);
        sim_print_val ((t_value) ctx->ByteCount, 10, 17, PV_RCOMMA);
        sim_printf (" bytes\n");
        ctx->ByteCount = ctx->DirCount = ctx->FileCount = 0;
        sim_printf ("%16d Dir(s)\n", ctx->DirCount);
        }
    ++ctx->DirChanges;
    sim_printf (" Directory of %*.*s\n\n", (int)(strlen (directory) - 1), (int)(strlen (directory) - 1), directory);
    strcpy (ctx->LastDir, directory);
    }
local = localtime (&filestat->st_mtime);
sim_printf ("%02d/%02d/%04d  %02d:%02d %s ", local->tm_mon+1, local->tm_mday, 1900+local->tm_year, local->tm_hour%12, local->tm_min, (local->tm_hour >= 12) ? "PM" : "AM");
if (filestat->st_mode & S_IFDIR) {
    ++ctx->DirCount;
    ++ctx->TotalDirs;
    sim_printf ("   <DIR>         ");
    }
else {
    if (filestat->st_mode & S_IFREG) {
        ++ctx->FileCount;
        ++ctx->TotalFiles;
        sim_print_val ((t_value) FileSize, 10, 17, PV_RCOMMA);
        ctx->ByteCount += FileSize;
        ctx->TotalBytes += FileSize;
        }
    else {
        sim_printf ("%17s", "");
        }
    }
sim_printf (" %s\n", filename);
}

t_stat dir_cmd (int32 flg, CONST char *cptr)
{
DIR_CTX dir_state;
t_stat r;
char WildName[PATH_MAX + 1];


memset (&dir_state, 0, sizeof (dir_state));
strlcpy (WildName, cptr, sizeof(WildName));
cptr = WildName;
sim_trim_endspc (WildName);
if (*cptr == '\0')
    cptr = "./*";
else {
    struct stat filestat;

    if ((!stat (WildName, &filestat)) && (filestat.st_mode & S_IFDIR))
        strlcat (WildName, "/*", sizeof (WildName));
    }
r = sim_dir_scan (cptr, sim_dir_entry, &dir_state);
sim_dir_entry (NULL, NULL, 0, NULL, &dir_state);    /* output summary */
if (r != SCPE_OK)
    return sim_messagef (SCPE_ARG, "File Not Found\n");
return r;
}


typedef struct {
    t_stat stat;
    } TYPE_CTX;

static void sim_type_entry (const char *directory, 
                            const char *filename,
                            t_offset FileSize,
                            const struct stat *filestat,
                            void *context)
{
TYPE_CTX *ctx = (TYPE_CTX *)context;
char FullPath[PATH_MAX + 1];
FILE *file;
char lbuf[4*CBUFSIZE];

sprintf (FullPath, "%s%s", directory, filename);

file = sim_fopen (FullPath, "r");
if (file == NULL)                           /* open failed? */
    return;
sim_printf ("\n%s\n\n", FullPath);
lbuf[sizeof(lbuf)-1] = '\0';
while (fgets (lbuf, sizeof(lbuf)-1, file))
    sim_printf ("%s", lbuf);
fclose (file);
}


t_stat type_cmd (int32 flg, CONST char *cptr)
{
FILE *file;
char lbuf[4*CBUFSIZE];

if ((!cptr) || (*cptr == 0))
    return SCPE_2FARG;
lbuf[sizeof(lbuf)-1] = '\0';
strlcpy (lbuf, cptr, sizeof(lbuf));
sim_trim_endspc(lbuf);
file = sim_fopen (lbuf, "r");
if (file == NULL) {                         /* open failed? */
    TYPE_CTX type_state;
    t_stat stat;

    memset (&type_state, 0, sizeof (type_state));
    stat = sim_dir_scan (cptr, sim_type_entry, &type_state);
    if (stat == SCPE_OK)
        return SCPE_OK;
    return sim_messagef (SCPE_OPENERR, "The system cannot find the file specified.\n");
    }
lbuf[sizeof(lbuf)-1] = '\0';
while (fgets (lbuf, sizeof(lbuf)-1, file))
    sim_printf ("%s", lbuf);
fclose (file);
return SCPE_OK;
}

typedef struct {
    t_stat stat;
    } DEL_CTX;

static void sim_delete_entry (const char *directory, 
                              const char *filename,
                              t_offset FileSize,
                              const struct stat *filestat,
                              void *context)
{
DEL_CTX *ctx = (DEL_CTX *)context;
char FullPath[PATH_MAX + 1];

sprintf (FullPath, "%s%s", directory, filename);

if (!unlink (FullPath))
    return;
ctx->stat = sim_messagef (SCPE_ARG, "%s\n", strerror (errno));
}

t_stat delete_cmd (int32 flg, CONST char *cptr)
{
DEL_CTX del_state;
t_stat stat;

if ((!cptr) || (*cptr == 0))
    return SCPE_2FARG;
memset (&del_state, 0, sizeof (del_state));
stat = sim_dir_scan (cptr, sim_delete_entry, &del_state);
if (stat == SCPE_OK)
    return del_state.stat;
return sim_messagef (SCPE_ARG, "No such file or directory: %s\n", cptr);
}

typedef struct {
    t_stat stat;
    int count;
    char destname[CBUFSIZE];
    } COPY_CTX;

static void sim_copy_entry (const char *directory, 
                            const char *filename,
                            t_offset FileSize,
                            const struct stat *filestat,
                            void *context)
{
COPY_CTX *ctx = (COPY_CTX *)context;
struct stat deststat;
char FullPath[PATH_MAX + 1];
char dname[CBUFSIZE];\
t_stat st;

strlcpy (dname, ctx->destname, sizeof (dname));

sprintf (FullPath, "%s%s", directory, filename);

if ((dname[strlen (dname) - 1] == '/') || (dname[strlen (dname) - 1] == '\\'))
    dname[strlen (dname) - 1] = '\0';
if ((!stat (dname, &deststat)) && (deststat.st_mode & S_IFDIR)) {
    const char *dslash = (strrchr (dname, '/') ? "/" : (strrchr (dname, '\\') ? "\\" : "/"));

    dname[sizeof (dname) - 1] = '\0';
    snprintf (&dname[strlen (dname)], sizeof (dname) - strlen (dname), "%s%s", dslash, filename);
    }
st = sim_copyfile (FullPath, dname, TRUE);
if (SCPE_OK == st)
    ++ctx->count;
else
    ctx->stat = st;
}

t_stat copy_cmd (int32 flg, CONST char *cptr)
{
char sname[CBUFSIZE];
COPY_CTX copy_state;
t_stat stat;

memset (&copy_state, 0, sizeof (copy_state));
if ((!cptr) || (*cptr == 0))
    return SCPE_2FARG;
cptr = get_glyph_quoted (cptr, sname, 0);
if ((!cptr) || (*cptr == 0))
    return SCPE_2FARG;
cptr = get_glyph_quoted (cptr, copy_state.destname, 0);
stat = sim_dir_scan (sname, sim_copy_entry, &copy_state);
if ((stat == SCPE_OK) && (copy_state.count))
    return sim_messagef (SCPE_OK, "      %3d file(s) copied\n", copy_state.count);
return copy_state.stat;
}

/* Debug command */

t_stat debug_cmd (int32 flg, CONST char *cptr)
{
char gbuf[CBUFSIZE];
CONST char *svptr;
DEVICE *dptr;

GET_SWITCHES (cptr);                                    /* get switches */
cptr = get_glyph (svptr = cptr, gbuf, 0);               /* get next glyph */
if ((dptr = find_dev (gbuf)))                           /* device match? */
return set_dev_debug (dptr, NULL, flg, *cptr ? cptr : NULL);
cptr = svptr;
if (flg)
    return sim_set_debon (0, cptr);
else
    return sim_set_deboff (0, cptr);
}

/* Breakpoint commands */

t_stat brk_cmd (int32 flg, CONST char *cptr)
{
GET_SWITCHES (cptr);                                    /* get switches */
return ssh_break (NULL, cptr, flg);                     /* call common code */
}

t_stat ssh_break (FILE *st, const char *cptr, int32 flg)
{
char gbuf[CBUFSIZE], *aptr, abuf[4*CBUFSIZE];
CONST char *tptr, *t1ptr;
DEVICE *dptr = sim_dflt_dev;
UNIT *uptr;
t_stat r;
t_addr lo, hi, max;
int32 cnt;

if (sim_brk_types == 0)
    return sim_messagef (SCPE_NOFNC, "No breakpoint support in this simulator\n");
if (dptr == NULL)
    return SCPE_IERR;
uptr = dptr->units;
if (uptr == NULL)
    return SCPE_IERR;
max = uptr->capac - 1;
abuf[sizeof(abuf)-1] = '\0';
strlcpy (abuf, cptr, sizeof(abuf));
if ((aptr = strchr (abuf, ';'))) {                      /* ;action? */
    cptr += aptr - abuf + 1;
    if (flg != SSH_ST)                                  /* only on SET */
        return sim_messagef (SCPE_ARG, "Invalid argument: %s\n", cptr);
    *aptr++ = 0;                                        /* separate strings */
    if ((cptr > sim_sub_instr_buf) && ((size_t)(cptr - sim_sub_instr_buf) < sim_sub_instr_size))
        aptr = &sim_sub_instr[sim_sub_instr_off[cptr - sim_sub_instr_buf]]; /* get un-substituted string */
    }
cptr = abuf;
if (*cptr == 0) {                                       /* no argument? */
    lo = (t_addr) get_rval (sim_PC, 0);                 /* use PC */
    return ssh_break_one (st, flg, lo, 0, aptr);
    }
while (*cptr) {
    cptr = get_glyph (cptr, gbuf, ',');
    tptr = get_range (dptr, gbuf, &lo, &hi, dptr->aradix, max, 0);
    if (tptr == NULL)
        return sim_messagef (SCPE_ARG, "Invalid address specifier: %s\n", gbuf);
    if (*tptr == '[') {
        cnt = (int32) strtotv (tptr + 1, &t1ptr, 10);
        if ((tptr == t1ptr) || (*t1ptr != ']') || (flg != SSH_ST))
            return sim_messagef (SCPE_ARG, "Invalid repeat count specifier: %s\n", tptr + 1);
        tptr = t1ptr + 1;
        }
    else cnt = 0;
    if (*tptr != 0)
        return sim_messagef (SCPE_ARG, "Unexpected argument: %s\n", tptr);
    if ((lo == 0) && (hi == max)) {
        if (flg == SSH_CL)
            sim_brk_clrall (sim_switches);
        else
            if (flg == SSH_SH)
                sim_brk_showall (st, sim_switches);
            else
                return SCPE_ARG;
        }
    else {
        for ( ; lo <= hi; lo = lo + 1) {
            r = ssh_break_one (st, flg, lo, cnt, aptr);
            if (r != SCPE_OK)
                return r;
            }
        }
    }
return SCPE_OK;
}

t_stat ssh_break_one (FILE *st, int32 flg, t_addr lo, int32 cnt, CONST char *aptr)
{
if (!sim_brk_types)
    return sim_messagef (SCPE_NOFNC, "No breakpoint support in this simulator\n");
switch (flg) {

    case SSH_ST:
        return sim_brk_set (lo, sim_switches, cnt, aptr);
        break;

    case SSH_CL:
        return sim_brk_clr (lo, sim_switches);
        break;

    case SSH_SH:
        return sim_brk_show (st, lo, sim_switches);
        break;

    default:
        return SCPE_ARG;
    }
}

/* Reset command and routines

   re[set]              reset all devices
   re[set] all          reset all devices
   re[set] device       reset specific device
*/

static t_bool run_cmd_did_reset = FALSE;

t_stat reset_cmd (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
DEVICE *dptr;

GET_SWITCHES (cptr);                                    /* get switches */
run_cmd_did_reset = FALSE;
if (*cptr == 0)                                         /* reset(cr) */
    return (reset_all (0));
cptr = get_glyph (cptr, gbuf, 0);                       /* get next glyph */
if (*cptr != 0)                                         /* now eol? */
    return SCPE_2MARG;
if (strcmp (gbuf, "ALL") == 0)
    return (reset_all (0));
dptr = find_dev (gbuf);                                 /* locate device */
if (dptr == NULL)                                       /* found it? */
    return SCPE_NXDEV;
if (dptr->reset != NULL)
    return dptr->reset (dptr);
else return SCPE_OK;
}

/* Reset devices start..end

   Inputs:
        start   =       number of starting device
   Outputs:
        status  =       error status
*/

t_stat reset_all (uint32 start)
{
DEVICE *dptr;
uint32 i;
t_stat reason;

for (i = 0; i < start; i++) {
    if (sim_devices[i] == NULL)
        return SCPE_IERR;
    }
for (i = start; (dptr = sim_devices[i]) != NULL; i++) {
    if (sim_switches & SWMASK('P'))
        tmxr_add_debug (dptr);          /* Add TMXR debug to MUX devices */
    if (dptr->reset != NULL) {
        reason = dptr->reset (dptr);
        if (reason != SCPE_OK)
            return reason;
        }
    }
for (i = 0; sim_internal_device_count && (dptr = sim_internal_devices[i]); ++i) {
    if (dptr->reset != NULL) {
        reason = dptr->reset (dptr);
        if (reason != SCPE_OK)
            return reason;
        }
    }
return SCPE_OK;
}

static DEBTAB scp_debug[] = {
  {"EVENT",     SIM_DBG_EVENT,      "event dispatch activities"},
  {"ACTIVATE",  SIM_DBG_ACTIVATE,   "queue insertion activities"},
  {"QUEUE",     SIM_DBG_AIO_QUEUE,  "asynch event queue activities"},
  {"EXPSTACK",  SIM_DBG_EXP_STACK,  "expression stack activities"},
  {"EXPEVAL",   SIM_DBG_EXP_EVAL,   "expression evaluation activities"},
  {"ACTION",    SIM_DBG_BRK_ACTION, "action activities"},
  {"DO",        SIM_DBG_DO,         "do activities"},
  {0}
};

t_stat sim_add_debug_flags (DEVICE *dptr, DEBTAB *debflags)
{
dptr->flags |= DEV_DEBUG;
if (!dptr->debflags)
    dptr->debflags = debflags;
else {
    DEBTAB *cdptr, *sdptr, *ndptr;

    for (sdptr = debflags; sdptr->name; sdptr++) {
        for (cdptr = dptr->debflags; cdptr->name; cdptr++) {
            if (sdptr->mask == cdptr->mask)
                break;
            }
        if (sdptr->mask != cdptr->mask) {
            int i, dcount = 0;

            for (cdptr = dptr->debflags; cdptr->name; cdptr++)
                dcount++;
            for (cdptr = debflags; cdptr->name; cdptr++)
                dcount++;
            ndptr = (DEBTAB *)calloc (1 + dcount, sizeof (*ndptr));
            for (dcount = 0, cdptr = dptr->debflags; cdptr->name; cdptr++)
                ndptr[dcount++] = *cdptr;
            for (cdptr = debflags; cdptr->name; cdptr++) {
                for (i = 0; i < dcount; i++) {
                    if (cdptr->mask == ndptr[i].mask)
                        break;
                    }
                if (i == dcount)
                    ndptr[dcount++] = *cdptr;
                }
            dptr->debflags = ndptr;
            break;
            }
        }
    }
return SCPE_OK;
}

/* Reset to powerup state

   Inputs:
        start   =       number of starting device
   Outputs:
        status  =       error status
*/

t_stat reset_all_p (uint32 start)
{
t_stat r;
int32 old_sw = sim_switches;

sim_switches = SWMASK ('P');
r = reset_all (start);
sim_switches = old_sw;
if (sim_dflt_dev)   /* Make sure that SCP debug options are available */
    sim_add_debug_flags (sim_dflt_dev, scp_debug);
return r;
}

/* Load and dump commands

   lo[ad] filename {arg}        load specified file
   du[mp] filename {arg}        dump to specified file
*/

/* Memory File use (for internal memory static ROM images) 

    when used to read ROM image with internally generated
    load commands, calling code setups with sim_set_memory_file() 
    sim_load uses Fgetc() instead of fgetc() or getc()
*/

static const unsigned char *mem_data = NULL;
static size_t mem_data_size = 0;

t_stat sim_set_memory_load_file (const unsigned char *data, size_t size)
{
mem_data = data;
mem_data_size = size;
return SCPE_OK;
}

int Fgetc (FILE *f)
{
if (mem_data) {
    if (mem_data_size == 0)
        return EOF;
    --mem_data_size;
    return (int)(*mem_data++);
    }
else
    return fgetc (f);
}


t_stat load_cmd (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
FILE *loadfile = NULL;
t_stat reason;

GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
cptr = get_glyph_nc (cptr, gbuf, 0);                    /* get file name */
if (!mem_data) {
    loadfile = sim_fopen (gbuf, flag? "wb": "rb");      /* open for wr/rd */
    if (loadfile == NULL)
        return SCPE_OPENERR;
    }
GET_SWITCHES (cptr);                                    /* get switches */
reason = sim_load (loadfile, (CONST char *)cptr, gbuf, flag);/* load or dump */
if (loadfile)
    fclose (loadfile);
return reason;
}

/* Attach command

   at[tach] unit file   attach specified unit to file
*/

t_stat attach_cmd (int32 flag, CONST char *cptr)
{
char gbuf[4*CBUFSIZE];
DEVICE *dptr;
UNIT *uptr;
t_stat r;

GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
cptr = get_glyph (cptr, gbuf, 0);                       /* get next glyph */
GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* now eol? */
    return SCPE_2FARG;
dptr = find_unit (gbuf, &uptr);                         /* locate unit */
if (dptr == NULL)                                       /* found dev? */
    return SCPE_NXDEV;
if (uptr == NULL)                                       /* valid unit? */
    return SCPE_NXUN;
if (uptr->flags & UNIT_ATT) {                           /* already attached? */
    if (!(uptr->dynflags & UNIT_ATTMULT) &&             /* and only single attachable */
        !(dptr->flags & DEV_DONTAUTO)) {                /* and auto detachable */
        r = scp_detach_unit (dptr, uptr);               /* detach it */
        if (r != SCPE_OK)                               /* error? */
            return r;
        }
    else {
        if (!(uptr->dynflags & UNIT_ATTMULT))
            return SCPE_ALATT;                          /* Already attached */
        }
    }
gbuf[sizeof(gbuf)-1] = '\0';
strlcpy (gbuf, cptr, sizeof(gbuf));
sim_trim_endspc (gbuf);                                 /* trim trailing spc */
return scp_attach_unit (dptr, uptr, gbuf);              /* attach */
}

/* Call device-specific or file-oriented attach unit routine */

t_stat scp_attach_unit (DEVICE *dptr, UNIT *uptr, const char *cptr)
{
if (uptr->flags & UNIT_DIS)                             /* disabled? */
    return SCPE_UDIS;
if (dptr->attach != NULL)                               /* device routine? */
    return dptr->attach (uptr, (CONST char *)cptr);     /* call it */
return attach_unit (uptr, (CONST char *)cptr);          /* no, std routine */
}

/* Attach unit to file */

t_stat attach_unit (UNIT *uptr, CONST char *cptr)
{
DEVICE *dptr;

if (!(uptr->flags & UNIT_ATTABLE))                      /* not attachable? */
    return SCPE_NOATT;
if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return SCPE_NOATT;
uptr->filename = (char *) calloc (CBUFSIZE, sizeof (char)); /* alloc name buf */
if (uptr->filename == NULL)
    return SCPE_MEM;
strlcpy (uptr->filename, cptr, CBUFSIZE);               /* save name */
if ((sim_switches & SWMASK ('R')) ||                    /* read only? */
    ((uptr->flags & UNIT_RO) != 0)) {
    if (((uptr->flags & UNIT_ROABLE) == 0) &&           /* allowed? */
        ((uptr->flags & UNIT_RO) == 0))
        return attach_err (uptr, SCPE_NORO);            /* no, error */
    uptr->fileref = sim_fopen (cptr, "rb");             /* open rd only */
    if (uptr->fileref == NULL)                          /* open fail? */
        return attach_err (uptr, SCPE_OPENERR);         /* yes, error */
    if (!(uptr->flags & UNIT_RO))
        sim_messagef (SCPE_OK, "%s: unit is read only\n", sim_dname (dptr));
    uptr->flags = uptr->flags | UNIT_RO;                /* set rd only */
    }
else {
    if (sim_switches & SWMASK ('N')) {                  /* new file only? */
        uptr->fileref = sim_fopen (cptr, "wb+");        /* open new file */
        if (uptr->fileref == NULL)                      /* open fail? */
            return attach_err (uptr, SCPE_OPENERR);     /* yes, error */
        sim_messagef (SCPE_OK, "%s: creating new file\n", sim_dname (dptr));
        }
    else {                                              /* normal */
        uptr->fileref = sim_fopen (cptr, "rb+");        /* open r/w */
        if (uptr->fileref == NULL) {                    /* open fail? */
#if defined(EPERM)
            if ((errno == EROFS) || (errno == EACCES) || (errno == EPERM)) {/* read only? */
#else
            if ((errno == EROFS) || (errno == EACCES)) {/* read only? */
#endif
                if ((uptr->flags & UNIT_ROABLE) == 0)   /* allowed? */
                    return attach_err (uptr, SCPE_NORO);/* no error */
                uptr->fileref = sim_fopen (cptr, "rb"); /* open rd only */
                if (uptr->fileref == NULL)              /* open fail? */
                    return attach_err (uptr, SCPE_OPENERR); /* yes, error */
                uptr->flags = uptr->flags | UNIT_RO;    /* set rd only */
                sim_messagef (SCPE_OK, "%s: unit is read only\n", sim_dname (dptr));
                }
            else {                                      /* doesn't exist */
                if (sim_switches & SWMASK ('E'))        /* must exist? */
                    return attach_err (uptr, SCPE_OPENERR); /* yes, error */
                uptr->fileref = sim_fopen (cptr, "wb+");/* open new file */
                if (uptr->fileref == NULL)              /* open fail? */
                    return attach_err (uptr, SCPE_OPENERR); /* yes, error */
                sim_messagef (SCPE_OK, "%s: creating new file\n", sim_dname (dptr));
                }
            }                                           /* end if null */
        }                                               /* end else */
    }
if (uptr->flags & UNIT_BUFABLE) {                       /* buffer? */
    uint32 cap = ((uint32) uptr->capac) / dptr->aincr;  /* effective size */
    if (uptr->flags & UNIT_MUSTBUF)                     /* dyn alloc? */
        uptr->filebuf = calloc (cap, SZ_D (dptr));      /* allocate */
    if (uptr->filebuf == NULL)                          /* no buffer? */
        return attach_err (uptr, SCPE_MEM);             /* error */
    sim_messagef (SCPE_OK, "%s: buffering file in memory\n", sim_dname (dptr));
    uptr->hwmark = (uint32)sim_fread (uptr->filebuf,    /* read file */
        SZ_D (dptr), cap, uptr->fileref);
    uptr->flags = uptr->flags | UNIT_BUF;               /* set buffered */
    }
uptr->flags = uptr->flags | UNIT_ATT;
uptr->pos = 0;
return SCPE_OK;
}

t_stat attach_err (UNIT *uptr, t_stat stat)
{
free (uptr->filename);
uptr->filename = NULL;
return stat;
}

/* Detach command

   det[ach] all         detach all units
   det[ach] unit        detach specified unit
*/

t_stat detach_cmd (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
DEVICE *dptr;
UNIT *uptr;

GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
cptr = get_glyph (cptr, gbuf, 0);                       /* get next glyph */
if (*cptr != 0)                                         /* now eol? */
    return SCPE_2MARG;
if (strcmp (gbuf, "ALL") == 0)
    return (detach_all (0, FALSE));
dptr = find_unit (gbuf, &uptr);                         /* locate unit */
if (dptr == NULL)                                       /* found dev? */
    return SCPE_NXDEV;
if (uptr == NULL)                                       /* valid unit? */
    return SCPE_NXUN;
return scp_detach_unit (dptr, uptr);                    /* detach */
}

/* Detach devices start..end

   Inputs:
        start   =       number of starting device
        shutdown =      TRUE if simulator shutting down
   Outputs:
        status  =       error status

   Note that during shutdown, detach routines for non-attachable devices
   will be called.  These routines can implement simulator shutdown.  Error
   returns during shutdown are ignored.
*/

t_stat detach_all (int32 start, t_bool shutdown)
{
uint32 i, j;
DEVICE *dptr;
UNIT *uptr;
t_stat r;

if ((start < 0) || (start > 1))
    return SCPE_IERR;
if (shutdown)
    sim_switches = sim_switches | SIM_SW_SHUT;          /* flag shutdown */
for (i = start; (dptr = sim_devices[i]) != NULL; i++) { /* loop thru dev */
    for (j = 0; j < dptr->numunits; j++) {              /* loop thru units */
        uptr = (dptr->units) + j;
        if ((uptr->flags & UNIT_ATT) ||                 /* attached? */
            (shutdown && dptr->detach &&                /* shutdown, spec rtn, */
            !(uptr->flags & UNIT_ATTABLE))) {           /* !attachable? */
            r = scp_detach_unit (dptr, uptr);           /* detach unit */

            if ((r != SCPE_OK) && !shutdown)            /* error and not shutting down? */
                return r;                               /* bail out now with error status */
            }
        }
    }
return SCPE_OK;
}


/* Call device-specific or file-oriented detach unit routine */

t_stat scp_detach_unit (DEVICE *dptr, UNIT *uptr)
{
if (dptr->detach != NULL)                               /* device routine? */
    return dptr->detach (uptr);
return detach_unit (uptr);                              /* no, standard */
}

/* Detach unit from file */

t_stat detach_unit (UNIT *uptr)
{
DEVICE *dptr;

if (uptr == NULL)
    return SCPE_IERR;
if (!(uptr->flags & UNIT_ATTABLE))                      /* attachable? */
    return SCPE_NOATT;
if (!(uptr->flags & UNIT_ATT)) {                        /* not attached? */
    if (sim_switches & SIM_SW_REST)                     /* restoring? */
        return SCPE_OK;                                 /* allow detach */
    else
        return SCPE_NOTATT;                             /* complain */
    }
if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return SCPE_OK;
if ((uptr->flags & UNIT_BUF) && (uptr->filebuf)) {
    uint32 cap = (uptr->hwmark + dptr->aincr - 1) / dptr->aincr;
    if (uptr->hwmark && ((uptr->flags & UNIT_RO) == 0)) {
        sim_messagef (SCPE_OK, "%s: writing buffer to file\n", sim_dname (dptr));
        rewind (uptr->fileref);
        sim_fwrite (uptr->filebuf, SZ_D (dptr), cap, uptr->fileref);
        if (ferror (uptr->fileref))
            sim_printf ("%s: I/O error - %s", sim_dname (dptr), strerror (errno));
        }
    if (uptr->flags & UNIT_MUSTBUF) {                   /* dyn alloc? */
        free (uptr->filebuf);                           /* free buf */
        uptr->filebuf = NULL;
        }
    uptr->flags = uptr->flags & ~UNIT_BUF;
    }
uptr->flags = uptr->flags & ~(UNIT_ATT | ((uptr->flags & UNIT_ROABLE) ? UNIT_RO : 0));
free (uptr->filename);
uptr->filename = NULL;
if (uptr->fileref) {                        /* Only close open file */
    if (fclose (uptr->fileref) == EOF) {
        uptr->fileref = NULL;
        return SCPE_IOERR;
        }
    uptr->fileref = NULL;
    }
return SCPE_OK;
}

/* Assign command

   as[sign] device name assign logical name to device
*/

t_stat assign_cmd (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
DEVICE *dptr;

GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
cptr = get_glyph (cptr, gbuf, 0);                       /* get next glyph */
GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* now eol? */
    return SCPE_2FARG;
dptr = find_dev (gbuf);                                 /* locate device */
if (dptr == NULL)                                       /* found dev? */
    return SCPE_NXDEV;
cptr = get_glyph (cptr, gbuf, 0);                       /* get next glyph */
if (*cptr != 0)                                         /* must be eol */
    return SCPE_2MARG;
if (find_dev (gbuf))                                    /* name in use */
    return SCPE_ARG;
deassign_device (dptr);                                 /* release current */
return assign_device (dptr, gbuf);
}

t_stat assign_device (DEVICE *dptr, const char *cptr)
{
dptr->lname = (char *) calloc (1 + strlen (cptr), sizeof (char));
if (dptr->lname == NULL)
    return SCPE_MEM;
strcpy (dptr->lname, cptr);
return SCPE_OK;
}

/* Deassign command

   dea[ssign] device    deassign logical name
*/

t_stat deassign_cmd (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
DEVICE *dptr;

GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
cptr = get_glyph (cptr, gbuf, 0);                       /* get next glyph */
if (*cptr != 0)                                         /* now eol? */
    return SCPE_2MARG;
dptr = find_dev (gbuf);                                 /* locate device */
if (dptr == NULL)                                       /* found dev? */
    return SCPE_NXDEV;
return deassign_device (dptr);
}

t_stat deassign_device (DEVICE *dptr)
{
free (dptr->lname);
dptr->lname = NULL;
return SCPE_OK;
}

/* Get device display name */

const char *sim_dname (DEVICE *dptr)
{
return (dptr ? (dptr->lname? dptr->lname: dptr->name) : "");
}

/* Get unit display name */

const char *sim_uname (UNIT *uptr)
{
DEVICE *d;
char uname[CBUFSIZE];

if (!uptr)
    return "";
if (uptr->uname)
    return uptr->uname;
d = find_dev_from_unit(uptr);
if (!d)
    return "";
if (d->numunits == 1)
    sprintf (uname, "%s", sim_dname (d));
else
    sprintf (uname, "%s%d", sim_dname (d), (int)(uptr-d->units));
return sim_set_uname (uptr, uname);
}

const char *sim_set_uname (UNIT *uptr, const char *uname)
{
free (uptr->uname);
return uptr->uname = strcpy ((char *)malloc (1 + strlen (uname)), uname);
}


/* Save command

   sa[ve] filename              save state to specified file
*/

t_stat save_cmd (int32 flag, CONST char *cptr)
{
FILE *sfile;
t_stat r;
char gbuf[4*CBUFSIZE];

GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
gbuf[sizeof(gbuf)-1] = '\0';
strlcpy (gbuf, cptr, sizeof(gbuf));
sim_trim_endspc (gbuf);
if ((sfile = sim_fopen (gbuf, "wb")) == NULL)
    return SCPE_OPENERR;
r = sim_save (sfile);
fclose (sfile);
return r;
}

t_stat sim_save (FILE *sfile)
{
void *mbuf;
int32 l, t;
uint32 i, j, device_count;
t_addr k, high;
t_value val;
t_stat r;
t_bool zeroflg;
size_t sz;
DEVICE *dptr;
UNIT *uptr;
REG *rptr;

#define WRITE_I(xx) sim_fwrite (&(xx), sizeof (xx), 1, sfile)

/* Don't make changes below without also changing save_vercur above */

fprintf (sfile, "%s\n%s\n%s\n%s\n%s\n%.0f\n",
    save_vercur,                                        /* [V2.5] save format */
    sim_savename,                                       /* sim name */
    sim_si64, sim_sa64, eth_capabilities(),             /* [V3.5] options */
    sim_time);                                          /* [V3.2] sim time */
WRITE_I (sim_rtime);                                    /* [V2.6] sim rel time */
#if defined(SIM_GIT_COMMIT_ID)
#define S_xstr(a) S_str(a)
#define S_str(a) #a
fprintf (sfile, "git commit id: %8.8s\n", S_xstr(SIM_GIT_COMMIT_ID));
#undef S_str
#undef S_xstr
#else
fprintf (sfile, "git commit id: unknown\n");
#endif

for (device_count = 0; sim_devices[device_count]; device_count++);/* count devices */
for (i = 0; i < (device_count + sim_internal_device_count); i++) {/* loop thru devices */
    if (i < device_count)
        dptr = sim_devices[i];
    else
        dptr = sim_internal_devices[i - device_count];
    if (dptr->flags & DEV_NOSAVE)
        continue;
    fputs (dptr->name, sfile);                          /* device name */
    fputc ('\n', sfile);
    if (dptr->lname)                                    /* [V3.0] logical name */
        fputs (dptr->lname, sfile);
    fputc ('\n', sfile);
    WRITE_I (dptr->flags);                              /* [V2.10] flags */
    for (j = 0; j < dptr->numunits; j++) {
        uptr = dptr->units + j;
        t = sim_activate_time (uptr);
        WRITE_I (j);                                    /* unit number */
        WRITE_I (t);                                    /* activation time */
        WRITE_I (uptr->u3);                             /* unit specific */
        WRITE_I (uptr->u4);
        WRITE_I (uptr->u5);                             /* [V3.0] more unit */
        WRITE_I (uptr->u6);
        WRITE_I (uptr->flags);                          /* [V2.10] flags */
        WRITE_I (uptr->dynflags);
        WRITE_I (uptr->wait);
        WRITE_I (uptr->buf);
        WRITE_I (uptr->capac);                          /* [V3.5] capacity */
        fprintf (sfile, "%.0f\n", uptr->usecs_remaining);/* [V4.0] remaining wait */
        if (uptr->flags & UNIT_ATT) {
            fputs (uptr->filename, sfile);
            if ((uptr->flags & UNIT_BUF) &&             /* writable buffered */
                uptr->hwmark &&                         /* files need to be */
                ((uptr->flags & UNIT_RO) == 0)) {       /* written on save */
                uint32 cap = (uptr->hwmark + dptr->aincr - 1) / dptr->aincr;
                rewind (uptr->fileref);
                sim_fwrite (uptr->filebuf, SZ_D (dptr), cap, uptr->fileref);
                fclose (uptr->fileref);                 /* flush data and state */
                uptr->fileref = sim_fopen (uptr->filename, "rb+");/* reopen r/w */
                }
            }
        fputc ('\n', sfile);
        if (((uptr->flags & (UNIT_FIX + UNIT_ATTABLE)) == UNIT_FIX) &&
             (dptr->examine != NULL) &&
             ((high = uptr->capac) != 0)) {             /* memory-like unit? */
            WRITE_I (high);                             /* [V2.5] write size */
            sz = SZ_D (dptr);
            if ((mbuf = calloc (SRBSIZ, sz)) == NULL) {
                fclose (sfile);
                return SCPE_MEM;
                }
            for (k = 0; k < high; ) {                   /* loop thru mem */
                zeroflg = TRUE;
                for (l = 0; (l < SRBSIZ) && (k < high); l++,
                     k = k + (dptr->aincr)) {           /* check for 0 block */
                    r = dptr->examine (&val, k, uptr, SIM_SW_REST);
                    if (r != SCPE_OK) {
                        free (mbuf);
                        return r;
                        }
                    if (val) zeroflg = FALSE;
                    SZ_STORE (sz, val, mbuf, l);
                    }                                   /* end for l */
                if (zeroflg) {                          /* all zero's? */
                    l = -l;                             /* invert block count */
                    WRITE_I (l);                        /* write only count */
                    }
                else {
                    WRITE_I (l);                        /* block count */
                    sim_fwrite (mbuf, sz, l, sfile);
                    }
                }                                       /* end for k */
            free (mbuf);                                /* dealloc buffer */
            }                                           /* end if mem */
        else {                                          /* no memory */
            high = 0;                                   /* write 0 */
            WRITE_I (high);
            }                                           /* end else mem */
        }                                               /* end unit loop */
    t = -1;                                             /* end units */
    WRITE_I (t);                                        /* write marker */
    for (rptr = dptr->registers; (rptr != NULL) &&      /* loop thru regs */
         (rptr->name != NULL); rptr++) {
        fputs (rptr->name, sfile);                      /* name */
        fputc ('\n', sfile);
        WRITE_I (rptr->depth);                          /* [V2.10] depth */
        for (j = 0; j < rptr->depth; j++) {             /* loop thru values */
            val = get_rval (rptr, j);                   /* get value */
            WRITE_I (val);                              /* store */
            }
        }
    fputc ('\n', sfile);                                /* end registers */
    }
fputc ('\n', sfile);                                    /* end devices */
return (ferror (sfile))? SCPE_IOERR: SCPE_OK;           /* error during save? */
}

/* Restore command

   re[store] filename           restore state from specified file
*/

t_stat restore_cmd (int32 flag, CONST char *cptr)
{
FILE *rfile;
t_stat r;
char gbuf[4*CBUFSIZE];

GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
gbuf[sizeof(gbuf)-1] = '\0';
strlcpy (gbuf, cptr, sizeof(gbuf));
sim_trim_endspc (gbuf);
if ((rfile = sim_fopen (gbuf, "rb")) == NULL)
    return SCPE_OPENERR;
r = sim_rest (rfile);
fclose (rfile);
return r;
}

t_stat sim_rest (FILE *rfile)
{
char buf[CBUFSIZE];
char **attnames = NULL;
UNIT **attunits = NULL;
int32 *attswitches = NULL;
int32 attcnt = 0;
void *mbuf;
int32 j, blkcnt, limit, unitno, time, flg;
uint32 us, depth;
t_addr k, high, old_capac;
t_value val, mask;
t_stat r;
size_t sz;
t_bool v40, v35, v32;
DEVICE *dptr;
UNIT *uptr;
REG *rptr;
struct stat rstat;
t_bool force_restore = ((sim_switches & SWMASK ('F')) != 0);
t_bool dont_detach_attach = ((sim_switches & SWMASK ('D')) != 0);
t_bool suppress_warning = ((sim_switches & SWMASK ('Q')) != 0);
t_bool warned = FALSE;

sim_switches &= ~(SWMASK ('F') | SWMASK ('D') | SWMASK ('Q'));  /* remove digested switches */
#define READ_S(xx) if (read_line ((xx), sizeof(xx), rfile) == NULL) {   \
    r = SCPE_IOERR;                                                     \
    goto Cleanup_Return;                                                \
    }
#define READ_I(xx) if (sim_fread (&xx, sizeof (xx), 1, rfile) == 0) {   \
    r = SCPE_IOERR;                                                     \
    goto Cleanup_Return;                                                \
    }

if (fstat (fileno (rfile), &rstat)) {
    r = SCPE_IOERR;
    goto Cleanup_Return;
    }
READ_S (buf);                                           /* [V2.5+] read version */
v40 = v35 = v32 = FALSE;
if (strcmp (buf, save_ver40) == 0)                      /* version 4.0? */
    v40 = v35 = v32 = TRUE;
else if (strcmp (buf, save_ver35) == 0)                 /* version 3.5? */
    v35 = v32 = TRUE;
else if (strcmp (buf, save_ver32) == 0)                 /* version 3.2? */
    v32 = TRUE;
else if (strcmp (buf, save_ver30) != 0) {               /* version 3.0? */
    sim_printf ("Invalid file version: %s\n", buf);
    return SCPE_INCOMP;
    }
if ((strcmp (buf, save_ver40) != 0) && (!sim_quiet) && (!suppress_warning)) {
    sim_printf ("warning - attempting to restore a saved simulator image in %s image format.\n", buf);
    warned = TRUE;
    }
READ_S (buf);                                           /* read sim name */
if (strcmp (buf, sim_savename)) {                       /* name match? */
    sim_printf ("Wrong system type: %s\n", buf);
    return SCPE_INCOMP;
    }
if (v35) {                                              /* [V3.5+] options */
    READ_S (buf);                                       /* integer size */
    if (strcmp (buf, sim_si64) != 0) {
        sim_printf ("Incompatible integer size, save file = %s\n", buf);
        return SCPE_INCOMP;
        }
    READ_S (buf);                                       /* address size */
    if (strcmp (buf, sim_sa64) != 0) {
        sim_printf ("Incompatible address size, save file = %s\n", buf);
        return SCPE_INCOMP;
        }
    READ_S (buf);                                       /* Ethernet */
    }
if (v32) {                                              /* [V3.2+] time as string */
    READ_S (buf);
    sscanf (buf, "%lf", &sim_time);
    }
else READ_I (sim_time);                                 /* sim time */
READ_I (sim_rtime);                                     /* [V2.6+] sim rel time */
if (v40) {
    READ_S (buf);                                       /* read git commit id */
#if defined(SIM_GIT_COMMIT_ID)
#define S_xstr(a) S_str(a)
#define S_str(a) #a
    if ((memcmp (buf, "git commit id: " S_xstr(SIM_GIT_COMMIT_ID), 23)) && 
        (!sim_quiet) && (!suppress_warning)) {
        sim_printf ("warning - different simulator git versions.\nSaved commit id: %8.8s, Running commit id: %8.8s\n", buf + 15, S_xstr(SIM_GIT_COMMIT_ID));
        warned = TRUE;
        }
#undef S_str
#undef S_xstr
#endif
    }
if (!dont_detach_attach)
    detach_all (0, 0);                                  /* Detach everything to start from a consistent state */
else {
    if (!suppress_warning) {
        uint32 i, j;

        for (i = 0; (dptr = sim_devices[i]) != NULL; i++) { /* loop thru dev */
            for (j = 0; j < dptr->numunits; j++) {      /* loop thru units */
                uptr = (dptr->units) + j;
                if (uptr->flags & UNIT_ATT) {           /* attached? */
                    sim_printf ("warning - leaving %s attached to '%s'\n", sim_uname (uptr), uptr->filename);
                    warned = TRUE;
                    }
                }
            }
        }
    }
for ( ;; ) {                                            /* device loop */
    READ_S (buf);                                       /* read device name */
    if (buf[0] == 0)                                    /* last? */
        break;
    if ((dptr = find_dev (buf)) == NULL) {              /* locate device */
        sim_printf ("Invalid device name: %s\n", buf);
        r = SCPE_INCOMP;
        goto Cleanup_Return;
        }
    READ_S (buf);                                       /* [V3.0+] logical name */
    deassign_device (dptr);                             /* delete old name */
    if ((buf[0] != 0) && 
        ((r = assign_device (dptr, buf)) != SCPE_OK)) {
        r = SCPE_INCOMP;
        goto Cleanup_Return;
        }
    READ_I (flg);                                       /* [V2.10+] ctlr flags */
    if (!v32)
        flg = ((flg & DEV_UFMASK_31) << (DEV_V_UF - DEV_V_UF_31)) |
            (flg & ~DEV_UFMASK_31);                     /* [V3.2+] flags moved */
    dptr->flags = (dptr->flags & ~DEV_RFLAGS) |         /* restore ctlr flags */
         (flg & DEV_RFLAGS);
    for ( ;; ) {                                        /* unit loop */
        sim_switches = SIM_SW_REST;                     /* flag rstr, clr RO */
        READ_I (unitno);                                /* unit number */
        if (unitno < 0)                                 /* end units? */
            break;
        if ((uint32) unitno >= dptr->numunits) {        /* too big? */
            sim_printf ("Invalid unit number: %s%d\n", sim_dname (dptr), unitno);
            r = SCPE_INCOMP;
            goto Cleanup_Return;
            }
        READ_I (time);                                  /* event time */
        uptr = (dptr->units) + unitno;
        sim_cancel (uptr);
        if (time > 0)
            sim_activate (uptr, time - 1);
        READ_I (uptr->u3);                              /* device specific */
        READ_I (uptr->u4);
        READ_I (uptr->u5);                              /* [V3.0+] more dev spec */
        READ_I (uptr->u6);
        READ_I (flg);                                   /* [V2.10+] unit flags */
        if (v40) {                                      /* [V4.0+] dynflags */
            READ_I (uptr->dynflags);
            READ_I (uptr->wait);
            READ_I (uptr->buf);
            }
        old_capac = uptr->capac;                        /* save current capacity */
        if (v35) {                                      /* [V3.5+] capacity */
            READ_I (uptr->capac);
            }
        if (v40) {
            READ_S (buf);
            sscanf (buf, "%lf", &uptr->usecs_remaining);
            }
        if (!v32)
            flg = ((flg & UNIT_UFMASK_31) << (UNIT_V_UF - UNIT_V_UF_31)) |
                (flg & ~UNIT_UFMASK_31);                /* [V3.2+] flags moved */
        uptr->flags = (uptr->flags & ~UNIT_RFLAGS) |
            (flg & UNIT_RFLAGS);                        /* restore */
        READ_S (buf);                                   /* attached file */
        if ((uptr->flags & UNIT_ATT) &&                 /* unit currently attached? */
            (!dont_detach_attach)) {
            r = scp_detach_unit (dptr, uptr);           /* detach it */
            if (r != SCPE_OK) {
                sim_printf ("Error detaching %s from %s: %s\n", sim_uname (uptr), uptr->filename, sim_error_text (r));
                r = SCPE_INCOMP;
                goto Cleanup_Return;
                }
            }
        if ((buf[0] != '\0') &&                         /* unit to be reattached? */
            ((uptr->flags & UNIT_ATTABLE) ||            /*  and unit is attachable */
             (dptr->attach != NULL))) {                 /*    or VM attach routine provided? */
            uptr->flags = uptr->flags & ~UNIT_DIS;      /* ensure device is enabled */
            if (flg & UNIT_RO)                          /* [V2.10+] saved flgs & RO? */
                sim_switches |= SWMASK ('R');           /* RO attach */
            /* add unit to list of units to attach after registers are read */
            attunits = (UNIT **)realloc (attunits, sizeof (*attunits)*(attcnt+1));
            attunits[attcnt] = uptr;
            attnames = (char **)realloc (attnames, sizeof (*attnames)*(attcnt+1));
            attnames[attcnt] = (char *)malloc(1+strlen(buf));
            strcpy (attnames[attcnt], buf);
            attswitches = (int32 *)realloc (attswitches, sizeof (*attswitches)*(attcnt+1));
            attswitches[attcnt] = sim_switches;
            ++attcnt;
            }
        READ_I (high);                                  /* memory capacity */
        if (high > 0) {                                 /* [V2.5+] any memory? */
            if (((uptr->flags & (UNIT_FIX + UNIT_ATTABLE)) != UNIT_FIX) ||
                 (dptr->deposit == NULL)) {
                sim_printf ("Can't restore memory: %s%d\n", sim_dname (dptr), unitno);
                r = SCPE_INCOMP;
                goto Cleanup_Return;
                }
            if (high != old_capac) {                    /* size change? */
                uptr->capac = old_capac;                /* temp restore old */
                if ((dptr->flags & DEV_DYNM) &&
                    ((dptr->msize == NULL) ||
                     (dptr->msize (uptr, (int32) high, NULL, NULL) != SCPE_OK))) {
                    sim_printf ("Can't change memory size: %s%d\n",
                                sim_dname (dptr), unitno);
                    r = SCPE_INCOMP;
                    goto Cleanup_Return;
                    }
                uptr->capac = high;                     /* new memory size */
                sim_printf ("Memory size changed: %s%d = ", sim_dname (dptr), unitno);
                fprint_capac (stdout, dptr, uptr);
                if (sim_log)
                    fprint_capac (sim_log, dptr, uptr);
                sim_printf ("\n");
                }
            sz = SZ_D (dptr);                           /* allocate buffer */
            if ((mbuf = calloc (SRBSIZ, sz)) == NULL) {
                r = SCPE_MEM;
                goto Cleanup_Return;
                }
            for (k = 0; k < high; ) {                   /* loop thru mem */
                if (sim_fread (&blkcnt, sizeof (blkcnt), 1, rfile) == 0) {/* block count */
                    free (mbuf);
                    r = SCPE_IOERR;
                    goto Cleanup_Return;
                    }
                if (blkcnt < 0)                         /* compressed? */
                    limit = -blkcnt;
                else limit = (int32)sim_fread (mbuf, sz, blkcnt, rfile);
                if (limit <= 0) {                       /* invalid or err? */
                    free (mbuf);
                    r = SCPE_IOERR;
                    goto Cleanup_Return;
                    }
                for (j = 0; j < limit; j++, k = k + (dptr->aincr)) {
                    if (blkcnt < 0)                     /* compressed? */
                        val = 0;
                    else SZ_LOAD (sz, val, mbuf, j);    /* saved value */
                    r = dptr->deposit (val, k, uptr, SIM_SW_REST);
                    if (r != SCPE_OK) {
                        free (mbuf);
                        goto Cleanup_Return;
                        }
                    }                                   /* end for j */
                }                                       /* end for k */
            free (mbuf);                                /* dealloc buffer */
            }                                           /* end if high */
        }                                               /* end unit loop */
    for ( ;; ) {                                        /* register loop */
        READ_S (buf);                                   /* read reg name */
        if (buf[0] == 0)                                /* last? */
            break;
        READ_I (depth);                                 /* [V2.10+] depth */
        if ((rptr = find_reg (buf, NULL, dptr)) == NULL) {
            sim_printf ("Invalid register name: %s %s\n", sim_dname (dptr), buf);
            for (us = 0; us < depth; us++) {            /* skip values */
                READ_I (val);
                }
            continue;
            }
        if (depth != rptr->depth) {                      /* [V2.10+] mismatch? */
            sim_printf ("Register depth mismatch: %s %s, file = %d, sim = %d\n",
                        sim_dname (dptr), buf, depth, rptr->depth);
            if (depth > rptr->depth)
                depth = rptr->depth;
            }
        mask = width_mask[rptr->width];                 /* get mask */
        for (us = 0; us < depth; us++) {                /* loop thru values */
            READ_I (val);                               /* read value */
            if (val > mask) {                           /* value ok? */
                sim_printf ("Invalid register value: %s %s\n", sim_dname (dptr), buf);
                }
            else if (us < rptr->depth)                  /* in range? */
                put_rval (rptr, us, val);
            }
        }                                               /* end register loop */
    }                                                   /* end device loop */
/* Now that all of the register state has been imported, we can attach 
   units which were originally attached.  Some of these attach operations 
   may depend on the state of the device (in registers) to work correctly */
for (j=0, r = SCPE_OK; j<attcnt; j++) {
    if ((r == SCPE_OK) && (!dont_detach_attach)) {
        struct stat fstat;
        t_addr saved_pos;

        dptr = find_dev_from_unit (attunits[j]);
        if ((!force_restore) && 
            (!stat(attnames[j], &fstat)))
            if (fstat.st_mtime > rstat.st_mtime + 30) {
                r = SCPE_INCOMP;
                sim_printf ("Error Attaching %s to %s - the restore state is %d seconds older than the attach file\n", sim_dname (dptr), attnames[j], (int)(fstat.st_mtime - rstat.st_mtime));
                sim_printf ("restore with the -F switch to override this sanity check\n");
                continue;
                }
        saved_pos = attunits[j]->pos;
        sim_switches = attswitches[j];
        r = scp_attach_unit (dptr, attunits[j], attnames[j]);/* reattach unit */
        attunits[j]->pos = saved_pos;
        if (r != SCPE_OK)
            sim_printf ("Error Attaching %s to %s\n", sim_dname (dptr), attnames[j]);
        }
    else {
        if ((r == SCPE_OK) && (dont_detach_attach)) {
            if ((!suppress_warning) && 
                ((!attunits[j]->filename) || (strcmp (attunits[j]->filename, attnames[j]) != 0))) {
                warned = TRUE;
                sim_printf ("warning - %s was attached to '%s'", sim_uname (attunits[j]), attnames[j]);
                if (attunits[j]->filename)
                    sim_printf (", now attached to '%s'\n", attunits[j]->filename);
                else
                    sim_printf (", now unattached\n");
                }
            }
        }
    free (attnames[j]);
    attnames[j] = NULL;
    }
Cleanup_Return:
for (j=0; j < attcnt; j++)
    free (attnames[j]);
free (attnames);
free (attunits);
free (attswitches);
if (warned)
    sim_printf ("restore with the -Q switch to suppress warning messages\n");
return r;
}

/* Run, go, boot, cont, step, next commands

   ru[n] [new PC]       reset and start simulation
   go [new PC]          start simulation
   co[nt]               start simulation
   s[tep] [step limit]  start simulation for 'limit' instructions
   next                 start simulation for 1 instruction 
                        stepping over subroutine calls
   b[oot] device        bootstrap from device and start simulation

   switches:
    -Q                  quiet return status
    -T                  (only for step), causes the step limit to 
                        be a number of microseconds to run for            
*/

t_stat run_cmd (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE] = "";
CONST char *tptr;
uint32 i, j;
int32 sim_next = 0;
int32 unitno;
t_value pcv, orig_pcv;
t_stat r;
DEVICE *dptr;
UNIT *uptr;

GET_SWITCHES (cptr);                                    /* get switches */
sim_step = 0;
if ((flag == RU_RUN) || (flag == RU_GO)) {              /* run or go */
    orig_pcv = get_rval (sim_PC, 0);                    /* get current PC value */
    if (*cptr != 0) {                                   /* argument? */
        cptr = get_glyph (cptr, gbuf, 0);               /* get next glyph */
        if (MATCH_CMD (gbuf, "UNTIL") != 0) {
            if (sim_vm_parse_addr)                      /* address parser? */
                pcv = sim_vm_parse_addr (sim_dflt_dev, gbuf, &tptr);
            else pcv = strtotv (gbuf, &tptr, sim_PC->radix);/* parse PC */
            if ((tptr == gbuf) || (*tptr != 0) ||       /* error? */
                (pcv > width_mask[sim_PC->width]))
                return SCPE_ARG;
            put_rval (sim_PC, 0, pcv);                  /* Save in PC */
            }
        }
    if ((flag == RU_RUN) &&                             /* run? */
        ((r = sim_run_boot_prep (flag)) != SCPE_OK)) {  /* reset sim */
        put_rval (sim_PC, 0, orig_pcv);                 /* restore original PC */
        return r;
        }
    if ((*cptr) || (MATCH_CMD (gbuf, "UNTIL") == 0)) {  /* should be end */
        int32 saved_switches = sim_switches;

        if (MATCH_CMD (gbuf, "UNTIL") != 0)
            cptr = get_glyph (cptr, gbuf, 0);           /* get next glyph */
        if (MATCH_CMD (gbuf, "UNTIL") != 0)
            return sim_messagef (SCPE_2MARG, "Unexpected %s command argument: %s %s\n", 
                                             (flag == RU_RUN) ? "RUN" : "GO", gbuf, cptr);
        sim_switches = 0;
        GET_SWITCHES (cptr);
        if (((*cptr == '\'') || (*cptr == '"')) ||      /* Expect UNTIL condition */
            (!sim_strncasecmp(cptr, "HALTAFTER=", 10))) {
            r = expect_cmd (1, cptr);
            if (r != SCPE_OK)
                return r;
            }
        else {                                          /* BREAK UNTIL condition */
            if (sim_switches == 0)
                sim_switches = sim_brk_dflt;
            sim_switches |= BRK_TYP_TEMP;               /* make this a one-shot breakpoint */
            sim_brk_types |= BRK_TYP_TEMP;
            r = ssh_break (NULL, cptr, SSH_ST);
            if (r != SCPE_OK)
                return sim_messagef (r, "Unable to establish breakpoint at: %s\n", cptr);
            }
        sim_switches = saved_switches;
        }
    }

else if ((flag == RU_STEP) ||
         ((flag == RU_NEXT) && !sim_vm_is_subroutine_call)) { /* step */
    static t_bool not_implemented_message = FALSE;

    if ((!not_implemented_message) && (flag == RU_NEXT)) {
        sim_printf ("This simulator does not have subroutine call detection.\nPerforming a STEP instead\n");
        not_implemented_message = TRUE;
        flag = RU_STEP;
        }
    if (*cptr != 0) {                                   /* argument? */
        cptr = get_glyph (cptr, gbuf, 0);               /* get next glyph */
        if (*cptr != 0)                                 /* should be end */
            return SCPE_2MARG;
        sim_step = (int32) get_uint (gbuf, 10, INT_MAX, &r);
        if ((r != SCPE_OK) || (sim_step <= 0))          /* error? */
            return SCPE_ARG;
        }
    else sim_step = 1;
    if ((flag == RU_STEP) && (sim_switches & SWMASK ('T')))
        sim_step = (int32)((sim_timer_inst_per_sec ()*sim_step)/1000000.0);
    }
else if (flag == RU_NEXT) {                             /* next */
    t_addr *addrs;

    if (*cptr != 0) {                                   /* argument? */
        cptr = get_glyph (cptr, gbuf, 0);               /* get next glyph */
        if (*cptr != 0)                                 /* should be end */
            return SCPE_2MARG;
        sim_next = (int32) get_uint (gbuf, 10, INT_MAX, &r);
        if ((r != SCPE_OK) || (sim_next <= 0))          /* error? */
            return SCPE_ARG;
        }
    else
        sim_next = 1;
    if (sim_vm_is_subroutine_call(&addrs)) {
        sim_brk_types |= BRK_TYP_DYN_STEPOVER;
        for (i=0; addrs[i]; i++)
            sim_brk_set (addrs[i], BRK_TYP_DYN_STEPOVER, 0, NULL);
        }
    else
        sim_step = 1;
    }
else if (flag == RU_BOOT) {                             /* boot */
    if (*cptr == 0)                                     /* must be more */
        return SCPE_2FARG;
    cptr = get_glyph (cptr, gbuf, 0);                   /* get next glyph */
    if (*cptr != 0)                                     /* should be end */
        return SCPE_2MARG;
    dptr = find_unit (gbuf, &uptr);                     /* locate unit */
    if (dptr == NULL)                                   /* found dev? */
        return SCPE_NXDEV;
    if (uptr == NULL)                                   /* valid unit? */
        return SCPE_NXUN;
    if (dptr->boot == NULL)                             /* can it boot? */
        return SCPE_NOFNC;
    if (uptr->flags & UNIT_DIS)                         /* disabled? */
        return SCPE_UDIS;
    if ((uptr->flags & UNIT_ATTABLE) &&                 /* if attable, att? */
        !(uptr->flags & UNIT_ATT))
        return SCPE_UNATT;
    unitno = (int32) (uptr - dptr->units);              /* recover unit# */
    if ((r = sim_run_boot_prep (flag)) != SCPE_OK)      /* reset sim */
        return r;
    if ((r = dptr->boot (unitno, dptr)) != SCPE_OK)     /* boot device */
        return r;
    }

else 
    if (flag != RU_CONT)                                /* must be cont */
        return SCPE_IERR;
    else                                                /* CONTINUE command */
        if (*cptr != 0)                                 /* should be end (no arguments allowed) */
            return sim_messagef (SCPE_2MARG, "CONTINUE command takes no arguments\n");

if (sim_switches & SIM_SW_HIDE)                         /* Setup only for Remote Console Mode */
    return SCPE_OK;

for (i = 1; (dptr = sim_devices[i]) != NULL; i++) {     /* reposition all */
    for (j = 0; j < dptr->numunits; j++) {              /* seq devices */
        uptr = dptr->units + j;
        if ((uptr->flags & (UNIT_ATT + UNIT_SEQ)) == (UNIT_ATT + UNIT_SEQ))
            if (sim_fseek (uptr->fileref, uptr->pos, SEEK_SET))
                return sim_messagef (SCPE_IERR, "Can't seek to %u in %s for %s\n", (unsigned)uptr->pos, uptr->filename, sim_uname (uptr));
        }
    }
if ((r = sim_ttrun ()) != SCPE_OK) {                    /* set console mode */
    r = sim_messagef (SCPE_TTYERR, "sim_ttrun() returned: %s - errno: %d - %s\n", sim_error_text (r), errno, strerror (errno));
    sim_ttcmd ();
    return r;
    }
if ((r = sim_check_console (30)) != SCPE_OK) {          /* check console, error? */
    r = sim_messagef (r, "sim_check_console () returned: %s - errno: %d - %s\n", sim_error_text (r), errno, strerror (errno));
    sim_ttcmd ();
    return r;
    }
#ifdef SIGHUP
if (signal (SIGHUP, int_handler) == SIG_ERR) {          /* set WRU */
    r = sim_messagef (SCPE_SIGERR, "Can't establish SIGHUP: errno: %d - %s", errno, strerror (errno));
    sim_ttcmd ();
    return r;
    }
#endif
if (signal (SIGTERM, int_handler) == SIG_ERR) {         /* set WRU */
    r = sim_messagef (SCPE_SIGERR, "Can't establish SIGTERM: errno: %d - %s", errno, strerror (errno));
    sim_ttcmd ();
    return r;
    }
stop_cpu = FALSE;
sim_is_running = TRUE;                                  /* flag running */
if (sim_step)                                           /* set step timer */
    sim_activate (&sim_step_unit, sim_step);
fflush(stdout);                                         /* flush stdout */
if (sim_log)                                            /* flush log if enabled */
    fflush (sim_log);
sim_throt_sched ();                                     /* set throttle */
sim_rtcn_init_all ();                                   /* re-init clocks */
sim_start_timer_services ();                            /* enable wall clock timing */

do {
    t_addr *addrs;

    while (1) {
        r = sim_instr();
        if (r != SCPE_REMOTE)
            break;
        sim_remote_process_command ();                  /* Process the command and resume processing */
        }
    if ((flag != RU_NEXT) ||                            /* done if not doing NEXT */
        (--sim_next <=0))
        break;
    if (sim_step == 0) {                                /* doing a NEXT? */
        t_addr val;
        BRKTAB *bp;

        if (SCPE_BARE_STATUS(r) >= SCPE_BASE)           /* done if an error occurred */
            break;
        if (sim_vm_pc_value)                            /* done if didn't stop at a dynamic breakpoint */
            val = (t_addr)(*sim_vm_pc_value)();
        else
            val = (t_addr)get_rval (sim_PC, 0);
        if ((!(bp = sim_brk_fnd (val))) || (!(bp->typ & BRK_TYP_DYN_STEPOVER)))
            break;
        sim_brk_clrall (BRK_TYP_DYN_STEPOVER);          /* cancel any step/over subroutine breakpoints */
        }
    else {
        if (r != SCPE_STEP)                             /* done if step didn't complete with step expired */
            break;
        }
    /* setup another next/step */
    sim_step = 0;
    if (sim_vm_is_subroutine_call(&addrs)) {
        sim_brk_types |= BRK_TYP_DYN_STEPOVER;
        for (i=0; addrs[i]; i++)
            sim_brk_set (addrs[i], BRK_TYP_DYN_STEPOVER, 0, NULL);
        }
    else
        sim_step = 1;
    if (sim_step)                                       /* set step timer */
        sim_activate (&sim_step_unit, sim_step);
    } while (1);

if ((SCPE_BARE_STATUS(r) == SCPE_STOP) &&               /* WRU exit from sim_instr() */
    (sim_on_actions[sim_do_depth][SCPE_STOP] == NULL) &&/* without a handler for a STOP condition */
    (sim_on_actions[sim_do_depth][0] == NULL))
    sim_os_ms_sleep (sim_stop_sleep_ms);                /* wait a bit for SIGINT */
sim_is_running = FALSE;                                 /* flag idle */
sim_stop_timer_services ();                             /* disable wall clock timing */
sim_ttcmd ();                                           /* restore console */
sim_brk_clrall (BRK_TYP_DYN_STEPOVER);                  /* cancel any step/over subroutine breakpoints */
#ifdef SIGHUP
signal (SIGHUP, SIG_DFL);                               /* cancel WRU */
#endif
signal (SIGTERM, SIG_DFL);                              /* cancel WRU */
if (sim_log)                                            /* flush console log */
    fflush (sim_log);
if (sim_deb)                                            /* flush debug log */
    sim_debug_flush ();
for (i = 1; (dptr = sim_devices[i]) != NULL; i++) {     /* flush attached files */
    for (j = 0; j < dptr->numunits; j++) {              /* if not buffered in mem */
        uptr = dptr->units + j;
        if (uptr->flags & UNIT_ATT) {                   /* attached, */
            if (uptr->io_flush)                         /* unit specific flush routine */
                uptr->io_flush (uptr);                  /* call it */
            else {
                if (!(uptr->flags & UNIT_BUF) &&        /* not buffered, */
                    (uptr->fileref) &&                  /* real file, */
                    !(uptr->dynflags & UNIT_NO_FIO) &&  /* is FILE *, */
                    !(uptr->flags & UNIT_RO))           /* not read only? */
                    fflush (uptr->fileref);
                }
            }
        }
    }
sim_cancel (&sim_step_unit);                            /* cancel step timer */
sim_throt_cancel ();                                    /* cancel throttle */
AIO_UPDATE_QUEUE;
UPDATE_SIM_TIME;                                        /* update sim time */
return r | ((sim_switches & SWMASK ('Q')) ? SCPE_NOMESSAGE : 0);
}

/* run command message handler */

void
run_cmd_message (const char *unechoed_cmdline, t_stat r)
{
#if defined (VMS)
printf ("\n");
#endif
if (unechoed_cmdline && (r >= SCPE_BASE) && (r != SCPE_STEP) && (r != SCPE_STOP) && (r != SCPE_EXPECT))
    sim_printf("%s> %s\n", do_position(), unechoed_cmdline);
fprint_stopped (stdout, r);                         /* print msg */
if ((!sim_oline) && ((sim_log && (sim_log != stdout))))/* log if enabled */
    fprint_stopped (sim_log, r);
if (sim_deb && (sim_deb != stdout) && (sim_deb != sim_log)) {/* debug if enabled */
    TMLN *saved_oline = sim_oline;

    sim_oline = NULL;                               /* avoid potential debug to active socket */
    fprint_stopped (sim_deb, r);
    sim_oline = saved_oline;                        /* restore original socket */
    }
}

/* Common setup for RUN or BOOT */

t_stat sim_run_boot_prep (int32 flag)
{
t_stat r;

sim_interval = 0;                                       /* reset queue */
sim_time = sim_rtime = 0;
noqueue_time = 0;                                       /* reset queue */
while (sim_clock_queue != QUEUE_LIST_END)
    sim_cancel (sim_clock_queue);
noqueue_time = sim_interval = 0;
r = reset_all (0);
if ((r == SCPE_OK) && (flag == RU_RUN)) {
    if ((run_cmd_did_reset) && (0 == (sim_switches & SWMASK ('Q')))) {
        sim_printf ("Resetting all devices...  This may not have been your intention.\n");
        sim_printf ("The GO and CONTINUE commands do not reset devices.\n");
        }
    run_cmd_did_reset = TRUE;
    }
return r;
}

/* Print stopped message 
 * For VM stops, if a VM-specific "sim_vm_fprint_stopped" pointer is defined,
 * call the indicated routine to print additional information after the message
 * and before the PC value is printed.  If the routine returns FALSE, skip
 * printing the PC and its related instruction.
 */

void fprint_stopped_gen (FILE *st, t_stat v, REG *pc, DEVICE *dptr)
{
int32 i;
t_stat r = 0;
t_addr k;
t_value pcval;

fputc ('\n', st);                                       /* start on a new line */

if (v >= SCPE_BASE)                                     /* SCP error? */
    fputs (sim_error_text (v), st);                     /* print it from the SCP list */
else {                                                  /* VM error */
    if (sim_stop_messages [v])
        fputs (sim_stop_messages [v], st);              /* print the VM-specific message */
    else
        fprintf (st, "Unknown %s simulator stop code %d", sim_name, v);
    if ((sim_vm_fprint_stopped != NULL) &&              /* if a VM-specific stop handler is defined */
        (!sim_vm_fprint_stopped (st, v)))               /*   call it; if it returned FALSE, */
        return;                                         /*     we're done */
    }

fprintf (st, ", %s: ", pc->name);                       /* print the name of the PC register */

pcval = get_rval (pc, 0);
if ((pc->flags & REG_VMAD) && sim_vm_fprint_addr)       /* if reg wants VM-specific printer */
    sim_vm_fprint_addr (st, dptr, (t_addr) pcval);      /*   call it to print the PC address */
else fprint_val (st, pcval, pc->radix, pc->width,       /* otherwise, print as a numeric value */
    pc->flags & REG_FMT);                               /*   with the radix and formatting specified */
if ((dptr != NULL) && (dptr->examine != NULL)) {
    for (i = 0; i < sim_emax; i++)
        sim_eval[i] = 0;
    for (i = 0, k = (t_addr) pcval; i < sim_emax; i++, k = k + dptr->aincr) {
        if ((r = dptr->examine (&sim_eval[i], k, dptr->units, SWMASK ('V')|SIM_SW_STOP)) != SCPE_OK)
            break;
        }
    if ((r == SCPE_OK) || (i > 0)) {
        fprintf (st, " (");
        if (fprint_sym (st, (t_addr) pcval, sim_eval, NULL, SWMASK('M')|SIM_SW_STOP) > 0)
            fprint_val (st, sim_eval[0], dptr->dradix, dptr->dwidth, PV_RZRO);
        fprintf (st, ")");
        }
    }
fprintf (st, "\n");
}

void fprint_stopped (FILE *st, t_stat v)
{
fprint_stopped_gen (st, v, sim_PC, sim_dflt_dev);
return;
}

/* Unit service for step timeout, originally scheduled by STEP n command
   Return step timeout SCP code, will cause simulation to stop */

t_stat step_svc (UNIT *uptr)
{
return SCPE_STEP;
}

/* Unit service to facilitate expect matching to stop simulation.
   Return expect SCP code, will cause simulation to stop */

t_stat expect_svc (UNIT *uptr)
{
return SCPE_EXPECT | (sim_do_echo ? 0 : SCPE_NOMESSAGE);
}

/* Cancel scheduled step service */

t_stat sim_cancel_step (void)
{
return sim_cancel (&sim_step_unit);
}

/* Signal handler for ^C signal - set stop simulation flag */

void int_handler (int sig)
{
stop_cpu = TRUE;
return;
}

/* Examine/deposit commands

   ex[amine] [modifiers] list           examine
   de[posit] [modifiers] list val       deposit
   ie[xamine] [modifiers] list          interactive examine
   id[eposit] [modifiers] list          interactive deposit

   modifiers
        @filename                       output file
        -letter(s)                      switches
        devname'n                       device name and unit number
        [{&|^}value]{=|==|!|!=|>|>=|<|<=} value search specification

   list                                 list of addresses and registers
        addr[:addr|-addr]               address range
        ALL                             all addresses
        register[:register|-register]   register range
        register[index]                 register array element
        register[start:end]             register array range
        STATE                           all registers
*/

t_stat exdep_cmd (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
CONST char *gptr;
CONST char *tptr = NULL;
const char *ap;
int32 opt;
t_addr low, high;
t_stat reason;
DEVICE *tdptr;
t_stat tstat = SCPE_OK;
REG *lowr, *highr;
FILE *ofile;

opt = CMD_OPT_SW|CMD_OPT_SCH|CMD_OPT_DFT;               /* options for all */
if (flag == EX_E)                                       /* extra for EX */
    opt = opt | CMD_OPT_OF;
cptr = get_sim_opt (opt, cptr, &reason);                /* get cmd options */
if (!cptr)                                              /* error? */
    return reason;
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
if (sim_dfunit == NULL)                                 /* got a unit? */
    return SCPE_NXUN;
cptr = get_glyph (cptr, gbuf, 0);                       /* get list */
if ((flag == EX_D) && (*cptr == 0))                     /* deposit needs more */
    return SCPE_2FARG;
ofile = sim_ofile? sim_ofile: stdout;                   /* no ofile? use stdout */

for (gptr = gbuf, reason = SCPE_OK;
    (*gptr != 0) && (reason == SCPE_OK); gptr = tptr) {
    tdptr = sim_dfdev;                                  /* working dptr */
    if (strncmp (gptr, "STATE", strlen ("STATE")) == 0) {
        tptr = gptr + strlen ("STATE");
        if (*tptr && (*tptr++ != ','))
            return SCPE_ARG;
        if ((lowr = sim_dfdev->registers) == NULL)
            return SCPE_NXREG;
        for (highr = lowr; highr->name != NULL; highr++) ;
        sim_switches = sim_switches | SIM_SW_HIDE;
        reason = exdep_reg_loop (ofile, sim_schrptr, flag, cptr,
            lowr, --highr, 0, 0xFFFFFFFF);
        if ((!sim_oline) && (sim_log && (ofile == stdout)))
            exdep_reg_loop (sim_log, sim_schrptr, EX_E, cptr,
                lowr, --highr, 0, 0xFFFFFFFF);
        continue;
        }

    if ((lowr = find_reg (gptr, &tptr, tdptr)) ||       /* local reg or */
        (!(sim_opt_out & CMD_OPT_DFT) &&                /* no dflt, global? */
        (lowr = find_reg_glob_reason (gptr, &tptr, &tdptr, &tstat)))) {
        low = high = 0;
        if ((*tptr == '-') || (*tptr == ':')) {
            highr = find_reg (tptr + 1, &tptr, tdptr);
            if (highr == NULL)
                return SCPE_NXREG;
            }
        else {
            highr = lowr;
            if (*tptr == '[') {
                if (lowr->depth <= 1)
                    return SCPE_ARG;
                tptr = get_range (NULL, tptr + 1, &low, &high,
                    10, lowr->depth - 1, ']');
                if (tptr == NULL)
                    return SCPE_ARG;
                }
            }
        if (*tptr && (*tptr++ != ','))
            return SCPE_ARG;
        reason = exdep_reg_loop (ofile, sim_schrptr, flag, cptr,
            lowr, highr, (uint32) low, (uint32) high);
        if ((flag & EX_E) && (!sim_oline) && (sim_log && (ofile == stdout)))
            exdep_reg_loop (sim_log, sim_schrptr, EX_E, cptr,
                lowr, highr, (uint32) low, (uint32) high);
        continue;
        }

    if ((ap = getenv (gptr))) {
        strlcpy (gbuf, ap, sizeof (gbuf));
        gptr = gbuf;
        }
    tptr = get_range (sim_dfdev, gptr, &low, &high, sim_dfdev->aradix,
        (((sim_dfunit->capac == 0) || (flag == EX_E))? 0:
        sim_dfunit->capac - sim_dfdev->aincr), 0);
    if (tptr == NULL)
        return (tstat ? tstat : SCPE_ARG);
    if (*tptr && (*tptr++ != ','))
        return SCPE_ARG;
    reason = exdep_addr_loop (ofile, sim_schaptr, flag, cptr, low, high,
        sim_dfdev, sim_dfunit);
    if ((flag & EX_E) && (!sim_oline) && (sim_log && (ofile == stdout)))
        exdep_addr_loop (sim_log, sim_schaptr, EX_E, cptr, low, high,
            sim_dfdev, sim_dfunit);
    }                                                   /* end for */
if (sim_ofile)                                          /* close output file */
    fclose (sim_ofile);
return reason;
}

/* Loop controllers for examine/deposit

   exdep_reg_loop       examine/deposit range of registers
   exdep_addr_loop      examine/deposit range of addresses
*/

t_stat exdep_reg_loop (FILE *ofile, SCHTAB *schptr, int32 flag, CONST char *cptr,
    REG *lowr, REG *highr, uint32 lows, uint32 highs)
{
t_stat reason;
uint32 idx, val_start=lows, limits;
t_value val, last_val;
REG *rptr;
int32 saved_switches = sim_switches;

if ((lowr == NULL) || (highr == NULL))
    return SCPE_IERR;
if (lowr > highr)
    return SCPE_ARG;
for (rptr = lowr; rptr <= highr; rptr++) {
    if ((sim_switches & SIM_SW_HIDE) &&
        (rptr->flags & REG_HIDDEN))
        continue;
    val = last_val = 0;
    limits = highs;
    if (highs == 0xFFFFFFFF)
        limits = (rptr->depth > 1) ? (rptr->depth - 1) : 0;
    for (idx = lows; idx <= limits; idx++) {
        if (idx >= rptr->depth)
            return SCPE_SUB;
        sim_eval[0] = val = get_rval (rptr, idx);
        sim_switches = saved_switches;
        if (schptr && !test_search (sim_eval, schptr))
            continue;
        if (flag == EX_E) {
            if ((idx > lows) && (val == last_val))
                continue;
            if (idx > val_start+1) {
                if (idx-1 == val_start+1) {
                    reason = ex_reg (ofile, val, flag, rptr, idx-1);
                    sim_switches = saved_switches;
                    if (reason != SCPE_OK)
                        return reason;
                    }
                else {
                    if (val_start+1 != idx-1)
                        fprintf (ofile, "%s[%d]-%s[%d]: same as above\n", rptr->name, val_start+1, rptr->name, idx-1);
                    else
                        fprintf (ofile, "%s[%d]: same as above\n", rptr->name, val_start+1);
                    }
                }
            sim_last_val = last_val = val;
            val_start = idx;
            reason = ex_reg (ofile, val, flag, rptr, idx);
            sim_switches = saved_switches;
            if (reason != SCPE_OK)
                return reason;
            }
        if (flag != EX_E) {
            reason = dep_reg (flag, cptr, rptr, idx);
            sim_switches = saved_switches;
            if (reason != SCPE_OK)
                return reason;
            }
        }
    if ((flag == EX_E) && (val_start != limits)) {
        if (highs == val_start+1) {
            reason = ex_reg (ofile, val, flag, rptr, limits);
            sim_switches = saved_switches;
            if (reason != SCPE_OK)
                return reason;
            }
        else {
            if (val_start+1 != limits)
                fprintf (ofile, "%s[%d]-%s[%d]: same as above\n", rptr->name, val_start+1, rptr->name, limits);
            else
                fprintf (ofile, "%s[%d]: same as above\n", rptr->name, val_start+1);
            }
        }
    }
return SCPE_OK;
}

t_stat exdep_addr_loop (FILE *ofile, SCHTAB *schptr, int32 flag, const char *cptr,
    t_addr low, t_addr high, DEVICE *dptr, UNIT *uptr)
{
t_addr i, mask;
t_stat reason;
int32 saved_switches = sim_switches;

if (uptr->flags & UNIT_DIS)                             /* disabled? */
    return SCPE_UDIS;
mask = (t_addr) width_mask[dptr->awidth];
if ((low > mask) || (high > mask) || (low > high))
    return SCPE_ARG;
for (i = low; i <= high; ) {                            /* all paths must incr!! */
    reason = get_aval (i, dptr, uptr);                  /* get data */
    sim_switches = saved_switches;
    if (reason != SCPE_OK)                              /* return if error */
        return reason;
    if (schptr && !test_search (sim_eval, schptr))
        i = i + dptr->aincr;                            /* sch fails, incr */
    else {                                              /* no sch or success */
        if (flag != EX_D) {                             /* ex, ie, or id? */
            reason = ex_addr (ofile, flag, i, dptr, uptr);
            sim_switches = saved_switches;
            if (reason > SCPE_OK)
                return reason;
            }
        else
            reason = 1 - dptr->aincr;                   /* no, dflt incr */
        if (flag != EX_E) {                             /* ie, id, or d? */
            reason = dep_addr (flag, cptr, i, dptr, uptr, reason);
            sim_switches = saved_switches;
            if (reason > SCPE_OK)
                return reason;
            }
        i = i + (1 - reason);                           /* incr */
        }
    }
return SCPE_OK;
}

/* Examine register routine

   Inputs:
        ofile   =       output stream
        val     =       current register value
        flag    =       type of ex/mod command (ex, iex, idep)
        rptr    =       pointer to register descriptor
        idx     =       index
   Outputs:
        return  =       error status
*/

t_stat ex_reg (FILE *ofile, t_value val, int32 flag, REG *rptr, uint32 idx)
{
int32 rdx;

if (rptr == NULL)
    return SCPE_IERR;
if (rptr->depth > 1)
    fprintf (ofile, "%s[%d]:\t", rptr->name, idx);
else
    fprintf (ofile, "%s:\t", rptr->name);
if (!(flag & EX_E))
    return SCPE_OK;
sim_eval[0] = val;
GET_RADIX (rdx, rptr->radix);
if ((rptr->flags & REG_VMAD) && sim_vm_fprint_addr)
    sim_vm_fprint_addr (ofile, sim_dflt_dev, (t_addr) val);
else if (!(rptr->flags & REG_VMFLAGS) ||
    (fprint_sym (ofile, (rptr->flags & REG_UFMASK) | rdx, sim_eval,
                 NULL, sim_switches | SIM_SW_REG) > 0)) {
        fprint_val (ofile, val, rdx, rptr->width, rptr->flags & REG_FMT);
        if (rptr->fields) {
            fprintf (ofile, "\t");
            fprint_fields (ofile, val, val, rptr->fields);
            }
        }
if (flag & EX_I)
    fprintf (ofile, "\t");
else
    fprintf (ofile, "\n");
return SCPE_OK;
}

/* Get register value

   Inputs:
        rptr    =       pointer to register descriptor
        idx     =       index
   Outputs:
        return  =       register value
*/

t_value get_rval (REG *rptr, uint32 idx)
{
size_t sz;
t_value val;
uint32 *ptr;

sz = SZ_R (rptr);
if ((rptr->depth > 1) && (rptr->flags & REG_CIRC)) {
    idx = idx + rptr->qptr;
    if (idx >= rptr->depth) idx = idx - rptr->depth;
    }
if ((rptr->depth > 1) && (rptr->flags & REG_UNIT)) {
    ptr = (uint32 *)(((UNIT *) rptr->loc) + idx);
#if defined (USE_INT64)
    if (sz <= sizeof (uint32))
        val = *ptr;
    else val = *((t_uint64 *) ptr);
#else
    val = *ptr;
#endif
    }
else if ((rptr->depth > 1) && (rptr->flags & REG_STRUCT)) {
    ptr = (uint32 *)(((size_t) rptr->loc) + (idx * rptr->str_size));
#if defined (USE_INT64)
    if (sz <= sizeof (uint32))
        val = *ptr;
    else val = *((t_uint64 *) ptr);
#else
    val = *ptr;
#endif
    }
else if (((rptr->depth > 1) || (rptr->flags & REG_FIT)) &&
    (sz == sizeof (uint8)))
    val = *(((uint8 *) rptr->loc) + idx);
else if (((rptr->depth > 1) || (rptr->flags & REG_FIT)) &&
    (sz == sizeof (uint16)))
    val = *(((uint16 *) rptr->loc) + idx);
#if defined (USE_INT64)
else if (sz <= sizeof (uint32))
     val = *(((uint32 *) rptr->loc) + idx);
else val = *(((t_uint64 *) rptr->loc) + idx);
#else
else val = *(((uint32 *) rptr->loc) + idx);
#endif
val = (val >> rptr->offset) & width_mask[rptr->width];
return val;
}

/* Deposit register routine

   Inputs:
        flag    =       type of deposit (normal/interactive)
        cptr    =       pointer to input string
        rptr    =       pointer to register descriptor
        idx     =       index
   Outputs:
        return  =       error status
*/

t_stat dep_reg (int32 flag, CONST char *cptr, REG *rptr, uint32 idx)
{
t_stat r;
t_value val, mask;
int32 rdx;
CONST char *tptr;
char gbuf[CBUFSIZE];

if ((cptr == NULL) || (rptr == NULL))
    return SCPE_IERR;
if (rptr->flags & REG_RO)
    return SCPE_RO;
if (flag & EX_I) {
    cptr = read_line (gbuf, sizeof(gbuf), stdin);
    if (sim_log)
        fprintf (sim_log, "%s\n", cptr? cptr: "");
    if (cptr == NULL)                                   /* force exit */
        return 1;
    if (*cptr == 0)                                     /* success */
        return SCPE_OK;
    }
mask = width_mask[rptr->width];
GET_RADIX (rdx, rptr->radix);
if ((rptr->flags & REG_VMAD) && sim_vm_parse_addr) {    /* address form? */
    val = sim_vm_parse_addr (sim_dflt_dev, cptr, &tptr);
    if ((tptr == cptr) || (*tptr != 0) || (val > mask))
        return SCPE_ARG;
    }
else
    if (!(rptr->flags & REG_VMFLAGS) ||                 /* dont use sym? */
        (parse_sym ((CONST char *)cptr, (rptr->flags & REG_UFMASK) | rdx, NULL,
                    &val, sim_switches | SIM_SW_REG) > SCPE_OK)) {
    val = get_uint (cptr, rdx, mask, &r);
    if (r != SCPE_OK)
        return SCPE_ARG;
    }
if ((rptr->flags & REG_NZ) && (val == 0))
    return SCPE_ARG;
put_rval (rptr, idx, val);
return SCPE_OK;
}

/* Put register value

   Inputs:
        rptr    =       pointer to register descriptor
        idx     =       index
        val     =       new value
        mask    =       mask
   Outputs:
        none
*/

void put_rval (REG *rptr, uint32 idx, t_value val)
{
size_t sz;
t_value mask;
uint32 *ptr;

#define PUT_RVAL(sz,rp,id,v,m) \
    *(((sz *) rp->loc) + id) = \
            (sz)((*(((sz *) rp->loc) + id) & \
            ~((m) << (rp)->offset)) | ((v) << (rp)->offset))

if (rptr == sim_PC)
    sim_brk_npc (0);
sz = SZ_R (rptr);
mask = width_mask[rptr->width];
if ((rptr->depth > 1) && (rptr->flags & REG_CIRC)) {
    idx = idx + rptr->qptr;
    if (idx >= rptr->depth)
        idx = idx - rptr->depth;
    }
if ((rptr->depth > 1) && (rptr->flags & REG_UNIT)) {
    ptr = (uint32 *)(((UNIT *) rptr->loc) + idx);
#if defined (USE_INT64)
    if (sz <= sizeof (uint32))
        *ptr = (*ptr &
        ~(((uint32) mask) << rptr->offset)) |
        (((uint32) val) << rptr->offset);
    else *((t_uint64 *) ptr) = (*((t_uint64 *) ptr)
        & ~(mask << rptr->offset)) | (val << rptr->offset);
#else
    *ptr = (*ptr &
        ~(((uint32) mask) << rptr->offset)) |
        (((uint32) val) << rptr->offset);
#endif
    }
else if ((rptr->depth > 1) && (rptr->flags & REG_STRUCT)) {
    ptr = (uint32 *)(((size_t) rptr->loc) + (idx * rptr->str_size));
#if defined (USE_INT64)
    if (sz <= sizeof (uint32))
        *((uint32 *) ptr) = (*((uint32 *) ptr) &
        ~(((uint32) mask) << rptr->offset)) |
        (((uint32) val) << rptr->offset);
    else *((t_uint64 *) ptr) = (*((t_uint64 *) ptr)
        & ~(mask << rptr->offset)) | (val << rptr->offset);
#else
    *ptr = (*ptr &
        ~(((uint32) mask) << rptr->offset)) |
        (((uint32) val) << rptr->offset);
#endif
    }
else if (((rptr->depth > 1) || (rptr->flags & REG_FIT)) &&
    (sz == sizeof (uint8)))
    PUT_RVAL (uint8, rptr, idx, (uint32) val, (uint32) mask);
else if (((rptr->depth > 1) || (rptr->flags & REG_FIT)) &&
    (sz == sizeof (uint16)))
    PUT_RVAL (uint16, rptr, idx, (uint32) val, (uint32) mask);
#if defined (USE_INT64)
else if (sz <= sizeof (uint32))
    PUT_RVAL (uint32, rptr, idx, (int32) val, (uint32) mask);
else PUT_RVAL (t_uint64, rptr, idx, val, mask);
#else
else PUT_RVAL (uint32, rptr, idx, val, mask);
#endif
}

/* Examine address routine

   Inputs: (sim_eval is an implicit argument)
        ofile   =       output stream
        flag    =       type of ex/mod command (ex, iex, idep)
        addr    =       address to examine
        dptr    =       pointer to device
        uptr    =       pointer to unit
   Outputs:
        return  =       if > 0, error status
                        if <= 0,-number of extra addr units retired
*/

t_stat ex_addr (FILE *ofile, int32 flag, t_addr addr, DEVICE *dptr, UNIT *uptr)
{
t_stat reason;
int32 rdx;

if (sim_vm_fprint_addr)
    sim_vm_fprint_addr (ofile, dptr, addr);
else fprint_val (ofile, addr, dptr->aradix, dptr->awidth, PV_LEFT);
fprintf (ofile, ":\t");
if (!(flag & EX_E))
    return (1 - dptr->aincr);

GET_RADIX (rdx, dptr->dradix);
if ((reason = fprint_sym (ofile, addr, sim_eval, uptr, sim_switches)) > 0) {
    fprint_val (ofile, sim_eval[0], rdx, dptr->dwidth, PV_RZRO);
    reason = 1 - dptr->aincr;
    }
if (flag & EX_I)
    fprintf (ofile, "\t");
else
    fprintf (ofile, "\n");
return reason;
}

/* Get address routine

   Inputs:
        flag    =       type of ex/mod command (ex, iex, idep)
        addr    =       address to examine
        dptr    =       pointer to device
        uptr    =       pointer to unit
   Outputs: (sim_eval is an implicit output)
        return  =       error status
*/

t_stat get_aval (t_addr addr, DEVICE *dptr, UNIT *uptr)
{
int32 i;
t_value mask;
t_addr j, loc;
size_t sz;
t_stat reason = SCPE_OK;

if ((dptr == NULL) || (uptr == NULL))
    return SCPE_IERR;
mask = width_mask[dptr->dwidth];
for (i = 0; i < sim_emax; i++)
    sim_eval[i] = 0;
for (i = 0, j = addr; i < sim_emax; i++, j = j + dptr->aincr) {
    if (dptr->examine != NULL) {
        reason = dptr->examine (&sim_eval[i], j, uptr, sim_switches);
        if (reason != SCPE_OK)
            break;
        }
    else {
        if (!(uptr->flags & UNIT_ATT))
            return SCPE_UNATT;
        if ((uptr->dynflags & UNIT_NO_FIO) ||
            (uptr->fileref == NULL))
            return SCPE_NOFNC;
        if ((uptr->flags & UNIT_FIX) && (j >= uptr->capac)) {
            reason = SCPE_NXM;
            break;
            }
        sz = SZ_D (dptr);
        loc = j / dptr->aincr;
        if (uptr->flags & UNIT_BUF) {
            SZ_LOAD (sz, sim_eval[i], uptr->filebuf, loc);
            }
        else {
            if (sim_fseek (uptr->fileref, (t_addr)(sz * loc), SEEK_SET)) {
                clearerr (uptr->fileref);
                reason = SCPE_IOERR;
                break;
                }
            sim_fread (&sim_eval[i], sz, 1, uptr->fileref);
            if ((feof (uptr->fileref)) &&
               !(uptr->flags & UNIT_FIX)) {
                reason = SCPE_EOF;
                break;
                }
            else if (ferror (uptr->fileref)) {
                clearerr (uptr->fileref);
                reason = SCPE_IOERR;
                break;
                }
            }
        }
    sim_last_val = sim_eval[i] = sim_eval[i] & mask;
    }
if ((reason != SCPE_OK) && (i == 0))
    return reason;
return SCPE_OK;
}

/* Deposit address routine

   Inputs:
        flag    =       type of deposit (normal/interactive)
        cptr    =       pointer to input string
        addr    =       address to examine
        dptr    =       pointer to device
        uptr    =       pointer to unit
        dfltinc =       value to return on cr input
   Outputs:
        return  =       if > 0, error status
                        if <= 0, -number of extra address units retired
*/

t_stat dep_addr (int32 flag, const char *cptr, t_addr addr, DEVICE *dptr,
    UNIT *uptr, int32 dfltinc)
{
int32 i, count, rdx;
t_addr j, loc;
t_stat r, reason;
t_value mask;
size_t sz;
char gbuf[CBUFSIZE];

if (dptr == NULL)
    return SCPE_IERR;
if (flag & EX_I) {
    cptr = read_line (gbuf, sizeof(gbuf), stdin);
    if (sim_log)
        fprintf (sim_log, "%s\n", cptr? cptr: "");
    if (cptr == NULL)                                   /* force exit */
        return 1;
    if (*cptr == 0)                                     /* success */
        return dfltinc;
    }
if (uptr->flags & UNIT_RO)                              /* read only? */
    return SCPE_RO;
mask = width_mask[dptr->dwidth];

GET_RADIX (rdx, dptr->dradix);
if ((reason = parse_sym ((CONST char *)cptr, addr, uptr, sim_eval, sim_switches)) > 0) {
    sim_eval[0] = get_uint (cptr, rdx, mask, &reason);
    if (reason != SCPE_OK)
        return reason;
    reason = dfltinc;
    }
count = (1 - reason + (dptr->aincr - 1)) / dptr->aincr;

for (i = 0, j = addr; i < count; i++, j = j + dptr->aincr) {
    sim_eval[i] = sim_eval[i] & mask;
    if (dptr->deposit != NULL) {
        r = dptr->deposit (sim_eval[i], j, uptr, sim_switches);
        if (r != SCPE_OK)
            return r;
        }
    else {
        if (!(uptr->flags & UNIT_ATT))
            return SCPE_UNATT;
        if (uptr->dynflags & UNIT_NO_FIO)
            return SCPE_NOFNC;
        if ((uptr->flags & UNIT_FIX) && (j >= uptr->capac))
            return SCPE_NXM;
        sz = SZ_D (dptr);
        loc = j / dptr->aincr;
        if (uptr->flags & UNIT_BUF) {
            SZ_STORE (sz, sim_eval[i], uptr->filebuf, loc);
            if (loc >= uptr->hwmark)
                uptr->hwmark = (uint32) loc + 1;
            }
        else {
            if (sim_fseek (uptr->fileref, (t_addr)(sz * loc), SEEK_SET)) {
                clearerr (uptr->fileref);
                return SCPE_IOERR;
                }
            sim_fwrite (&sim_eval[i], sz, 1, uptr->fileref);
            if (ferror (uptr->fileref)) {
                clearerr (uptr->fileref);
                return SCPE_IOERR;
                }
            }
        }
    }
return reason;
}

/* Evaluate command */

t_stat eval_cmd (int32 flg, CONST char *cptr)
{
DEVICE *dptr = sim_dflt_dev;
int32 i, rdx, a, lim;
t_stat r;

GET_SWITCHES (cptr);
GET_RADIX (rdx, dptr->dradix);
for (i = 0; i < sim_emax; i++)
    sim_eval[i] = 0;
if (*cptr == 0)
    return SCPE_2FARG;
if ((r = parse_sym ((CONST char *)cptr, 0, dptr->units, sim_eval, sim_switches)) > 0) {
    sim_eval[0] = get_uint (cptr, rdx, width_mask[dptr->dwidth], &r);
    if (r != SCPE_OK)
        return sim_messagef (r, "%s\nCan't be parsed as an instruction or data\n", cptr);
    }
lim = 1 - r;
for (i = a = 0; a < lim; ) {
    sim_printf ("%d:\t", a);
    if ((r = fprint_sym (stdout, a, &sim_eval[i], dptr->units, sim_switches)) > 0)
        r = fprint_val (stdout, sim_eval[i], rdx, dptr->dwidth, PV_RZRO);
    if (sim_log) {
        if ((r = fprint_sym (sim_log, a, &sim_eval[i], dptr->units, sim_switches)) > 0)
            r = fprint_val (sim_log, sim_eval[i], rdx, dptr->dwidth, PV_RZRO);
        }
    sim_printf ("\n");
    if (r < 0)
        a = a + 1 - r;
    else a = a + dptr->aincr;
    i = a / dptr->aincr;
    }
return SCPE_OK;
}

/* String processing routines

   read_line            read line

   Inputs:
        cptr    =       pointer to buffer
        size    =       maximum size
        stream  =       pointer to input stream
   Outputs:
        optr    =       pointer to first non-blank character
                        NULL if EOF
*/

char *read_line (char *cptr, int32 size, FILE *stream)
{
return read_line_p (NULL, cptr, size, stream);
}

/* read_line_p          read line with prompt

   Inputs:
        prompt  =       pointer to prompt string
        cptr    =       pointer to buffer
        size    =       maximum size
        stream  =       pointer to input stream
   Outputs:
        optr    =       pointer to first non-blank character
                        NULL if EOF
*/

char *read_line_p (const char *prompt, char *cptr, int32 size, FILE *stream)
{
char *tptr;
#if defined(HAVE_DLOPEN)
static int initialized = 0;
typedef char *(*readline_func)(const char *);
static readline_func p_readline = NULL;
typedef void (*add_history_func)(const char *);
static add_history_func p_add_history = NULL;

if (prompt && (!initialized)) {
    initialized = 1;
    void *handle;

#define S__STR_QUOTE(tok) #tok
#define S__STR(tok) S__STR_QUOTE(tok)
    handle = dlopen("libncurses." S__STR(HAVE_DLOPEN), RTLD_NOW|RTLD_GLOBAL);
    handle = dlopen("libcurses." S__STR(HAVE_DLOPEN), RTLD_NOW|RTLD_GLOBAL);
    handle = dlopen("libreadline." S__STR(HAVE_DLOPEN), RTLD_NOW|RTLD_GLOBAL);
    if (!handle)
        handle = dlopen("libreadline." S__STR(HAVE_DLOPEN) ".7", RTLD_NOW|RTLD_GLOBAL);
    if (!handle)
        handle = dlopen("libreadline." S__STR(HAVE_DLOPEN) ".6", RTLD_NOW|RTLD_GLOBAL);
    if (!handle)
        handle = dlopen("libreadline." S__STR(HAVE_DLOPEN) ".5", RTLD_NOW|RTLD_GLOBAL);
    if (handle) {
        p_readline = (readline_func)((size_t)dlsym(handle, "readline"));
        p_add_history = (add_history_func)((size_t)dlsym(handle, "add_history"));
        }
    }
if (prompt) {                                           /* interactive? */
    if (p_readline) {
        char *tmpc = p_readline (prompt);               /* get cmd line */
        if (tmpc == NULL)                               /* bad result? */
            cptr = NULL;
        else {
            strlcpy (cptr, tmpc, size);                 /* copy result */
            free (tmpc) ;                               /* free temp */
            }
        }
    else {
        printf ("%s", prompt);                          /* display prompt */
        cptr = fgets (cptr, size, stream);              /* get cmd line */
        }
    }
else cptr = fgets (cptr, size, stream);                 /* get cmd line */
#else
if (prompt)                                             /* interactive? */
    printf ("%s", prompt);                              /* display prompt */
cptr = fgets (cptr, size, stream);                      /* get cmd line */
#endif

if (cptr == NULL) {
    clearerr (stream);                                  /* clear error */
    return NULL;                                        /* ignore EOF */
    }
for (tptr = cptr; tptr < (cptr + size); tptr++) {       /* remove cr or nl */
    if ((*tptr == '\n') || (*tptr == '\r') ||
        (tptr == (cptr + size - 1))) {                  /* str max length? */
        *tptr = 0;                                      /* terminate */
        break;
        }
    }
if (0 == memcmp (cptr, "\xEF\xBB\xBF", 3))              /* Skip/ignore UTF8_BOM */
    memmove (cptr, cptr + 3, strlen (cptr + 3));
while (sim_isspace (*cptr))                             /* trim leading spc */
    cptr++;
sim_trim_endspc (cptr);                                 /* trim trailing spc */
if ((*cptr == ';') || (*cptr == '#')) {                 /* ignore comment */
    if (sim_do_echo)                                    /* echo comments if -v */
        sim_printf("%s> %s\n", do_position(), cptr);
    *cptr = 0;
    }

#if defined (HAVE_DLOPEN)
if (prompt && p_add_history && *cptr)                   /* Save non blank lines in history */
    p_add_history (cptr);
#endif

return cptr;
}

/* get_glyph            get next glyph (force upper case)
   get_glyph_nc         get next glyph (no conversion)
   get_glyph_quoted     get next glyph (potentially enclosed in quotes, no conversion)
   get_glyph_cmd        get command glyph (force upper case, extract leading !)
   get_glyph_gen        get next glyph (general case)

   Inputs:
        iptr        =   pointer to input string
        optr        =   pointer to output string
        mchar       =   optional end of glyph character
        uc          =   TRUE for convert to upper case (_gen only)
        quote       =   TRUE to allow quote enclosing values (_gen only)
        escape_char =   optional escape character within quoted strings (_gen only)

   Outputs
        result      =   pointer to next character in input string
*/

static const char *get_glyph_gen (const char *iptr, char *optr, char mchar, t_bool uc, t_bool quote, char escape_char)
{
t_bool quoting = FALSE;
t_bool escaping = FALSE;
t_bool got_quoted = FALSE;
char quote_char = 0;

while ((*iptr != 0) && (!got_quoted) &&
       ((quote && quoting) || ((sim_isspace (*iptr) == 0) && (*iptr != mchar)))) {
    if (quote) {
        if (quoting) {
            if (!escaping) {
                if (*iptr == escape_char)
                    escaping = TRUE;
                else
                    if (*iptr == quote_char) {
                        quoting = FALSE;
                        got_quoted = TRUE;
                        }
                }
            else
                escaping = FALSE;
            }
        else {
            if ((*iptr == '"') || (*iptr == '\'')) {
                quoting = TRUE;
                quote_char = *iptr;
                }
            }
        }
    if (sim_islower (*iptr) && uc)
        *optr = (char)sim_toupper (*iptr);
    else *optr = *iptr;
    iptr++; optr++;
    }
*optr = 0;
if (mchar && (*iptr == mchar))                          /* skip terminator */
    iptr++;
while (sim_isspace (*iptr))                             /* absorb spaces */
    iptr++;
return iptr;
}

CONST char *get_glyph (const char *iptr, char *optr, char mchar)
{
return (CONST char *)get_glyph_gen (iptr, optr, mchar, TRUE, FALSE, 0);
}

CONST char *get_glyph_nc (const char *iptr, char *optr, char mchar)
{
return (CONST char *)get_glyph_gen (iptr, optr, mchar, FALSE, FALSE, 0);
}

CONST char *get_glyph_quoted (const char *iptr, char *optr, char mchar)
{
return (CONST char *)get_glyph_gen (iptr, optr, mchar, FALSE, TRUE, '\\');
}

CONST char *get_glyph_cmd (const char *iptr, char *optr)
{
/* Tolerate "!subprocess" vs. requiring "! subprocess" */
if ((iptr[0] == '!') && (!sim_isspace(iptr[1]))) {
    strcpy (optr, "!");                     /* return ! as command glyph */
    return (CONST char *)(iptr + 1);        /* and skip over the leading ! */
    }
return (CONST char *)get_glyph_gen (iptr, optr, 0, TRUE, FALSE, 0);
}

/* Trim trailing spaces from a string

    Inputs:
        cptr    =       pointer to string
    Outputs:
        cptr    =       pointer to string
*/

char *sim_trim_endspc (char *cptr)
{
char *tptr;

tptr = cptr + strlen (cptr);
while ((--tptr >= cptr) && sim_isspace (*tptr))
    *tptr = 0;
return cptr;
}

int sim_isspace (int c)
{
return ((c < 0) || (c >= 128)) ? 0 : isspace (c);
}

int sim_islower (int c)
{
return (c >= 'a') && (c <= 'z');
}

int sim_isupper (int c)
{
return (c >= 'A') && (c <= 'Z');
}

int sim_toupper (int c)
{
return ((c >= 'a') && (c <= 'z')) ? ((c - 'a') + 'A') : c;
}

int sim_tolower (int c)
{
return ((c >= 'A') && (c <= 'Z')) ? ((c - 'A') + 'a') : c;
}

int sim_isalpha (int c)
{
return ((c < 0) || (c >= 128)) ? 0 : isalpha (c);
}

int sim_isprint (int c)
{
return ((c < 0) || (c >= 128)) ? 0 : isprint (c);
}

int sim_isdigit (int c)
{
return ((c < 0) || (c >= 128)) ? 0 : isdigit (c);
}

int sim_isgraph (int c)
{
return ((c < 0) || (c >= 128)) ? 0 : isgraph (c);
}

int sim_isalnum (int c)
{
return ((c < 0) || (c >= 128)) ? 0 : isalnum (c);
}

/* strncasecmp() is not available on all platforms */
int sim_strncasecmp (const char* string1, const char* string2, size_t len)
{
size_t i;
unsigned char s1, s2;

for (i=0; i<len; i++) {
    s1 = (unsigned char)string1[i];
    s2 = (unsigned char)string2[i];
    s1 = (unsigned char)sim_toupper (s1);
    s2 = (unsigned char)sim_toupper (s2);
    if (s1 < s2)
        return -1;
    if (s1 > s2)
        return 1;
    if (s1 == 0)
        return 0;
    }
return 0;
}

/* strcasecmp() is not available on all platforms */
int sim_strcasecmp (const char *string1, const char *string2)
{
size_t i = 0;
unsigned char s1, s2;

while (1) {
    s1 = (unsigned char)string1[i];
    s2 = (unsigned char)string2[i];
    s1 = (unsigned char)sim_toupper (s1);
    s2 = (unsigned char)sim_toupper (s2);
    if (s1 == s2) {
        if (s1 == 0)
            return 0;
        i++;
        continue;
        }
    if (s1 < s2)
        return -1;
    if (s1 > s2)
        return 1;
    }
return 0;
}

/* strlcat() and strlcpy() are not available on all platforms */
/* Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com> */
/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
size_t sim_strlcat(char *dst, const char *src, size_t size)
{
char *d = dst;
const char *s = src;
size_t n = size;
size_t dlen;

/* Find the end of dst and adjust bytes left but don't go past end */
while (n-- != 0 && *d != '\0')
    d++;
dlen = d - dst;
n = size - dlen;

if (n == 0)
    return (dlen + strlen(s));
while (*s != '\0') {
    if (n != 1) {
        *d++ = *s;
        n--;
        }
    s++;
    }
*d = '\0';

return (dlen + (s - src));          /* count does not include NUL */
}

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t sim_strlcpy (char *dst, const char *src, size_t size)
{
char *d = dst;
const char *s = src;
size_t n = size;

/* Copy as many bytes as will fit */
if (n != 0) {
    while (--n != 0) {
        if ((*d++ = *s++) == '\0')
            break;
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (size != 0)
            *d = '\0';              /* NUL-terminate dst */
        while (*s++)
            ;
        }
return (s - src - 1);               /* count does not include NUL */
}

/* get_yn               yes/no question

   Inputs:
        ques    =       pointer to question
        deflt   =       default answer
   Outputs:
        result  =       true if yes, false if no
*/

t_stat get_yn (const char *ques, t_stat deflt)
{
char cbuf[CBUFSIZE];
const char *cptr;

if (sim_switches & SWMASK ('Y'))
    return TRUE;
if (sim_switches & SWMASK ('N'))
    return FALSE;
if (sim_rem_cmd_active_line != -1)
    return deflt;
cptr = read_line_p (ques, cbuf, sizeof(cbuf), stdin);
if ((cptr == NULL) || (*cptr == 0))
    return deflt;
if ((*cptr == 'Y') || (*cptr == 'y'))
    return TRUE;
return FALSE;
}

/* get_uint             unsigned number

   Inputs:
        cptr    =       pointer to input string
        radix   =       input radix
        max     =       maximum acceptable value
        *status =       pointer to error status
   Outputs:
        val     =       value
*/

t_value get_uint (const char *cptr, uint32 radix, t_value max, t_stat *status)
{
t_value val;
CONST char *tptr;

*status = SCPE_OK;
val = strtotv ((CONST char *)cptr, &tptr, radix);
if ((cptr == tptr) || (val > max))
    *status = SCPE_ARG;
else {
    while (sim_isspace (*tptr)) tptr++;
    if (*tptr != 0)
        *status = SCPE_ARG;
    }
return val;
}

/* get_range            range specification

   Inputs:
        dptr    =       pointer to device (NULL if none)
        cptr    =       pointer to input string
        *lo     =       pointer to low result
        *hi     =       pointer to high result
        aradix  =       radix
        max     =       default high value
        term    =       terminating character, 0 if none
   Outputs:
        tptr    =       input pointer after processing
                        NULL if error
*/

CONST char *get_range (DEVICE *dptr, CONST char *cptr, t_addr *lo, t_addr *hi,
    uint32 rdx, t_addr max, char term)
{
CONST char *tptr;

if (max && strncmp (cptr, "ALL", strlen ("ALL")) == 0) {    /* ALL? */
    tptr = cptr + strlen ("ALL");
    *lo = 0;
    *hi = max;
    }
else {
    if ((strncmp (cptr, ".", strlen (".")) == 0) &&             /* .? */
        ((cptr[1] == '\0') || 
         (cptr[1] == '-')  || 
         (cptr[1] == ':')  || 
         (cptr[1] == '/'))) {
        tptr = cptr + strlen (".");
        *lo = *hi = sim_last_addr;
        }
    else {
        if (strncmp (cptr, "$", strlen ("$")) == 0) {           /* $? */
            tptr = cptr + strlen ("$");
            *hi = *lo = (t_addr)sim_last_val;
            }
        else {
            if (dptr && sim_vm_parse_addr)                      /* get low */
                *lo = sim_vm_parse_addr (dptr, cptr, &tptr);
            else
                *lo = (t_addr) strtotv (cptr, &tptr, rdx);
            if (cptr == tptr)                                   /* error? */
                    return NULL;
            }
        }
    if ((*tptr == '-') || (*tptr == ':')) {             /* range? */
        cptr = tptr + 1;
        if (dptr && sim_vm_parse_addr)                  /* get high */
            *hi = sim_vm_parse_addr (dptr, cptr, &tptr);
        else *hi = (t_addr) strtotv (cptr, &tptr, rdx);
        if (cptr == tptr)
            return NULL;
        if (*lo > *hi)
            return NULL;
        }
    else if (*tptr == '/') {                            /* relative? */
        cptr = tptr + 1;
        *hi = (t_addr) strtotv (cptr, &tptr, rdx);      /* get high */
        if ((cptr == tptr) || (*hi == 0))
            return NULL;
        *hi = *lo + *hi - 1;
        }
    else *hi = *lo;
    }
sim_last_addr = *hi;
if (term && (*tptr++ != term))
    return NULL;
return tptr;
}

/* sim_decode_quoted_string

   Inputs:
        iptr        =   pointer to input string
        optr        =   pointer to output buffer
                        the output buffer must be allocated by the caller 
                        and to avoid overrunat it must be at least as big 
                        as the input string.

   Outputs
        result      =   status of decode SCPE_OK when good, SCPE_ARG otherwise
        osize       =   size of the data in the optr buffer

   The input string must be quoted.  Quotes may be either single or 
   double but the opening anc closing quote characters must match.  
   Within quotes C style character escapes are allowed.  

   The following character escapes are explicitly supported:
        \r  ASCII Carriage Return character (Decimal value 13)
        \n  ASCII Linefeed character (Decimal value 10)
        \f  ASCII Formfeed character (Decimal value 12)
        \t  ASCII Horizontal Tab character (Decimal value 9)
        \v  ASCII Vertical Tab character (Decimal value 11)
        \b  ASCII Backspace character (Decimal value 8)
        \\  ASCII Backslash character (Decimal value 92)
        \'  ASCII Single Quote character (Decimal value 39)
        \"  ASCII Double Quote character (Decimal value 34)
        \?  ASCII Question Mark character (Decimal value 63)
        \e  ASCII Escape character (Decimal value 27)
     as well as octal character values of the form:
        \n{n{n}} where each n is an octal digit (0-7)
     and hext character values of the form:
        \xh{h} where each h is a hex digit (0-9A-Fa-f)
        
*/

t_stat sim_decode_quoted_string (const char *iptr, uint8 *optr, uint32 *osize)
{
char quote_char;
uint8 *ostart = optr;

*osize = 0;
if ((strlen(iptr) == 1) || 
    (iptr[0] != iptr[strlen(iptr)-1]) ||
    ((iptr[strlen(iptr)-1] != '"') && (iptr[strlen(iptr)-1] != '\'')))
    return SCPE_ARG;            /* String must be quote delimited */
quote_char = *iptr++;           /* Save quote character */
while (iptr[1]) {               /* Skip trailing quote */
    if (*iptr != '\\') {
        if (*iptr == quote_char)
            return SCPE_ARG;    /* Imbedded quotes must be escaped */
        *(optr++) = (uint8)(*(iptr++));
        continue;
        }
    ++iptr; /* Skip backslash */
    switch (*iptr) {
        case 'r':   /* ASCII Carriage Return character (Decimal value 13) */
            *(optr++) = 13; ++iptr;
            break;
        case 'n':   /* ASCII Linefeed character (Decimal value 10) */
            *(optr++) = 10; ++iptr;
            break;
        case 'f':   /* ASCII Formfeed character (Decimal value 12) */
            *(optr++) = 12; ++iptr;
            break;
        case 't':   /* ASCII Horizontal Tab character (Decimal value 9) */
            *(optr++) = 9; ++iptr;
            break;
        case 'v':   /* ASCII Vertical Tab character (Decimal value 11) */
            *(optr++) = 11; ++iptr;
            break;
        case 'b':   /* ASCII Backspace character (Decimal value 8) */
            *(optr++) = 8; ++iptr;
            break;
        case '\\':   /* ASCII Backslash character (Decimal value 92) */
            *(optr++) = 92; ++iptr;
            break;
        case 'e':   /* ASCII Escape character (Decimal value 27) */
            *(optr++) = 27; ++iptr;
            break;
        case '\'':   /* ASCII Single Quote character (Decimal value 39) */
            *(optr++) = 39; ++iptr;
            break;
        case '"':   /* ASCII Double Quote character (Decimal value 34) */
            *(optr++) = 34; ++iptr;
            break;
        case '?':   /* ASCII Question Mark character (Decimal value 63) */
            *(optr++) = 63; ++iptr;
            break;
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
            *optr = *(iptr++) - '0';
            if ((*iptr >= '0') && (*iptr <= '7'))
                *optr = ((*optr)<<3) + (*(iptr++) - '0');
            if ((*iptr >= '0') && (*iptr <= '7'))
                *optr = ((*optr)<<3) + (*(iptr++) - '0');
            ++optr;
            break;
        case 'x':
            if (1) {
                static const char *hex_digits = "0123456789ABCDEF";
                const char *c;

                ++iptr;
                *optr = 0;
                c = strchr (hex_digits, sim_toupper(*iptr));
                if (c) {
                    *optr = ((*optr)<<4) + (uint8)(c-hex_digits);
                    ++iptr;
                    }
                c = strchr (hex_digits, sim_toupper(*iptr));
                if (c) {
                    *optr = ((*optr)<<4) + (uint8)(c-hex_digits);
                    ++iptr;
                    }
                ++optr;
                }
            break;
        default:
            return SCPE_ARG;    /* Invalid escape */
        }
    }
*optr = '\0';
*osize = (uint32)(optr-ostart);
return SCPE_OK;
}

/* sim_encode_quoted_string

   Inputs:
        iptr        =   pointer to input buffer
        size        =   number of bytes of data in the buffer

   Outputs
        optr        =   pointer to output buffer
                        the output buffer must be freed by the caller

   The input data will be encoded into a simply printable form.
   Control and other non-printable data will be escaped using the
   following rules:

   The following character escapes are explicitly supported:
        \r  ASCII Carriage Return character (Decimal value 13)
        \n  ASCII Linefeed character (Decimal value 10)
        \f  ASCII Formfeed character (Decimal value 12)
        \t  ASCII Horizontal Tab character (Decimal value 9)
        \v  ASCII Vertical Tab character (Decimal value 11)
        \b  ASCII Backspace character (Decimal value 8)
        \\  ASCII Backslash character (Decimal value 92)
        \'  ASCII Single Quote character (Decimal value 39)
        \"  ASCII Double Quote character (Decimal value 34)
        \?  ASCII Question Mark character (Decimal value 63)
        \e  ASCII Escape character (Decimal value 27)
     as well as octal character values of the form:
        \n{n{n}} where each n is an octal digit (0-7)
     and hext character values of the form:
        \xh{h} where each h is a hex digit (0-9A-Fa-f)
        
*/

char *sim_encode_quoted_string (const uint8 *iptr, uint32 size)
{
uint32 i;
t_bool double_quote_found = FALSE;
t_bool single_quote_found = FALSE;
char quote = '"';
char *tptr, *optr;

optr = (char *)malloc (4*size + 3);
if (optr == NULL)
    return NULL;
tptr = optr;
for (i=0; i<size; i++)
    switch ((char)iptr[i]) {
        case '"':
            double_quote_found = TRUE;
            break;
        case '\'':
            single_quote_found = TRUE;
            break;
        }
if (double_quote_found && (!single_quote_found))
    quote = '\'';
*tptr++ = quote;
while (size--) {
    switch (*iptr) {
        case '\r': 
            *tptr++ = '\\'; *tptr++ = 'r'; break;
        case '\n':
            *tptr++ = '\\'; *tptr++ = 'n'; break;
        case '\f':
            *tptr++ = '\\'; *tptr++ = 'f'; break;
        case '\t':
            *tptr++ = '\\'; *tptr++ = 't'; break;
        case '\v':
            *tptr++ = '\\'; *tptr++ = 'v'; break;
        case '\b':
            *tptr++ = '\\'; *tptr++ = 'b'; break;
        case '\\':
            *tptr++ = '\\'; *tptr++ = '\\'; break;
        case '"':
        case '\'':
            if (quote == *iptr)
                *tptr++ = '\\';
            /* fall through */
        default:
            if (sim_isprint (*iptr))
                *tptr++ = *iptr;
            else {
                sprintf (tptr, "\\%03o", *iptr);
                tptr += 4;
                }
            break;
        }
    ++iptr;
    }
*tptr++ = quote;
*tptr++ = '\0';
return optr;
}

void fprint_buffer_string (FILE *st, const uint8 *buf, uint32 size)
{
char *string;

string = sim_encode_quoted_string (buf, size);
fprintf (st, "%s", string);
free (string);
}


/* Find_device          find device matching input string

   Inputs:
        cptr    =       pointer to input string
   Outputs:
        result  =       pointer to device
*/

DEVICE *find_dev (const char *cptr)
{
int32 i;
DEVICE *dptr;

if (cptr == NULL)
    return NULL;
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
    if ((strcmp (cptr, dptr->name) == 0) ||
        (dptr->lname &&
        (strcmp (cptr, dptr->lname) == 0)))
        return dptr;
    }
for (i = 0; sim_internal_device_count && (dptr = sim_internal_devices[i]); ++i) {
    if ((strcmp (cptr, dptr->name) == 0) ||
        (dptr->lname &&
        (strcmp (cptr, dptr->lname) == 0)))
        return dptr;
    }
return NULL;
}

/* Find_unit            find unit matching input string

   Inputs:
        cptr    =       pointer to input string
        uptr    =       pointer to unit pointer
   Outputs:
        result  =       pointer to device (null if no dev)
        *iptr   =       pointer to unit (null if nx unit)

*/

DEVICE *find_unit (const char *cptr, UNIT **uptr)
{
uint32 i, u;
const char *nptr;
const char *tptr;
t_stat r;
DEVICE *dptr;

if (uptr == NULL)                                       /* arg error? */
    return NULL;
*uptr = NULL;
if ((dptr = find_dev (cptr))) {                         /* exact match? */
    if (qdisable (dptr))                                /* disabled? */
        return NULL;
    *uptr = dptr->units;                                /* unit 0 */
    return dptr;
    }

for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {     /* base + unit#? */
    if (dptr->numunits &&                               /* any units? */
        (((nptr = dptr->name) &&
          (strncmp (cptr, nptr, strlen (nptr)) == 0)) ||
         ((nptr = dptr->lname) &&
          (strncmp (cptr, nptr, strlen (nptr)) == 0)))) {
        tptr = cptr + strlen (nptr);
        if (sim_isdigit (*tptr)) {
            if (qdisable (dptr))                        /* disabled? */
                return NULL;
            u = (uint32) get_uint (tptr, 10, dptr->numunits - 1, &r);
            if (r != SCPE_OK)                           /* error? */
                *uptr = NULL;
            else *uptr = dptr->units + u;
            return dptr;
            }
        }
    }
return NULL;
}

/* sim_register_internal_device   Add device to internal device list

   Inputs:
        dptr    =       pointer to device
*/

t_stat sim_register_internal_device (DEVICE *dptr)
{
uint32 i;

for (i = 0; i < sim_internal_device_count; i++)
    if (sim_internal_devices[i] == dptr)
        return SCPE_OK;
for (i = 0; (sim_devices[i] != NULL); i++)
    if (sim_devices[i] == dptr)
        return SCPE_OK;
++sim_internal_device_count;
sim_internal_devices = (DEVICE **)realloc(sim_internal_devices, (sim_internal_device_count+1)*sizeof(*sim_internal_devices));
sim_internal_devices[sim_internal_device_count-1] = dptr;
sim_internal_devices[sim_internal_device_count] = NULL;
return SCPE_OK;
}

/* Find_dev_from_unit   find device for unit

   Inputs:
        uptr    =       pointer to unit
   Outputs:
        result  =       pointer to device
*/

DEVICE *find_dev_from_unit (UNIT *uptr)
{
DEVICE *dptr;
uint32 i, j;

if (uptr == NULL)
    return NULL;
if (uptr->dptr)
    return uptr->dptr;
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
    for (j = 0; j < dptr->numunits; j++) {
        if (uptr == (dptr->units + j)) {
            uptr->dptr = dptr;
            return dptr;
            }
        }
    }
for (i = 0; i<sim_internal_device_count; i++) {
    dptr = sim_internal_devices[i];
    for (j = 0; j < dptr->numunits; j++) {
        if (uptr == (dptr->units + j)) {
            uptr->dptr = dptr;
            return dptr;
            }
        }
    }
return NULL;
}

/* Test for disabled device */

t_bool qdisable (DEVICE *dptr)
{
return (dptr->flags & DEV_DIS? TRUE: FALSE);
}

/* find_reg_glob        find globally unique register

   Inputs:
        cptr    =       pointer to input string
        optr    =       pointer to output pointer (can be null)
        gdptr   =       pointer to global device
   Outputs:
        result  =       pointer to register, NULL if error
        *optr   =       pointer to next character in input string
        *gdptr  =       pointer to device where found
        *stat   =       pointer to stat for reason
*/

REG *find_reg_glob_reason (CONST char *cptr, CONST char **optr, DEVICE **gdptr, t_stat *stat)
{
int32 i, j;
DEVICE *dptr, **devs, **dptrptr[] = {sim_devices, sim_internal_devices, NULL};
REG *rptr, *srptr = NULL;

if (stat)
    *stat = SCPE_OK;
*gdptr = NULL;
for (j = 0; (devs = dptrptr[j]) != NULL; j++) {
    for (i = 0; (dptr = devs[i]) != NULL; i++) {        /* all dev */
        if (dptr->flags & DEV_DIS)                          /* skip disabled */
            continue;
        if ((rptr = find_reg (cptr, optr, dptr))) {         /* found? */
            if (srptr) {                                    /* ambig? err */
                if (stat) {
                    if (sim_show_message) {
                        if (*stat == SCPE_OK)
                            sim_printf ("Ambiguous register.  %s appears in devices %s and %s", cptr, (*gdptr)->name, dptr->name);
                        else
                            sim_printf (" and %s", dptr->name);
                        }
                    *stat = SCPE_AMBREG|SCPE_NOMESSAGE;
                    }
                else
                    return NULL;
                }
            srptr = rptr;                                   /* save reg */
            *gdptr = dptr;                                  /* save unit */
            }
        }
    }
if (stat && (*stat != SCPE_OK)) {
    if (sim_show_message)
        sim_printf ("\n");
    srptr = NULL;
    }
return srptr;
}

REG *find_reg_glob (CONST char *cptr, CONST char **optr, DEVICE **gdptr)
{
return find_reg_glob_reason (cptr, optr, gdptr, NULL);
}

/* find_reg             find register matching input string

   Inputs:
        cptr    =       pointer to input string
        optr    =       pointer to output pointer (can be null)
        dptr    =       pointer to device
   Outputs:
        result  =       pointer to register, NULL if error
        *optr   =       pointer to next character in input string
*/

REG *find_reg (CONST char *cptr, CONST char **optr, DEVICE *dptr)
{
CONST char *tptr;
REG *rptr;
size_t slnt;

if ((cptr == NULL) || (dptr == NULL) || (dptr->registers == NULL))
    return NULL;
tptr = cptr;
do {
    tptr++;
    } while (sim_isalnum (*tptr) || (*tptr == '*') || (*tptr == '_') || (*tptr == '.'));
slnt = tptr - cptr;
for (rptr = dptr->registers; rptr->name != NULL; rptr++) {
    if ((slnt == strlen (rptr->name)) &&
        (strncmp (cptr, rptr->name, slnt) == 0)) {
        if (optr != NULL)
            *optr = tptr;
        return rptr;
        }
    }
return NULL;
}

/* get_switches         get switches from input string

   Inputs:
        cptr    =       pointer to input string
   Outputs:
        *sw      =       switch bit mask
        *mumber  =       numeric value
   Return value:        SW_ERROR     if error
                        SW_BITMASK   if switch bitmask or not a switch
                        SW_NUMBER    if numeric
*/

SWITCH_PARSE get_switches (const char *cptr, int32 *sw, int32 *number)
{
*sw = 0;
if (*cptr != '-')
    return SW_BITMASK;
if (number)
    *number = 0;
if (sim_isdigit(cptr[1])) {
    char *end;
    long val = strtol (1+cptr, &end, 10);

    if ((*end != 0) || (number == NULL))
        return SW_ERROR;
    *number = (int32)val;
    return SW_NUMBER;
    }
for (cptr++; (sim_isspace (*cptr) == 0) && (*cptr != 0); cptr++) {
    if (sim_isalpha (*cptr) == 0)
        return SW_ERROR;
    *sw = *sw | SWMASK (sim_toupper (*cptr));
    }
return SW_BITMASK;
}

/* get_sim_sw           accumulate sim_switches

   Inputs:
        cptr    =       pointer to input string
   Outputs:
        ptr     =       pointer to first non-string glyph
                        NULL if error
*/

CONST char *get_sim_sw (CONST char *cptr)
{
int32 lsw, lnum;
char gbuf[CBUFSIZE];

while (*cptr == '-') {                                  /* while switches */
    cptr = get_glyph (cptr, gbuf, 0);                   /* get switch glyph */
    switch (get_switches (gbuf, &lsw, &lnum)) {         /* parse */
        case SW_ERROR:
            return NULL;
        case SW_BITMASK:
            sim_switches = sim_switches | lsw;          /* accumulate */
            break;
        case SW_NUMBER:
            sim_switch_number = lnum;                   /* set number */
            break;
        }
    }
return cptr;
}

/* get_sim_opt          get simulator command options

   Inputs:
        opt     =       command options
        cptr    =       pointer to input string
   Outputs:
        ptr     =       pointer to next glypsh, NULL if error
        *stat   =       error status
*/

CONST char *get_sim_opt (int32 opt, CONST char *cptr, t_stat *st)
{
int32 t, n;
char gbuf[CBUFSIZE];
CONST char *svptr;
DEVICE *tdptr;
UNIT *tuptr;

sim_switches = 0;                                       /* no switches */
sim_switch_number = 0;                                  /* no numberuc switch */
sim_ofile = NULL;                                       /* no output file */
sim_schrptr = NULL;                                     /* no search */
sim_schaptr = NULL;                                     /* no search */
sim_stabr.logic = sim_staba.logic = SCH_OR;             /* default search params */
sim_stabr.boolop = sim_staba.boolop = SCH_GE;
sim_stabr.count = 1;
sim_stabr.mask = (t_value *)realloc (sim_stabr.mask, sim_emax * sizeof(*sim_stabr.mask));
memset (sim_stabr.mask, 0, sim_emax * sizeof(*sim_stabr.mask));
sim_stabr.comp = (t_value *)realloc (sim_stabr.comp, sim_emax * sizeof(*sim_stabr.comp));
memset (sim_stabr.comp, 0, sim_emax * sizeof(*sim_stabr.comp));
sim_staba.count = sim_emax;
sim_staba.mask = (t_value *)realloc (sim_staba.mask, sim_emax * sizeof(*sim_staba.mask));
memset (sim_staba.mask, 0, sim_emax * sizeof(*sim_staba.mask));
sim_staba.comp = (t_value *)realloc (sim_staba.comp, sim_emax * sizeof(*sim_staba.comp));
memset (sim_staba.comp, 0, sim_emax * sizeof(*sim_staba.comp));
sim_dfdev = sim_dflt_dev;
sim_dfunit = sim_dfdev->units;
sim_opt_out = 0;                                        /* no options yet */
*st = SCPE_OK;
while (*cptr) {                                         /* loop through modifiers */
    svptr = cptr;                                       /* save current position */
    if ((opt & CMD_OPT_OF) && (*cptr == '@')) {         /* output file spec? */
        if (sim_ofile) {                                /* already got one? */
            fclose (sim_ofile);                         /* one per customer */
            *st = SCPE_ARG;
            return NULL;
            }
        cptr = get_glyph (cptr + 1, gbuf, 0);
        sim_ofile = sim_fopen (gbuf, "a");              /* open for append */
        if (sim_ofile == NULL) {                        /* open failed? */
            *st = SCPE_OPENERR;
            return NULL;
            }
        sim_opt_out |= CMD_OPT_OF;                      /* got output file */
        continue;
        }
    cptr = get_glyph (cptr, gbuf, 0);
    switch (get_switches (gbuf, &t, &n)) {      /* try for switches */
        case SW_ERROR:                          /* err if bad switch */
            *st = SCPE_INVSW;
            return NULL;
        case SW_NUMBER:
            sim_switch_number = n;              /* set number */
            continue;
        case SW_BITMASK:
            if (t != 0) {
                sim_switches = sim_switches | t;/* or in new switches */
                continue;
                }
            break;
        }
    if ((opt & CMD_OPT_SCH) &&                     /* if allowed, */
        get_rsearch (gbuf, sim_dfdev->dradix, &sim_stabr)) { /* try for search */
        sim_schrptr = &sim_stabr;                       /* set search */
        sim_schaptr = get_asearch (gbuf, sim_dfdev->dradix, &sim_staba);/* populate memory version of the same expression */
        sim_opt_out |= CMD_OPT_SCH;                     /* got search */
        }
    else if ((opt & CMD_OPT_DFT) &&                     /* default allowed? */
        ((sim_opt_out & CMD_OPT_DFT) == 0) &&           /* none yet? */
        (tdptr = find_unit (gbuf, &tuptr)) &&           /* try for default */
        (tuptr != NULL)) {
        sim_dfdev = tdptr;                              /* set as default */
        sim_dfunit = tuptr;
        sim_opt_out |= CMD_OPT_DFT;                     /* got default */
        }
    else return svptr;                                  /* not rec, break out */
    }
return cptr;
}

/* put_switches         put switches into string

   Inputs:
        buf     =       pointer to string buffer
        bufsize =       size of string buffer
        sw      =       switch bit mask
   Outputs:
        buf     =       buffer with switches converted to text
*/

const char *put_switches (char *buf, size_t bufsize, uint32 sw)
{
char *optr = buf;
int32 bit;

memset (buf, 0, bufsize);
if ((sw == 0) || (bufsize < 3))
    return buf;
--bufsize;                          /* leave room for terminating NUL */
*optr++ = '-';
for (bit=0; bit <= ('Z'-'A'); bit++)
    if (sw & (1 << bit))
        if ((size_t)(optr - buf) < bufsize)
            *optr++ = 'A' + bit;
return buf;
}


/* Match file extension

   Inputs:
        fnam    =       file name
        ext     =       extension, without period
   Outputs:
        cp      =       pointer to final '.' if match, NULL if not
*/

CONST char *match_ext (CONST char *fnam, const char *ext)
{
CONST char *pptr, *fptr;
const char *eptr;

if ((fnam == NULL) || (ext == NULL))                    /* bad arguments? */
     return NULL;
pptr = strrchr (fnam, '.');                             /* find last . */
if (pptr) {                                             /* any? */
    for (fptr = pptr + 1, eptr = ext;                   /* match characters */
#if defined (VMS)                                       /* VMS: stop at ; or null */
    (*fptr != 0) && (*fptr != ';');
#else
    *fptr != 0;                                         /* others: stop at null */
#endif
    fptr++, eptr++) {
        if (sim_toupper (*fptr) != sim_toupper (*eptr))
            return NULL;
        }
    if (*eptr != 0)                                     /* ext exhausted? */
        return NULL;
    }
return pptr;
}

/* Get register search specification

   Inputs:
        cptr    =       pointer to input string
        radix   =       radix for numbers
        schptr =        pointer to search table
   Outputs:
        return =        NULL if error
                        schptr if valid search specification
*/

SCHTAB *get_rsearch (CONST char *cptr, int32 radix, SCHTAB *schptr)
{
int32 c, logop, cmpop;
t_value logval, cmpval;
const char *sptr;
CONST char *tptr;
const char logstr[] = "|&^", cmpstr[] = "=!><";

logval = cmpval = 0;
if (*cptr == 0)                                         /* check for clause */
    return NULL;
for (logop = cmpop = -1; (c = *cptr++); ) {             /* loop thru clauses */
    if ((sptr = strchr (logstr, c))) {                  /* check for mask */
        logop = (int32)(sptr - logstr);
        logval = strtotv (cptr, &tptr, radix);
        if (cptr == tptr)
            return NULL;
        cptr = tptr;
        }
    else if ((sptr = strchr (cmpstr, c))) {             /* check for boolop */
        cmpop = (int32)(sptr - cmpstr);
        if (*cptr == '=') {
            cmpop = cmpop + strlen (cmpstr);
            cptr++;
            }
        cmpval = strtotv (cptr, &tptr, radix);
        if (cptr == tptr)
            return NULL;
        cptr = tptr;
        }
    else return NULL;
    }                                                   /* end for */
if (schptr->count != 1) {
    free (schptr->mask);
    schptr->mask = (t_value *)calloc (sim_emax, sizeof(*schptr->mask));
    free (schptr->comp);
    schptr->comp = (t_value *)calloc (sim_emax, sizeof(*schptr->comp));
    }
if (logop >= 0) {
    schptr->logic = logop;
    schptr->mask[0] = logval;
    }
if (cmpop >= 0) {
    schptr->boolop = cmpop;
    schptr->comp[0] = cmpval;
    }
schptr->count = 1;
return schptr;
}

/* Get memory search specification

   Inputs:
        cptr    =       pointer to input string
        radix   =       radix for numbers
        schptr =        pointer to search table
   Outputs:
        return =        NULL if error
                        schptr if valid search specification
*/

SCHTAB *get_asearch (CONST char *cptr, int32 radix, SCHTAB *schptr)
{
int32 c, logop, cmpop;
t_value *logval, *cmpval;
t_stat reason;
CONST char *ocptr = cptr;
const char *sptr;
char gbuf[CBUFSIZE];
const char logstr[] = "|&^", cmpstr[] = "=!><";

if (*cptr == 0)                                         /* check for clause */
    return NULL;
logval = (t_value *)calloc (sim_emax, sizeof(*logval));
cmpval = (t_value *)calloc (sim_emax, sizeof(*cmpval));
for (logop = cmpop = -1; (c = *cptr++); ) {             /* loop thru clauses */
    if ((sptr = strchr (logstr, c))) {                  /* check for mask */
        logop = (int32)(sptr - logstr);
        cptr = get_glyph (cptr, gbuf, 0);
        reason = parse_sym (gbuf, 0, sim_dfunit, logval, sim_switches);
        if (reason > 0) {
            free (logval);
            free (cmpval);
            return get_rsearch (ocptr, radix, schptr);
            }
        }
    else if ((sptr = strchr (cmpstr, c))) {             /* check for boolop */
        cmpop = (int32)(sptr - cmpstr);
        if (*cptr == '=') {
            cmpop = cmpop + strlen (cmpstr);
            cptr++;
            }
        cptr = get_glyph (cptr, gbuf, 0);
        reason = parse_sym (gbuf, 0, sim_dfunit, cmpval, sim_switches);
        if (reason > 0) {
            free (logval);
            free (cmpval);
            return get_rsearch (ocptr, radix, schptr);
            }
        }
    else {
        free (logval);
        free (cmpval);
        return NULL;
        }
    }                                                   /* end for */
if (schptr->count != (1 - reason)) {
    schptr->count = 1 - reason;
    free (schptr->mask);
    schptr->mask = (t_value *)calloc (sim_emax, sizeof(*schptr->mask));
    free (schptr->comp);
    schptr->comp = (t_value *)calloc (sim_emax, sizeof(*schptr->comp));
    }
if (logop >= 0) {
    schptr->logic = logop;
    free (schptr->mask);
    schptr->mask = logval;
    }
else {
    free (logval);
    }
if (cmpop >= 0) {
    schptr->boolop = cmpop;
    free (schptr->comp);
    schptr->comp = cmpval;
    }
else {
    free (cmpval);
    }
return schptr;
}

/* Test value against search specification

   Inputs:
        val    =        value list to test
        schptr =        pointer to search table
   Outputs:
        return =        1 if value passes search criteria, 0 if not
*/

int32 test_search (t_value *values, SCHTAB *schptr)
{
t_value *val = NULL;
int32 i, updown;
int32 ret = 0;

if (schptr == NULL)
    return ret;

val = (t_value *)malloc (schptr->count * sizeof (*values));

for (i=0; i<(int32)schptr->count; i++) {
    val[i] = values[i];
    switch (schptr->logic) {                            /* case on logical */

        case SCH_OR:
            val[i] = val[i] | schptr->mask[i];
            break;

        case SCH_AND:
            val[i] = val[i] & schptr->mask[i];
            break;

        case SCH_XOR:
            val[i] = val[i] ^ schptr->mask[i];
            break;
            }
    }

ret = 1;
if (1) {    /* Little Endian VM */
    updown = -1;
    i=schptr->count-1;
    }
else {      /* Big Endian VM */
    updown = 1;
    i=0;
    }
for (; (i>=0) && (i<(int32)schptr->count) && ret; i += updown) {
    switch (schptr->boolop) {                           /* case on comparison */

        case SCH_E: case SCH_EE:
            if (val[i] != schptr->comp[i])
                ret = 0;
            break;

        case SCH_N: case SCH_NE:
            if (val[i] != schptr->comp[i])
                ret = 0;
            break;

        case SCH_G:
            if (val[i] <= schptr->comp[i])
                ret = 0;
            break;

        case SCH_GE:
            if (val[i] < schptr->comp[i])
                ret = 0;
            break;

        case SCH_L:
            if (val[i] >= schptr->comp[i])
                ret = 0;
            break;

        case SCH_LE:
            if (val[i] > schptr->comp[i])
                ret = 0;
            break;
        }
    }
free (val);
return ret;
}

/* Radix independent input/output package

   strtotv - general radix input routine
   strtotsv - general radix input routine

   Inputs:
        inptr   =       string to convert
        endptr  =       pointer to first unconverted character
        radix   =       radix for input
   Output strtotv:
        value   =       converted value
   Outputs strtotsv:
        value   =       converted signed value

   On an error, the endptr will equal the inptr.

   If the value of radix is zero, the syntax expected is similar 
   to that of integer constants, which is formed by a succession of:

      - An optional sign character (+ or -)
      - An optional prefix indicating octal or hexadecimal radix 
        ("0" or "0x"/"0X" respectively)
      - A sequence of decimal digits (if no radix prefix was specified) 
        or one of binary, octal or hexadecimal digits if a specific 
        prefix is present

   If the radix value is between 2 and 36, the format expected for 
   the integral number is a succession of any of the valid digits 
   and/or letters needed to represent integers of the specified 
   radix (starting from '0' and up to 'z'/'Z' for radix 36). The 
   sequence may optionally be preceded by a sign (either + or -) and, 
   if base is 16, an optional "0x" or "0X" prefix.  If the base is 2,
   an optional "0b" or "0B" prefix.

*/

t_value strtotv (CONST char *inptr, CONST char **endptr, uint32 radix)
{
t_bool nodigits;
t_value val;
uint32 c, digit;

if (endptr)
    *endptr = inptr;                                    /* assume fails */
if (((radix < 2) || (radix > 36)) && (radix != 0))
    return 0;
while (sim_isspace (*inptr))                            /* bypass white space */
    inptr++;
if (((radix == 0) || (radix == 16)) && 
    ((!memcmp("0x", inptr, 2)) || (!memcmp("0X", inptr, 2)))) {
    radix = 16;
    inptr += 2;
    }
if (((radix == 0) || (radix == 2)) && 
    ((!memcmp("0b", inptr, 2)) || (!memcmp("0B", inptr, 2)))) {
    radix = 2;
    inptr += 2;
    }
if ((radix == 0) && (*inptr == '0'))            /* Default radix and octal? */
    radix = 8;
if (radix == 0)                                 /* Default base 10 radix */
    radix = 10;
val = 0;
nodigits = TRUE;
for (c = *inptr; sim_isalnum(c); c = *++inptr) {        /* loop through char */
    c = sim_toupper (c);
    if (sim_isdigit (c))                                /* digit? */
        digit = c - (uint32) '0';
    else {
        if (radix <= 10)                                /* stop if not expected */
            break;
        else 
            digit = c + 10 - (uint32) 'A';              /* convert letter */
        }
    if (digit >= radix)                                 /* valid in radix? */
        return 0;
    val = (val * radix) + digit;                        /* add to value */
    nodigits = FALSE;
    }
if (nodigits)                                           /* no digits? */
    return 0;
if (endptr)
    *endptr = inptr;                                    /* result pointer */
return val;
}

t_svalue strtotsv (CONST char *inptr, CONST char **endptr, uint32 radix)
{
t_bool nodigits;
t_svalue val;
t_svalue negate = 1;
uint32 c, digit;

if (endptr)
    *endptr = inptr;                                    /* assume fails */
if (((radix < 2) || (radix > 36)) && (radix != 0))
    return 0;
while (sim_isspace (*inptr))                            /* bypass white space */
    inptr++;
if ((*inptr == '-') ||
    (*inptr == '+')) {
    if (*inptr == '-')
        negate = -1;
    ++inptr;
    }
if (((radix == 0) || (radix == 16)) && 
    ((!memcmp("0x", inptr, 2)) || (!memcmp("0X", inptr, 2)))) {
    radix = 16;
    inptr += 2;
    }
if (((radix == 0) || (radix == 2)) && 
    ((!memcmp("0b", inptr, 2)) || (!memcmp("0B", inptr, 2)))) {
    radix = 2;
    inptr += 2;
    }
if ((radix == 0) && (*inptr == '0'))            /* Default radix and octal? */
    radix = 8;
if (radix == 0)                                 /* Default base 10 radix */
    radix = 10;
val = 0;
nodigits = TRUE;
for (c = *inptr; sim_isalnum(c); c = *++inptr) {        /* loop through char */
    c = sim_toupper (c);
    if (sim_isdigit (c))                                /* digit? */
        digit = c - (uint32) '0';
    else {
        if (radix <= 10)                                /* stop if not expected */
            break;
        else
            digit = c + 10 - (uint32) 'A';              /* convert letter */
        }
    if (digit >= radix)                                 /* valid in radix? */
        return 0;
    val = (val * radix) + digit;                        /* add to value */
    nodigits = FALSE;
    }
if (nodigits)                                           /* no digits? */
    return 0;
if (endptr)
    *endptr = inptr;                                    /* result pointer */
return val * negate;
}

/* fprint_val - general radix printing routine

   Inputs:
        stream  =       stream designator
        val     =       value to print
        radix   =       radix to print
        width   =       width to print
        format  =       leading zeroes format
   Outputs:
        status  =       error status
        if stream is NULL, returns length of output that would
        have been generated.
*/

t_stat sprint_val (char *buffer, t_value val, uint32 radix,
    uint32 width, uint32 format)
{
#define MAX_WIDTH ((int) ((CHAR_BIT * sizeof (t_value) * 4 + 3)/3))
t_value owtest, wtest;
t_bool negative = FALSE;
int32 d, digit, ndigits, commas = 0;
char dbuf[MAX_WIDTH + 1];

if (((format == PV_LEFTSIGN) || (format == PV_RCOMMASIGN)) &&
    (0 > (t_svalue)val)) {
    val = (t_value)(-((t_svalue)val));
    negative = TRUE;
    }
for (d = 0; d < MAX_WIDTH; d++)
    dbuf[d] = (format == PV_RZRO)? '0': ' ';
dbuf[MAX_WIDTH] = 0;
d = MAX_WIDTH;
do {
    d = d - 1;
    digit = (int32) (val % radix);
    val = val / radix;
    dbuf[d] = (char)((digit <= 9)? '0' + digit: 'A' + (digit - 10));
    } while ((d > 0) && (val != 0));
if (negative && (format == PV_LEFTSIGN))
    dbuf[--d] = '-';

switch (format) {
    case PV_LEFT:
    case PV_LEFTSIGN:
        break;
    case PV_RCOMMA:
    case PV_RCOMMASIGN:
        for (digit = 0; digit < MAX_WIDTH; digit++)
            if (dbuf[digit] != ' ')
                break;
        ndigits = MAX_WIDTH - digit;
        commas = (ndigits - 1)/3;
        for (digit=0; digit<ndigits-3; digit++)
            dbuf[MAX_WIDTH + (digit - ndigits) - (ndigits - digit - 1)/3] = dbuf[MAX_WIDTH + (digit - ndigits)];
        for (digit=1; digit<=commas; digit++)
            dbuf[MAX_WIDTH - (digit * 4)] = ',';
        d = d - commas;
        if (negative && (format == PV_RCOMMASIGN))
            dbuf[--d] = '-';
        if (width > MAX_WIDTH) {
            if (!buffer)
                return width;
            sprintf (buffer, "%*s", -((int)width), dbuf);
            return SCPE_OK;
            }
        else
            if (width > 0)
                d = MAX_WIDTH - width;
        break;
    case PV_RZRO:
    case PV_RSPC:
        wtest = owtest = radix;
        ndigits = 1;
        while ((wtest < width_mask[width]) && (wtest >= owtest)) {
            owtest = wtest;
            wtest = wtest * radix;
            ndigits = ndigits + 1;
            }
        if ((MAX_WIDTH - (ndigits + commas)) < d)
            d = MAX_WIDTH - (ndigits + commas);
        break;
    }
if (!buffer)
    return strlen(dbuf+d);
*buffer = '\0';
if (width < strlen(dbuf+d))
    return SCPE_IOERR;
strcpy(buffer, dbuf+d);
return SCPE_OK;
}

t_stat fprint_val (FILE *stream, t_value val, uint32 radix,
    uint32 width, uint32 format)
{
char dbuf[MAX_WIDTH + 1];

if (!stream)
    return sprint_val (NULL, val, radix, width, format);
if (width > MAX_WIDTH)
    width = MAX_WIDTH;
sprint_val (dbuf, val, radix, width, format);
if (fprintf (stream, "%s", dbuf) < 0)
    return SCPE_IOERR;
return SCPE_OK;
}

t_stat sim_print_val (t_value val, uint32 radix,
    uint32 width, uint32 format)
{
char dbuf[MAX_WIDTH + 1];
t_stat stat = SCPE_OK;

if (width > MAX_WIDTH)
    width = MAX_WIDTH;
sprint_val (dbuf, val, radix, width, format);
if (fputs (dbuf, stdout) == EOF)
    stat = SCPE_IOERR;
if ((!sim_oline) && ((sim_log && (sim_log != stdout))))
    if (fputs (dbuf, sim_log) == EOF)
        stat = SCPE_IOERR;
if (sim_deb && (sim_deb != stdout)) {
    TMLN *saved_oline = sim_oline;

    sim_oline = NULL;                               /* avoid potential debug to active socket */
    if (fputs (dbuf, sim_deb) == EOF)
        stat = SCPE_IOERR;
    sim_oline = saved_oline;                        /* restore original socket */
    }
return stat;
}

const char *sim_fmt_secs (double seconds)
{
static char buf[60];
char frac[16] = "";
const char *sign = "";
double val = seconds;
double days, hours, mins, secs, msecs, usecs;

if (val == 0.0)
    return "";
if (val < 0.0) {
    sign = "-";
    val = -val;
    }
days = floor (val / (24.0*60.0*60.0));
val -= (days * 24.0*60.0*60.0);
hours = floor (val / (60.0*60.0));
val -= (hours * 60.0 * 60.0);
mins = floor (val / 60.0);
val -= (mins * 60.0);
secs = floor (val);
val -= secs;
val *= 1000.0;
msecs = floor (val);
val -= msecs;
val *= 1000.0;
usecs = floor (val+0.5);
if (usecs == 1000.0) {
    usecs = 0.0;
    msecs += 1;
    }
if ((msecs > 0.0) || (usecs > 0.0)) {
    sprintf (frac, ".%03.0f%03.0f", msecs, usecs);
    while (frac[strlen (frac) - 1] == '0')
        frac[strlen (frac) - 1] = '\0';
    if (strlen (frac) == 1)
        frac[0] = '\0';
    }
if (days > 0)
    sprintf (buf, "%s%.0f day%s %02.0f:%02.0f:%02.0f%s hour%s", sign, days, (days != 1)? "s" : "", hours, mins, secs, frac, (days == 1) ? "s" : "");
else
    if (hours > 0)
        sprintf (buf, "%s%.0f:%02.0f:%02.0f%s hour", sign, hours, mins, secs, frac);
    else
        if (mins > 0)
            sprintf (buf, "%s%.0f:%02.0f%s minute", sign, mins, secs, frac);
        else
            if (secs > 0)
                sprintf (buf, "%s%.0f%s second", sign, secs, frac);
            else
                if (msecs > 0) {
                    if (usecs > 0)
                        sprintf (buf, "%s%.0f.%s msec", sign, msecs, frac+4);
                    else
                        sprintf (buf, "%s%.0f msec", sign, msecs);
                    }
                else
                    sprintf (buf, "%s%.0f usec", sign, usecs);
if (0 != strncmp ("1 ", buf, 2))
    strcpy (&buf[strlen (buf)], "s");
return buf;
}

const char *sim_fmt_numeric (double number)
{
static char buf[60];
char tmpbuf[60];
size_t len;
uint32 c;
char *p;

sprintf (tmpbuf, "%.0f", number);
len = strlen (tmpbuf);
for (c=0, p=buf; c < len; c++) {
    if ((c > 0) && 
        (sim_isdigit (tmpbuf[c])) && 
        (0 == ((len - c) % 3)))
        *(p++) = ',';
    *(p++) = tmpbuf[c];
    }
*p = '\0';
return buf;
}

/* Event queue package

        sim_activate            add entry to event queue
        sim_activate_abs        add entry to event queue even if event already scheduled
        sim_activate_notbefore  add entry to event queue even if event already scheduled
                                but not before the specified time
        sim_activate_after      add entry to event queue after a specified amount of wall time
        sim_cancel              remove entry from event queue
        sim_process_event       process entries on event queue
        sim_is_active           see if entry is on event queue
        sim_activate_time       return time until activation
        sim_atime               return absolute time for an entry
        sim_gtime               return global time
        sim_qcount              return event queue entry count

   Asynchronous events are set up by queueing a unit data structure
   to the event queue with a timeout (in simulator units, relative
   to the current time).  Each simulator 'times' these events by
   counting down interval counter sim_interval.  When this reaches
   zero the simulator calls sim_process_event to process the event
   and to see if further events need to be processed, or sim_interval
   reset to count the next one.

   The event queue is maintained in clock order; entry timeouts are
   RELATIVE to the time in the previous entry.

   sim_process_event - process event

   Inputs:
        none
   Outputs:
        reason  =       reason code returned by any event processor,
                        or 0 (SCPE_OK) if no exceptions
*/

t_stat sim_process_event (void)
{
UNIT *uptr;
t_stat reason;

if (stop_cpu) {                                         /* stop CPU? */
    stop_cpu = 0;
    return SCPE_STOP;
    }
AIO_UPDATE_QUEUE;
UPDATE_SIM_TIME;                                        /* update sim time */

if (sim_clock_queue == QUEUE_LIST_END) {                /* queue empty? */
    sim_interval = noqueue_time = NOQUEUE_WAIT;         /* flag queue empty */
    sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Queue Empty New Interval = %d\n", sim_interval);
    return SCPE_OK;
    }
sim_processing_event = TRUE;
do {
    uptr = sim_clock_queue;                             /* get first */
    sim_clock_queue = uptr->next;                       /* remove first */
    uptr->next = NULL;                                  /* hygiene */
    uptr->time = 0;
    if (sim_clock_queue != QUEUE_LIST_END)
        sim_interval += sim_clock_queue->time;
    else
        sim_interval = noqueue_time = NOQUEUE_WAIT;
    AIO_EVENT_BEGIN(uptr);
    if (uptr->usecs_remaining) {
        sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Requeueing %s after %.0f usecs\n", sim_uname (uptr), uptr->usecs_remaining);
        reason = sim_timer_activate_after (uptr, uptr->usecs_remaining);
        }
    else {
        sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Processing Event for %s\n", sim_uname (uptr));
        if (uptr->uname && ((*uptr->uname == '\0') || (*uptr->uname == ' ')))
            reason = SCPE_OK;   /* do nothing breakpoint location */
        if (uptr->action != NULL)
            reason = uptr->action (uptr);
        else
            reason = SCPE_OK;
        }
    AIO_EVENT_COMPLETE(uptr, reason);
    if ((reason != SCPE_OK)     &&  /* Provide context for unexpected errors */
        (reason != SCPE_STOP)   && 
        (reason != SCPE_STEP)   && 
        (reason != SCPE_EXPECT) &&
        (reason != SCPE_EXIT)   && 
        (reason != SCPE_REMOTE))
        reason = sim_messagef (SCPE_IERR, "\nUnexpected internal error while processing event for %s which returned %d - %s\n", sim_uname (uptr), reason, sim_error_text (reason));
    } while ((reason == SCPE_OK) && 
             (sim_interval <= 0) && 
             (sim_clock_queue != QUEUE_LIST_END) &&
             (!stop_cpu));

if (sim_clock_queue == QUEUE_LIST_END) {                /* queue empty? */
    sim_interval = noqueue_time = NOQUEUE_WAIT;         /* flag queue empty */
    sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Processing Queue Complete New Interval = %d\n", sim_interval);
    }
else
    sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Processing Queue Complete New Interval = %d(%s)\n", sim_interval, sim_uname(sim_clock_queue));

if ((reason == SCPE_OK) && stop_cpu) {
    stop_cpu = FALSE;
    reason = SCPE_STOP;
    }
sim_processing_event = FALSE;
return reason;
}

/* sim_activate - activate (queue) event

   Inputs:
        uptr    =       pointer to unit
        event_time =    relative timeout
   Outputs:
        reason  =       result (SCPE_OK if ok)
*/

t_stat sim_activate (UNIT *uptr, int32 event_time)
{
if (uptr->dynflags & UNIT_TMR_UNIT)
    return sim_timer_activate (uptr, event_time);
return _sim_activate (uptr, event_time);
}

t_stat _sim_activate (UNIT *uptr, int32 event_time)
{
UNIT *cptr, *prvptr;
int32 accum;

AIO_ACTIVATE (_sim_activate, uptr, event_time);
if (sim_is_active (uptr))                               /* already active? */
    return SCPE_OK;
UPDATE_SIM_TIME;                                        /* update sim time */

sim_debug (SIM_DBG_ACTIVATE, sim_dflt_dev, "Activating %s delay=%d\n", sim_uname (uptr), event_time);

prvptr = NULL;
accum = 0;
for (cptr = sim_clock_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
    if (event_time < (accum + cptr->time))
        break;
    accum = accum + cptr->time;
    prvptr = cptr;
    }
if (prvptr == NULL) {                                   /* insert at head */
    cptr = uptr->next = sim_clock_queue;
    sim_clock_queue = uptr;
    }
else {
    cptr = uptr->next = prvptr->next;                   /* insert at prvptr */
    prvptr->next = uptr;
    }
uptr->time = event_time - accum;
if (cptr != QUEUE_LIST_END)
    cptr->time = cptr->time - uptr->time;
sim_interval = sim_clock_queue->time;
return SCPE_OK;
}

/* sim_activate_abs - activate (queue) event even if event already scheduled

   Inputs:
        uptr    =       pointer to unit
        event_time =    relative timeout
   Outputs:
        reason  =       result (SCPE_OK if ok)
*/

t_stat sim_activate_abs (UNIT *uptr, int32 event_time)
{
AIO_ACTIVATE (sim_activate_abs, uptr, event_time);
sim_cancel (uptr);
return _sim_activate (uptr, event_time);
}

/* sim_activate_notbefore - activate (queue) event even if event already scheduled
                            but not before the specified time

   Inputs:
        uptr    =       pointer to unit
        rtime   =       relative timeout
   Outputs:
        reason  =       result (SCPE_OK if ok)
*/

t_stat sim_activate_notbefore (UNIT *uptr, int32 rtime)
{
uint32 rtimenow, urtime = (uint32)rtime;

AIO_ACTIVATE (sim_activate_notbefore, uptr, rtime);
sim_cancel (uptr);
rtimenow = sim_grtime();
sim_cancel (uptr);
if (0x80000000 <= urtime-rtimenow)
    return _sim_activate (uptr, 0);
else
    return sim_activate (uptr, urtime-rtimenow);
}

/* sim_activate_after - activate (queue) event

   Inputs:
        uptr    =       pointer to unit
        usec_delay =    relative timeout (in microseconds)
   Outputs:
        reason  =       result (SCPE_OK if ok)
*/

t_stat sim_activate_after_abs (UNIT *uptr, uint32 usec_delay)
{
return _sim_activate_after_abs (uptr, usec_delay);
}

t_stat sim_activate_after_abs_d (UNIT *uptr, double usec_delay)
{
return _sim_activate_after_abs (uptr, usec_delay);
}

t_stat _sim_activate_after_abs (UNIT *uptr, double usec_delay)
{
AIO_VALIDATE;                   /* Can't call asynchronously */
sim_cancel (uptr);
return _sim_activate_after (uptr, usec_delay);
}

t_stat sim_activate_after (UNIT *uptr, uint32 usec_delay)
{
return _sim_activate_after (uptr, (double)usec_delay);
}

t_stat sim_activate_after_d (UNIT *uptr, double usec_delay)
{
return _sim_activate_after (uptr, usec_delay);
}

t_stat _sim_activate_after (UNIT *uptr, double usec_delay)
{
AIO_VALIDATE;                   /* Can't call asynchronously */
if (sim_is_active (uptr))                               /* already active? */
    return SCPE_OK;
return sim_timer_activate_after (uptr, usec_delay);
}

/* sim_cancel - cancel (dequeue) event

   Inputs:
        uptr    =       pointer to unit
   Outputs:
        reason  =       result (SCPE_OK if ok)

*/

t_stat sim_cancel (UNIT *uptr)
{
UNIT *cptr, *nptr;

AIO_VALIDATE;
if ((uptr->cancel) && uptr->cancel (uptr))
    return SCPE_OK;
if (uptr->dynflags & UNIT_TMR_UNIT)
    sim_timer_cancel (uptr);
AIO_CANCEL(uptr);
AIO_UPDATE_QUEUE;
if (sim_clock_queue == QUEUE_LIST_END)
    return SCPE_OK;
UPDATE_SIM_TIME;                                        /* update sim time */
if (!sim_is_active (uptr))
    return SCPE_OK;
sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Canceling Event for %s\n", sim_uname(uptr));
nptr = QUEUE_LIST_END;

if (sim_clock_queue == uptr) {
    nptr = sim_clock_queue = uptr->next;
    uptr->next = NULL;                                  /* hygiene */
    }
else {
    for (cptr = sim_clock_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
        if (cptr->next == uptr) {
            nptr = cptr->next = uptr->next;
            uptr->next = NULL;                          /* hygiene */
            break;                                      /* end queue scan */
            }
        }
    }
if (nptr != QUEUE_LIST_END)
    nptr->time += (uptr->next) ? 0 : uptr->time;
if (!uptr->next)
    uptr->time = 0;
uptr->usecs_remaining = 0;
if (sim_clock_queue != QUEUE_LIST_END)
    sim_interval = sim_clock_queue->time;
else sim_interval = noqueue_time = NOQUEUE_WAIT;
if (uptr->next) {
    sim_printf ("Cancel failed for %s\n", sim_uname(uptr));
    if (sim_deb)
        fclose(sim_deb);
    abort ();
    }
return SCPE_OK;
}

/* sim_is_active - test for entry in queue

   Inputs:
        uptr    =       pointer to unit
   Outputs:
        result =        TRUE if unit is busy, FALSE inactive
*/

t_bool sim_is_active (UNIT *uptr)
{
AIO_VALIDATE;
AIO_UPDATE_QUEUE;
return (((uptr->next) || AIO_IS_ACTIVE(uptr) || ((uptr->dynflags & UNIT_TMR_UNIT) ? sim_timer_is_active (uptr) : FALSE)) ? TRUE : FALSE);
}

/* sim_activate_time - return activation time

   Inputs:
        uptr    =       pointer to unit
   Outputs:
        result =        absolute activation time + 1, 0 if inactive
*/

int32 _sim_activate_time (UNIT *uptr)
{
UNIT *cptr;
int32 accum;

accum = 0;
for (cptr = sim_clock_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
    if (cptr == sim_clock_queue) {
        if (sim_interval > 0)
            accum = accum + sim_interval;
        }
    else
        accum = accum + cptr->time;
    if (cptr == uptr)
        return accum + 1 + (int32)((uptr->usecs_remaining * sim_timer_inst_per_sec ()) / 1000000.0);
    }
return 0;
}

int32 sim_activate_time (UNIT *uptr)
{
int32 accum;

AIO_VALIDATE;
accum = _sim_timer_activate_time (uptr);
if (accum >= 0)
    return accum;
return _sim_activate_time (uptr);
}

double sim_activate_time_usecs (UNIT *uptr)
{
UNIT *cptr;
int32 accum;
double result;

AIO_VALIDATE;
result = sim_timer_activate_time_usecs (uptr);
if (result >= 0)
    return result;
accum = 0;
for (cptr = sim_clock_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
    if (cptr == sim_clock_queue) {
        if (sim_interval > 0)
            accum = accum + sim_interval;
        }
    else
        accum = accum + cptr->time;
    if (cptr == uptr)
        return 1.0 + uptr->usecs_remaining + ((1000000.0 * accum) / sim_timer_inst_per_sec ());
    }
return 0.0;
}

/* sim_gtime - return global time
   sim_grtime - return global time with rollover

   Inputs: none
   Outputs:
        time    =       global time
*/

double sim_gtime (void)
{
if (AIO_MAIN_THREAD) {
    UPDATE_SIM_TIME;
    }
return sim_time;
}

uint32 sim_grtime (void)
{
UPDATE_SIM_TIME;
return sim_rtime;
}

/* sim_qcount - return queue entry count

   Inputs: none
   Outputs:
        count   =       number of entries on the queue
*/

int32 sim_qcount (void)
{
int32 cnt;
UNIT *uptr;

cnt = 0;
for (uptr = sim_clock_queue; uptr != QUEUE_LIST_END; uptr = uptr->next)
    cnt++;
return cnt;
}

/* Breakpoint package.  This module replaces the VM-implemented one
   instruction breakpoint capability.

   Breakpoints are stored in table sim_brk_tab, which is ordered by address for
   efficient binary searching.  A breakpoint consists of a six entry structure:

        addr                    address of the breakpoint
        type                    types of breakpoints set on the address
                                a bit mask representing letters A-Z
        cnt                     number of iterations before breakp is taken
        action                  pointer command string to be executed
                                when break is taken
        next                    list of other breakpoints with the same addr specifier
        time_fired              array of when this breakpoint was fired for each class

   sim_brk_summ is a summary of the types of breakpoints that are currently set (it
   is the bitwise OR of all the type fields).  A simulator need only check for
   a breakpoint of type X if bit SWMASK('X') is set in sim_brk_summ.

   The package contains the following public routines:

        sim_brk_init            initialize
        sim_brk_set             set breakpoint
        sim_brk_clr             clear breakpoint
        sim_brk_clrall          clear all breakpoints
        sim_brk_show            show breakpoint
        sim_brk_showall         show all breakpoints
        sim_brk_test            test for breakpoint
        sim_brk_npc             PC has been changed
        sim_brk_getact          get next action
        sim_brk_clract          clear pending actions

   Initialize breakpoint system.
*/

t_stat sim_brk_init (void)
{
int32 i;

for (i=0; i<sim_brk_lnt; i++) {
    BRKTAB *bp = sim_brk_tab[i];

    while (bp) {
        BRKTAB *bpt = bp->next;

        free (bp->act);
        free (bp);
        bp = bpt;
        }
    }
memset (sim_brk_tab, 0, sim_brk_lnt*sizeof (BRKTAB*));
sim_brk_lnt = SIM_BRK_INILNT;
sim_brk_tab = (BRKTAB **) realloc (sim_brk_tab, sim_brk_lnt*sizeof (BRKTAB*));
if (sim_brk_tab == NULL)
    return SCPE_MEM;
memset (sim_brk_tab, 0, sim_brk_lnt*sizeof (BRKTAB*));
sim_brk_ent = sim_brk_ins = 0;
sim_brk_clract ();
sim_brk_npc (0);
return SCPE_OK;
}

/* Search for a breakpoint in the sorted breakpoint table */

BRKTAB *sim_brk_fnd (t_addr loc)
{
int32 lo, hi, p;
BRKTAB *bp;

if (sim_brk_ent == 0) {                                 /* table empty? */
    sim_brk_ins = 0;                                    /* insrt at head */
    return NULL;                                        /* sch fails */
    }
lo = 0;                                                 /* initial bounds */
hi = sim_brk_ent - 1;
do {
    p = (lo + hi) >> 1;                                 /* probe */
    bp = sim_brk_tab[p];                                /* table addr */
    if (loc == bp->addr) {                              /* match? */
        sim_brk_ins = p;
        return bp;
        }
    else
        if (loc < bp->addr)                             /* go down? p is upper */
            hi = p - 1;
        else
            lo = p + 1;                                 /* go up? p is lower */
    } while (lo <= hi);
if (loc < bp->addr)                                     /* insrt before or */
    sim_brk_ins = p;
else
    sim_brk_ins = p + 1;                                /* after last sch */
return NULL;
}

BRKTAB *sim_brk_fnd_ex (t_addr loc, uint32 btyp, t_bool any_typ, uint32 spc)
{
BRKTAB *bp = sim_brk_fnd (loc);

while (bp) {
    if (any_typ ? ((bp->typ & btyp) && (bp->time_fired[spc] != sim_gtime())) : 
                  (bp->typ == btyp))
        return bp;
    bp = bp->next;
    }
return bp;
}

/* Insert a breakpoint */

BRKTAB *sim_brk_new (t_addr loc, uint32 btyp)
{
int32 i, t;
BRKTAB *bp, **newp;

if (sim_brk_ins < 0)
    return NULL;
if (sim_brk_ent >= sim_brk_lnt) {                       /* out of space? */
    t = sim_brk_lnt + SIM_BRK_INILNT;                   /* new size */
    newp = (BRKTAB **) calloc (t, sizeof (BRKTAB*));    /* new table */
    if (newp == NULL)                                   /* can't extend */
        return NULL;
    memcpy (newp, sim_brk_tab, sim_brk_lnt * sizeof (*sim_brk_tab));/* copy table */
    memset (newp + sim_brk_lnt, 0, SIM_BRK_INILNT * sizeof (*newp));/* zero new entries */
    free (sim_brk_tab);                                 /* free old table */
    sim_brk_tab = newp;                                 /* new base, lnt */
    sim_brk_lnt = t;
    }
if ((sim_brk_ins == sim_brk_ent) ||
    ((sim_brk_ins != sim_brk_ent) &&
     (sim_brk_tab[sim_brk_ins]->addr != loc))) {        /* need to open a hole? */
    for (i = sim_brk_ent; i > sim_brk_ins; --i)
        sim_brk_tab[i] = sim_brk_tab[i - 1];
    sim_brk_tab[sim_brk_ins] = NULL;
    }
bp = (BRKTAB *)calloc (1, sizeof (*bp));
bp->next = sim_brk_tab[sim_brk_ins];
sim_brk_tab[sim_brk_ins] = bp;
if (bp->next == NULL)
    sim_brk_ent += 1;
bp->addr = loc;
bp->typ = btyp;
bp->cnt = 0;
bp->act = NULL;
for (i = 0; i < SIM_BKPT_N_SPC; i++)
    bp->time_fired[i] = -1.0;
return bp;
}

/* Set a breakpoint of type sw */

t_stat sim_brk_set (t_addr loc, int32 sw, int32 ncnt, CONST char *act)
{
BRKTAB *bp;

if ((sw == 0) || (sw == BRK_TYP_DYN_STEPOVER))
    sw |= sim_brk_dflt;
if (~sim_brk_types & sw) {
    char gbuf[CBUFSIZE];

    return sim_messagef (SCPE_NOFNC, "Unknown breakpoint type; %s\n", put_switches(gbuf, sizeof(gbuf), sw & ~sim_brk_types));
    }
if ((sw & BRK_TYP_DYN_ALL) && act)                      /* can't specify an action with a dynamic breakpoint */
    return SCPE_ARG;
bp = sim_brk_fnd (loc);                                 /* loc present? */
if (!bp)                                                /* no, allocate */
    bp = sim_brk_new (loc, sw);
else {
    while (bp && (bp->typ != sw))
        bp = bp->next;
    if (!bp)
        bp = sim_brk_new (loc, sw);
    }
if (!bp)                                                /* still no? mem err */
    return SCPE_MEM;
bp->cnt = ncnt;                                         /* set count */
if ((!(sw & BRK_TYP_DYN_ALL)) &&                        /* Not Dynamic and */
    (bp->act != NULL) && (act != NULL)) {               /* replace old action? */
    free (bp->act);                                     /* deallocate */
    bp->act = NULL;                                     /* now no action */
    }
if ((act != NULL) && (*act != 0)) {                     /* new action? */
    char *newp = (char *) calloc (CBUFSIZE+1, sizeof (char)); /* alloc buf */
    if (newp == NULL)                                   /* mem err? */
        return SCPE_MEM;
    strlcpy (newp, act, CBUFSIZE);                      /* copy action */
    bp->act = newp;                                     /* set pointer */
    }
sim_brk_summ = sim_brk_summ | (sw & ~BRK_TYP_TEMP);
return SCPE_OK;
}

/* Clear a breakpoint */

t_stat sim_brk_clr (t_addr loc, int32 sw)
{
BRKTAB *bpl = NULL;
BRKTAB *bp = sim_brk_fnd (loc);
int32 i;

if (!bp)                                                /* not there? ok */
    return SCPE_OK;
if (sw == 0)
    sw = SIM_BRK_ALLTYP;

while (bp) {
    if (bp->typ == (bp->typ & sw)) {
        free (bp->act);                                 /* deallocate action */
        if (bp == sim_brk_tab[sim_brk_ins])
            bpl = sim_brk_tab[sim_brk_ins] = bp->next;  /* remove from head of list */
        else
            bpl->next = bp->next;                       /* remove from middle of list */
        free (bp);
        bp = bpl;
        }
    else {
        bpl = bp;
        bp = bp->next;
        }
    }
if (sim_brk_tab[sim_brk_ins] == NULL) {                 /* erased entry */
    sim_brk_ent = sim_brk_ent - 1;                      /* decrement count */
    for (i = sim_brk_ins; i < sim_brk_ent; i++)         /* shuffle remaining entries */
        sim_brk_tab[i] = sim_brk_tab[i+1];
    }
sim_brk_summ = 0;                                       /* recalc summary */
for (i = 0; i < sim_brk_ent; i++) {
    bp = sim_brk_tab[i];
    while (bp) {
        sim_brk_summ |= (bp->typ & ~BRK_TYP_TEMP);
        bp = bp->next;
        }
    }
return SCPE_OK;
}

/* Clear all breakpoints */

t_stat sim_brk_clrall (int32 sw)
{
int32 i;

if (sw == 0)
    sw = SIM_BRK_ALLTYP;
for (i = 0; i < sim_brk_ent;) {
    t_addr loc = sim_brk_tab[i]->addr;
    sim_brk_clr (loc, sw);
    if ((i < sim_brk_ent) && 
        (loc == sim_brk_tab[i]->addr))
        ++i;
    }
return SCPE_OK;
}

/* Show a breakpoint */

t_stat sim_brk_show (FILE *st, t_addr loc, int32 sw)
{
BRKTAB *bp = sim_brk_fnd_ex (loc, sw & (~SWMASK ('C')), FALSE, 0);
DEVICE *dptr;
int32 i, any;

if ((sw == 0) || (sw == SWMASK ('C')))
    sw = SIM_BRK_ALLTYP | ((sw == SWMASK ('C')) ? SWMASK ('C') : 0);
if (!bp || (!(bp->typ & sw)))
    return SCPE_OK;
dptr = sim_dflt_dev;
if (dptr == NULL)
    return SCPE_OK;
if (sw & SWMASK ('C'))
    fprintf (st, "SET BREAK ");
else {
    if (sim_vm_fprint_addr)
        sim_vm_fprint_addr (st, dptr, loc);
    else fprint_val (st, loc, dptr->aradix, dptr->awidth, PV_LEFT);
    fprintf (st, ":\t");
    }
for (i = any = 0; i < 26; i++) {
    if ((bp->typ >> i) & 1) {
        if ((sw & SWMASK ('C')) == 0) {
            if (any)
                fprintf (st, ", ");
            fputc (i + 'A', st);
            }
        else
            fprintf (st, "-%c", i + 'A');
        any = 1;
        }
    }
if (sw & SWMASK ('C')) {
    fprintf (st, " ");
    if (sim_vm_fprint_addr)
        sim_vm_fprint_addr (st, dptr, loc);
    else fprint_val (st, loc, dptr->aradix, dptr->awidth, PV_LEFT);
    }
if (bp->cnt > 0)
    fprintf (st, "[%d]", bp->cnt);
if (bp->act != NULL)
    fprintf (st, "; %s", bp->act);
fprintf (st, "\n");
return SCPE_OK;
}

/* Show all breakpoints */

t_stat sim_brk_showall (FILE *st, int32 sw)
{
int32 bit, mask, types;
BRKTAB **bpt;

if ((sw == 0) || (sw == SWMASK ('C')))
    sw = SIM_BRK_ALLTYP | ((sw == SWMASK ('C')) ? SWMASK ('C') : 0);
for (types=bit=0; bit <= ('Z'-'A'); bit++)
    if (sim_brk_types & (1 << bit))
        ++types;
if ((!(sw & SWMASK ('C'))) && sim_brk_types && (types > 1)) {
    fprintf (st, "Supported Breakpoint Types:");
    for (bit=0; bit <= ('Z'-'A'); bit++)
        if (sim_brk_types & (1 << bit))
            fprintf (st, " -%c", 'A' + bit);
    fprintf (st, "\n");
    }
if (((sw & sim_brk_types) != sim_brk_types) && (types > 1)) {
    mask = (sw & sim_brk_types);
    fprintf (st, "Displaying Breakpoint Types:");
    for (bit=0; bit <= ('Z'-'A'); bit++)
        if (mask & (1 << bit))
            fprintf (st, " -%c", 'A' + bit);
    fprintf (st, "\n");
    }
for (bpt = sim_brk_tab; bpt < (sim_brk_tab + sim_brk_ent); bpt++) {
    BRKTAB *prev = NULL;
    BRKTAB *cur = *bpt;
    BRKTAB *next;
    /* First reverse the list */
    while (cur) {
        next = cur->next;
        cur->next = prev;
        prev = cur;
        cur = next;
        }
    /* save reversed list in the head pointer so lookups work */
    *bpt = prev;
    /* Walk the reversed list and print it in the order it was defined in */
    cur = prev;
    while (cur) {
        if (cur->typ & sw)
            sim_brk_show (st, cur->addr, cur->typ | ((sw & SWMASK ('C')) ? SWMASK ('C') : 0));
        cur = cur->next;
        }
    /* reversing the list again */
    cur = prev;
    prev = NULL;
    while (cur) {
        next = cur->next;
        cur->next = prev;
        prev = cur;
        cur = next;
        }
    /* restore original list */
    *bpt = prev;
    }
return SCPE_OK;
}

/* Test for breakpoint */

uint32 sim_brk_test (t_addr loc, uint32 btyp)
{
BRKTAB *bp;
uint32 spc = (btyp >> SIM_BKPT_V_SPC) & (SIM_BKPT_N_SPC - 1);

if (sim_brk_summ & BRK_TYP_DYN_ALL)
    btyp |= BRK_TYP_DYN_ALL;

if ((bp = sim_brk_fnd_ex (loc, btyp, TRUE, spc))) {     /* in table, and type match? */
    if (bp->time_fired[spc] == sim_time)                /* already taken?  */
        return 0;
    bp->time_fired[spc] = sim_time;                     /* remember match time */
    if (--bp->cnt > 0)                                  /* count > 0? */
        return 0;
    bp->cnt = 0;                                        /* reset count */
    sim_brk_setact (bp->act);                           /* set up actions */
    sim_brk_match_type = btyp & bp->typ;                               /* set return value */
    if (bp->typ & BRK_TYP_TEMP)
        sim_brk_clr (loc, bp->typ);                     /* delete one-shot breakpoint */
    sim_brk_match_addr = loc;
    return sim_brk_match_type;
    }
return 0;
}

/* Get next pending action, if any */

CONST char *sim_brk_getact (char *buf, int32 size)
{
char *ep;
size_t lnt;

if (sim_brk_act[sim_do_depth] == NULL)                  /* any action? */
    return NULL;
while (sim_isspace (*sim_brk_act[sim_do_depth]))        /* skip spaces */
    sim_brk_act[sim_do_depth]++;
if (*sim_brk_act[sim_do_depth] == 0) {                  /* now empty? */
    return sim_brk_clract ();
    }
ep = strpbrk (sim_brk_act[sim_do_depth], ";\"'");       /* search for a semicolon or single or double quote */
if ((ep != NULL) && (*ep != ';')) {                     /* if a quoted string is present */
    char quote = *ep++;                                 /*   then save the opening quotation mark */

    while (ep [0] != '\0' && ep [0] != quote)           /* while characters remain within the quotes */
        if (ep [0] == '\\' && ep [1] == quote)          /*   if an escaped quote sequence follows */
            ep = ep + 2;                                /*     then skip over the pair */
        else                                            /*   otherwise */
            ep = ep + 1;                                /*     skip the non-quote character */  
    ep = strchr (ep, ';');                              /* the next semicolon is outside the quotes if it exists */
    }
if (ep != NULL) {                                       /* if a semicolon is present */
    lnt = ep - sim_brk_act[sim_do_depth];               /* cmd length */
    memcpy (buf, sim_brk_act[sim_do_depth], lnt + 1);   /* copy with ; */
    buf[lnt] = 0;                                       /* erase ; */
    sim_brk_act[sim_do_depth] += lnt + 1;               /* adv ptr */
    }
else {
    strlcpy (buf, sim_brk_act[sim_do_depth], size);     /* copy action */
    sim_brk_act[sim_do_depth] = NULL;                   /* mark as digested */
    sim_brk_clract ();                                  /* no more */
    }
sim_trim_endspc (buf);
sim_debug (SIM_DBG_BRK_ACTION, sim_dflt_dev, "sim_brk_getact(%d) - Returning: '%s'\n", sim_do_depth, buf);
return buf;
}

/* Clear pending actions */

char *sim_brk_clract (void)
{
if (sim_brk_act[sim_do_depth])
    sim_debug (SIM_DBG_BRK_ACTION, sim_dflt_dev, "sim_brk_clract(%d) - Clearing: '%s'\n", sim_do_depth, sim_brk_act[sim_do_depth]);
free (sim_brk_act_buf[sim_do_depth]);
return sim_brk_act[sim_do_depth] = sim_brk_act_buf[sim_do_depth] = NULL;
}

/* Set up pending actions */

void sim_brk_setact (const char *action)
{
if (action) {
    if (sim_brk_act[sim_do_depth] && (*sim_brk_act[sim_do_depth])) {
        /* push new actions ahead of whatever is already pending */
        size_t old_size = strlen (sim_brk_act[sim_do_depth]);
        size_t new_size = strlen (action) + old_size + 3;
        char *old_action = (char *)malloc (1 + old_size);

        strlcpy (old_action, sim_brk_act[sim_do_depth], 1 + old_size);
        sim_brk_act_buf[sim_do_depth] = (char *)realloc (sim_brk_act_buf[sim_do_depth], new_size);
        strlcpy (sim_brk_act_buf[sim_do_depth], action, new_size);
        strlcat (sim_brk_act_buf[sim_do_depth], "; ", new_size);
        strlcat (sim_brk_act_buf[sim_do_depth], old_action, new_size);
        sim_debug (SIM_DBG_BRK_ACTION, sim_dflt_dev, "sim_brk_setact(%d) - Pushed: '%s' ahead of: '%s'\n", sim_do_depth, action, old_action);
        free (old_action);
        }
    else {
        sim_brk_act_buf[sim_do_depth] = (char *)realloc (sim_brk_act_buf[sim_do_depth], strlen (action) + 1);
        strcpy (sim_brk_act_buf[sim_do_depth], action);
        sim_debug (SIM_DBG_BRK_ACTION, sim_dflt_dev, "sim_brk_setact(%d) - Set to: '%s'\n", sim_do_depth, action);
        }
    sim_brk_act[sim_do_depth] = sim_brk_act_buf[sim_do_depth];
    }
else
    sim_brk_clract ();
}

char *sim_brk_replace_act (char *new_action)
{
char *old_action = sim_brk_act_buf[sim_do_depth];

sim_brk_act_buf[sim_do_depth] = new_action;
return old_action;
}

/* New PC */

void sim_brk_npc (uint32 cnt)
{
uint32 spc;
BRKTAB **bpt, *bp;

if ((cnt == 0) || (cnt > SIM_BKPT_N_SPC))
    cnt = SIM_BKPT_N_SPC;
for (bpt = sim_brk_tab; bpt < (sim_brk_tab + sim_brk_ent); bpt++) {
    for (bp = *bpt; bp; bp = bp->next) {
        for (spc = 0; spc < cnt; spc++)
            bp->time_fired[spc] = -1.0;
        }
    }
}

/* Clear breakpoint space */

void sim_brk_clrspc (uint32 spc, uint32 btyp)
{
BRKTAB **bpt, *bp;

if (spc < SIM_BKPT_N_SPC) {
    for (bpt = sim_brk_tab; bpt < (sim_brk_tab + sim_brk_ent); bpt++) {
        for (bp = *bpt; bp; bp = bp->next) {
            if (bp->typ & btyp)
                bp->time_fired[spc] = -1.0;
            }
        }
    }
}

const char *sim_brk_message(void)
{
static char msg[256];
char addr[65];
char buf[32];

msg[0] = '\0';
if (sim_vm_sprint_addr)
    sim_vm_sprint_addr (addr, sim_dflt_dev, (t_value)sim_brk_match_addr);
else sprint_val (addr, (t_value)sim_brk_match_addr, sim_dflt_dev->aradix, sim_dflt_dev->awidth, PV_LEFT);
if (sim_brk_type_desc) {
    BRKTYPTAB *brk = sim_brk_type_desc;

    while (2 == strlen (put_switches (buf, sizeof(buf), brk->btyp))) {
        if (brk->btyp == sim_brk_match_type) {
            sprintf (msg, "%s: %s", brk->desc, addr);
            break;
            }
        brk++;
        }
    }
if (!msg[0])
    sprintf (msg, "%s Breakpoint at: %s\n", put_switches (buf, sizeof(buf), sim_brk_match_type), addr);

return msg;
}

/* Expect package.  This code provides a mechanism to stop and control simulator
   execution based on traffic coming out of simulated ports and as well as a means
   to inject data into those ports.  It can conceptually viewed as a string 
   breakpoint package.

   Expect rules are stored in tables associated with each port which can use this
   facility.  An expect rule consists of a five entry structure:

        match                   the expect match string
        size                    the number of bytes in the match string
        match_pattern           the expect match string in display format
        cnt                     number of iterations before match is declared
        action                  command string to be executed when match occurs

   All active expect rules are contained in an expect match context structure.

        rules                   the match rules
        size                    the count of match rules
        buf                     the buffer of output data which has been produced
        buf_ins                 the buffer insertion point for the next output data
        buf_size                the buffer size

   The package contains the following public routines:

        sim_set_expect          expect command parser and intializer
        sim_set_noexpect        noexpect command parser
        sim_exp_init            initialize an expect context
        sim_exp_set             set or add an expect rule
        sim_exp_clr             clear or delete an expect rule
        sim_exp_clrall          clear all expect rules
        sim_exp_show            show an expect rule
        sim_exp_showall         show all expect rules
        sim_exp_check           test for rule match
*/

/*   Initialize an expect context. */

t_stat sim_exp_init (EXPECT *exp)
{
memset (exp, 0, sizeof(*exp));
return SCPE_OK;
}

/* Set expect */

t_stat sim_set_expect (EXPECT *exp, CONST char *cptr)
{
char gbuf[CBUFSIZE];
CONST char *tptr;
CONST char *c1ptr;
const char *dev_name;
uint32 after;
t_bool after_set = FALSE;
int32 cnt = 0;
t_stat r;

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_2FARG;
dev_name = tmxr_expect_line_name (exp);
after = get_default_env_parameter (dev_name, "SIM_EXPECT_HALTAFTER", 0);
if (*cptr == '[') {
    cnt = (int32) strtotv (cptr + 1, &c1ptr, 10);
    if ((cptr == c1ptr) || (*c1ptr != ']'))
        return sim_messagef (SCPE_ARG, "Invalid Repeat count specification\n");
    cptr = c1ptr + 1;
    while (sim_isspace(*cptr))
        ++cptr;
    }
tptr = get_glyph (cptr, gbuf, ',');
if ((!strncmp(gbuf, "HALTAFTER=", 10)) && (gbuf[10])) {
    after = (uint32)get_uint (&gbuf[10], 10, 100000000, &r);
    if (r != SCPE_OK)
        return sim_messagef (SCPE_ARG, "Invalid Halt After Value\n");
    cptr = tptr;
    after_set = TRUE;
    }
if ((*cptr != '\0') && (*cptr != '"') && (*cptr != '\''))
    return sim_messagef (SCPE_ARG, "String must be quote delimited\n");
cptr = get_glyph_quoted (cptr, gbuf, 0);

/* Hsndle a bare HALTAFTER=nnn command */
if ((gbuf[0] == '\0') && (*cptr == '\0') && after_set) {
    set_default_env_parameter (dev_name, "SIM_EXPECT_HALTAFTER", after);
    return SCPE_OK;
    }

return sim_exp_set (exp, gbuf, cnt, after, sim_switches, cptr);
}

/* Clear expect */

t_stat sim_set_noexpect (EXPECT *exp, const char *cptr)
{
char gbuf[CBUFSIZE];

if (!cptr || !*cptr)
    return sim_exp_clrall (exp);                    /* clear all rules */
if ((*cptr != '"') && (*cptr != '\''))
    return sim_messagef (SCPE_ARG, "String must be quote delimited\n");
cptr = get_glyph_quoted (cptr, gbuf, 0);
if (*cptr != '\0')
    return SCPE_2MARG;                              /* No more arguments */
return sim_exp_clr (exp, gbuf);                     /* clear one rule */
}

/* Search for an expect rule in an expect context */

CONST EXPTAB *sim_exp_fnd (CONST EXPECT *exp, const char *match, int32 start_rule)
{
int32 i;

if (!exp->rules)
    return NULL;
for (i=start_rule; i<exp->size; i++)
    if (!strcmp (exp->rules[i].match_pattern, match))
        return &exp->rules[i];
return NULL;
}

/* Clear (delete) an expect rule */

t_stat sim_exp_clr_tab (EXPECT *exp, EXPTAB *ep)
{
int32 i;

if (!ep)                                                /* not there? ok */
    return SCPE_OK;
free (ep->match);                                       /* deallocate match string */
free (ep->match_pattern);                               /* deallocate the display format match string */
free (ep->act);                                         /* deallocate action */
#if defined(USE_REGEX)
if (ep->switches & EXP_TYP_REGEX)
    regfree (&ep->regex);                               /* release compiled regex */
#endif
exp->size -= 1;                                         /* decrement count */
for (i=ep-exp->rules; i<exp->size; i++)                 /* shuffle up remaining rules */
    exp->rules[i] = exp->rules[i+1];
if (exp->size == 0) {                                   /* No rules left? */
    free (exp->rules);
    exp->rules = NULL;
    }
return SCPE_OK;
}

t_stat sim_exp_clr (EXPECT *exp, const char *match)
{
EXPTAB *ep = (EXPTAB *)sim_exp_fnd (exp, match, 0);

while (ep) {
    sim_exp_clr_tab (exp, ep);
    ep = (EXPTAB *)sim_exp_fnd (exp, match, ep - exp->rules);
    }
return SCPE_OK;
}

/* Clear all expect rules */

t_stat sim_exp_clrall (EXPECT *exp)
{
int32 i;

for (i=0; i<exp->size; i++) {
    free (exp->rules[i].match);                         /* deallocate match string */
    free (exp->rules[i].match_pattern);                 /* deallocate display format match string */
    free (exp->rules[i].act);                           /* deallocate action */
    }
free (exp->rules);
exp->rules = NULL;
exp->size = 0;
free (exp->buf);
exp->buf = NULL;
exp->buf_size = 0;
exp->buf_data = exp->buf_ins = 0;
return SCPE_OK;
}

/* Set/Add an expect rule */

t_stat sim_exp_set (EXPECT *exp, const char *match, int32 cnt, uint32 after, int32 switches, const char *act)
{
EXPTAB *ep;
uint8 *match_buf;
uint32 match_size;
int i;

/* Validate the match string */
match_buf = (uint8 *)calloc (strlen (match) + 1, 1);
if (!match_buf)
    return SCPE_MEM;
if (switches & EXP_TYP_REGEX) {
#if !defined (USE_REGEX)
    free (match_buf);
    return sim_messagef (SCPE_ARG, "RegEx support not available\n");
    }
#else   /* USE_REGEX */
    int res;
    regex_t re;

    memset (&re, 0, sizeof(re));
    memcpy (match_buf, match+1, strlen(match)-2);       /* extract string without surrounding quotes */
    match_buf[strlen(match)-2] = '\0';
    res = regcomp (&re, (char *)match_buf, REG_EXTENDED | ((switches & EXP_TYP_REGEX_I) ? REG_ICASE : 0));
    if (res) {
        size_t err_size = regerror (res, &re, NULL, 0);
        char *err_buf = (char *)calloc (err_size+1, 1);

        regerror (res, &re, err_buf, err_size);
        sim_messagef (SCPE_ARG, "Regular Expression Error: %s\n", err_buf);
        free (err_buf);
        free (match_buf);
        return SCPE_ARG|SCPE_NOMESSAGE;
        }
    sim_debug (exp->dbit, exp->dptr, "Expect Regular Expression: \"%s\" has %d sub expressions\n", match_buf, (int)re.re_nsub);
    regfree (&re);
    }
#endif
else {
    if (switches & EXP_TYP_REGEX_I) {
        free (match_buf);
        return sim_messagef (SCPE_ARG, "Case independed matching is only valid for RegEx expect rules\n");
        }
    sim_data_trace(exp->dptr, exp->dptr->units, (const uint8 *)match, "", strlen(match)+1, "Expect Match String", exp->dbit);
    if (SCPE_OK != sim_decode_quoted_string (match, match_buf, &match_size)) {
        free (match_buf);
        return sim_messagef (SCPE_ARG, "Invalid quoted string\n");
        }
    }
free (match_buf);
for (i=0; i<exp->size; i++) {                           /* Make sure this rule won't be occluded */
    if ((0 == strcmp (match, exp->rules[i].match_pattern)) &&
        (exp->rules[i].switches & EXP_TYP_PERSIST))
        return sim_messagef (SCPE_ARG, "Persistent Expect rule with identical match string already exists\n");
    }
if (after && exp->size)
    return sim_messagef (SCPE_ARG, "Multiple concurrent EXPECT rules aren't valid when a HALTAFTER parameter is non-zero\n");
exp->rules = (EXPTAB *) realloc (exp->rules, sizeof (*exp->rules)*(exp->size + 1));
ep = &exp->rules[exp->size];
exp->size += 1;
memset (ep, 0, sizeof(*ep));
ep->after = after;                                     /* set halt after value */
ep->match_pattern = (char *)malloc (strlen (match) + 1);
if (ep->match_pattern)
    strcpy (ep->match_pattern, match);
ep->cnt = cnt;                                          /* set proceed count */
ep->switches = switches;                                /* set switches */
match_buf = (uint8 *)calloc (strlen (match) + 1, 1);
if ((match_buf == NULL) || (ep->match_pattern == NULL)) {
    sim_exp_clr_tab (exp, ep);                          /* clear it */
    free (match_buf);
    return SCPE_MEM;
    }
if (switches & EXP_TYP_REGEX) {
#if defined(USE_REGEX)
    memcpy (match_buf, match+1, strlen(match)-2);      /* extract string without surrounding quotes */
    match_buf[strlen(match)-2] = '\0';
    regcomp (&ep->regex, (char *)match_buf, REG_EXTENDED);
#endif
    free (match_buf);
    match_buf = NULL;
    }
else {
    sim_data_trace(exp->dptr, exp->dptr->units, (const uint8 *)match, "", strlen(match)+1, "Expect Match String", exp->dbit);
    /* quoted string was validated above, this decode operation will always succeed */
    (void)sim_decode_quoted_string (match, match_buf, &match_size);
    ep->match = match_buf;
    ep->size = match_size;
    }
ep->match_pattern = (char *)malloc (strlen (match) + 1);
strcpy (ep->match_pattern, match);
if (ep->act) {                                          /* replace old action? */
    free (ep->act);                                     /* deallocate */
    ep->act = NULL;                                     /* now no action */
    }
if (act)
    while (sim_isspace(*act)) ++act;                    /* skip leading spaces in action string */
if ((act != NULL) && (*act != 0)) {                     /* new action? */
    char *newp;

    if ((act > sim_sub_instr_buf) && ((size_t)(act - sim_sub_instr_buf) < sim_sub_instr_size))
        act = &sim_sub_instr[sim_sub_instr_off[act - sim_sub_instr_buf]]; /* get un-substituted string */
    newp = (char *) calloc (strlen (act)+1, sizeof (*act)); /* alloc buf */
    if (newp == NULL)                                   /* mem err? */
        return SCPE_MEM;
    strcpy (newp, act);                                 /* copy action */
    ep->act = newp;                                     /* set pointer */
    }
/* Make sure that the production buffer is large enough to detect a match for all rules including a NUL termination byte */
for (i=0; i<exp->size; i++) {
    uint32 compare_size = (exp->rules[i].switches & EXP_TYP_REGEX) ? MAX(10 * strlen(ep->match_pattern), 1024) : exp->rules[i].size;
    if (compare_size >= exp->buf_size) {
        exp->buf = (uint8 *)realloc (exp->buf, compare_size + 2); /* Extra byte to null terminate regex compares */
        exp->buf_size = compare_size + 1;
        }
    }
return SCPE_OK;
}

/* Show an expect rule */

t_stat sim_exp_show_tab (FILE *st, const EXPECT *exp, const EXPTAB *ep)
{
const char *dev_name = tmxr_expect_line_name (exp);
uint32 default_haltafter = get_default_env_parameter (dev_name, "SIM_EXPECT_HALTAFTER", 0);

if (!ep)
    return SCPE_OK;
fprintf (st, "    EXPECT");
if (ep->switches & EXP_TYP_PERSIST)
    fprintf (st, " -p");
if (ep->switches & EXP_TYP_CLEARALL)
    fprintf (st, " -c");
if (ep->switches & EXP_TYP_REGEX)
    fprintf (st, " -r");
if (ep->switches & EXP_TYP_REGEX_I)
    fprintf (st, " -i");
if (ep->after != default_haltafter)
    fprintf (st, " HALTAFTER=%d", (int)ep->after);
fprintf (st, " %s", ep->match_pattern);
if (ep->cnt > 0)
    fprintf (st, " [%d]", ep->cnt);
if (ep->act)
    fprintf (st, " %s", ep->act);
fprintf (st, "\n");
return SCPE_OK;
}

t_stat sim_exp_show (FILE *st, CONST EXPECT *exp, const char *match)
{
CONST EXPTAB *ep = (CONST EXPTAB *)sim_exp_fnd (exp, match, 0);
const char *dev_name = tmxr_expect_line_name (exp);
uint32 default_haltafter = get_default_env_parameter (dev_name, "SIM_EXPECT_HALTAFTER", 0);

if (exp->buf_size) {
    char *bstr = sim_encode_quoted_string (exp->buf, exp->buf_ins);

    fprintf (st, "  Match Buffer Size: %d\n", exp->buf_size);
    fprintf (st, "  Buffer Insert Offset: %d\n", exp->buf_ins);
    fprintf (st, "  Buffer Contents: %s\n", bstr);
    if (default_haltafter)
        fprintf (st, "  Default HaltAfter: %u instructions\n", (unsigned)default_haltafter);
    free (bstr);
    }
if (exp->dptr && (exp->dbit & exp->dptr->dctrl))
    fprintf (st, "  Expect Debugging via: SET %s DEBUG%s%s\n", sim_dname(exp->dptr), exp->dptr->debflags ? "=" : "", exp->dptr->debflags ? get_dbg_verb (exp->dbit, exp->dptr, NULL) : "");
fprintf (st, "  Match Rules:\n");
if (!*match)
    return sim_exp_showall (st, exp);
if (!ep) {
    fprintf (st, "  No Rules match '%s'\n", match);
    return SCPE_ARG;
    }
do {
    sim_exp_show_tab (st, exp, ep);
    ep = (CONST EXPTAB *)sim_exp_fnd (exp, match, 1 + (ep - exp->rules));
    } while (ep);
return SCPE_OK;
}

/* Show all expect rules */

t_stat sim_exp_showall (FILE *st, const EXPECT *exp)
{
int32 i;

for (i=0; i < exp->size; i++)
    sim_exp_show_tab (st, exp, &exp->rules[i]);
return SCPE_OK;
}

/* Test for expect match */

t_stat sim_exp_check (EXPECT *exp, uint8 data)
{
int32 i;
EXPTAB *ep;
int regex_checks = 0;
char *tstr = NULL;

if ((!exp) || (!exp->rules))                            /* Anying to check? */
    return SCPE_OK;

exp->buf[exp->buf_ins++] = data;                        /* Save new data */
exp->buf[exp->buf_ins] = '\0';                          /* Nul terminate for RegEx match */
if (exp->buf_data < exp->buf_size)
    ++exp->buf_data;                                    /* Record amount of data in buffer */

for (i=0; i < exp->size; i++) {
    ep = &exp->rules[i];
    if (ep->switches & EXP_TYP_REGEX) {
#if defined (USE_REGEX)
        regmatch_t *matches;
        char *cbuf = (char *)exp->buf;
        static size_t sim_exp_match_sub_count = 0;

        if (tstr)
            cbuf = tstr;
        else {
            if (strlen ((char *)exp->buf) != exp->buf_ins) { /* Nul characters in buffer? */
                size_t off;

                tstr = (char *)malloc (exp->buf_ins + 1);
                tstr[0] = '\0';
                for (off=0; off < exp->buf_ins; off += 1 + strlen ((char *)&exp->buf[off]))
                    strcpy (&tstr[strlen (tstr)], (char *)&exp->buf[off]);
                cbuf = tstr;
                }
            }
        ++regex_checks;
        matches = (regmatch_t *)calloc ((ep->regex.re_nsub + 1), sizeof(*matches));
        if (sim_deb && exp->dptr && (exp->dptr->dctrl & exp->dbit)) {
            char *estr = sim_encode_quoted_string (exp->buf, exp->buf_ins);
            sim_debug (exp->dbit, exp->dptr, "Checking String: %s\n", estr);
            sim_debug (exp->dbit, exp->dptr, "Against RegEx Match Rule: %s\n", ep->match_pattern);
            free (estr);
            }
        if (!regexec (&ep->regex, cbuf, ep->regex.re_nsub + 1, matches, REG_NOTBOL)) {
            size_t j;
            char *buf = (char *)malloc (1 + exp->buf_ins);

            for (j=0; j<ep->regex.re_nsub + 1; j++) {
                char env_name[32];

                sprintf (env_name, "_EXPECT_MATCH_GROUP_%d", (int)j);
                memcpy (buf, &cbuf[matches[j].rm_so], matches[j].rm_eo-matches[j].rm_so);
                buf[matches[j].rm_eo-matches[j].rm_so] = '\0';
                setenv (env_name, buf, 1);      /* Make the match and substrings available as environment variables */
                sim_debug (exp->dbit, exp->dptr, "%s=%s\n", env_name, buf);
                }
            for (; j<sim_exp_match_sub_count; j++) {
                char env_name[32];

                sprintf (env_name, "_EXPECT_MATCH_GROUP_%d", (int)j);
                setenv (env_name, "", 1);      /* Remove previous extra environment variables */
                }
            sim_exp_match_sub_count = ep->regex.re_nsub;
            free (matches);
            free (buf);
            break;
            }
        free (matches);
#endif
        }
    else {
        if (exp->buf_data < ep->size)                           /* Too little data to match yet? */
            continue;                                           /* Yes, Try next one. */
        if (exp->buf_ins < ep->size) {                          /* Match might stradle end of buffer */
            /* 
             * First compare the newly deposited data at the beginning 
             * of buffer with the end of the match string
             */
            if (exp->buf_ins) {
                if (sim_deb && exp->dptr && (exp->dptr->dctrl & exp->dbit)) {
                    char *estr = sim_encode_quoted_string (exp->buf, exp->buf_ins);
                    char *mstr = sim_encode_quoted_string (&ep->match[ep->size-exp->buf_ins], exp->buf_ins);

                    sim_debug (exp->dbit, exp->dptr, "Checking String[0:%d]: %s\n", exp->buf_ins, estr);
                    sim_debug (exp->dbit, exp->dptr, "Against Match Data: %s\n", mstr);
                    free (estr);
                    free (mstr);
                    }
                if (memcmp (exp->buf, &ep->match[ep->size-exp->buf_ins], exp->buf_ins)) /* Tail Match? */
                    continue;                                   /* Nope, Try next one. */
                }
            if (sim_deb && exp->dptr && (exp->dptr->dctrl & exp->dbit)) {
                char *estr = sim_encode_quoted_string (&exp->buf[exp->buf_size-(ep->size-exp->buf_ins)], ep->size-exp->buf_ins);
                char *mstr = sim_encode_quoted_string (ep->match, ep->size-exp->buf_ins);

                sim_debug (exp->dbit, exp->dptr, "Checking String[%d:%d]: %s\n", exp->buf_size-(ep->size-exp->buf_ins), ep->size-exp->buf_ins, estr);
                sim_debug (exp->dbit, exp->dptr, "Against Match Data: %s\n", mstr);
                free (estr);
                free (mstr);
                }
            if (memcmp (&exp->buf[exp->buf_size-(ep->size-exp->buf_ins)], ep->match, ep->size-exp->buf_ins)) /* Front Match? */
                continue;                                       /* Nope, Try next one. */
            break;
            }
        else {
            if (sim_deb && exp->dptr && (exp->dptr->dctrl & exp->dbit)) {
                char *estr = sim_encode_quoted_string (&exp->buf[exp->buf_ins-ep->size], ep->size);
                char *mstr = sim_encode_quoted_string (ep->match, ep->size);

                sim_debug (exp->dbit, exp->dptr, "Checking String[%d:%d]: %s\n", exp->buf_ins-ep->size, ep->size, estr);
                sim_debug (exp->dbit, exp->dptr, "Against Match Data: %s\n", mstr);
                free (estr);
                free (mstr);
                }
            if (memcmp (&exp->buf[exp->buf_ins-ep->size], ep->match, ep->size)) /* Whole string match? */
                continue;                                       /* Nope, Try next one. */
            break;
            }
        }
    }
if (exp->buf_ins == exp->buf_size) {                    /* At end of match buffer? */
    if (regex_checks) {
        /* When processing regular expressions, let the match buffer fill 
           up and then shuffle the buffer contents down by half the buffer size
           so that the regular expression has a single contiguous buffer to 
           match against instead of the wrapping buffer paradigm which is 
           used when no regular expression rules are in effect */
        memmove (exp->buf, &exp->buf[exp->buf_size/2], exp->buf_size-(exp->buf_size/2));
        exp->buf_ins -= exp->buf_size/2;
        exp->buf_data = exp->buf_ins;
        sim_debug (exp->dbit, exp->dptr, "Buffer Full - sliding the last %d bytes to start of buffer new insert at: %d\n", (exp->buf_size/2), exp->buf_ins);
        }
    else {
        exp->buf_ins = 0;                               /* wrap around to beginning */
        sim_debug (exp->dbit, exp->dptr, "Buffer wrapping\n");
        }
    }
if (i != exp->size) {                                   /* Found? */
    sim_debug (exp->dbit, exp->dptr, "Matched expect pattern: %s\n", ep->match_pattern);
    setenv ("_EXPECT_MATCH_PATTERN", ep->match_pattern, 1);   /* Make the match detail available as an environment variable */
    if (ep->cnt > 0) {
        ep->cnt -= 1;
        sim_debug (exp->dbit, exp->dptr, "Waiting for %d more match%s before stopping\n", 
                                         ep->cnt, (ep->cnt == 1) ? "" : "es");
        }
    else {
        uint32 after = ep->after;
        int32 switches = ep->switches;

        if (ep->act && *ep->act) {
            sim_debug (exp->dbit, exp->dptr, "Initiating actions: %s\n", ep->act);
            }
        else {
            sim_debug (exp->dbit, exp->dptr, "No actions specified, stopping...\n");
            }
        sim_brk_setact (ep->act);                       /* set up actions */
        if (ep->switches & EXP_TYP_CLEARALL)            /* Clear-all expect rule? */
            sim_exp_clrall (exp);                       /* delete all rules */
        else {
            if (!(ep->switches & EXP_TYP_PERSIST))      /* One shot expect rule? */
                sim_exp_clr_tab (exp, ep);              /* delete it */
            }
        sim_activate (&sim_expect_unit,                 /* schedule simulation stop when indicated */
                      (switches & EXP_TYP_TIME) ?
                            (int32)((sim_timer_inst_per_sec ()*after)/1000000.0) : 
                            after);
        }
    /* Matched data is no longer available for future matching */
    exp->buf_data = exp->buf_ins = 0;
    }
free (tstr);
return SCPE_OK;
}

/* Queue input data for sending */

t_stat sim_send_input (SEND *snd, uint8 *data, size_t size, uint32 after, uint32 delay)
{
if (snd->extoff != 0) {
    if (snd->insoff-snd->extoff > 0)
        memmove(snd->buffer, snd->buffer+snd->extoff, snd->insoff-snd->extoff);
    snd->insoff -= snd->extoff;
    snd->extoff -= snd->extoff;
    }
if (snd->insoff+size > snd->bufsize) {
    snd->bufsize = snd->insoff+size;
    snd->buffer = (uint8 *)realloc(snd->buffer, snd->bufsize);
    }
memcpy(snd->buffer+snd->insoff, data, size);
snd->insoff += size;
snd->delay = (sim_switches & SWMASK ('T')) ? (uint32)((sim_timer_inst_per_sec()*delay)/1000000.0) : delay;
snd->after = (sim_switches & SWMASK ('T')) ? (uint32)((sim_timer_inst_per_sec()*after)/1000000.0) : after;
snd->next_time = sim_gtime() + snd->after;
return SCPE_OK;
}

/* Cancel Queued input data */
t_stat sim_send_clear (SEND *snd)
{
snd->insoff = 0;
snd->extoff = 0;
return SCPE_OK;
}

/* Display console Queued input data status */

t_stat sim_show_send_input (FILE *st, const SEND *snd)
{
const char *dev_name = tmxr_send_line_name (snd);
uint32 delay = get_default_env_parameter (dev_name, "SIM_SEND_DELAY", SEND_DEFAULT_DELAY);
uint32 after = get_default_env_parameter (dev_name, "SIM_SEND_AFTER", delay);

fprintf (st, "%s\n", tmxr_send_line_name (snd));
if (snd->extoff < snd->insoff) {
    fprintf (st, "  %d bytes of pending input Data:\n    ", snd->insoff-snd->extoff);
    fprint_buffer_string (st, snd->buffer+snd->extoff, snd->insoff-snd->extoff);
    fprintf (st, "\n");
    }
else
    fprintf (st, "  No Pending Input Data\n");
if ((snd->next_time - sim_gtime()) > 0) {
    if (((snd->next_time - sim_gtime()) > (sim_timer_inst_per_sec()/1000000.0)) && ((sim_timer_inst_per_sec()/1000000.0) > 0.0))
        fprintf (st, "  Minimum of %d instructions (%d microseconds) before sending first character\n", (int)(snd->next_time - sim_gtime()),
                                                        (int)((snd->next_time - sim_gtime())/(sim_timer_inst_per_sec()/1000000.0)));
    else
        fprintf (st, "  Minimum of %d instructions before sending first character\n", (int)(snd->next_time - sim_gtime()));
    }
if ((snd->delay > (sim_timer_inst_per_sec()/1000000.0)) && ((sim_timer_inst_per_sec()/1000000.0) > 0.0))
    fprintf (st, "  Minimum of %d instructions (%d microseconds) between characters\n", (int)snd->delay, (int)(snd->delay/(sim_timer_inst_per_sec()/1000000.0)));
else
    fprintf (st, "  Minimum of %d instructions between characters\n", (int)snd->delay);
if (after)
    fprintf (st, "  Default delay before first character input is %u instructions\n", after);
if (delay)
    fprintf (st, "  Default delay between character input is %u instructions\n", after);
if (snd->dptr && (snd->dbit & snd->dptr->dctrl))
    fprintf (st, "  Send Debugging via: SET %s DEBUG%s%s\n", sim_dname(snd->dptr), snd->dptr->debflags ? "=" : "", snd->dptr->debflags ? get_dbg_verb (snd->dbit, snd->dptr, NULL) : "");
return SCPE_OK;
}

/* Poll for Queued input data */

t_bool sim_send_poll_data (SEND *snd, t_stat *stat)
{
if (snd && (snd->extoff < snd->insoff)) {               /* pending input characters available? */
    if (sim_gtime() < snd->next_time) {                 /* too soon? */
        *stat = SCPE_OK;
        sim_debug (snd->dbit, snd->dptr, "Too soon to inject next byte\n");
        }
    else {
        char dstr[8] = "";

        *stat = snd->buffer[snd->extoff++] | SCPE_KFLAG;/* get one */
        snd->next_time = sim_gtime() + snd->delay;
        if (sim_isgraph(*stat & 0xFF) || ((*stat & 0xFF) == ' '))
            sprintf (dstr, " '%c'", *stat & 0xFF);
        sim_debug (snd->dbit, snd->dptr, "Byte value: 0x%02X%s injected\n", *stat & 0xFF, dstr);
        }
    return TRUE;
    }
return FALSE;
}


/* Message Text */

const char *sim_error_text (t_stat stat)
{
static char msgbuf[64];

stat &= ~(SCPE_KFLAG|SCPE_BREAK|SCPE_NOMESSAGE);        /* remove any flags */
if (stat == SCPE_OK)
    return "No Error";
if ((stat >= SCPE_BASE) && (stat <= SCPE_MAX_ERR))
    return scp_errors[stat-SCPE_BASE].message;
sprintf(msgbuf, "Error %d", stat);
return msgbuf;
}

t_stat sim_string_to_stat (const char *cptr, t_stat *stat)
{
char gbuf[CBUFSIZE];
int32 cond;

*stat = SCPE_ARG;
cptr = get_glyph (cptr, gbuf, 0);
if (0 == memcmp("SCPE_", gbuf, 5))
    memmove (gbuf, gbuf+5, 1 + strlen (gbuf+5));/* skip leading SCPE_ */
for (cond=0; cond <= (SCPE_MAX_ERR-SCPE_BASE); cond++)
    if (0 == strcmp(scp_errors[cond].code, gbuf)) {
        cond += SCPE_BASE;
        break;
        }
if (0 == strcmp(gbuf, "OK"))
    cond = SCPE_OK;
if (cond == (1+SCPE_MAX_ERR-SCPE_BASE)) {       /* not found? */
    if (0 == (cond = strtol(gbuf, NULL, 0)))  /* try explicit number */
        return SCPE_ARG;
    }
*stat = cond;
if (cond > SCPE_MAX_ERR)
    return SCPE_ARG;
return SCPE_OK;    
}

/* Debug printout routines, from Dave Hittner */

const char* debug_bstates = "01_^";
AIO_TLS char debug_line_prefix[256];
int32 debug_unterm  = 0;

/* Finds debug phrase matching bitmask from from device DEBTAB table */

static const char *get_dbg_verb (uint32 dbits, DEVICE* dptr, UNIT *uptr)
{
static const char *debtab_none    = "DEBTAB_ISNULL";
static const char *debtab_nomatch = "DEBTAB_NOMATCH";
const char *some_match = NULL;
int32 offset = 0;

if (dptr->debflags == 0)
    return debtab_none;

dbits &= (dptr->dctrl | (uptr ? uptr->dctrl : 0));/* Look for just the bits that matched */

/* Find matching words for bitmask */

while (dptr->debflags[offset].name && (offset < 32)) {
    if (dptr->debflags[offset].mask == dbits)   /* All Bits Match */
        return dptr->debflags[offset].name;
    if (dptr->debflags[offset].mask & dbits)
        some_match = dptr->debflags[offset].name;
    offset++;
    }
return some_match ? some_match : debtab_nomatch;
}

/* Prints standard debug prefix unless previous call unterminated */

static const char *sim_debug_prefix (uint32 dbits, DEVICE* dptr, UNIT* uptr)
{
const char* debug_type = get_dbg_verb (dbits, dptr, uptr);
char tim_t[32] = "";
char tim_a[32] = "";
char pc_s[64] = "";
struct timespec time_now;

if (sim_deb_switches & (SWMASK ('T') | SWMASK ('R') | SWMASK ('A'))) {
    clock_gettime(CLOCK_REALTIME, &time_now);
    if (sim_deb_switches & SWMASK ('R'))
        sim_timespec_diff (&time_now, &time_now, &sim_deb_basetime);
    if (sim_deb_switches & SWMASK ('T')) {
        time_t tnow = (time_t)time_now.tv_sec;
        struct tm *now = localtime(&tnow);

        sprintf(tim_t, "%02d:%02d:%02d.%03d ", now->tm_hour, now->tm_min, now->tm_sec, (int)(time_now.tv_nsec/1000000));
        }
    if (sim_deb_switches & SWMASK ('A')) {
        sprintf(tim_t, "%" LL_FMT "d.%03d ", (LL_TYPE)(time_now.tv_sec), (int)(time_now.tv_nsec/1000000));
        }
    }
if (sim_deb_switches & SWMASK ('P')) {
    t_value val;
    
    /* Some simulators expose the PC as a register, some don't expose it or expose a register 
       which is not a variable which is updated during instruction execution (i.e. only upon
       exit of sim_instr()).  For the -P debug option to be effective, such a simulator should
       provide a routine which returns the value of the current PC and set the sim_vm_pc_value
       routine pointer to that routine.
     */
    if (sim_vm_pc_value)
        val = (*sim_vm_pc_value)();
    else
        val = get_rval (sim_PC, 0);
    sprintf(pc_s, "-%s:", sim_PC->name);
    sprint_val (&pc_s[strlen(pc_s)], val, sim_PC->radix, sim_PC->width, sim_PC->flags & REG_FMT);
    }
sprintf(debug_line_prefix, "DBG(%s%s%.0f%s)%s> %s %s: ", tim_t, tim_a, sim_gtime(), pc_s, AIO_MAIN_THREAD ? "" : "+", dptr->name, debug_type);
return debug_line_prefix;
}

void fprint_fields (FILE *stream, t_value before, t_value after, BITFIELD* bitdefs)
{
int32 i, fields, offset;
uint32 value, beforevalue, mask;

for (fields=offset=0; bitdefs[fields].name; ++fields) {
    if (bitdefs[fields].offset == 0xffffffff)       /* fixup uninitialized offsets */
        bitdefs[fields].offset = offset;
    offset += bitdefs[fields].width;
    }
for (i = fields-1; i >= 0; i--) {                   /* print xlation, transition */
    if (bitdefs[i].name[0] == '\0')
        continue;
    if ((bitdefs[i].width == 1) && (bitdefs[i].valuenames == NULL)) {
        int off = ((after >> bitdefs[i].offset) & 1) + (((before ^ after) >> bitdefs[i].offset) & 1) * 2;
        fprintf(stream, "%s%c ", bitdefs[i].name, debug_bstates[off]);
        }
    else {
        const char *delta = "";

        mask = 0xFFFFFFFF >> (32-bitdefs[i].width);
        value = (uint32)((after >> bitdefs[i].offset) & mask);
        beforevalue = (uint32)((before >> bitdefs[i].offset) & mask);
        if (value < beforevalue)
            delta = "_";
        if (value > beforevalue)
            delta = "^";
        if (bitdefs[i].valuenames)
            fprintf(stream, "%s=%s%s ", bitdefs[i].name, delta, bitdefs[i].valuenames[value]);
        else
            if (bitdefs[i].format) {
                fprintf(stream, "%s=%s", bitdefs[i].name, delta);
                fprintf(stream, bitdefs[i].format, value);
                fprintf(stream, " ");
                }
            else
                fprintf(stream, "%s=%s0x%X ", bitdefs[i].name, delta, value);
        }
    }
}

/* Prints state of a register: bit translation + state (0,1,_,^)
   indicating the state and transition of the bit and bitfields. States:
   0=steady(0->0), 1=steady(1->1), _=falling(1->0), ^=rising(0->1) */

void sim_debug_bits_hdr(uint32 dbits, DEVICE* dptr, const char *header, 
    BITFIELD* bitdefs, uint32 before, uint32 after, int terminate)
{
if (sim_deb && dptr && (dptr->dctrl & dbits)) {
    TMLN *saved_oline = sim_oline;

    sim_oline = NULL;                                                   /* avoid potential debug to active socket */
    if (!debug_unterm)
        fprintf(sim_deb, "%s", sim_debug_prefix(dbits, dptr, NULL));    /* print prefix if required */
    if (header)
        fprintf(sim_deb, "%s: ", header);
    fprint_fields (sim_deb, (t_value)before, (t_value)after, bitdefs);  /* print xlation, transition */
    if (terminate)
        fprintf(sim_deb, "\r\n");
    debug_unterm = terminate ? 0 : 1;                                   /* set unterm for next */
    sim_oline = saved_oline;                                            /* restore original socket */
    }
}
void sim_debug_bits(uint32 dbits, DEVICE* dptr, BITFIELD* bitdefs,
    uint32 before, uint32 after, int terminate)
{
sim_debug_bits_hdr(dbits, dptr, NULL, bitdefs, before, after, terminate);
}

/* Print message to stdout, sim_log (if enabled) and sim_deb (if enabled) */
void sim_printf (const char* fmt, ...)
{
char stackbuf[STACKBUFSIZE];
int32 bufsize = sizeof(stackbuf);
char *buf = stackbuf;
int32 len;
va_list arglist;

while (1) {                                         /* format passed string, args */
    va_start (arglist, fmt);
#if defined(NO_vsnprintf)
    len = vsprintf (buf, fmt, arglist);
#else                                               /* !defined(NO_vsnprintf) */
    len = vsnprintf (buf, bufsize-1, fmt, arglist);
#endif                                              /* NO_vsnprintf */
    va_end (arglist);

/* If the formatted result didn't fit into the buffer, then grow the buffer and try again */

    if ((len < 0) || (len >= bufsize-1)) {
        if (buf != stackbuf)
            free (buf);
        bufsize = bufsize * 2;
        if (bufsize < len + 2)
            bufsize = len + 2;
        buf = (char *) malloc (bufsize);
        if (buf == NULL)                            /* out of memory */
            return;
        buf[bufsize-1] = '\0';
        continue;
        }
    break;
    }

if (sim_is_running) {
    char *c, *remnant = buf;

    while ((c = strchr (remnant, '\n'))) {
        if ((c != buf) && (*(c - 1) != '\r'))
            fprintf (stdout, "%.*s\r\n", (int)(c-remnant), remnant);
        else
            fprintf (stdout, "%.*s\n", (int)(c-remnant), remnant);
        remnant = c + 1;
        }
    fprintf (stdout, "%s", remnant);
    }
else
    fprintf (stdout, "%s", buf);
if ((!sim_oline) && (sim_log && (sim_log != stdout)))
    fprintf (sim_log, "%s", buf);
if (sim_deb && (sim_deb != stdout) && (sim_deb != sim_log))
    fwrite (buf, 1, strlen (buf), sim_deb);

if (buf != stackbuf)
    free (buf);
}

void sim_perror (const char *msg)
{
int saved_errno = errno;

perror (msg);
sim_printf ("%s: %s\n", msg, strerror (saved_errno));
}

/* Print command result message to stdout, sim_log (if enabled) and sim_deb (if enabled) */
t_stat sim_messagef (t_stat stat, const char* fmt, ...)
{
char stackbuf[STACKBUFSIZE];
int32 bufsize = sizeof(stackbuf);
char *buf = stackbuf;
int32 len;
va_list arglist;
t_bool inhibit_message = (!sim_show_message || (stat & SCPE_NOMESSAGE));

if ((stat == SCPE_OK) && (sim_quiet || (sim_switches & SWMASK ('Q'))))
    return stat;
while (1) {                                         /* format passed string, args */
    va_start (arglist, fmt);
#if defined(NO_vsnprintf)
    len = vsprintf (buf, fmt, arglist);
#else                                               /* !defined(NO_vsnprintf) */
    len = vsnprintf (buf, bufsize-1, fmt, arglist);
#endif                                              /* NO_vsnprintf */
    va_end (arglist);

/* If the formatted result didn't fit into the buffer, then grow the buffer and try again */

    if ((len < 0) || (len >= bufsize-1)) {
        if (buf != stackbuf)
            free (buf);
        bufsize = bufsize * 2;
        if (bufsize < len + 2)
            bufsize = len + 2;
        buf = (char *) malloc (bufsize);
        if (buf == NULL)                            /* out of memory */
            return SCPE_MEM;
        buf[bufsize-1] = '\0';
        continue;
        }
    break;
    }

if (sim_do_ocptr[sim_do_depth]) {
    if (!sim_do_echo && !inhibit_message && !sim_cmd_echoed) {
        sim_printf("%s> %s\n", do_position(), sim_do_ocptr[sim_do_depth]);
        sim_cmd_echoed = TRUE;
        }
    else {
        if (sim_deb) {                      /* Always put context in debug output */
            TMLN *saved_oline = sim_oline;

            sim_oline = NULL;               /* avoid potential debug to active socket */
            fprintf (sim_deb, "%s> %s\n", do_position(), sim_do_ocptr[sim_do_depth]);
            sim_oline = saved_oline;        /* restore original socket */
            }
        }
    }
if (sim_is_running && !inhibit_message) {
    char *c, *remnant = buf;

    while ((c = strchr(remnant, '\n'))) {
        if ((c != buf) && (*(c - 1) != '\r'))
            fprintf (stdout, "%.*s\r\n", (int)(c-remnant), remnant);
        else
            fprintf (stdout, "%.*s\n", (int)(c-remnant), remnant);
        remnant = c + 1;
        }
    fprintf (stdout, "%s", remnant);
    }
else {
    if (!inhibit_message)
        fprintf (stdout, "%s", buf);
    }
if ((!sim_oline) && (sim_log && (sim_log != stdout) && !inhibit_message))
    fprintf (sim_log, "%s", buf);
/* Always display messages in debug output */
if (sim_deb && (((sim_deb != stdout) && (sim_deb != sim_log)) || inhibit_message)) {
    TMLN *saved_oline = sim_oline;

    sim_oline = NULL;                           /* avoid potential debug to active socket */
    fprintf (sim_deb, "%s", buf);
    sim_oline = saved_oline;                    /* restore original socket */
    }

if (buf != stackbuf)
    free (buf);
return stat | ((stat != SCPE_OK) ? SCPE_NOMESSAGE : 0);
}

/* Inline debugging - will print debug message if debug file is
   set and the bitmask matches the current device debug options.
   Extra returns are added for un*x systems, since the output
   device is set into 'raw' mode when the cpu is booted,
   and the extra returns don't hurt any other systems. 
   Callers should be calling sim_debug() which is a macro
   defined in scp.h which evaluates the action condition before 
   incurring call overhead. */
static void _sim_vdebug (uint32 dbits, DEVICE* dptr, UNIT *uptr, const char* fmt, va_list arglist)
{
if (sim_deb && dptr && ((dptr->dctrl | (uptr ? uptr->dctrl : 0)) & dbits)) {
    TMLN *saved_oline = sim_oline;
    char stackbuf[STACKBUFSIZE];
    int32 bufsize = sizeof(stackbuf);
    char *buf = stackbuf;
    int32 i, j, len;
    const char* debug_prefix = sim_debug_prefix(dbits, dptr, uptr);   /* prefix to print if required */

    sim_oline = NULL;                                   /* avoid potential debug to active socket */
    buf[bufsize-1] = '\0';

    while (1) {                                         /* format passed string, args */
#if defined(NO_vsnprintf)
        len = vsprintf (buf, fmt, arglist);
#else                                                   /* !defined(NO_vsnprintf) */
        len = vsnprintf (buf, bufsize-1, fmt, arglist);
#endif                                                  /* NO_vsnprintf */

/* If the formatted result didn't fit into the buffer, then grow the buffer and try again */

        if ((len < 0) || (len >= bufsize-1)) {
            if (buf != stackbuf)
                free (buf);
            bufsize = bufsize * 2;
            if (bufsize < len + 2)
                bufsize = len + 2;
            buf = (char *) malloc (bufsize);
            if (buf == NULL)                            /* out of memory */
                return;
            buf[bufsize-1] = '\0';
            continue;
            }
        break;
        }

/* Output the formatted data expanding newlines where they exist */

    for (i = j = 0; i < len; ++i) {
        if ('\n' == buf[i]) {
            if (i >= j) {
                if ((i != j) || (i == 0)) {
                    if (!debug_unterm)                  /* print prefix when required */
                        fwrite (debug_prefix, 1, strlen (debug_prefix), sim_deb);
                    fwrite (&buf[j], 1, i-j, sim_deb);
                    fwrite ("\r\n", 1, 2, sim_deb);
                    }
                debug_unterm = 0;
                }
            j = i + 1;
            }
        }
    if (i > j) {
        if (!debug_unterm)                          /* print prefix when required */
            fwrite (debug_prefix, 1, strlen (debug_prefix), sim_deb);
        fwrite (&buf[j], 1, i-j, sim_deb);
        }

/* Set unterminated flag for next time */

    debug_unterm = len ? (((buf[len-1]=='\n')) ? 0 : 1) : debug_unterm;
    if (buf != stackbuf)
        free (buf);
    sim_oline = saved_oline;                            /* restore original socket */
    }
}

void _sim_debug_unit (uint32 dbits, UNIT *uptr, const char* fmt, ...)
{
DEVICE *dptr = (uptr ? uptr->dptr : NULL);

if (sim_deb && (((dptr ? dptr->dctrl : 0) | (uptr ? uptr->dctrl : 0)) & dbits)) {
    va_list arglist;
    va_start (arglist, fmt);
    _sim_vdebug (dbits, dptr, uptr, fmt, arglist);
    va_end (arglist);
    }
}

void _sim_debug_device (uint32 dbits, DEVICE* dptr, const char* fmt, ...)
{
if (sim_deb && dptr && (dptr->dctrl & dbits)) {
    va_list arglist;
    va_start (arglist, fmt);
    _sim_vdebug (dbits, dptr, NULL, fmt, arglist);
    va_end (arglist);
    }
}

void sim_data_trace(DEVICE *dptr, UNIT *uptr, const uint8 *data, const char *position, size_t len, const char *txt, uint32 reason)
{

if (sim_deb && ((dptr->dctrl | (uptr ? uptr->dctrl : 0)) & reason)) {
    _sim_debug_unit (reason, uptr, "%s %s %slen: %08X\n", sim_uname(uptr), txt, position, (unsigned int)len);
    if (data && len) {
        unsigned int i, same, group, sidx, oidx, ridx, eidx, soff;
        char outbuf[80], strbuf[28], rad50buf[36], ebcdicbuf[32];
        static char hex[] = "0123456789ABCDEF";
        static char rad50[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$._0123456789";
        static unsigned char ebcdic2ascii[] = {
            0000,0001,0002,0003,0234,0011,0206,0177,
            0227,0215,0216,0013,0014,0015,0016,0017,
            0020,0021,0022,0023,0235,0205,0010,0207,
            0030,0031,0222,0217,0034,0035,0036,0037,
            0200,0201,0202,0203,0204,0012,0027,0033,
            0210,0211,0212,0213,0214,0005,0006,0007,
            0220,0221,0026,0223,0224,0225,0226,0004,
            0230,0231,0232,0233,0024,0025,0236,0032,
            0040,0240,0241,0242,0243,0244,0245,0246,
            0247,0250,0133,0056,0074,0050,0053,0041,
            0046,0251,0252,0253,0254,0255,0256,0257,
            0260,0261,0135,0044,0052,0051,0073,0136,
            0055,0057,0262,0263,0264,0265,0266,0267,
            0270,0271,0174,0054,0045,0137,0076,0077,
            0272,0273,0274,0275,0276,0277,0300,0301,
            0302,0140,0072,0043,0100,0047,0075,0042,
            0303,0141,0142,0143,0144,0145,0146,0147,
            0150,0151,0304,0305,0306,0307,0310,0311,
            0312,0152,0153,0154,0155,0156,0157,0160,
            0161,0162,0313,0314,0315,0316,0317,0320,
            0321,0176,0163,0164,0165,0166,0167,0170,
            0171,0172,0322,0323,0324,0325,0326,0327,
            0330,0331,0332,0333,0334,0335,0336,0337,
            0340,0341,0342,0343,0344,0345,0346,0347,
            0173,0101,0102,0103,0104,0105,0106,0107,
            0110,0111,0350,0351,0352,0353,0354,0355,
            0175,0112,0113,0114,0115,0116,0117,0120,
            0121,0122,0356,0357,0360,0361,0362,0363,
            0134,0237,0123,0124,0125,0126,0127,0130,
            0131,0132,0364,0365,0366,0367,0370,0371,
            0060,0061,0062,0063,0064,0065,0066,0067,
            0070,0071,0372,0373,0374,0375,0376,0377,
            };

        for (i=same=0; i<len; i += 16) {
            if ((i > 0) && (0 == memcmp (&data[i], &data[i-16], 16))) {
                ++same;
                continue;
                }
            if (same > 0) {
                _sim_debug_unit (reason, uptr, "%04X thru %04X same as above\n", i-(16*same), i-1);
                same = 0;
                }
            group = (((len - i) > 16) ? 16 : (len - i));
            strcpy (ebcdicbuf, (sim_deb_switches & SWMASK ('E')) ? " EBCDIC:" : "");
            eidx = strlen(ebcdicbuf);
            strcpy (rad50buf, (sim_deb_switches & SWMASK ('D')) ? " RAD50:" : "");
            ridx = strlen(rad50buf);
            strcpy (strbuf, (sim_deb_switches & (SWMASK ('E') | SWMASK ('D'))) ? "ASCII:" : "");
            soff = strlen(strbuf);
            for (sidx=oidx=0; sidx<group; ++sidx) {
                outbuf[oidx++] = ' ';
                outbuf[oidx++] = hex[(data[i+sidx]>>4)&0xf];
                outbuf[oidx++] = hex[data[i+sidx]&0xf];
                if (sim_isprint (data[i+sidx]))
                    strbuf[soff+sidx] = data[i+sidx];
                else
                    strbuf[soff+sidx] = '.';
                if (ridx && ((sidx&1) == 0)) {
                    uint16 word = data[i+sidx] + (((uint16)data[i+sidx+1]) << 8);

                    if (word >= 64000) {
                        rad50buf[ridx++] = '|'; /* Invalid RAD-50 character */
                        rad50buf[ridx++] = '|'; /* Invalid RAD-50 character */
                        rad50buf[ridx++] = '|'; /* Invalid RAD-50 character */
                        }
                    else {
                        rad50buf[ridx++] = rad50[word/1600];
                        rad50buf[ridx++] = rad50[(word/40)%40];
                        rad50buf[ridx++] = rad50[word%40];
                        }
                    }
                if (eidx) {
                    if (sim_isprint (ebcdic2ascii[data[i+sidx]]))
                        ebcdicbuf[eidx++] = ebcdic2ascii[data[i+sidx]];
                    else
                        ebcdicbuf[eidx++] = '.';
                    }
                }
            outbuf[oidx] = '\0';
            strbuf[soff+sidx] = '\0';
            ebcdicbuf[eidx] = '\0';
            rad50buf[ridx] = '\0';
            _sim_debug_unit (reason, uptr, "%04X%-48s %s%s%s\n", i, outbuf, strbuf, ebcdicbuf, rad50buf);
            }
        if (same > 0) {
            _sim_debug_unit (reason, uptr, "%04X thru %04X same as above\n", i-(16*same), (unsigned int)(len-1));
            }
        }
    }
}

int Fprintf (FILE *f, const char* fmt, ...)
{
int ret = 0;
va_list args;

if (sim_mfile) {
    char stackbuf[STACKBUFSIZE];
    int32 bufsize = sizeof(stackbuf);
    char *buf = stackbuf;
    va_list arglist;
    int32 len;

    buf[bufsize-1] = '\0';

    while (1) {                                         /* format passed string, args */
        va_start (arglist, fmt);
#if defined(NO_vsnprintf)
        len = vsprintf (buf, fmt, arglist);
#else                                                   /* !defined(NO_vsnprintf) */
        len = vsnprintf (buf, bufsize-1, fmt, arglist);
#endif                                                  /* NO_vsnprintf */
        va_end (arglist);

/* If the formatted result didn't fit into the buffer, then grow the buffer and try again */

        if ((len < 0) || (len >= bufsize-1)) {
            if (buf != stackbuf)
                free (buf);
            bufsize = bufsize * 2;
            if (bufsize < len + 2)
                bufsize = len + 2;
            buf = (char *) malloc (bufsize);
            if (buf == NULL)                            /* out of memory */
                return -1;
            buf[bufsize-1] = '\0';
            continue;
            }
        break;
        }

/* Store the formatted data */

    if (sim_mfile->pos + len > sim_mfile->size) {
        sim_mfile->size = sim_mfile->pos + 2 * MAX(bufsize, 512);
        sim_mfile->buf = (char *)realloc (sim_mfile->buf, sim_mfile->size);
        }
    memcpy (sim_mfile->buf + sim_mfile->pos, buf, len);
    sim_mfile->pos += len;

    if (buf != stackbuf)
        free (buf);
    }
else {
    va_start (args, fmt);
    if (sim_oline)
        tmxr_linemsgvf (sim_oline, fmt, args);
    else
        ret = vfprintf (f, fmt, args);
    va_end (args);
    }
return ret;
}

/* Hierarchical help presentation
 *
 * Device help can be presented hierarchically by calling
 *
 * t_stat scp_help (FILE *st, DEVICE *dptr,
 *                  UNIT *uptr, int flag, const char *help, char *cptr)
 *
 * or one of its three cousins from the device HELP routine.
 *
 * *help is the pointer to the structured help text to be displayed.
 *
 * The format and usage, and some helper macros can be found in scp_help.h
 * If you don't use the macros, it is not necessary to #include "scp_help.h".
 *
 * Actually, if you don't specify a DEVICE pointer and don't include
 * other device references, it can be used for non-device help.
 */

#define blankch(x) ((x) == ' ' || (x) == '\t')

typedef struct topic {
    uint32         level;
    char          *title;
    char          *label;
    struct topic  *parent;
    struct topic **children;
    uint32         kids;
    char          *text;
    size_t         len;
    uint32         flags;
    uint32         kidwid;
#define HLP_MAGIC_TOPIC  1
    } TOPIC;

static volatile struct {
    const char *error;
    const char *prox;
    size_t block;
    size_t line;
    } help_where = { "", NULL, 0, 0 };
jmp_buf (help_env);
#define FAIL(why,text,here) { help_where.error = #text; help_where.prox = here; longjmp (help_env, (why)); }

/* Add to topic text.
 * Expands text buffer as necessary.
 */

static void appendText (TOPIC *topic, const char *text, size_t len)
{
char *newt;

if (!len) 
    return;

newt = (char *)realloc (topic->text, topic->len + len +1);
if (!newt) {
    FAIL (SCPE_MEM, No memory, NULL);
    }
topic->text = newt;
memcpy (newt + topic->len, text, len);
topic->len +=len;
newt[topic->len] = '\0';
return;
}

/* Release memory held by a topic and its children.
 */
static void cleanHelp (TOPIC *topic)
{
TOPIC *child;
size_t i;

free (topic->title);
free (topic->text);
free (topic->label);
for (i = 0; i < topic->kids; i++) {
    child = topic->children[i];
    cleanHelp (child);
    free (child);
    }
free (topic->children);
return;
}

/* Build a help tree from a string.
 * Handles substitutions, formatting.
 */
static TOPIC *buildHelp (TOPIC *topic, DEVICE *dptr,
                         UNIT *uptr, const char *htext, va_list ap)
{
char *end;
size_t n, ilvl;
#define VSMAX 100
char *vstrings[VSMAX];
size_t vsnum = 0;
char * astrings[VSMAX+1];
size_t asnum = 0;
char *const *hblock;
const char *ep;
t_bool excluded = FALSE;

/* variable arguments consumed table.
 * The scheme used allows arguments to be accessed in random
 * order, but for portability, all arguments must be char *.
 * If you try to violate this, there ARE machines that WILL break.
 */

memset (vstrings, 0, sizeof (vstrings));
memset (astrings, 0, sizeof (astrings));
astrings[asnum++] = (char *) htext;

for (hblock = astrings; (htext = *hblock) != NULL; hblock++) {
    help_where.block = hblock - astrings;
    help_where.line = 0;
    while (*htext) {
        const char *start;

        help_where.line++;
        if (sim_isspace (*htext) || *htext == '+') {/* Topic text, indented topic text */
            if (excluded) {                     /* Excluded topic text */
                while (*htext && *htext != '\n')
                    htext++;
                if (*htext)
                    ++htext;
                continue;
                }
            ilvl = 1;
            appendText (topic, "    ", 4);      /* Basic indentation */
            if (*htext == '+') {                /* More for each + */
                while (*htext == '+') {
                    ilvl++;
                    appendText (topic, "    ", 4);
                    htext++;
                    }
                }
            while (*htext && *htext != '\n' && sim_isspace (*htext))
                htext++;
            if (!*htext)                        /* Empty after removing leading spaces */
                break;
            start = htext;
            while (*htext) {                    /* Process line for substitutions */
                if (*htext == '%') {
                    appendText (topic, start, htext - start); /* Flush up to escape */
                    switch (*++htext) {         /* Evaluate escape */
                        case 'U':
                            if (dptr) {
                                char buf[129];
                                n = uptr? uptr - dptr->units: 0;
                                sprintf (buf, "%s%u", dptr->name, (int)n);
                                appendText (topic, buf, strlen (buf));
                                }
                            break;
                        case 'D':
                            if (dptr) {
                                appendText (topic, dptr->name, strlen (dptr->name));
                                break;
                                }
                        case 'S':
                            appendText (topic, sim_name, strlen (sim_name));
                            break;
                        case '%':
                            appendText (topic, "%", 1);
                            break;
                        case '+':
                            appendText (topic, "+", 1);
                            break;
                        default:                    /* Check for vararg # */
                            if (sim_isdigit (*htext)) {
                                n = 0;
                                while (sim_isdigit (*htext))
                                    n += (n * 10) + (*htext++ - '0');
                                if (( *htext != 'H' && *htext != 's') ||
                                    n == 0 || n >= VSMAX)
                                    FAIL (SCPE_ARG, Invalid escape, htext);
                                while (n > vsnum)   /* Get arg pointer if not cached */
                                    vstrings[vsnum++] = va_arg (ap, char *);
                                start = vstrings[n-1]; /* Insert selected string */
                                if (*htext == 'H') {   /* Append as more input */
                                    if (asnum >= VSMAX) {
                                        FAIL (SCPE_ARG, Too many blocks, htext);
                                        }
                                    astrings[asnum++] = (char *)start;
                                    break;
                                    }
                                ep = start;
                                while (*ep) {
                                    if (*ep == '\n') {
                                        ep++;       /* Segment to \n */
                                        appendText (topic, start, ep - start);
                                        if (*ep) {  /* More past \n, indent */
                                            size_t i;
                                            for (i = 0; i < ilvl; i++)
                                                appendText (topic, "    ", 4);
                                            }
                                        start = ep;
                                        } 
                                    else
                                        ep++;
                                    }
                                appendText (topic, start, ep-start);
                                break;
                                }
                            FAIL (SCPE_ARG, Invalid escape, htext);
                        } /* switch (escape) */
                    start = ++htext;
                    continue;                   /* Current line */
                    } /* if (escape) */
                if (*htext == '\n') {           /* End of line, append last segment */
                    htext++;
                    appendText (topic, start, htext - start);
                    break;                      /* To next line */
                    }
                htext++;                        /* Regular character */
                }
            continue;
            } /* topic text line */
        if (sim_isdigit (*htext)) {             /* Topic heading */
            TOPIC **children;
            TOPIC *newt;
            char nbuf[100];

            n = 0;
            start = htext;
            while (sim_isdigit (*htext))
                n += (n * 10) + (*htext++ - '0');
            if ((htext == start) || !n) {
                FAIL (SCPE_ARG, Invalid topic heading, htext);
                }
            if (n <= topic->level) {            /* Find level for new topic */
                while (n <= topic->level)
                    topic = topic->parent;
                } 
            else {
                if (n > topic->level +1) {      /* Skipping down more than 1 */
                    FAIL (SCPE_ARG, Level not contiguous, htext); /* E.g. 1 3, not reasonable */
                    }
                }
            while (*htext && (*htext != '\n') && sim_isspace (*htext))
                htext++;
            if (!*htext || (*htext == '\n')) {  /* Name missing */
                FAIL (SCPE_ARG, Missing topic name, htext);
                }
            start = htext;
            while (*htext && (*htext != '\n'))
                htext++;
            if (start == htext) {               /* Name NULL */
                FAIL (SCPE_ARG, Null topic name, htext);
                }
            excluded = FALSE;
            if (*start == '?') {                /* Conditional topic? */
                size_t n = 0;
                start++;
                while (sim_isdigit (*start))    /* Get param # */
                    n += (n * 10) + (*start++ - '0');
                if (!*start || *start == '\n'|| n == 0 || n >= VSMAX)
                    FAIL (SCPE_ARG, Invalid parameter number, start);
                while (n > vsnum)               /* Get arg pointer if not cached */
                    vstrings[vsnum++] = va_arg (ap, char *);
                end = vstrings[n-1];            /* Check for True */
                if (!end || !(sim_toupper (*end) == 'T' || *end == '1')) {
                    excluded = TRUE;            /* False, skip topic this time */
                    if (*htext)
                        htext++;
                    continue;
                    }
                }
            newt = (TOPIC *) calloc (sizeof (TOPIC), 1);
            if (!newt) {
                FAIL (SCPE_MEM, No memory, NULL);
                }
            newt->title = (char *) malloc ((htext - start)+1);
            if (!newt->title) {
                free (newt);
                FAIL (SCPE_MEM, No memory, NULL);
                }
            memcpy (newt->title, start, htext - start);
            newt->title[htext - start] = '\0';
            if (*htext)
                htext++;

            if (newt->title[0] == '$')
                newt->flags |= HLP_MAGIC_TOPIC;

            children = (TOPIC **) realloc (topic->children,
                                           (topic->kids +1) * sizeof (TOPIC *));
            if (!children) {
                free (newt->title);
                free (newt);
                FAIL (SCPE_MEM, No memory, NULL);
                }
            topic->children = children;
            topic->children[topic->kids++] = newt;
            newt->level = n;
            newt->parent = topic;
            n = strlen (newt->title);
            if (n > topic->kidwid)
                topic->kidwid = n;
            sprintf (nbuf, ".%u", topic->kids);
            n = strlen (topic->label) + strlen (nbuf) + 1;
            newt->label = (char *) malloc (n);
            if (!newt->label) {
                free (newt->title);
                topic->children[topic->kids -1] = NULL;
                free (newt);
                FAIL (SCPE_MEM, No memory, NULL);
                }
            sprintf (newt->label, "%s%s", topic->label, nbuf);
            topic = newt;
            continue;
            } /* digits introducing a topic */
        if (*htext == ';') {                    /* Comment */
            while (*htext && *htext != '\n')
                htext++;
            continue;
            }
        FAIL (SCPE_ARG, Unknown line type, htext);     /* Unknown line */
        } /* htext not at end */
    memset (vstrings, 0, VSMAX * sizeof (char *));
    vsnum = 0;
    } /* all strings */

return topic;
}

/* Create prompt string - top thru current topic
 * Add prompt at end.
 */
static char *helpPrompt ( TOPIC *topic, const char *pstring, t_bool oneword )
{
char *prefix;
char *newp, *newt;

if (topic->level == 0) {
    prefix = (char *) calloc (2,1);
    if (!prefix) {
        FAIL (SCPE_MEM, No memory, NULL);
        }
    prefix[0] = '\n';
    } 
else
    prefix = helpPrompt (topic->parent, "", oneword);

newp = (char *) malloc (strlen (prefix) + 1 + strlen (topic->title) + 1 +
                        strlen (pstring) +1);
if (!newp) {
    free (prefix);
    FAIL (SCPE_MEM, No memory, NULL);
    }
strcpy (newp, prefix);
if (topic->children) {
    if (topic->level != 0)
        strcat (newp, " ");
    newt = (topic->flags & HLP_MAGIC_TOPIC)?
            topic->title+1: topic->title;
    if (oneword) {
        char *np = newp + strlen (newp);
        while (*newt) {
            *np++ = blankch (*newt)? '_' : *newt;
            newt++;
            }
        *np = '\0';
        }
    else
        strcat (newp, newt);
    if (*pstring && *pstring != '?')
        strcat (newp, " ");
    }
strcat (newp, pstring);
free (prefix);
return newp;
}

static void displayMagicTopic (FILE *st, DEVICE *dptr, TOPIC *topic)
{
char tbuf[CBUFSIZE];
size_t i, skiplines = 0;
#ifdef _WIN32
FILE *tmp;
char *tmpnam;

do {
    int fd;
    tmpnam = _tempnam (NULL, "simh");
    fd = _open (tmpnam, _O_CREAT | _O_RDWR | _O_EXCL, _S_IREAD | _S_IWRITE);
    if (fd != -1) {
        tmp = _fdopen (fd, "w+");
        break;
        }
    } while (1);
#else
FILE *tmp = tmpfile();
#endif

if (!tmp) {
    fprintf (st, "Unable to create temporary file: %s\n", strerror (errno));
    return;
    }
    
if (topic->title) {
    fprintf (st, "%s\n", topic->title+1);

    skiplines = 0;
    if (dptr) {
        if (!strcmp (topic->title+1, "Registers")) {
            fprint_reg_help (tmp, dptr) ;
            skiplines = 1;
            }
        else
            if (!strcmp (topic->title+1, "Set commands")) {
                fprint_set_help (tmp, dptr);
                skiplines = 3;
                }
            else
                if (!strcmp (topic->title+1, "Show commands")) {
                    fprint_show_help (tmp, dptr);
                    skiplines = 3;
                    }
        }
    }
rewind (tmp);

/* Discard leading blank lines/redundant titles */

for (i =0; i < skiplines; i++)
    fgets (tbuf, sizeof (tbuf), tmp);

while (fgets (tbuf, sizeof (tbuf), tmp)) {
    if (tbuf[0] != '\n')
        fputs ("    ", st);
    fputs (tbuf, st);
    }
fclose (tmp);
#ifdef _WIN32
remove (tmpnam);
free (tmpnam);
#endif
return;
}
/* Flatten and display help for those who say they prefer it.
 */

static t_stat displayFlatHelp (FILE *st, DEVICE *dptr,
                               UNIT *uptr, int32 flag,
                               TOPIC *topic, va_list ap )
{
size_t i;

if (topic->flags & HLP_MAGIC_TOPIC) {
    fprintf (st, "\n%s ", topic->label);
    displayMagicTopic (st, dptr, topic);
    }
else
    fprintf (st, "\n%s %s\n", topic->label, topic->title);
    
/* Topic text (for magic topics, follows for explanations)
 * It's possible/reasonable for a magic topic to have no text.
 */

if (topic->text)
    fputs (topic->text, st);

for (i = 0; i < topic->kids; i++)
    displayFlatHelp (st, dptr, uptr, flag, topic->children[i], ap);

return SCPE_OK;
}

#define HLP_MATCH_AMBIGUOUS (~0u)
#define HLP_MATCH_WILDCARD  (~1U)
#define HLP_MATCH_NONE      0
static int matchHelpTopicName (TOPIC *topic, const char *token)
{
size_t i, match;
char cbuf[CBUFSIZE], *cptr;

if (!strcmp (token, "*"))
    return HLP_MATCH_WILDCARD;

match = 0;
for (i = 0; i < topic->kids; i++) {
    strcpy (cbuf,topic->children[i]->title +
            ((topic->children[i]->flags & HLP_MAGIC_TOPIC)? 1 : 0));
    cptr = cbuf;
    while (*cptr) {
        if (blankch (*cptr)) {
            *cptr++ = '_';
            } 
        else {
            *cptr = (char)sim_toupper (*cptr);
            cptr++;
            }
        }
    if (!strncmp (cbuf, token, strlen (token))) {
        if (match)
            return HLP_MATCH_AMBIGUOUS;
        match = i+1;
        }
    }
return match;
}

/* Main help routine
 * Takes a va_list
 */

t_stat scp_vhelp (FILE *st, DEVICE *dptr,
                  UNIT *uptr, int32 flag,
                  const char *help, const char *cptr, va_list ap)
{

TOPIC top;
TOPIC *topic = &top;
int failed;
size_t match;
size_t i;
const char *p;
t_bool flat_help = FALSE;
char cbuf [CBUFSIZE], gbuf[CBUFSIZE];

static const char attach_help[] = { " ATTACH" };
static const char brief_help[] = { "%s help.  Type <CR> to exit, HELP for navigation help" };
static const char onecmd_help[] = { "%s help." };
static const char help_help[] = {

    /****|***********************80 column width guide********************************/
    "    This help command provides hierarchical help.  To see more information,\n"
    "    type an offered subtopic name.  To move back a level, just type <CR>.\n"
    "    To review the current topic/subtopic, type \"?\".\n"
    "    To view all subtopics, type \"*\".\n"
    "    To exit help at any time, type EXIT.\n"
    };

memset (&top, 0, sizeof(top));
top.parent = &top;
if ((failed = setjmp (help_env)) != 0) {
    fprintf (stderr, "\nHelp was unable to process the help for this device.\n"
                     "Error in block %u line %u: %s\n"
                     "%s%*.*s%s"
                     " Please contact the device maintainer.\n", 
             (int)help_where.block, (int)help_where.line, help_where.error, 
             help_where.prox ? "Near '" : "", 
             help_where.prox ? 15 : 0, help_where.prox ? 15 : 0, 
             help_where.prox ? help_where.prox : "", 
                 help_where.prox ? "'" : "");
    cleanHelp (&top);
    return failed;
    }

/* Compile string into navigation tree */

/* Root */

if (dptr) {
    p = dptr->name;
    flat_help = (dptr->flags & DEV_FLATHELP) != 0;
    }
else
    p = sim_name;
top.title = (char *) malloc (strlen (p) + ((flag & SCP_HELP_ATTACH)? sizeof (attach_help)-1: 0) +1);
for (i = 0; p[i]; i++ )
    top.title[i] = (char)sim_toupper (p[i]);
top.title[i] = '\0';
if (flag & SCP_HELP_ATTACH)
    strcpy (top.title+i, attach_help);

top.label = (char *) malloc (sizeof ("1"));
strcpy (top.label, "1");

flat_help = flat_help || !sim_ttisatty() || (flag & SCP_HELP_FLAT);

if (flat_help) {
    flag |= SCP_HELP_FLAT;
    if (sim_ttisatty())
        fprintf (st, "%s help.\nThis help is also available in hierarchical form.\n", top.title);
    else
        fprintf (st, "%s help.\n", top.title);
    }
else
    fprintf (st, ((flag & SCP_HELP_ONECMD)? onecmd_help: brief_help), top.title);

/* Add text and subtopics */

(void) buildHelp (&top, dptr, uptr, help, ap);

/* Go to initial topic if provided */

while (cptr && *cptr) {
    cptr = get_glyph (cptr, gbuf, 0);
    if (!gbuf[0])
        break;
    if (!strcmp (gbuf, "HELP")) {           /* HELP (about help) */
        fprintf (st, "\n");
        fputs (help_help, st);
        break;
        }
    match =  matchHelpTopicName (topic, gbuf);
    if (match == HLP_MATCH_WILDCARD) {
        displayFlatHelp (st, dptr, uptr, flag, topic, ap);
        cleanHelp (&top);
        return SCPE_OK;
        }
    if (match == HLP_MATCH_AMBIGUOUS) {
        fprintf (st, "\n%s is ambiguous in %s\n", gbuf, topic->title);
        break;
        }
    if (match == HLP_MATCH_NONE) {
        fprintf (st, "\n%s is not available in %s\n", gbuf, topic->title);
        break;
        }
    topic = topic->children[match-1];
    }
cptr = NULL;

if (flat_help) {
    displayFlatHelp (st, dptr, uptr, flag, topic, ap);
    cleanHelp (&top);
    return SCPE_OK;
    }

/* Interactive loop displaying help */

while (TRUE) {
    char *pstring;
    const char *prompt[2] = {"? ", "Subtopic? "};

    /* Some magic topic names for help from data structures */

    if (topic->flags & HLP_MAGIC_TOPIC) {
        fputc ('\n', st);
        displayMagicTopic (st, dptr, topic);
        }
    else
        fprintf (st, "\n%s\n", topic->title);

    /* Topic text (for magic topics, follows for explanations)
     * It's possible/reasonable for a magic topic to have no text.
     */

    if (topic->text)
        fputs (topic->text, st);

    if (topic->kids) {
        size_t w = 0;
        char *p;
        char tbuf[CBUFSIZE];

        fprintf (st, "\n    Additional information available:\n\n");
        for (i = 0; i < topic->kids; i++) {
            strcpy (tbuf, topic->children[i]->title + 
                    ((topic->children[i]->flags & HLP_MAGIC_TOPIC)? 1 : 0));
            for (p = tbuf; *p; p++) {
                if (blankch (*p))
                    *p = '_';
                }
            w += 4 + topic->kidwid;
            if (w > 80) {
                w = 4 + topic->kidwid;
                fputc ('\n', st);
                }
            fprintf (st, "    %-*s", topic->kidwid, tbuf);
            }
        fprintf (st, "\n\n");
        if (flag & SCP_HELP_ONECMD) {
            pstring = helpPrompt (topic, "", TRUE);
            fprintf (st, "To view additional topics, type HELP %s topicname\n", pstring+1);
            free (pstring);
            break;
            }
        }

    if (!sim_ttisatty() || (flag & SCP_HELP_ONECMD))
        break;

  reprompt:
    if (!cptr || !*cptr) {
        pstring = helpPrompt (topic, prompt[topic->kids != 0], FALSE);

        cptr = read_line_p (pstring, cbuf, sizeof (cbuf), stdin);
        free (pstring);
        }

    if (!cptr)                              /* EOF, exit help */
        break;

    cptr = get_glyph (cptr, gbuf, 0);
    if (!strcmp (gbuf, "*")) {              /* Wildcard */
        displayFlatHelp (st, dptr, uptr, flag, topic, ap);
        gbuf[0] = '\0';                     /* Displayed all subtopics, go up */
        }
    if (!gbuf[0]) {                         /* Blank, up a level */
        if (topic->level == 0)
            break;
        topic = topic->parent;
        continue;
        }
    if (!strcmp (gbuf, "?"))                /* ?, repaint current topic */
        continue;
    if (!strcmp (gbuf, "HELP")) {           /* HELP (about help) */
        fputs (help_help, st);
        goto reprompt;
        }
    if (!strcmp (gbuf, "EXIT") || !strcmp (gbuf, "QUIT"))   /* EXIT (help) */
        break;

    /* String - look for that topic */

    if (!topic->kids) {
        fprintf (st, "No additional help at this level.\n");
        cptr = NULL;
        goto reprompt;
        }
    match = matchHelpTopicName (topic, gbuf);
    if (match == HLP_MATCH_AMBIGUOUS) {
        fprintf (st, "%s is ambiguous, please type more of the topic name\n", gbuf);
        cptr = NULL;
        goto reprompt;
        }

    if (match == HLP_MATCH_NONE) {
        fprintf (st, "Help for %s is not available\n", gbuf);
        cptr = NULL;
        goto reprompt;
        }
    /* Found, display subtopic */

    topic = topic->children[match-1];
    }

/* Free structures and return */

cleanHelp (&top);

return SCPE_OK;
}

/* variable argument list shell - most commonly used
 */

t_stat scp_help (FILE *st, DEVICE *dptr,
                 UNIT *uptr, int32 flag,
                 const char *help, const char *cptr, ...)
{
t_stat r;
va_list ap;

va_start (ap, cptr);
r = scp_vhelp (st, dptr, uptr, flag, help, cptr, ap);
va_end (ap);

return r;
}

#if 01
/* Read help from a file
 *
 * Not recommended due to OS-dependent issues finding the file, + maintenance.
 * Don't hardcode any path - just name.hlp - so there's a chance the file can
 * be found.
 */

t_stat scp_vhelpFromFile (FILE *st, DEVICE *dptr,
                          UNIT *uptr, int32 flag,
                          const char *helpfile,
                          const char *cptr, va_list ap)
{
FILE *fp;
char *help, *p;
t_offset size, n;
int c;
t_stat r;

fp = sim_fopen (helpfile, "r");
if (fp == NULL) {
    if (sim_argv && *sim_argv[0]) {
        char fbuf[(4*PATH_MAX)+1]; /* PATH_MAX is ridiculously small on some platforms */
        const char *d = NULL;

        /* Try to find a path from argv[0].  This won't always
         * work (one reason files are probably not a good idea),
         * but we might as well try.  Some OSs won't include a
         * path.  Having failed in the CWD, try to find the location
         * of the executable.  Failing that, try the 'help' subdirectory
         * of the executable.  Failing that, we're out of luck.
         */
        fbuf[sizeof(fbuf)-1] = '\0';
        strlcpy (fbuf, sim_argv[0], sizeof (fbuf));
        if ((p = (char *)match_ext (fbuf, "EXE")))
            *p = '\0';
        if ((p = strrchr (fbuf, '\\'))) {
            p[1] = '\0';
            d = "%s\\";
            } 
        else {
            if ((p = strrchr (fbuf, '/'))) {
                p[1] = '\0';
                d = "%s/";
#ifdef VMS
                } 
            else {
                if ((p = strrchr (fbuf, ']'))) {
                    p[1] = '\0';
                    d = "[%s]";
                    }
#endif
                }
            }
        if (p && (strlen (fbuf) + strlen (helpfile) +1) <= sizeof (fbuf)) {
            strcat (fbuf, helpfile);
            fp = sim_fopen (fbuf, "r");
            }
        if (!fp && p && (strlen (fbuf) + strlen (d) + sizeof ("help") +
                          strlen (helpfile) +1) <= sizeof (fbuf)) {
            sprintf (p+1, d, "help");
            strcat (p+1, helpfile);
            fp = sim_fopen (fbuf, "r");
            }
        }
    }
if (fp == NULL) {
    fprintf (stderr, "Unable to open %s\n", helpfile);
    return SCPE_UNATT;
    }

size = sim_fsize_ex (fp);                   /* Estimate; line endings will vary */

help = (char *) malloc ((size_t) size +1);
if (!help) {
    fclose (fp);
    return SCPE_MEM;
    }
p = help;
n = 0;

while ((c = fgetc (fp)) != EOF) {
    if (++n > size) {
#define XPANDQ 512
        p = (char *) realloc (help, ((size_t)size) + XPANDQ +1);
        if (!p) {
            free (help);
            fclose (fp);
            return SCPE_MEM;
            }
        help = p;
        size += XPANDQ;
        p += n;
        }
    *p++ = (char)c;
    }
*p++ = '\0';

fclose (fp);

r = scp_vhelp (st, dptr, uptr, flag, help, cptr, ap);
free (help);

return r;
}

t_stat scp_helpFromFile (FILE *st, DEVICE *dptr,
                         UNIT *uptr, int32 flag,
                         const char *helpfile, const char *cptr, ...)
{
t_stat r;
va_list ap;

va_start (ap, cptr);
r = scp_vhelpFromFile (st, dptr, uptr, flag, helpfile, cptr, ap);
va_end (ap);

return r;
}
#endif

/*
 * Expression evaluation package
 * 
 * Derived from code provided by Gabriel Pizzolato
 */


typedef t_svalue (*Operator_Function)(t_svalue, t_svalue);

typedef t_svalue (*Operator_String_Function)(const char *, const char *);

typedef struct Operator {
    const char *string;
    int         precedence;
    int         unary;
    Operator_Function
                function;
    Operator_String_Function
                string_function;
    const char *description;
    } Operator;

typedef struct Stack_Element {
    Operator *op;
    char data[72];
    } Stack_Element;

typedef struct Stack {
    int size;
    int pointer;
    int id;
    Stack_Element *elements;
    } Stack;

#define STACK_GROW_AMOUNT 5             /* Number of elements to add to stack when needed */

/* static variable allocation */
static int stack_counter = 0; /* number of stacks current allocated */

/* start of true stack code */
/*-----------------------------------------------------------------------------
 * Delete the given stack and free all memory associated with it.
 -----------------------------------------------------------------------------*/
void delete_Stack (Stack *sp)
{
if (sp == NULL)
    return;

sim_debug (SIM_DBG_EXP_STACK, sim_dflt_dev, "[Stack %d has been deallocated]\n", sp->id);

/* Free the data that the stack was pointing at */
free (sp->elements);
free (sp);
stack_counter--;
}

/*-----------------------------------------------------------------------------
 * Check to see if a stack is empty
 -----------------------------------------------------------------------------*/
static t_bool isempty_Stack (Stack *this_Stack)
{
return (this_Stack->pointer == 0);
}

/*-----------------------------------------------------------------------------
 * Create a new stack.
 -----------------------------------------------------------------------------*/
static Stack *new_Stack (void)
{
/* Stack variable to return */
Stack *this_Stack = (Stack *)calloc(1, sizeof(*this_Stack));

this_Stack->id = ++stack_counter;
sim_debug (SIM_DBG_EXP_STACK, sim_dflt_dev, "[Stack %d has been allocated]\n", this_Stack->id);

return this_Stack; /* Returns created stack */
}

/*-----------------------------------------------------------------------------
 * Remove the top element from the specified stack
 -----------------------------------------------------------------------------*/
static t_bool pop_Stack (Stack * this_Stack, char *data, Operator **op)
{
*op = NULL;
*data = '\0';

if (isempty_Stack(this_Stack))
    return FALSE;

strcpy (data, this_Stack->elements[this_Stack->pointer-1].data);
*op = this_Stack->elements[this_Stack->pointer-1].op;
--this_Stack->pointer;

if (*op)
    sim_debug (SIM_DBG_EXP_STACK, sim_dflt_dev, "[Stack %d - Popping '%s'(precedence %d)]\n", 
                                                this_Stack->id, (*op)->string, (*op)->precedence);
else
    sim_debug (SIM_DBG_EXP_STACK, sim_dflt_dev, "[Stack %d - Popping %s]\n", 
                                                this_Stack->id, data);

return TRUE;                      /* Success */
}

/*-----------------------------------------------------------------------------
 * Add an element to the specified stack.
 -----------------------------------------------------------------------------*/
static t_bool push_Stack (Stack * this_Stack, char *data, Operator *op)
{
if (this_Stack == NULL)
    return FALSE;

if (this_Stack->pointer == this_Stack->size) {  /* If necessary, grow stack */
    this_Stack->size += STACK_GROW_AMOUNT;
    this_Stack->elements = (Stack_Element *)realloc (this_Stack->elements, this_Stack->size * sizeof(*this_Stack->elements));
    memset (&this_Stack->elements[this_Stack->size - STACK_GROW_AMOUNT], 0, STACK_GROW_AMOUNT * sizeof(*this_Stack->elements));
    }

this_Stack->elements[this_Stack->pointer].op = op;
strlcpy (this_Stack->elements[this_Stack->pointer].data, data, sizeof (this_Stack->elements[this_Stack->pointer].data));
++this_Stack->pointer;

if (op)
    sim_debug (SIM_DBG_EXP_STACK, sim_dflt_dev, "[Stack %d - Pushing '%s'(precedence %d)]\n", 
                                                this_Stack->id, op->string, op->precedence);
else
    sim_debug (SIM_DBG_EXP_STACK, sim_dflt_dev, "[Stack %d - Pushing %s]\n", 
                                                this_Stack->id, data);

return TRUE;                      /* Success */
}

/*-----------------------------------------------------------------------------
 * Return the element at the top of the stack.
 -----------------------------------------------------------------------------*/
static t_bool top_Stack (Stack * this_Stack, char *data, Operator **op)
{
if (isempty_Stack(this_Stack))
    return FALSE;

strcpy (data, this_Stack->elements[this_Stack->pointer-1].data);
*op = this_Stack->elements[this_Stack->pointer-1].op;

if (*op)
    sim_debug (SIM_DBG_EXP_STACK, sim_dflt_dev, "[Stack %d - Topping '%s'(precedence %d)]\n", 
                                                this_Stack->id, (*op)->string, (*op)->precedence);
else
    sim_debug (SIM_DBG_EXP_STACK, sim_dflt_dev, "[Stack %d - Topping %s]\n", 
                                                this_Stack->id, data);

return TRUE;                      /* Success */
}

/* All Functions implementing operations */

static t_svalue _op_add (t_svalue augend, t_svalue addend)
{
return augend + addend;
}

static t_svalue _op_sub (t_svalue subtrahend, t_svalue minuend)
{
return minuend - subtrahend;
}

static t_svalue _op_mult (t_svalue factorx, t_svalue factory)
{
return factorx * factory;
}

static t_svalue _op_div (t_svalue divisor, t_svalue dividend)
{
if (divisor != 0)
    return dividend / divisor;
return T_SVALUE_MAX;
}

static t_svalue _op_mod (t_svalue divisor, t_svalue dividend)
{
if (divisor != 0)
    return dividend % divisor;
return 0;
}

static t_svalue _op_comp (t_svalue data, t_svalue unused)
{
return ~data;
}

static t_svalue _op_log_not (t_svalue data, t_svalue unused)
{
return !data;
}

static t_svalue _op_log_and (t_svalue data1, t_svalue data2)
{
return data2 && data1;
}

static t_svalue _op_log_or (t_svalue data1, t_svalue data2)
{
return data2 || data1;
}

static t_svalue _op_bit_and (t_svalue data1, t_svalue data2)
{
return data2 & data1;
}

static t_svalue _op_bit_rsh (t_svalue shift, t_svalue data)
{
return data >> shift;
}

static t_svalue _op_bit_lsh (t_svalue shift, t_svalue data)
{
return data << shift;
}

static t_svalue _op_bit_or (t_svalue data1, t_svalue data2)
{
return data2 | data1;
}

static t_svalue _op_bit_xor (t_svalue data1, t_svalue data2)
{
return data2 ^ data1;
}

static t_svalue _op_eq (t_svalue data1, t_svalue data2)
{
return data2 == data1;
}

static t_svalue _op_ne (t_svalue data1, t_svalue data2)
{
return data2 != data1;
}

static t_svalue _op_le (t_svalue data1, t_svalue data2)
{
return data2 <= data1;
}

static t_svalue _op_lt (t_svalue data1, t_svalue data2)
{
return data2 < data1;
}

static t_svalue _op_ge (t_svalue data1, t_svalue data2)
{
return data2 >= data1;
}

static t_svalue _op_gt (t_svalue data1, t_svalue data2)
{
return data2 > data1;
}

static int _i_strcmp (const char *s1, const char *s2)
{
return ((sim_switches & SWMASK('I')) ? strcasecmp (s2, s1) : strcmp (s2, s1));
}

static t_svalue _op_str_eq (const char *str1, const char *str2)
{
return (0 == _i_strcmp (str2, str1));
}

static t_svalue _op_str_ne (const char *str1, const char *str2)
{
return (0 != _i_strcmp (str2, str1));
}

static t_svalue _op_str_le (const char *str1, const char *str2)
{
return (0 <= _i_strcmp (str2, str1));
}

static t_svalue _op_str_lt (const char *str1, const char *str2)
{
return (0 < _i_strcmp (str2, str1));
}

static t_svalue _op_str_ge (const char *str1, const char *str2)
{
return (0 >= _i_strcmp (str2, str1));
}

static t_svalue _op_str_gt (const char *str1, const char *str2)
{
return (0 > _i_strcmp (str2, str1));
}

/* 
 * The order of elements in this array is significant.  
 * Care must be taken so that longer strings which have
 * shorter strings contained in them must come first.
 * For example "!=" must come before "!".
 *
 * These operators implement C language operator precedence
 * as described in:
 *   http://en.cppreference.com/w/c/language/operator_precedence
 */
static Operator operators[] = {
    {"(",       99, 0, NULL,            NULL,           "Open Parenthesis"},
    {")",       99, 0, NULL,            NULL,           "Close Parenthesis"},
    {"+",        4, 0, _op_add,         NULL,           "Addition"},
    {"-",        4, 0, _op_sub,         NULL,           "Subtraction"},
    {"*",        3, 0, _op_mult,        NULL,           "Multiplication"},
    {"/",        3, 0, _op_div,         NULL,           "Division"},
    {"%",        3, 0, _op_mod,         NULL,           "Modulus"},
    {"&&",      11, 0, _op_log_and,     NULL,           "Logical AND"},
    {"||",      12, 0, _op_log_or,      NULL,           "Logical OR"},
    {"&",        8, 0, _op_bit_and,     NULL,           "Bitwise AND"},
    {">>",       5, 0, _op_bit_rsh,     NULL,           "Bitwise Right Shift"},
    {"<<",       5, 0, _op_bit_lsh,     NULL,           "Bitwise Left Shift"},
    {"|",       10, 0, _op_bit_or,      NULL,           "Bitwise Inclusive OR"},
    {"^",        9, 0, _op_bit_xor,     NULL,           "Bitwise Exclusive OR"},
    {"==",       7, 0, _op_eq,          _op_str_eq,     "Equality"},
    {"!=",       7, 0, _op_ne,          _op_str_ne,     "Inequality"},
    {"<=",       6, 0, _op_le,          _op_str_le,     "Less than or Equal"},
    {"<",        6, 0, _op_lt,          _op_str_lt,     "Less than"},
    {">=",       6, 0, _op_ge,          _op_str_ge,     "Greater than or Equal"},
    {">",        6, 0, _op_gt,          _op_str_gt,     "Greater than"},
    {"!",        2, 1, _op_log_not,     NULL,           "Logical Negation"},
    {"~",        2, 1, _op_comp,        NULL,           "Bitwise Compliment"},
    {NULL}};

static const char *get_glyph_exp (const char *cptr, char *buf, Operator **oper, t_stat *stat)
{
static const char HexDigits[] = "0123456789abcdefABCDEF";
static const char OctalDigits[] = "01234567";
static const char BinaryDigits[] = "01";

*stat = SCPE_OK;                            /* Assume Success */
*buf = '\0';
*oper = NULL;
while (isspace (*cptr))
    ++cptr;
if (isalpha (*cptr) || (*cptr == '_')) {
    while (isalnum (*cptr) || (*cptr == '.') || (*cptr == '_'))
        *buf++ = *cptr++;
    *buf = '\0';
    }
else {
    if (isdigit (*cptr)) {
        if ((!memcmp (cptr, "0x", 2)) ||    /* Hex Number */
            (!memcmp (cptr, "0X", 2))) {
            memcpy (buf, cptr, 2);
            cptr += 2;
            buf += 2;
            while (*cptr && strchr (HexDigits, *cptr))
                *buf++ = *cptr++;
            *buf = '\0';
            }
        else {
            if ((!memcmp (cptr, "0b", 2)) ||/* Binary Number */
                (!memcmp (cptr, "0B", 2))) {
                memcpy (buf, cptr, 2);
                cptr += 2;
                buf += 2;
                while (*cptr && strchr (BinaryDigits, *cptr))
                    *buf++ = *cptr++;
                *buf = '\0';
                }
            else {
                if (*cptr == '0') {         /* Octal Number */
                    while (*cptr && strchr (OctalDigits, *cptr))
                        *buf++ = *cptr++;
                    *buf = '\0';
                    }
                else {                      /* Decimal Number */
                    while (isdigit (*cptr))
                        *buf++ = *cptr++;
                    *buf = '\0';
                    }
                }
            }
        if (isalpha (*cptr)) {              /* Numbers can't be followed by alpha character */
            *stat = SCPE_INVEXPR;
            return cptr;
            }
        }
    else {
        if (((cptr[0] == '-') || (cptr[0] == '+')) &&
            (isdigit (cptr[1]))) {          /* Signed Decimal Number */
            *buf++ = *cptr++;
            while (isdigit (*cptr))
                *buf++ = *cptr++;
            *buf = '\0';
            if (isalpha (*cptr)) {           /* Numbers can't be followed by alpha character */
                *stat = SCPE_INVEXPR;
                return cptr;
                }
            }
        else {                               /* Special Characters (operators) */
            if ((*cptr == '"') || (*cptr == '\'')) {
                cptr = (CONST char *)get_glyph_gen (cptr, buf, 0, (sim_switches & SWMASK ('I')), TRUE, '\\');
                }
            else {
                Operator *op;

                for (op = operators; op->string; op++) {
                    if (!memcmp (cptr, op->string, strlen (op->string))) {
                        strcpy (buf, op->string);
                        cptr += strlen (op->string);
                        *oper = op;
                        break;
                        }
                    }
                if (!op->string) {
                    *stat = SCPE_INVEXPR;
                    return cptr;
                    }
                }
            }
        }
    }
while (isspace (*cptr))
    ++cptr;
return cptr;
}

/*
 * Translate a string containing an infix ordered expression 
 * into a stack containing that expression in postfix order 
 */
static const char *sim_into_postfix (Stack *stack1, const char *cptr, t_stat *stat, t_bool parens_required)
{
const char *start = cptr;
const char *last_cptr;
int parens = 0;
Operator *op = NULL, *last_op;
Stack *stack2 = new_Stack();        /* operator stack */
char gbuf[CBUFSIZE];

while (isspace(*cptr))              /* skip leading whitespace */
    ++cptr;
if (parens_required && (*cptr != '(')) {
    delete_Stack (stack2);
    *stat = SCPE_INVEXPR;
    return cptr;
    }
while (*cptr) {
    last_cptr = cptr;
    last_op = op;
    cptr = get_glyph_exp (cptr, gbuf, &op, stat);
    if (*stat != SCPE_OK) {
        delete_Stack (stack2);
        return cptr;
        }
    if (!last_op && !op && ((gbuf[0] == '-') || (gbuf[0] == '+'))) {
        for (op = operators; gbuf[0] != op->string[0]; op++);
        gbuf[0] = '0';
        cptr = last_cptr + 1;
        }
    sim_debug (SIM_DBG_EXP_EVAL, sim_dflt_dev, "[Glyph: %s]\n", op ? op->string : gbuf);
    if (!op) {
        push_Stack (stack1, gbuf, op);
        continue;
        }
    if (0 == strcmp (op->string, "(")) {
        ++parens;
        push_Stack (stack2, gbuf, op);
        continue;
        }
    if (0 == strcmp (op->string, ")")) {
        char temp_buf[CBUFSIZE];
        Operator *temp_op;

        --parens;
        if ((!pop_Stack (stack2, temp_buf, &temp_op)) ||
            (parens < 0)){
            *stat = sim_messagef (SCPE_INVEXPR, "Invalid Parenthesis nesting\n");
            delete_Stack (stack2);
            return cptr;
            }
        while (0 != strcmp (temp_op->string, "(")) {
            push_Stack (stack1, temp_buf, temp_op);
            if (!pop_Stack (stack2, temp_buf, &temp_op))
                break;
            }
        if (parens_required && (parens == 0)) {
            delete_Stack (stack2);
            return cptr;
            }
        continue;
        }
    while (!isempty_Stack(stack2)) {
        char top_buf[CBUFSIZE];
        Operator *top_op;

        top_Stack (stack2, top_buf, &top_op);
        if (top_op->precedence > op->precedence)
            break;
        pop_Stack (stack2, top_buf, &top_op);
        push_Stack (stack1, top_buf, top_op);
        }
    push_Stack (stack2, gbuf, op);
    }
if (parens != 0)
    *stat = sim_messagef (SCPE_INVEXPR, "Invalid Parenthesis nesting\n");
/* migrate the rest of stack2 onto stack1 */
while (!isempty_Stack (stack2)) {
    pop_Stack (stack2, gbuf, &op);
    push_Stack (stack1, gbuf, op);
    }

delete_Stack (stack2);          /* delete the working operator stack */
return cptr;                    /* return any unprocessed input */
}

static t_bool _value_of (const char *data, t_svalue *svalue, char *string, size_t string_size)
{
CONST char *gptr;
size_t data_size = strlen (data);

if (isalpha (*data) || (*data == '_')) {
    REG *rptr = NULL;
    DEVICE *dptr = sim_dfdev;
    const char *dot;
    
    dot = strchr (data, '.');
    if (dot) {
        char devnam[CBUFSIZE];

        memcpy (devnam, data, dot - data);
        devnam[dot - data] = '\0';
        if (find_dev (devnam)) {
            dptr = find_dev (devnam);
            data = dot + 1;
            rptr = find_reg (data, &gptr, dptr);
            }
        }
    else
        rptr = find_reg_glob (data, &gptr, &dptr);
    if (rptr) {
        *svalue = (t_svalue)get_rval (rptr, 0);
        sprint_val (string, *svalue, 10, string_size - 1, PV_LEFTSIGN);
        return TRUE;
        }
    gptr = _sim_get_env_special (data, string, string_size - 1);
    if (gptr) {
        *svalue = strtotsv(string, &gptr, 0);
        return ((*gptr == '\0') && (*string));
        }
    else {
        data = "";
        data_size = 0;
        }
    }
string[0] = '\0';
if ((data[0] == '\'') && (data_size > 1) && (data[data_size - 1] == '\''))
    snprintf (string, string_size - 1, "\"%*.*s\"", (int)(data_size - 2), (int)(data_size - 2), data + 1);
if ((data[0] == '"') && (data_size > 1) && (data[data_size - 1] == '"'))
    strlcpy (string, data, string_size);
if (string[0] == '\0') {
    *svalue = strtotsv(data, &gptr, 0);
    return ((*gptr == '\0') && (*data));
    }
sim_sub_args (string, string_size, sim_exp_argv);
*svalue = strtotsv(string, &gptr, 0);
return ((*gptr == '\0') && (*string));
}

/*
 * Evaluate a given stack1 containing a postfix expression 
 */
static t_svalue sim_eval_postfix (Stack *stack1, t_stat *stat)
{
Stack *stack2 = new_Stack();    /* local working stack2 which is holds the numbers operators */
char temp_data[CBUFSIZE];       /* Holds the items popped from the stack2 */
Operator *temp_op;
t_svalue temp_val;
char temp_string[CBUFSIZE + 2];

*stat = SCPE_OK;
/* Reverse stack1 onto stack2 leaving stack1 empty */
while (!isempty_Stack(stack1)) {
    pop_Stack (stack1, temp_data, &temp_op);
    if (temp_op)
        sim_debug (SIM_DBG_EXP_EVAL, sim_dflt_dev, "[Expression element: %s (%d)\n", 
                                                   temp_op->string, temp_op->precedence);
    else
        sim_debug (SIM_DBG_EXP_EVAL, sim_dflt_dev, "[Expression element: %s\n", 
                                                   temp_data);
    push_Stack (stack2, temp_data, temp_op);
    }

/* Loop through the postfix expression in stack2 evaluating until stack2 is empty */
while (!isempty_Stack(stack2)) {
    pop_Stack (stack2, temp_data, &temp_op);
    if (temp_op) {              /* operator? */
        t_bool num1;
        t_svalue val1;
        char item1[CBUFSIZE], string1[CBUFSIZE+2];
        Operator *op1;
        t_bool num2;
        t_svalue val2;
        char item2[CBUFSIZE], string2[CBUFSIZE+2];
        Operator *op2;

        if (!pop_Stack (stack1, item1, &op1)) {
            *stat = SCPE_INVEXPR;
            delete_Stack (stack2);
            return 0;
            }
        if (temp_op->unary)
            strlcpy (item2, "0", sizeof (item2));
        else {
            if ((!pop_Stack (stack1, item2, &op2)) && 
                (temp_op->string[0] != '-') &&
                (temp_op->string[0] != '+')) {
                *stat = SCPE_INVEXPR;
                delete_Stack (stack2);
                return 0;
                }
            }
        num1 = _value_of (item1, &val1, string1, sizeof (string1));
        num2 = _value_of (item2, &val2, string2, sizeof (string2));
        if ((!(num1 && num2)) && temp_op->string_function)
            sprint_val (temp_data, (t_value)temp_op->string_function (string1, string2), 10, sizeof(temp_data) - 1, PV_LEFTSIGN);
        else
            sprint_val (temp_data, (t_value)temp_op->function (val1, val2), 10, sizeof(temp_data) - 1, PV_LEFTSIGN);
        push_Stack (stack1, temp_data, NULL);
        }
    else
        push_Stack (stack1, temp_data, temp_op);
    }
if (!pop_Stack (stack1, temp_data, &temp_op)) {
    *stat = SCPE_INVEXPR;
    delete_Stack (stack2);
    return 0;
    }
delete_Stack (stack2);
if (_value_of (temp_data, &temp_val, temp_string, sizeof (temp_string)))
    return temp_val;
else
    return (t_svalue)(strlen (temp_string) > 2);
}

const char *sim_eval_expression (const char *cptr, t_svalue *value, t_bool parens_required, t_stat *stat)
{
const char *iptr = cptr;
Stack *postfix = new_Stack (); /* for the postfix expression */

sim_debug (SIM_DBG_EXP_EVAL, sim_dflt_dev, "[Evaluate Expression: %s\n", cptr);
*value = 0;
cptr = sim_into_postfix (postfix, cptr, stat, parens_required);
if (*stat != SCPE_OK) {
    delete_Stack (postfix);
    *stat = sim_messagef (SCPE_ARG, "Invalid Expression Element:\n%s\n%*s^\n", iptr, (int)(cptr-iptr), "");
    return cptr;
    }

*value = sim_eval_postfix (postfix, stat);
delete_Stack (postfix);
return cptr;
}
