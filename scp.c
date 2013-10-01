/* scp.c: simulator control program

   Copyright (c) 1993-2012, Robert M Supnik

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

#define NOT_MUX_USING_CODE /* sim_tmxr library provider or agnostic */

#include "sim_defs.h"
#include "sim_rev.h"
#include "sim_disk.h"
#include "sim_tape.h"
#include "sim_ether.h"
#include "sim_serial.h"
#include "sim_video.h"
#include "sim_sock.h"
#include <signal.h>
#include <ctype.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <sys/stat.h>

#if defined(HAVE_DLOPEN)                                /* Dynamic Readline support */
#include <dlfcn.h>
#endif

#define EX_D            0                               /* deposit */
#define EX_E            1                               /* examine */
#define EX_I            2                               /* interactive */
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
#define SSH_ST          0                               /* set */
#define SSH_SH          1                               /* show */
#define SSH_CL          2                               /* clear */

#define MAX_DO_NEST_LVL 20                              /* DO cmd nesting level */
#define SRBSIZ          1024                            /* save/restore buffer */
#define SIM_BRK_INILNT  4096                            /* bpt tbl length */
#define SIM_BRK_ALLTYP  0xFFFFFFFF
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
UNIT * volatile sim_wallclock_queue;
UNIT * volatile sim_wallclock_entry;
UNIT * volatile sim_clock_cosched_queue;
t_bool sim_asynch_enabled = TRUE;
int32 sim_asynch_check;
int32 sim_asynch_latency = 4000;      /* 4 usec interrupt latency */
int32 sim_asynch_inst_latency = 20;   /* assume 5 mip simulator */
#else
t_bool sim_asynch_enabled = FALSE;
#endif

/* The per-simulator init routine is a weak global that defaults to NULL
   The other per-simulator pointers can be overrriden by the init routine */

void (*sim_vm_init) (void);
char* (*sim_vm_read) (char *ptr, int32 size, FILE *stream) = NULL;
void (*sim_vm_post) (t_bool from_scp) = NULL;
CTAB *sim_vm_cmd = NULL;
void (*sim_vm_fprint_addr) (FILE *st, DEVICE *dptr, t_addr addr) = NULL;
t_addr (*sim_vm_parse_addr) (DEVICE *dptr, char *cptr, char **tptr) = NULL;

/* Prototypes */

/* Set and show command processors */

t_stat set_dev_radix (DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat set_dev_enbdis (DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat set_dev_debug (DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat set_unit_enbdis (DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat ssh_break (FILE *st, char *cptr, int32 flg);
t_stat show_cmd_fi (FILE *ofile, int32 flag, char *cptr);
t_stat show_config (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_queue (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_time (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_mod_names (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_show_commands (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_log_names (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_dev_radix (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_dev_debug (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_dev_logicals (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_dev_modifiers (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_dev_show_commands (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_version (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_default (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_break (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_on (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat show_device (FILE *st, DEVICE *dptr, int32 flag);
t_stat show_unit (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag);
t_stat show_all_mods (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flg, int32 *toks);
t_stat show_one_mod (FILE *st, DEVICE *dptr, UNIT *uptr, MTAB *mptr, char *cptr, int32 flag);
t_stat sim_check_console (int32 sec);
t_stat sim_save (FILE *sfile);
t_stat sim_rest (FILE *rfile);

/* Breakpoint package */

t_stat sim_brk_init (void);
t_stat sim_brk_set (t_addr loc, int32 sw, int32 ncnt, char *act);
t_stat sim_brk_clr (t_addr loc, int32 sw);
t_stat sim_brk_clrall (int32 sw);
t_stat sim_brk_show (FILE *st, t_addr loc, int32 sw);
t_stat sim_brk_showall (FILE *st, int32 sw);
char *sim_brk_getact (char *buf, int32 size);
void sim_brk_clract (void);
void sim_brk_npc (uint32 cnt);
BRKTAB *sim_brk_new (t_addr loc);

/* Commands support routines */

SCHTAB *get_search (char *cptr, int32 radix, SCHTAB *schptr);
int32 test_search (t_value val, SCHTAB *schptr);
static char *get_glyph_gen (char *iptr, char *optr, char mchar, t_bool uc, t_bool quote);
int32 get_switches (char *cptr);
char *get_sim_sw (char *cptr);
t_stat get_aval (t_addr addr, DEVICE *dptr, UNIT *uptr);
t_value get_rval (REG *rptr, uint32 idx);
void put_rval (REG *rptr, uint32 idx, t_value val);
t_value strtotv (const char *inptr, char **endptr, uint32 radix);
void fprint_help (FILE *st);
void fprint_stopped (FILE *st, t_stat r);
void fprint_capac (FILE *st, DEVICE *dptr, UNIT *uptr);
void fprint_sep (FILE *st, int32 *tokens);
char *read_line (char *ptr, int32 size, FILE *stream);
char *read_line_p (char *prompt, char *ptr, int32 size, FILE *stream);
REG *find_reg_glob (char *ptr, char **optr, DEVICE **gdptr);
char *sim_trim_endspc (char *cptr);

/* Forward references */

t_stat scp_attach_unit (DEVICE *dptr, UNIT *uptr, char *cptr);
t_stat scp_detach_unit (DEVICE *dptr, UNIT *uptr);
t_bool qdisable (DEVICE *dptr);
t_stat attach_err (UNIT *uptr, t_stat stat);
t_stat detach_all (int32 start_device, t_bool shutdown);
t_stat assign_device (DEVICE *dptr, char *cptr);
t_stat deassign_device (DEVICE *dptr);
t_stat ssh_break_one (FILE *st, int32 flg, t_addr lo, int32 cnt, char *aptr);
t_stat run_boot_prep (void);
t_stat exdep_reg_loop (FILE *ofile, SCHTAB *schptr, int32 flag, char *cptr,
    REG *lowr, REG *highr, uint32 lows, uint32 highs);
t_stat ex_reg (FILE *ofile, t_value val, int32 flag, REG *rptr, uint32 idx);
t_stat dep_reg (int32 flag, char *cptr, REG *rptr, uint32 idx);
t_stat exdep_addr_loop (FILE *ofile, SCHTAB *schptr, int32 flag, char *cptr,
    t_addr low, t_addr high, DEVICE *dptr, UNIT *uptr);
t_stat ex_addr (FILE *ofile, int32 flag, t_addr addr, DEVICE *dptr, UNIT *uptr);
t_stat dep_addr (int32 flag, char *cptr, t_addr addr, DEVICE *dptr,
    UNIT *uptr, int32 dfltinc);
void fprint_fields (FILE *stream, t_value before, t_value after, BITFIELD* bitdefs);
t_stat step_svc (UNIT *ptr);
t_stat shift_args (char *do_arg[], size_t arg_count);
t_stat set_on (int32 flag, char *cptr);
t_stat set_verify (int32 flag, char *cptr);
t_stat set_message (int32 flag, char *cptr);
t_stat set_quiet (int32 flag, char *cptr);
t_stat set_asynch (int32 flag, char *cptr);
t_stat do_cmd_label (int32 flag, char *cptr, char *label);
void int_handler (int signal);
t_stat set_prompt (int32 flag, char *cptr);

/* Global data */

DEVICE *sim_dflt_dev = NULL;
UNIT *sim_clock_queue = QUEUE_LIST_END;
int32 sim_interval = 0;
int32 sim_switches = 0;
FILE *sim_ofile = NULL;
SCHTAB *sim_schptr = FALSE;
DEVICE *sim_dfdev = NULL;
UNIT *sim_dfunit = NULL;
DEVICE **sim_internal_devices = NULL;
uint32 sim_internal_device_count = 0;
int32 sim_opt_out = 0;
int32 sim_is_running = 0;
uint32 sim_brk_summ = 0;
uint32 sim_brk_types = 0;
uint32 sim_brk_dflt = 0;
char *sim_brk_act[MAX_DO_NEST_LVL];
BRKTAB *sim_brk_tab = NULL;
int32 sim_brk_ent = 0;
int32 sim_brk_lnt = 0;
int32 sim_brk_ins = 0;
t_bool sim_brk_pend[SIM_BKPT_N_SPC] = { FALSE };
t_addr sim_brk_ploc[SIM_BKPT_N_SPC] = { 0 };
int32 sim_quiet = 0;
int32 sim_step = 0;
static double sim_time;
static uint32 sim_rtime;
static int32 noqueue_time;
volatile int32 stop_cpu = 0;
t_value *sim_eval = NULL;
FILE *sim_log = NULL;                                   /* log file */
FILEREF *sim_log_ref = NULL;                            /* log file file reference */
FILE *sim_deb = NULL;                                   /* debug file */
FILEREF *sim_deb_ref = NULL;                            /* debug file file reference */
int32 sim_deb_switches = 0;                             /* debug switches */
REG *sim_deb_PC = NULL;                                 /* debug PC register pointer */
struct timespec sim_deb_basetime;                       /* debug timestamp relative base time */
char *sim_prompt = NULL;                                /* prompt string */
static FILE *sim_gotofile;                              /* the currently open do file */
static int32 sim_goto_line[MAX_DO_NEST_LVL+1];          /* the current line number in the currently open do file */
static int32 sim_do_echo = 0;                           /* the echo status of the currently open do file */
static int32 sim_show_message = 1;                      /* the message display status of the currently open do file */
static int32 sim_on_inherit = 0;                        /* the inherit status of on state and conditions when executing do files */
int32 sim_do_depth = 0;

static int32 sim_on_check[MAX_DO_NEST_LVL+1];
static char *sim_on_actions[MAX_DO_NEST_LVL+1][SCPE_MAX_ERR+1];
static char sim_do_filename[MAX_DO_NEST_LVL+1][CBUFSIZE];
static char *sim_do_label[MAX_DO_NEST_LVL+1];

static t_stat sim_last_cmd_stat;                        /* Command Status */

static SCHTAB sim_stab;

static UNIT sim_step_unit = { UDATA (&step_svc, 0, 0)  };
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
#if defined (USE_NETWORK) || defined (USE_SHARED)
static const char *sim_snet = "Ethernet support";
#else
static const char *sim_snet = "no Ethernet";
#endif

/* Tables and strings */

const char save_vercur[] = "V3.5";
const char save_ver32[] = "V3.2";
const char save_ver30[] = "V3.0";
const struct scp_error {
    char *code;
    char *message;
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
         {"SIGERR",  "SIGINT handler setup error"},
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

static CTAB cmd_table[] = {
    { "RESET", &reset_cmd, 0,
      "r{eset} {ALL|<device>}   reset simulator\n" },
    { "EXAMINE", &exdep_cmd, EX_E,
      "e{xamine} <list>         examine memory or registers\n" },
    { "IEXAMINE", &exdep_cmd, EX_E+EX_I,
      "ie{xamine} <list>        interactive examine memory or registers\n" },
    { "DEPOSIT", &exdep_cmd, EX_D,
      "d{eposit} <list> <val>   deposit in memory or registers\n" },
    { "IDEPOSIT", &exdep_cmd, EX_D+EX_I,
      "id{eposit} <list>        interactive deposit in memory or registers\n" },
    { "EVALUATE", &eval_cmd, 0,
      "ev{aluate} <expr>        evaluate symbolic expression\n" },
    { "RUN", &run_cmd, RU_RUN,
      "ru{n} {new PC}           reset and start simulation\n", &run_cmd_message },
    { "GO", &run_cmd, RU_GO,
      "go {new PC}              start simulation\n", &run_cmd_message }, 
    { "STEP", &run_cmd, RU_STEP,
      "s{tep} {n}               simulate n instructions\n", &run_cmd_message },
    { "CONTINUE", &run_cmd, RU_CONT,
      "c{ont}                   continue simulation\n", &run_cmd_message },
    { "BOOT", &run_cmd, RU_BOOT,
      "b{oot} <unit>            bootstrap unit\n", &run_cmd_message },
    { "BREAK", &brk_cmd, SSH_ST,
      "br{eak} <list>           set breakpoints\n" },
    { "NOBREAK", &brk_cmd, SSH_CL,
      "nobr{eak} <list>         clear breakpoints\n" },
    { "ATTACH", &attach_cmd, 0,
      "at{tach} <unit> <file>   attach file to simulated unit\n"
      "h{elp} <dev> attach      displays any device specific attach help\n" },
    { "DETACH", &detach_cmd, 0,
      "det{ach} <unit>          detach file from simulated unit\n" },
    { "ASSIGN", &assign_cmd, 0,
      "as{sign} <device> <name> assign logical name for device\n" },
    { "DEASSIGN", &deassign_cmd, 0,
      "dea{ssign} <device>      deassign logical name for device\n" },
    { "SAVE", &save_cmd, 0,
      "sa{ve} <file>            save simulator to file\n" },
    { "RESTORE", &restore_cmd, 0,
      "rest{ore}|ge{t} <file>   restore simulator from file\n" },
    { "GET", &restore_cmd, 0, NULL },
    { "LOAD", &load_cmd, 0,
      "l{oad} <file> {<args>}   load binary file\n" },
    { "DUMP", &load_cmd, 1,
      "du(mp) <file> {<args>}   dump binary file\n" },
    { "EXIT", &exit_cmd, 0,
      "exi{t}|q{uit}|by{e}      exit from simulation\n" },
    { "QUIT", &exit_cmd, 0, NULL },
    { "BYE", &exit_cmd, 0, NULL },
    { "CD", &set_default_cmd, 0,
      "cd                       set the current directory\n" },
    { "PWD", &pwd_cmd, 0, 
      "pwd                      show current directory\n" },
    { "DIR", &dir_cmd, 0, 
      "dir {dir}                list directory files\n" },
    { "LS", &dir_cmd, 0, 
      "ls {dir}                 list directory files\n" },
    { "SET", &set_cmd, 0,
      "set console arg{,arg...} set console options\n"
      "set console WRU          specify console drop to simh char\n"
      "set console BRK          specify console Break character\n"
      "set console DEL          specify console delete char\n"
      "set console PCHAR        specify console printable chars\n"
      "set console TELNET=port  specify console telnet port\n"
      "set console TELNET=LOG=log_file\n"
      "                         specify console telnet logging to the\n"
      "                         specified destination {LOG,STDOUT,STDERR,DEBUG\n"
      "                         or filename)\n"
      "set console TELNET=NOLOG disables console telnet logging\n"
      "set console TELNET=BUFFERED[=bufsize]\n"
      "                         specify console telnet buffering\n"
      "set console TELNET=NOBUFFERED\n"
      "                         disables console telnet buffering\n"
      "set console TELNET=UNBUFFERED\n"
      "                         disables console telnet buffering\n"
      "set console NOTELNET     disable console telnet\n"
      "set console SERIAL=serialport[;config]\n"
      "                         specify console serial port and optionally\n"
      "                         the port config (i.e. ;9600-8n1)\n"
      "set console NOSERIAL     disable console serial session\n"
      "set console LOG=log_file enable console logging to the\n"
      "                         specified destination {STDOUT,STDERR,DEBUG\n"
      "                         or filename)\n"
      "set console NOLOG        disable console logging\n"
      "set remote TELNET=port   specify remote console telnet port\n"
      "set remote NOTELNET      disables remote console\n"
      "set remote CONNECTIONS=n specify number of concurrent remote console sessions\n"
      "set remote TIMEOUT=n     specify number of seconds without input before\n"
      "                         automatic continue\n"
      "set default <dir>        set the current directory\n"
      "set log log_file         specify the log destination\n"
      "                         (STDOUT,DEBUG or filename)\n"
      "set nolog                disables any currently active logging\n"
      "set debug debug_file     specify the debug destination\n"
      "                         (STDOUT,STDERR,LOG or filename)\n"
      "set nodebug              disables any currently active debug output\n"
      "set break <list>         set breakpoints\n"
      "set nobreak <list>       clear breakpoints\n"
      "set throttle {x{M|K|%}}|{x/t}\n"
      "                         set simulation rate\n"
      "set nothrottle           set simulation rate to maximum\n"
      "set asynch               enable asynchronous I/O\n"
      "set noasynch             disable asynchronous I/O\n"
      "set environment name=val set environment variable\n"
      "set on                   enables error checking after command execution\n"
      "set noon                 disables error checking after command execution\n"
      "set on inherit           enables inheritance of ON state and actions into do command files\n"
      "set on noinherit         disables inheritance of ON state and actions into do command files\n"
      "set verify               re-enables display of command file processed commands\n"
      "set verbose              re-enables display of command file processed commands\n"
      "set noverify             disables display of command file processed commands\n"
      "set noverbose            disables display of command file processed commands\n"
      "set message              re-enables display of command file error messages\n"
      "set nomessage            disables display of command file error messages\n"
      "set quiet                disables suppression of some output and messages\n"
      "set noquiet              re-enables suppression of some output and messages\n"
      "set prompt \"string\"      sets an alternate simulator prompt string\n"
      "set <dev> OCT|DEC|HEX    set device display radix\n"
      "set <dev> ENABLED        enable device\n"
      "set <dev> DISABLED       disable device\n"
      "set <dev> DEBUG{=arg}    set device debug flags\n"
      "set <dev> NODEBUG={arg}  clear device debug flags\n"
      "set <dev> arg{,arg...}   set device parameters (see show modifiers)\n"
      "set <unit> ENABLED       enable unit\n"
      "set <unit> DISABLED      disable unit\n"
      "set <unit> arg{,arg...}  set unit parameters (see show modifiers)\n"
      "help <dev> set           displays the device specific set commands available\n" },
    { "SHOW", &show_cmd, 0,
      "sh{ow} br{eak} <list>    show breakpoints\n"
      "sh{ow} con{figuration}   show configuration\n"
      "sh{ow} cons{ole} {arg}   show console options\n"
      "sh{ow} dev{ices}         show devices\n"
      "sh{ow} fea{tures}        show system devices with descriptions\n"
      "sh{ow} m{odifiers}       show modifiers for all devices\n" 
      "sh{ow} s{how}            show SHOW commands for all devices\n" 
      "sh{ow} n{ames}           show logical names\n"
      "sh{ow} q{ueue}           show event queue\n"
      "sh{ow} ti{me}            show simulated time\n"
      "sh{ow} th{rottle}        show simulation rate\n"
      "sh{ow} a{synch}          show asynchronouse I/O state\n" 
      "sh{ow} ve{rsion}         show simulator version\n"
      "sh{ow} def{ault}         show current directory\n" 
      "sh{ow} re{mote}          show remote console configuration\n" 
      "sh{ow} <dev> RADIX       show device display radix\n"
      "sh{ow} <dev> DEBUG       show device debug flags\n"
      "sh{ow} <dev> MODIFIERS   show device modifiers\n"
      "sh{ow} <dev> NAMES       show device logical name\n"
      "sh{ow} <dev> SHOW        show device SHOW commands\n"
      "sh{ow} <dev> {arg,...}   show device parameters\n"
      "sh{ow} <unit> {arg,...}  show unit parameters\n"
      "sh{ow} ethernet          show ethernet devices\n"
      "sh{ow} serial            show serial devices\n"
      "sh{ow} multiplexer       show open multiplexer devices\n"
      "sh{ow} clocks            show calibrated timers\n"
      "sh{ow} on                show on condition actions\n"
      "h{elp} <dev> show        displays the device specific show commands available\n" },
    { "DO", &do_cmd, 1,
      "do {-V} {-O} {-E} {-Q} <file> {arg,arg...}\n"
      "                         process command file\n" },
    { "GOTO", &goto_cmd, 1,
      "goto <label>             goto label in command file\n" },
    { "RETURN", &return_cmd, 0,
      "return                   return from command file with last command status\n"
      "return {-Q} <status>     return from command file with specific status\n" },
    { "SHIFT", &shift_cmd, 0,
      "shift                    shift the command file's positional parameters\n" },
    { "CALL", &call_cmd, 0,
      "call                     transfer control to a labeled subroutine\n"
      "                         a command file.\n" },
    { "ON", &on_cmd, 0,
      "on <condition> <action>  perform action(s) after condition\n"
      "on <condition>           clear action for specific condition\n" },
    { "PROCEED", &noop_cmd, 0,
      "proceed                  continue command file execution without doing anything\n" },
    { "IGNORE", &noop_cmd, 0,
      "ignore                   continue command file execution without doing anything\n" },
    { "ECHO", &echo_cmd, 0,
      "echo <string>            display <string>\n" },
    { "ASSERT", &assert_cmd, 0,
      "assert {<dev>} <cond>    test simulator state against condition\n" },
    { "!", &spawn_cmd, 0,
      "!                        execute local command interpreter\n"
      "! <command>              execute local host command\n" },
    { "HELP", &help_cmd, 0,
      "h{elp}                   type this message\n"
      "h{elp} <command>         type help for command\n" 
      "h{elp} <dev>             type help for device\n"
      "h{elp} <dev> registers   type help for device register variables\n"
      "h{elp} <dev> attach      type help for device specific ATTACH command\n"
      "h{elp} <dev> set         type help for device specific SET commands\n"
      "h{elp} <dev> show        type help for device specific SHOW commands\n"
      "h{elp} <dev> <command>   type help for device specific <command> command\n" },
    { NULL, NULL, 0 }
    };

#if defined(_WIN32) || defined(__hpux)
static
int setenv(const char *envname, const char *envval, int overwrite)
{
char *envstr = malloc(strlen(envname)+strlen(envval)+2);
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
#endif


/* Main command loop */

int main (int argc, char *argv[])
{
char cbuf[4*CBUFSIZE], gbuf[CBUFSIZE], *cptr, *cptr2;
char nbuf[PATH_MAX + 7];
int32 i, sw;
t_bool lookswitch;
t_stat stat, stat_nomessage;
CTAB *cmdp;

#if defined (__MWERKS__) && defined (macintosh)
argc = ccommand (&argv);
#endif

set_prompt (0, "sim>");                                 /* start with set standard prompt */
*cbuf = 0;                                              /* init arg buffer */
sim_switches = 0;                                       /* init switches */
lookswitch = TRUE;
for (i = 1; i < argc; i++) {                            /* loop thru args */
    if (argv[i] == NULL)                                /* paranoia */
        continue;
    if ((*argv[i] == '-') && lookswitch) {              /* switch? */
        if ((sw = get_switches (argv[i])) < 0) {
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
            strcat (cbuf, " "); 
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
stop_cpu = 0;
sim_interval = 0;
sim_time = sim_rtime = 0;
noqueue_time = 0;
sim_clock_queue = QUEUE_LIST_END;
sim_is_running = 0;
sim_log = NULL;
if (sim_emax <= 0)
    sim_emax = 1;
sim_timer_init ();

if ((stat = sim_ttinit ()) != SCPE_OK) {
    fprintf (stderr, "Fatal terminal initialization error\n%s\n",
        sim_error_text (stat));
    return 0;
    }
if ((sim_eval = (t_value *) calloc (sim_emax, sizeof (t_value))) == NULL) {
    fprintf (stderr, "Unable to allocate examine buffer\n");
    return 0;
    };
if ((stat = reset_all_p (0)) != SCPE_OK) {
    fprintf (stderr, "Fatal simulator initialization error\n%s\n",
        sim_error_text (stat));
    return 0;
    }
if ((stat = sim_brk_init ()) != SCPE_OK) {
    fprintf (stderr, "Fatal breakpoint table initialization error\n%s\n",
        sim_error_text (stat));
    return 0;
    }
if (!sim_quiet) {
    printf ("\n");
    show_version (stdout, NULL, NULL, 0, NULL);
    }
if (sim_dflt_dev == NULL)                               /* if no default */
    sim_dflt_dev = sim_devices[0];

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
    if ((np = match_ext (nbuf, "EXE")))                 /* remove .exe */
        *np = 0;
    strcat (nbuf, ".ini\"");                            /* add .ini" */
    stat = do_cmd (-1, nbuf);                           /* proc cmd file */
    }

stat = SCPE_BARE_STATUS(stat);                          /* remove possible flag */

while (stat != SCPE_EXIT) {                             /* in case exit */
    if ((cptr = sim_brk_getact (cbuf, sizeof(cbuf))))   /* pending action? */
        printf ("%s%s\n", sim_prompt, cptr);            /* echo */
    else if (sim_vm_read != NULL) {                     /* sim routine? */
        printf ("%s", sim_prompt);                      /* prompt */
        cptr = (*sim_vm_read) (cbuf, sizeof(cbuf), stdin);
        }
    else cptr = read_line_p (sim_prompt, cbuf, sizeof(cbuf), stdin);/* read with prmopt*/
    if (cptr == NULL) {                                 /* EOF? */
        if (sim_ttisatty()) continue;                   /* ignore tty EOF */
        else break;                                     /* otherwise exit */
        }
    if (*cptr == 0)                                     /* ignore blank */
        continue;
    sim_sub_args (cbuf, sizeof(cbuf), argv);
    if (sim_log)                                        /* log cmd */
        fprintf (sim_log, "%s%s\n", sim_prompt, cptr);
    cptr = get_glyph (cptr, gbuf, 0);                   /* get command glyph */
    sim_switches = 0;                                   /* init switches */
    if ((cmdp = find_cmd (gbuf)))                       /* lookup command */
        stat = cmdp->action (cmdp->arg, cptr);          /* if found, exec */
    else stat = SCPE_UNK;
    stat_nomessage = stat & SCPE_NOMESSAGE;             /* extract possible message supression flag */
    stat_nomessage = stat_nomessage || (!sim_show_message);/* Apply global suppression */
    stat = SCPE_BARE_STATUS(stat);                      /* remove possible flag */
    sim_last_cmd_stat = stat;                           /* save command error status */
    if (!stat_nomessage) {                              /* displaying message status? */
        if (cmdp && (cmdp->message))                    /* special message handler? */
            cmdp->message (NULL, stat);                 /* let it deal with display */
        else
            if (stat >= SCPE_BASE) {                    /* error? */
                printf ("%s\n", sim_error_text (stat));
                if (sim_log)
                    fprintf (sim_log, "%s\n", sim_error_text (stat));
                }
        }
    if (sim_vm_post != NULL)
        (*sim_vm_post) (TRUE);
    }                                                   /* end while */

detach_all (0, TRUE);                                   /* close files */
sim_set_deboff (0, NULL);                               /* close debug */
sim_set_logoff (0, NULL);                               /* close log */
sim_set_notelnet (0, NULL);                             /* close Telnet */
sim_ttclose ();                                         /* close console */
AIO_CLEANUP;                                            /* Asynch I/O */
sim_cleanup_sock ();                                    /* cleanup sockets */
return 0;
}

/* Set prompt routine */

t_stat set_prompt (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];

if ((!cptr) || (*cptr == '\0'))
    return SCPE_ARG;

cptr = get_glyph_nc (cptr, gbuf, '"');                  /* get quote delimted token */
if (gbuf[0] == '\0') {                                  /* Token started with quote */
    gbuf[sizeof (gbuf)-1] = '\0';
    strncpy (gbuf, cptr, sizeof (gbuf)-1);
    cptr = strchr (gbuf, '"');
    if (cptr)
        *cptr = '\0';
    }
sim_prompt = realloc (sim_prompt, strlen (gbuf) + 2);   /* nul terminator and trailing blank */
sprintf (sim_prompt, "%s ", gbuf);
return SCPE_OK;
}

/* Find command routine */

CTAB *find_cmd (char *gbuf)
{
CTAB *cmdp = NULL;

if (sim_vm_cmd)                                         /* try ext commands */
    cmdp = find_ctab (sim_vm_cmd, gbuf);
if (cmdp == NULL)                                       /* try regular cmds */
    cmdp = find_ctab (cmd_table, gbuf);
return cmdp;
}

/* Exit command */

t_stat exit_cmd (int32 flag, char *cptr)
{
return SCPE_EXIT;
}

/* Help command */

void fprint_help (FILE *st)
{
CTAB *cmdp;

for (cmdp = sim_vm_cmd; cmdp && (cmdp->name != NULL); cmdp++) {
    if (cmdp->help)
        fputs (cmdp->help, st);
    }
for (cmdp = cmd_table; cmdp && (cmdp->name != NULL); cmdp++) {
    if (cmdp->help && (!sim_vm_cmd || !find_ctab (sim_vm_cmd, cmdp->name)))
        fputs (cmdp->help, st);
    }
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
char *tptr;
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
    namebuf = calloc (max_namelen + 1, sizeof (*namebuf));
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
            fprintf (st, "%-30s\t%s\n", buf, (strchr(mptr->mstring, '=')) ? "" : (mptr->help ? mptr->help : ""));
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
if (dptr->flags & DEV_DEBUG) {
    fprint_header (st, &found, header);
    sprintf (buf, "set %s DEBUG", sim_dname (dptr));
    fprintf (st,  "%-30s\tEnables debugging for device %s\n", buf, sim_dname (dptr));
    sprintf (buf, "set %s NODEBUG", sim_dname (dptr));
    fprintf (st,  "%-30s\tDisables debugging for device %s\n", buf, sim_dname (dptr));
    if (dptr->debflags) {
        strcpy (buf, "");
        fprintf (st, "set %s DEBUG=", sim_dname (dptr));
        for (dep = dptr->debflags; dep->name != NULL; dep++)
            fprintf (st, "%s%s", ((dep == dptr->debflags) ? "" : ";"), dep->name);
        fprintf (st, "\n");
        fprintf (st,  "%-30s\tEnables specific debugging for device %s\n", buf, sim_dname (dptr));
        fprintf (st, "set %s NODEBUG=", sim_dname (dptr));
        for (dep = dptr->debflags; dep->name != NULL; dep++)
            fprintf (st, "%s%s", ((dep == dptr->debflags) ? "" : ";"), dep->name);
        fprintf (st, "\n");
        fprintf (st,  "%-30s\tDisables specific debugging for device %s\n", buf, sim_dname (dptr));
        }
    }
if ((dptr->modifiers) && (dptr->numunits != 1)) {
    if (dptr->units->flags & UNIT_DISABLE) {
        fprint_header (st, &found, header);
        sprintf (buf, "set %sn ENABLE", sim_dname (dptr));
        fprintf (st,  "%-30s\tEnables unit %sn\n", buf, sim_dname (dptr));
        sprintf (buf, "set %sn DISABLE", sim_dname (dptr));
        fprintf (st,  "%-30s\tDisables unit %sn\n", buf, sim_dname (dptr));
        }
    for (mptr = dptr->modifiers; mptr->mask != 0; mptr++) {
        if ((!MODMASK(mptr,MTAB_VUN)) && MODMASK(mptr,MTAB_XTD))
            continue;                                           /* skip device only modifiers */
        if ((!mptr->valid) && MODMASK(mptr,MTAB_XTD))
            continue;                                           /* skip show only modifiers */
        if (mptr->mstring) {
            fprint_header (st, &found, header);
            sprintf (buf, "set %s%s %s%s", sim_dname (dptr), (dptr->numunits > 1) ? "n" : "0", mptr->mstring, (strchr(mptr->mstring, '=')) ? "" : (MODMASK(mptr,MTAB_VALR) ? "=val" : (MODMASK(mptr,MTAB_VALO) ? "{=val}": "")));
            fprintf (st, "%-30s\t%s\n", buf, (strchr(mptr->mstring, '=')) ? "" : (mptr->help ? mptr->help : ""));
            }
        }
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
if (dptr->flags & DEV_DEBUG) {
    fprint_header (st, &found, header);
    sprintf (buf, "show %s DEBUG", sim_dname (dptr));
    fprintf (st, "%-30s\tDisplays debugging status for device %s\n", buf, sim_dname (dptr));
    }
if ((dptr->modifiers) && (dptr->numunits != 1)) {
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

t_stat help_dev_help (FILE *st, DEVICE *dptr, UNIT *uptr, char *cptr)
{
char gbuf[CBUFSIZE];
CTAB *cmdp;

if (*cptr) {
    cptr = get_glyph (cptr, gbuf, 0);
    if ((cmdp = find_cmd (gbuf))) {
        if (cmdp->action == &exdep_cmd) {
            if (dptr->help)
                dptr->help (st, dptr, uptr, 0, cptr);
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
        fprintf (st, "No %s help is available for the %s device\n", cmdp->name, dptr->name);
        if (dptr->help)
            dptr->help (st, dptr, uptr, 0, cptr);
        return SCPE_OK;
        }
    if (MATCH_CMD (gbuf, "REGISTERS") == 0) {
        fprint_reg_help_ex (st, dptr, FALSE);
        return SCPE_OK;
        }
    fprintf (st, "No %s help is available for the %s device\n", gbuf, dptr->name);
    if (dptr->help)
        dptr->help (st, dptr, uptr, 0, cptr);
    return SCPE_OK;
    }
if (dptr->help) {
    dptr->help (st, dptr, uptr, 0, cptr);
    return SCPE_OK;
    }
if (dptr->description)
    fprintf (st, "%s %s help\n", dptr->description (dptr), dptr->name);
else
    fprintf (st, "%s help\n", dptr->name);
fprint_set_help_ex (st, dptr, TRUE);
fprint_show_help_ex (st, dptr, TRUE);
fprint_attach_help_ex (st, dptr, TRUE);
fprint_reg_help_ex (st, dptr, TRUE);
return SCPE_OK;
}

t_stat help_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
CTAB *cmdp;

GET_SWITCHES (cptr);
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
                if (dptr == NULL) {
                    dptr = find_dev (gbuf);
                    if (dptr == NULL)
                        return SCPE_2MARG;
                    }
                r = help_dev_help (stdout, dptr, uptr, (cmdp->action == &set_cmd) ? "SET" : "SHOW");
                if (sim_log)
                    help_dev_help (sim_log, dptr, uptr, (cmdp->action == &set_cmd) ? "SET" : "SHOW");
                return r;
                }
            else
                return SCPE_2MARG;
            }
        if (cmdp->help) {
            fputs (cmdp->help, stdout);
            if (sim_log)
                fputs (cmdp->help, sim_log);
            if (strcmp (cmdp->name, "HELP") == 0) {
                DEVICE *dptr;
                int i;

                for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
                    if (dptr->help) {
                        fprintf (stdout, "h{elp} %-17s type help for device %s\n", dptr->name, dptr->name);
                        if (sim_log)
                            fprintf (sim_log, "h{elp} %-17s type help for device %s\n", dptr->name, dptr->name);
                        }
                    if (dptr->attach_help || 
                        (DEV_TYPE(dptr) == DEV_MUX) ||
                        (DEV_TYPE(dptr) == DEV_DISK) ||
                        (DEV_TYPE(dptr) == DEV_TAPE)) {
                        fprintf (stdout, "h{elp} %s ATTACH\t type help for device %s ATTACH command\n", dptr->name, dptr->name);
                        if (sim_log)
                            fprintf (sim_log, "h{elp} %s ATTACH\t type help for device %s ATTACH command\n", dptr->name, dptr->name);
                        }
                    if (dptr->registers) {
                        if (dptr->registers->name != NULL) {
                            fprintf (stdout, "h{elp} %s REGISTERS\t type help for device %s register variables\n", dptr->name, dptr->name);
                            if (sim_log)
                                fprintf (sim_log, "h{elp} %s REGISTERS\t type help for device %s register variables\n", dptr->name, dptr->name);
                            }
                        }
                    if (dptr->modifiers) {
                        MTAB *mptr;

                        for (mptr = dptr->modifiers; mptr->pstring != NULL; mptr++) {
                            if (mptr->help) {
                                fprintf (stdout, "h{elp} %s SET\t\t type help for device %s SET commands (modifiers)\n", dptr->name, dptr->name);
                                if (sim_log)
                                    fprintf (sim_log, "h{elp} %s SET\t\t type help for device %s SET commands (modifiers)\n", dptr->name, dptr->name);
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
            }
        else { /* no help so it is likely a command alias */
            CTAB *cmdpa;

            for (cmdpa=cmd_table; cmdpa->name != NULL; cmdpa++)
                if ((cmdpa->action == cmdp->action) && (cmdpa->help)) {
                    fprintf (stdout, "%s is an alias for the %s command:\n%s", 
                                cmdp->name, cmdpa->name, cmdpa->help);
                    if (sim_log)
                        fprintf (sim_log, "%s is an alias for the %s command.\n%s", 
                                    cmdp->name, cmdpa->name, cmdpa->help);
                    break;
                    }
            if (cmdpa->name == NULL) {              /* not found? */
                fprintf (stdout, "No help available for the %s command\n", cmdp->name);
                if (sim_log)
                    fprintf (sim_log, "No help available for the %s command\n", cmdp->name);
                }
            }
        }
    else { 
        DEVICE *dptr;
        UNIT *uptr;

        dptr = find_unit (gbuf, &uptr);
        if (dptr == NULL) {
            dptr = find_dev (gbuf);
            if (dptr == NULL)
                return SCPE_ARG;
            if (dptr->flags & DEV_DISABLE) {
                fprintf (stdout, "Device %s is currently disabled\n", dptr->name);
                if (sim_log)
                    fprintf (sim_log, "Device %s is currently disabled\n", dptr->name);
                }
            }
        help_dev_help (stdout, dptr, uptr, cptr);
        if (sim_log)
            help_dev_help (stdout, dptr, uptr, cptr);
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

t_stat spawn_cmd (int32 flag, char *cptr)
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
status = system (cptr);
#if defined (VMS)
printf ("\n");
#endif

return status;
}

/* Echo command */

t_stat echo_cmd (int32 flag, char *cptr)
{
puts (cptr);
if (sim_log)
    fprintf (sim_log, "%s\n", cptr);
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

t_stat do_cmd (int32 flag, char *fcptr)
{
return do_cmd_label (flag, fcptr, NULL);
}

static char *do_position(void)
{
static char cbuf[CBUFSIZE];

sprintf (cbuf, "%s%s%s-%d", sim_do_filename[sim_do_depth], sim_do_label[sim_do_depth] ? "::" : "", sim_do_label[sim_do_depth] ? sim_do_label[sim_do_depth] : "", sim_goto_line[sim_do_depth]);
return cbuf;
}

t_stat do_cmd_label (int32 flag, char *fcptr, char *label)
{
char *cptr, cbuf[4*CBUFSIZE], gbuf[CBUFSIZE], *c, quote, *do_arg[10];
FILE *fpin;
CTAB *cmdp = NULL;
int32 echo, nargs, errabort, i;
int32 saved_sim_do_echo = sim_do_echo, 
      saved_sim_show_message = sim_show_message,
      saved_sim_on_inherit = sim_on_inherit,
      saved_sim_quiet = sim_quiet;
t_bool staying;
t_stat stat, stat_nomessage;
char *ocptr;

stat = SCPE_OK;
staying = TRUE;
if (flag > 0)                                           /* need switches? */
    GET_SWITCHES (fcptr);                               /* get switches */
echo = (sim_switches & SWMASK ('V')) || sim_do_echo;    /* -v means echo */
sim_quiet = (sim_switches & SWMASK ('Q')) || sim_quiet; /* -q means quiet */
sim_on_inherit =(sim_switches & SWMASK ('O')) || sim_on_inherit; /* -o means inherit ON condition actions */

errabort = sim_switches & SWMASK ('E');                 /* -e means abort on error */

c = fcptr;
for (nargs = 0; nargs < 10; ) {                         /* extract arguments */
    while (isspace (*c))                                /* skip blanks */
        c++;
    if (*c == 0)                                        /* all done? */
        do_arg [nargs++] = NULL;                        /* null argument */
    else {
        if (*c == '\'' || *c == '"')                    /* quoted string? */
            quote = *c++;
        else quote = 0;
        do_arg[nargs++] = c;                            /* save start */
        while (*c && (quote ? (*c != quote) : !isspace (*c)))
            c++;
        if (*c)                                         /* term at quote/spc */
            *c++ = 0;
        }
    }                                                   /* end for */

if (do_arg [0] == NULL)                                 /* need at least 1 */
    return SCPE_2FARG;
if ((fpin = fopen (do_arg[0], "r")) == NULL) {          /* file failed to open? */
    strcat (strcpy (cbuf, do_arg[0]), ".sim");          /* try again with .sim extension */
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
        for (i=0; i<SCPE_MAX_ERR; i++) {                /* replicate any on commands */
            if (sim_on_actions[sim_do_depth-1][i]) {
                sim_on_actions[sim_do_depth][i] = malloc(1+strlen(sim_on_actions[sim_do_depth-1][i]));
                if (NULL == sim_on_actions[sim_do_depth][i]) {
                    while (--i >= 0) {
                        free(sim_on_actions[sim_do_depth][i]);
                        sim_on_actions[sim_do_depth][i] = NULL;
                        }
                    sim_on_check[sim_do_depth] = 0;
                    sim_brk_clract ();                  /* defang breakpoint actions */
                    --sim_do_depth;                     /* unwind nesting */
                    return SCPE_MEM;
                    }
                strcpy(sim_on_actions[sim_do_depth][i], sim_on_actions[sim_do_depth-1][i]);
                }
            }
        }
    }

strcpy( sim_do_filename[sim_do_depth], do_arg[0]);      /* stash away do file name for possible use by 'call' command */
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
    ocptr = cptr = sim_brk_getact (cbuf, sizeof(cbuf)); /* get bkpt action */
    if (!ocptr) {                                       /* no pending action? */
        ocptr = cptr = read_line (cbuf, sizeof(cbuf), fpin);/* get cmd line */
        sim_goto_line[sim_do_depth] += 1;
        }
    sim_sub_args (cbuf, sizeof(cbuf), do_arg);          /* substitute args */
    if (cptr == NULL) {                                 /* EOF? */
        stat = SCPE_OK;                                 /* set good return */
        break;
        }
    if (*cptr == 0)                                     /* ignore blank */
        continue;
    if (echo) {                                         /* echo if -v */
        printf("%s> %s\n", do_position(), cptr);
        if (sim_log)
            fprintf (sim_log, "%s> %s\n", do_position(), cptr);
        }
    if (*cptr == ':')                                   /* ignore label */
        continue;
    cptr = get_glyph (cptr, gbuf, 0);                   /* get command glyph */
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
    else stat = SCPE_UNK;                               /* bad cmd given */
    stat_nomessage = stat & SCPE_NOMESSAGE;             /* extract possible message supression flag */
    stat_nomessage = stat_nomessage || (!sim_show_message);/* Apply global suppression */
    stat = SCPE_BARE_STATUS(stat);                      /* remove possible flag */
    if ((stat != SCPE_OK) ||
        ((cmdp->action != &return_cmd) &&
         (cmdp->action != &goto_cmd) &&
         (cmdp->action != &on_cmd) &&
         (cmdp->action != &echo_cmd)))
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
        if (!echo && !sim_quiet &&                      /* report if not echoing */
            !stat_nomessage &&                          /* and not suppressing messages */
            !(cmdp && cmdp->message)) {                 /* and not handling them specially */
            printf("%s> %s\n", do_position(), ocptr);
            if (sim_log)
                fprintf (sim_log, "%s> %s\n", do_position(), ocptr);
            }
        }
    if (!stat_nomessage) {                              /* report error if not suppressed */
        if (cmdp && cmdp->message)                      /* special message handler */
            cmdp->message ((!echo && !sim_quiet) ? ocptr : NULL, stat);
        else
            if (stat >= SCPE_BASE) {                    /* report error if not suppressed */
                printf ("%s\n", sim_error_text (stat));
                if (sim_log)
                    fprintf (sim_log, "%s\n", sim_error_text (stat));
                }
        }
    if (staying &&
        (sim_on_check[sim_do_depth]) && 
        (stat != SCPE_OK) &&
        (stat != SCPE_STEP)) {
        if ((stat <= SCPE_MAX_ERR) && sim_on_actions[sim_do_depth][stat])
            sim_brk_act[sim_do_depth] = sim_on_actions[sim_do_depth][stat];
        else
            sim_brk_act[sim_do_depth] = sim_on_actions[sim_do_depth][0];
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
    }
sim_quiet = saved_sim_quiet;                            /* restore quiet mode we entered with */
if ((flag >= 0) || (!sim_on_inherit)) {
    for (i=0; i<SCPE_MAX_ERR; i++) {                    /* release any on commands */
        free (sim_on_actions[sim_do_depth][i]);
        sim_on_actions[sim_do_depth][i] = NULL;
        }
    sim_on_check[sim_do_depth] = 0;                     /* clear on mode */
    }
if (flag >= 0)
    --sim_do_depth;                                     /* unwind nesting */
sim_brk_clract ();                                      /* defang breakpoint actions */
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

   Token "%0" expands to the command file name. 
   Token %n (n being a single digit) expands to the n'th argument
   Tonen %* expands to the whole set of arguments (%1 ... %9)

   The input sequence "\%" represents a literal "%", and "\\" represents a
   literal "\".  All other character combinations are rendered literally.

   Omitted parameters result in null-string substitutions.

   A Tokens preceeded and followed by % characters are expanded as environment
   variables, and if one isn't found then can be one of several special 
   variables: 
          %DATE%              yyyy/mm/dd
          %TIME%              hh:mm:ss
          %CTIME%             Www Mmm dd hh:mm:ss yyyy
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

void sim_sub_args (char *instr, size_t instr_size, char *do_arg[])
{
char gbuf[CBUFSIZE];
char *ip = instr, *op, *ap, *oend, *istart, *tmpbuf;
char rbuf[CBUFSIZE];
int i;

tmpbuf = malloc(instr_size);
op = tmpbuf;
oend = tmpbuf + instr_size - 2;
while (isspace (*ip))                                   /* skip leading spaces */
    *op++ = *ip++;
istart = ip;
for (; *ip && (op < oend); ) {
    if ((ip [0] == '\\') &&                             /* literal escape? */
        ((ip [1] == '%') || (ip [1] == '\\'))) {        /*   and followed by '%' or '\'? */
        ip++;                                           /* skip '\' */
        *op++ = *ip++;                                  /* copy escaped char */
        }
    else 
        if (*ip == '%') {                               /* sub? */
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
                }
            else {                                      /* environment variable */
                ap = NULL;
                get_glyph_nc (ip+1, gbuf, '%');         /* first try using the literal name */
                ap = getenv(gbuf);
                if (!ap) {
                    get_glyph (ip+1, gbuf, '%');        /* now try using the upcased name */
                    ap = getenv(gbuf);
                    }
                ip += 1 + strlen (gbuf);
                if (*ip == '%') ++ip;
                if (!ap) {
                    time_t now;
                    struct tm *tmnow;

                    time(&now);
                    tmnow = localtime(&now);
                    if (!strcmp ("DATE", gbuf)) {
                        sprintf (rbuf, "%4d/%02d/%02d", tmnow->tm_year+1900, tmnow->tm_mon+1, tmnow->tm_mday);
                        ap = rbuf;
                        }
                    else if (!strcmp ("TIME", gbuf)) {
                        sprintf (rbuf, "%02d:%02d:%02d", tmnow->tm_hour, tmnow->tm_min, tmnow->tm_sec);
                        ap = rbuf;
                        }
                    else if (!strcmp ("CTIME", gbuf)) {
                        strcpy (rbuf, ctime(&now));
                        rbuf[strlen (rbuf)-1] = '\0';    /* remove trailing \n */
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
                }
            if (ap) {                                   /* non-null arg? */
                while (*ap && (op < oend))              /* copy the argument */
                    *op++ = *ap++;
                }
            }
        else
            if (ip == istart) {                         /* at beginning of input? */
                get_glyph (instr, gbuf, 0);             /* substitute initial token */
                ap = getenv(gbuf);                      /* if it is an environment variable name */
                if (!ap) {                              /* nope? */
                    *op++ = *ip++;                      /* press on with literal character */
                    continue;
                    }
                while (*ap && (op < oend))              /* copy the translation */
                    *op++ = *ap++;
                ip += strlen(gbuf);
                }
            else
                *op++ = *ip++;                          /* literal character */
    }
*op = 0;                                                /* term buffer */
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

/* Assert command

   Syntax: ASSERT {<dev>} <reg>{<logical-op><value>}<conditional-op><value>

   If <dev> is not specified, CPU is assumed.  <value> is expressed in the radix
   specified for <reg>.  <logical-op> and <conditional-op> are the same as that
   allowed for examine and deposit search specifications. */

t_stat assert_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE], *gptr, *tptr;
REG *rptr;
uint32 idx;
t_value val;
t_stat r;

cptr = get_sim_opt (CMD_OPT_SW|CMD_OPT_DFT, cptr, &r);  /* get sw, default */
sim_stab.boolop = -1;                                   /* no relational op dflt */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
cptr = get_glyph (cptr, gbuf, 0);                       /* get register */
rptr = find_reg (gbuf, &gptr, sim_dfdev);               /* parse register */
if (!rptr)                                              /* not there */
    return SCPE_NXREG;
if (*gptr == '[') {                                     /* subscript? */
    if (rptr->depth <= 1)                               /* array register? */
        return SCPE_ARG;
    idx = (uint32) strtotv (++gptr, &tptr, 10);         /* convert index */
    if ((gptr == tptr) || (*tptr++ != ']'))
        return SCPE_ARG;
    gptr = tptr;                                        /* update */
    }
else idx = 0;                                           /* not array */
if (idx >= rptr->depth)                                 /* validate subscript */
    return SCPE_SUB;
if (*gptr != 0)                                         /* more? must be search */
    get_glyph (gptr, gbuf, 0);
else {
    if (*cptr == 0)                                     /* must be more */
            return SCPE_2FARG;
    cptr = get_glyph (cptr, gbuf, 0);                   /* get search cond */
    }
if (*cptr != 0)                                         /* must be done */
    return SCPE_2MARG;
if (!get_search (gbuf, rptr->radix, &sim_stab) ||       /* parse condition */
    (sim_stab.boolop == -1))                            /* relational op reqd */
    return SCPE_MISVAL;
val = get_rval (rptr, idx);                             /* get register value */
if (test_search (val, &sim_stab))                       /* test condition */
    return SCPE_OK;
return SCPE_AFAIL;                                      /* condition fails */
}


/* Goto command */

t_stat goto_cmd (int32 flag, char *fcptr)
{
char *cptr, cbuf[CBUFSIZE], gbuf[CBUFSIZE], gbuf1[CBUFSIZE];
long fpos;
int32 saved_do_echo = sim_do_echo;
int32 saved_goto_line = sim_goto_line[sim_do_depth];

if (NULL == sim_gotofile) return SCPE_UNK;              /* only valid inside of do_cmd */
get_glyph (fcptr, gbuf1, 0);
if ('\0' == gbuf1[0]) return SCPE_ARG;                  /* unspecified goto target */
fpos = ftell(sim_gotofile);                             /* Save start position */
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
    while (isspace (*cptr)) ++cptr;                     /* skip blanks */
    cptr = get_glyph (cptr, gbuf, 0);                   /* get label glyph */
    if (0 == strcmp(gbuf, gbuf1)) {
        sim_brk_clract ();                              /* goto defangs current actions */
        sim_do_echo = saved_do_echo;                    /* restore echo mode */
        if (sim_do_echo)                                /* echo if -v */
            printf("%s> %s\n", do_position(), cbuf);
        if (sim_do_echo && sim_log)
            fprintf (sim_log, "%s> %s\n", do_position(), cbuf);
        return SCPE_OK;
        }
    }
sim_do_echo = saved_do_echo;                            /* restore echo mode */
fseek(sim_gotofile, fpos, SEEK_SET);                    /* resture start position */
sim_goto_line[sim_do_depth] = saved_goto_line;          /* restore start line number */
return SCPE_ARG;
}

/* Return command */
/* The return command is invalid unless encountered in a do_cmd context, */
/* and in that context, it is handled as a special case inside of do_cmd() */
/* and not dispatched here, so if we get here a return has been issued from */
/* interactive input */

t_stat return_cmd (int32 flag, char *fcptr)
{
return SCPE_UNK;                                        /* only valid inside of do_cmd */
}

/* Shift command */
/* The shift command is invalid unless encountered in a do_cmd context, */
/* and in that context, it is handled as a special case inside of do_cmd() */
/* and not dispatched here, so if we get here a shift has been issued from */
/* interactive input (it is not valid interactively since it would have to */
/* mess with the program's argv which is owned by the C runtime library */

t_stat shift_cmd (int32 flag, char *fcptr)
{
return SCPE_UNK;                                        /* only valid inside of do_cmd */
}

/* Call command */
/* The call command is invalid unless encountered in a do_cmd context, */
/* and in that context, it is handled as a special case inside of do_cmd() */
/* and not dispatched here, so if we get here a call has been issued from */
/* interactive input */

t_stat call_cmd (int32 flag, char *fcptr)
{
char *cptr, cbuf[CBUFSIZE], gbuf[CBUFSIZE];

if (NULL == sim_gotofile) return SCPE_UNK;              /* only valid inside of do_cmd */
cptr = get_glyph (fcptr, gbuf, 0);
if ('\0' == gbuf[0]) return SCPE_ARG;                   /* unspecified goto target */
sprintf(cbuf, "%s %s", sim_do_filename[sim_do_depth], cptr);
sim_switches |= SWMASK ('O');                           /* inherit ON state and actions */
return do_cmd_label (flag, cbuf, gbuf);
}

/* On command */

t_stat on_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
t_stat cond;

cptr = get_glyph (cptr, gbuf, 0);
if ('\0' == gbuf[0]) return SCPE_ARG;                   /* unspecified condition */
if (0 == strcmp("ERROR", gbuf))
    cond = 0;
else
    if (SCPE_OK != sim_string_to_stat (gbuf, &cond))
        return SCPE_ARG;
if ((NULL == cptr) || ('\0' == *cptr)) {                /* Empty Action */
    free(sim_on_actions[sim_do_depth][cond]);           /* Clear existing condition */
    sim_on_actions[sim_do_depth][cond] = NULL; }
else {
    sim_on_actions[sim_do_depth][cond] = 
        realloc(sim_on_actions[sim_do_depth][cond], 1+strlen(cptr));
    strcpy(sim_on_actions[sim_do_depth][cond], cptr);
    }
return SCPE_OK;
}

/* noop command */
/* The noop command (IGNORE, PROCEED) does nothing */

t_stat noop_cmd (int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
return SCPE_OK;                                         /* we're happy doing nothing */
}

/* Set on/noon routine */

t_stat set_on (int32 flag, char *cptr)
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
        malloc(1+strlen("RETURN"));                     /* be the default action */
    strcpy(sim_on_actions[sim_do_depth][0], "RETURN");
    }
if ((sim_do_depth != 0) && 
    (NULL == sim_on_actions[sim_do_depth][SCPE_AFAIL])) {/* handler set for AFAIL? */
    sim_on_actions[sim_do_depth][SCPE_AFAIL] =          /* No, so make "RETURN" */
        malloc(1+strlen("RETURN"));                     /* be the action */
    strcpy(sim_on_actions[sim_do_depth][SCPE_AFAIL], "RETURN");
    }
return SCPE_OK;
}

/* Set verify/noverify routine */

t_stat set_verify (int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
if (flag == sim_do_echo)                                /* already set correctly? */
    return SCPE_OK;
sim_do_echo = flag;
return SCPE_OK;
}

/* Set message/nomessage routine */

t_stat set_message (int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
if (flag == sim_show_message)                           /* already set correctly? */
    return SCPE_OK;
sim_show_message = flag;
return SCPE_OK;
}

/* Set quiet/noquiet routine */

t_stat set_quiet (int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))                               /* now eol? */
    return SCPE_2MARG;
if (flag == sim_quiet)                                  /* already set correctly? */
    return SCPE_OK;
sim_quiet = flag;
return SCPE_OK;
}

/* Set asynch/noasynch routine */

t_stat sim_set_asynch (int32 flag, char *cptr)
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
                !(uptr->flags & UNIT_BUF) &&            /* not buffered, */
                (uptr->fileref))                        /* real file, */
                if (uptr->io_flush)                     /* unit specific flush routine */
                    uptr->io_flush (uptr);
            }
        }
    }
if (!sim_quiet)
    printf ("Asynchronous I/O %sabled\n", sim_asynch_enabled ? "en" : "dis");
if (sim_log)
    fprintf (sim_log, "Asynchronous I/O %sabled\n", sim_asynch_enabled ? "en" : "dis");
return SCPE_OK;
#else
if (!sim_quiet)
    printf ("Asynchronous I/O is not available in this simulator\n");
if (sim_log)
    fprintf (sim_log, "Asynchronous I/O is not available in this simulator\n");
return SCPE_NOFNC;
#endif
}

/* Show asynch routine */

t_stat sim_show_asynch (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
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

t_stat sim_set_environment (int32 flag, char *cptr)
{
char varname[CBUFSIZE];

if ((!cptr) || (*cptr == 0))                            /* now eol? */
    return SCPE_2FARG;
cptr = get_glyph (cptr, varname, '=');                  /* get environment variable name */
setenv(varname, cptr, 1);
return SCPE_OK;
}

/* Set command */

t_stat set_cmd (int32 flag, char *cptr)
{
uint32 lvl = 0;
t_stat r;
char gbuf[CBUFSIZE], *cvptr, *svptr;
DEVICE *dptr;
UNIT *uptr;
MTAB *mptr;
CTAB *gcmdp;
C1TAB *ctbr, *glbr;

static CTAB set_glob_tab[] = {
    { "CONSOLE", &sim_set_console, 0 },
    { "REMOTE", &sim_set_remote_console, 0 },
    { "BREAK", &brk_cmd, SSH_ST },
    { "DEFAULT", &set_default_cmd, 1 },
    { "NOBREAK", &brk_cmd, SSH_CL },
    { "TELNET", &sim_set_telnet, 0 },                   /* deprecated */
    { "NOTELNET", &sim_set_notelnet, 0 },               /* deprecated */
    { "LOG", &sim_set_logon, 0 },                       /* deprecated */
    { "NOLOG", &sim_set_logoff, 0 },                    /* deprecated */
    { "DEBUG", &sim_set_debon, 0 },                     /* deprecated */
    { "NODEBUG", &sim_set_deboff, 0 },                  /* deprecated */
    { "THROTTLE", &sim_set_throt, 1 },
    { "NOTHROTTLE", &sim_set_throt, 0 },
    { "ASYNCH", &sim_set_asynch, 1 },
    { "NOASYNCH", &sim_set_asynch, 0 },
    { "ENVIRONMENT", &sim_set_environment, 1 },
    { "ON", &set_on, 1 },
    { "NOON", &set_on, 0 },
    { "VERIFY", &set_verify, 1 },
    { "VEBOSE", &set_verify, 1 },
    { "NOVERIFY", &set_verify, 0 },
    { "NOVEBOSE", &set_verify, 0 },
    { "MESSAGE", &set_message, 1 },
    { "NOMESSAGE", &set_message, 0 },
    { "QUIET", &set_quiet, 1 },
    { "NOQUIET", &set_quiet, 0 },
    { "PROMPT", &set_prompt, 0 },
    { NULL, NULL, 0 }
    };

static C1TAB set_dev_tab[] = {
    { "OCTAL", &set_dev_radix, 8 },
    { "DECIMAL", &set_dev_radix, 10 },
    { "HEX", &set_dev_radix, 16 },
    { "ENABLED", &set_dev_enbdis, 1 },
    { "DISABLED", &set_dev_enbdis, 0 },
    { "DEBUG", &set_dev_debug, 1 },
    { "NODEBUG", &set_dev_debug, 0 },
    { NULL, NULL, 0 }
    };

static C1TAB set_unit_tab[] = {
    { "ENABLED", &set_unit_enbdis, 1 },
    { "DISABLED", &set_unit_enbdis, 0 },
    { NULL, NULL, 0 }
    };

GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
cptr = get_glyph (cptr, gbuf, 0);                       /* get glob/dev/unit */

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
    if (sim_dflt_dev->modifiers)
        for (mptr = sim_dflt_dev->modifiers; mptr->mask != 0; mptr++) {
            if (mptr->mstring && (MATCH_CMD (gbuf, mptr->mstring) == 0)) {
                dptr = sim_dflt_dev;
                cptr -= strlen (gbuf) + 1;
                while (isspace(*cptr))
                    ++cptr;
                break;
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
//                else if (mptr->mask & MTAB_VAL) {       /* take a value? */
//                    if (!cvptr) return SCPE_MISVAL;     /* none? error */
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

CTAB *find_ctab (CTAB *tab, char *gbuf)
{
for (; tab->name != NULL; tab++) {
    if (MATCH_CMD (gbuf, tab->name) == 0)
        return tab;
    }
return NULL;
}

C1TAB *find_c1tab (C1TAB *tab, char *gbuf)
{
for (; tab->name != NULL; tab++) {
    if (MATCH_CMD (gbuf, tab->name) == 0)
        return tab;
    }
return NULL;
}

/* Set device data radix routine */

t_stat set_dev_radix (DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
if (cptr)
    return SCPE_ARG;
dptr->dradix = flag & 037;
return SCPE_OK;
}

/* Set device enabled/disabled routine */

t_stat set_dev_enbdis (DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
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

t_stat set_unit_enbdis (DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
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

/* Set device debug enabled/disabled routine */

t_stat set_dev_debug (DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
DEBTAB *dep;

if ((dptr->flags & DEV_DEBUG) == 0)
    return SCPE_NOFNC;
if (cptr == NULL) {                                     /* no arguments? */
    dptr->dctrl = flag;                                 /* disable/enable w/o table */
    if (flag && dptr->debflags) {                       /* enable with table? */
        for (dep = dptr->debflags; dep->name != NULL; dep++)
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
                dptr->dctrl = dptr->dctrl | dep->mask;
            else dptr->dctrl = dptr->dctrl & ~dep->mask;
            break;
            }
        }                                               /* end for */
    if (dep->mask == 0)                                 /* no match? */
        return SCPE_ARG;
    }                                                   /* end while */
return SCPE_OK;
}

/* Show command */

t_stat show_cmd (int32 flag, char *cptr)
{
t_stat r;

cptr = get_sim_opt (CMD_OPT_SW|CMD_OPT_OF, cptr, &r);   /* get sw, ofile */
if (!cptr)                                              /* error? */
    return r;
if (sim_ofile) {                                        /* output file? */
    r = show_cmd_fi (sim_ofile, flag, cptr);            /* do show */
    fclose (sim_ofile);
    }
else {
    r = show_cmd_fi (stdout, flag, cptr);               /* no, stdout, log */
    if (sim_log)
        show_cmd_fi (sim_log, flag, cptr);
    }
return r;
}

t_stat show_cmd_fi (FILE *ofile, int32 flag, char *cptr)
{
uint32 lvl = 0xFFFFFFFF;
char gbuf[CBUFSIZE], *cvptr;
DEVICE *dptr;
UNIT *uptr;
MTAB *mptr;
SHTAB *shtb = NULL, *shptr;

static SHTAB show_glob_tab[] = {
    { "CONFIGURATION", &show_config, 0 },
    { "DEVICES", &show_config, 1 },
    { "FEATURES", &show_config, 2 },
    { "QUEUE", &show_queue, 0 },
    { "TIME", &show_time, 0 },
    { "MODIFIERS", &show_mod_names, 0 },
    { "NAMES", &show_log_names, 0 },
    { "SHOW", &show_show_commands, 0 },
    { "VERSION", &show_version, 1 },
    { "DEFAULT", &show_default, 0 },
    { "CONSOLE", &sim_show_console, 0 },
    { "REMOTE", &sim_show_remote_console, 0 },
    { "BREAK", &show_break, 0 },
    { "LOG", &sim_show_log, 0 },                        /* deprecated */
    { "TELNET", &sim_show_telnet, 0 },                  /* deprecated */
    { "DEBUG", &sim_show_debug, 0 },                    /* deprecated */
    { "THROTTLE", &sim_show_throt, 0 },
    { "ASYNCH", &sim_show_asynch, 0 },
    { "ETHERNET", &eth_show_devices, 0 },
    { "SERIAL", &sim_show_serial, 0 },
    { "MULTIPLEXER", &tmxr_show_open_devices, 0 },
    { "MUX", &tmxr_show_open_devices, 0 },
    { "CLOCKS", &sim_show_timers, 0 },
    { "ON", &show_on, 0 },
    { NULL, NULL, 0 }
    };

static SHTAB show_dev_tab[] = {
    { "RADIX", &show_dev_radix, 0 },
    { "DEBUG", &show_dev_debug, 0 },
    { "MODIFIERS", &show_dev_modifiers, 0 },
    { "NAMES", &show_dev_logicals, 0 },
    { "SHOW", &show_dev_show_commands, 0 },
    { NULL, NULL, 0 }
    };

static SHTAB show_unit_tab[] = {
    { NULL, NULL, 0 }
    };

GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
cptr = get_glyph (cptr, gbuf, 0);                       /* get next glyph */

if ((dptr = find_dev (gbuf))) {                         /* device match? */
    uptr = dptr->units;                                 /* first unit */
    shtb = show_dev_tab;                                /* global table */
    lvl = MTAB_VDV;                                     /* device match */
    }
else if ((dptr = find_unit (gbuf, &uptr))) {            /* unit match? */
    if (uptr == NULL)                                   /* invalid unit */
        return SCPE_NXUN;
    if (uptr->flags & UNIT_DIS)                         /* disabled? */
        return SCPE_UDIS;
    shtb = show_unit_tab;                               /* global table */
    lvl = MTAB_VUN;                                     /* unit match */
    }
else if ((shptr = find_shtab (show_glob_tab, gbuf)))    /* global? */
    return shptr->action (ofile, NULL, NULL, shptr->arg, cptr);
else {
    if (sim_dflt_dev->modifiers)
        for (mptr = sim_dflt_dev->modifiers; mptr->mask != 0; mptr++) {
            if ((((mptr->mask & MTAB_VDV) == MTAB_VDV) &&
                 (mptr->pstring && (MATCH_CMD (gbuf, mptr->pstring) == 0))) ||
                (!(mptr->mask & MTAB_VDV) && (mptr->mstring && (MATCH_CMD (gbuf, mptr->mstring) == 0)))) {
                dptr = sim_dflt_dev;
                lvl = MTAB_VDV;                         /* device match */
                cptr -= strlen (gbuf) + 1;
                while (isspace(*cptr))
                    ++cptr;
                break;
                }
            }
    if (!dptr)
        return SCPE_NXDEV;                              /* no match */
    }

if (*cptr == 0) {                                       /* now eol? */
    return (lvl == MTAB_VDV)?
        show_device (ofile, dptr, 0):
        show_unit (ofile, dptr, uptr, -1);
    }
if (dptr->modifiers == NULL)                            /* any modifiers? */
    return SCPE_NOPARAM;

while (*cptr != 0) {                                    /* do all mods */
    cptr = get_glyph (cptr, gbuf, ',');                 /* get modifier */
    if ((cvptr = strchr (gbuf, '=')))                   /* = value? */
        *cvptr++ = 0;
    for (mptr = dptr->modifiers; mptr->mask != 0; mptr++) {
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
    if (mptr->mask == 0) {                              /* no match? */
        if (shtb && (shptr = find_shtab (shtb, gbuf)))          /* global match? */
            shptr->action (ofile, dptr, uptr, shptr->arg, cptr);
        else return SCPE_ARG;
        }                                               /* end if */
    }                                                   /* end while */
return SCPE_OK;
}

SHTAB *find_shtab (SHTAB *tab, char *gbuf)
{
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

void fprint_capac (FILE *st, DEVICE *dptr, UNIT *uptr)
{
t_addr kval = (uptr->flags & UNIT_BINK)? 1024: 1000;
t_addr mval = kval * kval;
t_addr psize = uptr->capac;
char scale, width;

if (dptr->flags & DEV_SECTORS) {
    kval = kval / 512;
    mval = mval / 512;
    }
if ((dptr->dwidth / dptr->aincr) > 8)
    width = 'W';
else width = 'B';
if (uptr->capac < (kval * 10))
    scale = 0;
else if (uptr->capac < (mval * 10)) {
    scale = 'K';
    psize = psize / kval;
    }
else {
    scale = 'M';
    psize = psize / mval;
    }
fprint_val (st, (t_value) psize, 10, T_ADDR_W, PV_LEFT);
if (scale)
    fputc (scale, st);
fputc (width, st);
return;
}

/* Show <global name> processors  */

t_stat show_version (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
int32 vmaj = SIM_MAJOR, vmin = SIM_MINOR, vpat = SIM_PATCH, vdelt = SIM_DELTA;

if (cptr && (*cptr != 0))
    return SCPE_2MARG;
fprintf (st, "%s simulator V%d.%d-%d", sim_name, vmaj, vmin, vpat);
if (vdelt)
    fprintf (st, " delta %d", vdelt);
#if defined (SIM_VERSION_MODE)
fprintf (st, " %s", SIM_VERSION_MODE);
#endif
if (flag) {
    uint32 idle_capable, os_tick_size;

    fprintf (st, "\n\tSimulator Framework Capabilities:");
    fprintf (st, "\n\t\t%s", sim_si64);
    fprintf (st, "\n\t\t%s", sim_sa64);
    fprintf (st, "\n\t\t%s", sim_snet);
    idle_capable = sim_timer_idle_capable (&os_tick_size);
    fprintf (st, "\n\t\tIdle/Throttling support is %savailable", ((idle_capable == 0) ? "NOT " : ""));
    if (sim_disk_vhd_support())
        fprintf (st, "\n\t\tVirtual Hard Disk (VHD) support");
    if (sim_disk_raw_support())
        fprintf (st, "\n\t\tRAW disk and CD/DVD ROM support");
#if defined (SIM_ASYNCH_IO)
    fprintf (st, "\n\t\tAsynchronous I/O support");
#endif
#if defined (SIM_ASYNCH_MUX)
    fprintf (st, "\n\t\tAsynchronous Multiplexer support");
#endif
#if defined (SIM_ASYNCH_CLOCKS)
    fprintf (st, "\n\t\tAsynchronous Clock support");
#endif
    fprintf (st, "\n\tHost Platform:");
#if defined (__clang_version__)
    fprintf (st, "\n\t\tCompiler: clang %s", __clang_version__);
#elif defined (__GNUC__) && defined (__VERSION__)
    fprintf (st, "\n\t\tCompiler: GCC %s", __VERSION__);
#elif defined (_MSC_FULL_VER) && defined (_MSC_BUILD)
    fprintf (st, "\n\t\tCompiler: Microsoft Visual C++ %d.%02d.%05d.%02d", _MSC_FULL_VER/10000000, (_MSC_FULL_VER/100000)%100, _MSC_FULL_VER%100000, _MSC_BUILD);
#elif defined (__DECC_VER)
    fprintf (st, "\n\t\tCompiler: DEC C %c%d.%d-%03d", ("T SV")[((__DECC_VER/10000)%10)-6], __DECC_VER/10000000, (__DECC_VER/100000)%100, __DECC_VER%10000);
#elif defined (SIM_COMPILER)
#define S_xstr(a) S_str(a)
#define S_str(a) #a
    fprintf (st, "\n\t\tCompiler: %s", S_xstr(SIM_COMPILER));
#undef S_str
#undef S_xstr
#endif
#if defined (__DATE__) && defined (__TIME__)
    fprintf (st, "\n\t\tSimulator Compiled: %s at %s", __DATE__, __TIME__);
#endif
    fprintf (st, "\n\t\tMemory Access: %s Endian", sim_end ? "Little" : "Big");
    fprintf (st, "\n\t\tMemory Pointer Size: %d bits", (int)sizeof(dptr)*8);
    fprintf (st, "\n\t\t%s", sim_toffset_64 ? "Large File (>2GB) support" : "No Large File support");
#if defined (USE_SIM_VIDEO)
    fprintf (st, "\n\t\tSDL Video support: %s", vid_version());
#endif
    fprintf (st, "\n\t\tOS clock tick size: %dms", os_tick_size);
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
        fprintf (st, "\n\t\tOS: OpenVMS %s %s", arch, __VMS_VERSION);
        }
#elif defined(_WIN32)
    fprintf (st, "\n\t\tOS: Windows: ");
    fflush (st);
    system ("ver");
    system ("echo \t\t%PROCESSOR_IDENTIFIER% - %PROCESSOR_ARCHITECTURE%-%PROCESSOR_ARCHITEW6432%");
#else
    fprintf (st, "\n\t\tOS: ");
    fflush (st);
    system ("uname -a");
#endif
    }
#if defined(SIM_GIT_COMMIT_ID)
#define S_xstr(a) S_str(a)
#define S_str(a) #a
fprintf (st, "%sgit commit id: %8.8s", flag ? "\n        " : "        ", S_xstr(SIM_GIT_COMMIT_ID));
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

t_stat show_config (FILE *st, DEVICE *dnotused, UNIT *unotused, int32 flag, char *cptr)
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
return SCPE_OK;
}

t_stat show_log_names (FILE *st, DEVICE *dnotused, UNIT *unotused, int32 flag, char *cptr)
{
int32 i;
DEVICE *dptr;

if (cptr && (*cptr != 0))
    return SCPE_2MARG;
for (i = 0; (dptr = sim_devices[i]) != NULL; i++)
    show_dev_logicals (st, dptr, NULL, 1, cptr);
return SCPE_OK;
}

t_stat show_dev_logicals (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
if (dptr->lname)
    fprintf (st, "%s -> %s\n", dptr->lname, dptr->name);
else if (!flag)
    fputs ("no logical name assigned\n", st);
return SCPE_OK;
}

t_stat show_queue (FILE *st, DEVICE *dnotused, UNIT *unotused, int32 flag, char *cptr)
{
DEVICE *dptr;
UNIT *uptr;
int32 accum;

if (cptr && (*cptr != 0))
    return SCPE_2MARG;
if (sim_clock_queue == QUEUE_LIST_END)
    fprintf (st, "%s event queue empty, time = %.0f, executing %.0f instructios/sec\n",
             sim_name, sim_time, sim_timer_inst_per_sec ());
else {
    fprintf (st, "%s event queue status, time = %.0f, executing %.0f instructions/sec\n",
             sim_name, sim_time, sim_timer_inst_per_sec ());
    accum = 0;
    for (uptr = sim_clock_queue; uptr != QUEUE_LIST_END; uptr = uptr->next) {
        if (uptr == &sim_step_unit)
            fprintf (st, "  Step timer");
        else if ((dptr = find_dev_from_unit (uptr)) != NULL) {
            fprintf (st, "  %s", sim_dname (dptr));
            if (dptr->numunits > 1)
                fprintf (st, " unit %d", (int32) (uptr - dptr->units));
            }
        else fprintf (st, "  Unknown");
        fprintf (st, " at %d\n", accum + uptr->time);
        accum = accum + uptr->time;
        }
    }
#if defined (SIM_ASYNCH_IO)
pthread_mutex_lock (&sim_timer_lock);
if (sim_wallclock_queue == QUEUE_LIST_END)
    fprintf (st, "%s wall clock event queue empty, time = %.0f\n",
             sim_name, sim_time);
else {
    fprintf (st, "%s wall clock event queue status, time = %.0f\n",
             sim_name, sim_time);
    for (uptr = sim_wallclock_queue; uptr != QUEUE_LIST_END; uptr = uptr->a_next) {
        if ((dptr = find_dev_from_unit (uptr)) != NULL) {
            fprintf (st, "  %s", sim_dname (dptr));
            if (dptr->numunits > 1)
                fprintf (st, " unit %d", (int32) (uptr - dptr->units));
            }
        else fprintf (st, "  Unknown");
        fprintf (st, " after ");
        fprint_val (st, (t_value)uptr->a_usec_delay, 10, 0, PV_RCOMMA);
        fprintf (st, " usec\n");
        }
    }
if (sim_clock_cosched_queue != QUEUE_LIST_END) {
    fprintf (st, "%s clock (%s) co-schedule event queue status, time = %.0f\n",
             sim_name, sim_uname(sim_clock_unit), sim_time);
    for (uptr = sim_clock_cosched_queue; uptr != QUEUE_LIST_END; uptr = uptr->a_next) {
        if ((dptr = find_dev_from_unit (uptr)) != NULL) {
            fprintf (st, "  %s", sim_dname (dptr));
            if (dptr->numunits > 1)
                fprintf (st, " unit %d", (int32) (uptr - dptr->units));
            }
        else fprintf (st, "  Unknown");
        fprintf (st, "\n");
        }
    }
pthread_mutex_unlock (&sim_timer_lock);
pthread_mutex_lock (&sim_asynch_lock);
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
#endif /* SIM_ASYNCH_IO */
return SCPE_OK;
}

t_stat show_time (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
if (cptr && (*cptr != 0))
    return SCPE_2MARG;
fprintf (st, "Time:\t%.0f\n", sim_time);
return SCPE_OK;
}

t_stat show_break (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
t_stat r;

if (cptr && (*cptr != 0))
    r = ssh_break (st, cptr, 1);  /* more? */
else r = sim_brk_showall (st, sim_switches);
return r;
}

t_stat show_dev_radix (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprintf (st, "Radix=%d\n", dptr->dradix);
return SCPE_OK;
}

t_stat show_dev_debug (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
int32 any = 0;
DEBTAB *dep;

if (dptr->flags & DEV_DEBUG) {
    if (dptr->dctrl == 0)
        fputs ("Debugging disabled", st);
    else if (dptr->debflags == NULL)
        fputs ("Debugging enabled", st);
    else {
        fputs ("Debug=", st);
        for (dep = dptr->debflags; dep->name != NULL; dep++) {
            if (dptr->dctrl & dep->mask) {
                if (any)
                    fputc (';', st);
                fputs (dep->name, st);
                any = 1;
                }
            }
        }
    fputc ('\n', st);
    return SCPE_OK;
    }
else return SCPE_NOFNC;
}

/* Show On actions */

t_stat show_on (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
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
            fprintf(st, "    on %5d    %s\n", i, sim_on_actions[lvl][i]); }
    for (i=SCPE_BASE; i<=SCPE_MAX_ERR; ++i) {
        if (sim_on_actions[lvl][i])
            fprintf(st, "    on %-5s    %s\n", scp_errors[i-SCPE_BASE].code, sim_on_actions[lvl][i]); }
    if (sim_on_actions[lvl][0])
        fprintf(st, "    on ERROR    %s\n", sim_on_actions[lvl][0]);
    fprintf(st, "\n");
    }
if (sim_on_inherit)
    fprintf(st, "on state and actions are inherited by nested do commands and subroutines\n");
return SCPE_OK;
}

/* Show modifiers */

t_stat show_mod_names (FILE *st, DEVICE *dnotused, UNIT *unotused, int32 flag, char *cptr)
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

t_stat show_dev_modifiers (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprint_set_help (st, dptr);
return SCPE_OK;
}

t_stat show_all_mods (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, int32 *toks)
{
MTAB *mptr;

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
        fprint_sep (st, toks);
        show_one_mod (st, dptr, uptr, mptr, NULL, 0);
        }
    }
return SCPE_OK;
}

t_stat show_one_mod (FILE *st, DEVICE *dptr, UNIT *uptr, MTAB *mptr,
    char *cptr, int32 flag)
{
//t_value val;

if (mptr->disp)
    mptr->disp (st, uptr, mptr->match, cptr? cptr: mptr->desc);
//else if ((mptr->mask & MTAB_XTD) && (mptr->mask & MTAB_VAL)) {
//    REG *rptr = (REG *) mptr->desc;
//    fprintf (st, "%s=", mptr->pstring);
//    val = get_rval (rptr, 0);
//    fprint_val (st, val, rptr->radix, rptr->width,
//        rptr->flags & REG_FMT);
//    }
else fputs (mptr->pstring, st);
if (flag && !((mptr->mask & MTAB_XTD) && MODMASK(mptr,MTAB_NMO)))
    fputc ('\n', st);
return SCPE_OK;
}

/* Show show commands */

t_stat show_show_commands (FILE *st, DEVICE *dnotused, UNIT *unotused, int32 flag, char *cptr)
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

t_stat show_dev_show_commands (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprint_show_help (st, dptr);
return SCPE_OK;
}

/* Show/change the current working directiory commands */

t_stat show_default (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
char buffer[PATH_MAX];
char *wd = getcwd(buffer, PATH_MAX);
fprintf (st, "%s\n", wd);
return SCPE_OK;
}

t_stat set_default_cmd (int32 flg, char *cptr)
{
if (sim_is_running)
    return SCPE_INVREM;
if ((!cptr) || (*cptr == 0))
    return SCPE_2FARG;
sim_trim_endspc(cptr);
if (chdir(cptr) != 0) {
    printf("Unable to change to: %s\n", cptr);
    return SCPE_IOERR & SCPE_NOMESSAGE;
    } 
return SCPE_OK;
}

t_stat pwd_cmd (int32 flg, char *cptr)
{
return show_cmd (0, "DEFAULT");
}

#if defined (_WIN32)

t_stat dir_cmd (int32 flg, char *cptr)
{
HANDLE hFind;
WIN32_FIND_DATAA File;
struct stat filestat;
char WildName[PATH_MAX + 1];

if (*cptr == '\0')
    cptr = "./*";
if ((!stat (cptr, &filestat)) && (filestat.st_mode & S_IFDIR)) {
    sprintf (WildName, "%s%c*", cptr, strchr (cptr, '/') ? '/' : '\\');
    cptr = WildName;
    }
if ((hFind =  FindFirstFileA (cptr, &File)) != INVALID_HANDLE_VALUE) {
    t_int64 FileSize, TotalSize = 0;
    int DirCount = 0, FileCount = 0;
    char DirName[PATH_MAX + 1], FileName[PATH_MAX + 1];
    char *c, pathsep = '/';
    struct tm *local;

    GetFullPathNameA(cptr, sizeof(DirName), DirName, &c);
    c = strrchr(DirName, pathsep);
    if (NULL == c) {
        pathsep = '\\';
        c = strrchr(cptr, pathsep);
        }
    if (c) {
        memcpy(DirName, cptr, c - cptr);
        DirName[c - cptr] = '\0';
        }
    else {
        getcwd(DirName, PATH_MAX);
        }
    printf (" Directory of %s\n\n", DirName);
    if (sim_log)
        fprintf (sim_log, " Directory of %s\n\n", DirName);
    do {
        FileSize = (((t_int64)(File.nFileSizeHigh)) << 32) | File.nFileSizeLow;
        sprintf (FileName, "%s%c%s", DirName, pathsep, File.cFileName);
        stat (FileName, &filestat);
        local = localtime (&filestat.st_mtime);
        printf ("%02d/%02d/%04d  %02d:%02d %s ", local->tm_mon+1, local->tm_mday, 1900+local->tm_year, local->tm_hour%12, local->tm_min, (local->tm_hour >= 12) ? "PM" : "AM");
        if (sim_log)
            fprintf (sim_log, "%02d/%02d/%04d  %02d:%02d %s ", local->tm_mon+1, local->tm_mday, 1900+local->tm_year, local->tm_hour%12, local->tm_min, (local->tm_hour >= 12) ? "PM" : "AM");
        if (filestat.st_mode & S_IFDIR) {
            ++DirCount;
            printf ("   <DIR>         ");
            if (sim_log)
                fprintf (sim_log, "   <DIR>         ");
            }
        else {
            if (filestat.st_mode & S_IFREG) {
                ++FileCount;
                fprint_val (stdout, (t_value) FileSize, 10, 17, PV_RCOMMA);
                if (sim_log)
                    fprint_val (sim_log, (t_value) FileSize, 10, 17, PV_RCOMMA);
                TotalSize += FileSize;
                }
            else {
                printf ("%17s", "");
                if (sim_log)
                    fprintf (sim_log, "%17s", "");
                }
            }
        printf (" %s\n", File.cFileName);
        if (sim_log)
            fprintf (sim_log, " %s\n", File.cFileName);
        } while (FindNextFile (hFind, &File));
    printf ("%16d File(s)", FileCount);
    fprint_val (stdout, (t_value) TotalSize, 10, 15, PV_RCOMMA);
    printf (" bytes\n");
    printf ("%16d Dir(s)\n", DirCount);
    if (sim_log) {
        fprintf (sim_log, "%16d File(s)", FileCount);
        fprint_val (sim_log, (t_value) TotalSize, 10, 15, PV_RCOMMA);
        fprintf (sim_log, " bytes\n");
        fprintf (sim_log, "%16d Dir(s)\n", DirCount);
        }
    FindClose (hFind);
    }
else {
    printf ("Can't list files for %s\n", cptr);
    if (sim_log)
        fprintf (sim_log, "Can't list files for %s\n", cptr);
    return SCPE_ARG;
    }
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

t_stat dir_cmd (int32 flg, char *cptr)
{
#if defined (HAVE_GLOB)
glob_t  paths;
#else
DIR *dir;
#endif
struct stat filestat;
char *c;
char DirName[PATH_MAX + 1], WholeName[PATH_MAX + 1], WildName[PATH_MAX + 1];

if (*cptr == '\0')
    cptr = "./*";
strcpy (WildName, cptr);
cptr = WildName;
while (strlen(WildName) && isspace(WildName[strlen(WildName)-1]))
    WildName[strlen(WildName)-1] = '\0';
if ((!stat (WildName, &filestat)) && (filestat.st_mode & S_IFDIR))
    strcat (WildName, "/*");
if ((*cptr != '/') || (0 == memcmp (cptr, "./", 2)) || (0 == memcmp (cptr, "../", 3))) {
#if defined (VMS)
    getcwd (WholeName, PATH_MAX, 0);
#else
    getcwd (WholeName, PATH_MAX);
#endif
    strcat (WholeName, "/");
    strcat (WholeName, cptr);
    while (strlen(WholeName) && isspace(WholeName[strlen(WholeName)-1]))
        WholeName[strlen(WholeName)-1] = '\0';
    }
while ((c = strstr (WholeName, "/./")))
    strcpy (c + 1, c + 3);
while ((c = strstr (WholeName, "//")))
    strcpy (c + 1, c + 2);
while ((c = strstr (WholeName, "/../"))) {
    char *c1;
    c1 = c - 1;
    while ((c1 >= WholeName) && (*c1 != '/'))
        c1 = c1 - 1;
    strcpy (c1, c + 3);
    while (0 == memcmp (WholeName, "/../", 4))
        strcpy (WholeName, WholeName+3);
    }
c = strrchr (WholeName, '/');
if (c) {
    memcpy (DirName, WholeName, c-WholeName);
    DirName[c-WholeName] = '\0';
    }
else
#if defined (VMS)
    getcwd (WholeName, PATH_MAX, 0);
#else
    getcwd (WholeName, PATH_MAX);
#endif
cptr = WholeName;
#if defined (HAVE_GLOB)
memset (&paths, 0, sizeof(paths));
if (0 == glob (cptr, 0, NULL, &paths)) {
#else
dir = opendir(DirName[0] ? DirName : "/.");
if (dir) {
    struct dirent *ent;
#endif
    t_offset FileSize, TotalSize = 0;
    int DirCount = 0, FileCount = 0;
    char FileName[PATH_MAX + 1], *MatchName;
    char *c;
    struct tm *local;
    int i;

    MatchName = 1 + strrchr (cptr, '/');
    printf (" Directory of %s\n\n", DirName[0] ? DirName : "/");
    if (sim_log)
        fprintf (sim_log, " Directory of %s\n\n", DirName[0] ? DirName : "/");
#if defined (HAVE_GLOB)
    for (i=0; i<paths.gl_pathc; i++) {
        sprintf (FileName, "%s", paths.gl_pathv[i]);
#else
    while ((ent = readdir (dir))) {
#if defined (HAVE_FNMATCH)
        if (fnmatch(MatchName, ent->d_name, 0))
            continue;
#endif
        sprintf (FileName, "%s/%s", DirName, ent->d_name);
#endif
        stat (FileName, &filestat);
        local = localtime (&filestat.st_mtime);
        printf ("%02d/%02d/%04d  %02d:%02d %s ", local->tm_mon+1, local->tm_mday, 1900+local->tm_year, local->tm_hour%12, local->tm_min, (local->tm_hour >= 12) ? "PM" : "AM");
        if (sim_log)
            fprintf (sim_log, "%02d/%02d/%04d  %02d:%02d %s ", local->tm_mon+1, local->tm_mday, 1900+local->tm_year, local->tm_hour%12, local->tm_min, (local->tm_hour >= 12) ? "PM" : "AM");
        if (filestat.st_mode & S_IFDIR) {
            ++DirCount;
            printf ("   <DIR>         ");
            if (sim_log)
                fprintf (sim_log, "   <DIR>         ");
            }
        else {
            if (filestat.st_mode & S_IFREG) {
                ++FileCount;
                FileSize = sim_fsize_name_ex (FileName);
                fprint_val (stdout, (t_value) FileSize, 10, 17, PV_RCOMMA);
                if (sim_log)
                    fprint_val (sim_log, (t_value) FileSize, 10, 17, PV_RCOMMA);
                TotalSize += FileSize;
                }
            else {
                printf ("%17s", "");
                if (sim_log)
                    fprintf (sim_log, "%17s", "");
                }
            }
        c = strrchr (FileName, '/');
        printf (" %s\n", c ? c + 1 : FileName);
        if (sim_log)
            fprintf (sim_log, " %s\n", c ? c + 1 : FileName);
        }
    if (FileCount) {
        printf ("%16d File(s)", FileCount);
        fprint_val (stdout, (t_value) TotalSize, 10, 15, PV_RCOMMA);
        printf (" bytes\n");
        printf ("%16d Dir(s)\n", DirCount);
        if (sim_log) {
            fprintf (sim_log, "%16d File(s)", FileCount);
            fprint_val (sim_log, (t_value) TotalSize, 10, 15, PV_RCOMMA);
            fprintf (sim_log, " bytes\n");
            fprintf (sim_log, "%16d Dir(s)\n", DirCount);
            }
        }
    else {
        printf ("File Not Found\n");
        if (sim_log)
            fprintf (sim_log, "File Not Found\n");
        }
#if defined (HAVE_GLOB)
    globfree (&paths);
#else
    closedir (dir);
#endif
    }
else {
    printf ("Can't list files for %s\n", cptr);
    if (sim_log)
        fprintf (sim_log, "Can't list files for %s\n", cptr);
    return SCPE_ARG;
    }
return SCPE_OK;
}

#endif /* !defined(_WIN32) */

/* Breakpoint commands */

t_stat brk_cmd (int32 flg, char *cptr)
{
GET_SWITCHES (cptr);                                    /* get switches */
return ssh_break (NULL, cptr, flg);                     /* call common code */
}

t_stat ssh_break (FILE *st, char *cptr, int32 flg)
{
char gbuf[CBUFSIZE], *tptr, *t1ptr, *aptr;
DEVICE *dptr = sim_dflt_dev;
UNIT *uptr = dptr->units;
t_stat r;
t_addr lo, hi, max = uptr->capac - 1;
int32 cnt;

if (sim_brk_types == 0)
    return SCPE_NOFNC;
if ((dptr == NULL) || (uptr == NULL))
    return SCPE_IERR;
if ((aptr = strchr (cptr, ';'))) {                      /* ;action? */
    if (flg != SSH_ST)                                  /* only on SET */
        return SCPE_ARG;
    *aptr++ = 0;                                        /* separate strings */
    }
if (*cptr == 0) {                                       /* no argument? */
    lo = (t_addr) get_rval (sim_PC, 0);                 /* use PC */
    return ssh_break_one (st, flg, lo, 0, aptr);
    }
while (*cptr) {
    cptr = get_glyph (cptr, gbuf, ',');
    tptr = get_range (dptr, gbuf, &lo, &hi, dptr->aradix, max, 0);
    if (tptr == NULL)
        return SCPE_ARG;
    if (*tptr == '[') {
        cnt = (int32) strtotv (tptr + 1, &t1ptr, 10);
        if ((tptr == t1ptr) || (*t1ptr != ']') || (flg != SSH_ST))
            return SCPE_ARG;
        tptr = t1ptr + 1;
        }
    else cnt = 0;
    if (*tptr != 0)
        return SCPE_ARG;
    if ((lo == 0) && (hi == max)) {
        if (flg == SSH_CL)
            sim_brk_clrall (sim_switches);
        else if (flg == SSH_SH)
            sim_brk_showall (st, sim_switches);
        else return SCPE_ARG;
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

t_stat ssh_break_one (FILE *st, int32 flg, t_addr lo, int32 cnt, char *aptr)
{
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

t_stat reset_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
DEVICE *dptr;

GET_SWITCHES (cptr);                                    /* get switches */
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
return r;
}

/* Load and dump commands

   lo[ad] filename {arg}        load specified file
   du[mp] filename {arg}        dump to specified file
*/

t_stat load_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
FILE *loadfile;
t_stat reason;

GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
cptr = get_glyph_nc (cptr, gbuf, 0);                    /* get file name */
loadfile = sim_fopen (gbuf, flag? "wb": "rb");          /* open for wr/rd */
if (loadfile == NULL)
    return SCPE_OPENERR;
GET_SWITCHES (cptr);                                    /* get switches */
reason = sim_load (loadfile, cptr, gbuf, flag);         /* load or dump */
fclose (loadfile);
return reason;
}

/* Attach command

   at[tach] unit file   attach specified unit to file
*/

t_stat attach_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE];
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
if (uptr->flags & UNIT_ATT)                             /* already attached? */
    if (!(uptr->dynflags & UNIT_ATTMULT) &&             /* and only single attachable */
        !(dptr->flags & DEV_DONTAUTO)) {                /* and auto detachable */
        r = scp_detach_unit (dptr, uptr);               /* detach it */
        if (r != SCPE_OK)                               /* error? */
            return r;
        }
    else
        if (!(uptr->dynflags & UNIT_ATTMULT))
            return SCPE_ALATT;                          /* Already attached */
sim_trim_endspc (cptr);                                 /* trim trailing spc */
return scp_attach_unit (dptr, uptr, cptr);              /* attach */
}

/* Call device-specific or file-oriented attach unit routine */

t_stat scp_attach_unit (DEVICE *dptr, UNIT *uptr, char *cptr)
{
if (dptr->attach != NULL)                               /* device routine? */
    return dptr->attach (uptr, cptr);                   /* call it */
return attach_unit (uptr, cptr);                        /* no, std routine */
}

/* Attach unit to file */

t_stat attach_unit (UNIT *uptr, char *cptr)
{
DEVICE *dptr;

if (uptr->flags & UNIT_DIS)                             /* disabled? */
    return SCPE_UDIS;
if (!(uptr->flags & UNIT_ATTABLE))                      /* not attachable? */
    return SCPE_NOATT;
if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return SCPE_NOATT;
uptr->filename = (char *) calloc (CBUFSIZE, sizeof (char)); /* alloc name buf */
if (uptr->filename == NULL)
    return SCPE_MEM;
strncpy (uptr->filename, cptr, CBUFSIZE);               /* save name */
if (sim_switches & SWMASK ('R')) {                      /* read only? */
    if ((uptr->flags & UNIT_ROABLE) == 0)               /* allowed? */
        return attach_err (uptr, SCPE_NORO);            /* no, error */
    uptr->fileref = sim_fopen (cptr, "rb");             /* open rd only */
    if (uptr->fileref == NULL)                          /* open fail? */
        return attach_err (uptr, SCPE_OPENERR);         /* yes, error */
    uptr->flags = uptr->flags | UNIT_RO;                /* set rd only */
    if (!sim_quiet)
        printf ("%s: unit is read only\n", sim_dname (dptr));
    }
else {
    if (sim_switches & SWMASK ('N')) {                  /* new file only? */
        uptr->fileref = sim_fopen (cptr, "wb+");        /* open new file */
        if (uptr->fileref == NULL)                      /* open fail? */
            return attach_err (uptr, SCPE_OPENERR);     /* yes, error */
        if (!sim_quiet)
            printf ("%s: creating new file\n", sim_dname (dptr));
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
                if (!sim_quiet)
                    printf ("%s: unit is read only\n", sim_dname (dptr));
                }
            else {                                      /* doesn't exist */
                if (sim_switches & SWMASK ('E'))        /* must exist? */
                    return attach_err (uptr, SCPE_OPENERR); /* yes, error */
                uptr->fileref = sim_fopen (cptr, "wb+");/* open new file */
                if (uptr->fileref == NULL)              /* open fail? */
                    return attach_err (uptr, SCPE_OPENERR); /* yes, error */
                if (!sim_quiet)
                    printf ("%s: creating new file\n", sim_dname (dptr));
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
    if (!sim_quiet) printf ("%s: buffering file in memory\n", sim_dname (dptr));
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

t_stat detach_cmd (int32 flag, char *cptr)
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
if (uptr == NULL)                                        /* valid unit? */
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
        return SCPE_NOATT;                              /* complain */
    }
if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return SCPE_OK;
if (uptr->flags & UNIT_BUF) {
    uint32 cap = (uptr->hwmark + dptr->aincr - 1) / dptr->aincr;
    if (uptr->hwmark && ((uptr->flags & UNIT_RO) == 0)) {
        if (!sim_quiet)
            printf ("%s: writing buffer to file\n", sim_dname (dptr));
        rewind (uptr->fileref);
        sim_fwrite (uptr->filebuf, SZ_D (dptr), cap, uptr->fileref);
        if (ferror (uptr->fileref))
            perror ("I/O error");
        }
    if (uptr->flags & UNIT_MUSTBUF) {                   /* dyn alloc? */
        free (uptr->filebuf);                           /* free buf */
        uptr->filebuf = NULL;
        }
    uptr->flags = uptr->flags & ~UNIT_BUF;
    }
uptr->flags = uptr->flags & ~(UNIT_ATT | UNIT_RO);
free (uptr->filename);
uptr->filename = NULL;
if (fclose (uptr->fileref) == EOF)
    return SCPE_IOERR;
return SCPE_OK;
}

/* Assign command

   as[sign] device name assign logical name to device
*/

t_stat assign_cmd (int32 flag, char *cptr)
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

t_stat assign_device (DEVICE *dptr, char *cptr)
{
dptr->lname = (char *) calloc (CBUFSIZE, sizeof (char));
if (dptr->lname == NULL)
    return SCPE_MEM;
strncpy (dptr->lname, cptr, CBUFSIZE);
return SCPE_OK;
}

/* Deassign command

   dea[ssign] device    deassign logical name
*/

t_stat deassign_cmd (int32 flag, char *cptr)
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
if (dptr->lname)
    free (dptr->lname);
dptr->lname = NULL;
return SCPE_OK;
}

/* Get device display name */

char *sim_dname (DEVICE *dptr)
{
return (dptr->lname? dptr->lname: dptr->name);
}

/* Get unit display name */

char *sim_uname (UNIT *uptr)
{
DEVICE *d = find_dev_from_unit(uptr);
static AIO_TLS char uname[CBUFSIZE];

if (!d)
    return "";
if (d->numunits == 1)
    return sim_dname (d);
sprintf (uname, "%s%d", sim_dname (d), (int)(uptr-d->units));
return uname;
}

/* Save command

   sa[ve] filename              save state to specified file
*/

t_stat save_cmd (int32 flag, char *cptr)
{
FILE *sfile;
t_stat r;
GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
sim_trim_endspc (cptr);
if ((sfile = sim_fopen (cptr, "wb")) == NULL)
    return SCPE_OPENERR;
r = sim_save (sfile);
fclose (sfile);
return r;
}

t_stat sim_save (FILE *sfile)
{
void *mbuf;
int32 l, t;
uint32 i, j;
t_addr k, high;
t_value val;
t_stat r;
t_bool zeroflg;
size_t sz;
DEVICE *dptr;
UNIT *uptr;
REG *rptr;

#define WRITE_I(xx) sim_fwrite (&(xx), sizeof (xx), 1, sfile)

fprintf (sfile, "%s\n%s\n%s\n%s\n%s\n%.0f\n",
    save_vercur,                                        /* [V2.5] save format */
    sim_name,                                           /* sim name */
    sim_si64, sim_sa64, sim_snet,                       /* [V3.5] options */
    sim_time);                                          /* [V3.2] sim time */
WRITE_I (sim_rtime);                                    /* [V2.6] sim rel time */

for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {     /* loop thru devices */
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
        WRITE_I (uptr->capac);                          /* [V3.5] capacity */
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

t_stat restore_cmd (int32 flag, char *cptr)
{
FILE *rfile;
t_stat r;

GET_SWITCHES (cptr);                                    /* get switches */
if (*cptr == 0)                                         /* must be more */
    return SCPE_2FARG;
sim_trim_endspc (cptr);
if ((rfile = sim_fopen (cptr, "rb")) == NULL)
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
t_bool v35, v32;
DEVICE *dptr;
UNIT *uptr;
REG *rptr;
struct stat rstat;
t_bool force_restore = sim_switches & SWMASK ('F');

#define READ_S(xx) if (read_line ((xx), sizeof(xx), rfile) == NULL) \
    return SCPE_IOERR;
#define READ_I(xx) if (sim_fread (&xx, sizeof (xx), 1, rfile) == 0) \
    return SCPE_IOERR;

fstat (fileno (rfile), &rstat);
READ_S (buf);                                           /* [V2.5+] read version */
v35 = v32 = FALSE;
if (strcmp (buf, save_vercur) == 0)                     /* version 3.5? */
    v35 = v32 = TRUE;
else if (strcmp (buf, save_ver32) == 0)                 /* version 3.2? */
    v32 = TRUE;
else if (strcmp (buf, save_ver30) != 0) {               /* version 3.0? */
    printf ("Invalid file version: %s\n", buf);
    if (sim_log)
        fprintf (sim_log, "Invalid file version: %s\n", buf);
    return SCPE_INCOMP;
    }
READ_S (buf);                                           /* read sim name */
if (strcmp (buf, sim_name)) {                           /* name match? */
    printf ("Wrong system type: %s\n", buf);
    if (sim_log)
        fprintf (sim_log, "Wrong system type: %s\n", buf);
    return SCPE_INCOMP;
    }
if (v35) {                                              /* [V3.5+] options */
    READ_S (buf);                                       /* integer size */
    if (strcmp (buf, sim_si64) != 0) {
        printf ("Incompatible integer size, save file = %s\n", buf);
        if (sim_log)
            fprintf (sim_log, "Incompatible integer size, save file = %s\n", buf);
        return SCPE_INCOMP;
        }
    READ_S (buf);                                       /* address size */
    if (strcmp (buf, sim_sa64) != 0) {
        printf ("Incompatible address size, save file = %s\n", buf);
        if (sim_log)
            fprintf (sim_log, "Incompatible address size, save file = %s\n", buf);
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
detach_all (0, 0);                                      /* Detach everything to start from a consistent state */
for ( ;; ) {                                            /* device loop */
    READ_S (buf);                                       /* read device name */
    if (buf[0] == 0)                                    /* last? */
        break;
    if ((dptr = find_dev (buf)) == NULL) {              /* locate device */
        printf ("Invalid device name: %s\n", buf);
        if (sim_log)
            fprintf (sim_log, "Invalid device name: %s\n", buf);
        return SCPE_INCOMP;
        }
    READ_S (buf);                                       /* [V3.0+] logical name */
    deassign_device (dptr);                             /* delete old name */
    if ((buf[0] != 0) && 
        ((r = assign_device (dptr, buf)) != SCPE_OK))
        return r;
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
            printf ("Invalid unit number: %s%d\n", sim_dname (dptr), unitno);
            if (sim_log)
                fprintf (sim_log, "Invalid unit number: %s%d\n", sim_dname (dptr), unitno);
            return SCPE_INCOMP;
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
        READ_I (uptr->dynflags);
        old_capac = uptr->capac;                        /* save current capacity */
        if (v35) {                                      /* [V3.5+] capacity */
            READ_I (uptr->capac);
            }
        if (!v32)
            flg = ((flg & UNIT_UFMASK_31) << (UNIT_V_UF - UNIT_V_UF_31)) |
                (flg & ~UNIT_UFMASK_31);                /* [V3.2+] flags moved */
        uptr->flags = (uptr->flags & ~UNIT_RFLAGS) |
            (flg & UNIT_RFLAGS);                        /* restore */
        READ_S (buf);                                   /* attached file */
        if (uptr->flags & UNIT_ATT) {                   /* unit currently attached? */
            r = scp_detach_unit (dptr, uptr);           /* detach it */
            if (r != SCPE_OK)
                return r;
            }
        if ((buf[0] != '\0') &&                         /* unit to be reattached? */
            ((uptr->flags & UNIT_ATTABLE) ||            /*  and unit is attachable */
             (dptr->attach != NULL))) {                 /*    or VM attach routine provided? */
            uptr->flags = uptr->flags & ~UNIT_DIS;      /* ensure device is enabled */
            if (flg & UNIT_RO)                          /* [V2.10+] saved flgs & RO? */
                sim_switches |= SWMASK ('R');           /* RO attach */
            /* add unit to list of units to attach after registers are read */
            attunits = realloc (attunits, sizeof (*attunits)*(attcnt+1));
            attunits[attcnt] = uptr;
            attnames = realloc (attnames, sizeof (*attnames)*(attcnt+1));
            attnames[attcnt] = malloc(1+strlen(buf));
            strcpy (attnames[attcnt], buf);
            attswitches = realloc (attswitches, sizeof (*attswitches)*(attcnt+1));
            attswitches[attcnt] = sim_switches;
            ++attcnt;
            }
        READ_I (high);                                  /* memory capacity */
        if (high > 0) {                                 /* [V2.5+] any memory? */
            if (((uptr->flags & (UNIT_FIX + UNIT_ATTABLE)) != UNIT_FIX) ||
                 (dptr->deposit == NULL)) {
                printf ("Can't restore memory: %s%d\n", sim_dname (dptr), unitno);
                if (sim_log)
                    fprintf (sim_log, "Can't restore memory: %s%d\n", sim_dname (dptr), unitno);
                return SCPE_INCOMP;
                }
            if (high != old_capac) {                    /* size change? */
                uptr->capac = old_capac;                /* temp restore old */
                if ((dptr->flags & DEV_DYNM) &&
                    ((dptr->msize == NULL) ||
                     (dptr->msize (uptr, (int32) high, NULL, NULL) != SCPE_OK))) {
                    printf ("Can't change memory size: %s%d\n",
                        sim_dname (dptr), unitno);
                    if (sim_log)
                        fprintf (sim_log, "Can't change memory size: %s%d\n",
                            sim_dname (dptr), unitno);
                    return SCPE_INCOMP;
                    }
                uptr->capac = high;                     /* new memory size */
                printf ("Memory size changed: %s%d = ", sim_dname (dptr), unitno);
                fprint_capac (stdout, dptr, uptr);
                printf ("\n");
                if (sim_log) {
                    fprintf (sim_log, "Memory size changed: %s%d = ", sim_dname (dptr), unitno);
                    fprint_capac (sim_log, dptr, uptr);
                    fprintf (sim_log, "\n");
                    }
                }
            sz = SZ_D (dptr);                           /* allocate buffer */
            if ((mbuf = calloc (SRBSIZ, sz)) == NULL)
                return SCPE_MEM;
            for (k = 0; k < high; ) {                   /* loop thru mem */
                if (sim_fread (&blkcnt, sizeof (blkcnt), 1, rfile) == 0) {/* block count */
                    free (mbuf);
                    return SCPE_IOERR;
                    }
                if (blkcnt < 0)                         /* compressed? */
                    limit = -blkcnt;
                else limit = (int32)sim_fread (mbuf, sz, blkcnt, rfile);
                if (limit <= 0) {                       /* invalid or err? */
                    free (mbuf);
                    return SCPE_IOERR;
                    }
                for (j = 0; j < limit; j++, k = k + (dptr->aincr)) {
                    if (blkcnt < 0)                     /* compressed? */
                        val = 0;
                    else SZ_LOAD (sz, val, mbuf, j);    /* saved value */
                    r = dptr->deposit (val, k, uptr, SIM_SW_REST);
                    if (r != SCPE_OK) {
                        free (mbuf);
                        return r;
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
            printf ("Invalid register name: %s %s\n", sim_dname (dptr), buf);
            if (sim_log)
                fprintf (sim_log, "Invalid register name: %s %s\n", sim_dname (dptr), buf);
            for (us = 0; us < depth; us++) {            /* skip values */
                READ_I (val);
                }
            continue;
            }
        if (depth != rptr->depth) {                      /* [V2.10+] mismatch? */
            printf ("Register depth mismatch: %s %s, file = %d, sim = %d\n",
                sim_dname (dptr), buf, depth, rptr->depth);
            if (sim_log)
                fprintf (sim_log, "Register depth mismatch: %s %s, file = %d, sim = %d\n",
                    sim_dname (dptr), buf, depth, rptr->depth);
            }
        mask = width_mask[rptr->width];                 /* get mask */
        for (us = 0; us < depth; us++) {                /* loop thru values */
            READ_I (val);                               /* read value */
            if (val > mask) {                           /* value ok? */
                printf ("Invalid register value: %s %s\n", sim_dname (dptr), buf);
                if (sim_log)
                    fprintf (sim_log, "Invalid register value: %s %s\n", sim_dname (dptr), buf);
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
    if (r == SCPE_OK) {
        struct stat fstat;

        dptr = find_dev_from_unit (attunits[j]);
        if ((!force_restore) && 
            (!stat(attnames[j], &fstat)))
            if (fstat.st_mtime > rstat.st_mtime + 30) {
                r = SCPE_INCOMP;
                printf ("Error Attaching %s to %s - the restore state is %d seconds older than the attach file\n", sim_dname (dptr), attnames[j], (int)(fstat.st_mtime - rstat.st_mtime));
                printf ("restore with the -F switch to override this sanity check\n");
                if (sim_log) {
                    fprintf (sim_log, "Error Attaching %s to %s - the restore state is %d seconds older than the attach file\n", sim_dname (dptr), attnames[j], (int)(fstat.st_mtime - rstat.st_mtime));
                    fprintf (sim_log, "restore with the -F switch to override this sanity check\n");
                    }
                continue;
                }
        sim_switches = attswitches[j];
        r = scp_attach_unit (dptr, attunits[j], attnames[j]);/* reattach unit */
        if (r != SCPE_OK) {
            printf ("Error Attaching %s to %s\n", sim_dname (dptr), attnames[j]);
            if (sim_log)
                fprintf (sim_log, "Error Attaching %s to %s\n", sim_dname (dptr), attnames[j]);
            }
        }
    free (attnames[j]);
    }
free (attnames);
free (attunits);
free (attswitches);
return r;
}

/* Run, go, cont, step commands

   ru[n] [new PC]       reset and start simulation
   go [new PC]          start simulation
   co[nt]               start simulation
   s[tep] [step limit]  start simulation for 'limit' instructions
   b[oot] device        bootstrap from device and start simulation
*/

t_stat run_cmd (int32 flag, char *cptr)
{
char *tptr, gbuf[CBUFSIZE];
uint32 i, j;
int32 unitno;
t_value pcv;
t_stat r;
DEVICE *dptr;
UNIT *uptr;

GET_SWITCHES (cptr);                                    /* get switches */
sim_step = 0;
if ((flag == RU_RUN) || (flag == RU_GO)) {              /* run or go */
    if (*cptr != 0) {                                   /* argument? */
        cptr = get_glyph (cptr, gbuf, 0);               /* get next glyph */
        if (*cptr != 0)                                 /* should be end */
            return SCPE_2MARG;
        if (sim_vm_parse_addr)                          /* address parser? */
            pcv = sim_vm_parse_addr (sim_dflt_dev, gbuf, &tptr);
        else pcv = strtotv (gbuf, &tptr, sim_PC->radix);/* parse PC */
        if ((tptr == gbuf) || (*tptr != 0) ||           /* error? */
            (pcv > width_mask[sim_PC->width]))
            return SCPE_ARG;
        put_rval (sim_PC, 0, pcv);
        }
    if ((flag == RU_RUN) &&                             /* run? */
        ((r = run_boot_prep ()) != SCPE_OK))            /* reset sim */
        return r;
    }

else if (flag == RU_STEP) {                             /* step */
   if (*cptr != 0) {                                    /* argument? */
        cptr = get_glyph (cptr, gbuf, 0);               /* get next glyph */
        if (*cptr != 0)                                 /* should be end */
            return SCPE_2MARG;
        sim_step = (int32) get_uint (gbuf, 10, INT_MAX, &r);
        if ((r != SCPE_OK) || (sim_step <= 0))          /* error? */
            return SCPE_ARG;
        }
    else sim_step = 1;
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
    if ((r = run_boot_prep ()) != SCPE_OK)              /* reset sim */
        return r;
    if ((r = dptr->boot (unitno, dptr)) != SCPE_OK)     /* boot device */
        return r;
    }

else if (flag != RU_CONT)                               /* must be cont */
    return SCPE_IERR;

for (i = 1; (dptr = sim_devices[i]) != NULL; i++) {     /* reposition all */
    for (j = 0; j < dptr->numunits; j++) {              /* seq devices */
        uptr = dptr->units + j;
        if ((uptr->flags & (UNIT_ATT + UNIT_SEQ)) == (UNIT_ATT + UNIT_SEQ))
            sim_fseek (uptr->fileref, uptr->pos, SEEK_SET);
        }
    }
stop_cpu = 0;
sim_is_running = 1;                                     /* flag running */
if (sim_ttrun () != SCPE_OK) {                          /* set console mode */
    sim_is_running = 0;                                 /* flag idle */
    sim_ttcmd ();
    return SCPE_TTYERR;
    }
if ((r = sim_check_console (30)) != SCPE_OK) {          /* check console, error? */
    sim_is_running = 0;                                 /* flag idle */
    sim_ttcmd ();
    return r;
    }
if (signal (SIGINT, int_handler) == SIG_ERR) {          /* set WRU */
    sim_is_running = 0;                                 /* flag idle */
    sim_ttcmd ();
    return SCPE_SIGERR;
    }
#ifdef SIGHUP
if (signal (SIGHUP, int_handler) == SIG_ERR) {          /* set WRU */
    sim_is_running = 0;                                 /* flag idle */
    sim_ttcmd ();
    return SCPE_SIGERR;
    }
#endif
if (signal (SIGTERM, int_handler) == SIG_ERR) {         /* set WRU */
    sim_is_running = 0;                                 /* flag idle */
    sim_ttcmd ();
    return SCPE_SIGERR;
    }
if (sim_step)                                           /* set step timer */
    sim_activate (&sim_step_unit, sim_step);
fflush(stdout);                                         /* flush stdout */
if (sim_log)                                            /* flush log if enabled */
    fflush (sim_log);
sim_throt_sched ();                                     /* set throttle */
sim_brk_clract ();                                      /* defang actions */
sim_rtcn_init_all ();                                   /* re-init clocks */
sim_start_timer_services ();                            /* enable wall clock timing */
r = sim_instr();

sim_is_running = 0;                                     /* flag idle */
sim_stop_timer_services ();                             /* disable wall clock timing */
sim_ttcmd ();                                           /* restore console */
signal (SIGINT, SIG_DFL);                               /* cancel WRU */
#ifdef SIGHUP
signal (SIGHUP, SIG_DFL);                               /* cancel WRU */
#endif
signal (SIGTERM, SIG_DFL);                              /* cancel WRU */
if (sim_log)                                            /* flush console log */
    fflush (sim_log);
if (sim_deb)                                            /* flush debug log */
    fflush (sim_deb);
for (i = 1; (dptr = sim_devices[i]) != NULL; i++) {     /* flush attached files */
    for (j = 0; j < dptr->numunits; j++) {              /* if not buffered in mem */
        uptr = dptr->units + j;
        if ((uptr->flags & UNIT_ATT) &&                 /* attached, */
            !(uptr->flags & UNIT_BUF) &&                /* not buffered, */
            (uptr->fileref)) {                          /* real file, */
            if (uptr->io_flush)                         /* unit specific flush routine */
                uptr->io_flush (uptr);
            else
                if (!(uptr->dynflags & UNIT_NO_FIO) &&  /* is FILE *, */
                    !(uptr->flags & UNIT_RO))           /* not read only? */
                    fflush (uptr->fileref);
            }
        }
    }
sim_cancel (&sim_step_unit);                            /* cancel step timer */
sim_throt_cancel ();                                    /* cancel throttle */
AIO_UPDATE_QUEUE;
UPDATE_SIM_TIME;                                        /* update sim time */
return r;
}

/* run command message handler */

void
run_cmd_message (const char *unechoed_cmdline, t_stat r)
{
#if defined (VMS)
printf ("\n");
#endif
if (unechoed_cmdline) {
    printf("%s> %s\n", do_position(), unechoed_cmdline);
    if (sim_log)
        fprintf (sim_log, "%s> %s\n", do_position(), unechoed_cmdline);
    }
fprint_stopped (stdout, r);                         /* print msg */
if (sim_log)                                        /* log if enabled */
    fprint_stopped (sim_log, r);
}

/* Common setup for RUN or BOOT */

t_stat run_boot_prep (void)
{
UNIT *uptr;

sim_interval = 0;                                       /* reset queue */
sim_time = sim_rtime = 0;
noqueue_time = 0;
for (uptr = sim_clock_queue; uptr != QUEUE_LIST_END; uptr = sim_clock_queue) {
    sim_clock_queue = uptr->next;
    uptr->next = NULL;
    }
return reset_all (0);
}

/* Print stopped message */

void fprint_stopped_gen (FILE *st, t_stat v, REG *pc, DEVICE *dptr)
{
int32 i;
t_stat r = 0;
t_addr k;
t_value pcval;

if (v >= SCPE_BASE)
    fprintf (st, "\n%s, %s: ", sim_error_text (v), pc->name);
else
    fprintf (st, "\n%s, %s: ", sim_stop_messages[v], pc->name);
pcval = get_rval (pc, 0);
if (sim_vm_fprint_addr)
    sim_vm_fprint_addr (st, dptr, (t_addr) pcval);
else fprint_val (st, pcval, pc->radix, pc->width,
    pc->flags & REG_FMT);
if ((dptr != NULL) && (dptr->examine != NULL)) {
    for (i = 0; i < sim_emax; i++)
        sim_eval[i] = 0;
    for (i = 0, k = (t_addr) pcval; i < sim_emax; i++, k = k + dptr->aincr) {
        if ((r = dptr->examine (&sim_eval[i], k, dptr->units, SWMASK ('V'))) != SCPE_OK)
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
return;
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

/* Cancel scheduled step service */

t_stat sim_cancel_step (void)
{
return sim_cancel (&sim_step_unit);
}

/* Signal handler for ^C signal - set stop simulation flag */

void int_handler (int sig)
{
stop_cpu = 1;
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

t_stat exdep_cmd (int32 flag, char *cptr)
{
char gbuf[CBUFSIZE], *gptr, *tptr;
int32 opt;
t_addr low, high;
t_stat reason;
DEVICE *tdptr;
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
        reason = exdep_reg_loop (ofile, sim_schptr, flag, cptr,
            lowr, --highr, 0, 0);
        continue;
        }

    if ((lowr = find_reg (gptr, &tptr, tdptr)) ||       /* local reg or */
        (!(sim_opt_out & CMD_OPT_DFT) &&                /* no dflt, global? */
        (lowr = find_reg_glob (gptr, &tptr, &tdptr)))) {
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
        reason = exdep_reg_loop (ofile, sim_schptr, flag, cptr,
            lowr, highr, (uint32) low, (uint32) high);
        continue;
        }

    tptr = get_range (sim_dfdev, gptr, &low, &high, sim_dfdev->aradix,
        (((sim_dfunit->capac == 0) || (flag == EX_E))? 0:
        sim_dfunit->capac - sim_dfdev->aincr), 0);
    if (tptr == NULL)
        return SCPE_ARG;
    if (*tptr && (*tptr++ != ','))
        return SCPE_ARG;
    reason = exdep_addr_loop (ofile, sim_schptr, flag, cptr, low, high,
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

t_stat exdep_reg_loop (FILE *ofile, SCHTAB *schptr, int32 flag, char *cptr,
    REG *lowr, REG *highr, uint32 lows, uint32 highs)
{
t_stat reason;
uint32 idx, val_start=lows;
t_value val, last_val;
REG *rptr;

if ((lowr == NULL) || (highr == NULL))
    return SCPE_IERR;
if (lowr > highr)
    return SCPE_ARG;
for (rptr = lowr; rptr <= highr; rptr++) {
    if ((sim_switches & SIM_SW_HIDE) &&
        (rptr->flags & REG_HIDDEN))
        continue;
    val = last_val = 0;
    for (idx = lows; idx <= highs; idx++) {
        if (idx >= rptr->depth)
            return SCPE_SUB;
        val = get_rval (rptr, idx);
        if (schptr && !test_search (val, schptr))
            continue;
        if (flag == EX_E) {
            if ((idx > lows) && (val == last_val))
                continue;
            if (idx > val_start+1) {
                if (idx-1 == val_start+1) {
                    reason = ex_reg (ofile, val, flag, rptr, idx-1);
                    if (reason != SCPE_OK)
                        return reason;
                    }
                else
                    fprintf (ofile, "%s[%d]-%s[%d]: same as above\n", rptr->name, val_start+1, rptr->name, idx-1);
                }
            last_val = val;
            val_start = idx;
            reason = ex_reg (ofile, val, flag, rptr, idx);
            if (reason != SCPE_OK)
                return reason;
            if (sim_log && (ofile == stdout))
                ex_reg (sim_log, val, flag, rptr, idx);
            }
        if (flag != EX_E) {
            reason = dep_reg (flag, cptr, rptr, idx);
            if (reason != SCPE_OK)
                return reason;
            }
        }
    if ((flag == EX_E) && (val_start != highs)) {
        if (highs == val_start+1) {
            reason = ex_reg (ofile, val, flag, rptr, highs);
            if (reason != SCPE_OK)
                return reason;
            }
        else
            fprintf (ofile, "%s[%d]-%s[%d]: same as above\n", rptr->name, val_start+1, rptr->name, highs);
        }
    }
return SCPE_OK;
}

t_stat exdep_addr_loop (FILE *ofile, SCHTAB *schptr, int32 flag, char *cptr,
    t_addr low, t_addr high, DEVICE *dptr, UNIT *uptr)
{
t_addr i, mask;
t_stat reason;

if (uptr->flags & UNIT_DIS)                             /* disabled? */
    return SCPE_UDIS;
mask = (t_addr) width_mask[dptr->awidth];
if ((low > mask) || (high > mask) || (low > high))
    return SCPE_ARG;
for (i = low; i <= high; ) {                            /* all paths must incr!! */
    reason = get_aval (i, dptr, uptr);                  /* get data */
    if (reason != SCPE_OK)                              /* return if error */
        return reason;
    if (schptr && !test_search (sim_eval[0], schptr))
        i = i + dptr->aincr;                            /* sch fails, incr */
    else {                                              /* no sch or success */
        if (flag != EX_D) {                             /* ex, ie, or id? */
            reason = ex_addr (ofile, flag, i, dptr, uptr);
            if (reason > SCPE_OK)
                return reason;
            if (sim_log && (ofile == stdout))
                ex_addr (sim_log, flag, i, dptr, uptr);
            }
        else reason = 1 - dptr->aincr;                  /* no, dflt incr */
        if (flag != EX_E) {                             /* ie, id, or d? */
            reason = dep_addr (flag, cptr, i, dptr, uptr, reason);
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
else fprintf (ofile, "%s:\t", rptr->name);
if (!(flag & EX_E))
    return SCPE_OK;
GET_RADIX (rdx, rptr->radix);
if ((rptr->flags & REG_VMAD) && sim_vm_fprint_addr)
    sim_vm_fprint_addr (ofile, sim_dflt_dev, (t_addr) val);
else if (!(rptr->flags & REG_VMIO) ||
    (fprint_sym (ofile, rdx, &val, NULL, sim_switches | SIM_SW_REG) > 0)) {
        fprint_val (ofile, val, rdx, rptr->width, rptr->flags & REG_FMT);
        if (rptr->fields) {
            fprintf (ofile, "\t");
            fprint_fields (ofile, val, val, rptr->fields);
            }
        }
if (flag & EX_I)
    fprintf (ofile, "\t");
else fprintf (ofile, "\n");
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
UNIT *uptr;

sz = SZ_R (rptr);
if ((rptr->depth > 1) && (rptr->flags & REG_CIRC)) {
    idx = idx + rptr->qptr;
    if (idx >= rptr->depth) idx = idx - rptr->depth;
    }
if ((rptr->depth > 1) && (rptr->flags & REG_UNIT)) {
    uptr = ((UNIT *) rptr->loc) + idx;
#if defined (USE_INT64)
    if (sz <= sizeof (uint32))
        val = *((uint32 *) uptr);
    else val = *((t_uint64 *) uptr);
#else
    val = *((uint32 *) uptr);
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

t_stat dep_reg (int32 flag, char *cptr, REG *rptr, uint32 idx)
{
t_stat r;
t_value val, mask;
int32 rdx;
char *tptr, gbuf[CBUFSIZE];

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
else if (!(rptr->flags & REG_VMIO) ||                   /* dont use sym? */
    (parse_sym (cptr, rdx, NULL, &val, sim_switches | SIM_SW_REG) > SCPE_OK)) {
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
UNIT *uptr;

#define PUT_RVAL(sz,rp,id,v,m) \
    *(((sz *) rp->loc) + id) = \
            (*(((sz *) rp->loc) + id) & \
            ~((m) << (rp)->offset)) | ((v) << (rp)->offset)

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
    uptr = ((UNIT *) rptr->loc) + idx;
#if defined (USE_INT64)
    if (sz <= sizeof (uint32))
        *((uint32 *) uptr) = (*((uint32 *) uptr) &
        ~(((uint32) mask) << rptr->offset)) |
        (((uint32) val) << rptr->offset);
    else *((t_uint64 *) uptr) = (*((t_uint64 *) uptr)
        & ~(mask << rptr->offset)) | (val << rptr->offset);
#else
    *((uint32 *) uptr) = (*((uint32 *) uptr) &
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
return;
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
else fprintf (ofile, "\n");
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
        if (uptr->dynflags & UNIT_NO_FIO)
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
            sim_fseek (uptr->fileref, (t_addr)(sz * loc), SEEK_SET);
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
    sim_eval[i] = sim_eval[i] & mask;
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

t_stat dep_addr (int32 flag, char *cptr, t_addr addr, DEVICE *dptr,
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
if ((reason = parse_sym (cptr, addr, uptr, sim_eval, sim_switches)) > 0) {
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
            sim_fseek (uptr->fileref, (t_addr)(sz * loc), SEEK_SET);
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

t_stat eval_cmd (int32 flg, char *cptr)
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
if ((r = parse_sym (cptr, 0, dptr->units, sim_eval, sim_switches)) > 0) {
    sim_eval[0] = get_uint (cptr, rdx, width_mask[dptr->dwidth], &r);
    if (r != SCPE_OK)
        return r;
    }
lim = 1 - r;
for (i = a = 0; a < lim; ) {
    printf ("%d:\t", a);
    if ((r = fprint_sym (stdout, a, &sim_eval[i], dptr->units, sim_switches)) > 0)
        r = fprint_val (stdout, sim_eval[i], rdx, dptr->dwidth, PV_RZRO);
    printf ("\n");
    if (sim_log) {
        fprintf (sim_log, "%d\t", i);
        if ((r = fprint_sym (sim_log, a, &sim_eval[i], dptr->units, sim_switches)) > 0)
            r = fprint_val (sim_log, sim_eval[i], rdx, dptr->dwidth, PV_RZRO);
        fprintf (sim_log, "\n");
        }
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

char *read_line_p (char *prompt, char *cptr, int32 size, FILE *stream)
{
char *tptr;
#if defined(HAVE_DLOPEN)
static int initialized = 0;
typedef char *(*readline_func)(const char *);
static readline_func p_readline = NULL;
typedef void (*add_history_func)(const char *);
static add_history_func p_add_history = NULL;

if (!initialized) {
    initialized = 1;
    void *handle;

#define S__STR_QUOTE(tok) #tok
#define S__STR(tok) S__STR_QUOTE(tok)
    handle = dlopen("libncurses." S__STR(HAVE_DLOPEN), RTLD_NOW|RTLD_GLOBAL);
    handle = dlopen("libcurses." S__STR(HAVE_DLOPEN), RTLD_NOW|RTLD_GLOBAL);
    handle = dlopen("libreadline." S__STR(HAVE_DLOPEN), RTLD_NOW|RTLD_GLOBAL);
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
            strncpy (cptr, tmpc, size);                 /* copy result */
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
while (isspace (*cptr))                                 /* trim leading spc */
    cptr++;
if (*cptr == ';') {                                     /* ignore comment */
    if (sim_do_echo)                                    /* echo comments if -v */
        printf("%s> %s\n", do_position(), cptr);
    if (sim_do_echo && sim_log)
        fprintf (sim_log, "%s> %s\n", do_position(), cptr);
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
   get_glyph_gen        get next glyph (general case)

   Inputs:
        iptr    =       pointer to input string
        optr    =       pointer to output string
        mchar   =       optional end of glyph character
        uc      =       TRUE for convert to upper case (_gen only)
        quote   =       TRUE to allow quote enclosing values (_gen only)
   Outputs
        result  =       pointer to next character in input string
*/

static char *get_glyph_gen (char *iptr, char *optr, char mchar, t_bool uc, t_bool quote)
{
t_bool quoting = FALSE;
char quote_char = 0;

while ((*iptr != 0) && 
       ((quote && quoting) || ((isspace (*iptr) == 0) && (*iptr != mchar)))) {
    if (quote) {
        if (quoting) {
            if (*iptr == quote_char)
                quoting = FALSE;
            }
        else {
            if ((*iptr == '"') || (*iptr == '\'')) {
                quoting = TRUE;
                quote_char = *iptr;
                }
            }
        }
    if (islower (*iptr) && uc)
        *optr = toupper (*iptr);
    else *optr = *iptr;
    iptr++; optr++;
    }
*optr = 0;
if (mchar && (*iptr == mchar))                          /* skip terminator */
    iptr++;
while (isspace (*iptr))                                 /* absorb spaces */
    iptr++;
return iptr;
}

char *get_glyph (char *iptr, char *optr, char mchar)
{
return get_glyph_gen (iptr, optr, mchar, TRUE, FALSE);
}

char *get_glyph_nc (char *iptr, char *optr, char mchar)
{
return get_glyph_gen (iptr, optr, mchar, FALSE, FALSE);
}

char *get_glyph_quoted (char *iptr, char *optr, char mchar)
{
return get_glyph_gen (iptr, optr, mchar, FALSE, TRUE);
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
while ((--tptr >= cptr) && isspace (*tptr))
    *tptr = 0;
return cptr;
}

/* get_yn               yes/no question

   Inputs:
        cptr    =       pointer to question
        deflt   =       default answer
   Outputs:
        result  =       true if yes, false if no
*/

t_stat get_yn (char *ques, t_stat deflt)
{
char cbuf[CBUFSIZE], *cptr;

printf ("%s ", ques);
cptr = read_line (cbuf, sizeof(cbuf), stdin);
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

t_value get_uint (char *cptr, uint32 radix, t_value max, t_stat *status)
{
t_value val;
char *tptr;

*status = SCPE_OK;
val = strtotv (cptr, &tptr, radix);
if ((cptr == tptr) || (val > max))
    *status = SCPE_ARG;
else {
    while (isspace (*tptr)) tptr++;
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

char *get_range (DEVICE *dptr, char *cptr, t_addr *lo, t_addr *hi,
    uint32 rdx, t_addr max, char term)
{
char *tptr;

if (max && strncmp (cptr, "ALL", strlen ("ALL")) == 0) { /* ALL? */
    tptr = cptr + strlen ("ALL");
    *lo = 0;
    *hi = max;
    }
else {
    if (dptr && sim_vm_parse_addr)                      /* get low */
        *lo = sim_vm_parse_addr (dptr, cptr, &tptr);
    else *lo = (t_addr) strtotv (cptr, &tptr, rdx);
    if (cptr == tptr)                                   /* error? */
            return NULL;
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
if (term && (*tptr++ != term))
    return NULL;
return tptr;
}

/* Find_device          find device matching input string

   Inputs:
        cptr    =       pointer to input string
   Outputs:
        result  =       pointer to device
*/

DEVICE *find_dev (char *cptr)
{
int32 i;
DEVICE *dptr;

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

DEVICE *find_unit (char *cptr, UNIT **uptr)
{
uint32 i, u;
char *nptr, *tptr;
t_stat r;
DEVICE *dptr;

if (uptr == NULL)                                       /* arg error? */
    return NULL;
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
        if (isdigit (*tptr)) {
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

for (i = 0; (sim_devices[i] != NULL); i++)
    if (sim_devices[i] == dptr)
        return SCPE_OK;
for (i = 0; i < sim_internal_device_count; i++)
    if (sim_internal_devices[i] == dptr)
        return SCPE_OK;
++sim_internal_device_count;
sim_internal_devices = realloc(sim_internal_devices, (sim_internal_device_count+1)*sizeof(*sim_internal_devices));
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
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
    for (j = 0; j < dptr->numunits; j++) {
        if (uptr == (dptr->units + j))
            return dptr;
        }
    }
for (i = 0; i<sim_internal_device_count; i++) {
    dptr = sim_internal_devices[i];
    for (j = 0; j < dptr->numunits; j++) {
        if (uptr == (dptr->units + j))
            return dptr;
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
*/

REG *find_reg_glob (char *cptr, char **optr, DEVICE **gdptr)
{
int32 i;
DEVICE *dptr;
REG *rptr, *srptr = NULL;

for (i = 0; (dptr = sim_devices[i]) != 0; i++) {        /* all dev */
    if (dptr->flags & DEV_DIS)                          /* skip disabled */
        continue;
    if ((rptr = find_reg (cptr, optr, dptr))) {         /* found? */
        if (srptr)                                      /* ambig? err */
            return NULL;
        srptr = rptr;                                   /* save reg */
        *gdptr = dptr;                                  /* save unit */
        }
    }
return srptr;
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

REG *find_reg (char *cptr, char **optr, DEVICE *dptr)
{
char *tptr;
REG *rptr;
size_t slnt;

if ((cptr == NULL) || (dptr == NULL) || (dptr->registers == NULL))
    return NULL;
tptr = cptr;
do {
    tptr++;
    } while (isalnum (*tptr) || (*tptr == '*') || (*tptr == '_') || (*tptr == '.'));
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
        sw      =       switch bit mask
                        0 if no switches, -1 if error
*/

int32 get_switches (char *cptr)
{
int32 sw;

if (*cptr != '-')
    return 0;
sw = 0;
for (cptr++; (isspace (*cptr) == 0) && (*cptr != 0); cptr++) {
    if (isalpha (*cptr) == 0)
        return -1;
    sw = sw | SWMASK (toupper (*cptr));
    }
return sw;
}

/* get_sim_sw           accumulate sim_switches

   Inputs:
        cptr    =       pointer to input string
   Outputs:
        ptr     =       pointer to first non-string glyph
                        NULL if error
*/

char *get_sim_sw (char *cptr)
{
int32 lsw;
char gbuf[CBUFSIZE];

while (*cptr == '-') {                                  /* while switches */
    cptr = get_glyph (cptr, gbuf, 0);                   /* get switch glyph */
    lsw = get_switches (gbuf);                          /* parse */
    if (lsw <= 0)                                       /* invalid? */
        return NULL;
    sim_switches = sim_switches | lsw;                  /* accumulate */
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

char *get_sim_opt (int32 opt, char *cptr, t_stat *st)
{
int32 t;
char *svptr, gbuf[CBUFSIZE];
DEVICE *tdptr;
UNIT *tuptr;

sim_switches = 0;                                       /* no switches */
sim_ofile = NULL;                                       /* no output file */
sim_schptr = NULL;                                      /* no search */
sim_stab.logic = SCH_OR;                                /* default search params */
sim_stab.boolop = SCH_GE;
sim_stab.mask = 0;
sim_stab.comp = 0;
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
        cptr = get_glyph_nc (cptr + 1, gbuf, 0);
        sim_ofile = sim_fopen (gbuf, "a");              /* open for append */
        if (sim_ofile == NULL) {                        /* open failed? */
            *st = SCPE_OPENERR;
            return NULL;
            }
        sim_opt_out |= CMD_OPT_OF;                      /* got output file */
        continue;
        }
    cptr = get_glyph (cptr, gbuf, 0);
    if ((t = get_switches (gbuf)) != 0) {               /* try for switches */
        if (t < 0) {                                    /* err if bad switch */
            *st = SCPE_INVSW;
            return NULL;
            }
        sim_switches = sim_switches | t;                /* or in new switches */
        }
    else if ((opt & CMD_OPT_SCH) &&                     /* if allowed, */
        get_search (gbuf, sim_dfdev->dradix, &sim_stab)) { /* try for search */
        sim_schptr = &sim_stab;                         /* set search */
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

/* Match file extension

   Inputs:
        fnam    =       file name
        ext     =       extension, without period
   Outputs:
        cp      =       pointer to final '.' if match, NULL if not
*/

char *match_ext (char *fnam, char *ext)
{
char *pptr, *fptr, *eptr;

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
        if (toupper (*fptr) != toupper (*eptr))
            return NULL;
        }
    if (*eptr != 0)                                     /* ext exhausted? */
        return NULL;
    }
return pptr;
}

/* Get search specification

   Inputs:
        cptr    =       pointer to input string
        radix   =       radix for numbers
        schptr =        pointer to search table
   Outputs:
        return =        NULL if error
                        schptr if valid search specification
*/

SCHTAB *get_search (char *cptr, int32 radix, SCHTAB *schptr)
{
int32 c, logop, cmpop;
t_value logval, cmpval;
char *sptr, *tptr;
const char logstr[] = "|&^", cmpstr[] = "=!><";

logval = cmpval = 0;
if (*cptr == 0)                                         /* check for clause */
    return NULL;
for (logop = cmpop = -1; (c = *cptr++); ) {               /* loop thru clauses */
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
if (logop >= 0) {
    schptr->logic = logop;
    schptr->mask = logval;
    }
if (cmpop >= 0) {
    schptr->boolop = cmpop;
    schptr->comp = cmpval;
    }
return schptr;
}

/* Test value against search specification

   Inputs:
        val     =       value to test
        schptr =        pointer to search table
   Outputs:
        return =        1 if value passes search criteria, 0 if not
*/

int32 test_search (t_value val, SCHTAB *schptr)
{
if (schptr == NULL) return 0;

switch (schptr->logic) {                                /* case on logical */

    case SCH_OR:
        val = val | schptr->mask;
        break;

    case SCH_AND:
        val = val & schptr->mask;
        break;

    case SCH_XOR:
        val = val ^ schptr->mask;
        break;
        }

switch (schptr->boolop) {                                       /* case on comparison */

    case SCH_E: case SCH_EE:
        return (val == schptr->comp);

    case SCH_N: case SCH_NE:
        return (val != schptr->comp);

    case SCH_G:
        return (val > schptr->comp);

    case SCH_GE:
        return (val >= schptr->comp);

    case SCH_L:
        return (val < schptr->comp);

    case SCH_LE:
        return (val <= schptr->comp);
        }

return 0;
}

/* Radix independent input/output package

   strtotv - general radix input routine

   Inputs:
        inptr   =       string to convert
        endptr  =       pointer to first unconverted character
        radix   =       radix for input
   Outputs:
        value   =       converted value

   On an error, the endptr will equal the inptr.
*/

t_value strtotv (const char *inptr, char **endptr, uint32 radix)
{
int32 nodigit;
t_value val;
uint32 c, digit;

*endptr = (char *)inptr;                                /* assume fails */
if ((radix < 2) || (radix > 36))
    return 0;
while (isspace (*inptr))                                /* bypass white space */
    inptr++;
val = 0;
nodigit = 1;
for (c = *inptr; isalnum(c); c = *++inptr) {            /* loop through char */
    if (islower (c))
        c = toupper (c);
    if (isdigit (c))                                    /* digit? */
        digit = c - (uint32) '0';
    else if (radix <= 10)                               /* stop if not expected */
        break;
    else digit = c + 10 - (uint32) 'A';                 /* convert letter */
    if (digit >= radix)                                 /* valid in radix? */
        return 0;
    val = (val * radix) + digit;                        /* add to value */
    nodigit = 0;
    }
if (nodigit)                                            /* no digits? */
    return 0;
*endptr = (char *)inptr;                                /* result pointer */
return val;
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
int32 d, digit, ndigits, commas = 0;
char dbuf[MAX_WIDTH + 1];

for (d = 0; d < MAX_WIDTH; d++)
    dbuf[d] = (format == PV_RZRO)? '0': ' ';
dbuf[MAX_WIDTH] = 0;
d = MAX_WIDTH;
do {
    d = d - 1;
    digit = (int32) (val % radix);
    val = val / radix;
    dbuf[d] = (digit <= 9)? '0' + digit: 'A' + (digit - 10);
    } while ((d > 0) && (val != 0));

switch (format) {
    case PV_LEFT:
        break;
    case PV_RCOMMA:
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
if (width < strlen(dbuf+d))
    return SCPE_IOERR;
strcpy(buffer, dbuf+d);
return SCPE_OK;
}

t_stat fprint_val (FILE *stream, t_value val, uint32 radix,
    uint32 width, uint32 format)
{
char dbuf[MAX_WIDTH + 1];
t_stat r;

if (!stream)
    return sprint_val (NULL, val, radix, width, format);
if (width > MAX_WIDTH)
    width = MAX_WIDTH;
r = sprint_val (dbuf, val, radix, width, format);
if (fputs (dbuf, stream) == EOF)
    return SCPE_IOERR;
return SCPE_OK;
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

if (stop_cpu)                                           /* stop CPU? */
    return SCPE_STOP;
AIO_UPDATE_QUEUE;
UPDATE_SIM_TIME;                                        /* update sim time */

if (sim_clock_queue == QUEUE_LIST_END) {                /* queue empty? */
    sim_interval = noqueue_time = NOQUEUE_WAIT;         /* flag queue empty */
    sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Queue Emptry New Interval = %d\n", sim_interval);
    return SCPE_OK;
    }
do {
    uptr = sim_clock_queue;                             /* get first */
    sim_clock_queue = uptr->next;                       /* remove first */
    uptr->next = NULL;                                  /* hygiene */
    uptr->time = 0;
    if (sim_clock_queue != QUEUE_LIST_END)
        sim_interval = sim_clock_queue->time;
    else
        sim_interval = noqueue_time = NOQUEUE_WAIT;
    sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Processing Event for %s\n", sim_uname (uptr));
    AIO_EVENT_BEGIN(uptr);
    if (uptr->action != NULL)
        reason = uptr->action (uptr);
    else
        reason = SCPE_OK;
    AIO_EVENT_COMPLETE(uptr, reason);
    } while ((reason == SCPE_OK) && 
             (sim_interval <= 0) && 
             (sim_clock_queue != QUEUE_LIST_END));

if (sim_clock_queue == QUEUE_LIST_END) {                /* queue empty? */
    sim_interval = noqueue_time = NOQUEUE_WAIT;         /* flag queue empty */
    sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Processing Queue Complete New Interval = %d\n", sim_interval);
    }
else
    sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Processing Queue Complete New Interval = %d(%s)\n", sim_interval, sim_uname(sim_clock_queue));

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

t_stat sim_activate_after (UNIT *uptr, int32 event_time)
{
return _sim_activate_after (uptr, event_time);
}

t_stat _sim_activate_after (UNIT *uptr, int32 usec_delay)
{
if (sim_is_active (uptr))                               /* already active? */
    return SCPE_OK;
AIO_ACTIVATE (_sim_activate_after, uptr, usec_delay);
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
AIO_CANCEL(uptr);
AIO_UPDATE_QUEUE;
if (sim_clock_queue == QUEUE_LIST_END)
    return SCPE_OK;
sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Canceling Event for %s\n", sim_uname(uptr));
UPDATE_SIM_TIME;                                        /* update sim time */
if (!sim_is_active (uptr))
    return SCPE_OK;
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
if (sim_clock_queue != QUEUE_LIST_END)
    sim_interval = sim_clock_queue->time;
else sim_interval = noqueue_time = NOQUEUE_WAIT;
if (uptr->next) {
    if (sim_deb) {
        sim_debug (SIM_DBG_EVENT, sim_dflt_dev, "Cancel failed for %s\n", sim_uname(uptr));
        fclose(sim_deb);
        }
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
return (((uptr->next) || AIO_IS_ACTIVE(uptr)) ? TRUE : FALSE);
}

/* sim_activate_time - return activation time

   Inputs:
        uptr    =       pointer to unit
   Outputs:
        result =        absolute activation time + 1, 0 if inactive
*/

int32 sim_activate_time (UNIT *uptr)
{
UNIT *cptr;
int32 accum = 0;

AIO_VALIDATE;
AIO_RETURN_TIME(uptr);
for (cptr = sim_clock_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
    if (cptr == sim_clock_queue) {
        if (sim_interval > 0)
            accum = accum + sim_interval;
        }
    else accum = accum + cptr->time;
    if (cptr == uptr)
        return accum + 1;
    }
return 0;
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
   efficient binary searching.  A breakpoint consists of a four entry structure:

        addr                    address of the breakpoint
        type                    types of breakpoints set on the address
                                a bit mask representing letters A-Z
        cnt                     number of iterations before breakp is taken
        action                  pointer command string to be executed
                                when break is taken

   sim_brk_summ is a summary of the types of breakpoints that are currently set (it
   is the bitwise OR of all the type fields).  A simulator need only check for
   a breakpoint of type X if bit SWMASK('X') is set in sim_brk_sum.

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
sim_brk_lnt = SIM_BRK_INILNT;
sim_brk_tab = (BRKTAB *) calloc (sim_brk_lnt, sizeof (BRKTAB));
if (sim_brk_tab == NULL)
    return SCPE_MEM;
sim_brk_ent = sim_brk_ins = 0;
sim_brk_act[sim_do_depth] = NULL;
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
    bp = sim_brk_tab + p;                               /* table addr */
    if (loc == bp->addr)                                /* match? */
        return bp;
    else if (loc < bp->addr)                            /* go down? p is upper */
        hi = p - 1;
    else lo = p + 1;                                    /* go up? p is lower */
    } while (lo <= hi);
if (loc < bp->addr)                                     /* insrt before or */
    sim_brk_ins = p;
else sim_brk_ins = p + 1;                               /* after last sch */
return NULL;
}

/* Insert a breakpoint */

BRKTAB *sim_brk_new (t_addr loc)
{
int32 i, t;
BRKTAB *bp, *newp;

if (sim_brk_ins < 0)
    return NULL;
if (sim_brk_ent >= sim_brk_lnt) {                       /* out of space? */
    t = sim_brk_lnt + SIM_BRK_INILNT;                   /* new size */
    newp = (BRKTAB *) calloc (t, sizeof (BRKTAB));      /* new table */
    if (newp == NULL)                                   /* can't extend */
        return NULL;
    for (i = 0; i < sim_brk_lnt; i++)                   /* copy table */
        *(newp + i) = *(sim_brk_tab + i);
    free (sim_brk_tab);                                 /* free old table */
    sim_brk_tab = newp;                                 /* new base, lnt */
    sim_brk_lnt = t;
    }
if (sim_brk_ins != sim_brk_ent) {                       /* move needed? */
    for (bp = sim_brk_tab + sim_brk_ent;
         bp > sim_brk_tab + sim_brk_ins; bp--)
        *bp = *(bp - 1);
    }
bp = sim_brk_tab + sim_brk_ins;
bp->addr = loc;
bp->typ = 0;
bp->cnt = 0;
bp->act = NULL;
sim_brk_ent = sim_brk_ent + 1;
return bp;
}

/* Set a breakpoint of type sw */

t_stat sim_brk_set (t_addr loc, int32 sw, int32 ncnt, char *act)
{
BRKTAB *bp;

if (sw == 0) sw = sim_brk_dflt;
if ((sim_brk_types & sw) == 0)
    return SCPE_NOFNC;
bp = sim_brk_fnd (loc);                                 /* present? */
if (!bp)                                                /* no, allocate */
    bp = sim_brk_new (loc);
if (!bp)                                                /* still no? mem err */
    return SCPE_MEM;
bp->typ = sw;                                           /* set type */
bp->cnt = ncnt;                                         /* set count */
if ((bp->act != NULL) && (act != NULL)) {               /* replace old action? */
    free (bp->act);                                     /* deallocate */
    bp->act = NULL;                                     /* now no action */
    }
if ((act != NULL) && (*act != 0)) {                     /* new action? */
    char *newp = (char *) calloc (CBUFSIZE, sizeof (char)); /* alloc buf */
    if (newp == NULL)                                   /* mem err? */
        return SCPE_MEM;
    strncpy (newp, act, CBUFSIZE);                      /* copy action */
    bp->act = newp;                                     /* set pointer */
    }
sim_brk_summ = sim_brk_summ | sw;
return SCPE_OK;
}

/* Clear a breakpoint */

t_stat sim_brk_clr (t_addr loc, int32 sw)
{
BRKTAB *bp = sim_brk_fnd (loc);

if (!bp)                                                /* not there? ok */
    return SCPE_OK;
if (sw == 0)
    sw = SIM_BRK_ALLTYP;
bp->typ = bp->typ & ~sw;
if (bp->typ)                                            /* clear all types? */
    return SCPE_OK;
if (bp->act != NULL)                                    /* deallocate action */
    free (bp->act);
for ( ; bp < (sim_brk_tab + sim_brk_ent - 1); bp++)     /* erase entry */
    *bp = *(bp + 1);
sim_brk_ent = sim_brk_ent - 1;                          /* decrement count */
sim_brk_summ = 0;                                       /* recalc summary */
for (bp = sim_brk_tab; bp < (sim_brk_tab + sim_brk_ent); bp++)
    sim_brk_summ = sim_brk_summ | bp->typ;
return SCPE_OK;
}

/* Clear all breakpoints */

t_stat sim_brk_clrall (int32 sw)
{
BRKTAB *bp;

if (sw == 0) sw = SIM_BRK_ALLTYP;
for (bp = sim_brk_tab; bp < (sim_brk_tab + sim_brk_ent); ) {
    if (bp->typ & sw)
        sim_brk_clr (bp->addr, sw);
    else bp++;
    }
return SCPE_OK;
}

/* Show a breakpoint */

t_stat sim_brk_show (FILE *st, t_addr loc, int32 sw)
{
BRKTAB *bp = sim_brk_fnd (loc);
DEVICE *dptr;
int32 i, any;

if (sw == 0)
    sw = SIM_BRK_ALLTYP;
if (!bp || (!(bp->typ & sw)))
    return SCPE_OK;
dptr = sim_dflt_dev;
if (dptr == NULL)
    return SCPE_OK;
if (sim_vm_fprint_addr)
    sim_vm_fprint_addr (st, dptr, loc);
else fprint_val (st, loc, dptr->aradix, dptr->awidth, PV_LEFT);
fprintf (st, ":\t");
for (i = any = 0; i < 26; i++) {
    if ((bp->typ >> i) & 1) {
        if (any)
            fprintf (st, ", ");
        fputc (i + 'A', st);
        any = 1;
        }
    }
if (bp->cnt > 0)
    fprintf (st, " [%d]", bp->cnt);
if (bp->act != NULL)
    fprintf (st, "; %s", bp->act);
fprintf (st, "\n");
return SCPE_OK;
}

/* Show all breakpoints */

t_stat sim_brk_showall (FILE *st, int32 sw)
{
BRKTAB *bp;

if (sw == 0)
    sw = SIM_BRK_ALLTYP;
for (bp = sim_brk_tab; bp < (sim_brk_tab + sim_brk_ent); bp++) {
    if (bp->typ & sw)
        sim_brk_show (st, bp->addr, sw);
    }
return SCPE_OK;
}

/* Test for breakpoint */

uint32 sim_brk_test (t_addr loc, uint32 btyp)
{
BRKTAB *bp;
uint32 spc = (btyp >> SIM_BKPT_V_SPC) & (SIM_BKPT_N_SPC - 1);

if ((bp = sim_brk_fnd (loc)) && (btyp & bp->typ)) {     /* in table, type match? */
    if ((sim_brk_pend[spc] && (loc == sim_brk_ploc[spc])) || /* previous location? */
        (--bp->cnt > 0))                                /* count > 0? */
        return 0;
    bp->cnt = 0;                                        /* reset count */
    sim_brk_ploc[spc] = loc;                            /* save location */
    sim_brk_pend[spc] = TRUE;                           /* don't do twice */
    sim_brk_act[sim_do_depth] = bp->act;                /* set up actions */
    return (btyp & bp->typ);
    }
sim_brk_pend[spc] = FALSE;
return 0;
}

/* Get next pending action, if any */

char *sim_brk_getact (char *buf, int32 size)
{
char *ep;
size_t lnt;

if (sim_brk_act[sim_do_depth] == NULL)                  /* any action? */
    return NULL;
while (isspace (*sim_brk_act[sim_do_depth]))            /* skip spaces */
    sim_brk_act[sim_do_depth]++;
if (*sim_brk_act[sim_do_depth] == 0)                    /* now empty? */
    return (sim_brk_act[sim_do_depth] = NULL);
if ((ep = strchr (sim_brk_act[sim_do_depth], ';'))) {   /* cmd delimiter? */
    lnt = ep - sim_brk_act[sim_do_depth];               /* cmd length */
    memcpy (buf, sim_brk_act[sim_do_depth], lnt + 1);   /* copy with ; */
    buf[lnt] = 0;                                       /* erase ; */
    sim_brk_act[sim_do_depth] += lnt + 1;               /* adv ptr */
    }
else {
    strncpy (buf, sim_brk_act[sim_do_depth], size);     /* copy action */
    sim_brk_act[sim_do_depth] = NULL;                   /* no more */
    }
return buf;
}

/* Clear pending actions */

void sim_brk_clract (void)
{
sim_brk_act[sim_do_depth] = NULL;
}

/* New PC */

void sim_brk_npc (uint32 cnt)
{
uint32 i;

if ((cnt == 0) || (cnt > SIM_BKPT_N_SPC))
    cnt = SIM_BKPT_N_SPC;
for (i = 0; i < cnt; i++) {
    sim_brk_pend[i] = FALSE;
    sim_brk_ploc[i] = 0;
    }
return;
}

/* Clear breakpoint space */

void sim_brk_clrspc (uint32 spc)
{
if (spc < SIM_BKPT_N_SPC) {
    sim_brk_pend[spc] = FALSE;
    sim_brk_ploc[spc] = 0;
    }
return;
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

t_stat sim_string_to_stat (char *cptr, t_stat *stat)
{
char gbuf[CBUFSIZE];
int32 cond;

*stat = SCPE_ARG;
cptr = get_glyph (cptr, gbuf, 0);
if (0 == memcmp("SCPE_", gbuf, 5))
    strcpy (gbuf, gbuf+5);   /* skip leading SCPE_ */
for (cond=0; cond < (SCPE_MAX_ERR-SCPE_BASE); cond++)
    if (0 == strcmp(scp_errors[cond].code, gbuf)) {
        cond += SCPE_BASE;
        break;
        }
if (0 == strcmp(gbuf, "OK"))
    cond = SCPE_OK;
if (cond == (SCPE_MAX_ERR-SCPE_BASE)) {       /* not found? */
    if (0 == (cond = strtol(gbuf, NULL, 0)))  /* try explicit number */
        return SCPE_ARG;
    }
if (cond > SCPE_MAX_ERR)
    return SCPE_ARG;
*stat = cond;
return SCPE_OK;    
}

/* Debug printout routines, from Dave Hittner */

const char* debug_bstates = "01_^";
char debug_line_prefix[256];
int32 debug_unterm  = 0;

/* Finds debug phrase matching bitmask from from device DEBTAB table */

static char* get_dbg_verb (uint32 dbits, DEVICE* dptr)
{
static char* debtab_none    = "DEBTAB_ISNULL";
static char* debtab_nomatch = "DEBTAB_NOMATCH";
int32 offset = 0;

if (dptr->debflags == 0)
    return debtab_none;

/* Find matching words for bitmask */

while (dptr->debflags[offset].name && (offset < 32)) {
    if (dptr->debflags[offset].mask & dbits)
        return dptr->debflags[offset].name;
    offset++;
    }
return debtab_nomatch;
}

/* Prints standard debug prefix unless previous call unterminated */

static const char *sim_debug_prefix (uint32 dbits, DEVICE* dptr)
{
char* debug_type = get_dbg_verb (dbits, dptr);
static const char* debug_fmt     = "DBG(%.0f)%s> %s %s: ";
char tim_t[32] = "";
char tim_a[32] = "";
char pc_s[64] = "";
struct timespec time_now;

if (sim_deb_switches & (SWMASK ('T') | SWMASK ('R') | SWMASK ('A'))) {
    clock_gettime(CLOCK_REALTIME, &time_now);
    if (sim_deb_switches & SWMASK ('R'))
        sim_timespec_diff (&time_now, &time_now, &sim_deb_basetime);
    }
if (sim_deb_switches & SWMASK ('T')) {
    time_t tnow = (time_t)time_now.tv_sec;
    struct tm *now = gmtime(&tnow);

    sprintf(tim_t, "%02d:%02d:%02d.%03d ", now->tm_hour, now->tm_min, now->tm_sec, (int)(time_now.tv_nsec/1000000));
    }
if (sim_deb_switches & SWMASK ('A')) {
    sprintf(tim_t, "%lld.%03d ", (long long)(time_now.tv_sec), (int)(time_now.tv_nsec/1000000));
    }
if (sim_deb_switches & SWMASK ('P')) {
    t_value val = get_rval (sim_deb_PC, 0);
    sprintf(pc_s, "-%s:", sim_deb_PC->name);
    sprint_val (&pc_s[strlen(pc_s)], val, sim_deb_PC->radix, sim_deb_PC->width, sim_deb_PC->flags & REG_FMT);
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
        char *delta = "";

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

void sim_debug_bits(uint32 dbits, DEVICE* dptr, BITFIELD* bitdefs,
    uint32 before, uint32 after, int terminate)
{
if (sim_deb && (dptr->dctrl & dbits)) {
    if (!debug_unterm)
        fprintf(sim_deb, "%s", sim_debug_prefix(dbits, dptr));                      /* print prefix if required */
    fprint_fields (sim_deb, (t_value)before, (t_value)after, bitdefs); /* print xlation, transition */
    if (terminate)
        fprintf(sim_deb, "\r\n");
    debug_unterm = terminate ? 0 : 1;                   /* set unterm for next */
    }
}

/* Inline debugging - will print debug message if debug file is
   set and the bitmask matches the current device debug options.
   Extra returns are added for un*x systems, since the output
   device is set into 'raw' mode when the cpu is booted,
   and the extra returns don't hurt any other systems. 
   Callers should be calling sim_debug() which is a macro
   defined in scp.h which evaluates the action condition before 
   incurring call overhead. */

void _sim_debug (uint32 dbits, DEVICE* dptr, const char* fmt, ...)
{
if (sim_deb && (dptr->dctrl & dbits)) {

    char stackbuf[STACKBUFSIZE];
    int32 bufsize = sizeof(stackbuf);
    char *buf = stackbuf;
    va_list arglist;
    int32 i, j, len;
    char* debug_type = get_dbg_verb (dbits, dptr);
    const char* debug_prefix = sim_debug_prefix(dbits, dptr);   /* prefix to print if required */

    buf[bufsize-1] = '\0';

    while (1) {                                         /* format passed string, args */
        va_start (arglist, fmt);
#if defined(NO_vsnprintf)
#if defined(HAS_vsprintf_void)

/* Note, this could blow beyond the buffer, and we couldn't tell */
/* That is a limitation of the C runtime library available on this platform */

        vsprintf (buf, fmt, arglist);
        for (len = 0; len < bufsize-1; len++)
            if (buf[len] == 0) break;
#else
        len = vsprintf (buf, fmt, arglist);
#endif                                                  /* HAS_vsprintf_void */
#else                                                   /* NO_vsnprintf */
#if defined(HAS_vsnprintf_void)
        vsnprintf (buf, bufsize-1, fmt, arglist);
        for (len = 0; len < bufsize-1; len++)
            if (buf[len] == 0) break;
#else
        len = vsnprintf (buf, bufsize-1, fmt, arglist);
#endif                                                  /* HAS_vsnprintf_void */
#endif                                                  /* NO_vsnprintf */
        va_end (arglist);

/* If the formatted result didn't fit into the buffer, then grow the buffer and try again */

        if ((len < 0) || (len >= bufsize-1)) {
            if (buf != stackbuf)
                free (buf);
            bufsize = bufsize * 2;
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
                    if (debug_unterm)
                        fprintf (sim_deb, "%.*s\r\n", i-j, &buf[j]);
                    else                                /* print prefix when required */
                        fprintf (sim_deb, "%s%.*s\r\n", debug_prefix, i-j, &buf[j]);
                    }
                debug_unterm = 0;
                }
            j = i + 1;
            }
        }
    if (i > j) {
        if (debug_unterm)
            fprintf (sim_deb, "%.*s", i-j, &buf[j]);
        else                                        /* print prefix when required */
            fprintf (sim_deb, "%s%.*s", debug_prefix, i-j, &buf[j]);
        }

/* Set unterminated flag for next time */

    debug_unterm = (len && (buf[len-1]=='\n')) ? 0 : 1;
    if (buf != stackbuf)
        free (buf);
    }
return;
}
