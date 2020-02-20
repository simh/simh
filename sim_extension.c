/* sim_extension.c: SCP extension routines

   Copyright (c) 2019-2020, J. David Bryan

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   14-Feb-20    JDB     First release version
   18-Mar-19    JDB     Created


   This module implements extensions to the base Simulation Control Program
   (SCP) front end for SIMH version 3.x.  The current extensions are:

     - host serial port support for the console and terminal multiplexers

     - automated prompt/response processing, initially for the system console,
       but extendable to other keyboard/display units

     - concurrent console mode to enter SCP commands without stopping simulation

     - work-alikes for a subset of the SCP 4.x commands

     - execution of a global initialization file at simulator startup

   This module, and its associated declarations file (sim_extension.h) act as
   shims between the front end and a simulator-specific back end, such as the HP
   2100 or HP 3000 simulator.  Each simulator back end must have this inclusion:

     #include "sim_extension.h"

   ...placed in the "<sim>_defs.h" file that is included by all back-end
   modules, and all back-end modules must be compiled with the inclusion.  Also,
   this module (sim_extension.c) must be compiled and linked with the other SCP
   and back-end modules.

   Extending SCP is possible by the use of hooks and shims.  SCP 3.x provides a
   number of replaceable function and variable pointers (the "hooks") that may
   be altered to implement custom behavior.  Hooks are necessary where the
   internal behavior of the SCP must change -- for example, when adding new
   command executors to the original table of commands.  A one-time initializer
   within this module is called by SCP at simulator startup.  This initializer
   points the desired hooks at functions within this module to implement the
   extended actions.

   To extend the capability at the interface between the SCP front end and the
   simulator-specific back end, shims are used to intercept the calls between
   them.  One call from the front end to the back end -- "sim_instr" -- is
   intercepted.  All of the other shims are for calls from the back end to
   front-end services or global variables set by the back end.  The general
   mechanism is to use a macro to rename a given function identifier within the
   context of the back end. The new name refers to the extension shim, which,
   internally, may call the original function.  For example, "sim_extension.h"
   includes this macro:

     #define sim_putchar       ex_sim_putchar

   ...which is included during the back-end compilation.  Therefore, a back-end
   module that called "sim_putchar" would actually be compiled to call
   "ex_sim_putchar", a function within this module, instead.  Within the shim,
   the macro substitution is not done, so a call to "sim_putchar" calls the
   front-end function.


   The following shims are provided:

     Shimmed Routine     Source Module   Shimming Routine
     ================    =============   =================
     tmxr_poll_conn      sim_tmxr.c      ex_tmxr_poll_conn

     sim_putchar         sim_console.c   ex_sim_putchar
     sim_putchar_s       sim_console.c   ex_sim_putchar_s
     sim_poll_kbd        sim_console.c   ex_sim_poll_kbd

     sim_brk_test        scp.c           ex_sim_brk_test

     sim_instr           hp----_cpu.c    vm_sim_instr
     sim_vm_init         hp----_sys.c    vm_sim_vm_init
     sim_vm_cmd          hp----_sys.c    vm_sim_vm_cmd


   The following extensions are provided:

     Extension Routine   Module Extended  Extended Action
     ==================  ===============  ===================================
     tmxr_attach_unit    sim_tmxr.c       Attach a network or serial port
     tmxr_detach_unit    sim_tmxr.c       Detach a network or serial port
     tmxr_detach_line    sim_tmxr.c       Detach a serial port
     tmxr_control_line   sim_tmxr.c       Control a line
     tmxr_line_status    sim_tmxr.c       Get a line's status
     tmxr_line_free      sim_tmxr.c       Check if a line is disconnected
     tmxr_mux_free       sim_tmxr.c       Check if all lines are disconnected


   The following extension hooks are provided:

     Hook Name                Hook Description
     ======================   ===========================
     vm_console_input_unit    Console input unit pointer
     vm_console_output_unit   Console output unit pointer


   The following SCP hooks are used:

     Hook Name           Source Module
     ================    =============
     sim_vm_init         scp.c
     sim_vm_cmd          scp.c
     sim_vm_unit_name    scp.c
     sub_args            scp.c
     sim_get_radix       scp.c

     tmxr_read           sim_tmxr.c
     tmxr_write          sim_tmxr.c
     tmxr_show           sim_tmxr.c
     tmxr_close          sim_tmxr.c
     tmxr_is_extended    sim_tmxr.c

   The extension hooks should be set by the back end to point at the console
   units.  They are used to identify the console units for the REPLY and BREAK
   commands.

   The VM-specific SCP hooks may be set as usual by the back end.  The extension
   module intercepts these but ensures that they are called as part of extension
   processing, so their effects are maintained.


   Implementation notes:

    1. If the base set of options to the SHOW CONSOLE command is changed in
       "scp.c", the "ex_show_console" routine below must be changed as well.
       See the routine's implementation note for details.

    2. The existing SCP command table model presents some difficulties when
       extending the command set.  When adding new commands that begin with the
       same letter(s) as existing commands, the existing commands must be
       duplicated in the extension table before the new ones.  This is required
       to preserve the standard command abbreviation behavior when the
       extension table is searched first.  For example, when adding a new CLEAR
       command, the existing CONTINUE command must be duplicated and placed
       ahead of the CLEAR entry, so that entering "C" invokes CONTINUE and not
       CLEAR.  But duplicating the CONTINUE entry introduces the potential for
       error if the standard CONTINUE entry is changed.  This means that
       duplicated entries must be copied dynamically, which then means that the
       new table cannot be a constant.

       Also, in some standard and extension cases, additional actions may need
       to be taken for specific commands.  The method of identifying the
       command by testing the "action" field of the resulting CTAB entry does
       not work if the command has been replaced.  For instance, testing
       "cmdp->action == &do_cmd" doesn't work if the DO command was replaced.
       The only reliable way is to test the "name" field for the expected
       command name string, i.e., testing "strcmp (cmdp->name, "DO") == 0",
       which is both slow and awkward.

       Further, new commands may be valid only in certain contexts, e.g., in a
       command file or only during some specific mode of execution.  There is
       no easy way of passing context to the command executor, except by global
       variables, as all executors receive the same parameters (an integer
       argument and a character pointer).

       In short, we have to jump through some hoops in implementing the command
       extensions.  These are described in the comments associated with their
       appearances.
*/



#define COMPILING_EXTENSIONS                    /* defined to prevent shimming in sim_extension.h */

#include <ctype.h>
#include <float.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>

#include "sim_defs.h"
#include "sim_serial.h"
#include "sim_extension.h"



/* The following pragmas quell clang and Microsoft Visual C++ warnings that are
   on by default but should not be, in my opinion.  They warn about the use of
   perfectly valid code and require the addition of redundant parentheses and
   braces to silence them.  Rather than clutter up the code with scores of extra
   symbols that, in my view, make the code harder to read and maintain, I elect
   to suppress these warnings.

   VC++ 2008 warning descriptions:

    - 4114: "same type qualifier used more than once" [legal per C99]

    - 4554: "check operator precedence for possible error; use parentheses to
            clarify precedence"

    - 4996: "function was declared deprecated"
*/

#if defined (__clang__)

  #pragma clang diagnostic ignored "-Wlogical-not-parentheses"
  #pragma clang diagnostic ignored "-Wlogical-op-parentheses"
  #pragma clang diagnostic ignored "-Wbitwise-op-parentheses"
  #pragma clang diagnostic ignored "-Wshift-op-parentheses"
  #pragma clang diagnostic ignored "-Wdangling-else"

#elif defined (_MSC_VER)

  #pragma warning (disable: 4114 4554 4996)

#endif


/* MSVC does not have the C-standard "snprintf" function */

#if defined (_MSC_VER)
  #define snprintf  _snprintf
#endif


/* Character constants (as integers) */

#define BS                  0010
#define CR                  0015
#define LF                  0012
#define ESC                 0033
#define DEL                 0177


/* Flags for restricted-use commands */

#define EX_GOTO             0
#define EX_CALL             1
#define EX_RETURN           2
#define EX_ABORT            3


/* External variable declarations (actually should be in sim_console.h) */

extern TMXR sim_con_tmxr;


/* External routine declarations (actually should be in scp.h) */

extern t_stat show_break (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);


/* One-time extension initializer */

static void ex_initialize (void);


/* Hooks provided by SCP */

void (*sim_vm_init) (void) = ex_initialize;     /* use our one-time initializer */


/* Hooks provided by us for the back end virtual machine */

CTAB *vm_sim_vm_cmd = NULL;

void (*vm_sim_vm_init) (void);

UNIT *vm_console_input_unit;                    /* console input unit pointer */
UNIT *vm_console_output_unit;                   /* console output unit pointer */


/* Pointer to the VM handler for unit names */

static char * (*vm_unit_name_handler) (const UNIT *uptr) = NULL;



/* Extended terminal multiplexer line descriptor.

   This structure extends the TMLN structure defined by the multiplexer library
   to enable serial port support.  The TMLN structure contains a void extension
   pointer, "exptr", which will be initialized to NULL by the line descriptor
   declarations in the various multiplexer simulators.  For lines controlled by
   extension routines, this pointer is changed to point at an EX_TMLN extension
   structure, which is defined below.


   Implementation notes:

    1. The name of the serial port is kept in an allocated buffer and referenced
       by the UNIT's "filename" pointer.  The "sername" pointer points at the
       same buffer; it is needed only to permit the "ex_tmxr_show" routine to
       print the name when given the TMLN structure.  This pointer must NOT be
       freed; the buffer is deallocated by freeing the "filename" pointer.
*/

typedef struct {                                /* extended line descriptor */
    SERHANDLE  serport;                         /*   serial port handle */
    char       *sername;                        /*   copy of the serial port name pointer */
    t_bool     controlled;                      /*   TRUE if the modem lines are controlled */
    TMCKT      signals;                         /*   modem control signals */
    } EX_TMLN;


/* Pointers to the standard routines provided by the TMXR library */

static int32 (*tmxr_base_read)  (TMLN *lp, int32 length);
static int32 (*tmxr_base_write) (TMLN *lp, int32 length);
static void  (*tmxr_base_show)  (TMLN *lp, FILE *stream);
static void  (*tmxr_base_close) (TMLN *lp);


/* Hooked terminal multiplexer replacement routine declarations */

static int32  ex_tmxr_read     (TMLN *lp, int32 length);
static int32  ex_tmxr_write    (TMLN *lp, int32 length);
static void   ex_tmxr_show     (TMLN *lp, FILE *stream);
static void   ex_tmxr_close    (TMLN *lp);
static t_bool ex_tmxr_extended (TMLN *lp);


/* Local terminal multiplexer extension routine declarations */

static t_stat  ex_tmxr_attach_line (TMXR *mp, UNIT *uptr, char *cptr);
static EX_TMLN *serial_line        (TMLN *lp);



/* String breakpoint structure.

   String breakpoints are implemented by shimming the terminal output routines
   and matching each output character to a breakpoint string.  A string
   breakpoint structure holds the character string to be matched and some
   additional data that defines how the breakpoint is handled.  The structure is
   populated by a BREAK command having a quoted string parameter.  The structure
   may exist only until the breakpoint occurs (a "temporary" breakpoint) or
   until a NOBREAK command is issued to cancel it (a "permanent" breakpoint).
   Consequently, a breakpoint may be reused and so much contain enough
   information to allow the breakpoint to be reset after it triggers.

   The set of active breakpoint structures are maintained in a linked list
   headed by the "sb_list" global variable.


   Implementation notes:

    1. The structure contains some fields (e.g., "count") that are not used in
       the initial implementation.  They are present for future expansion.

    2. String breakpoints are initially implemented for the system console only.
       Future expansion to allow breakpoints on terminal multiplexer lines is
       envisioned.

    3. The trigger field contains the simulation global time at which a matched
       breakpoint should trigger.  It is set to -1 if the breakpoint has not yet
       matched (i.e., is still pending).
*/

typedef struct sbnode {                         /* string breakpoint descriptor */
    UNIT          *uptr;                        /*   output unit pointer */
    char          match [CBUFSIZE];             /*   match string */
    char          *mptr;                        /*   match pointer */
    int32         type;                         /*   mask of breakpoint types */
    int32         count;                        /*   proceed count */
    int32         delay;                        /*   trigger enable delay */
    double        trigger;                      /*   trigger time */
    char          action [CBUFSIZE];            /*   action string */
    struct sbnode *next;                        /*   pointer to the next entry in the list */
    } STRING_BREAKPOINT;

typedef STRING_BREAKPOINT *SBPTR;               /* the string breakpoint node pointer type */

static SBPTR sb_list = NULL;                    /* the pointer to the head of the breakpoint list */

#define BP_STRING           (SWMASK ('_'))      /* the default string breakpoint type */


/* String breakpoint local SCP support routines */

static char  *breakpoint_name    (const UNIT *uptr);
static t_stat breakpoint_service (UNIT *uptr);


/* String breakpoint SCP data structures */

/* Unit list */

static UNIT breakpoint_unit [] = {
/*           Event Routine        Unit Flags  Capacity  Delay */
/*           -------------------  ----------  --------  ----- */
    { UDATA (&breakpoint_service, UNIT_IDLE,     0),      0   }
    };


/* Reply structure.

   Replies are implemented by shimming the terminal input routines and supplying
   characters one-at-a-time from a response string.  A reply structure holds the
   character string to be supplied and some additional data that defines how the
   reply is handled.  The structure is populated by a REPLY command having a
   quoted string parameter.  The structure exists only until the reply is
   completed.


   Implementation notes:

    1. Replies are initially implemented for the system console only.  Future
       expansion to allow replies on terminal multiplexer lines is envisioned.

    2. Only a single reply may exist in the initial implementation.  Multiple
       replies for different devices would be implemented by a linked-list of
       structures that are allocated dynamically.  Initially, though, the
       "reply" global points at a single static structure instead of the head of
       a list of structures.

    3. A reply is pending if "rptr" points at "reply [0]" and the current
       simulation time is earlier than the "trigger" time.
*/

typedef struct rpnode {                         /* reply descriptor */
    UNIT          *uptr;                        /*   input unit pointer */
    char          reply [CBUFSIZE];             /*   reply string */
    char          *rptr;                        /*   reply pointer */
    double        trigger;                      /*   trigger time */
    struct rpnode *next;                        /*   pointer to the next entry in the list */
    } REPLY;

typedef REPLY *RPPTR;                           /* the reply node pointer type */

static RPPTR rp_list = NULL;                    /* the pointer to the head of the reply list */

static REPLY rpx;                               /* the initial reply descriptor */


/* Local default break and reply delay declarations */

static int32 break_delay = 0;
static int32 reply_delay = 0;


/* Local string breakpoint extension routine declarations */

static void  free_breakpoints (void);
static SBPTR find_breakpoint  (char *match, SBPTR *prev);
static void  test_breakpoint  (int32 test_char);


/* Concurrent console mode status returns */

#define SCPE_EXEC   (SCPE_LAST + 1)             /* a command is ready to execute */
#define SCPE_ABORT  (SCPE_LAST + 2)             /* an ABORT command was entered */


/* Concurrent console mode enumerator */

typedef enum {                                  /* keyboard mode enumerator */
    Console,                                    /*   keystrokes are sent to the console */
    Command                                     /*   keystrokes are sent to the command buffer */
    } KEY_MODE;


/* Signal handler function type */

typedef void (*SIG_HANDLER) (int);


/* Concurrent console mode local variables */

static t_bool concurrent_mode = TRUE;           /* the console mode, initially in Concurrent mode */

static KEY_MODE keyboard_mode = Console;        /* the keyboard mode, initially delivered to the console */

static char cmd_buf [CBUFSIZE];                 /* the concurrent console command buffer */
static char *cmd_ptr;                           /* the command buffer pointer */
static char *concurrent_do_ptr = NULL;          /* the pointer to a concurrent DO command line */

static t_bool concurrent_run = FALSE;           /* TRUE if the VM is executing in concurrent mode */
static t_bool stop_requested = FALSE;           /* TRUE if a CTRL+E was entered by the user */


/* Concurrent console mode local routine declarations */

static void   wru_handler (int sig);
static void   put_string  (const char *cptr);
static t_stat get_command (char *cptr, CTAB **cmdp);



/* Local command extension declarations */

static int32 ex_quiet = 0;                      /* a copy of the global "sim_quiet" setting  */


/* Command handler function type */

typedef t_stat CMD_HANDLER (int32 flag, char *cptr);


/* Command extension handler function declarations */

static t_stat ex_break_cmd      (int32 flag, char *cptr);
static t_stat ex_reply_cmd      (int32 flag, char *cptr);
static t_stat ex_run_cmd        (int32 flag, char *cptr);
static t_stat ex_do_cmd         (int32 flag, char *cptr);
static t_stat ex_if_cmd         (int32 flag, char *cptr);
static t_stat ex_delete_cmd     (int32 flag, char *cptr);
static t_stat ex_restricted_cmd (int32 flag, char *cptr);
static t_stat ex_set_cmd        (int32 flag, char *cptr);
static t_stat ex_show_cmd       (int32 flag, char *cptr);


/* Command extension table.

   This table defines commands and command behaviors that are specific to this
   extension.  Specifically:

     * RUN and GO accept an UNTIL clause, and all forms provide a quiet DO mode

     * BREAK and NOBREAK provide temporary and string breakpoints

     * REPLY and NOREPLY provide console responses

     * DO provides transfers to labels and quiet stops

     * IF and GOTO add conditional command file execution

     * DELETE adds platform-independent file purges

     * CALL and RETURN add labeled subroutine invocations

     * ABORT stops execution of the current and any nested DO command files

     * SET adds ENVIRONMENT to define environment variables
       and CONSOLE CONCURRENT/NOCONCURRENT to set the console concurrency mode

     * SHOW adds string breakpoints to BREAK and adds REPLY for replies

   The table is initialized with only those fields that differ from the standard
   command table.  During one-time initialization, empty or zero fields are
   filled in from the corresponding standard command table entries.  This
   ensures that the extension table automatically picks up any changes to the
   standard commands that it modifies.


   Implementation notes:

    1. The RESET and DEPOSIT commands are duplicated from the standard SCP
       command table so that entering "R" doesn't invoke the RUN command and
       entering "D" doesn't invoke the DO command.  This would otherwise occur
       because the extension command table is searched before the standard
       command table.  Similarly, the ATTACH, ASSIGN, and ASSERT commands are
       duplicated so that entering "A" doesn't invoke the ABORT command.

    2. The "execute_file" routine needs to do special processing for the RUN,
       GO, STEP, CONTINUE, and BOOT commands.  Unfortunately, there is no easy
       way to determine these commands from their CTAB entries.  We cannot
       simply check the action routine pointers, because the VM may override
       them, potentially with five different routines.  We can't use special
       argument values, because they are used by the standard RUN executor to
       differentiate between the various commands (and they are not unique to
       these commands).  What we need is a VM extension field in the CTAB
       structure, but there isn't one.  So we rely on the hack that nothing in
       3.x uses the "help_base" field, which was added for 4.x compatibility. We
       therefore indicate a RUN, etc. command by setting the "help_base" field
       non-null (the actual value is, so far, immaterial).
*/

static CTAB ex_cmds [] = {
/*    Name        Action Routine     Argument     Help String                                                       */
/*    ----------  -----------------  -----------  ----------------------------------------------------------------- */
    { "RESET",    NULL,               0,          NULL                                                              },
    { "DEPOSIT",  NULL,               0,          NULL                                                              },
    { "ATTACH",   NULL,               0,          NULL                                                              },
    { "ASSIGN",   NULL,               0,          NULL                                                              },
    { "ASSERT",   NULL,               0,          NULL                                                              },

    { "RUN",      ex_run_cmd,         0,          NULL, "RUN"                                                       },
    { "GO",       ex_run_cmd,         0,          NULL, "RUN"                                                       },
    { "STEP",     ex_run_cmd,         0,          NULL, "RUN"                                                       },
    { "CONTINUE", ex_run_cmd,         0,          NULL, "RUN"                                                       },
    { "BOOT",     ex_run_cmd,         0,          NULL, "RUN"                                                       },

    { "BREAK",    ex_break_cmd,       0,          NULL                                                              },
    { "NOBREAK",  ex_break_cmd,       0,          NULL                                                              },

    { "REPLY",    ex_reply_cmd,       0,          "reply <string> {<delay>} send characters to the console\n"       },
    { "NOREPLY",  ex_reply_cmd,       1,          "noreply                  cancel a pending reply\n"               },

    { "DO",       ex_do_cmd,          1,          NULL                                                              },

    { "IF",       ex_if_cmd,          0,          "if <cond> <cmd>;...      execute commands if condition TRUE\n"   },
    { "DELETE",   ex_delete_cmd,      0,          "del{ete} <file>          delete a file\n"                        },

    { "GOTO",     ex_restricted_cmd,  EX_GOTO,    "goto <label>             transfer control to the labeled line\n" },
    { "CALL",     ex_restricted_cmd,  EX_CALL,    "call <label> {<par>...}  call the labeled subroutine\n"          },
    { "RETURN",   ex_restricted_cmd,  EX_RETURN,  "return                   return control from a subroutine\n"     },
    { "ABORT",    ex_restricted_cmd,  EX_ABORT,   "abort                    abort nested command files\n"           },

    { "SET",      ex_set_cmd,         0,          NULL                                                              },
    { "SHOW",     ex_show_cmd,        0,          NULL                                                              },

    { NULL }
    };

static const uint32 ex_cmd_count = sizeof ex_cmds / sizeof ex_cmds [0]; /* the count of commands in the table */


/* Standard front-end command handler pointer declarations */

static CMD_HANDLER *break_handler = NULL;
static CMD_HANDLER *run_handler   = NULL;
static CMD_HANDLER *set_handler   = NULL;
static CMD_HANDLER *show_handler  = NULL;


/* Extended command handler pointer declarations */

static CMD_HANDLER *ex_do_handler = NULL;


/* SET/SHOW command extension handler routine declarations */

static t_stat ex_set_console     (int32 flag, char *cptr);
static t_stat ex_set_environment (int32 flag, char *cptr);
static t_stat ex_set_concurrent  (int32 flag, char *cptr);
static t_stat ex_set_serial      (int32 flag, char *cptr);

static t_stat ex_show_console    (FILE *stream, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
static t_stat ex_show_break      (FILE *stream, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
static t_stat ex_show_reply      (FILE *stream, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
static t_stat ex_show_delays     (FILE *stream, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
static t_stat ex_show_concurrent (FILE *stream, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
static t_stat ex_show_serial     (FILE *stream, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);


/* Hooked command extension replacement routine declarations */

static void  ex_substitute_args (char *iptr, char *optr, int32 bufsize, char *args []);
static int32 ex_get_radix       (const char *cptr, int32 switches, int32 default_radix);


/* Local command extension routine declarations */

static t_stat execute_file         (FILE *file, int32 flag, char *cptr);
static t_stat goto_label           (FILE *stream, char *cptr);
static t_stat gosub_label          (FILE *stream, char *filename, int32 flag, char *cptr);
static void   replace_token        (char **out_ptr, int32 *out_size, char *token_ptr);
static void   copy_string          (char **target, int32 *target_size, const char *source, int32 source_size);
static char   *parse_quoted_string (char *sptr, char *dptr, t_bool upshift);
static t_stat parse_delay          (char **cptr, int32 *delay);
static char   *encode              (const char *source);



/* *********************  Extension Module Initializer  ************************

   This routine is called once by the SCP startup code.  It fills in the
   extension command table from the corresponding system command table entries,
   saves pointers to the original system command handlers where needed, and
   installs the extension command table and argument substituter.

   If the VM defines an initializer, it is called.  Then if the VM set up its
   own command table, this routine merges the two auxiliary tables, ensuring
   that any VM-defined commands override the corresponding extension commands.
   This is required because SCP only allows a single user-specified command
   table.


   Implementation notes:

    1. The "ex_cmd_count" includes the NULL entry at the end of the extension
       command table, so adding in any VM command table entries gives the total
       command count plus one for the NULL entry, as is needed for the correct
       memory allocation size.

    2. Because the initializer is a void function, it cannot return an
       indication that the memory allocation failed.  If it did, the VM-defined
       command table will be ignored.

    3. For those overriding extension commands that will call the system command
       handlers (e.g., BREAK), we save the action routine pointers before
       installing the extension command table.  Similarly, those overriding VM
       commands that intend to call the system command handlers will save the
       corresponding action pointers.  However, because the extension table is
       installed before calling the VM initializer, the VM's action pointers
       will point at the extension command handlers.  So, for example, a BREAK
       command calls the VM BREAK handler, which in turn calls the extension
       BREAK handler, which in turn calls the system BREAK handler.
*/

static void ex_initialize (void)
{
uint32 cmd_count;
CTAB   *systab, *vmtab, *extab = ex_cmds;

while (extab->name != NULL) {                           /* loop through the extension command table */
    systab = find_cmd (extab->name);                    /* find the corresponding system command table entry */

    if (systab != NULL) {                               /* if it is present */
        if (extab->action == NULL)                      /*   then if the action routine field is empty */
            extab->action = systab->action;             /*     then fill it in */

        if (extab->arg == 0)                            /* if the command argument field is empty */
            extab->arg = systab->arg;                   /*   then fill it in */

        if (extab->help == NULL)                        /* if the help string field is empty */
            extab->help = systab->help;                 /*   then fill it in */

        if (extab->help_base == NULL)                   /* if the help base string field is empty */
            extab->help_base = systab->help_base;       /*   then fill it in */

        extab->message = systab->message;               /* fill in the message field as we never override it */
        }

    extab++;                                            /* point at the next table entry */
    }

break_handler = find_cmd ("BREAK")->action;             /* set up the BREAK/NOBREAK command handler */
run_handler   = find_cmd ("RUN")->action;               /*   and the RUN/GO command handler */
set_handler   = find_cmd ("SET")->action;               /*     and the SET command handler */
show_handler  = find_cmd ("SHOW")->action;              /*       and the SHOW command handler */

sim_vm_cmd    = ex_cmds;                                /* set up the extension command table */
sub_args      = ex_substitute_args;                     /*   and argument substituter */
sim_get_radix = ex_get_radix;                           /*     and EX/DEP/SET radix configuration */

if (vm_sim_vm_init != NULL)                             /* if the VM has a one-time initializer */
    vm_sim_vm_init ();                                  /*   then call it now */

vm_unit_name_handler = sim_vm_unit_name;                /* save the unit name hook in case the VM set it */
sim_vm_unit_name = breakpoint_name;                     /*   and substitute our own */

if (vm_sim_vm_cmd != NULL) {                            /* if the VM defines its own command table */
    cmd_count = ex_cmd_count;                           /*   then extension table entry count */

    for (vmtab = vm_sim_vm_cmd; vmtab->name != NULL; vmtab++)   /* add the number of VM command entries */
        cmd_count = cmd_count + 1;                              /*   to the number of extension entries */

    systab = (CTAB *) calloc (cmd_count, sizeof (CTAB));    /* allocate a table large enough to hold all entries */

    if (systab != NULL) {                               /* if the allocation succeeded */
        memcpy (systab, ex_cmds, sizeof ex_cmds);       /*   then populate the extension commands first */

        for (vmtab = vm_sim_vm_cmd; vmtab->name != NULL; vmtab++) { /* for each VM command */
            for (extab = systab; extab->name != NULL; extab++)      /*   if it overrides */
                if (strcmp (extab->name, vmtab->name) == 0) {       /*     an extension command */
                    memcpy (extab, vmtab, sizeof (CTAB));           /*       then replace the extension entry */
                    break;                                          /*         with the VM entry */
                    }

            if (extab->name == NULL)                    /* if the VM command does not match an extension command */
                memcpy (extab, vmtab, sizeof (CTAB));   /*   then add it to the end of the table */
            }

        sim_vm_cmd = systab;                            /* install the combined VM and extension table */
        }
    }

ex_do_handler = find_cmd ("DO")->action;                /* get the address of the extended DO command handler */

ex_quiet = sim_quiet;                                   /* save the global quietness setting */

tmxr_base_read  = tmxr_read;                            /* get the dedicated socket reader */
tmxr_read       = ex_tmxr_read;                         /*   and replace it with the generic reader */

tmxr_base_write = tmxr_write;                           /* do the same */
tmxr_write      = ex_tmxr_write;                        /*   for the generic writer */

tmxr_base_show  = tmxr_show;                            /* and the same */
tmxr_show       = ex_tmxr_show;                         /*   for the generic show */

tmxr_base_close = tmxr_close;                           /* and the same */
tmxr_close      = ex_tmxr_close;                        /*   for the generic closer */

tmxr_is_extended = ex_tmxr_extended;                    /* install the extension detection hook */

return;
}



/* ********************  Terminal Multiplexer Extensions  **********************

   This module extends the following existing routines in "sim_tmxr.c":

     tmxr_poll_conn -- poll for new network or serial connections

   ...and adds the following new routines:

     tmxr_attach_unit   -- attach a network or serial port
     tmxr_detach_unit   -- detach a network or serial port
     tmxr_detach_line   -- detach a serial port
     tmxr_control_line  -- control a line
     tmxr_line_status   -- get a line' status
     tmxr_line_free     -- return TRUE if a specified line is disconnected
     tmxr_mux_free      -- return TRUE if all lines and network port are disconnected

   The module implementation requires that multiline multiplexers define a unit
   per line and that unit numbers correspond to line numbers.

   Multiplexer lines may be connected to terminal emulators supporting the
   Telnet protocol via sockets, or to hardware terminals via host serial ports.
   Concurrent Telnet and serial connections may be mixed on multiline
   multiplexers.

   When connecting via serial ports, individual multiplexer lines are attached
   to specific host ports using port names appropriate for the host system:

     sim> attach MUX2 com1      (or /dev/ttyS0)

   Serial port parameters may be optionally specified:

     sim> attach MUX2 com1;9600-8n1

   If the port parameters are omitted, then the host system defaults for the
   specified port are used.  The port is allocated during the attach call, but
   the actual connection is deferred until the multiplexer is polled for
   connections, as with Telnet connections.

   Individual lines may be disconnected from serial ports with:

     sim> detach MUX2

   Telnet and serial port connections may be dropped with:

     sim> set MUX2 DISCONNECT

   This will disconnect a Telnet client and will drop the Data Terminal Ready
   (DTR) signal on a serial port for 500 milliseconds.  The serial port remains
   attached to the line.


   Single-line devices may be attached either to a Telnet listening port or to a
   serial port.  The device attach routine may be passed either a port number or
   a serial port name.  This routine calls "tmxr_attach_unit".  If the return
   value is SCPE_OK, then a Telnet port number or serial port name was passed
   and was opened.  Otherwise, the attachment failed, and the returned status
   code value should be reported.

   The device detach routine calls "tmxr_detach_unit" to close either the Telnet
   listening port or the serial port.

   Multi-line devices with a unit per line and a separate poll unit attach
   serial ports to the former and a Telnet listening port to the latter.  Both
   types of attachments may be made concurrently.  The system ATTACH and DETACH
   commands are used, for example:

     sim> attach MUX 1050       -- attach the listening port
     sim> attach MUX0 com1      -- attach a serial port

   SCP passes the same pointer to unit 0 in both cases.  However, the SCP global
   variable "sim_ref_type" will indicate whether the device (REF_DEVICE) or a
   unit (REF_UNIT) was specified.  In the cases of a RESTORE or a DETACH ALL,
   where the user does not specify a device or unit, "sim_ref_type" will be set
   to REF_NONE.

   After a line is detached, the device simulator should clear the "rcve" field
   of the associated line descriptor.  However, detaching a listening port will
   disconnect all active Telnet lines but will not disturb any serial lines.
   Consequently, the device simulator cannot determine whether or not a
   multiplexer is active solely by examining the UNIT_ATT flag on the poll unit.
   Instead, the "tmxr_line_free" routine should be called for each line, and
   reception on the line should be inhibited if the routine returns TRUE.

   Finally, the "tmxr_mux_free" routine should be called to determine if the
   multiplexer is now free (i.e., the listening port is detached and no other
   serial connections exist).  If the routine returns TRUE, the poll service may
   be stopped.

   The "tmxr_attach_unit" and "tmxr_detach_unit" decide which type of port to
   use by examining the UNIT_ATTABLE flag on the supplied unit.  If the flag is
   present, then a Telnet port attach/detach is attempted.  If the flag is
   missing, then a serial port is assumed.  Multiline multiplexers therefore
   will specify UNIT_ATTABLE on the poll unit and not on the line units.

   Serial port connections are only supported on multiplexer lines that appear
   in the line connection order array.  If the line is not present, or the
   default value (i.e., -1 indicating all lines are to be connected) is not set,
   the attachment is rejected with a "Unit not attachable" error.


   Implementation notes:

    1. The system RESTORE command does not restore devices having the DEV_NET
       flag.  This flag indicates that the device employs host-specific port
       names that are non-transportable across RESTOREs.

       If a multiplexer specifies DEV_NET, the device connection state will not
       be altered when a RESTORE is done.  That is, all current connections,
       including Telnet sessions, will remain untouched, and connections
       specified at the time of the SAVE will not be reestablished during the
       RESTORE.  If DEV_NET is not specified, then the system will attempt to
       restore the attachment state present at the time of the SAVE, including
       Telnet listening and serial ports.  Telnet client sessions on individual
       multiplexer lines cannot be reestablished by RESTORE and must be
       reestablished manually.

    2. Single-line multiplexers must set or clear UNIT_ATTABLE on the unit
       representing the line dynamically, depending on whether a numeric
       (listening port) or non-numeric (serial port) value is specified.
       Multiline unit-per-line multiplexers should not have UNIT_ATTABLE on the
       units representing the lines.  UNIT_ATTABLE does not affect the
       attachability when VM-specific attach routines are employed, although it
       does determine which type of port is to be attached.  UNIT_ATTABLE also
       controls the reporting of attached units for the SHOW <dev> command.

       A single-line device will be either detached, attached to a listening
       port, or attached to a serial port.  With UNIT_ATTABLE, the device will
       be reported as "not attached," "attached to 1050" (e.g.), or "attached to
       COM1" (e.g.), which is desirable.

       A unit-per-line device will report the listening port as attached to the
       device (or to a separate device).  The units representing lines either
       will be connected to a Telnet session or attached to a serial port.
       Telnet sessions are not reported by SHOW <dev>, so having UNIT_ATTABLE
       present will cause each non-serial line to be reported as "not attached,"
       even if there may be a current Telnet connection.  This will be confusing
       to users.  Without UNIT_ATTABLE, attachment status will be reported only
       if the line is attached to a serial port, which is preferable.
*/


/* Global terminal multiplexer extension routines */


/* Attach a network or serial port.

   This extension for "tmxr_attach" attempts to attach the network or serial
   port name specified by "cptr" to the multiplexer line associated with mux
   descriptor pointer "mp" and unit pointer "uptr".  The unit is implicitly
   associated with the line number corresponding to the position of the unit in
   the zero-based array of units belonging to the associated device.

   The validity of the attachment is determined by the presence or absence of
   the UNIT_ATTABLE ("unit is attachable") flag on the unit indicated by "uptr".
   The Telnet poll unit will have this flag; the individual line units will not.
   The presence or absence of the flag determines the type of attachment to
   attempt.

   If a device is referenced, the poll unit specified by the "pptr" parameter is
   attached instead of the referenced unit.  This is because a device reference
   passes a pointer to unit 0 (i.e., ATTACH MUX and ATTACH MUX0 both set "uptr"
   to point at unit 0).

   An attempt to attach the poll unit directly via a unit reference will be
   rejected by the "ex_tmxr_attach_line" routine because the unit does not
   correspond to a multiplexer port.
*/

t_stat ex_tmxr_attach_unit (TMXR *mp, UNIT *pptr, UNIT *uptr, char *cptr)
{
t_stat status;

if (sim_ref_type == REF_DEVICE)                         /* if this is a device reference */
    uptr = pptr;                                        /*   then substitute the poll unit */

if (mp == NULL || pptr == NULL || uptr == NULL)         /* if the descriptor, poll, or unit pointer is null */
    status = SCPE_IERR;                                 /*   then report an internal error */

else if (sim_ref_type != REF_UNIT                       /* otherwise if this is a device or null reference */
  && uptr->flags & UNIT_ATTABLE)                        /*   and the poll unit is attachable */
    status = tmxr_attach (mp, uptr, cptr);              /*     then try to attach a listening port */

else                                                    /* otherwise it's a unit reference */
    status = ex_tmxr_attach_line (mp, uptr, cptr);      /*   so try to attach a serial port */

return status;                                          /* return the status of the attachment */
}


/* Detach a network or serial port.

   This extension for "tmxr_detach" attempts to detach the network or serial
   port from the multiplexer line associated with mux descriptor pointer "mp"
   and unit pointer "uptr".  The unit is implicitly associated with the line
   number corresponding to the position of the unit in the zero-based array of
   units belonging to the associated device.

   The validity of the detachment is determined by the presence or absence of
   the UNIT_ATTABLE ("unit is attachable") flag on the unit indicated by "uptr".
   The Telnet poll unit will have this flag; the individual line units will not.
   The presence or absence of the flag determines the type of detachment to
   attempt.

   If a device is referenced, the poll unit specified by the "pptr" parameter is
   detached instead of the referenced unit.  This is because a device reference
   passes a pointer to unit 0 (i.e., DETACH MUX and DETACH MUX0 both set "uptr"
   to point at unit 0).

   An attempt to detach the poll unit directly via a unit reference will be
   rejected by the "ex_tmxr_detach_line" routine because the unit does not
   correspond to a multiplexer port.
*/

t_stat ex_tmxr_detach_unit (TMXR *mp, UNIT *pptr, UNIT *uptr)
{
t_stat status;

if (sim_ref_type == REF_DEVICE)                         /* if this is a device reference */
    uptr = pptr;                                        /*   then substitute the poll unit */

if (mp == NULL || pptr == NULL || uptr == NULL)         /* if the descriptor, poll, or unit pointer is null */
    status = SCPE_IERR;                                 /*   then report an internal error */

else if (sim_ref_type != REF_UNIT                       /* otherwise if this is a device or null reference */
  && uptr->flags & UNIT_ATTABLE)                        /*   and the poll unit is attachable */
    status = tmxr_detach (mp, uptr);                    /*     then try to detach a listening port */

else                                                    /* otherwise it's a line unit */
    status = ex_tmxr_detach_line (mp, uptr);            /*   so try to detach a serial port */

return status;                                          /* return the status of the detachment */
}


/* Detach a line from serial port.

   This extension routine disconnects and detaches a line of the multiplexer
   associated with mux descriptor pointer "mp" and unit pointer "uptr" from its
   serial port. The line number is given by the position of the unit in the
   zero-based array of units belonging to the associated device.  For example,
   if "uptr" points to unit 3 in a given device, then line 3 will be detached.

   If the specified unit does not correspond with a multiplexer line, then
   SCPE_NOATT is returned.  If the line is not connected to a serial port, then
   SCPE_UNATT is returned.  Otherwise, the port is disconnected, and SCPE_OK is
   returned.


   Implementation notes:

    1. If the serial connection had been completed, we disconnect the line,
       which drops DTR to ensure that a modem will disconnect.
*/

t_stat ex_tmxr_detach_line (TMXR *mp, UNIT *uptr)
{
TMLN    *lp;
EX_TMLN *exlp;

if (uptr == NULL)                                       /* if this is a console reference */
    lp = mp->ldsc;                                      /*   point at the (only) line */
else                                                    /* otherwise */
    lp = tmxr_find_ldsc (uptr, mp->lines, mp);          /*   determine the line from the unit */

if (lp == NULL)                                         /* if the unit does not correspond to a line */
    return SCPE_NOATT;                                  /*   then report that the unit is not attachable */
else                                                    /* otherwise */
    exlp = serial_line (lp);                            /*   get the serial line extension */

if (exlp == NULL)                                       /* if the line is not a serial line */
    return SCPE_UNATT;                                  /*   then report that the unit is unattached */

if (lp->conn)                                           /* if the connection has been completed */
    tmxr_disconnect_line (lp);                          /*   then disconnect the line */

sim_close_serial (exlp->serport);                       /* close the serial port */
free (exlp->sername);                                   /*   and free the port name */

exlp->serport = INVALID_HANDLE;                         /* reinitialize the structure */
exlp->sername = NULL;                                   /*   to show it is not connected */

if (uptr != NULL) {                                     /* if this is not a console detach */
    uptr->filename = NULL;                              /*   then clear the port name pointer */

    uptr->flags &= ~UNIT_ATT;                           /* mark the unit as unattached */
    }

return SCPE_OK;                                         /* return success */
}


/* Control a terminal line.

   This extension routine controls a multiplexer line, specified by the "lp"
   parameter, as though it were connected to a modem.  The caller designates
   that the line's Data Terminal Ready (DTR) and Request To Send (RTS) signals
   should be asserted or denied as specified by the "control" parameter.  If the
   line is connected to a Telnet session, dropping DTR will disconnect the
   session.  If the line is connected to a serial port, the signals are sent to
   the device connected to the hardware port, which reacts in a device-dependent
   manner.

   Calling this routine establishes VM control over the multiplexer line.
   Control is only relevant when a line is attached to a serial port.

   Initially, a line is uncontrolled.  In this state, attaching a line to a
   serial port automatically asserts DTR and RTS, and detaching the line drops
   both signals.  After this routine has been called, this default action no
   longer occurs, and it is the responsibility of the VM to raise and lower DTR
   and RTS explicitly.

   The caller may reset a line to the uncontrolled state by calling the routine
   with the "control" parameter set to the "Reset_Control" value.  Typically,
   this is only necessary if a RESET -P (i.e., power-on reset) is performed.

   If a null pointer is passed for the "lp" parameter, the routine returns
   SCPE_IERR.  If the line extension structure has not been allocated when the
   routine is called, it is allocated here.  If the allocation fails, SCPE_MEM
   is returned.  If the line is attached to a serial port, the serial control
   routine status (SCPE_OK or SCPE_IOERR) is returned.  Otherwise, the routine
   returns SCPE_OK.


   Implementation notes:

    1. The TMCKT and SERCIRCUIT types are renamings of the underlying
       RS232_SIGNAL type, so a type cast is valid.
*/

t_stat ex_tmxr_control_line (TMLN *lp, TMCKT control)
{
EX_TMLN *exlp;
t_stat  status = SCPE_OK;

if (lp == NULL)                                         /* if the line pointer is invalid */
    return SCPE_IERR;                                   /*   then report an internal error */
else                                                    /* otherwise */
    exlp = (EX_TMLN *) lp->exptr;                       /*   point to the descriptor extension */

if (exlp == NULL) {                                     /* if the extension has not been allocated */
    lp->exptr = malloc (sizeof (EX_TMLN));              /*   then allocate it now */

    if (lp->exptr == NULL)                              /* if the memory allocation failed */
        return SCPE_MEM;                                /*   then report the failure */

    else {                                              /* otherwise */
        exlp = (EX_TMLN *) lp->exptr;                   /*   point to the new descriptor extension */

        exlp->serport = INVALID_HANDLE;                 /* clear the serial port handle */
        exlp->sername = NULL;                           /*   and the port name */
        }
    }

if (control == Reset_Control) {                         /* if a reset is requested */
    exlp->controlled = FALSE;                           /*   then mark the line as uncontrolled */

    if (lp->conn == 0)                                  /* if the line is currently disconnected */
        exlp->signals = No_Signals;                     /*   then default to no control signals */
    else                                                /* otherwise */
        exlp->signals = DTR_Control | RTS_Control;      /*   default to the connected control signals */
    }

else {                                                  /* otherwise signal control is requested */
    exlp->controlled = TRUE;                            /*   so mark the line as controlled */
    exlp->signals = control;                            /*     and record the requested signal states */

    if (exlp->serport != INVALID_HANDLE)                /* if the line is connected to a serial port */
        status = sim_control_serial (exlp->serport,     /*   then let the hardware handle it */
                                     (SERCIRCUIT) control);

    else if (lp->conn != 0                              /* otherwise if the Telnet line is currently connected */
      && (control & DTR_Control) == 0)                  /*   and DTR is being dropped */
        tmxr_disconnect_line (lp);                      /*     then disconnect the line */
    }

return status;                                          /* return the operation status */
}


/* Get a terminal line's status.

   This extension routine returns the status of a multiplexer line, specified by
   the "lp" parameter.  If the line is connected to a serial port, the hardware
   port status is returned.  If the line is connected to a Telnet port,
   simulated modem status (Data Set Ready, Clear To Send, and Data Carrier
   Detect) is returned.  If the line is not connected, no signals are returned.

   If a null pointer is passed for the "lp" parameter, the routine returns the
   Error_Status value.


   Implementation notes:

    1. The TMCKT and SERCIRCUIT types are renamings of the underlying
       RS232_SIGNAL type, so a type cast is valid.
*/

TMCKT ex_tmxr_line_status (TMLN *lp)
{
EX_TMLN *exlp;

if (lp == NULL)                                         /* if the line pointer is invalid */
    return Error_Status;                                /*   then report an internal error */
else                                                    /* otherwise */
    exlp = (EX_TMLN *) lp->exptr;                       /*   point to the descriptor extension */

if (exlp != NULL && exlp->serport != INVALID_HANDLE)    /* if the line is connected to a serial port */
    return (TMCKT) sim_status_serial (exlp->serport);   /*   then return the hardware port status */

else if (lp->conn != 0)                                 /* otherwise if the line is connected to a Telnet port */
    return DSR_Status | CTS_Status | DCD_Status;        /*   then simulate a connected modem */

else                                                    /* otherwise */
    return No_Signals;                                  /*   simulate a disconnected modem */
}


/* Poll for a new network or serial connection.

   This shim for "tmxr_poll_conn" polls for new Telnet or serial connections for
   the multiplexer descriptor indicated by "mp".  If a Telnet or serial
   connection is made, the routine returns the line number of the new
   connection.  Otherwise, the routine returns 0.  If a serial connection and a
   Telnet connection are both pending, the serial connection takes precedence.


   Implementation notes:

    1. When a serial port is attached to a line, the connection is made pending
       until we are called to poll for new connections.  This is because VM
       multiplexer service routines recognize new connections only as a result
       of calls to this routine.

    2. A pending serial (re)connection may be deferred.  This is needed when a
       line clear drops DTR, as DTR must remain low for a period of time in
       order to be recognized by the serial device.  If the "cnms" value
       specifies a time in the future, the connection is deferred until that
       time is reached.  This leaves DTR low for the necessary time.

    3. If the serial line is uncontrolled, the default control signals (i.e.,
       DTR and RTS) will be asserted.  Otherwise, the last signals set by the VM
       will be used.
*/

int32 ex_tmxr_poll_conn (TMXR *mp)
{
TMLN    *lp;
EX_TMLN *exlp;
int32   line;
uint32  current_time;

if (mp == NULL)                                         /* if the mux descriptor is invalid */
    return 0;                                           /*   then return "no connection" status */

current_time = sim_os_msec ();                          /* get the current time */

for (line = 0; line < mp->lines; line++) {              /* check each line in sequence for connections */
    lp = mp->ldsc + line;                               /* get a pointer to the line descriptor */
    exlp = serial_line (lp);                            /*   and to the serial line extension */

    if (exlp != NULL && lp->conn == 0                   /* if the line is a serial line but not yet connected */
      && current_time >= lp->cnms) {                    /*   and the connection time has been reached */
        tmxr_init_line (lp);                            /*     then initialize the line state */

        if (exlp->controlled == FALSE)                  /* if the line as uncontrolled */
            exlp->signals = DTR_Control | RTS_Control;  /*   then default to the connected control signals */

        sim_control_serial (exlp->serport, exlp->signals);  /* connect the line as directed */

        lp->conn = 1;                                   /* mark the line as now connected */
        lp->cnms = current_time;                        /* record the time of connection */

        tmxr_report_connection (mp, lp, line);          /* report the connection to the connected device */
        return line;                                    /*   and return the line number */
        }
    }

return tmxr_poll_conn (mp);                             /* there are no serial connections, so check for Telnet */
}


/* Determine if a line is free.

   If the line described by the line descriptor pointer "lp" is not connected to
   either a Telnet session or a serial port, this routine returns TRUE.
   Otherwise, it returns FALSE.  A TRUE return, therefore, indicates that the
   line is not in use.
*/

t_bool ex_tmxr_line_free (TMLN *lp)
{
if (lp == NULL || lp->conn != 0)                        /* if the line is invalid or is connected */
    return FALSE;                                       /*   then mark the line as busy */
else                                                    /* otherwise */
    return serial_line (lp) == NULL;                    /*   the line is free if it's not a serial line */
}


/* Determine if a multiplexer is free.

   If the multiplexer described by the mux descriptor pointer "mp" is not
   listening for new Telnet connections and has no lines that are connected to
   serial ports, then this routine returns TRUE.  Otherwise, it returns FALSE.
   A TRUE return, therefore, indicates that the multiplexer is not in use.


   Implementation notes:

    1.  If the listening network socket is detached, then no Telnet sessions can
        exist, so we only need to check for serial connections on the lines.
*/

t_bool ex_tmxr_mux_free (TMXR *mp)
{
int32 line;
TMLN  *lp;

if (mp == NULL || mp->master != 0)                      /* if the descriptor is invalid or the socket is open */
    return FALSE;                                       /*   then the multiplexer is not free */

lp = mp->ldsc;                                          /* point at the first line descriptor */

for (line = 0; line < mp->lines; line++, lp++)          /* check each line for a serial connection */
    if (ex_tmxr_line_free (lp) == FALSE)                /* if a serial port is open */
        return FALSE;                                   /*   then the multiplexer is not free */

return TRUE;                                            /* the mux is free, as there are no connections */
}


/* Hooked terminal multiplexer replacement extension routines */


/* Read from a multiplexer line.

   This hook routine reads up to "length" characters into the character buffer
   associated with line descriptor pointer "lp".  The actual number of
   characters read is returned.  If no characters are available, 0 is returned.
   If an error occurred while reading, -1 is returned.

   If the line is connected to a serial port, a serial read is issued.
   Otherwise, the read routine in the TMXR library is called to read from a
   network port.

   If a line break was detected on serial input, the associated receive break
   status flag in the line descriptor will be set.  Line break indication for
   Telnet connections is embedded in the Telnet protocol and must be determined
   externally.


   Implementation notes:

    1. It is up to the caller to ensure that the line is connected and a read of
       the specified length fits in the buffer with the current buffer index.
*/

static int32 ex_tmxr_read (TMLN *lp, int32 length)
{
EX_TMLN *exlp;

if (lp == NULL)                                         /* if the descriptor pointer is not set */
    return -1;                                          /*   then return failure */
else                                                    /* otherwise */
    exlp = serial_line (lp);                            /*   get the serial line extension */

if (exlp == NULL)                                       /* if the line is not a serial line */
    return tmxr_base_read (lp, length);                 /*   then call the standard library routine */

else                                                    /* otherwise */
    return sim_read_serial (exlp->serport,              /*   call the serial read routine */
                            lp->rxb + lp->rxbpi,        /*     with the buffer pointer */
                            length,                     /*       and maximum read length */
                            lp->rbr + lp->rxbpi);       /*         and the break array pointer */
}


/* Write to a multiplexer line.

   This hook routine writes up to "length" characters from the character buffer
   associated with line descriptor pointer "lp".  The actual number of
   characters written is returned.  If an error occurred while writing, -1 is
   returned.

   If the line is connected to a serial port, a serial write is issued.
   Otherwise, the write routine in the TMXR library is called to write to a
   network port.


   Implementation notes:

    1. It is up to the caller to ensure that the line is connected and a write
       of the specified length is contained in the buffer with the current
       buffer index.
*/

static int32 ex_tmxr_write (TMLN *lp, int32 length)
{
EX_TMLN *exlp;

if (lp == NULL)                                         /* if the descriptor pointer is not set */
    return -1;                                          /*   then return failure */
else                                                    /* otherwise */
    exlp = serial_line (lp);                            /*   get the serial line extension */

if (exlp == NULL)                                       /* if the line is not a serial line */
    return tmxr_base_write (lp, length);                /*   then call the standard library routine */

else                                                    /* otherwise */
    return sim_write_serial (exlp->serport,             /*   call the serial write routine */
                             lp->txb + lp->txbpr,       /*     with the buffer pointer */
                             length);                   /*       and write length */
}


/* Show a multiplexer line connection.

   This hook routine is called from the "tmxr_fconns" to display the line
   connection status, typically in response to a SHOW <mux> CONNECTIONS command.
   Depending on the line connection type, the Telnet IP address or serial port
   name is displayed.
*/

static void ex_tmxr_show (TMLN *lp, FILE *stream)
{
EX_TMLN *exlp;

if (lp == NULL)                                         /* if the descriptor pointer is not set */
    return;                                             /*   then a programming error has occurred */
else                                                    /* otherwise */
    exlp = serial_line (lp);                            /*   get the serial line extension */

if (exlp == NULL)                                       /* if the line is not a serial line */
    tmxr_base_show (lp, stream);                        /*   then call the standard library routine */
else                                                    /* otherwise */
    fprintf (stream, "Serial port %s", exlp->sername);  /*   print the serial port name */

return;
}


/* Close a multiplexer line connection.

   This hook routine disconnects the Telnet or serial session associated with
   line descriptor "lp".  If the line is connected to a Telnet port, the close
   routine in the TMXR library is called to close and deallocate the port.
   Otherwise, if the line is connected to an uncontrolled serial port, DTR and
   RTS are dropped to disconnect the attached serial device; the port remains
   connected to the line, which is scheduled for reconnection after a short
   delay for DTR recognition.  If the line is controlled, this routine takes no
   action; it is up to the VM to decide how to proceed.


   Implementation notes:

    1. The base close routine does not return a value, so we cannot report an
       error when a null line descriptor pointer was passed.
*/

static void ex_tmxr_close (TMLN *lp)
{
EX_TMLN *exlp;

if (lp == NULL)                                         /* if the descriptor pointer is not set */
    return;                                             /*   then a programming error has occurred */
else                                                    /* otherwise */
    exlp = serial_line (lp);                            /*   get the serial line extension */

if (exlp == NULL)                                       /* if the line is not a serial line */
    tmxr_base_close (lp);                               /*   then call the standard library routine */

else if (exlp->controlled == FALSE) {                   /* otherwise if the line is uncontrolled */
    sim_control_serial (exlp->serport, No_Signals);     /*   then disconnect the line by dropping DTR */
    lp->cnms = sim_os_msec () + 500;                    /*     and schedule reconnection 500 msec from now */
    }

return;
}


/* Determine if a line is extended.

   This hook routine returns TRUE if the line described by the line descriptor
   pointer "lp" is controlled by this extension module and FALSE if it is not.
   A line is extended only if it is connected to a serial port; the presence of
   a non-null TMLN extension structure pointer is not sufficient, as that
   pointer may be set if a line control call is made for a Telnet port.

   Returning FALSE indicates to the caller that the line should receive the
   standard operations.  Returning TRUE indicates that this extension module
   will operate the line.
*/

static t_bool ex_tmxr_extended (TMLN *lp)
{
return serial_line (lp) != NULL;                        /* return TRUE if it's a serial line */
}



/* Local terminal multiplexer extension routines */


/* Attach a line to a serial port.

   Attach a line of the multiplexer associated with mux descriptor pointer "mp"
   and unit pointer "uptr" to the serial port name indicated by "cptr".  The
   unit is implicitly associated with the line number corresponding to the
   position of the unit in the zero-based array of units belonging to the
   associated device.  For example, if "uptr" points to unit 3 in a given
   device, and "cptr" points to the string "COM1", then line 3 will be attached
   to serial port "COM1".

   An optional configuration string may be present after the port name.  If
   present, it must be separated from the port name with a semicolon and has
   this form:

      <rate>-<charsize><parity><stopbits>

   where:

     rate     = communication rate in bits per second
     charsize = character size in bits (5-8, including optional parity)
     parity   = parity designator (N/E/O/M/S for no/even/odd/mark/space parity)
     stopbits = number of stop bits (1, 1.5, or 2)

   As an example:

     9600-8n1

   The supported rates, sizes, and parity options are host-specific.  If a
   configuration string is not supplied, then host system defaults for the
   specified port are used.

   If the serial port allocation is successful, then the port name is stored in
   the UNIT structure, the UNIT_ATT flag is set, and the routine returns
   SCPE_OK.  If it fails, the error code is returned.

   Implementation notes:

    1. If the device associated with the unit referenced by "uptr" does not have
       the DEV_NET flag set, then the optional configuration string is saved
       with the port name in the UNIT structure.  This allows a RESTORE to
       reconfigure the attached serial port during reattachment.  The combined
       string will be displayed when the unit is SHOWed.

       If the unit has the DEV_NET flag, the optional configuration string is
       removed before the attached port name is saved in the UNIT structure, as
       RESTORE will not reattach the port, and so reconfiguration is not needed.

    2. The "exptr" field of the line descriptor will be set on entry if a call
       to the "ex_tmxr_control_line" routine has preceded this call.  If the
       structure has not been allocated, it is allocated here and is set to the
       uncontrolled state; a subsequent call to "ex_tmxr_control_line" will
       establish VM control if desired.

    3. Attempting to attach line that does not appear in the connection order
       array will be rejected.  This ensures that an omitted line will receive
       neither a Telnet connection nor a serial connection.
*/

static t_stat ex_tmxr_attach_line (TMXR *mp, UNIT *uptr, char *cptr)
{
TMLN      *lp;
EX_TMLN   *exlp;
DEVICE    *dptr;
char      *pptr, *sptr, *tptr;
SERHANDLE serport;
t_stat    status;
int32     cntr, line;
char      portname [1024];
t_bool    arg_error = FALSE;
SERCONFIG config = { 0 };

if (uptr == NULL)                                       /* if this is a console reference */
    lp = mp->ldsc;                                      /*   point at the (only) line */
else                                                    /* otherwise */
    lp = tmxr_find_ldsc (uptr, mp->lines, mp);          /*   determine the line from the unit */

if (lp == NULL)                                         /* if the unit does not correspond to a line */
    return SCPE_NXUN;                                   /*   then report that the unit does not exist */

else if (lp->conn)                                      /* otherwise if the line is connected via Telnet */
    return SCPE_NOFNC;                                  /*   then the command is not allowed */

else if (cptr == NULL)                                  /* otherwise if the port name is missing */
    return SCPE_2FARG;                                  /*   then report a missing argument */

else {                                                  /* otherwise get the multiplexer line number */
    line = (int32) (lp - mp->ldsc);                     /*   implied by the line descriptor */

    if (mp->lnorder != NULL && mp->lnorder [0] >= 0) {  /* if the line order exists and is not defaulted */
        for (cntr = 0; cntr < mp->lines; cntr++)        /*   then see if the line to attach */
            if (line == mp->lnorder [cntr])             /*     is present in the */
                break;                                  /*       connection order array */

        if (cntr == mp->lines)                          /* if the line was not found */
            return SCPE_NOATT;                          /*   then report that the line is not attachable */
        }
    }

pptr = get_glyph_nc (cptr, portname, ';');              /* separate the port name from the optional configuration */

if (*pptr != '\0') {                                        /* if a parameter string is present */
    config.baudrate = (uint32) strtotv (pptr, &sptr, 10);   /*   then parse the baud rate */
    arg_error = (pptr == sptr);                             /*     and check for a bad argument */

    if (*sptr != '\0')                                      /* if a separator is present */
        sptr++;                                             /*   then skip it */

    config.charsize = (uint32) strtotv (sptr, &tptr, 10);   /* parse the character size */
    arg_error = arg_error || (sptr == tptr);                /*   and check for a bad argument */

    if (*tptr != '\0')                                      /* if the parity character is present */
        config.parity = toupper (*tptr++);                  /*   then save it */

    config.stopbits = (uint32) strtotv (tptr, &sptr, 10);   /* parse the number of stop bits */
    arg_error = arg_error || (tptr == sptr);                /*   and check for a bad argument */

    if (arg_error)                                          /* if any parse failure occurred */
        return SCPE_ARG;                                    /*   then report an invalid argument error */

    else if (strcmp (sptr, ".5") == 0)                      /* otherwise if 1.5 stop bits are requested */
        config.stopbits = 0;                                /*   then recode the request */
    }

serport = sim_open_serial (portname);                   /* open the named serial port */

if (serport == INVALID_HANDLE)                          /* if the port name is invalid or in use */
    return SCPE_OPENERR;                                /*   then report the attach failure */

else {                                                  /* otherwise we have a good serial port */
    if (*pptr) {                                        /* if the parameter string was specified */
        status = sim_config_serial (serport, config);   /*   then set the serial configuration */

        if (status != SCPE_OK) {                        /* if configuration failed */
            sim_close_serial (serport);                 /*   then close the port */
            return status;                              /*     and report the error */
            }
        }

    dptr = find_dev_from_unit (uptr);                   /* find the device that owns the unit */

    if (dptr != NULL && dptr->flags & DEV_NET)          /* if RESTORE will be inhibited */
        cptr = portname;                                /*   then save just the port name */

    if (mp->dptr == NULL)                               /* if the device has not been set in the descriptor */
        mp->dptr = dptr;                                /*   then set it now */

    tptr = (char *) malloc (strlen (cptr) + 1);         /* get a buffer for the port name and configuration */

    if (tptr == NULL) {                                 /* if the memory allocation failed */
        sim_close_serial (serport);                     /*   then close the port */
        return SCPE_MEM;                                /*     and report the failure */
        }

    else                                                /* otherwise */
        strcpy (tptr, cptr);                            /*   copy the port name into the buffer */

    exlp = (EX_TMLN *) lp->exptr;                       /* point to the descriptor extension */

    if (exlp == NULL) {                                 /* if the extension has not been allocated */
        lp->exptr = malloc (sizeof (EX_TMLN));          /*   then allocate it now */

        if (lp->exptr == NULL) {                        /* if the memory allocation failed */
            free (tptr);                                /*   then free the port name buffer */
            sim_close_serial (serport);                 /*     and close the port */

            return SCPE_MEM;                            /* report the failure */
            }

        else {                                          /* otherwise */
            exlp = (EX_TMLN *) lp->exptr;               /*   point to the new descriptor extension */

            exlp->controlled = FALSE;                   /* mark the line as uncontrolled */
            exlp->signals = No_Signals;                 /*   and set the unconnected control signals */
            }
        }

    exlp->serport = serport;                            /* save the serial port handle */
    exlp->sername = tptr;                               /*   and the port name */

    if (uptr != NULL) {                                 /* if this is not a console attach */
        uptr->filename = tptr;                          /*   then save the port name pointer in the UNIT */
        uptr->flags |= UNIT_ATT;                        /*     and mark the unit as attached */
        }

    tmxr_init_line (lp);                                /* initialize the line state */

    lp->cnms = 0;                                       /* schedule for an immediate connection */
    lp->conn = 0;                                       /*   and indicate that there is no connection yet */
    }

return SCPE_OK;                                         /* the line has been connected */
}


/* Get the extension pointer for a serial line.

   This routine returns a pointer to the TMLN extension structure if it exists
   and is currently in use for a serial line.  Otherwise, it returns NULL.  A
   non-null return therefore indicates that the line is connected to a serial
   port and a serial operation should be performed instead of a Telnet
   operation.
*/

static EX_TMLN *serial_line (TMLN *lp)
{
EX_TMLN *exlp;

if (lp == NULL)                                         /* if the line pointer is invalid */
    return NULL;                                        /*   then the line cannot be a serial line */
else                                                    /* otherwise */
    exlp = (EX_TMLN *) lp->exptr;                       /*   point at the corresponding line extension */

if (exlp != NULL && exlp->serport != INVALID_HANDLE)    /* if it's allocated to a serial port */
    return exlp;                                        /*   then return the extension pointer */
else                                                    /* otherwise */
    return NULL;                                        /*   it's not a serial line */
}



/* *********************  String Breakpoint Extensions  ************************

   This module extends the following existing routines in "sim_console.c" and
   "scp.c":

     sim_putchar   -- write a character to the console window
     sim_putchar_s -- write a character to the console window and stall if busy

     sim_brk_test  -- test for a breakpoint at the current program location

   The console output routines are extended to match output characters to
   pending string breakpoints, and the breakpoint test routine is extended to
   check for triggered string breakpoints.

   If a string breakpoint for the console is set, each output character is
   matched to the breakpoint string.  This matching takes place only if the
   console input has not been redirected to the command buffer, i.e., if it is
   in "Console" mode and not "Command" mode.  If the output characters form a
   matching string, the breakpoint is triggered, which will cause the next call
   to "sim_brk_test" to return the string breakpoint type.  The VM typically
   makes such a call once per instruction.

   If a breakpoint has a delay specified, triggering is not enabled until the
   delay time has expired.  The state of a breakpoint -- not triggered
   (pending), trigger delayed, or triggered -- is indicated by the "trigger"
   field of the breakpoint structure.  If the value is negative, the breakpoint
   is not triggered.  Otherwise, the value indicates the simulator global time
   at which the breakpoint transitions from trigger delayed to triggered.
   Comparison to the global time therefore indicates the trigger state.
*/


/* Global string breakpoint extension routines */


/* Put a character to the console.

   This shim for "sim_putchar" outputs the character designated by "c" to the
   console window.  If the keyboard is in Console mode, and a string breakpoint
   is set, the character is matched to the current breakpoint.  The matching is
   not done in Command mode to prevent a breakpoint from triggering due to
   command echoing or output.


   Implementation notes:

    1. If the output character cannot be written due to a stall, it is ignored.
       Output is normally stalled when the keyboard is in Command mode, so any
       characters output via calls to this routine while in Command mode will be
       lost.
*/

t_stat ex_sim_putchar (int32 c)
{
if (keyboard_mode == Console) {                         /* if we are in console mode */
    if (sb_list != NULL)                                /*   then if string breakpoints exist */
        test_breakpoint (c);                            /*     then test for a match */

    return sim_putchar (c);                             /* output the character */
    }

else if (sim_con_tmxr.master != 0)                      /* otherwise if the consoles are separate */
    return sim_putchar (c);                             /*   then output the character */

else                                                    /* otherwise we're in unified command mode */
    return SCPE_OK;                                     /*   so discard the output */
}


/* Put a character to the console with stall detection.

   This shim for "sim_putchar_s" outputs the character designated by "c" to the
   console window.  If the keyboard is in Console mode, and a string breakpoint
   is set, the character is matched to the current breakpoint.  The matching is
   not done in Command mode to prevent a breakpoint from triggering due to
   command echoing or output.


   Implementation notes:

    1. If the output character cannot be written due to a stall, SCPE_STALL is
       returned.  The calling routine should detect this condition and
       reschedule the output.  Output is normally stalled when the keyboard is
       in Command mode, so any characters output via calls to this routine while
       in Command mode should be rescheduled for output when the keyboard
       returns to Console mode.
*/

t_stat ex_sim_putchar_s (int32 c)
{
if (keyboard_mode == Console) {                         /* if we are in console mode */
    if (sb_list != NULL)                                /*   then if string breakpoints exist */
        test_breakpoint (c);                            /*     then test for a match */

    return sim_putchar_s (c);                           /* output the character */
    }

else if (sim_con_tmxr.master != 0)                      /* otherwise if the consoles are separate */
    return sim_putchar_s (c);                           /*   then output the character */

else                                                    /* otherwise we're in unified command mode */
    return SCPE_STALL;                                  /*   so stall the output */
}


/* Test for a breakpoint at the current location.

   This shim for "sim_brk_test" checks for a triggered string breakpoint or a
   numeric breakpoint of type "type" at the address designated by "location".
   If a breakpoint is detected, the type of the breakpoint is returned.

   The "type" parameter is the union of all numeric breakpoint types that are
   valid for this address location.  To be triggered, a numeric breakpoint must
   match both the location and one of the specified types.

   String breakpoints are always tested, as only one type of string breakpoint
   is defined -- the default string breakpoint type.  Therefore, it is not
   necessary for the VM to add the string breakpoint type to the "type"
   parameter when calling this routine.


   Implementation notes:

    1. For numeric breakpoint types, a type's presence in "sim_brk_summ"
       indicates that one or more breakpoints of that type are pending and must
       be checked for triggering during this routine.  For string breakpoints,
       presence of the BP_STRING type indicates that a string breakpoint has
       already triggered, and no further check is required.

    2. String breakpoints are triggered by the "breakpoint_service" routine when
       the breakpoint delay expires.  There would be no need to handle
       triggering here if there were a global breakpoint status value, as the
       service routine could simply return that hypothetical SCPE_BREAK code to
       stop instruction execution.  Unfortunately, "breakpoint triggered" is a
       VM-specific status code (e.g., STOP_BRKPNT), so we must use this routine
       to return a breakpoint indication to the VM, which then stops execution
       and returns its own VM-specific status.
*/

uint32 ex_sim_brk_test (t_addr location, uint32 type)
{
static char tempbuf [CBUFSIZE];
uint32 result;
BRKTAB *bp;

if (sim_brk_summ & BP_STRING) {                         /* if a string breakpoint has triggered */
    sim_brk_summ &= ~BP_STRING;                         /*   then clear the code */
    result = BP_STRING;                                 /*     and return the match type */
    }

else {                                                  /* otherwise */
    result = sim_brk_test (location, type);             /*   test for a numeric breakpoint */

    if (result != 0) {                                  /* if the breakpoint fired */
        bp = sim_brk_fnd (location);                    /*   then find it */

        if (bp != NULL && bp->typ & SWMASK ('T')) {         /* if the breakpoint is temporary */
            if (bp->act != NULL)                            /*   then if actions are defined for it */
                sim_brk_act = strcpy (tempbuf, bp->act);    /*     then copy the action string */

            sim_brk_clr (location, bp->typ);                /* clear the breakpoint */
            }
        }
    }

return result;                                          /* return the test result */
}


/* Local string breakpoint SCP support routines */


/* Return the name of the breakpoint delay unit.

   The unit that implements string breakpoint delays does not have a
   corresponding device, so the SHOW QUEUE command will fail to find the unit in
   the list of devices.  To provide the name, we set the "sim_vm_unit_name" hook
   in the one-time initialization to point at this routine, which supplies the
   name when passed the break delay unit as the parameter.  If the unit is not
   the break delay unit, then we let the VM-defined name routine handle it, if
   one was defined.  Otherwise, we return NULL to indicate that we did not
   recognize it.
*/

static char *breakpoint_name (const UNIT *uptr)
{
if (uptr == breakpoint_unit)                            /* if the request is for the break delay unit */
    return "Break delay timer";                         /*   then return the descriptive name */
else if (vm_unit_name_handler)                          /* otherwise if the VM defined a name handler */
    return vm_unit_name_handler (uptr);                 /*   then call it to process the request */
else                                                    /* otherwise */
    return NULL;                                        /*   report that we do not recognize the unit */
}


/* Service a breakpoint.

   A matched breakpoint remains in the trigger-delayed state until any specified
   delay elapses.  When a pending breakpoint is satisfied, service is scheduled
   on the breakpoint unit to wait until the delay expires.  This service routine
   then triggers the breakpoint and handles removal of the allocated structure
   if it is temporary or resetting the breakpoint if it is permanent.

   On entry, one of the breakpoints in the linked list will be ready to trigger.
   We scan the list looking for the first breakpoint whose trigger time has
   passed.  The scan is done only if no prior trigger is waiting to be
   acknowledged.  This condition could occur if the VM has not called
   "sim_brk_test" since the earlier breakpoint triggered.  In that case, we
   postpone our check, so that we do not have two triggered breakpoints at the
   same time (as only one of the two sets of breakpoint actions can be
   performed).

   When a breakpoint triggers, the BP_STRING type is set in the "sim_brk_summ"
   variable to tell the "sim_brk_test" routine to pass a breakpoint indication
   back to the VM for action.  Then we clean up the breakpoint structure,
   copying the action string into a local static buffer if the breakpoint was
   temporary; it will be executed from there.

   When we are called, there may be additional breakpoints in the trigger-
   delayed state, i.e., waiting for their respective delays to expire before
   triggering.  While we are processing the breakpoint list, we also look for
   the earliest breakpoint in that state and schedule our service to reactivate
   when that delay expires.  If no additional breakpoints are delayed, the
   service stops.


   Implementation notes:

    1. If a second breakpoint is waiting, the delay until that breakpoint
       triggers could be zero -- if, for example, a breakpoint with a delay of
       2000 has already waited for a period of 1000 when a second breakpoint
       with a delay of 1000 is satisfied.  After servicing the first breakpoint,
       we cannot reactivate the service with a zero time, because we would be
       reentered before the VM had a chance to call "sim_brk_test" and so the
       first would still be pending, which would hold off recognizing the
       second.  This would result in another zero service time, and the VM would
       never get an opportunity to recognize any breakpoints.  So we arbitrarily
       reschedule with a delay of one, which allows the VM to recognize the
       first triggered breakpoint.

    2. It is safe to use a single static breakpoint action buffer for temporary
       breakpoints, because a triggered breakpoint causes the VM to exit the
       instruction loop, and reentry (a necessary condition for a second
       breakpoint to trigger) clears any pending actions.
*/

static t_stat breakpoint_service (UNIT *uptr)
{
static char tempbuf [CBUFSIZE];
SBPTR  bp, prev;
int32  delay;
const  double entry_time = sim_gtime ();                /* the global simulation time at entry */
double next_time = DBL_MAX;                             /* the time at which the next breakpoint triggers */

bp   = sb_list;                                         /* start searching at the head of the breakpoint list */
prev = NULL;                                            /*   for a triggered breakpoint */

while (bp != NULL) {                                    /* loop until the list is exhausted */
    if (bp->trigger >= 0.0)                             /*   looking for a triggered breakpoint */
        if ((sim_brk_summ & BP_STRING) == 0             /* if no outstanding service request exists */
          && entry_time >= bp->trigger) {               /*   and this breakpoint is now triggered */
            sim_brk_summ |= BP_STRING;                  /*     then indicate that a breakpoint stop is required */

            if (bp->type & SWMASK ('T')) {                  /* if a temporary breakpoint just triggered */
                sim_brk_act = strcpy (tempbuf, bp->action); /*   then copy the action string */

                if (prev != NULL) {                     /* if there is a previous node */
                    prev->next = bp->next;              /*   then link it to the next one, */
                    free (bp);                          /*     free the current node */
                    bp = prev->next;                    /*       and make the next node current */
                    }

                else {                                  /* otherwise we're clearing the first node */
                    sb_list = bp->next;                 /*   so point the list header at the next one, */
                    free (bp);                          /*     free the current node */
                    bp = sb_list;                       /*       and make the next node current */
                    }

                continue;                               /* continue the search with the next node */
                }

            else {                                      /* otherwise it's a persistent breakpoint */
                sim_brk_act = bp->action;               /*   so copy the action string pointer */
                bp->mptr = bp->match;                   /*     and reset the match pointer */
                bp->trigger = -1.0;                     /*       and the trigger */
                }
            }

        else if (bp->trigger < next_time)               /* otherwise obtain the earliest trigger time */
            next_time = bp->trigger;                    /*   of all of the trigger-delayed breakpoints */

    prev = bp;                                          /* the current node becomes the prior one */
    bp = bp->next;                                      /*   and the next node becomes current */
    }

if (next_time < DBL_MAX) {                              /* if another triggered breakpoint was seen */
    delay = (int32) (next_time - entry_time);           /*   then get the relative delay */

    if (delay < 1 && sim_brk_summ & BP_STRING)          /* if a breakpoint was triggered in this pass */
        delay = 1;                                      /*   ensure that the VM has time to process it */

    sim_activate (breakpoint_unit, delay);              /* reschedule the service to handle the next breakpoint */
    }

return SCPE_OK;                                         /* return with success */
}


/* Local string breakpoint extension routines */


/* Cancel all string breakpoints.

   This routine cancels all of the string breakpoints.  It is called in response
   to the NOBREAK "" and NOBREAK ALL commands.  It walks the linked list headed
   by "sb_list" and releases each string breakpoint structure encountered.
   Before returning, it clears the list head pointer and cancels the breakpoint
   delay time, in case a breakpoint had entered the trigger-delayed state.
*/

static void free_breakpoints (void)
{
SBPTR bp, node;

bp = sb_list;                                           /* start at the list head */

while (bp != NULL) {                                    /* if the current node exists */
    node = bp;                                          /*   then save a node pointer */
    bp = bp->next;                                      /* point at the next node */
    free (node);                                        /*   and free the current one */
    }

sb_list = NULL;                                         /* clear the breakpoint list header */
sim_cancel (breakpoint_unit);                           /*   and cancel the breakpoint timer */

return;
}


/* Find a string breakpoint.

   This routine looks through the list of string breakpoints for one matching
   the supplied string.  If a matching breakpoint is found, a pointer to the
   structure is returned.  Otherwise, NULL is returned.  In either case, a
   pointer to the prior (or last, if no match) structure is returned via the
   second function parameter.

   A case-sensitive match is performed.
*/

static SBPTR find_breakpoint (char *match, SBPTR *prev)
{
SBPTR bp = sb_list;                                     /* start at the list head */

*prev = NULL;                                           /* initialize the previous node pointer */

while (bp != NULL)                                      /* if the current node exists */
    if (strcmp (match, bp->match) == 0)                 /*   and it matches the search string */
        break;                                          /*     then the search is over */

    else {                                              /* otherwise */
        *prev = bp;                                     /*   save the node pointer */
        bp = bp->next;                                  /*     and point at the next one */
        }

return bp;                                              /* return the matching node pointer or NULL */
}


/* Test for a string breakpoint.

   This routine is called when string breakpoints exist and a character is to be
   output.  It checks for a match between that character (the test character)
   and the next character in each match string in the linked list of breakpoint
   structures.  If a match string is completed, the breakpoint enters the
   trigger-delayed state, and the break delay timer is scheduled if it is not
   running.  Matching a particular breakpoint does not inhibit matching against
   all of the other breakpoints.

   Within each STRING_BREAKPOINT structure in the linked list of breakpoints,
   the "match" field contains the string to match, and the "mptr" field points
   at the next character to check (initially, the first character of the match
   string).

   If the test character equals the match character, then the match pointer is
   advanced.  If the pointer now points at the end of the match string, then the
   breakpoint enters the trigger delayed state.  The "trigger" field is set to
   the trigger activation time.  This is normally the current time but will be a
   time in the future if a breakpoint delay was specified.  If no other
   breakpoint is in this state, the break delay timer will not be active, and so
   the routine will activate it with the specified delay.  If the timer is
   already active, the remaining delay time is compared to the delay time for
   the newly matched breakpoint.  If the remaining time is greater, the timer is
   reset to the shorter delay, as the new breakpoint will trigger before the
   existing one.  Otherwise, the timer continues with the remaining time.

   If the test character does not equal the current match character, then a
   check is made to see if it matches any prior characters in the match string.
   If it does not, then the match pointer is reset to the start of the string.
   However, if it does, then it's possible that the characters output so
   far match a prior substring of the match string.

   Consider a match string of "ABABC" that has been matched through the second
   "B", so that the match pointer points at the "C".  We are called with an "A"
   as the test character.  It does not match "C", but in looking backward, we
   see that it does match the second "A".  So we search backward from there to
   see if the output characters match the earlier part of the match string.  In
   this case, the last three characters output match the leading substring "ABA"
   of the match string, so the match pointer is reset to point at the fourth
   character, rather than the second.  Then, if "BC" is subsequently output, the
   match will succeed.

   Conceptually, this search begins with:

                   match pointer
                         |
             A  B  A  B  C      match characters
             A  B  A  B  A      output characters
                         |
                  test character

   Because the characters do not match, we "slide" the output string to the left
   to see if we can find a trailing output substring that matches a leading
   match substring.  We start with the right-most match character that equals
   the test character

         first matching character
                   |
             A  B  A  B  C      match characters
       A  B  A  B  A            output characters
             |  |  |
              match

   Here, the last three output string characters match the first three match
   string characters, so we reset the match pointer to the fourth character:

                match pointer
                      |
             A  B  A  B  C      match characters

   Now if an additional "B" and "C" are output (i.e., the entire output is
   "ABABABC"), then the breakpoint will trigger on receipt of the "C".

             A  B  A  B  C      match characters
       A  B  A  B  A  B  C      output characters
             |  |  |  |  |
                 match

   Now consider a match string of "ABAB" that has matched through the second
   "A", and we are called with a test character of "A":

                    match pointer
                          |
                 A  B  A  B     match characters
                 A  B  A  A     output characters
                          |
                   test character

   The first substring test does not match:

             first matching character
                       |
                 A  B  A  B     match characters
              A  B  A  A        output characters
                 |  |
               no match

   So we search backward for another test character match and try again, and
   this one succeeds:

     second matching character
                 |
                 A  B  A  B     match characters
        A  B  A  A              output characters
                 |
               match

   The match pointer is reset to point at the following "B", and the breakpoint
   will trigger if the subsequent output produces "ABAABAB".

   Effectively, the search starts with the longest possible substring and then
   attempts shorter and shorter substrings until either a match occurs or no
   substring matches.  In the first case, the match pointer is reset
   appropriately, and partial matching continues.  In the second, the match
   pointer is reset to the beginning of the match string, and a new match is
   sought.

   Searching for the longest substring that matches the output stream would
   appear to require an output history buffer.  However, the fact that all of
   the prior output characters until the current one have matched means that the
   match string itself IS the history of the relevant part of the output stream.
   We need only search for substrings that equal the substring of the match
   string that ends with the last-matched character.

   This matching process is repeated for each node in the list of breakpoints.
*/

static void test_breakpoint (int32 test_char)
{
char  *history, *hptr, *sptr;
int32 trigger_time;
SBPTR bp = sb_list;                                     /* start at the list head */

while (bp != NULL) {                                    /* if the current node exists */
    if (*bp->mptr != '\0')                              /*   then if the search string is not exhausted */
        if (*bp->mptr == test_char) {                   /*     then if the search character matches */
            bp->mptr++;                                 /*       then point at the next search character */

            if (*bp->mptr == '\0') {                    /* if the search string is completely matched */
                bp->trigger =                           /*   then set the trigger time */
                  sim_gtime () + bp->delay;             /*     to the current time plus any delay */

                trigger_time = sim_is_active (breakpoint_unit); /* get any remaining delay time */

                if (trigger_time == 0 || trigger_time > bp->delay)  /* if it's not running or the delay is too long */
                    sim_activate_abs (breakpoint_unit, bp->delay);  /*   then reschedule the timer to the shorter time */
                }
            }

        else if (bp->mptr != bp->match) {               /* otherwise if we have a partial match */
            history = --bp->mptr;                       /*   then save a pointer to the output history */

            do {                                        /* search for a substring match */
                while (bp->mptr >= bp->match            /* while still within the match string */
                  && *bp->mptr != test_char)            /*   and the search character doesn't match */
                    bp->mptr--;                         /*     back up until a matching character is found */

                if (bp->mptr < bp->match) {             /* if no matching character was found */
                    bp->mptr = bp->match;               /*   then reset the search pointer to the start */
                    sptr = NULL;                        /*     and exit the substring search */
                    }

                else {                                  /* otherwise there is a potential substring match */
                    hptr = history;                     /*   so set up the output history */
                    sptr = bp->mptr - 1;                /*     and matching substring pointers */

                    while (sptr >= bp->match            /* test for a substring match */
                      && *sptr == *hptr) {              /*   in reverse */
                        sptr--;                         /*     until a match fails */
                        hptr--;                         /*       or the entire substring matches */
                        }

                    if (sptr < bp->match) {             /* if a matching substring was found */
                        bp->mptr++;                     /*   then point at the next character to match */
                        sptr = NULL;                    /*     and exit the substring search */
                        }

                    else                                /* otherwise the substring did not match */
                        bp->mptr = sptr;                /*   so try the next shorter substring */
                    }
                }
            while (sptr);                               /* continue testing until a match or exhaustion */
            }

    bp = bp->next;                                      /* point at the next breakpoint node */
    }                                                   /*   and continue until all nodes are checked */

return;
}



/* *************  Concurrent Console Mode and Reply Extensions ****************

   This module extends the following existing routines in "hp----_cpu.c" and
   "sim_console.c":

     sim_instr    -- execute simulated machine instructions

     sim_poll_kbd -- poll the console keyboard for input

   The instruction execution routine is extended to process commands entered in
   concurrent console mode.  The keyboard poll routine is extended to allow
   entry of commands concurrently with instruction execution and also to supply
   previously established character string replies automatically.

   In the normal console mode, entry of SCP commands first requires instruction
   execution to be stopped by entering the WRU character (default is CTRL+E).
   This prints "Simulation stopped" on the console, terminates instruction
   execution, and returns to the "sim>" prompt.  At this point, a command such
   as a tape image attachment may be entered, and then execution may be resumed.

   The problem with this is that while instruction execution is stopped, the
   simulated time-of-day clock is also stopped.  It that clock had been set
   accurately at target OS startup, it will lose time each time an SCP command
   must be entered.

   To alleviate this, a "concurrent" console mode may be established.  In this
   mode, entering CTRL+E does not terminate instruction execution but rather
   diverts console keystrokes into a separate command buffer instead of
   returning them to the VM.  During command entry, instructions continue to
   execute, so the simulated time-of-day clock remains accurate.  When the
   command is terminated with ENTER, the VM's "sim_instr" routine returns to the
   extension shim.  The shim executes the command and then automatically resumes
   instruction execution.  The simulated clock is stopped for the command
   execution time, but that time is usually shorter than one clock tick and so
   is absorbed by the clock calibration routine.

   The extended keyboard poll routine switches between "Console" mode, where the
   characters are delivered to the VM, and "Command" mode, where the characters
   are delivered to a command buffer.  Entry into command mode is made by
   sensing CTRL+E, and exit from command mode is made by sensing ENTER.  Limited
   editing is provided in Command mode because the poll routine obtains
   characters, not keystrokes, and so can't sense the non-character keys such as
   the arrow keys.

   Concurrent console mode is an option, set by the SET CONSOLE [NO]CONCURRENT
   command.
*/


/* Global concurrent console and reply extension routines */


/* Execute CPU instructions.

   This shim for the virtual machine's "sim_instr" routine detects commands
   entered in concurrent console mode, executes them, and then calls "sim_instr"
   again.  This loop continues until a simulation stop condition occurs.


   Implementation notes:

    1. On Unix systems, WRU is registered as the SIGINT character, so CTRL+E
       does not arrive via the keyboard poll.  Instead, the SIGINT handler
       installed by the "run_cmd" routine is called, which sets the "stop_cpu"
       flag.  This value is tested in "sim_process_event", which returns
       SCPE_STOP in response.  This causes the VM's "sim_instr" to stop
       execution and print "Simulation stopped" before returning.  We cannot
       test "stop_cpu" in our "ex_sim_poll_kbd" routine to trigger the
       concurrent mode prompt because "sim_process_event" is called after every
       machine instruction, so it will be seen there before we can act on it in
       our keyboard poll.  So instead we must replace the installed SIGINT
       handler with one of our own that sets a local "stop_requested" flag that
       is tested in our keyboard poll.

    2. When the system console is connected to a serial port, we fake a Telnet
       connection by setting "sim_con_tmxr.master" non-zero during instruction
       execution.  This tricks the "sim_poll_kbd" and "sim_putchar[_s]" routines
       in "sim_console.c" into calling the terminal multiplexer routines, which
       will read from or write to the serial console.

    3. Leading spaces must be skipped before calling "get_glyph" to parse the
       command keyword, as that routine uses spaces to mark the end of the
       keyword.  If a leading space is present, "get_glyph" will return a null
       string as the keyword, and "find_cmd" (called via "get_command") will
       return a successful match with the first entry in the command table.

    4. The routine must restore the console to "command mode" to reenable text
       mode on the console log, which is necessary to obtain the correct line
       ends on logged commands.  It must also save and restore the command line
       switches in effect at entry, so that any switches present in the entered
       command line aren't incorrectly used by the VM's "sim_instr" routine.

    5. With one exception, entered commands must be "unrestricted", i.e., must
       not interfere with the partial unwinding of the VM run state.  The
       exception is the DO command.  To allow command files to contain
       prompt/response actions, we handle DO specially by setting the global
       "concurrent_do_ptr" to point at the DO command line and then stop
       execution to unwind the run state.  The DO command is then handled in the
       "ex_run_cmd" routine by executing the command file and then automatically
       reentering this routine to resume instruction execution.

    7. We can't simply return SCPE_EXIT in response to an EXIT command because
       the standard "run_cmd" routine ignores the return status and always
       returns SCPE_OK to the command loop.  To get the loop to exit, we could
       either set a global here and return SCPE_EXIT from "ex_run_cmd" in
       response, or set up EXIT as a "breakpoint" action to be executed upon
       return.  The latter option is implemented.
*/

t_stat sim_instr (void)
{
SIG_HANDLER prior_handler;
char        *cptr, gbuf [CBUFSIZE], tbuf [CBUFSIZE];
t_stat      status, reason;
int32       saved_switches;
CTAB        *cmdp;

prior_handler = signal (SIGINT, wru_handler);           /* install our WRU handler in place of the current one */

if (prior_handler == SIG_ERR)                           /* if installation failed */
    status = SCPE_SIGERR;                               /*   then report an error */

else do {                                               /* otherwise */
    stop_requested = FALSE;                             /*   clear any pending WRU stop */

    if (serial_line (sim_con_tmxr.ldsc) != NULL)        /* if the system console is on a serial port */
        sim_con_tmxr.master = 1;                        /*   then fake a Telnet connection */

    status = vm_sim_instr ();                           /* call the instruction executor */

    if (serial_line (sim_con_tmxr.ldsc) != NULL)        /* if the system console is on a serial port */
        sim_con_tmxr.master = 0;                        /*   then clear the fake Telnet connection */

    if (status == SCPE_EXEC) {                          /* if a concurrent command was entered */
        cptr = cmd_buf;                                 /*   then point at the command buffer */

        ex_substitute_args (cptr, tbuf, sizeof cmd_buf, NULL);  /* substitute variables in the command line */

        while (isspace (*cptr))                         /* remove any leading spaces */
            cptr++;                                     /*   that would confuse the "get_glyph" routine */

        if (*cptr == '\0')                              /* if the command was entirely blank */
            continue;                                   /*   then ignore it */

        sim_ttcmd ();                                   /* return the console to command state */

        if (sim_log)                                    /* if the console is being logged */
            fprintf (sim_log, "\nscp> %s\n", cptr);     /*   then echo the command to the log file */

        if (*cptr == ';') {                             /* if a comment was entered */
            sim_ttrun ();                               /*   then return the console to run mode */
            continue;                                   /*     and ignore the command */
            }

        saved_switches = sim_switches;                  /* save the switches currently in effect */
        sim_switches = 0;                               /*   and reset them to avoid interference */

        cptr = get_glyph (cptr, gbuf, 0);               /* parse the command keyword */

        reason = get_command (gbuf, &cmdp);             /* get the command descriptor */

        if (cmdp != NULL                                /* if the command is valid */
          && cmdp->action == ex_do_handler) {           /*   and is a DO command */
            concurrent_do_ptr = cptr;                   /*     then point at the parameters */
            status = SCPE_OK;                           /*       and stop execution */
            }

        else {                                              /* otherwise */
            if (reason == SCPE_OK)                          /*   if the command is legal */
                reason = cmdp->action (cmdp->arg, cptr);    /*     then execute it */

            if (reason != SCPE_OK)                          /* if an error is indicated */
                if (reason == SCPE_EXIT) {                  /*   the if the command was EXIT (or QUIT or BYE) */
                    sim_brk_act = "exit";                   /*     then set execute an EXIT command on return */
                    status = SCPE_STOP;                     /*       and stop execution */
                    }

                else if (cmdp != NULL                       /* otherwise if the command is known */
                  && cmdp->action == ex_restricted_cmd      /*   and is */
                  && cmdp->arg == EX_ABORT) {               /*     an ABORT command */
                    stop_requested = TRUE;                  /*       then set the flag */
                    status = SCPE_STOP;                     /*         and handle it as a simulation stop */
                    }

                else {                                      /* otherwise report the error */
                    printf ("%s\n", sim_error_text (reason));

                    if (sim_log)                            /* if the console is being logged */
                        fprintf (sim_log, "%s\n",           /*   then report it to the log as well */
                                 sim_error_text (reason));
                    }

            if (sim_vm_post != NULL)                        /* if the VM wants command notification */
                (*sim_vm_post) (TRUE);                      /*   then let it know we executed a command */
            }

        sim_ttrun ();                                   /* return the console to run mode */
        sim_switches = saved_switches;                  /*   and restore the original switches */
        }
    }

while (status == SCPE_EXEC);                            /* continue execution if stopped for a command */

if (status != SCPE_SIGERR)                              /* if the signal handler was set up properly */
    signal (SIGINT, prior_handler);                     /*   then restore the prior handler */

return status;                                          /* return the result of instruction execution */
}


/* Poll the console keyboard.

   This shim for "sim_poll_kbd" polls the console keyboard for keystrokes and
   delivers the resulting characters to the caller.  The routine extends the
   standard one to supply automatic responses for the REPLY command and to
   enable a "concurrent" command mode that allows SCP commands to be entered
   without stopping simulation execution.

   During simulator execution, the system console is connected to the simulation
   console by default.  While it is so connected, keystrokes entered at the
   simulation console are delivered to the system console device.

   With a SET CONSOLE TELNET command, the system console may be redirected to a
   Telnet port.  After this separation, two console windows exist: the
   simulation console remains attached to the originating command window, while
   the system console is attached to the Telnet client window.  When the system
   console window has the input focus, keystrokes are delivered to the system
   console device.  When the simulation console window has the focus, keystrokes
   are delivered to SCP.

   In non-concurrent mode, SCP responds only to CTRL+E and ignores all other
   keystrokes.  If concurrent mode is enabled with a SET CONSOLE CONCURRENT
   command, then the simulation console becomes interactive after a CTRL+E while
   simulator execution continues.  During this time, system console operation
   depends on whether the simulation and system consoles are joined or
   separated.

   In concurrent mode, the simulation console is in one of two states: Console
   or Command.  It starts in Console state.  If the simulation and system
   consoles are joined, then keystrokes are delivered to the system console.  If
   they are separated, then keystrokes are ignored.

   Pressing CTRL+E prints an "scp> " prompt on the simulation console, and the
   console switches to the Command state.  While execution continues, keystrokes
   forming an SCP command are placed in a command buffer; the command is
   executed when ENTER is pressed.  Limited editing is provided in the Command
   state.  Pressing BACKSPACE deletes the last character entered, and pressing
   ESCAPE clears all characters.  Pressing ENTER with no characters present
   terminates the Command state and returns to the Console state.

   In the Command state, if the simulation and system console are joined, then
   the system console device will not receive any keyboard input, and any
   console output call will return with an SCPE_STALL result.  To the user,
   pressing CTRL+E pauses any output in progress to the system console, which
   resumes automatically after the entered command is executed.  If the
   simulation and system console are separate, then the system console continues
   to function normally while the command is being entered at the simulation
   console.

   Pressing CTRL+E while in the Command state causes a simulation stop, just as
   it does in non-concurrent mode.  The console is returned to Console state,
   and any partially entered command is abandoned.

   While in Console mode, this routine supplies characters from a prior REPLY
   command as though they were typed by the user.  This allows command files to
   contain automated prompt/response pairs for console interaction.

   If a reply exists, a check is made to ensure that any specified reply delay
   has been met.  If it hasn't, then keyboard polling is performed normally.  If
   it has, or if we are in the middle of a reply, the next character in the
   reply string is returned to the caller.  If the next character is a NUL, then
   the reply is exhausted, and the reply context is cleared.


   Implementation notes:

    1. It would be nice to have better editing capability in Command mode, e.g.,
       to be able to recall from a command history and edit the resulting line.
       Unfortunately, the standard "sim_poll_kbd" routine returns characters and
       not keystrokes, so it is impossible to detect, e.g., an up-arrow key.  It
       might be easy to implement a one-line recall command by restoring the
       command buffer to the state just prior to ENTER, though then only the
       usual editing (BS and typing replacements) would be available.

    2. Currently, only a reply to a single device (the console) is allowed, so
       the reply list head pointer is used directly.  In the future, a linked
       list of reply structures will be used, and the routine will have to
       search the list for the one matching the console unit.
*/

t_stat ex_sim_poll_kbd (void)
{
RPPTR  rp;
int32  reply_char;
t_stat key_char;

rp = rp_list;                                           /* start searching at the head of the reply list */

if (keyboard_mode == Console) {                         /* if we are in console mode */
    if (rp != NULL) {                                   /*   then if a REPLY is pending */
        if (rp->rptr > rp->reply                        /*     then if we are already replying */
          || sim_gtime () >= rp->trigger) {             /*       or the delay time has been met */
            reply_char = (int32) *rp->rptr++;           /*         then get the reply next character */

            if (reply_char == 0)                        /* if it's the end-of-string NUL */
                rp_list = NULL;                         /*   then clear the reply */

            else if (reply_char == sim_brk_char)        /* otherwise if it's the break character */
                return SCPE_BREAK;                      /*   then report the break */

            else                                        /* otherwise */
                return reply_char | SCPE_KFLAG;         /*   return the reply character */
            }
        }

    if (stop_requested) {                               /* if WRU was detected via a signal */
        key_char = SCPE_STOP;                           /*   then indicate a simulator stop */
        stop_requested = FALSE;                         /*     and clear the request */
        }

    else                                                /* otherwise */
        key_char = sim_poll_kbd ();                     /*   poll the keyboard for a key */

    if (key_char == SCPE_STOP && concurrent_mode) {     /* if it's the sim stop character and in concurrent mode */
        keyboard_mode = Command;                        /*   then switch to command mode */

        put_string ("\r\nscp> ");                       /* print the concurrent command prompt */

        cmd_ptr = cmd_buf;                              /* reset the command buffer pointer */
        *cmd_ptr = '\0';                                /*   and clear any previous command */

        return SCPE_OK;                                 /* return while absorbing the character */
        }

    else                                                /* otherwise */
        return key_char;                                /*   return the character */
    }

else {                                                  /* otherwise we're in command mode */
    if (stop_requested) {                               /* if WRU was detected via a signal */
        key_char = SCPE_STOP;                           /*   then indicate a simulator stop */
        stop_requested = FALSE;                         /*     and clear the request */
        }

    else                                                /* otherwise */
        key_char = sim_os_poll_kbd ();                  /*   poll the simulation console keyboard for a key */

    if (key_char == SCPE_STOP) {                        /* if it's the sim stop character */
        keyboard_mode = Console;                        /*   then return to console mode */

        put_string ("\r\n");                            /* skip to the next line */

        cmd_ptr = cmd_buf;                              /* reset the command buffer pointer */
        *cmd_ptr = '\0';                                /*   and clear any pending command */

        return SCPE_STOP;                               /* stop the simulator */
        }

    else if (key_char & SCPE_KFLAG) {                   /* otherwise if a character was obtained */
        key_char = key_char & 0177;                     /*   then mask to just the value */

        if (key_char == CR || key_char == LF) {         /* if the character is carriage return or line feed */
            keyboard_mode = Console;                    /*   then return to console mode */

            put_string ("\r\n");                        /* skip to the next line */

            if (cmd_ptr != cmd_buf) {                   /* if the buffer is occupied */
                *cmd_ptr = '\0';                        /*   then terminate the command buffer */
                return SCPE_EXEC;                       /*     and execute the command */
                }
            }

        else if (key_char == BS || key_char == DEL) {   /* otherwise if the character is backspace or delete */
            if (cmd_ptr > cmd_buf) {                    /*   then if the buffer contains characters */
                cmd_ptr--;                              /*     then drop the last one */
                put_string ("\b \b");                   /*       and clear it from the screen */
                }
            }

        else if (key_char == ESC)                       /* otherwise if the character is escape */
            while (cmd_ptr > cmd_buf) {                 /*   then while characters remain in the buffer */
                cmd_ptr--;                              /*     then drop them one by one */
                put_string ("\b \b");                   /*       and clear them from the screen */
                }

        else {                                          /* otherwise it's a normal character */
            *cmd_ptr++ = (char) (key_char);             /*   so add it to the buffer and advance the pointer */
            sim_os_putchar (key_char);                  /*     and echo it to the screen */
            }
        }

    if (sim_con_tmxr.master != 0)                       /* if the consoles are separate */
        return sim_poll_kbd ();                         /*   then poll the system console keyboard */
    else                                                /* otherwise we're in unified command mode */
        return SCPE_OK;                                 /*   so return with any obtained character absorbed */
    }
}



/* Local concurrent console and reply extension routines */


/* Signal handler for CTRL+E.

   This routine is a SIGINT handler that is installed to detect CTRL+E on Unix
   systems and CTRL+C on others.  It is used in place of the standard
   "int_handler" routine.  That routine sets the global "stop_cpu" flag, which
   is tested in "sim_process_event" and causes simulated execution to stop.  For
   concurrent console operation, we must detect the condition without setting
   the global flag.  So this routine sets a local flag that is tested by our
   "ex_sim_poll_kbd" routine to switch from Console to Command mode.

   It is also used in our "execute_file" command to abort a DO command file that
   may be stuck in an infinite loop.


   Implementation notes:

    1. On Unix systems, WRU is registered as the SIGINT character, so CTRL+E
       does not arrive via the keyboard poll.  Instead, a SIGINT handler is
       used, which is why we need this handler to detect initiation of Command
       mode on Unix.
*/

static void wru_handler (int sig)
{
stop_requested = TRUE;                                  /* indicate that WRU was seen */
return;                                                 /*   and continue execution */
}


/* Write a string of characters to the console */

static void put_string (const char *cptr)
{
while (*cptr != '\0')                                   /* write characters to the console */
    sim_os_putchar (*cptr++);                           /*   until the end of the string */

return;
}


/* Get a command descriptor.

   This routine searches for the command whose name is indicated by the "cptr"
   parameter and copies the corresponding command descriptor (CTAB) entry
   pointer into the pointer variable designated by the "cmdp" pointer.  If the
   command is not found, the routine sets the pointer variable to NULL and
   returns SCPE_UNK (Unknown command).  If the command is found but is
   restricted, the routine returns SCPE_NOFNC (Command not allowed).  Otherwise,
   the routine returns SCPE_OK.

   This routine is similar to the standard "find_cmd" routine, except that it
   also checks to see if the simulator is currently running and, if so, that the
   command is in the list of unrestricted commands.  A command entered while the
   simulator is running must not interfere with execution; those that do are
   deemed "restricted" commands.


   Implementation notes:

    1. The unrestricted ("allowed") command list is structured as a simple
       string, with the command names preceded and followed by spaces.  This
       allows a simple "strstr" search to look for a match.

    2. We search the list of unrestricted commands for the full command name to
       avoid false matches by, e.g., searching for the entered command "R"
       instead of "RUN".  We also add leading and trailing blanks before
       searching to ensure that we haven't matched a substring of an
       unrestricted command (although currently there are no restricted commands
       that are substrings of unrestricted commands).

    3. The "cptr" parameter cannot be declared "const" because it is passed to
       "find_cmd", which takes a non-const pointer.
*/

static const char allowed_cmds [] = " "                         /* the list of unrestricted commands */
    "RESET "    "EXAMINE "  "DEPOSIT "  "EVALUATE " "BREAK "    /*   standard commands */
    "NOBREAK "  "ATTACH "   "DETACH "   "ASSIGN "   "DEASSIGN "
    "EXIT "     "QUIT "     "BYE "      "SET "      "SHOW "
    "DO "       "ECHO "     "ASSERT "   "HELP "

    "REPLY "    "NOREPLY "  "IF "       "DELETE "   "ABORT "    /*   extended commands */

    "POWER ";                                                   /*   simulator-specific commands */

static t_stat get_command (char *cptr, CTAB **cmdp)
{
char   cmd_name [80];
t_stat status;

*cmdp = find_cmd (cptr);                                /* search for the command */

if (*cmdp == NULL)                                      /* if the command is not valid */
    status = SCPE_UNK;                                  /*   then report it as unknown */

else if (sim_is_running) {                              /* otherwise if commands are currently restricted */
    cmd_name [0] = ' ';                                 /*   then surround */
    strcpy (cmd_name + 1, (*cmdp)->name);               /*     the command name */
    strcat (cmd_name, " ");                             /*       with leading and trailing blanks */

    if (strstr (allowed_cmds, cmd_name) == NULL)        /* if the command keyword was not found in the list */
        status = SCPE_NOFNC;                            /*   then the command is restricted */
    else                                                /* otherwise */
        status = SCPE_OK;                               /*   the command is allowed */
    }

else                                                    /* otherwise commands are not restricted */
    status = SCPE_OK;                                   /*   so the command is valid */

return status;                                          /* return the search result */
}



/* ************************  SCP Command Extensions  ***************************

   This module extends the following existing commands:

     RUN      -- reset and start simulation
     GO       -- start simulation
     STEP     -- execute <n> instructions
     CONTINUE -- continue simulation
     BOOT     -- bootstrap simulation
     BREAK    -- set breakpoints
     NOBREAK  -- clear breakpoints
     DO       -- execute commands in a file
     SET      -- set simulator options
     SHOW     -- show simulator options

   ...and adds the following new commands:

     REPLY   -- send characters to the console
     NOREPLY -- cancel a pending reply
     IF      -- execute commands if condition TRUE
     DELETE  -- delete a file
     GOTO    -- transfer control to the labeled line
     CALL    -- call the labeled subroutine
     RETURN  -- return control from a subroutine
     ABORT   -- abort nested command files

   The RUN and GO commands are enhanced to add an UNTIL option that sets a
   temporary breakpoint, and all of the simulated execution commands are
   enhanced to save and restore an existing SIGINT handler that may be in effect
   within a DO command file.  The BREAK and NOBREAK commands are enhanced to
   provide temporary and string breakpoints.  DO is enhanced to provide GOTO,
   CALL, RETURN, and ABORT commands that affect the flow of control.  SET and
   SHOW are enhanced to provide access to environment variables, to provide
   serial support to the system console, and to provide a concurrent command
   mode during simulated execution.

   The new REPLY and NOREPLY commands enable and disable automated responses
   from the system console.  IF provides conditional command execution.  DELETE
   provides a host-independent method of deleting a file, such as a temporary or
   scratch file.

   Also, an extension is provided to add optional binary data interpretation to
   the existing octal, decimal, and hexadecimal command-line overrides.

   In addition, command-line parameter substitution is extended to provide a set
   of substitution variables that yield the current date and time in various
   formats, as well as environment variable values.  Combined with the new SET
   ENVIRONMENT and IF commands, arbitrary variable values may be set, tested,
   and used to affect command file execution.
*/


/* Global SCP command extension handler routines */


/* Execute the BREAK and NOBREAK commands.

   This command processing routine enhances the existing BREAK and NOBREAK
   commands to provide temporary and string breakpoints.  The routine processes
   commands of the form:

     BREAK { -T } <address-list> { ; <action> ... }
     BREAK { -T } <quoted-string> { ; <action> ... }
     BREAK { -T } <quoted-string> DELAY <delay> { ; <action> ... }
     BREAK DELAY <delay>
     NOBREAK <quoted-string>
     NOBREAK ""
     NOBREAK ALL

   Where:

     -T    = indicates that the breakpoint is temporary

     delay = the number of event ticks that elapse after the breakpoint is
             satisfied before execution is stopped; default is 0

   The new "-T" breakpoint type switch indicates that the breakpoint will be set
   temporarily.  A temporary breakpoint is removed once it occurs; this is
   equivalent to setting the (first) breakpoint action to NOBREAK.  Without
   "-T", the breakpoint is persistent and will cause a simulation stop each time
   it occurs.

   String breakpoints cause simulator stops when the character sequences are
   encountered in the system console output stream; these are similar to stops
   that occur when CPU execution reaches specified memory addresses.

   The first string form sets a breakpoint that stops simulator execution when
   the content of the quoted string appears in the system console output.  By
   default, the simulator stops immediately after the final character of the
   quoted string is output.  The second string form may be used to insert a
   delay of the specified number of event ticks (e.g., machine instructions)
   before execution stops.  The delay is set temporarily for that breakpoint; it
   then reverts to a zero delay for subsequent breakpoints.

   If all of the string breakpoints for a program require a delay, it may be set
   as the new default by using the BREAK DELAY command.

   The optional action commands are executed when the breakpoint occurs.  If the
   breakpoint is temporary, the actions are executed once; otherwise, they
   execute each time the breakpoint occurs.

   The first NOBREAK command form cancels the string breakpoint specified by the
   quoted string.  The second form cancels all string breakpoints; the empty
   quoted string is required to differentiate between canceling string
   breakpoints and canceling the current address breakpoint.  The NOBREAK ALL
   command cancels all string breakpoints in addition to canceling all address
   breakpoints.

   Specifying a quoted string in a BREAK command that matches an existing
   breakpoint replaces the delay and actions of that breakpoint with the values
   specified in the new BREAK command.  It does not create a second breakpoint
   with the same string.


   Implementation notes:

    1. Currently, only one type of string breakpoint is defined (the implicit
       "BP_STRING" type), although we include all command-line switches in the
       "type" field of the breakpoint structure for future use.

    2. A NOBREAK command specifying a breakpoint string that does not match any
       existing breakpoint succeeds with no warning message to be consistent
       with the behavior of NOBREAK <address> that does match an existing
       breakpoint.
*/

#define SIM_BREAK_MASK      ((1u << 26) - 1 & ~SWMASK ('T'))    /* mask for the alpha switches except "T" */

static t_stat ex_break_cmd (int32 flag, char *cptr)
{
SBPTR  bp, prev;
char   *aptr, *optr, mbuf [CBUFSIZE];
int32  delay;
t_stat status;

cptr = get_sim_sw (cptr);                               /* get any command-line switches */

if (cptr == NULL)                                       /* if an invalid switch was present */
    return SCPE_INVSW;                                  /*   then report it */
else                                                    /* otherwise */
    optr = cptr;                                        /*   save the original command-line pointer */

if (flag == SSH_ST && (*cptr == 'd' || *cptr == 'D')) { /* if this might be a BREAK DELAY command */
    status = parse_delay (&cptr, &delay);               /*   then attempt to parse a DELAY clause */

    if (status != SCPE_OK)                              /* if the numeric parse failed */
        return status;                                  /*   then return the error status */

    else if (delay >= 0)                                /* otherwise if the delay was given */
        if (*cptr != '\0')                              /*   then if more characters follow */
            return SCPE_2MARG;                          /*     then too many arguments were given */

        else {                                          /*   otherwise */
            break_delay = delay;                        /*     set the global delay value */
            return SCPE_OK;                             /*       and we're done */
            }
    }

if (*cptr == '\'' || *cptr == '"') {                    /* if a quoted string is present */
    cptr = parse_quoted_string (cptr, mbuf, FALSE);     /*   then parse it with decoding */

    if (cptr == NULL)                                   /* if the string is not terminated */
        return SCPE_ARG;                                /*   then report a bad argument */

    else                                                /* otherwise the string is valid */
        if (flag == SSH_CL)                             /*   so if this is a NOBREAK command */
            if (*cptr != '\0')                          /*     then if there are extraneous characters */
                return SCPE_2MARG;                      /*       then report too many arguments */

            else if (mbuf [0] == '\0') {                /*     otherwise if the string is empty */
                free_breakpoints ();                    /*       then free all of the string breakpoints */
                return SCPE_OK;                         /*         and we're done */
                }

            else {                                      /*     otherwise */
                bp = find_breakpoint (mbuf, &prev);     /*       find the specified breakpoint */

                if (bp != NULL) {                       /* if it is present */
                    if (prev != NULL)                   /*   then if there is a previous node */
                        prev->next = bp->next;          /*     then link it to the next one */
                    else                                /*   otherwise we're clearing the first node */
                        sb_list = bp->next;             /*     so point the header at the next one */

                    free (bp);                          /* free the current node */
                    }

                return SCPE_OK;                         /* either way, we're done */
                }

        else {                                          /*   otherwise this is a BREAK command */
            aptr = strchr (cptr, ';');                  /*     so search for actions */

            if (aptr != NULL)                           /* if actions are present */
                *aptr++ = '\0';                         /*   then separate the actions from the breakpoints */

            if (*cptr == '\0')                          /* if no DELAY clause follows */
                delay = break_delay;                    /*   then use the global delay value */

            else {                                      /* otherwise */
                status = parse_delay (&cptr, &delay);   /*   attempt to parse a DELAY clause */

                if (status != SCPE_OK)                  /* if the numeric parse failed */
                    return status;                      /*   then return the error status */

                else if (delay < 0)                     /* otherwise if the keyword is not DELAY */
                    return SCPE_ARG;                    /*   then the syntax is bad */

                else if (*cptr != '\0')                 /* otherwise if more characters follow */
                    return SCPE_2MARG;                  /*   then too many arguments were given */
                }

            bp = find_breakpoint (mbuf, &prev);         /* see if the string matches an existing breakpoint */

            if (bp == NULL) {                                       /* if it does not */
                bp = (SBPTR) malloc (sizeof (STRING_BREAKPOINT));   /*   then allocate a new breakpoint */

                if (bp == NULL)                         /* if the allocation failed */
                    return SCPE_MEM;                    /*   then report the error */

                else if (prev == NULL)                  /* otherwise if this is the first breakpoint */
                    sb_list = bp;                       /*   then set the list header to point at it */

                else                                    /* otherwise */
                    prev->next = bp;                    /*   add it to the end of the existing list */
                }

            bp->next = NULL;                            /* set the next node pointer */
            bp->uptr = vm_console_output_unit;          /*   and the output unit pointer */

            strcpy (bp->match, mbuf);                   /* copy the match string */
            bp->mptr = bp->match;                       /*   and set the match pointer to the start */

            bp->type    = sim_switches | BP_STRING;     /* add the "string breakpoint" flag */
            bp->count   = 0;                            /* clear the count */
            bp->delay   = delay;                        /* set the delay value */
            bp->trigger = -1.0;                         /*   and clear the trigger time */

            if (aptr == NULL)                           /* if no actions were specified */
                bp->action [0] = '\0';                  /*   then clear the action buffer */

            else {                                      /* otherwise */
                while (isspace (*aptr))                 /*   skip any leading blanks */
                    aptr++;                             /*     that might precede the first action */

                strcpy (bp->action, aptr);              /* copy the action string */
                }

            return SCPE_OK;                             /* return with success */
            }
    }

else {                                                          /* otherwise */
    if (flag == SSH_ST && (sim_switches & SIM_BREAK_MASK) == 0) /*   if no breakpoint type switches are set */
        sim_switches |= sim_brk_dflt;                           /*     then use the specified default types */

    status = break_handler (flag, optr);                /* process numeric breakpoints */

    if (status == SCPE_OK && flag == SSH_CL) {          /* if the NOBREAK succeeded */
        get_glyph (cptr, mbuf, 0);                      /*   then parse out the next glyph */

        if (strcmp (mbuf, "ALL") == 0)                  /* if this was a NOBREAK ALL command */
            free_breakpoints ();                        /*   then clear all string breakpoints too */
        }

    return status;                                      /* return the command status */
    }
}


/* Execute the REPLY and NOREPLY commands.

   This command processing routine adds new REPLY and NOREPLY commands to
   automate replies through the system console when programmatic input is next
   requested by the target OS.  The routine processes commands of the form:

     REPLY <quoted-string>
     REPLY <quoted-string> DELAY <delay>
     REPLY DELAY <delay>
     NOREPLY

   Where:

     delay = the number of event ticks that must elapse before the first
             character of the reply is sent; default is 0

   The first form supplies the content of the quoted string to the system
   console, character by character, as though entered by pressing keys on the
   keyboard.  By default, the first character is supplied to the console device
   immediately after simulation is resumed with a GO or CONTINUE command.  The
   second form may be used to insert a delay of the specified number of event
   ticks (e.g., machine instructions) before the first character is supplied.

   If the second form is used, the delay is set temporarily for that reply; it
   then reverts to a zero delay for subsequent replies.  If all of the replies
   to a program require a delay, it may be set as the new default by using the
   third form.

   The NOREPLY command cancels any pending reply.  Replies are also effectively
   canceled when they are consumed.


   Implementation notes:

    1. Currently, only a reply to a single device (the console) is allowed, so
       the reply list head pointer is set to point at a static structure.  In
       the future, the structures will be allocated and deallocated dynamically.
*/

static t_stat ex_reply_cmd (int32 flag, char *cptr)
{
char   rbuf [CBUFSIZE];
int32  delay;
t_stat status;

if (flag) {                                             /* if this is a NOREPLY command */
    rp_list = NULL;                                     /*   then clear any pending reply */
    return SCPE_OK;                                     /*     and we're done */
    }

else if (*cptr == '\0')                                 /* otherwise if a REPLY has no quoted string */
    return SCPE_MISVAL;                                 /*   then report it as missing */

if (*cptr == 'd' || *cptr == 'D') {                     /* if this might be a REPLY DELAY command */
    status = parse_delay (&cptr, &delay);               /*   then attempt to parse a DELAY clause */

    if (status != SCPE_OK)                              /* if the numeric parse failed */
        return status;                                  /*   then return the error status */

    else if (delay >= 0)                                /* otherwise if the delay was given */
        if (*cptr != '\0')                              /*   then if more characters follow */
            return SCPE_2MARG;                          /*     then too many arguments were given */

        else {                                          /*   otherwise */
            reply_delay = delay;                        /*     set the global delay value */
            return SCPE_OK;                             /*       and we're done */
            }
    }

if (*cptr == '\'' || *cptr == '"') {                    /* if a quoted string is present */
    cptr = parse_quoted_string (cptr, rbuf, FALSE);     /*   then parse it with decoding */

    if (cptr == NULL)                                   /* if the string is not terminated */
        return SCPE_ARG;                                /*   then report a bad argument */

    else {                                              /* otherwise the string is valid */
        if (*cptr == '\0')                              /* if no DELAY clause follows */
            delay = reply_delay;                        /*   then use the global delay value */

        else {                                          /* otherwise */
            status = parse_delay (&cptr, &delay);       /*   attempt to parse a DELAY clause */

            if (status != SCPE_OK)                      /* if the numeric parse failed */
                return status;                          /*   then return the error status */

            else if (delay < 0)                         /* otherwise if the keyword is not DELAY */
                return SCPE_ARG;                        /*   then the syntax is bad */

            else if (*cptr != '\0')                     /* otherwise if more characters follow */
                return SCPE_2MARG;                      /*   then too many arguments were given */
            }

        rp_list = &rpx;                                 /* point at the new reply structure */

        rp_list->uptr = vm_console_input_unit;          /* set the input unit pointer */

        strcpy (rp_list->reply, rbuf);                  /* copy the reply string */
        rp_list->rptr = rp_list->reply;                 /*   and point at the starting character */

        rp_list->trigger = sim_gtime () + delay;        /* set the trigger time delay */

        return SCPE_OK;                                 /* return success */
        }
    }

else                                                    /* otherwise something other than */
    return SCPE_ARG;                                    /*   a quoted string is present */
}


/* Execute the RUN, GO, STEP, CONTINUE, and BOOT commands.

   This command processing routine enhances the existing RUN and GO commands to
   provide optional temporary breakpoints.  The routine processes RUN and GO
   commands of the form:

     GO UNTIL <stop-address> { ; <action> ... }
     GO UNTIL <quoted-string> { ; <action> ... }
     GO UNTIL <quoted-string> DELAY <delay> { ; <action> ... }
     GO <start-address> UNTIL <stop-address> { ; <action> ... }
     GO <start-address> UNTIL <quoted-string> { ; <action> ... }
     GO <start-address> UNTIL <quoted-string> DELAY <delay> { ; <action> ... }

   The "GO UNTIL" command is equivalent to "BREAK -T" and "GO".  Multiple
   <stop-address>es, separated by commas, may be specified.  For example, "GO 5
   UNTIL 10,20" sets temporary breakpoints at addresses 10 and 20 and then
   resumes simulator execution at address 5.  As with the BREAK command,
   specifying a DELAY value sets a temporary delay of the specified number of
   event ticks before execution stops.  If a DELAY value is not given, the
   breakpoint uses the default delay set by an earlier BREAK DELAY command, or a
   zero delay if the default has not been overridden.

   The STEP, CONTINUE, and BOOT commands are unaltered.  They are handled here
   so that all execution commands may set the "SIM_SW_HIDE" command switch to
   suppress step and breakpoint messages while executing in command files.  This
   allows automated prompt/response pairs to be displayed without cluttering up
   the output with intervening "Step completed" and "Breakpoint" messages.


   Implementation notes:

    1. The DO executor sets a SIGINT handler to permit interrupting an infinite
       command loop.  We save and restore this handler around the call to the
       standard RUN command handler because that routine restores the default
       handler instead of the previous handler when it completes.  This action
       would cancel the DO executor's handler installation if we did not save
       and restore it here.

       We save the prior handler by installing the default handler, which is the
       condition the standard RUN handler expects on entry.

    2. A DO command in concurrent mode must be handled outside of the VM's
       instruction execution routine.  This is because we want to permit
       unrestricted commands, such as GO UNTIL, to enable prompt/response
       entries in the command file.  But we cannot execute the DO command after
       exiting our routine, e.g., by setting up the command as a breakpoint
       action and then returning, because if we've been called by an enclosing
       command file, returning will advance the file pointer, so that the
       command that invoked us won't be reexecuted.  Instead, we must call the
       DO command processor here and then reenter the instruction execution
       routine if no error exists.

    3. The DO command handler clears "sim_switches" for each command invocation,
       so we must save the run switches and restore them after handling a
       concurrent DO command.

    4. The global "concurrent_run" flag may be examined within a DO command file
       executing in concurrent mode.  The flag is initially FALSE.  We save and
       restore the flag on entry and exit to ensure that it remains TRUE if
       simulation is stopped within the DO file (if we set it FALSE on exit, it
       would show FALSE when examined after a command file breakpoint (e.g.),
       even though we were still executing within the context of a
       currently-running session, which would resume when the DO file is
       finished.

    5. All errors from a concurrent DO file invocation are reported but do not
       stop CPU execution.  The exception is the EXIT command, which not only
       stops the CPU but also exits the simulator (the alternative would be to
       treat EXIT as a NOP, but then the DO file would exhibit different
       behavior, depending on whether or not it was invoked in concurrent mode).
*/

static t_stat ex_run_cmd (int32 flag, char *cptr)
{
SIG_HANDLER prior_handler;
char        gbuf [CBUFSIZE], pbuf [CBUFSIZE];
t_stat      status;
t_bool      entry_concurrency = concurrent_run;         /* save the concurrent run status on entry */
int32       entry_switches = sim_switches;              /* save a copy of the entry switches */

keyboard_mode = Console;                                /* always start in console mode */

if (*cptr != '\0' && (flag == RU_RUN || flag == RU_GO)) {   /* if something follows and this is a RUN or GO */
    if (*cptr == 'U' || *cptr == 'u')                       /*   then if an UNTIL clause follows */
        pbuf [0] = '\0';                                    /*     there there is no new P value */
    else                                                    /*   otherwise */
        cptr = get_glyph (cptr, pbuf, 0);                   /*     get the new P value */

    if (*cptr == '\0')                                  /* if nothing follows the new P value */
        cptr = pbuf;                                    /*   then point at the P value */

    else {                                              /* otherwise */
        cptr = get_glyph (cptr, gbuf, 0);               /*   get the next glyph */

        if (strcmp (gbuf, "UNTIL") == 0)                /* if this is an UNTIL clause */
            if (*cptr == '\0')                          /*   then if nothing follows */
                return SCPE_MISVAL;                     /*     then report that the address is missing */

            else if (*cptr == 'D' || *cptr == 'd')      /*   otherwise if it is immediately followed by a DELAY clause */
                return SCPE_ARG;                        /*     then report a syntax error */

            else {                                      /* otherwise */
                sim_switches |= SWMASK ('T');           /*   add the temporary breakpoint flag */

                status = ex_break_cmd (SSH_ST, cptr);   /* process and set the breakpoint */

                sim_switches = entry_switches;          /* restore the original switches */

                if (status == SCPE_OK)                  /* if the breakpoint parsed correctly */
                    cptr = pbuf;                        /*   then point at the P value */
                else                                    /* otherwise a parse error occurred */
                    return status;                      /*   so report it */
                }

        else                                            /* otherwise something other than UNTIL follows */
            return SCPE_ARG;                            /*   so report a syntax error */
        }
    }

prior_handler = signal (SIGINT, SIG_DFL);               /* install the default handler and save the current one */

if (prior_handler == SIG_ERR)                           /* if installation failed */
    status = SCPE_SIGERR;                               /*   then report an error */

else {                                                  /* otherwise */
    concurrent_run = TRUE;                              /*   mark the VM as running */

    do {                                                /* loop to process concurrent DO commands */
        concurrent_do_ptr = NULL;                       /* clear the DO pointer */

        status = run_handler (flag, cptr);              /* call the base handler to run the simulator */

        if (concurrent_do_ptr == NULL)                  /* if a DO command was not entered */
            break;                                      /*   then fall out of the loop */

        else {                                          /* otherwise a concurrent DO command was entered */
            strcpy (gbuf, concurrent_do_ptr);           /*   so copy the command parameters locally */
            status = ex_do_handler (1, gbuf);           /*     and execute the DO command */

            if (status != SCPE_OK && status != SCPE_EXIT) { /* if the command failed */
                printf ("%s\n", sim_error_text (status));   /*   then print the error message */

                if (sim_log)                                /* if the console is logging */
                    fprintf (sim_log, "%s\n",               /*   then write it to the log file as well */
                             sim_error_text (status));

                status = SCPE_OK;                       /* continue execution unless it was an EXIT command */
                }

            if (sim_vm_post != NULL)                    /* if the VM wants command notification */
                (*sim_vm_post) (TRUE);                  /*   then let it know we executed a command */

            sim_switches = entry_switches;              /* restore the original switches */
            }
        }
    while (status == SCPE_OK);                          /* continue to execute in the absence of errors */

    concurrent_run = entry_concurrency;                 /* return to the previous VM-running state */

    signal (SIGINT, prior_handler);                     /* restore the prior handler */
    }

return status;                                          /* return the command status */
}


/* Execute the DO command.

   This command processing routine enhances the existing DO command to permit
   CTRL+C to abort a command file or a nested series of command files.  The
   actual command file processing is handled by a subsidiary routine.

   It also executes commands in a new global initialization file at system
   startup.  It looks for the file "simh.ini" in the current directory; if not
   found, it then looks for the file in the HOME or USERPROFILE directory.  The
   file is optional; if it exists, it is executed before the command-line file
   or the simulator-specific file.

   Therefore, the search order for "simh.ini" is the current directory first,
   then the HOME directory if the HOME variable exists, or else the USERPROFILE
   directory if that variable exists.  So a user may override a "simh.ini" file
   present in the HOME or USERPROFILE directories by one in the current
   directory.

   An error encountered in the global initialization file is reported and stops
   execution of that file but does not inhibit execution of the simulator-
   specific initialization file.

   On entry, the "cptr" parameter points at the invocation string after the DO
   keyword.  The "file" parameter is NULL to indicate that the routine is to
   open the filename present at the start of the "cptr" string.  The "flag"
   parameter indicates the source of the call and nesting level and contains one
   of these values:

     < 0 = initialization file (no alternate if not found)
       0 = startup command line file
       1 = "DO" command
     > 1 = nested DO or CALL command

   For a nested command call, "flag" contains the nesting level in bits 0-3 and
   the value of the invoking switches in bits 4-29.  This allows the switch
   settings to propagate to nested command files.


   Implementation notes:

    1. This routine is always called during system startup before the main
       command loop is entered.  It is called to execute the command file
       specified on the command line, or, if there is none, the command file
       associated with the simulator (e.g., "hp2100.ini").  The call occurs even
       if neither of these files exist.  We detect this initial call and execute
       the global initialization file before either of these command files.

    2. We save the command-line switches before executing the global
       initialization file, so that they will apply to the command-line file or
       the local initialization file.  Otherwise, execution of a command in the
       global file would reset the switches before the second file is executed.

    3. The invocations of the global and command/simulator files are considered
       to be separate executions.  Consequently, an ABORT in the global file
       terminates that execution but does not affect execution of the
       command/simulator file.

    4. If the simulator was invoked with command-line parameters, we pass them
       to the global initialization file.  So, for example, "%1" will be the
       command filename that will be executed, "%2" will be the first parameter
       passed to that file, etc.  In this case, the "flag" parameter will be 0.
       If we are called to execute the simulator-specific initialization file,
       then there must not have been any command-line parameters, and "flag"
       will be -1.

    5. We use a filename buffer of twice the standard size, because the home
       path and the command-line parameter string may each be up to almost a
       full standard buffer size in length.
*/

static t_stat ex_do_cmd (int32 flag, char *cptr)
{
static t_bool first_call = TRUE;                        /* TRUE if this is the first DO call of the session */
SIG_HANDLER   prior_handler;
t_stat        status;
int32         entry_switches;
char          separator, filename [CBUFSIZE * 2], *home;

prior_handler = signal (SIGINT, wru_handler);           /* install our WRU handler in place of the current one */

if (prior_handler == SIG_ERR)                           /* if installation failed */
    status = SCPE_SIGERR;                               /*   then report an error */

else {                                                  /* otherwise */
    if (first_call) {                                   /*   if this is the startup call */
        first_call = FALSE;                             /*     then clear the flag for subsequent calls */
        entry_switches = sim_switches;                  /*       and save the command-line switches */

        strcpy (filename, "simh.ini ");                 /* start with the filename in the working directory */

        if (flag == 0)                                  /* if command-line parameters were specified */
            strcat (filename, cptr);                    /*   then append them to the filename */

        status = ex_do_handler (-1, filename);          /* try to execute the global startup file */

        if (status == SCPE_OPENERR) {                   /* if it was not found */
            home = getenv ("HOME");                     /*   then get the home directory */

            if (home == NULL)                           /* if it's not defined */
                home = getenv ("USERPROFILE");          /*   then try the profile directory */

            if (home != NULL) {                             /* if it's defined */
                separator = home [strcspn (home, "/\\")];   /*   then look for a directory separator */

                if (separator == '\0')                  /* if there isn't one */
                    separator = '/';                    /*   then guess that "/" will do */

                sprintf (filename, "%s%csimh.ini %s",   /* form the home path and global filename */
                         home, separator,               /*   and add any command-line parameters */
                         (flag == 0 ? cptr : ""));      /*     if they are present */

                status = ex_do_handler (-1, filename);  /* try to execute the global startup file */
                }
            }

        sim_switches = entry_switches;                  /* restore the entry switches */
        }

    status = execute_file (NULL, flag, cptr);           /* execute the indicated command file */

    if (status == SCPE_ABORT && flag <= 1)              /* if an abort occurred and we're at the outermost level */
        status = SCPE_OK;                               /*   then clear the error to suppress the abort message */

    signal (SIGINT, prior_handler);                     /* restore the prior handler */
    }

return status;                                          /* return the command status */
}


/* Execute the IF command.

   This command processing routine adds a new IF command to test a condition and
   execute the associated command(s) if the condition is true.  The routine
   processes commands of the form:

     IF { -I } <comparative-expression> <action> { ; <action> ... }

   Where the comparative expression forms are:

     <Boolean-expression>
     <Boolean-expression> <logical> <comparative-expression>

   ...and the Boolean expression forms are:

     <quoted-string> <equality> <quoted-string>
     <quoted-string> IN <quoted-string> { , <quoted-string> ...}
     <quoted-string> NOT IN <quoted-string> { , <quoted-string> ...}
     EXIST <quoted-string>
     NOT EXIST <quoted-string>

   The logical operators are && (And) and || (Or).  The equality operators are
   == (equal to) and != (not equal to).

   The IN operation returns true if the first quoted string is equal to any of
   the listed quoted strings, and the NOT IN operation returns true if the first
   quoted string is not equal to any of the listed strings.

   The EXIST operation returns true if the file specified by the quoted string
   exists.  The NOT EXIST operation returns true if the file does not exist.

   If the comparative expression is true, the associated actions are executed.
   If the expression is false, the actions have no effect.  Adding the "-I"
   switch causes the comparisons to be made case-insensitively.  Evaluation is
   strictly from left to right; embedded parentheses to change the evaluation
   order are not accepted.

   Typically, one quoted-string is a percent-enclosed substitution variable and
   the other is a literal string.  Comparisons are always textual, so, for
   example, "3" is not equal to "03".


   Implementation notes:

    1. For a true comparison, the action part of the IF command line is copied
       to a temporary buffer, and the break action pointer is set to point at
       the buffer.  This is done instead of simply setting "sim_brk_act" to
       "cptr" because the "memcpy" and "strncpy" functions that are used to copy
       each command into the command buffer produce implementation-defined
       results if the buffers overlap ("cptr" points into the command buffer, so
       "sim_brk_act" would be copying a command within the buffer to the start
       of the same buffer).
*/

typedef enum {                                  /* test operators */
    Comparison,                                 /*   == or != operator */
    Existence,                                  /*   EXIST or NOT EXIST operator */
    Inclusion                                   /*   IN or NOT IN operator */
    } TEST_OP;

typedef enum {                                  /* logical operators */
    Assign,                                     /*   null operator */
    And,                                        /*   AND operator */
    Or                                          /*   OR operator */
    } LOGICAL_OP;

static t_stat ex_if_cmd (int32 flag, char *cptr)
{
static char tempbuf [CBUFSIZE];
struct stat statbuf;
int         result, condition;
char        abuf [CBUFSIZE], bbuf [CBUFSIZE];
t_bool      upshift, invert;
TEST_OP     test;
LOGICAL_OP  logical = Assign;
t_bool      not_done = TRUE;                            /* TRUE if more comparisons are present */

cptr = get_sim_sw (cptr);                               /* get a possible case-sensitivity switch */

if (cptr == NULL)                                       /* if an invalid switch was present */
    return SCPE_INVSW;                                  /*   then report it */

else if (*cptr == '\0')                                 /* otherwise if the first operand is missing */
    return SCPE_2FARG;                                  /*   then report it */

upshift = (sim_switches & SWMASK ('I')) != 0;           /* TRUE if the comparison is case-insensitive */

do {                                                    /* loop until all conditionals are processed */
    test = Comparison;                                  /* assume a comparison until proven otherwise */

    if (*cptr == '\'' || *cptr == '"') {                    /* if a quoted string is present */
        cptr = parse_quoted_string (cptr, abuf, upshift);   /*   then get the first operand */

        if (cptr == NULL)                               /* if the operand isn't quoted properly */
            return SCPE_ARG;                            /*   then report a bad argument */

        else if (*cptr == '\0')                         /* otherwise if the operator is missing */
            return SCPE_2FARG;                          /*   then report it */

        else {                                          /* otherwise */
            cptr = get_glyph (cptr, bbuf, 0);           /*   parse the next token */

            if (strcmp (bbuf, "==") == 0)               /* if the operator is "equal to" */
                invert = FALSE;                         /*   then we want a true test */

            else if (strcmp (bbuf, "!=") == 0)          /* otherwise if the operator is "not equal to" */
                invert = TRUE;                          /*   then we want an inverted test */

            else {                                      /* otherwise */
                invert = (strcmp (bbuf, "NOT") == 0);   /*   a NOT operator inverts the result */

                if (invert)                             /* if it was NOT */
                    cptr = get_glyph (cptr, bbuf, 0);   /*   then get another token */

                if (strcmp (bbuf, "IN") == 0) {         /* if it is IN */
                    test = Inclusion;                   /*   then this is a membership test */
                    result = invert;                    /*     so set the initial matching condition */
                    }

                else                                    /* otherwise the operator is invalid */
                    return SCPE_ARG;                    /*   so report it */
                }
            }
        }

    else {                                              /* otherwise it may be a unary operator */
        cptr = get_glyph (cptr, abuf, 0);               /*   so get the next token */

        invert = (strcmp (abuf, "NOT") == 0);           /* a NOT operator inverts the result */

        if (invert)                                     /* if it was NOT */
            cptr = get_glyph (cptr, abuf, 0);           /*   then get another token */

        if (strcmp (abuf, "EXIST") == 0)                /* if it is EXIST */
            test = Existence;                           /*   then this is a file existence check */
        else                                            /* otherwise */
            return SCPE_ARG;                            /*   the operator is unknown */
        }

    do {                                                /* loop for membership tests */
        if (*cptr != '\'' && *cptr != '"')              /* if a quoted string is not present */
            return SCPE_ARG;                            /*   then report a bad argument */

        cptr = parse_quoted_string (cptr, bbuf,                     /* get the second operand and upshift it */
                                    upshift && test != Existence);  /*   if requested and not a filename */

        if (cptr == NULL)                               /* if the operand isn't properly quoted */
            return SCPE_ARG;                            /*   then report a bad argument */

        else if (test == Inclusion) {                   /* otherwise if this is a membership test */
            if (invert)                                 /*   then */
                result &= (strcmp (abuf, bbuf) != 0);   /*     AND an exclusive check */
            else                                        /*   otherwise */
                result |= (strcmp (abuf, bbuf) == 0);   /*     OR an inclusive check */

            if (*cptr == ',')                           /* if the membership list continues */
                while (isspace (*++cptr));              /*   then discard the comma and any trailing spaces */
            else                                        /* otherwise */
                test = Comparison;                      /*   exit the membership loop */
            }

        else if (test == Existence)                         /* otherwise if this is an existence check */
            result = (stat (bbuf, &statbuf) == 0) ^ invert; /*   then test the filename */

        else                                                /* otherwise compare the operands */
            result = (strcmp (abuf, bbuf) == 0) ^ invert;   /*   with the appropriate test */
        }
    while (test == Inclusion);                          /* continue if additional members are present */

    switch (logical) {                                  /* apply the logical operator */
        case Assign:                                    /* for a null operator */
            condition = result;                         /*   use the condition directly */
            break;

        case And:                                       /* for a logical AND operator */
            condition = condition & result;             /*   AND the two results */
            break;

        case Or:                                        /* for a logical OR operator */
            condition = condition | result;             /*   OR the two results */
            break;
        }                                               /* all cases are handled */

    if (*cptr == '\0')                                  /* if the rest of the command is missing */
        return SCPE_2FARG;                              /*   then report it */

    else if (strncmp (cptr, "&&", 2) == 0) {            /* otherwise if an AND operator is present */
        logical = And;                                  /*   then record it */
        cptr++;                                         /*     and skip over it and continue */
        }

    else if (strncmp (cptr, "||", 2) == 0) {            /* otherwise if an OR operator is present */
        logical = Or;                                   /*   then record it */
        cptr++;                                         /*     and skip over it and continue */
        }

    else {                                              /* otherwise */
        not_done = FALSE;                               /*   this is the end of the condition */
        cptr--;                                         /*     so back up to point at the action string */
        }

    while (isspace (*++cptr));                          /* discard any trailing spaces */
    }
while (not_done);                                       /* continue to process logical comparisons until done */

if (condition)                                          /* if the comparison is true */
    sim_brk_act = strcpy (tempbuf, cptr);               /*   then copy the action string and execute the commands */

return SCPE_OK;                                         /* either way, the command succeeded */
}


/* Execute the DELETE command.

   This command processing routine adds a new DELETE command to delete the
   specified file.  The routine processes commands of the form:

     DELETE <filename>

   It provides a platform-independent way to delete files from a command file
   (e.g., temporary files created by a diagnostic program).
*/

static t_stat ex_delete_cmd (int32 flag, char *cptr)
{
if (*cptr == '\0')                                      /* if the filename is missing */
    return SCPE_2FARG;                                  /*   then report it */

else if (remove (cptr) == 0)                            /* otherwise if the delete succeeds */
    return SCPE_OK;                                     /*   then return success */

else                                                    /* otherwise */
    return SCPE_OPENERR;                                /*   report that the file could not be opened */
}


/* Execute a restricted command.

   This command processing routine is called when the user attempts to execute
   from the command line a command that is restricted to command files.
   Commands such as GOTO have no meaning when executed interactively, so we
   simply return "Command not allowed" status here.
*/

static t_stat ex_restricted_cmd (int32 flag, char *ptr)
{
return SCPE_NOFNC;                                      /* the command is not allowed interactively */
}


/* Execute the SET command.

   This command processing routine enhances the existing SET command to add
   setting environment variables and to extend console modes to include
   concurrent command execution and serial port support.  The routine processes
   commands of the form:

     SET ENVIRONMENT ...
     SET CONSOLE ...

   The other SET commands are handled by the standard handler.
*/

static CTAB ex_set_table [] = {                 /* the SET extension table */
    { "ENVIRONMENT", &ex_set_environment, 0 },  /*   SET ENVIRONMENT */
    { "CONSOLE",     &ex_set_console,     0 },  /*   SET CONSOLE */
    { NULL,          NULL,                0 }
    };

static t_stat ex_set_cmd (int32 flag, char *cptr)
{
char   *tptr, gbuf [CBUFSIZE];
CTAB   *cmdp;

tptr = get_glyph (cptr, gbuf, 0);                       /* get the SET target */

cmdp = find_ctab (ex_set_table, gbuf);                  /* find the associated command handler */

if (cmdp == NULL)                                       /* if the target is not one of ours */
    return set_handler (flag, cptr);                    /*   then let the base handler process it */
else                                                    /* otherwise */
    return cmdp->action (cmdp->arg, tptr);              /*   call our handler */
}


/* Execute the SHOW command.

   This command processing routine enhances the existing SHOW command to add
   pending string breakpoint and reply displays and to extend console modes to
   display the concurrent command execution mode.  The routine processes
   commands of the form:

     SHOW BREAK ...
     SHOW REPLY ...
     SHOW DELAYS
     SHOW CONSOLE ...

   The other SHOW commands are handled by the standard handler.
*/

static SHTAB ex_show_table [] = {               /* the SHOW extension table */
    { "BREAK",   &ex_show_break,   0 },         /*   SHOW BREAK */
    { "REPLY",   &ex_show_reply,   0 },         /*   SHOW REPLY */
    { "DELAYS",  &ex_show_delays,  0 },         /*   SHOW DELAYS */
    { "CONSOLE", &ex_show_console, 0 },         /*   SHOW CONSOLE */
    { NULL,      NULL,             0 }
    };

static t_stat ex_show_cmd (int32 flag, char *cptr)
{
char   *tptr, gbuf [CBUFSIZE];
SHTAB  *cmdp;
t_stat status;

cptr = get_sim_sw (cptr);                               /* get any command-line switches */

if (cptr == NULL)                                       /* if an invalid switch was present */
    return SCPE_INVSW;                                  /*   then report it */

else {                                                  /* otherwise */
    tptr = get_glyph (cptr, gbuf, 0);                   /*   get the SHOW target */

    cmdp = find_shtab (ex_show_table, gbuf);            /* find the associated command handler */

    if (cmdp == NULL)                                   /* if the target is not one of ours */
        return show_handler (flag, cptr);               /*   then let the base handler process it */

    else {                                              /* otherwise */
        status = cmdp->action (stdout, NULL, NULL,      /*   report the option on the console */
                               cmdp->arg, tptr);

        if (sim_log != NULL)                            /* if a console log is defined */
            cmdp->action (sim_log, NULL, NULL,          /*   then report again on the log file */
                          cmdp->arg, tptr);
        }

    return status;                                      /* return the command status */
    }
}


/* Execute the SET ENVIRONMENT command.

   This command processing routine adds a new SET ENVIRONMENT command to create,
   set, and clear variables in the host system's environment.  The routine
   processes commands of the form:

     SET ENVIRONMENT <name>=<value>
     SET ENVIRONMENT <name>=

   ...where <name> is the name of the variable, and value is the (unquoted)
   string value to be set.  If the value is missing, the variable is cleared.
   Legal names and values are host-system dependent.


   Implementation notes:

    1. MSVC does not offer the "setenv" function, so we use their "_putenv"
       function instead.  However, that function takes the full "name=value"
       string, rather than separate name and value parameters.

    2. We explicitly check for an equals sign before calling "get_glyph" because
       while that function will separate the name and value correctly at the
       equals sign, it will also allow the separation character to be a space.
       That would be confusing; for example, "SET ENV a b c" would set variable
       "a" to value "b c".

    3. While the 4.x version of the SET ENVIRONMENT processor calls "get_glyph",
       which forces all variable names to uppercase, the POSIX standard allows
       lowercase environment variable names and requires them to be distinct
       from uppercase names.  This can lead to unexpected behavior on POSIX
       systems.  Therefore, we call "get_glyph_nc" to preserve the case of the
       variable name.  Note that Windows treats environment variable names
       case-insensitively, so this change only affects POSIX systems.
*/

static t_stat ex_set_environment (int32 flag, char *cptr)
{
int  result;
char *bptr;

if (*cptr == '\0')                                      /* if no name is present */
    return SCPE_2FARG;                                  /*   then report a missing argument */

bptr = cptr + strlen (cptr);                            /* point at the end of the string */

while (isspace (*--bptr))                               /* if trailing spaces exist */
    *bptr = '\0';                                       /*   then remove them */

#if defined (_MSC_VER)

result = _putenv (cptr);                                /* enter the equate into the environment */

#else

if (cptr [strcspn (cptr, "= ")] != '=')                 /* if there's no equals sign */
    result = -1;                                        /*   then report a bad argument */

else {                                                  /* otherwise */
    char vbuf [CBUFSIZE];                               /*   declare a buffer to hold the variable name */

    bptr = get_glyph_nc (cptr, vbuf, '=');              /* split the variable name and value, preserving case */
    result = setenv (vbuf, bptr, 1);                    /*   and enter the equate into the environment */
    }

#endif

if (result == 0)                                        /* if the assignment succeeds */
    return SCPE_OK;                                     /*   then report success */
else                                                    /* otherwise */
    return SCPE_ARG;                                    /*   report a bad argument */
}


/* Execute the SET CONSOLE command.

   This command processing routine enhances the existing SET CONSOLE command to
   add configuration for concurrent command execution and serial port support.
   The routine processes commands of the form:

     SET CONSOLE CONCURRENT
     SET CONSOLE NOCONCURRENT
     SET CONSOLE SERIAL ...
     SET CONSOLE NOSERIAL

   It also intercepts the SET CONSOLE TELNET command to close an existing serial
   connection if a Telnet connection is being established.

   Because the SET CONSOLE command accepts a comma-separated set of options, we
   must parse each option and decide whether it is a standard option or an
   extension option.


   Implementation notes:

    1. We parse each option without case conversion and then separate the option
       name from its value, if present, with conversion to upper case.  This is
       necessary because the option name must be uppercase to match the command
       table, but the option value might be case-sensitive, such as a serial
       port name.

    2. The SET CONSOLE LOG/NOLOG and SET CONSOLE DEBUG/NODEBUG commands print
       messages to report logging initiation or completion.  When executing
       command files, we normally suppress messages by setting "sim_quiet" to 1.
       However, the console messages are useful even in a command-file setting,
       so we restore the saved setting before calling the SET processor.  This
       is redundant but causes no harm if a command file is not executing.
*/

static CTAB set_console_table [] = {            /* the SET CONSOLE extension table */
    { "CONCURRENT",   &ex_set_concurrent, 1 },  /*   SET CONSOLE CONCURRENT */
    { "NOCONCURRENT", &ex_set_concurrent, 0 },  /*   SET CONSOLE NOCONCURRENT */
    { "SERIAL",       &ex_set_serial,     1 },  /*   SET CONSOLE SERIAL */
    { "NOSERIAL",     &ex_set_serial,     0 },  /*   SET CONSOLE NOSERIAL */
    { "TELNET",       &ex_set_serial,     2 },  /*   SET CONSOLE TELNET */
    { NULL,           NULL,               0 }
    };

static t_stat ex_set_console (int32 flag, char *cptr)
{
char   *tptr, gbuf [CBUFSIZE], cbuf [CBUFSIZE];
CTAB   *cmdp;
t_stat status = SCPE_OK;

sim_quiet = ex_quiet;                                   /* restore the global quiet setting */

if (cptr == NULL || *cptr == '\0')                      /* if no options follow */
    return SCPE_2FARG;                                  /*   then report them as missing */

else while (*cptr != '\0') {                            /* otherwise loop through the argument list */
    cptr = get_glyph_nc (cptr, gbuf, ',');              /* get the next argument without altering case */
    tptr = get_glyph (gbuf, cbuf, '=');                 /*   and then just the option name in upper case */

    cmdp = find_ctab (set_console_table, cbuf);         /* get the associated command handler */

    if (cmdp == NULL)                                   /* if the target is not one of ours */
        status = sim_set_console (flag, gbuf);          /*   then let the base handler process it */
    else                                                /* otherwise */
        status = cmdp->action (cmdp->arg, tptr);        /*   call our handler */

    if (status != SCPE_OK)                              /* if the command failed */
        break;                                          /*   then bail out now */
    }

return status;                                          /* return the resulting status */
}


/* Execute the SET CONSOLE CONCURRENT/NOCONCURRENT commands.

   This command processing routine adds new SET CONSOLE [NO]CONCURRENT commands
   to enable or disable concurrent command mode.  The routine processes commands
   of the form:

     SET CONSOLE CONCURRENT
     SET CONSOLE NOCONCURRENT

   The mode is enabled if the "flag" parameter is 1 and disabled if it is 0.
*/

static t_stat ex_set_concurrent (int32 flag, char *cptr)
{
if (flag == 1)                                          /* if this is the CONCURRENT option */
    concurrent_mode = TRUE;                             /*   then enable concurrent mode */
else                                                    /* otherwise */
    concurrent_mode = FALSE;                            /*   disable concurrent mode */

return SCPE_OK;
}


/* Execute the SET CONSOLE SERIAL/NOSERIAL commands.

   This command processing routine adds new SET CONSOLE [NO]SERIAL commands to
   connect or disconnect the console to or from a serial port.  The routine
   processes commands of the form:

     SET CONSOLE SERIAL=<port>[;<configuration>]
     SET CONSOLE NOSERIAL

   On entry, the "flag" parameter is set to 1 to connect or 0 to disconnect.  If
   connecting, the "cptr" parameter points to the serial port name and optional
   configuration string.  If present, the configuration string must be separated
   from the port name with a semicolon and has this form:

      <rate>-<charsize><parity><stopbits>

   where:

     rate     = communication rate in bits per second
     charsize = character size in bits (5-8, including optional parity)
     parity   = parity designator (N/E/O/M/S for no/even/odd/mark/space parity)
     stopbits = number of stop bits (1, 1.5, or 2)

   As an example:

     SET CONSOLE SERIAL=com1;9600-8n1

   The supported rates, sizes, and parity options are host-specific.  If a
   configuration string is not supplied, then host system defaults for the
   specified port are used.

   This routine is also called for the SET CONSOLE TELNET command with the
   "flag" parameter set to 2.  In this case, we check for a serial connection
   and perform an automatic SET CONSOLE NOSERIAL first before calling the
   standard command handler to attach the Telnet port.
*/

static t_stat ex_set_serial (int32 flag, char *cptr)
{
t_stat status;

if (flag == 2) {                                        /* if this is a SET CONSOLE TELNET command */
    if (serial_line (sim_con_tmxr.ldsc) != NULL)        /*   then if a serial connection exists */
        ex_tmxr_detach_line (&sim_con_tmxr, NULL);      /*     then detach the serial port first */

    status = sim_set_telnet (flag, cptr);               /* call the base handler to set the Telnet connection */
    }

else if (flag == 1) {                                   /* otherwise if this is a SET CONSOLE SERIAL command */
    sim_set_notelnet (flag, NULL);                      /*   then detach any existing Telnet connection first */

    if (serial_line (sim_con_tmxr.ldsc) != NULL)        /* if already connected to a serial port */
        status = SCPE_ALATT;                            /*   then reject the command */

    else {                                              /* otherwise */
        status = ex_tmxr_attach_line (&sim_con_tmxr,    /*   try to attach the serial port */
                                      NULL, cptr);

        if (status == SCPE_OK) {                        /* if the attach succeeded */
            ex_tmxr_poll_conn (&sim_con_tmxr);          /*   then poll to complete the connection */
            sim_con_tmxr.ldsc [0].rcve = 1;             /*     and enable reception */
            }
        }
    }

else {                                                  /* otherwise this is a SET CONSOLE NOSERIAL command */
    status = ex_tmxr_detach_line (&sim_con_tmxr, NULL); /*   so detach the serial port */
    sim_con_tmxr.ldsc [0].rcve = 0;                     /*     and disable reception */
    }

return status;                                          /* return the command status */
}


/* Execute the SHOW BREAK command.

   This command processing routine enhances the existing SHOW BREAK command to
   display string breakpoints.  The routine processes commands of the form:

     SHOW { <types> } BREAK { ALL | <address-list> }

   Where:

     <types> = a dash and one or more letters denoting breakpoint types

   String breakpoints are displayed in this form:

     <unit>:  <types> <quoted-string> { DELAY <delay> } { ; <action> ... }

   ...and are displayed only if the <address-list> is omitted and the <types>
   are either omitted or matches the type of the string breakpoint. In practice,
   this means that string breakpoints could be displayed for these commands:

     SHOW BREAK { ALL }
     SHOW -T BREAK { ALL }
     SHOW -T <other-types> BREAK { ALL }

   Persistent string breakpoints are displayed only with the first form.  All
   three forms will display temporary string breakpoints.  But a SHOW -N BREAK
   command would not display any string breakpoints because none have an "N"
   type.

   The ALL keyword is redundant but is accepted for compatibility with the
   standard SHOW BREAK command.


   Implementation notes:

    1. The matching string is stored in the breakpoint structure in decoded
       form, i.e., control characters are stored explicitly rather than as a
       character escape.  So the string must be encoded for display.
*/

static t_stat ex_show_break (FILE *stream, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
char   gbuf [CBUFSIZE];
int32  types;
uint32 sw;
t_bool sep;
SBPTR  bp = sb_list;

if (sim_switches == 0)                                  /* if no type switches were specified */
    types = BP_STRING;                                  /*   then match any string breakpoint */
else                                                    /* otherwise */
    types = sim_switches;                               /*   match only the specified type(s) */

get_glyph (cptr, gbuf, 0);                              /* parse the first option, if any */

if (*cptr == '\0' || strcmp (gbuf, "ALL") == 0)         /* if no address list or ALL was specified */
    while (bp != NULL) {                                /*   then while string breakpoints exist */
        if (bp->type & types) {                         /*     then if breakpoint matches the type */
            fprintf (stream, "%s:\t",                   /*       then report the associated unit */
                             (bp->uptr ? sim_uname (bp->uptr) : "CONS"));

            sep = FALSE;                                /* no separator is needed to start */

            for (sw = 0; sw < 26; sw++)                 /* check the type letters */
                if (bp->type >> sw & 1) {               /* if this type is indicated */
                    if (sep)                            /*   then if a separator is needed */
                        fprintf (stream, ", ");         /*     then output it first */

                    fputc (sw + 'A', stream);           /* output the type letter */
                    sep = TRUE;                         /*   and indicate that a separator will be needed */
                    }

            if (bp->count > 0)                          /* if the count is defined */
                fprintf (stream, " [%d]", bp->count);   /*   then output it */

            fprintf (stream, "%s%s%s%.0d",              /* output the breakpoint */
                             (sep || bp->count > 0 ? " " : ""),
                             encode (bp->match),
                             (bp->delay ? " delay " : ""),
                             bp->delay);

            if (bp->action [0] != '\0')                 /* if actions are defined */
                fprintf (stream, " ; %s", bp->action);  /*   then output them */

            fprintf (stream, "\n");                     /* terminate the line */
            }

        bp = bp->next;                                  /* move on to the next breakpoint */
        }

return show_break (stream, dptr, uptr, flag, cptr);     /* let the base handler show any numeric breakpoints */
}


/* Execute the SHOW REPLY command.

   This command processing routine adds a new SHOW REPLY command to display
   pending replies.  The routine processes commands of the form:

     SHOW REPLY

   Replies are displayed in this form:

     <unit>:  <quoted-string> { DELAY <delay> }

   If a delay is present, then the value displayed is the remaining delay before
   the first character of the string is output.  If a delay was originally
   specified but is not displayed, then the reply is already underway.


   Implementation notes:

    1. The output string is stored in the reply structure in decoded form, i.e.,
       control characters are stored explicitly rather than as a character
       escape.  So the string must be encoded for display.
*/

static t_stat ex_show_reply (FILE *stream, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
int32 delay;
RPPTR rp = rp_list;

if (*cptr != '\0')                                      /* if something follows */
    return SCPE_2MARG;                                  /*   then report extraneous characters */

else if (rp == NULL)                                    /* otherwise if no replies are pending */
    fprintf (stream, "No replies pending\n");           /*   then report it as such */

else {                                                  /* otherwise report the replies */
    delay = rp->trigger - sim_gtime ();                 /* get the relative delay time */

    if (delay < 0)                                      /* if the reply has already started */
        delay = 0;                                      /*   then suppress reporting the delay */

    fprintf (stream, "%s:\t%s%s%.0d\n",                 /* display the reply */
            (rp->uptr ? sim_uname (rp->uptr) : "CONS"),
            encode (rp->reply),
            (delay ? " delay " : ""), delay);
    }

return SCPE_OK;                                         /* report the success of the command */
}


/* Execute the SHOW DELAYS command.

   This command processing routine adds a new SHOW DELAYS command to display the
   global delay settings for string breakpoints and replies.  The routine
   processes commands of the form:

     SHOW DELAYS

   The delay values are reported in units of event ticks.
*/

static t_stat ex_show_delays (FILE *stream, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
if (*cptr != '\0')                                      /* if something follows */
    return SCPE_2MARG;                                  /*   then report extraneous characters */

else {                                                      /* otherwise */
    fprintf (stream, "Break delay = %d\n", break_delay);    /*   report the break */
    fprintf (stream, "Reply delay = %d\n", reply_delay);    /*     and reply delays */

    return SCPE_OK;                                     /* return success */
    }
}


/* Execute the SHOW CONSOLE command.

   This command processing routine enhances the existing SHOW CONSOLE command to
   add configuration displays for concurrent command execution and serial port
   support.  The routine processes commands of the form:

     SHOW CONSOLE CONCURRENT
     SHOW CONSOLE SERIAL

   It also intercepts the SHOW CONSOLE TELNET command to convert it from a
   two-state report (i.e., connected to Telnet or console window) to a
   three-state report (Telnet, serial, or console window).

   Because the SHOW CONSOLE command accepts a comma-separated set of options, we
   must parse each option and decide whether it is a standard option or an
   extension option.


   Implementation notes:

    1. We parse each option without case conversion and then separate the option
       name from its value, if present, with conversion to upper case.  This is
       necessary because the option name must be uppercase to match the command
       table, but the option value might be case-sensitive, such as a serial
       port name.

    2. For the SHOW CONSOLE command with no specified options (i.e., SHOW ALL),
       we cannot simply call the standard "sim_show_console" routine to display
       the base set of values because it calls "sim_show_telnet", which displays
       "Connected to console window" if no Telnet connection exists.  This is
       incorrect if a serial connection exists.  Instead, we call that routine
       with a command line consisting of all options except the TELNET option.
       In that way, we get the base display and can then add our own line for
       the Telnet/serial/window connection.

       If the base set of options is changed, we must also change the "show_set"
       value below to match.

    3. For SHOW CONSOLE (i.e., SHOW ALL), we loop through the extension table to
       call the individual SHOW executors.  However, we don't want to call
       "ex_show_serial" twice, or we'll get the connection report twice, so we
       use the otherwise-unused "arg" values as a flag to skip the call, which
       we do after processing the table.
*/

#define SH_SER              -2
#define SH_TEL              -1
#define SH_NONE              0

static SHTAB show_console_table [] = {              /* the SHOW CONSOLE extension table */
    { "CONCURRENT", &ex_show_concurrent,   0    },  /*   SHOW CONSOLE CONCURRENT */
    { "SERIAL",     &ex_show_serial,     SH_SER },  /*   SHOW CONSOLE SERIAL */
    { "TELNET",     &ex_show_serial,     SH_TEL },  /*   SHOW CONSOLE TELNET */
    { NULL,         NULL,                  0    }
    };

static char show_set [] = "WRU,BRK,DEL,PCHAR,LOG,DEBUG";    /* the standard set of options */

static t_stat ex_show_console (FILE *stream, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
char   *tptr, gbuf [CBUFSIZE], cbuf [CBUFSIZE];
SHTAB  *cmdp;
t_stat status = SCPE_OK;

if (*cptr == '\0') {                                        /* if no options follow */
    sim_show_console (stream, NULL, NULL, flag, show_set);  /*   then show the base console options first */

    for (cmdp = show_console_table; cmdp->name != NULL; cmdp++) /* loop through the extension options */
        if (cmdp->arg >= 0)                                     /*   and if it's not to be omitted */
            cmdp->action (stream, NULL, NULL, cmdp->arg, "");   /*     then show the option */

    ex_show_serial (stream, NULL, NULL, SH_NONE, "");       /* add the console connection status */
    }

else do {                                               /* otherwise loop through the option list */
    cptr = get_glyph_nc (cptr, gbuf, ',');              /* get the next option and value without altering case */
    tptr = get_glyph (gbuf, cbuf, '=');                 /*   and then just the option name in upper case */

    cmdp = find_shtab (show_console_table, cbuf);       /* get the associated command handler */

    if (cmdp == NULL)                                                   /* if the target is not one of ours */
        status = sim_show_console (stream, NULL, NULL, flag, gbuf);     /*   then let the base handler process it */
    else                                                                /* otherwise */
        status = cmdp->action (stream, NULL, NULL, cmdp->arg, tptr);    /*   call our handler */

    if (status != SCPE_OK)                              /* if the command failed */
        break;                                          /*   then bail out now */
    }
while (*cptr != '\0');                                  /* continue until all options are processed */

return status;                                          /* return the resulting status */
}


/* Execute the SHOW CONSOLE CONCURRENT command.

   This command processing routine adds a new SHOW CONSOLE CONCURRENT command
   to display the concurrent command mode.  The routine processes commands
   of the form:

     SHOW CONSOLE CONCURRENT

   The global mode value is reported.
*/

static t_stat ex_show_concurrent (FILE *stream, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
if (*cptr != '\0')                                      /* if something follows */
    return SCPE_2MARG;                                  /*   then report extraneous characters */

else {                                                  /* otherwise */
    if (concurrent_mode)                                /*   if concurrent mode is enabled */
        fprintf (stream, "Concurrent mode enabled\n");  /*     then report it as such */
    else                                                /*   otherwise */
        fprintf (stream, "Concurrent mode disabled\n"); /*     report that it is disabled */

    return SCPE_OK;                                     /* return command success */
    }
}


/* Execute the SHOW CONSOLE SERIAL/TELNET command.

   This command processing routine adds a new SHOW CONSOLE SERIAL command and
   enhances the existing SHOW CONSOLE TELNET command to display the connection
   status of the system console.  The routine processes commands of the form:

     SHOW CONSOLE SERIAL
     SHOW CONSOLE TELNET

   It is also called as part of the SHOW CONSOLE processing.  The three calls
   are differentiated by the value of the "flag" parameter, as follows:

     Flag     Meaning
     -------  -----------
     SH_SER   SHOW SERIAL
     SH_TEL   SHOW TELNET
     SH_NONE  SHOW

   For the SHOW SERIAL and SHOW TELNET commands, if the console is connected to
   the specified type of port, the port status (connection and transmit/receive
   counts) is printed.  Otherwise, the command simply reports the connection
   type.  So, for example, a SHOW SERIAL command reports "Connected to Telnet
   port" or "Connected to console window" if the console is not using a serial
   port.

   The plain SHOW command reports the port status of the connection in use.


   Implementation notes:

    1. We must duplicate the code from the "sim_show_telnet" routine in
       "sim_console.c" here because that routine reports a console window
       connection if Telnet is not used, and there is no way to suppress that
       report when a serial port is used.
*/

static t_stat ex_show_serial (FILE *stream, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
if (*cptr != '\0')                                      /* if something follows */
    return SCPE_2MARG;                                  /*   then report extraneous characters */

else {                                                      /* otherwise */
    if (serial_line (sim_con_tmxr.ldsc) != NULL)            /*   if a serial connection exists */
        if (flag == SH_TEL)                                 /*     but we've asked for Telnet status explicitly */
            fputs ("Connected to serial port\n", stream);   /*       then report the other connection type */

        else {                                              /*     otherwise */
            fprintf (stream, "Connected to ");              /*       report */
            tmxr_fconns (stream, sim_con_tmxr.ldsc, -1);    /*         the connection port and time */
            tmxr_fstats (stream, sim_con_tmxr.ldsc, -1);    /*           and the transmit and receive counts */
            }

    else if (sim_con_tmxr.master != 0)                      /*   otherwise if a Telnet connection exists */
        if (flag == SH_SER)                                 /*     but we've asked for serial status explicitly */
            fputs ("Connected to Telnet port\n", stream);   /*       then report the other connection type */

        else if (sim_con_tmxr.ldsc [0].conn == 0)           /*     otherwise if the client hasn't connected yet */
            fprintf (stream, "Listening on port %d\n",      /*       then report that we're still listening */
                     sim_con_tmxr.port);

        else {                                              /*     otherwise report the socket connection */
            fprintf (stream, "Listening on port %d, connected to socket %d\n",
                     sim_con_tmxr.port, sim_con_tmxr.ldsc [0].conn);

            tmxr_fconns (stream, sim_con_tmxr.ldsc, -1);    /* report the connection address and time */
            tmxr_fstats (stream, sim_con_tmxr.ldsc, -1);    /*   and the transmit and receive counts */
            }

    else                                                    /*     otherwise no port connection exists */
        fprintf (stream, "Connected to console window\n");  /*       so report the default association */

    return SCPE_OK;                                     /* return command success */
    }
}


/* Hooked command extension replacement routines */


/* Substitute arguments into a command line.

   This hook routine extends the standard "sub_args" routine to perform token
   substitution as well as parameter substitution.  The standard behavior
   substitutes parameter placeholders of the form "%n", where "n" is a digit
   from 0-9, with the strings pointed to by the corresponding elements in the
   supplied "args" array.

   In addition to the parameter substitution, built-in and environment tokens
   surrounded by percent signs are replaced.  These substitutions provide the
   value of environment variables, as well as the current date and time in
   various formats that may be used with the REPLY facility to set the target OS
   time on bootup.  They also provide some internal simulator values, such as
   the path to the binary and the current code version, that may be used in
   command files.

   The routine interprets these character sequences as follows:

     %0 - %9   =  parameter substitution from the supplied "args" array

     %token%   =  substitute a value corresponding to the predefined token

     %envvar%  =  substitute the value of an environment variable

     %%        =  substitute a literal percent sign

   Tokens consist of characters without intervening blanks.  For example,
   "%TOKEN%" is a token, but "100% PURE" is not (however, this should be
   represented as "100%% PURE" to designate the literal percent sign
   explicitly).  If a token or environment variable is not defined, it will be
   replaced with a null value.

   On entry, "iptr" points at the buffer containing the command line to scan for
   substitutions, "optr" points at a temporary output buffer of the same size
   as the input buffer, "bufsize" is the size of the buffers, and "args" points
   at an array of parameter substitution values.  On return, the "iptr" buffer
   contains the substituted line.  If substitution overflows the output buffer,
   the result will be truncated at "bufsize" characters with no error
   indication.

   This routine is also called for commands entered at the simulation console in
   addition to those appearing in command files.  The numeric arguments in the
   former case represent the parameters provided at SIMH invocation.


   Implementation notes:

    1. A literal percent sign is designated by a pair of percent signs (i.e.,
       "%%").  This change from the earlier practice of using a backslash as the
       escape character is for compatibility with version 4.x.

    2. The C "str..." functions offer no performance advantage over inline code
       and are used for clarity only.

    3. If supplied, the "args" array must contain 10 elements.  If "args" is
       NULL, then parameter substitution will not occur.
*/

static void ex_substitute_args (char *iptr, char *optr, int32 bufsize, char *args [])
{
char  *start, *end, *in, *out, *env;
int32 i;

start = strchr (iptr, '%');                             /* see if any % characters are present */

if (start == NULL)                                      /* if not (the usual case) */
    return;                                             /*   then we are done */

in = iptr;                                              /* at least one % is present */
out = optr;                                             /*   so set up the input and output buffer pointers */

bufsize = bufsize - 1;                                  /* ensure there is space for the trailing NUL */

do {                                                    /* copy string segments to a temporary buffer */
    if (start - in != 0)                                /* if the leading substring is not empty */
        copy_string (&out, &bufsize, in, start - in);   /*   then copy up to the next % */

    if (isdigit (start [1])) {                          /* if the next character is a digit */
        i = (int32) (start [1] - '0');                  /*   then convert to an index */

        if (i < 10 && args != NULL && args [i] != NULL) /* if the corresponding argument is present */
            copy_string (&out, &bufsize, args [i], 0);  /*   then copy the value */

        in = start + 2;                                 /* skip over the argument substitution */
        }

    else {                                              /* otherwise it might be a keyword substitution */
        end = strpbrk (start + 1, "% ");                /* search for an embedded blank before the next % */

        if (end == NULL) {                              /* if there is no ending % */
            in = start;                                 /*   then copy the remainder of the line */
            break;                                      /*     to the output buffer */
            }

        else if (bufsize > 0)                           /* otherwise we'll need at least one more character */
            if (*end != '%') {                          /* if there is an intervening blank */
                *out++ = *start;                        /*   then the initial % does not start a token */
                *out = '\0';                            /*     so copy the character */

                in = start + 1;                         /* advance over it */
                bufsize = bufsize - 1;                  /*   and count it */
                }

            else if (end == start + 1) {                /* otherwise if the % signs are adjacent */
                *out++ = '%';                           /*   then output a literal % */
                *out = '\0';                            /*     and terminate the string */

                in = start + 2;                         /* advance over it */
                bufsize = bufsize - 1;                  /*   and count it */
                }

            else {                                      /* otherwise a substitution token may be present */
                in = end + 1;                           /*   so skip over it */

                *end = '\0';                            /* temporarily terminate the string at the second % */

                env = getenv (start + 1);               /* search the environment for a match */

                *end = '%';                             /* restore the % before testing */

                if (env)                                    /* if the keyword is an environment variable */
                    copy_string (&out, &bufsize, env, 0);   /*   then copy the value */
                else                                        /* otherwise */
                    replace_token (&out, &bufsize, start);  /*   replace the token if it's defined */
                }
        }

    start = strchr (in, '%');                           /* search for the next initial % */
    }                                                   /*   and continue to copy and substitute */
while (start != NULL && bufsize > 0);                   /*     until there are no more tokens */

if (bufsize > 0)                                        /* if any space remains */
    copy_string (&out, &bufsize, in, 0);                /*   then copy any remaining input to the output buffer */

strcpy (iptr, optr);                                    /* replace the original buffer */
return;                                                 /*   and return */
}


/* Get a specified radix from the command-line switches or keyword.

   This hook routine extends the standard "sim_get_radix" routine to permit a
   request for binary interpretation of numeric data.  The standard behavior
   permits commands such as EXAMINE to override the default radix of the data
   item by specifying one of the following switches on the command line:

     Switch  Interpretation
     ------  -------------------
       -O    An octal value
       -D    A decimal value
       -H    A hexadecimal value

    ...and to set the default radix for a device with these commands:

      Command            Interpretation
      -----------------  -------------------
      SET <dev> OCTAL    An octal value
      SET <dev> DECIMAL  A decimal value
      SET <dev> HEX      A hexadecimal value

    This routine extends the interpretation to add -B and SET <dev> BINARY for
    binary value interpretations.

    On entry, if the "cptr" parameter is non-null, then it points at the keyword
    for a SET <dev> command.  If the keyword is "BINARY", the returned radix is
    2.  Otherwise, it is 0 to indicate that the keyword was not understood by
    this routine.

    Otherwise, the "cptr" parameter is NULL, and the "switches" parameter is
    checked for the presence of the O, D, H, or B flags.  If one is present, the
    corresponding radix is returned.  Otherwise, the "default_radix" parameter
    value is returned.


    Implementation notes:

     1. The order of switch interpretation matches that of the standard SCP
        routine, so that the same precedence is used when multiple conflicting
        switches are present.

     2. This must be implemented as an extension, as several simulators already
        use -B for their own purposes (e.g., for the PDP11, it indicates a byte
        access).
*/

static int32 ex_get_radix (const char *cptr, int32 switches, int32 default_radix)
{
if (cptr != NULL)
    if (strncmp (cptr, "BINARY", strlen (cptr)) == 0)   /* if the keyword is "BINARY" */
        return 2;                                       /*   then return radix 2 */
    else                                                /* otherwise */
        return 0;                                       /*   indicate that the keyword is not recognized */

else if (switches & SWMASK ('O'))                       /* otherwise if the -O switch was specified */
    return 8;                                           /*   then interpret data as octal */

else if (switches & SWMASK ('D'))                       /* otherwise if the -D switch was specified */
    return 10;                                          /*   then interpret data as decimal */

else if (switches & SWMASK ('H'))                       /* otherwise if the -H switch was specified */
    return 16;                                          /*   then interpret data as hex */

else if (sim_switches & SWMASK ('B'))                   /* otherwise if the -B switch was specified */
    return 2;                                           /*   then interpret data as binary */

else                                                    /* otherwise */
    return default_radix;                               /*   use the default radix for the data item */
}


/* Local SCP command extension routines */


/* Execute commands in a text file.

   This routine is called to execute the SCP commands present in a text file.
   It is called by the CALL and DO command executors, where it processes
   commands of the form:

     DO { <switches> } <filename> { <param 1> { <param 2> { ... <param 9> } } }

   The <filename> must be present.  Up to nine optional parameters may be
   specified, which may be referenced within the command file with the
   substitutions "%1" through "%9", respectively.  If a parameter is omitted,
   the corresponding substitution will be the empty string.  Parameters are
   normally delimited by spaces.  However, spaces may be embedded in a parameter
   if it is quoted, either with single (') or double (") quote marks.  The
   starting and ending quotes must match, or an "Invalid argument" error is
   indicated.

   Three optional switches are recognized:

     -E = continue execution in the presence of errors
     -V = echo each command line as it is executed
     -A = echo all simulation stop and ATTACH/DETACH messages

   The -E switch causes command file execution to continue regardless of any
   error conditions.  If -E is present, only EXIT or an ASSERT failure will stop
   execution.  Without -E, execution stops if a command returns an SCP error.

   Specifying the -V switch will print each command line before it is executed.
   The filename containing the command is printed as a prompt, e.g.:

     cmdfile> echo Hello!

   If -V is absent, then the command line is printed only if the command fails.
   Simulation stops, including the expiration of a STEP command, are not
   considered errors and so do not cause the associated commands to be printed.

   The -A switch causes all simulation stops and messages from the ATTACH and
   DETACH commands to be reported.  In its absence, the "Breakpoint" and "Step
   expired" messages that would normally be printed in response to the
   associated stop condition are suppressed, as are the informational messages
   from ATTACH and DETACH, such as "Creating new file."  This provides a cleaner
   console display log when automated prompts and responses are used.  As an
   example, if "cmdfile" contains:

     GO UNTIL "Memory size? " ; ATTACH -N MS0 new.tape ; REPLY "1024\r" ; GO

   ...then "DO cmdfile" would display:

     Memory size? 1024

   ...whereas "DO -A cmdfile" would display:

     Memory size?
     Breakpoint, P: 37305 (CLF 10)
     MS: creating new file
     1024

   All three switches propagate from top-level command files to nested files.
   For example, invoking a top-level command file with "DO -V" will verbosely
   list not only that file's commands but also the commands within any DO files
   invoked therein.

   On entry, the "cptr" parameter points at the invocation string after the DO
   or CALL keyword (i.e., points at the optional switches or command filename).
   The "file" parameter is NULL if the routine is to open the filename present
   at the start of the "cptr" string or is a file stream pointer to an open
   file.  The "flag" parameter indicates the source of the call and nesting
   level and contains one of these values:

     < 0 = global or local initialization file (no error if not found)
       0 = startup command line file
       1 = "DO" command
     > 1 = nested DO or CALL command

   For a nested command call, the parameter contains the nesting level in bits
   0-3 and the value of the invoking switches in bits 4-29.

   The routine begins by separating the nesting level from the propagated
   switches.  If the nesting level is too deep, the routine returns with an
   error.

   It then obtains any switches specified on the command line and merges them
   into the propagated switches.  Then it separates the command line parameters,
   removing any leading or trailing spaces (unless the parameter is quoted).  If
   the "file" parameter is NULL, it then attempts to open the specified command
   file.  If the open fails, and this is not an initialization file request,
   then the attempt is repeated after appending the ".sim" extension to the
   specified name.  If this second attempt also fails, the command fails with a
   "File open error" message.

   The routine then enters the command loop.  The next command line is obtained,
   either from a pending breakpoint action or from the command file, and
   argument substitutions are made.  If the end of the file was reached, the
   loop exits with success status.  Otherwise, if the resulting line is empty
   (e.g., is a blank or comment line), the loop continues with the next command
   line.

   If the line contains a command, it is echoed if the -V switch was present.
   If it's a label line, it is ignored, and the loop continues.  Otherwise, the
   global switches are cleared, and the "sim_quiet" value is set if neither the
   -A (audible) nor -V (verbose) switch was specified.  This suppresses certain
   noise messages unless they were specifically requested by the invocation.
   Then the command keyword is parsed, and the action routine is obtained.

   If it is a DO command, the DO executor is called with the nesting level
   increased by one.  On reentry here, the nesting level is checked, and an
   error occurs if the limit is exceeded.

   If the command is RUN (or any of the execution commands), and neither the
   -A nor -V switches was specified, the SIM_SW_HIDE switch will be passed to
   the RUN executor to suppress BREAK and STEP messages.  Then the associated
   command handler is invoked, and its status result is obtained.

   If the command is one of those restricted to use within a command file (CALL,
   RETURN, GOTO, or ABORT), the appropriate action is taken.  Otherwise, if the
   command succeeds, the command loop continues.  If the command fails, actions
   then depend on the specific error code and switch settings.  Three
   determinations are made: whether command file execution continues, whether
   the command line is printed, and whether the error message text is printed.

   Execution stays in the command loop if the error is not due to an EXIT or
   ABORT command or an ASSERT failure -- these always terminate DO execution --
   and the code is for a simulation stop (all VM-defined status codes and the
   STEP expired code) or the -E switch was specified.  That is, the loop
   terminates for an EXIT, ABORT, ASSERT that fails, or an SCP error and the -E
   switch was not given.

   The failing command line is printed if an SCP error occurred, the -V switch
   was not given, and the command was not DO unless it failed because the file
   could not be opened.  This excludes simulator stops, which are not considered
   errors in a command file.  it also avoids printing the line a second time if
   it was previously printed by the -V switch.  Finally, because the status of a
   failing command is passed back as the status of the enclosing DO or CALL
   command to ensure that execution stops for a failure in a nested invocation,
   the command line is not printed unless the DO command itself failed.  This
   determination is made independently of the determination to stay, because we
   want command lines causing errors to be printed, regardless of whether or not
   we will ignore the error.

   The error text is printed if it is an SCP error and either the command file
   is nested or the error will be ignored.  Simulation stops have already been
   printed by the "fprint_stopped" call in the RUN handler, and if the DO
   command was initiated from the command line and not by a nested DO or CALL
   invocation, and the execution loop will be exited, then the main command loop
   will print the error text when this routine returns.

   Finally, we call the VM's post-command handler if it is defined and check for
   a CTRL+C stop.  If it is detected, a message is printed, any pending
   breakpoint actions are cleared, and the status return is set to propagate the
   abort back through all nesting levels.

   Once the command loop exits, either for an error, or because the command file
   is exhausted, the file is closed if we had opened it, and the last command
   status is returned as the status of the invoking DO or CALL.  If we will be
   exiting the simulator, the end-of-session detach for all units is performed
   before returning, so that the same quietness as was used for the attaches is
   used.


   Implementation notes:

    1. The nesting limit is arbitrary and is intended mainly to prevent a stack
       overflow abort if a command file executes a recursive DO in an infinite
       loop.  Note that CALL invocations count toward the nesting limit.  Note
       also that four bits of the "flag" parameter are allocated to the nesting
       level, so the limit must be less than 15.

    2. The command line switches are not extracted here when processing the
       initialization or command-line files.  This is because the switches were
       already processed and stripped in the "main" routine before we were
       called.

    3. The standard ATTACH and DETACH commands are too noisy for command-file
       execution.  Specifically, ATTACH reports "unit is read only" for files
       without write permission, "creating new file" for newly created
       files, and "buffering file in memory" when the unit has the UNIT_BUFABLE
       flag.  DETACH reports "writing buffer to file" for a unit with
       UNIT_BUFABLE.  All of the messages report conditions over which the user
       of an executing command file has no control.

    4. The "do_arg" array is an array of string pointers, rather than an array
       of strings, that point into the command line buffer that is passed to
       this routine.  That buffer must not be constant, as we separate the
       parameters by writing a NUL after each parsed parameter.

    5. The command file is opened as a binary, rather than text, file on Windows
       systems.  This is because "fgetpos" and "fsetpos" (used in the CALL
       executor) fail to work properly on text files.  Opening as binary means
       that lines obtained with "fgets" have trailing CRLFs instead of LFs, but
       the "read_line" routine strips both line termination characters, so the
       result is as expected.

    6. The HP simulators look for SIM_SW_HIDE in "sim_switches" and return
       SCPE_OK status in lieu of the SCPE_STEP or STOP_BRKPNT codes.  In turn,
       this suppresses the "Step expired" and "Breakpoint" simulation stop
       messages that otherwise would be interspersed with prompts and responses
       supplied by the GO UNTIL and REPLY commands.

    7. Execution commands are identified by the "help_base" field of their
       corresponding CTAB entries being non-null.  This is a hack; see the
       comments for the "ex_cmds" structure declaration for details.

    8. A failing command within a command file returns an error status that
       causes the bad command to be echoed to identify the cause of the error.
       That status is also returned as the command file execution status, which
       would normally cause the invoking DO or CALL command to be echoed as
       well.  This is undesirable, as the command file name has already been
       printed as part of the echo, so this is suppressed.

       An exception is made for the SCPE_OPENERR status returned when the DO
       command file cannot be opened; otherwise, there would be no indication
       what caused the error message.  However, when this status is passed back
       to nested invocations, the exception would cause each invoking DO command
       to be listed, which would be wrong (the outer DOs did not fail with file
       open errors).

       To fix this, we tag the innermost nested return only with SCPE_DOFAILED,
       and use this to implement the exception.  This suppresses the propagating
       errors, so that only the DO command that actually encountered the file
       open error is echoed.

    9. The CALL command currently does not take switches (they aren't parsed
       from the command line string), so the called context uses the same switch
       environment as the caller.
*/

#define ARG_COUNT           10                          /* number of DO command arguments */
#define NEST_LIMIT          10                          /* DO command nesting limit (must be <= 15) */

#define LEVEL_SHIFT         4                           /* bits allocated to the level value */
#define LEVEL_MASK          ((1u << LEVEL_SHIFT) - 1)   /* mask for the level value */

static t_stat execute_file (FILE *file, int32 flag, char *cptr)
{
const  t_bool interactive = (flag > 0);         /* TRUE if DO was issued interactively */
static t_bool must_detach = TRUE;               /* TRUE until "detach_all" has been called during exit */
FILE   *do_file;
CTAB   *cmdp;
char   term, *kptr, *do_arg [ARG_COUNT], cbuf [CBUFSIZE], kbuf [CBUFSIZE];
int32  level, switches, audible, errignore, verbose, count;
t_bool is_do;
t_bool staying = TRUE;
t_stat status = SCPE_OK;

if (interactive) {                                      /* if we were originally called from the SCP prompt */
    level    = flag & LEVEL_MASK;                       /*   then isolate the nesting level */
    switches = flag >> LEVEL_SHIFT;                     /*     and propagated switches from the parameter */

    if (level >= NEST_LIMIT)                            /* if the call nesting level is too deep */
        return SCPE_NEST;                               /*   then report the error */

    cptr = get_sim_sw (cptr);                           /* get any command line switches */

    if (cptr == NULL)                                   /* if an invalid switch was present */
        return SCPE_INVSW;                              /*   then report it */
    }

else {                                                  /* otherwise this is an initialization file call */
    level    = 1;                                       /*   so start out at level 1 */
    switches = 0;                                       /*     with no propagated switches */
    }

switches |= sim_switches;                               /* merge specified and propagated switches */

audible   = switches & SWMASK ('A');                    /* set the audible flag if -A is present */
errignore = switches & SWMASK ('E');                    /*   and the error ignore flag if -E is present */
verbose   = switches & SWMASK ('V');                    /*     and the verbose flag if -V is present */

for (count = 0; count < ARG_COUNT; count++) {           /* parse the arguments */
    if (cptr == NULL || *cptr == '\0')                  /* if the argument list is exhausted */
        do_arg [count] = NULL;                          /*   then invalidate the remaining pointers */

    else {                                              /* otherwise */
        if (*cptr == '\'' || *cptr == '"')              /*   if a quoted string is present */
            term = *cptr++;                             /*     then save the terminator */
        else                                            /*   otherwise */
            term = ' ';                                 /*     use a space as the terminator */

        do_arg [count] = cptr;                          /* point at the start of the parameter */

        cptr = strchr (cptr, term);                     /* find the terminator */

        if (cptr != NULL) {                             /* if the parameter was terminated */
            *cptr++ = '\0';                             /*   then separate the parameters */

            while (isspace (*cptr))                     /* discard any trailing spaces */
                cptr++;                                 /*   that follow the parameter */
            }

        else if (term != ' ')                           /* otherwise if a quoted string is not terminated */
            return SCPE_ARG;                            /*   then report a bad argument */
        }
    }

if (do_arg [0] == NULL)                                 /* if the command filename is missing */
    return SCPE_2FARG;                                  /*   then report it */

else if (file != NULL)                                  /* otherwise if a stream was specified */
    do_file = file;                                     /*   then use it */

else {                                                  /* otherwise */
    do_file = fopen (do_arg [0], "rb");                 /*   open the specified command file */

    if (do_file == NULL) {                              /* if the file failed to open as is */
        if (flag < 0)                                   /*   then if this is an initialization file  */
            return SCPE_OPENERR;                        /*     then do not try an alternate filename */

        strcat (strcpy (cbuf, do_arg [0]), ".sim");     /* append a ".sim" extension to the filename */
        do_file = fopen (cbuf, "rb");                   /*   and try again */

        if (do_file == NULL) {                              /* if the open failed a second time */
            if (flag == 0)                                  /*   then if we're executing the startup file */
                fprintf (stderr, "Can't open file %s\n",    /*     then report the failure */
                         do_arg [0]);

            if (level > 1)                              /* if this is a nested DO call */
                return SCPE_OPENERR | SCPE_DOFAILED;    /*   then return failure with the internal flag */

            else                                        /* otherwise this is a top-level call */
                return SCPE_OPENERR;                    /*   so simply return failure */
            }
        }
    }

stop_requested = FALSE;                                 /* clear any pending WRU stop */

do {
    cptr = sim_brk_getact (cbuf, CBUFSIZE);             /* get any pending breakpoint actions */

    if (cptr == NULL)                                   /* if there are no pending actions */
         cptr = read_line (cbuf, CBUFSIZE, do_file);    /*   then get a line from the command file */

    ex_substitute_args (cbuf, kbuf, CBUFSIZE, do_arg);  /* substitute arguments in the command */

    if (cptr == NULL) {                                 /* if the end of the command file was reached */
        status = SCPE_OK;                               /*   then set normal status */
        break;                                          /*     and exit the command loop */
        }

    else if (*cptr == '\0')                             /* otherwise if the line is blank */
        continue;                                       /*   then ignore it */

    if (verbose) {                                      /* if command echo is specified */
        printf ("%s> %s\n", do_arg [0], cptr);          /*   then output the command filename and line */

        if (sim_log)                                    /* if the console is logging */
            fprintf (sim_log, "%s> %s\n",               /*   then write it to the log file as well */
                              do_arg [0], cptr);
        }

    if (*cptr == ':')                                   /* if this is a label line */
        continue;                                       /*   then ignore it */

    sim_switches = 0;                                   /* initialize the switches for the new command */
    sim_quiet = ! (audible | verbose);                  /*   and quiet the noise if not specifically requested */

    kptr = get_glyph (cptr, kbuf, 0);                   /* parse the command keyword */
    status = get_command (kbuf, &cmdp);                 /*   and get the associated descriptor */

    if (status == SCPE_OK) {                            /* if the command is valid */
        is_do = (cmdp->action == ex_do_handler);        /*   then set a flag if this is a DO command  */

        if (is_do)                                          /* if this is a DO command */
            status = cmdp->action (switches << LEVEL_SHIFT  /*   then execute the DO */
                                     | level + 1, kptr);    /*     and propagate the switches */

        else {                                          /* otherwise */
            if (cmdp->help_base != NULL && sim_quiet)   /*   if this is a quiet RUN (GO, etc.) command */
                sim_switches = SIM_SW_HIDE;             /*     then suppress BREAK and STEP stop messages */

            status = cmdp->action (cmdp->arg, kptr);    /* execute the command */

            if (cmdp->action == ex_restricted_cmd)          /* if this is a restricted command */
                if (cmdp->arg == EX_GOTO)                   /*   then if this is a GOTO command */
                    status = goto_label (do_file, kptr);    /*     then transfer control to the label */

                else if (cmdp->arg == EX_CALL)                  /* otherwise if this is a CALL command */
                    status = gosub_label (do_file, do_arg [0],  /*   then transfer control */
                                          switches << LEVEL_SHIFT
                                            | level + 1,
                                          kptr);

                else if (cmdp->arg == EX_RETURN) {      /* otherwise if this is a RETURN command */
                    status = SCPE_OK;                   /*   then clear the error status */
                    break;                              /*     and drop out of the loop */
                    }

                else if (cmdp->arg == EX_ABORT) {       /* otherwise if this is an ABORT command */
                    stop_requested = TRUE;              /*   then set the flag */
                    status = SCPE_ABORT;                /*     and abort status */
                    }
            }
        }

    else                                                /* otherwise the command is invalid */
        is_do = FALSE;                                  /*   so it can't be a DO command */

    staying = (status != SCPE_ABORT                     /* stay if not aborting */
                && status != SCPE_EXIT                  /*   and not exiting */
                && status != SCPE_AFAIL                 /*   and no assertion failed */
                && (errignore                           /*   and errors are ignored */
                  || status < SCPE_BASE                 /*     or a simulator stop occurred */
                  || status == SCPE_STEP));             /*       or a step expired */

    if (! staying)                                      /* if leaving due to an error */
        sim_brk_clract ();                              /*   then cancel any pending actions */

    if (status >= SCPE_BASE                             /* if an SCP error occurred */
      && status != SCPE_EXIT && status != SCPE_STEP) {  /*   other then EXIT or STEP */
        if (! verbose                                   /*     then if the line has not already been printed */
          && ! is_do || status & SCPE_DOFAILED) {       /*       and it's not a command status returned by DO */
            printf("%s> %s\n", do_arg [0], cptr);       /*         then print the offending command line */

            if (sim_log)                                /* if the console is logging */
                fprintf (sim_log, "%s> %s\n",           /*   then write it to the log file as well */
                                  do_arg [0], cptr);
            }

        if (is_do)                                      /* if it's a DO command that has failed */
            status &= ~SCPE_DOFAILED;                   /*   then remove the failure location flag */
        }

    if (status >= SCPE_BASE && status <= SCPE_LAST      /* if an SCP error occurred */
      && (staying || ! interactive)) {                  /*   and we're staying or were not invoked from the SCP prompt */
        printf ("%s\n", sim_error_text (status));       /*     then print the error message */

        if (sim_log)                                    /* if the console is logging */
            fprintf (sim_log, "%s\n",                   /*   then write it to the log file as well */
                              sim_error_text (status));
        }

    if (sim_vm_post != NULL)                            /* if the VM defined a post-processor */
        sim_vm_post (TRUE);                             /*   then call it, specifying SCP origin */

    if (stop_requested) {                               /* if a stop was detected via a signal */
        stop_requested = FALSE;                         /*   then clear the request */

        printf ("Command file execution aborted\n");

        if (sim_log)
            fprintf (sim_log, "Command file execution aborted\n");

        sim_brk_clract ();                              /* cancel any pending actions */

        staying = FALSE;                                /* end command file execution */
        status = SCPE_ABORT;                            /*   and set status to clear nested invocations */
        }
    }

while (staying);                                        /* continue execution until a terminating condition */

if (status == SCPE_EXIT && must_detach) {               /* if we are exiting the simulator and haven't detached */
    detach_all (0, TRUE);                               /*   then detach all units while still quiet */
    must_detach = FALSE;                                /*     and clear flag to avoid repeating if nested */
    }

sim_quiet = ex_quiet;                                   /* restore the original quiet setting */

if (file == NULL)                                       /* if we opened the command file */
    fclose (do_file);                                   /*   then close it before leaving */

return status;                                          /* return the termination status */
}


/* Execute the GOTO command.

   This routine is called to process a GOTO command within a command file.  The
   command form is:

     GOTO <label>

   Where:

     <label> is an identifier that appears in a label statement somewhere within
             the current command file.

   This routine transfers command file execution to the labeled statement
   specified by the "cptr" parameter by positioning the file specified by the
   "stream" parameter to the line following the label.  On entry, "cptr" points
   at the remainder of the GOTO command line, which should contain a transfer
   label.  If it is missing or is followed by additional characters, "Too few
   arguments" or "Too many arguments" errors are returned.  Otherwise, a search
   for the label is performed by rewinding the file stream and reading lines
   until either the label is found or the EOF is reached.  In the latter case,
   an "Invalid argument" error is returned.


   Implementation notes:

    1. A colon preceding the specified label is ignored.  Either "GOTO :label"
       or "GOTO label" is accepted.

    2. A simple linear search from the start of the file is performed.  Most
       labels will follow their GOTOs in the file, but optimizing the search
       into two parts (i.e., from the current point to the end, and then from
       the start to the current point) isn't worth the complexity.

    3. The search is case-sensitive.
*/

static t_stat goto_label (FILE *stream, char *cptr)
{
char cbuf [CBUFSIZE], label [CBUFSIZE], *lptr;

lptr = label;                                           /* save a pointer to the target label buffer */

cptr = get_glyph_nc (cptr, lptr, 0);                    /* parse the label from the remaining command line */

if (*cptr != '\0')                                      /* if there are extraneous characters following */
    return SCPE_2MARG;                                  /*   then return the "Too many parameters" error */

else if (*lptr == '\0')                                 /* otherwise if the label is missing */
    return SCPE_2FARG;                                  /*   then return the "Too few parameters" error */

else if (*lptr == ':')                                  /* otherwise if the label starts with a colon */
    lptr++;                                             /*   then skip over it to point at the identifier */

rewind (stream);                                        /* reposition the file to the start */

do {                                                    /* loop until the label or the EOF is found */
    cptr = read_line (cbuf, CBUFSIZE, stream);          /* read the next line from the file */

    if (cptr == NULL)                                   /* if the end of file was seen */
        return SCPE_ARG;                                /*   then report that the label was not found */

    else if (*cptr == ':') {                            /* otherwise if this is a label line */
        cptr++;                                         /*   then skip the leading colon */

        if (strcmp (cptr, lptr) == 0)                   /* if this is the target label */
            break;                                      /*   then terminate the search here */
        }
    }
while (TRUE);                                           /* otherwise continue to search */

return SCPE_OK;                                         /* return success as the label was found */
}


/* Execute the CALL command.

   This routine is called to process a CALL command within a command file.  The
   command form is:

     CALL <label> { <param 1> { <param 2> { ... <param 9> } } }

   Where:

     <label> is an identifier that appears in a label statement somewhere within
             the current command file.

   This routine saves the current position in the file specified by the "stream"
   parameter and then transfers command file execution to the labeled statement
   specified by the "cptr" parameter by positioning the file to the line
   following the label.  If positioning succeeds, the commands there are
   executed until a RETURN or ABORT command is executed, or the end of the
   command file is reached.  At that point, the original position is restored,
   and the command file continues to execute at the line after the original CALL
   command.

   On entry, "cptr" points at the remainder of the CALL command line, which
   should contain a transfer label.  The "filename" parameter points to the the
   name of the current command file, "level" is set to the current nesting
   level, and "switches" contain the set of switches that were used to invoke
   the current command file.  These three parameters are used to set up the same
   command file environment for subroutine execution.

   If the transfer label is missing, "Too few arguments" status is returned.
   Otherwise, the current stream position is saved, and the "goto_label" routine
   is called to position the stream to the start of the subroutine.  If that
   routine succeeds, the current filename is copied into a parameter buffer,
   followed by the remainder of the CALL command line parameters.  The switches
   are reset, and the "execute_file" routine is called to execute the
   subroutine.  On return, the file position is restored, and the execution
   status is returned.


   Implementation notes:

    1.  The "execute_file" routine parses the command line for switches and the
        command filename, which must be the "zeroth" parameter.  We must supply
        these so that the subroutine executes in the same environment as the
        containing command file, except that the parameters to the subroutine
        are set to those passed in the CALL command rather than from the
        original parameters to the command file invocation.

    2. The saved file position is the line after the CALL statement, which is
       where the called subroutine will return.
*/

static t_stat gosub_label (FILE *stream, char *filename, int32 flag, char *cptr)
{
char   label [CBUFSIZE];
t_stat status;
fpos_t current;

cptr = get_glyph_nc (cptr, label, 0);                   /* parse the label from the remaining command line */

if (label [0] == '\0')                                  /* if the label is missing */
    return SCPE_2FARG;                                  /*   then return the "Too few parameters" error */

else {                                                  /* otherwise */
    if (fgetpos (stream, &current) != 0) {              /*   save the current position; if that fails */
        perror ("Saving the file position failed");     /*     then report the error to the console */
        return SCPE_ABORT;                              /*       and abort command file execution */
        }

    status = goto_label (stream, label);                /* position the file to the subroutine label */

    if (status == SCPE_OK) {                            /* if positioning succeeded */
        strcpy (label, filename);                       /*   then form a parameter line */
        strcat (label, " ");                            /*     with the command filename */
        strcat (label, cptr);                           /*       preceding the CALL parameters */

        status = execute_file (stream, flag, label);    /* execute the subroutine */

        if (fsetpos (stream, &current) != 0) {              /* restore the file position; if that fails */
            perror ("Restoring the file position failed");  /*     then report the error to the console */
            return SCPE_ABORT;                              /*       and abort command file execution */
            }
        }

    return status;                                      /* return the command status */
    }
}


/* Replace a predefined token.

   This routine replaces a predefined token with its associated value.  Token
   references are character strings surrounded by percent signs ("%") and not
   containing blanks.  For example, "%token%" is a token reference, but "%not a
   token%" is not.

   On entry, "token_ptr" points at a valid token reference, "out_ptr" points at
   a pointer to the buffer where the associated value is to be placed, and
   "out_size" points at the size of the output buffer.  The token is obtained by
   stripping the percent signs and converting to upper case.  The keyword table
   is then searched for a match for the token.  If it is found, the associated
   substitution action directs the generation of the returned value.

   For date and time values, the current time (or a time rescaled to the 20th
   century) is formatted as directed by the keyword entry.  Other values are
   generally static and are copied to the output buffer.  After copying, the
   output pointer is advanced over the value, and the buffer size is decreased
   appropriately.


   Implementation notes:

    1. We get the current local time unconditionally, as most of the token
       substitutions requested will be for date or time values.

    2. The "token_ptr" cannot be "const" because the "get_glyph" routine takes a
       variable input parameter, even though it is not modified.

    3. If insufficient space remains in the output buffer, the "strftime"
       routine return 0 and leaves the buffer in an indeterminate state.  In
       this case, we store a NUL at the start of the buffer to ensure that the
       buffer is properly terminated.

    4. The SIM_MAJOR value is a constant and so cannot be the target of the
       "ptr" field.  Therefore, we must declare a variable that is set to this
       value and point at that.

    5. "sim_prog_name" is a pointer to the program name, rather than the program
       name string itself.  So the associated "ptr" field is a pointer to a
       pointer to the name.

    6. The "copy_string" routine updates its output pointer and size
       parameters, so we avoid updating them again when that routine is called.
*/

typedef enum {                                  /* substitution actions */
    Format_Value,                               /*   format an integer value */
    Format_Date,                                /*   format a date or time */
    Rescale_Date,                               /*   format a date rescaled to the 20th century */
    Copy_String,                                /*   copy a string value directly */
    } ACTION;

typedef struct {                                /* the keyword descriptor */
    const char  *token;                         /*   the token name */
    void        *ptr;                           /*   a pointer to the substitution value */
    const char  *format;                        /*   the substitution format */
    ACTION      substitution;                   /*   the substitution action */
    } KEYWORD;

static uint32 sim_major = SIM_MAJOR;                    /* the simulator major version number */
static char **sim_name_ptr = (char **) &sim_name;       /* a pointer to the simulator name */

static const KEYWORD keys [] = {
/*    Token           Pointer           Format  Substitution */
/*    --------------  ----------------  ------  ------------ */
    { "DATE_YYYY",    NULL,             "%Y",   Format_Date  }, /* four-digit year */
    { "DATE_YY",      NULL,             "%y",   Format_Date  }, /* two-digit year 00-99 */
    { "DATE_MM",      NULL,             "%m",   Format_Date  }, /* two-digit month 01-12 */
    { "DATE_MMM",     NULL,             "%b",   Format_Date  }, /* three-character month JAN-DEC */
    { "DATE_DD",      NULL,             "%d",   Format_Date  }, /* two-digit day of the month 01-31 */
    { "DATE_JJJ",     NULL,             "%j",   Format_Date  }, /* three-digit Julian day of the year 001-366 */
    { "DATE_RRRR",    NULL,             "%Y",   Rescale_Date }, /* four-digit year rescaled to the 20th century 1972-1999 */
    { "DATE_RR",      NULL,             "%y",   Rescale_Date }, /* two-digit year rescaled to the 20th century 72-99 */
    { "TIME_HH",      NULL,             "%H",   Format_Date  }, /* two-digit hour of the day 01-23 */
    { "TIME_MM",      NULL,             "%M",   Format_Date  }, /* two-digit minute of the hour 00-59 */
    { "TIME_SS",      NULL,             "%S",   Format_Date  }, /* two-digit second of the minute 00-59 */
    { "SIM_MAJOR",    &sim_major,       "%d",   Format_Value }, /* the major version number of the simulator */
    { "SIM_NAME",     &sim_name_ptr,    NULL,   Copy_String  }, /* the name of the simulator */
    { "SIM_EXEC",     &sim_prog_name,   NULL,   Copy_String  }, /* the simulator executable path and filename */
    { "SIM_RUNNING",  &concurrent_run,  "%d",   Format_Value }, /* non-zero if the simulator is running */
    { NULL,           0,                NULL,   0            }
    };

static void replace_token (char **out_ptr, int32 *out_size, char *token_ptr)
{
const KEYWORD *kptr;
char          tbuf [CBUFSIZE];
size_t        space;
time_t        time_value;
struct tm     *now;

get_glyph (token_ptr + 1, tbuf, '%');                   /* copy the token and convert it to upper case */

for (kptr = keys; kptr->token != NULL; kptr++)          /* search the keyword table for the token */
    if (strcmp (tbuf, kptr->token) == 0) {              /* if the current keyword matches */
        time_value = time (NULL);                       /*   then get the current UTC time */
        now = localtime (&time_value);                  /*     and convert to local time */

        switch (kptr->substitution) {                   /* dispatch for the substitution */

            case Rescale_Date:                          /* current date with year rescaled to 1972-1999 */
                while (now->tm_year >= 100)             /* reduce the current year */
                    now->tm_year = now->tm_year - 28;   /*   until it aligns with one in the 20th century */

            /* fall through into Format_Date */

            case Format_Date:                           /* current time/date */
                space = strftime (*out_ptr, *out_size,  /* format and store a time or date value */
                                  kptr->format, now);

                if (space == 0)                         /* if the value wasn't stored */
                    **out_ptr = '\0';                   /*   then be sure the output string is terminated */
                break;

            case Format_Value:                          /* integer value */
                space = snprintf (*out_ptr, *out_size,  /* format and store an integer value */
                                  kptr->format,
                                  *(int *) kptr->ptr);
                break;

            default:                                    /* needed to quiet warning about "space" being undefined */
            case Copy_String:                           /* string value */
                copy_string (out_ptr, out_size,         /* copy a string value */
                             *(char **) kptr->ptr, 0);
                space = 0;                              /* output pointer and size have already been adjusted */
                break;
            }                                           /* all cases are covered */

        *out_ptr = *out_ptr + space;                    /* adjust the output pointer */
        *out_size = *out_size - space;                  /*   and the size remaining for the copy */
        }

return;
}


/* Copy a string without overrun.

   This routine copies a string to a buffer while ensuring that the buffer does
   not overflow.  On entry, "source" points to the string to copy, "source_size"
   is zero if the entire string is to be copied or a positive value if only a
   leading substring is to be copied, "target" points at a pointer to the target
   buffer, and "target_size" points at the size of the buffer.

   If "source_size" is zero, the string length is obtained.  If the source
   length is greater than the space available in the target buffer, the length
   is reduced to match the space available.  Then the string is copied.  The
   target buffer pointer is updated, and a NUL is appended to terminate the
   string.  Finally, the buffer size is decreased by the size of the copied
   string.


   Implementation notes:

    1. This routine is needed because the standard "strncpy" function does not
       guarantee that a NUL is appended.  Also, if space is available, the
       remainder of the output buffer is filled with NULs, which is unnecessary
       for our use.
*/

static void copy_string (char **target, int32 *target_size, const char *source, int32 source_size)
{
int32 copy_size;

if (source_size == 0)                                   /* if we are asked to calculate the source length */
    copy_size = strlen (source);                        /*   then do it now */
else                                                    /* otherwise */
    copy_size = source_size;                            /*   use the supplied size */

if (copy_size > *target_size)                           /* if there is not enough space remaining */
    copy_size = *target_size;                           /*   then copy only enough to fill the buffer */

memcpy (*target, source, copy_size);                    /* copy the string */

*target = *target + copy_size;                          /* advance the output buffer pointer */
**target = '\0';                                        /*   and terminate the copied string */

*target_size = *target_size - copy_size;                /* drop the remaining space count */
return;                                                 /*   and return */
}


/* Parse a quoted string.

   A string delimited by single or double quotation marks is parsed from the
   buffer pointed to by the "sptr" parameter and copied into the buffer pointed
   to by the "dptr" parameter.  These specialized escapes are decoded and
   replaced with the indicated substitutions:

      Escape  Substitution
      ------  ------------
        \"         "
        \'         '
        \\         \
        \r         CR
        \n         LF

   In addition, a backslash followed by exactly three octal digits is replaced
   with the corresponding ASCII code.  The opening and closing quotes are not
   copied, but any escaped embedded quotes are.  All other characters are copied
   verbatim, except that lowercase alphabetic characters are replaced with
   uppercase characters if the "upshift" parameter is TRUE.

   The function returns a pointer to the next character in the source buffer
   after the closing quotation mark.  If the opening and closing quotation marks
   are not the same or the closing quotation mark is missing, the function
   returns NULL.


   Implementation notes:

    1. The routine assumes that the first "sptr" character is the opening quote,
       and it is this character that will be sought as the closing quote.  It is
       not necessary to escape the alternate quote character if it appears in
       the string.  For example, "It's great!" and 'Say "Hi"' are accepted as
       legal.
*/

static char *parse_quoted_string (char *sptr, char *dptr, t_bool upshift)
{
char   quote;
uint32 i, octal;

quote = *sptr++;                                        /* save the opening quotation mark */

while (sptr [0] != '\0' && sptr [0] != quote)           /* while characters remain */
    if (sptr [0] == '\\')                               /* if an escape sequence follows */
        if (sptr [1] == quote || sptr [1] == '\\') {    /*   then if it is a quote or backslash escape */
            sptr++;                                     /*     then skip over the sequence identifier */
            *dptr++ = *sptr++;                          /*       and copy the escaped character verbatim */
            }

        else if (sptr [1] == 'r' || sptr [1] == 'R') {  /* otherwise if it is a carriage return escape */
            sptr = sptr + 2;                            /*   then skip the escape pair */
            *dptr++ = CR;                               /*     and substitute a CR */
            }

        else if (sptr [1] == 'n' || sptr [1] == 'N') {  /* otherwise if it is a line feed escape */
            sptr = sptr + 2;                            /*   then skip the escape pair */
            *dptr++ = LF;                               /*     and substitute a LF */
            }

        else if (isdigit (sptr [1])) {                  /* otherwise if it's a numeric escape */
            sptr++;                                     /*   then skip over the sequence identifier */

            for (i = octal = 0; i < 3; i++)             /* look for three octal digits */
                if (*sptr >= '0' && *sptr <= '7')       /* if it's an octal digit */
                    octal = octal * 8 + *sptr++ - '0';  /*   then accumulate the value */
                else                                    /* otherwise */
                    break;                              /*   the escape is invalid */

            if (i == 3 && octal <= DEL)                 /* if the result is valid */
                *dptr++ = (char) octal;                 /*   then copy the escaped value */
            else                                        /* otherwise */
                return NULL;                            /*   the numeric escape is invalid */
            }

        else                                            /* otherwise the escape is unrecognized */
            *dptr++ = *sptr++;                          /*   so copy the character verbatim */

    else if (upshift)                                   /* otherwise if case conversion is requested */
        *dptr++ = toupper (*sptr++);                    /*   then copy the character and upshift */
    else                                                /* otherwise */
        *dptr++ = *sptr++;                              /*   copy the character verbatim */

*dptr = '\0';                                           /* terminate the destination buffer */

if (sptr [0] == '\0')                                   /* if we did not see a closing quotation mark */
    return NULL;                                        /*   then the string is invalid */

else {                                                  /* otherwise */
    while (isspace (*++sptr));                          /*   discard any trailing spaces */

    return sptr;                                        /* return a pointer to the remainder of the source string */
    }
}


/* Parse a DELAY clause.

   This routine parses a clause of the form:

     DELAY <delay>

   ...where <delay> is an unsigned count representing the number of event ticks
   to delay an operation.

   On entry, "cptr" points at a pointer to the input string, and "delay" points
   at an integer variable that will receive the delay value.  If the input
   string does not begin with the DELAY keyword, the "delay" value is set to -1,
   and SCPE_OK is returned.  If the keyword is present, the following value is
   converted to a number.  If the value does not parse, SCPE_ARG status is
   returned, and "delay" is unchanged.  Otherwise, the value stored in the
   variable indicated by "delay", "cptr" is advanced over the clause, and
   SCPE_OK is returned.
*/

static t_stat parse_delay (char **cptr, int32 *delay)
{
char   *tptr, vbuf [CBUFSIZE];
t_stat status;

tptr = get_glyph (*cptr, vbuf, 0);                      /* parse out the next glyph */

if (strcmp (vbuf, "DELAY") == 0) {                      /* if this is a DELAY keyword */
    tptr = get_glyph (tptr, vbuf, 0);                   /*   then get the delay value */

    *delay = (int32) get_uint (vbuf, 10, INT_MAX, &status);  /* parse the number */

    if (status == SCPE_OK)                              /* if the parse succeeded */
        *cptr = tptr;                                   /*   then advance the input pointer over the clause */
    else                                                /* otherwise */
        return status;                                  /*   return the parse status */
    }

else                                                    /* otherwise */
    *delay = -1;                                        /*   the keyword is not DELAY */

return SCPE_OK;                                         /* return success */
}


/* Encode a string for printing.

   This routine encodes a string containing control characters into the
   equivalent escaped form, surrounded by quote marks.  On entry, "source"
   points at the string to encode.  Embedded quotes and control characters are
   replaced with their escaped counterparts.  The return value points at an
   internal static buffer containing the encoded string, as the routine is
   intended to be called from a print function and so used immediately.

   The supported escapes are the same as those parsed for quoted strings.


   Implementation notes:

    1. To avoid dealing with buffer overflows, we declare an output buffer large
       enough to accommodate the largest decoded input string.  This would be a
       string where each character is a control character requiring four
       encoding characters in the output buffer (plus three more for the
       surrounding quotes and the trailing NUL).  Consequently, we need not
       check for the end of the buffer while encoding.
*/

static char *encode (const char *source)
{
static char encoding [CBUFSIZE * 4 + 3];                /* ensure there is always enough space */
char   *eptr;

encoding [0] = '"';                                     /* start with a leading quote */
eptr = encoding + 1;                                    /*   and point at the next character */

while (*source != '\0') {                               /* while source characters remain */
    if (iscntrl (*source)                               /* if the next character is a control character */
      || *source == '"' || *source == '\\') {           /*   or is a quote or backslash */
        *eptr++ = '\\';                                 /*     then escape it */

        if (*source == '\r')                            /* if it's a CR character */
            *eptr++ = 'r';                              /*   then replace it with the \r sequence */

        else if (*source == '\n')                       /* otherwise if it's an LF character */
            *eptr++ = 'n';                              /*   then replace it with the \n sequence */

        else if (*source == '"' || *source == '\\')     /* otherwise if it's a quote or backslash character */
            *eptr++ = *source;                          /*   then replace it with the \" or \\ sequence */

        else {                                          /* otherwise */
            sprintf (eptr, "%03o", (int) *source);      /*   replace it */
            eptr = eptr + 3;                            /*      with the \ooo sequence */
            }
        }

    else                                                /* otherwise it's a normal character */
        *eptr++ = *source;                              /*   so copy it verbatim */

    source++;                                           /* bump the source pointer */
    }                                                   /*   and continue until all characters are encoded */

*eptr++ = '"';                                          /* add a closing quote */
*eptr   = '\0';                                         /*   and terminate the string */

return encoding;                                        /* return a pointer to the encoded string */
}
