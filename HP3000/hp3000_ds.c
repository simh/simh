/* hp3000_ds.c: HP 3000 30229B Cartridge Disc Interface simulator

   Copyright (c) 2016-2018, J. David Bryan

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
   AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be used
   in advertising or otherwise to promote the sale, use or other dealings in
   this Software without prior written authorization from the author.

   DS           HP 30229B Cartridge Disc Interface

   27-Dec-18    JDB     Revised fall through comments to comply with gcc 7
   05-Sep-17    JDB     Changed REG_A (permit any symbolic override) to REG_X
   12-Sep-16    JDB     Changed DIB register macro usage from SRDATA to DIB_REG
   09-Jun-16    JDB     Added casts for ptrdiff_t to int32 values
   08-Jun-16    JDB     Corrected %d format to %u for unsigned values
   16-May-16    JDB     Fixed interrupt mask setting
   13-May-16    JDB     Modified for revised SCP API function parameter types
   24-Mar-16    JDB     Changed the buffer element type from uint16 to DL_BUFFER
   21-Mar-16    JDB     Changed uint16 types to HP_WORD
   21-Jul-15    JDB     First release version
   15-Jun-15    JDB     Passes the cartridge disc diagnostic (D419A)
   15-Feb-15    JDB     Created

   References:
     - Model 7905A Cartridge Disc Subsystem Installation and Service Manual
         (30129-90003, May 1976)
     - Stand-Alone HP 30129A (7905A) Disc Cartridge Diagnostic
         (30129-90007, February 1976)
     - HP 3000 Series III Engineering Diagrams Set
         (30000-90141, April 1980)
     - 13037 Disc Controller Technical Information Package
         (13037-90902, August 1980)
     - 7925D Disc Drive Service Manual
         (07925-90913, April 1984)


   The HP 30129A Cartridge Disc Subsystem connects the 7905A, 7906A, 7920A, and
   7925A disc drives to the HP 3000.  The subsystem consists of a 30229B
   Cartridge Disc Interface, a 13037D Multiple-Access Disc Controller ("MAC"),
   and from one to eight MAC drives.  The subsystem uses the Selector Channel to
   achieve a 937.5 KB/second transfer rate to the CPU.

   The disc controller connects from one to eight HP 7905 (15 MB), 7906 (20 MB),
   7920 (50 MB), or 7925 (120 MB) disc drives to interfaces installed in from
   one to eight CPUs.  The drives use a common command set and present data to
   the controller synchronously at a 468.75 kiloword per second (2.133
   microseconds per word) data rate.

   The disc interface is used to connect the HP 3000 CPU to the 13037's device
   controller.  While the controller supports multiple-CPU systems, the HP 3000
   does not use this capability.

   This module simulates a 30229B interface connected to a 13037D controller;
   the controller simulation is provided by the hp_disclib module.  From one to
   eight drives may be connected, and drive types may be freely intermixed.  A
   unit that is enabled but not attached appears to be a connected drive that
   does not have a disc pack in place.  A unit that is disabled appears to be
   disconnected.  An extra unit for the use of the disc controller library is
   also allocated.

   In hardware, the controller runs continuously in one of three states: in the
   Poll Loop (idle state), in the Command Wait Loop (wait state), or in command
   execution (busy state).  In simulation, the controller is run only when a
   command is executing or when a transition into or out of the two loops might
   occur.  Internally, the controller handles these transitions:

     - when a command other than End terminates (busy => wait)
     - when the End command terminates (busy => idle)
     - when a command timeout occurs (wait => idle)
     - when a parameter timeout occurs (busy => idle)
     - when a seek completes (if idle, and interrupts are enabled: idle => wait)

   The interface must call the controller library to handle these transitions:

     - when a command is received from the CPU (idle or wait => busy)
     - when interrupts are enabled (if idle and drive Attention, idle => wait)

   In addition, each transition to the wait state must check for a pending
   command, and each transition to the idle state must check for both a pending
   command and a drive with Attention status asserted.

   While the controller is in the busy state, command execution is broken up
   into a series of phases.  Phase transitions are scheduled on the drive units
   for commands that access the drives and on the controller unit otherwise.
   The interface unit service routine must call the disc controller to inform it
   of these events.


   The disc interface responds to direct and programmed I/O instructions, as
   follows:

   Control Word Format (CIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R | T | -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     M = master reset
     R = reset interrupts
     T = test mode

   Test mode inhibits the flag bus signals.  This allows the diagnostic to
   exercise the interface without causing the disc controller to react.


   Control Word Format (SIO Control):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   -   -   -   -   - | W |  word 1
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                 disc controller command word                  |  word 2
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     W = set the Wait flip-flop

   The command opcode is the disc controller command to execute.  If the command
   takes or returns parameters, an SIO Write or Read must follow the Control
   order to supply or receive them.


   Status Word Format (TIO and SIO Status):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | T | I |termination status | -   -   -   - |  unit number  |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     S = SIO OK
     T = test mode is enabled (also DIO OK)
     I = interrupt request

   The termination status and unit number report the success or failure of the
   last disc controller command.  Also, note that the test mode flip-flop output
   is reported as DIO OK.  This means that RIO and WIO are inhibited unless test
   mode is set.


   Output Data Word Format (WIO and SIO Write):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                  data buffer register value                   |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   Input Data Word Format (RIO and SIO Read):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                  data buffer register value                   |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   Disc read or write commands may transfer up to 4K words with a single SIO
   Read or Write order.  Chained orders are necessary if longer transfers are
   required.

   The interface allows the channel to retry a failed transfer without CPU
   intervention.  The controller asserts the DVEND signal for transfer errors
   that it considers retryable (e.g., a disc read error).  A channel program can
   detect this condition via a Conditional Jump order, which will succeed for
   each retryable failure until the retry count expires.

   Unusually among HP 3000 interfaces, this device reacts to the PFWARN signal.
   A pending power failure will abort the current disc transfer and channel
   program, so that the operating system will know to retry the transfer once
   power has been restored.


   Implementation notes:

    1. As only a single interface connected to the disc controller is supported,
       the interface select address jumpers are not simulated.  Instead, the
       interface behaves as though it is always selected and does not process
       the SELIF and DSCIF functions from the controller.

    2. In hardware, jumper W1 selects whether the interface should assert the
       CLEAR signal to the disc controller when the interface is preset.  This
       jumper is needed in a multiple-interface system so that only one
       interface clears the controller.  The simulation does check the state of
       jumper W1, but as only a single interface is supported, the jumper
       position is hard-coded as ENABLED rather than being configurable via the
       user interface.

    3. Several of the hardware flip-flops that directly drive flag signals to
       the controller are modeled in simulation by setting and clearing the
       corresponding bits in the flags word itself.

    4. The simulation provides REALTIME and FASTTIME options.  FASTTIME settings
       may be altered via the register interface.  Performing a power-on reset
       (RESET -P) will restore the original FASTTIME values.

    5. This simulation provides diagnostic override settings to allow complete
       testing coverage via the offline disc diagnostic.  See the comments in
       the disc controller library for details of this capability.
*/



#include "hp3000_defs.h"
#include "hp3000_io.h"
#include "hp_disclib.h"



/* Program constants */

#define DRIVE_COUNT         (DL_MAXDRIVE + 1)           /* number of disc drive units */
#define UNIT_COUNT          (DRIVE_COUNT + DL_AUXUNITS) /* total number of units */

#define ds_cntlr            ds_unit [DL_MAXDRIVE + 1]   /* controller unit alias */

#define OVERRIDE_COUNT      50                          /* count of diagnostic override entries */

#define PRESET_ENABLE       TRUE                        /* Preset Jumper (W1) is enabled */

#define UNUSED_COMMANDS     (BUSY | DSCIF | SELIF | IFPRF | STDFL | FREE)   /* unused disc interface commands */


/* Debug flags (interface-specific) */

#define DEB_IOB             DL_DEB_IOB                  /* trace I/O bus signals and data words */
#define DEB_CSRW            (1u << DL_DEB_V_UF + 0)     /* trace control, status, read, and write commands */


/* Control word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R | T | -   -   -   -   -   -   -   -   -   -   -   -   - |  DIO
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   -   -   -   -   - | W |  PIO word 1
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                 disc controller command word                  |  PIO word 2
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define CN_MR               0100000u            /* (M) master reset */
#define CN_RIN              0040000u            /* (R) reset interrupt */
#define CN_TEST             0020000u            /* (T) test mode */

#define CN_WAIT             0000001u            /* (W) wait for data */

#define CN_OPCODE_MASK      0017400u            /* command word opcode mask */

#define CN_OPCODE_SHIFT     8                   /* controller opcode alignment shift */

#define CN_OPCODE(c)        ((CNTLR_OPCODE) (((c) & CN_OPCODE_MASK) >> CN_OPCODE_SHIFT))

static const BITSET_NAME control_names [] = {   /* Control word names */
    "master reset",                             /*   bit  0 */
    "reset interrupt",                          /*   bit  1 */
    "test mode"                                 /*   bit  2 */
    };

static const BITSET_FORMAT control_format =     /* names, offset, direction, alternates, bar */
    { FMT_INIT (control_names, 13, msb_first, no_alt, no_bar) };


/* Status word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | T | I |termination status | -   -   -   - |  unit number  |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define ST_SIO_OK           0100000u            /* (S) SIO OK to use */
#define ST_TEST             0040000u            /* (T) test mode enabled */
#define ST_INTREQ           0020000u            /* (I) interrupt requested */
#define ST_STATUS_MASK      0017400u            /* encoded termination status mask */
#define ST_UNIT_MASK        0000017u            /* unit number mask */

#define ST_MASK             ~(ST_SIO_OK | ST_TEST | ST_INTREQ)

#define ST_STATUS_SHIFT     8                   /* termination status alignment shift */
#define ST_UNIT_SHIFT       0                   /* unit number alignment shift */

#define ST_STATUS(n)        ((n) << ST_STATUS_SHIFT & ST_STATUS_MASK)

#define ST_TO_UNIT(s)       (((s) & ST_UNIT_MASK) >> ST_UNIT_SHIFT)
#define ST_TO_STATUS(s)     (CNTLR_STATUS) (((s) & ST_STATUS_MASK) >> ST_STATUS_SHIFT)


static const BITSET_NAME status_names [] = {    /* Status word names */
    "SIO OK",                                   /*   bit  0 */
    "test mode",                                /*   bit  1 */
    "interrupt"                                 /*   bit  2 */
    };

static const BITSET_FORMAT status_format =      /* names, offset, direction, alternates, bar */
    { FMT_INIT (status_names, 13, msb_first, no_alt, append_bar) };


/* Disc controller library data structures */

#define DS_SEEK_ONE         uS (25)             /* track-to-track seek time */
#define DS_SEEK_FULL        uS (250)            /* full-stroke seek time */
#define DS_SECTOR_FULL      uS (50)             /* full sector rotation time */
#define DS_DATA_XFER        uS (1)              /* data transfer response time */
#define DS_ISG              uS (25)             /* intersector gap rotation time */
#define DS_OVERHEAD         uS (25)             /* controller execution overhead */

static DELAY_PROPS fast_times =                 /* FASTTIME delays */
    { DELAY_INIT (DS_SEEK_ONE,    DS_SEEK_FULL,
                  DS_SECTOR_FULL, DS_DATA_XFER,
                  DS_ISG,         DS_OVERHEAD) } ;

static DIAG_ENTRY overrides [OVERRIDE_COUNT] = { /* diagnostic overrides array */
    { DL_OVEND }
    };


/* Interface state */

static FLIP_FLOP sio_busy       = CLEAR;        /* SIO busy flip-flop */
static FLIP_FLOP device_sr      = CLEAR;        /* device service request flip-flop */
static FLIP_FLOP input_xfer     = CLEAR;        /* input transfer flip-flop */
static FLIP_FLOP output_xfer    = CLEAR;        /* output transfer flip-flop */
static FLIP_FLOP interrupt_mask = SET;          /* interrupt mask flip-flop */
static FLIP_FLOP jump_met       = CLEAR;        /* jump met flip-flop */
static FLIP_FLOP device_end     = CLEAR;        /* device end flip-flop */
static FLIP_FLOP data_overrun   = CLEAR;        /* data overrun flip-flop */
static FLIP_FLOP end_of_data    = CLEAR;        /* end of data flip-flop */
static FLIP_FLOP test_mode      = CLEAR;        /* test mode flip-flop */
static FLIP_FLOP data_wait      = CLEAR;        /* wait flip-flop */

static HP_WORD        status_word   = 0;        /* status register */
static HP_WORD        buffer_word   = 0;        /* data buffer register */
static HP_WORD        retry_counter = 0;        /* retry counter */
static CNTLR_FLAG_SET flags         = NO_FLAGS; /* disc controller interface flag set */

static DL_BUFFER      buffer [DL_BUFSIZE];      /* command/status/sector buffer */

DEVICE ds_dev;                                  /* incomplete device structure */

static CNTLR_VARS mac_cntlr =                   /* MAC controller */
    { CNTLR_INIT (MAC, ds_dev, buffer, overrides, fast_times) };


/* Interface local SCP support routines */

static CNTLR_INTRF ds_interface;
static t_stat      ds_service     (UNIT   *uptr);
static t_stat      ds_reset       (DEVICE *dptr);
static t_stat      ds_boot        (int32  unit_number, DEVICE *dptr);
static t_stat      ds_attach      (UNIT   *uptr, CONST char *cptr);
static t_stat      ds_detach      (UNIT   *uptr);
static t_stat      ds_load_unload (UNIT   *uptr, int32 value, CONST char *cptr, void *desc);


/* Interface local utility routines */

static void master_reset          (void);
static void deny_sio_busy         (void);
static void clear_interface_logic (DIB  *dibptr);
static void call_controller       (UNIT *uptr);


/* Interface SCP data structures */


/* Device information block */

static DIB ds_dib = {
    &ds_interface,                              /* device interface */
    4,                                          /* device number */
    SRNO_UNUSED,                                /* service request number */
    4,                                          /* interrupt priority */
    INTMASK_E                                   /* interrupt mask */
    };

/* Unit list */

#define UNIT_FLAGS          (UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE | UNIT_DISABLE | UNIT_UNLOAD)

static UNIT ds_unit [UNIT_COUNT] = {
    { UDATA (&ds_service, UNIT_FLAGS | UNIT_7905, WORDS_7905) },   /* drive unit 0 */
    { UDATA (&ds_service, UNIT_FLAGS | UNIT_7905, WORDS_7905) },   /* drive unit 1 */
    { UDATA (&ds_service, UNIT_FLAGS | UNIT_7905, WORDS_7905) },   /* drive unit 2 */
    { UDATA (&ds_service, UNIT_FLAGS | UNIT_7905, WORDS_7905) },   /* drive unit 3 */
    { UDATA (&ds_service, UNIT_FLAGS | UNIT_7905, WORDS_7905) },   /* drive unit 4 */
    { UDATA (&ds_service, UNIT_FLAGS | UNIT_7905, WORDS_7905) },   /* drive unit 5 */
    { UDATA (&ds_service, UNIT_FLAGS | UNIT_7905, WORDS_7905) },   /* drive unit 6 */
    { UDATA (&ds_service, UNIT_FLAGS | UNIT_7905, WORDS_7905) },   /* drive unit 7 */
    { UDATA (&ds_service, UNIT_DIS,               0)          }    /* controller unit */
    };

/* Register list */

static REG ds_reg [] = {
/*    Macro   Name    Location         Width  Offset            Flags           */
/*    ------  ------  ---------------  -----  ------  ------------------------- */
    { FLDATA (SIOBSY, sio_busy,                  0)                             },
    { FLDATA (DEVSR,  device_sr,                 0)                             },
    { FLDATA (INXFR,  input_xfer,                0)                             },
    { FLDATA (OUTXFR, output_xfer,               0)                             },
    { FLDATA (INTMSK, interrupt_mask,            0)                             },
    { FLDATA (JMPMET, jump_met,                  0)                             },
    { FLDATA (DEVEND, device_end,                0)                             },

    { FLDATA (DATOVR, data_overrun,              0)                             },
    { FLDATA (ENDDAT, end_of_data,               0)                             },
    { FLDATA (TEST,   test_mode,                 0)                             },
    { FLDATA (WAIT,   data_wait,                 0)                             },

    { FLDATA (CLEAR,  flags,                     0)                             },
    { FLDATA (CMRDY,  flags,                     1)                             },
    { FLDATA (DTRDY,  flags,                     2)                             },
    { FLDATA (EOD,    flags,                     3)                             },
    { FLDATA (INTOK,  flags,                     4)                             },
    { FLDATA (OVRUN,  flags,                     5)                             },
    { FLDATA (XFRNG,  flags,                     6)                             },

    { ORDATA (BUFFER, buffer_word,      16),          REG_X | REG_FIT | PV_RZRO },
    { ORDATA (STATUS, status_word,      16),                  REG_FIT | PV_RZRO },
    { DRDATA (RETRY,  retry_counter,     4),                  REG_FIT | PV_LEFT },

    { SRDATA (DIAG,   overrides,                              REG_HRO)          },

      DIB_REGS (ds_dib),

      DL_REGS (mac_cntlr, ds_unit, UNIT_COUNT, buffer, fast_times),

    { NULL }
    };

/* Modifier list */

static MTAB ds_mod [] = {

    DL_MODS (mac_cntlr, ds_load_unload, OVERRIDE_COUNT),

/*    Entry Flags  Value        Print String  Match String  Validation    Display        Descriptor       */
/*    -----------  -----------  ------------  ------------  ------------  -------------  ---------------- */
    { MTAB_XDV,    VAL_DEVNO,   "DEVNO",      "DEVNO",      &hp_set_dib,  &hp_show_dib,  (void *) &ds_dib },
    { MTAB_XDV,    VAL_INTMASK, "INTMASK",    "INTMASK",    &hp_set_dib,  &hp_show_dib,  (void *) &ds_dib },
    { MTAB_XDV,    VAL_INTPRI,  "INTPRI",     "INTPRI",     &hp_set_dib,  &hp_show_dib,  (void *) &ds_dib },
    { 0 }
    };

/* Debugging trace list */

static DEBTAB ds_deb [] = {
    { "CMD",   DL_DEB_CMD   },                  /* controller commands */
    { "INCO",  DL_DEB_INCO  },                  /* controller command initiations and completions */
    { "CSRW",  DEB_CSRW     },                  /* interface control, status, read, and write actions */
    { "STATE", DL_DEB_STATE },                  /* controller execution state changes */
    { "SERV",  DL_DEB_SERV  },                  /* controller unit service scheduling calls */
    { "XFER",  DL_DEB_XFER  },                  /* controller data reads and writes */
    { "IOBUS", DEB_IOB      },                  /* interface and controller I/O bus signals and data words */
    { NULL,    0            }
    };

/* Device descriptor */

DEVICE ds_dev = {
    "DS",                                       /* device name */
    ds_unit,                                    /* unit array */
    ds_reg,                                     /* register array */
    ds_mod,                                     /* modifier array */
    UNIT_COUNT,                                 /* number of units */
    8,                                          /* address radix */
    27,                                         /* address width = 128 MB */
    1,                                          /* address increment */
    8,                                          /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &ds_reset,                                  /* reset routine */
    &ds_boot,                                   /* boot routine */
    &ds_attach,                                 /* attach routine */
    &ds_detach,                                 /* detach routine */
    &ds_dib,                                    /* device information block */
    DEV_DEBUG | DEV_DISABLE,                    /* device flags */
    0,                                          /* debug control flags */
    ds_deb,                                     /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* Interface local SCP support routines */



/* Disc controller interface.

   The interface is installed on the IOP and Selector Channel buses and receives
   direct and programmed I/O commands from the IOP and Selector Channel,
   respectively.  In simulation, the asserted signals on the buses are
   represented as bits in the inbound_signals set.  Each signal is processed
   sequentially in numerical order, and a set of similar outbound_signals is
   assembled and returned to the caller, simulating assertion of the
   corresponding backplane signals.

   MAC disc controller commands take from 0 to 2 parameters and return from 0 to
   7 status words.  All communication with the disc controller is via programmed
   I/O.  Direct I/O is used only to communicate with the interface.

   Commands consist of a Control I/O order optionally followed by a one- or
   two-word Write order to supply the parameters.  Commands that return status
   consist of a Control order followed by a Read order to send the status.
   Controller command opcodes are carried in the IOAW of a programmed I/O
   Control order.  The IOCW is not used, except for bit 15, which is clocked
   into the WAIT flip-flop.  This bit must be set for commands that return
   parameters (Request Status, Request Sector Address, Request Syndrome, and
   Request Disc Address) to hold off the controller until the channel has
   executed a Read I/O order.  Setting WAIT asserts DTRDY unconditionally; the
   controller will not send a word to the CPU until DTRDY denies, indicating
   that the interface data buffer is empty and ready to receive the word.


   Implementation notes:

    1. In hardware, the disc controller executes a status command, such as
       Request Status, by first asserting IFGTC to clear the command from the
       interface and then asserting IFIN to tell the interface that the (first)
       status word is ready for pickup.  Both IFGTC and IFIN assert CHANSR to
       the channel; the first completes the Control I/O order, and the second
       completes the TOGGLEINXFER phase of the Read I/O order.  Simulating this
       sequential assertion requires two calls to the controller.  The second
       call is placed the TOGGLEINXFER handler, although in hardware this signal
       has no effect on the controller state.

    2. In hardware, the PREADSTB and PWRITESTB signals each toggle the Data
       Ready flip-flop, rather than explicitly clearing and setting it,
       respectively.  The simulation maintains this action.

    3. In hardware, three serially connected End of Data flip-flops are
       employed.  The first presets on EOT, the second clocks on the leading
       edge of TOGGLEXFER, and the third clocks when the Data Ready flip-flop
       clears.  The output of the third drives the EOD line to the disc
       controller.  For a read, DTRDY denies when the trailing edge of PREADSTB
       clocks the third flip-flop.  For this to work, the implied relationship
       is EOT asserts before the leading edge of TOGGLEINXFER, which asserts
       before the trailing edge of PREADSTB.

       In simulation, PREADSTB is processed before EOT, which is processed
       before TOGGLEINXFER, which is the order of the leading edges of the
       hardware signals.  To simulate clocking the Data Ready flip-flop on the
       trailing edge, the action is performed in the TOGGLEINXFER handler
       instead of the PREADSTB handler.

    4. In hardware, the Device SR 1 flip-flop is cleared by assertion of the
       PCONTSTB or PWRITESTB signals, and the the Device SR 2 flip-flop is
       cleared by assertion of CHANSO without DEVEND or by the clear output of
       the SIO busy flip-flop.  Also, DEVEND forces CHANSR assertion.  In
       simulation, a unified device_sr flip-flop is employed that is cleared if
       CHANSO is asserted or SIO Busy is clear.

    5. When TOGGLESIOOK clears the sio_busy flip-flop, the controller must be
       called to poll the drives for attention.  Consider an SIO program that
       does a Seek on drive 0, followed by a Read on drive 1, followed by an
       End.  If the seek completes during the read, the Drive Attention
       interrupt won't occur after the End unless the drive is polled from the
       TOGGLESIOOK handler, as INTOK isn't asserted until the channel program
       ends.

    6. Receipt of a DRESETINT signal clears the interrupt request and active
       flip-flops but does not cancel a request pending but not yet serviced by
       the IOP.  However, when the IOP does service the request by asserting
       INTPOLLIN, the interface routine returns INTPOLLOUT, which will cancel
       the request.
*/

static SIGNALS_DATA ds_interface (DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
INBOUND_SIGNAL signal;
INBOUND_SET    working_set      = inbound_signals;
HP_WORD        outbound_value   = 0;
OUTBOUND_SET   outbound_signals = NO_SIGNALS;

dprintf (ds_dev, DEB_IOB, "Received data %06o with signals %s\n",
         inbound_value, fmt_bitset (inbound_signals, inbound_format));

if (inbound_signals & CHANSO || sio_busy == CLEAR)      /* if a PIO signal is asserted or SIO is inactive */
    device_sr = CLEAR;                                  /*   then clear the device SR flip-flop */

while (working_set) {
    signal = IONEXTSIG (working_set);                   /* isolate the next signal */

    switch (signal) {                                   /* dispatch an I/O signal */

        case SETINT:
        case DSETINT:
            dibptr->interrupt_request = SET;            /* request an interrupt */

            if (interrupt_mask)                         /* if the interrupt mask is satisfied */
                outbound_signals |= INTREQ;             /*   then assert the INTREQ signal */
            break;


        case DRESETINT:
            dibptr->interrupt_active = CLEAR;           /* reset the interrupt active flip-flop */
            break;


        case DSETMASK:
            if (dibptr->interrupt_mask == INTMASK_E)            /* if the mask is always enabled */
                interrupt_mask = SET;                           /*   then set the mask flip-flop */
            else                                                /* otherwise */
                interrupt_mask = D_FF (dibptr->interrupt_mask   /*   set the mask flip-flop if the mask bit */
                                       & inbound_value);        /*     is present in the mask value */

            if (interrupt_mask && dibptr->interrupt_request)    /* if the mask is enabled and a request is pending */
                outbound_signals |= INTREQ;                     /*   then assert the INTREQ signal */
            break;


        case DCONTSTB:
            dprintf (ds_dev, DEB_CSRW, "Control is %s\n",
                     fmt_bitset (inbound_value, control_format));

            if (inbound_value & CN_MR)                  /* if the master reset bit is set */
                master_reset ();                        /*   then reset the interface */

            if (inbound_value & CN_RIN)                 /* if the reset interrupt bit is set */
                dibptr->interrupt_request = CLEAR;      /*   then clear the interrupt request */

            test_mode = D_FF (inbound_value & CN_TEST); /* set the test mode flip-flop from the test bit */
            break;


        case PSTATSTB:
        case DSTATSTB:
            outbound_value = status_word;               /* get the controller status */

            if (sio_busy == CLEAR && sel_is_idle)       /* if the interface and channel are inactive */
                outbound_value |= ST_SIO_OK;            /*   then add the SIO OK status bit */

            if (test_mode == SET)                       /* if test mode is enabled */
                outbound_value |= ST_TEST;              /*   then add the DIO OK status bit */

            if (dibptr->interrupt_request == SET)       /* if an interrupt request is pending */
                outbound_value |= ST_INTREQ;            /*   then add the IRQ status bit */

            dprintf (ds_dev, DEB_CSRW, "Status is %s%s | unit %u\n",
                     fmt_bitset (outbound_value, status_format),
                     dl_status_name (ST_TO_STATUS (outbound_value)),
                     ST_TO_UNIT (outbound_value));
            break;


        case DREADSTB:
            outbound_value = buffer_word;               /* return the data buffer register value */

            dprintf (ds_dev, DEB_CSRW, "Buffer value %06o returned\n",
                     outbound_value);
            break;


        case DWRITESTB:
            dprintf (ds_dev, DEB_CSRW, "Buffer value %06o set\n",
                     inbound_value);

            buffer_word = inbound_value;                /* set the data buffer register value */
            break;


        case DSTARTIO:
            dprintf (ds_dev, DEB_CSRW, "Channel program started\n");

            sio_busy = SET;                             /* set the SIO busy flip-flop */
            flags &= ~INTOK;                            /*   and clear the interrupt OK flag */

            sel_assert_REQ (dibptr);                    /* request the channel */
            break;


        case TOGGLESIOOK:
            TOGGLE (sio_busy);                          /* set or clear the SIO busy flip-flop */

            if (sio_busy == CLEAR) {                    /* if the flip-flop was cleared */
                deny_sio_busy ();                       /*   then reset the associated devices */

                dprintf (ds_dev, DEB_CSRW, "Channel program ended\n");

                call_controller (NULL);                 /* check for drive attention held off by INTOK denied */
                }
            break;


        case TOGGLEINXFER:
            TOGGLE (input_xfer);                        /* set or clear the input transfer flip-flop */

            if (input_xfer == SET)                      /* if the transfer is starting */
                call_controller (NULL);                 /*   then let the controller know to output the first word */

            else if (end_of_data == SET)                /* otherwise if EOT is asserted */
                flags |= EOD;                           /*   then PREADSTB has cleared DTRDY */
            break;


        case TOGGLEOUTXFER:
            TOGGLE (output_xfer);                       /* set or clear the output transfer flip-flop */

            if (output_xfer == SET)                     /* if the transfer is starting */
                device_sr = SET;                        /*   then request the first word from the channel */
            break;


        case PCMD1:
            data_wait = D_FF (inbound_value & CN_WAIT); /* set the wait flip-flop from the supplied value */

            if (data_wait == SET)                       /* if the wait flip-flip is set */
              flags |= DTRDY;                           /*   then the data ready flag is forced true */

            device_sr = SET;                            /* request the second control word */

            dprintf (ds_dev, DEB_CSRW, "Control is %s wait\n",
                     (data_wait == SET ? "set" : "clear"));
            break;


        case PCONTSTB:
            dprintf (ds_dev, DEB_CSRW, "Control is %06o (%s)\n",
                     inbound_value, dl_opcode_name (MAC, CN_OPCODE (inbound_value)));

            buffer_word = inbound_value;                /* store the command in the data buffer register */
            flags |= CMRDY;                             /*   and set the command ready flag */

            call_controller (NULL);                     /* tell the controller to start the command */
            break;


        case PREADSTB:
            outbound_value = buffer_word;               /* return the data buffer register value */
            flags ^= DTRDY;                             /*   and toggle (clear) the data ready flag */

            call_controller (NULL);                     /* tell the controller that the buffer is empty */
            break;


        case PWRITESTB:
            buffer_word = inbound_value;                /* save the word to write */
            flags ^= DTRDY;                             /*   and toggle (set) the data ready flag */

            if (inbound_signals & TOGGLEOUTXFER)        /* EOT asserted with TOGGLEOUTXFER */
                end_of_data = SET;                      /*   sets the End of Data flip-flop */

            call_controller (NULL);                     /* tell the controller that the buffer is full */
            break;


        case EOT:
            if (inbound_signals & TOGGLEINXFER)         /* EOT asserted with TOGGLEINXFER */
                end_of_data = SET;                      /*   sets the End of Data flip-flop */
            break;


        case INTPOLLIN:
            if (dibptr->interrupt_request) {            /* if a request is pending */
                dibptr->interrupt_request = CLEAR;      /*   then clear it */
                dibptr->interrupt_active  = SET;        /*     and mark it now active */

                outbound_signals |= INTACK;             /* acknowledge the interrupt */
                outbound_value = dibptr->device_number; /*   and return our device number */
                }
            else                                        /* otherwise the request has been reset */
                outbound_signals |= INTPOLLOUT;         /*   so let the IOP know to cancel it */
            break;


        case XFERERROR:
        case PFWARN:
            dprintf (ds_dev, DEB_CSRW, "Channel program aborted\n");

            flags |= XFRNG;                             /* set the transfer error flag */
            clear_interface_logic (dibptr);             /*   and clear the interface to abort the transfer */
            break;


        case SETJMP:
            if (jump_met == SET)                        /* if the jump met flip-flop is set */
                outbound_signals |= JMPMET;             /*   then assert the JMPMET signal */

            jump_met = CLEAR;                           /* reset the flip-flop */
            break;


        case CHANSO:
            if (device_end == SET) {                        /* if the device end flip-flop is set */
                outbound_signals |= DEVEND | CHANSR;        /*   then assert DEVEND and CHANSR to the channel */

                device_end = D_FF (input_xfer | output_xfer);   /* clear device end if the transfer has stopped */
                }

            else if (device_sr == SET || test_mode == SET)  /* if the interface requests service */
                outbound_signals |= CHANSR;                 /*   then assert CHANSR to the channel */

            outbound_signals |= CHANACK;                    /* assert CHANACK to acknowledge the signal */
            break;


        case READNEXTWD:                                /* not used by this interface */
        case ACKSR:                                     /* not used by this interface */
        case DEVNODB:                                   /* not used by this interface */
        case TOGGLESR:                                  /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }

dprintf (ds_dev, DEB_IOB, "Returned data %06o with signals %s\n",
         outbound_value, fmt_bitset (outbound_signals, outbound_format));

return IORETURN (outbound_signals, outbound_value);     /* return the outbound signals and value */
}


/* Service a controller or drive unit.

   The service routine is called to execute scheduled controller command phases
   for the specified unit.  The actions to be taken depend on the current state
   of the controller and the drive unit.

   This routine is entered for three general reasons:

    1. A disc unit is ready to execute the next command phase.

    2. The controller unit is ready to execute the next command phase.

    3. The controller unit has timed out while waiting for a new command.

   Generally, the controller library handles all of the disc operations.  All
   that is necessary is to notify the controller, which will process the next
   phase of command execution.  Because the controller can overlap operations,
   in particular scheduling seeks on several drive units simultaneously, each
   drive unit carries its own current operation code and execution phase.  The
   controller uses these to determine what to do next.
*/

static t_stat ds_service (UNIT *uptr)
{
dprintf (ds_dev, DL_DEB_SERV, (uptr == &ds_cntlr
                                 ? "Controller unit service entered\n"
                                 : "Unit %d service entered\n"),
         (int32) (uptr - &ds_unit [0]));

call_controller (uptr);                                 /* call the controller */

if (device_sr == SET)                                   /* if the interface requests service */
    sel_assert_CHANSR (&ds_dib);                        /*   then assert CHANSR to the channel */

return SCPE_OK;
}


/* Device reset routine.

   This routine is called for a RESET, RESET DS, or BOOT DS command.  It is the
   simulation equivalent of the IORESET signal, which is asserted by the front
   panel LOAD and DUMP switches.

   For this interface, IORESET is identical to the programmed master reset.  In
   addition, if a power-on reset (RESET -P) is done, the original FASTTIME
   settings are restored.
*/

static t_stat ds_reset (DEVICE *dptr)
{
master_reset ();                                        /* perform a master reset */

if (sim_switches & SWMASK ('P')) {                      /* if this is a power-on reset */
    fast_times.seek_one        = DS_SEEK_ONE;           /*   then reset the track-to-track seek time, */
    fast_times.seek_full       = DS_SEEK_FULL;          /*     the full-stroke seek time, */
    fast_times.sector_full     = DS_SECTOR_FULL;        /*       the full-sector rotation time, */
    fast_times.data_xfer       = DS_DATA_XFER;          /*         the per-word data transfer time, */
    fast_times.intersector_gap = DS_ISG;                /*           the intersector gap time, */
    fast_times.overhead        = DS_OVERHEAD;           /*             and the controller execution overhead */
    }

return SCPE_OK;
}


/* Device boot routine.

   This routine is called for the BOOT DS command to initiate the system cold
   load procedure for the disc.  It is the simulation equivalent to presetting
   the System Switch Register to the appropriate control and device number bytes
   and then pressing the ENABLE+LOAD front panel switches.

   For this interface, the switch register is set to %0000nn, where "nn"
   is the current disc interface device number, which defaults to 4.  The
   control byte is 0 (Cold Load Read).

   The cold load procedure always uses unit 0.
*/

static t_stat ds_boot (int32 unit_number, DEVICE *dptr)
{
if (unit_number != 0)                                   /* if a unit other than 0 is specified */
    return SCPE_ARG;                                    /*   then fail with an invalid argument error */

else {                                                  /* otherwise */
    cpu_front_panel (TO_WORD (Cold_Load_Read,           /*   set up the cold load */
                              ds_dib.device_number),    /*     from disc unit 0 */
                    Cold_Load);

    return SCPE_OK;                                     /* return to run the bootstrap */
    }
}


/* Attach a disc image file to a drive unit.

   The specified file is attached to the indicated drive unit.  This is the
   simulation equivalent to inserting a disc pack into the drive and setting
   the RUN/STOP switch to RUN, which will load the heads and set the First
   Status and Attention bits in the drive status.

   The controller library routine handles command validation and setting the
   appropriate drive unit status.  It will return an error code if the command
   fails.  Otherwise, it will return SCPE_INCOMP if the command must be
   completed with a controller call or SCPE_OK if the command is complete.  If
   the controller is idle, a call will be needed to poll the drives for
   attention; otherwise, the drives will be polled the next time the controller
   becomes idle.


   Implementation notes:

    1. If we are called during a RESTORE command to reattach a file previously
       attached when the simulation was SAVEd, the unit status will not be
       changed by the controller, so the unit will not request attention.
*/

static t_stat ds_attach (UNIT *uptr, CONST char *cptr)
{
t_stat result;

result = dl_attach (&mac_cntlr, uptr, cptr);            /* attach the drive */

if (result == SCPE_INCOMP) {                            /* if the controller must be called before returning */
    call_controller (NULL);                             /*   then let it know to poll the drives */
    return SCPE_OK;                                     /*     before returning with success */
    }

else                                                    /* otherwise */
    return result;                                      /*   return the status of the attach */
}


/* Detach a disc image file from a drive unit.

   The specified file is detached from the indicated drive unit.  This is the
   simulation equivalent to setting the RUN/STOP switch to STOP and removing the
   disc pack from the drive.  Stopping the drive will unload the heads and set
   the Attention bit in the drive status.

   The controller library routine handles command validation and setting the
   appropriate drive unit status.  It will return an error code if the command
   fails.  Otherwise, it will return SCPE_INCOMP if the command must be
   completed with a controller call or SCPE_OK if the command is complete.  If
   the controller is idle, a call will be needed to poll the drives for
   attention; otherwise, the drives will be polled the next time the controller
   becomes idle.
*/

static t_stat ds_detach (UNIT *uptr)
{
t_stat result;

result = dl_detach (&mac_cntlr, uptr);                  /* detach the drive */

if (result == SCPE_INCOMP) {                            /* if the controller must be called before returning */
    call_controller (NULL);                             /*   then let it know to poll the drives */
    return SCPE_OK;                                     /*     before returning with success */
    }

else                                                    /* otherwise */
    return result;                                      /*   return the status of the detach */
}


/* Load or unload the drive heads.

   The SET DSn UNLOADED command simulates setting the hardware RUN/STOP switch
   to STOP.  The heads are unloaded, and the drive is spun down.

   The SET DSn LOADED command simulates setting the switch to RUN.  The drive is
   spun up, and the heads are loaded.  Loading fails if there is no pack in the
   drive, i.e., if the unit is not attached to a disc image file.

   The controller library routine handles command validation and setting the
   appropriate drive unit status.  It will return an error code if the command
   fails.  Otherwise, it will return SCPE_INCOMP if the command must be
   completed with a controller call or SCPE_OK if the command is complete.  If
   the controller is idle, a call will be needed to poll the drives for
   attention; otherwise, the drives will be polled the next time the controller
   becomes idle.
*/

static t_stat ds_load_unload (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
const t_bool load = (value != UNIT_UNLOAD);             /* TRUE if the heads are loading */
t_stat result;

result = dl_load_unload (&mac_cntlr, uptr, load);       /* load or unload the heads */

if (result == SCPE_INCOMP) {                            /* if the controller must be called before returning */
    call_controller (NULL);                             /*   then let it know to poll the drives */
    return SCPE_OK;                                     /*     before returning with success */
    }

else                                                    /* otherwise */
    return result;                                      /*   return the status of the load or unload */
}



/* Interface local utility routines */



/* Master reset.

   A master reset is generated either by an IORESET signal or a Programmed
   Master Clear (CIO with bit 0 set).  It initializes the interface to its idle
   state.  In addition, if jumper W1 (PRESET_ENABLE) is set, it asserts the
   CLEAR flag to the disc controller to perform a hard clear.
*/

static void master_reset (void)
{
interrupt_mask = SET;                                   /* set the interrupt mask */

ds_dib.interrupt_request = CLEAR;                       /* clear any current */
ds_dib.interrupt_active  = CLEAR;                       /*   interrupt request */

sio_busy     = CLEAR;                                   /* clear the SIO busy */
input_xfer   = CLEAR;                                   /*   input transfer */
output_xfer  = CLEAR;                                   /*     and output transfer flip-flops */
data_overrun = CLEAR;                                   /* clear the data overrun */
end_of_data  = CLEAR;                                   /*   end of data */
test_mode    = CLEAR;                                   /*     and test mode flip-flops */

deny_sio_busy ();                                       /* clear the logic affected by SIO Busy */

flags &= ~XFRNG;                                        /* clear the transfer error flag */

status_word = 0;                                        /* clear the status register */

if (PRESET_ENABLE) {                                    /* if jumper W1 (preset) is set to "E" */
    flags |= CLEARF;                                    /*   then assert CLEAR */
    call_controller (NULL);                             /*     to the controller */
    flags &= ~CLEARF;                                   /*       to do a hard clear */
    }

return;
}


/* Deny SIO busy.

   The internal SIO Busy signal enables a number of logic devices on the
   interface associated with SIO channel transfers.  When SIO Busy is denied,
   those devices are set or cleared as appropriate in preparation for the next
   SIO program.
*/

static void deny_sio_busy (void)
{
device_sr = CLEAR;                                      /* clear the service request, */
jump_met  = CLEAR;                                      /*   jump met, */
data_wait = CLEAR;                                      /*     and wait flip-flops */

retry_counter = 0;                                      /* clear the retry counter */

flags = flags & ~(CMRDY | DTRDY) | INTOK | EOD;         /* clear CMRDY and DTRDY and set INTOK and EOD flags */

return;
}


/* Clear interface logic.

   The clear interface logic signal is asserted during channel operation either
   when the interface requests an interrupt or the channel indicates a transfer
   failure by asserting XFERERROR.  It clears the SIO Busy, Input Transfer, and
   Output Transfer flip-flops, pulses the REQ line to abort the channel program,
   and sends EOD to the disc controller to abort any in-progress data transfer.

   The signal is inhibited when an SIO program is not active.
*/

static void clear_interface_logic (DIB *dibptr)
{
if (sio_busy == SET) {                                  /* if a channel program is in progress */
    sio_busy    = CLEAR;                                /*   then clear the SIO busy */
    input_xfer  = CLEAR;                                /*     input transfer */
    output_xfer = CLEAR;                                /*       and output transfer flip-flops */

    end_of_data = SET;                                  /* set the end of data flip-flop */

    deny_sio_busy ();                                   /* deny the SIO Busy signal */

    sel_assert_REQ (dibptr);                            /* abort the channel program */
    }

return;
}


/* Call the disc controller.

   The 13037 disc controller connects to CPU interfaces via a 16-bit data bus, a
   6-bit flag bus, a 4-bit function bus, and five additional control signals.
   The controller continuously monitors the flag bus and reacts to the interface
   changing the flag states by placing or accepting data on the data bus and
   issuing commands to the interface via the function bus.  The controller
   supports up to eight CPU interfaces simultaneously, and provision is made to
   poll each interface in turn via select and disconnect functions.  An
   interface only responds if it is currently selected.

   In simulation, a call to the dl_controller routine informs the controller of
   a (potential) change in flag state.  The current set of flags and data bus
   value are supplied, and the controller returns a combined set of functions
   and a data bus value.

   The controller must be called any time there is a change in the state of the
   interface or the drive units.  Generally, the cases that require notification
   are when the interface:

     - has a new command to execute
     - has a new data word available to send
     - has obtained the last data word received
     - has received a unit service event notification
     - has detected insertion or removal of a disc pack from a drive
     - has detected loading or unloading of a drive's heads
     - wants to hard-clear the controller

   The set of returned functions is processed sequentially, updating the
   interface state as indicated.  Some functions are not used by this interface,
   so they are masked off before processing to improve performance.

   Disc commands may be "stacked" on the interface by asserting PCONTSTB to
   store the new command into the data buffer register while the controller is
   still busy with the previous command.  This will assert CMRDY (if not in test
   mode), but the controller will not react to this signal until it finishes the
   current command.  For example, a channel program containing the Wakeup and
   End commands will transmit the End before the Wakeup completes.  This occurs
   because Wakeup asserts IFGTC to clear the command (and thereby asserts CHANSR
   to allow the channel to continue) about 18 microseconds before the controller
   completes the command and returns to the wait loop.  In simulation, an
   explicit check is made when a command completes.  If a stacked command is
   seen, the controller is called again to start it.

   Because the disc is a synchronous device, overrun or underrun can occur if
   the interface is not ready when the controller must transfer data.  There are
   four conditions that lead to an overrun or underrun:

    1. The controller is ready with a disc read word (IFCLK * IFIN), but the
       interface buffer is full (DTRDY).

    2. The controller needs a disc write word (IFCLK * IFOUT), but the interface
       buffer is empty (~DTRDY).

    3. The CPU attempts to read a word, but the interface buffer is empty
       (~DTRDY).

    4. The CPU attempts to write a word, but the interface buffer is full
       (DTRDY).

   The hardware design of the interface prevents the last two conditions, as the
   interface will assert CHANSR only when the buffer is full (read) or empty
   (write).  The interface does detect the first two conditions and sets the
   data overrun flip-flop if either occurs.

   Implementation notes:

    1. In hardware, OVRUN will be asserted when the controller requests write
       data when the buffer is empty.  In simulation, OVRUN will not be asserted
       when the controller is called with the empty buffer; instead, it will be
       asserted for the next controller call.  Because the controller will be
       called for the intersector phase, and because OVRUN isn't checked until
       that point, this "late" assertion does not affect overrun detection.

    2. In hardware, the data ready flip-flop is toggled as a result of reading
       or writing a word from or to the controller.  We follow that practice
       here, rather than setting or clearing it, which would be more
       appropriate.

    3. The hardware interface decodes the DSCIF and SELIF functions to allow the
       controller to be shared by two or more CPUs.  In simulation, these
       functions are ignored, as the simulator supports only one CPU connected
       to the interface.
*/

static void call_controller (UNIT *uptr)
{
CNTLR_IFN_IBUS result;
CNTLR_IFN_SET  command_set;
CNTLR_IFN      command;
CNTLR_FLAG_SET flag_set;

if (data_overrun == SET && (flags & XFRNG) == NO_FLAGS) /* if an overrun occurred without a transfer error */
    flags |= OVRUN;                                     /*   then tell the controller */

if (test_mode == SET)                                   /* if in test mode */
    flag_set = flags & CLEARF;                          /*   then all flags except CLEAR are inhibited */
else                                                    /* otherwise */
    flag_set = flags;                                   /*   present the full set of flags to the controller */


do {                                                    /* call the controller potentially more than once */
    result =                                            /*   to start or continue a command */
       dl_controller (&mac_cntlr, uptr, flag_set, (CNTLR_IBUS) buffer_word);

    command_set = DLIFN (result) & ~UNUSED_COMMANDS;    /* strip the commands we don't use as an efficiency */

    while (command_set) {                               /* process the set of returned interface commands */
        command = DLNEXTIFN (command_set);              /* isolate the next command */

        switch (command) {                              /* dispatch an interface command */

            case IFIN:                                  /* Interface In */
                if (flags & DTRDY)                      /* if the buffer is still full */
                    data_overrun = SET;                 /*   then this input overruns it */

                else {                                  /* otherwise the buffer is empty */
                    device_sr = D_FF (! end_of_data);   /*   so request the next word unless EOT */

                    if ((input_xfer == CLEAR            /* if not configured to read */
                      || output_xfer == SET)            /*   or configured to write */
                      && (flags & EOD) == NO_FLAGS)     /*     and the transfer is active */
                        flags |= XFRNG;                 /*       then set the transfer is no good */
                    }

                buffer_word = DLIBUS (result);          /* store the data word in the buffer */
                flags ^= DTRDY;                         /*   and toggle (set) the data ready flag */
                break;


            case IFOUT:                                 /* Interface Out */
                if ((flags & DTRDY) == NO_FLAGS)        /* if the buffer is empty */
                    data_overrun = SET;                 /*   then this output underruns it */

                if (end_of_data == SET)                 /* if this is the last transfer */
                    flags |= EOD;                       /*   then tell the controller */

                else {                                  /* otherwise the transfer continues */
                    device_sr = SET;                    /*   so request the next word */

                    if ((output_xfer == CLEAR           /* if not configured to write */
                      || input_xfer == SET)             /*   or configured to read */
                      && (flags & EOD) == NO_FLAGS)     /*     and the transfer is active */
                        flags |= XFRNG;                 /*       then set the transfer is no good */
                    }

                flags ^= DTRDY;                         /* toggle (clear) the data ready flag */
                break;


            case IFGTC:                                     /* Interface Get Command */
                flags &= ~(CMRDY | DTRDY | EOD | OVRUN);    /* clear the interface transfer flags */

                end_of_data = CLEAR;                        /* clear the end-of-data */
                data_overrun = CLEAR;                       /*   and data-overrun flip-flops */

                device_sr = SET;                            /* request channel service */
                break;


            case RQSRV:                                 /* Request Service */
                flags &= ~(EOD | OVRUN);                /* clear the end of data and data overrun flags */

                end_of_data = CLEAR;                    /* clear the */
                data_overrun = CLEAR;                   /*   corresponding flip-flops */

                device_sr = SET;                        /* request channel service */
                break;


            case SRTRY:                                 /* Set Retry */
                retry_counter = DLIBUS (result);        /* store the data value into the retry counter */
                break;


            case DVEND:                                 /* Device End */
                device_end = SET;                       /* set the device end */
                jump_met = SET;                         /*   and the "jump met condition" flip-flops */

                if (retry_counter > 0) {                /* if retries remain */
                    retry_counter = retry_counter - 1;  /*   then decrement the retry counter */
                    break;                              /*     and try again */
                    }
                                                        /* otherwise, request an interrupt */
            /* fall through into the STINT case */

            case STINT:                                 /* Set Interrupt */
                flags &= ~XFRNG;                        /* clear the transfer error flag */

                clear_interface_logic (&ds_dib);        /* clear the interface to abort the transfer */

                ds_dib.interrupt_request = SET;         /* set the request flip-flop */

                if (interrupt_mask)                     /* if the interrupt mask is satisfied */
                    iop_assert_INTREQ (&ds_dib);        /*   then assert the INTREQ signal */
                break;


            case WRTIO:                                     /* Write TIO */
                status_word = DLIBUS (result) & ST_MASK;    /* save the value without the SPD bits for TIO */
                break;


            case DSCIF:                                 /* not used by this simulation */
            case SELIF:                                 /* not used by this simulation */
                break;

            case BUSY:                                  /* not decoded by this interface */
            case IFPRF:                                 /* not decoded by this interface */
            case FREE:                                  /* not decoded by this interface */
            case STDFL:                                 /* not decoded by this interface */
                break;
            }

        command_set &= ~command;                        /* remove the current command from the set */
        }                                               /*   and continue with the remaining commands */
    }
while (flags & CMRDY                                    /* call the controller again if a command is pending */
  && result & FREE                                      /*   and a prior command just completed */
  && test_mode == CLEAR);                               /*     and not in test mode, which inhibits CMRDY */

return;
}
