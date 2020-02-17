/* hp2100_pt.c: HP 12597A-002/005 Paper Tape Reader/Punch Interface simulator

   Copyright (c) 1993-2016, Robert M. Supnik
   Copyright (c) 2017-2018, J. David Bryan

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
   AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   PTR          12597A-002 Paper Tape Reader Interface
   PTP          12597A-005 Paper Tape Punch Interface

   27-Sep-18    JDB     RESET -P now resets to original FASTTIME timing
   10-Jun-18    JDB     Added loopback diagnostic modes
   08-Jun-18    JDB     Added tracing
   05-Jun-18    JDB     Revised I/O model
   04-Jun-18    JDB     Split out from hp2100_stddev.c
                        Trimmed revisions to current file applicability
   27-Feb-18    JDB     Added the BBL
   18-Sep-17    JDB     Changed PTR "DIAG" modifier to "DIAGNOSTIC"
   03-Aug-17    JDB     PTP and TTY now append to existing file data
   18-Jul-17    JDB     The PTR device now handles the IOERR simulation stop
   11-Jul-17    JDB     Renamed "ibl_copy" to "cpu_ibl"
   27-Feb-17    JDB     ibl_copy no longer returns a status code
   23-Feb-17    JDB     Modified ptr_boot to use IBL_S_CLR to clear the S register
   17-Jan-17    JDB     Changed "hp_---sc" and "hp_---dev" to "hp_---_dib"
   13-May-16    JDB     Modified for revised SCP API function parameter types
   30-Dec-14    JDB     Added S-register parameters to ibl_copy
   24-Dec-14    JDB     Added casts for explicit downward conversions
   09-May-12    JDB     Separated assignments from conditional expressions
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   09-Jan-08    JDB     Fixed PTR trailing null counter for tape re-read
   31-Dec-07    JDB     Corrected and verified ioCRS actions
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   13-Sep-04    JDB     Added paper tape loop mode, DIAG/READER modifiers to PTR
                        Added PV_LEFT to PTR TRLLIM register
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Fixed SR setting in IBL
                        Implemented DMA SRQ (follows FLG)
   25-Apr-03    RMS     Added extended file support
   01-Nov-02    RMS     Revised BOOT command for IBL ROMs
   30-May-02    RMS     Widened POS to 32b
   22-Mar-02    RMS     Revised for dynamically allocated memory
   03-Nov-01    RMS     Changed DEVNO to use extended SET/SHOW
   29-Nov-01    RMS     Added read only unit support
   07-Sep-01    RMS     Moved function prototypes
   21-Nov-00    RMS     Fixed flag, buffer power up state
                        Added status input for ptp, tty
   15-Oct-00    RMS     Added dynamic device number support

   References:
     - 2748B Tape Reader Operating and Service Manual
         (02748-90041, October 1977)
     - 2895B Tape Punch Operating and Service Manual
         (02895-90008, August 1976)
     - 12597A-002 Tape Reader Interface Kit Operating and Service Manual
         (12597-90022, February 1975)
     - 12597A-005 Tape Punch Interface Kit Operating and Service Manual
         (12597-90025, April 1975)


   The 12597A-002 Tape Reader Interface is an 8-bit duplex register card that
   connects the HP 2748A/B Tape Reader to the HP computer family.  The 2748 is a
   photoreader capable of reading eight-level punched paper tapes at a rate of
   500 bytes per second.

   The 12597A interface contains nine configuration jumpers.  These are preset
   for proper tape reader operation.  The interface responds to I/O instructions
   as follows:

   Output Data Word format (OTA and OTB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   An IOO signal clocks the lower eight bits into the output register, but the
   output lines are not connected to the tape reader.  Therefore, output
   instructions are useful only during diagnostic program execution.


   Input Data Word format (LIA and LIB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   - |           tape data           |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The presence of a feed hole clocks the data byte into the input register.  An
   IOI signal enables the input register onto the I/O Data Bus.

   The 2748B provides an "end of tape" status indication, but this is not
   connected to the interface card, as all eight input bits are used for data.
   In hardware, if a tape is not loaded, or the end of the tape has passed
   through the reader, then attempting to read will cause the reader to hang.
   This simulator has the same behavior.  However, specifying a SET CPU
   STOP=IOERR command will cause a simulation stop with a "No tape loaded" error
   if either of these conditions occur.  If the error is corrected, either by
   attaching the paper tape image file or by rewinding the tape, then resuming
   simulation will retry the operation.

   A paper tape image need not contain trailing NUL bytes to act as the physical
   tape trailer.  Instead, when the physical EOF is reached, NUL bytes are
   automatically supplied by the simulator until the trailing NUL limit is
   reached.  The limit defaults to 40 bytes but may be changed via the register
   interface.  Most HP operating systems detect the end of the tape trailer
   after 30 NULs (feed frames) are seen in succession.

   Booting an absolute binary paper tape is supported by the Basic Binary Loader
   (BBL) on the 21xx machines and the 12992K Paper Tape Loader ROM on
   1000-series CPUs.

   This simulator supports two diagnostic modes.  If a paper tape image file is
   not attached, then the DIAGNOSTIC option simulates the installation of the HP
   1251-0332 diagnostic test (loopback) connector in place of the reader cable.
   This is needed to run the General Purpose Register Diagnostic (DSN 143300) as
   well as to serve as the standard I/O card for several other diagnostics that
   test interrupts.  If a file is attached, then the DIAGNOSTIC option converts
   the attached paper tape image into a continuous loop by rewinding the tape
   image file upon EOF.  This is used by the High-Speed Tape Reader/Punch
   Diagnostic (DSN 146200).  Setting the READER option returns the tape image to
   its normal linear configuration.


   The 12597A-005 Tape Punch Interface is an 8-bit duplex register card that
   connects the HP 2895A/B Tape Punch to the HP computer family.  The 2895 is an
   eight-level paper tape punch capable of punching at a rate of 75 bytes per
   second.

   The 12597A interface contains nine configuration jumpers.  These are preset
   for proper tape reader punch operation.  The interface responds to I/O
   instructions as follows:

   Output Data Word format (OTA and OTB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   - |           tape data           |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   An IOO signal clocks the lower eight bits into the output register.  The data
   is punched when the STC signal sets the command flip-flop, which asserts the
   PUNCH signal to the tape punch.


   Input Data Word format (LIA and LIB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   - | L | -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     L = Tape supply is low

   Pin 21 of the interface connector is grounded, so the input register is
   transparent, and bit 5 reflects the current state of the the tape low signal.
   An IOI signal enables the input register to the I/O Data Bus.


   Implementation notes:

    1. The PTR and PTP devices each support realistic/optimized timing and
       normal/diagnostic modes.  These properly are device characteristics, but
       because each device has only a single unit, the options are reflected in
       the unit flags fields instead of the device flags.  This avoids the
       necessity of having a validation routine just to handle setting the
       device flags.
*/



#include "hp2100_defs.h"
#include "hp2100_io.h"



/* Program limits */

#define CARD_COUNT          2                   /* count of interface cards supported */


/* Program constants */

#define NUL                 '\000'              /* null */


/* Unit flags */

#define UNIT_DIAG_SHIFT     (UNIT_V_UF + 0)     /* diagnostic mode */
#define UNIT_REALTIME_SHIFT (UNIT_V_UF + 1)     /* realistic timing mode */

#define UNIT_DIAG           (1u << UNIT_DIAG_SHIFT)
#define UNIT_REALTIME       (1u << UNIT_REALTIME_SHIFT)


/* Unit references */

typedef enum {
    ptr,                                        /* paper tape reader card index */
    ptp                                         /* paper tape punch card index */
    } CARD_INDEX;


/* Device properties.

   The paper tape reader/punch diagnostic depends on the reader being at least
   twice as fast as the punch.  The FASTTIME values are selected to meet this
   requirement.
*/

#define PTR_FAST_TIME       100                 /* paper tape reader optimized timing delay */
#define PTR_REAL_TIME       mS (2)              /* paper tape reader realistic timing delay */

#define PTP_FAST_TIME       200                 /* paper tape punch optimized timing delay */
#define PTP_REAL_TIME       mS (13.3)           /* paper tape punch realistic timing delay */

#define PT_DIAG_TIME        2                   /* interface loopback delay */


/* Paper tape punch status word.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   - | L | -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define PS_LOW_TAPE         0000040             /* low tape supply */

static const BITSET_NAME ptp_status_names [] = {        /* Printer status word names */
    "tape low"                                          /*   bit  5 */
    };

static const BITSET_FORMAT ptp_status_format =          /* names, offset, direction, alternates, bar */
    { FMT_INIT (ptp_status_names, 5, msb_first, no_alt, no_bar) };


/* Interface local state declarations */

typedef struct {
    uint8        output_data;                   /* output data register */
    uint8        input_data;                    /* input data register */
    FLIP_FLOP    command;                       /* command flip-flop */
    FLIP_FLOP    control;                       /* control flip-flop */
    FLIP_FLOP    flag;                          /* flag flip-flop */
    FLIP_FLOP    flag_buffer;                   /* flag buffer flip-flop */
    } CARD_STATE;

static CARD_STATE pt [CARD_COUNT];              /* per-card state */


/* PTR local state declarations */

static int32 ptr_trlcnt = 0;                    /* trailer counter */
static int32 ptr_trllim = 40;                   /* trailer to add */
static int32 fast_read_time = PTR_FAST_TIME;    /* fast read time */


/* PTP local state declarations */

static int32 fast_punch_time = PTP_FAST_TIME;   /* fast punch time */


/* I/O interface routine declaration */

static INTERFACE pt_interface;


/* Interface local utility routines */

static t_stat set_mode (UNIT *uptr, int32 value, CONST char *cptr, void *desc);


/* PTR local SCP support routine declarations */

static t_stat ptr_attach (UNIT *uptr, CONST char *cptr);
static t_stat ptr_reset  (DEVICE *dptr);
static t_stat ptr_boot   (int32 unitno, DEVICE *dptr);

/* PTP local SCP support routine declarations */

static t_stat ptp_reset (DEVICE *dptr);


/* PTR local utility routine declarations */

static t_stat ptr_service (UNIT *uptr);

/* PTP local utility routine declarations */

static t_stat ptp_service (UNIT *uptr);


/* Interface SCP data declarations */

/* Unit lists */

static UNIT pt_unit [CARD_COUNT] = {            /* unit array, indexed by CARD_INDEX */
/*           Event Routine  Unit Flags                             Capacity  Delay         */
/*           -------------  -------------------------------------  --------  ------------- */
    { UDATA (&ptr_service,  UNIT_SEQ | UNIT_ATTABLE | UNIT_ROABLE,    0),    PTR_FAST_TIME },
    { UDATA (&ptp_service,  UNIT_SEQ | UNIT_ATTABLE,                  0),    PTP_FAST_TIME }
    };

#define ptr_unit            pt_unit [ptr]       /* paper tape reader unit */
#define ptp_unit            pt_unit [ptp]       /* paper tape punch unit */


/* PTR SCP data declarations */

/* Device information block */

static DIB ptr_dib = {
    &pt_interface,                              /* the device's I/O interface function pointer */
    PTR,                                        /* the device's select code (02-77) */
    0,                                          /* the card index */
    "12597A-002 Tape Reader Interface",         /* the card description */
    "12992K Paper Tape Loader"                  /* the ROM description */
    };


/* Register list */

static REG ptr_reg [] = {
/*    Macro   Name    Location                    Width     Offset        Flags       */
/*    ------  ------  ------------------------  ----------  ------  ----------------- */
    { ORDATA (BUF,    pt [ptr].input_data,          8)                                },
    { ORDATA (OBUF,   pt [ptr].output_data,         8),             REG_HIDDEN        },
    { FLDATA (CTL,    pt [ptr].control,                       0)                      },
    { FLDATA (FLG,    pt [ptr].flag,                          0)                      },
    { FLDATA (FBF,    pt [ptr].flag_buffer,                   0)                      },
    { DRDATA (TRLCTR, ptr_trlcnt,                   8),             REG_HRO           },
    { DRDATA (TRLLIM, ptr_trllim,                   8),             PV_LEFT           },
    { DRDATA (POS,    ptr_unit.pos,             T_ADDR_W),          PV_LEFT           },
    { DRDATA (TIME,   fast_read_time,              24),             PV_LEFT           },

      DIB_REGS (ptr_dib),

    { NULL }
    };


/* Modifier list */

static MTAB ptr_mod [] = {
/*    Mask Value     Match Value    Print String        Match String  Validation  Display  Descriptor */
/*    -------------  -------------  ------------------  ------------  ----------  -------  ---------- */
    { UNIT_DIAG,     0,             "reader mode",      "READER",     NULL,       NULL,    NULL       },
    { UNIT_DIAG,     UNIT_DIAG,     "diagnostic mode",  "DIAGNOSTIC", NULL,       NULL,    NULL       },
    { UNIT_REALTIME, 0,             "fast timing",      "FASTTIME",   &set_mode,  NULL,    NULL       },
    { UNIT_REALTIME, UNIT_REALTIME, "realistic timing", "REALTIME",   &set_mode,  NULL,    NULL       },

/*    Entry Flags          Value  Print String  Match String  Validation    Display        Descriptor        */
/*    -------------------  -----  ------------  ------------  ------------  -------------  ----------------- */
    { MTAB_XDV,             1u,   "SC",         "SC",         &hp_set_dib,  &hp_show_dib,  (void *) &ptr_dib },
    { MTAB_XDV | MTAB_NMO, ~1u,   "DEVNO",      "DEVNO",      &hp_set_dib,  &hp_show_dib,  (void *) &ptr_dib },
    { 0 }
    };


/* Trace list */

static DEBTAB ptr_deb [] = {
    { "SERV",  TRACE_SERV  },                   /* trace unit service scheduling calls and entries */
    { "XFER",  TRACE_XFER  },                   /* trace data transmissions */
    { "IOBUS", TRACE_IOBUS },                   /* trace I/O bus signals and data words received and returned */
    { NULL,    0           }
    };


/* Device descriptor */

DEVICE ptr_dev = {
    "PTR",                                      /* device name */
    &ptr_unit,                                  /* unit array */
    ptr_reg,                                    /* register array */
    ptr_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &ptr_reset,                                 /* reset routine */
    &ptr_boot,                                  /* boot routine */
    &ptr_attach,                                /* attach routine */
    NULL,                                       /* detach routine */
    &ptr_dib,                                   /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    ptr_deb,                                    /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };


/* PTP SCP data declarations */

/* Device information block */

static DIB ptp_dib = {
    &pt_interface,                              /* the device's I/O interface function pointer */
    PTP,                                        /* the device's select code (02-77) */
    1,                                          /* the card index */
    "12597A-005 Tape Punch Interface",          /* the card description */
    NULL                                        /* the ROM description */
    };


/* Register list */

static REG ptp_reg [] = {
/*    Macro   Name    Location                    Width     Offset        Flags       */
/*    ------  ------  ------------------------  ----------  ------  ----------------- */
    { ORDATA (IBUF,   pt [ptr].input_data,          8),             REG_HIDDEN        },
    { ORDATA (BUF,    pt [ptp].output_data,         8)                                },
    { FLDATA (CTL,    pt [ptp].control,                       0)                      },
    { FLDATA (FLG,    pt [ptp].flag,                          0)                      },
    { FLDATA (FBF,    pt [ptp].flag_buffer,                   0)                      },
    { DRDATA (POS,    ptp_unit.pos,             T_ADDR_W),          PV_LEFT           },
    { DRDATA (TIME,   fast_punch_time,             24),             PV_LEFT           },

      DIB_REGS (ptp_dib),

    { NULL }
    };


/* Modifier list */

static MTAB ptp_mod [] = {
/*    Mask Value     Match Value    Print String        Match String  Validation  Display  Descriptor */
/*    -------------  -------------  ------------------  ------------  ----------  -------  ---------- */
    { UNIT_DIAG,     0,             "punch mode",       "PUNCH",      NULL,       NULL,    NULL       },
    { UNIT_DIAG,     UNIT_DIAG,     "diagnostic mode",  "DIAGNOSTIC", NULL,       NULL,    NULL       },
    { UNIT_REALTIME, 0,             "fast timing",      "FASTTIME",   &set_mode,  NULL,    NULL       },
    { UNIT_REALTIME, UNIT_REALTIME, "realistic timing", "REALTIME",   &set_mode,  NULL,    NULL       },

/*    Entry Flags          Value  Print String  Match String  Validation    Display        Descriptor        */
/*    -------------------  -----  ------------  ------------  ------------  -------------  ----------------- */
    { MTAB_XDV,             1u,   "SC",         "SC",         &hp_set_dib,  &hp_show_dib,  (void *) &ptp_dib },
    { MTAB_XDV | MTAB_NMO, ~1u,   "DEVNO",      "DEVNO",      &hp_set_dib,  &hp_show_dib,  (void *) &ptp_dib },
    { 0 }
    };


/* Trace list */

static DEBTAB ptp_deb [] = {
    { "CSRW",  TRACE_CSRW  },                   /* trace interface control, status, read, and write actions */
    { "SERV",  TRACE_SERV  },                   /* trace unit service scheduling calls and entries */
    { "XFER",  TRACE_XFER  },                   /* trace data transmissions */
    { "IOBUS", TRACE_IOBUS },                   /* trace I/O bus signals and data words received and returned */
    { NULL,    0           }
    };


/* Device descriptor */

DEVICE ptp_dev = {
    "PTP",                                      /* device name */
    &ptp_unit,                                  /* unit array */
    ptp_reg,                                    /* register array */
    ptp_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &ptp_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    &hp_attach,                                 /* attach routine */
    NULL,                                       /* detach routine */
    &ptp_dib,                                   /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    ptp_deb,                                    /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };

static DEVICE *dptrs [CARD_COUNT] = {           /* device pointers, indexed by CARD_INDEX */
    &ptr_dev,
    &ptp_dev
    };



/* PTR and PTP I/O interface routine */


/* 8-bit duplex interface.

   The duplex interface is installed on the I/O bus and receives I/O commands
   from the CPU and DMA/DCPC channels.  In simulation, the asserted signals on
   the bus are represented as bits in the inbound_signals set.  Each signal is
   processed sequentially in ascending numerical order.

   Two diagnostic modes are provided.  If no file is attached, then the
   interface behaves as though it has a loopback connector installed.  If a file
   is attached, then, for the paper tape reader only, the paper tape image is
   made into a physical tape loop by rewinding it when the EOF is reached.  When
   the loopback connector is installed, data output is connected to data input,
   and Device Control is connected to Device Flag.  Asserting STC schedules
   event service for fast turnaround.  In normal (non-diagnostic) mode, the
   event service routine is scheduled at a time appropriate for the device
   (reader or punch).


   Implementation notes:

    1. The 12597A duplex register cards are used to interface the paper tape
       reader and punch to the computer.  These cards have device command
       flip-flops, which assert the READ and PUNCH signals to the devices.
       Under simulation, these states are implied by the activation of the
       respective units.
*/

static SIGNALS_VALUE pt_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
const CARD_INDEX card = (CARD_INDEX) dibptr->card_index;    /* the card selector */
UNIT * const     uptr = &(pt_unit [card]);                  /* the associated unit pointer */
INBOUND_SIGNAL   signal;
INBOUND_SET      working_set = inbound_signals;
SIGNALS_VALUE    outbound    = { ioNONE, 0 };
t_bool           irq_enabled = FALSE;
int32            delay;

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            pt [card].flag_buffer = CLEAR;              /* reset the flag buffer */
            pt [card].flag        = CLEAR;              /*   and flag flip-flops */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            pt [card].flag_buffer = SET;                /* set the flag buffer flip-flop */
            break;


        case ioENF:                                     /* Enable Flag */
            if (pt [card].flag_buffer == SET)           /* if the flag buffer flip-flop is set */
                pt [card].flag = SET;                   /*   then set the flag flip-flop */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (pt [card].flag == CLEAR)                /* if the flag flip-flop is clear */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (pt [card].flag == SET)                  /* if the flag flip-flop is set */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                                 /* I/O Data Input */
            if (card == ptp && (uptr->flags & UNIT_DIAG) == 0) {    /* if this is the punch interface in punch mode */
                if (uptr->flags & UNIT_ATT)                         /*     then if the unit is attached */
                    pt [ptp].input_data = 0;                        /*       then report that tape is loaded in the punch */
                else                                                /*     otherwise */
                    pt [ptp].input_data = PS_LOW_TAPE;              /*       report that the punch is out of tape */

                tprintf (ptp_dev, TRACE_CSRW, "Status is %s\n",
                         fmt_bitset (pt [ptp].input_data, ptp_status_format));
                }

            outbound.value = (HP_WORD) pt [card].input_data;    /* return the data byte */
            break;


        case ioIOO:                                             /* I/O Data Output */
            pt [card].output_data = LOWER_BYTE (inbound_value); /* save the data byte */
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            pt [card].flag_buffer = SET;                /* set the flag buffer flip-flop */
            pt [card].output_data = 0;                  /*   and clear the output register */
            break;


        case ioCRS:                                     /* Control Reset */
        case ioCLC:                                     /* Clear Control flip-flop */
            pt [card].control = CLEAR;                  /* clear the control flip-flop */
            pt [card].command = CLEAR;                  /*   and the command flip-flop */
            break;


        case ioSTC:                                     /* Set Control flip-flop */
            pt [card].control = SET;                    /* set the control flip-flop */
            pt [card].command = SET;                    /*   and the command flip-flop */

            if ((uptr->flags & (UNIT_DIAG | UNIT_ATT)) == UNIT_DIAG)    /* if the loopback connector is installed */
                delay = PT_DIAG_TIME;                                   /*   then use an immediate turnaround */
            else                                                        /* otherwise */
                delay = uptr->wait;                                     /*   use the normal device delay */

            sim_activate (uptr, delay);                 /* schedule the event */

            tpprintf (dptrs [card], TRACE_SERV, "Unit delay %d service scheduled\n",
                      delay);
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (pt [card].control & pt [card].flag)     /* if the control and flag flip-flops are set */
                outbound.signals |= cnVALID;            /*   then deny PRL */
            else                                        /* otherwise */
                outbound.signals |= cnPRL | cnVALID;    /*   conditionally assert PRL */

            if (pt [card].control & pt [card].flag      /* if control and flag */
              & pt [card].flag_buffer)                  /*   and flag buffer flip-flops are set */
                outbound.signals |= cnIRQ | cnVALID;    /*     then conditionally assert IRQ */

            if (pt [card].flag == SET)                  /* if the flag flip-flop is set */
                outbound.signals |= ioSRQ;              /*   then assert SRQ */
            break;


        case ioIAK:                                     /* Interrupt Acknowledge */
            pt [card].flag_buffer = CLEAR;              /* clear the flag buffer flip-flop */
            break;


        case ioIEN:                                     /* Interrupt Enable */
            irq_enabled = TRUE;                         /* permit IRQ to be asserted */
            break;


        case ioPRH:                                         /* Priority High */
            if (irq_enabled && outbound.signals & cnIRQ)    /* if IRQ is enabled and conditionally asserted */
                outbound.signals |= ioIRQ | ioFLG;          /*   then assert IRQ and FLG */

            if (!irq_enabled || outbound.signals & cnPRL)   /* if IRQ is disabled or PRL is conditionally asserted */
                outbound.signals |= ioPRL;                  /*   then assert it unconditionally */
            break;


        case ioEDT:                                     /* not used by this interface */
        case ioPON:                                     /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }                                                   /*   and continue until all signals are processed */

return outbound;                                        /* return the outbound signals and value */
}



/* Interface local SCP support routines */


/* Set the timing mode.

   This validation routine is called to set the timing mode to realistic or
   optimized timing.  On entry, the "uptr" parameter points to either the reader
   unit or the punch unit, and the "value" parameter is UNIT_REALTIME if real
   time mode is being set and zero if optimized ("fast") timing mode is being
   set.  The character and descriptor pointers are not used.
*/

static t_stat set_mode (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
if (uptr == &ptr_unit)                                  /* if the reader mode is being set */
    if (value == 0)                                     /*   then if optimized timing is desired */
        uptr->wait = fast_read_time;                    /*     then use the current fast time setting */
    else                                                /*   otherwise */
        uptr->wait = PTR_REAL_TIME;                     /*     use the hardware operation time */

else                                                    /* otherwise the punch mode is being set */
    if (value == 0)                                     /*   so if optimized timing is desired */
        uptr->wait = fast_punch_time;                   /*     then use the current fast time setting */
    else                                                /*   otherwise */
        uptr->wait = PTP_REAL_TIME;                     /*     use the hardware operation time */

return SCPE_OK;                                         /* setting the mode always succeeds */
}



/* Paper tape reader local SCP support routines */


/* Reset the paper tape reader.

   This routine is called for a RESET, RESET PTR, RUN, or BOOT command.  It is
   the simulation equivalent of an initial power-on condition (corresponding to
   PON, POPIO, and CRS signal assertion in the CPU) or a front-panel PRESET
   button press (corresponding to POPIO and CRS assertion).  SCP delivers a
   power-on reset to all devices when the simulator is started.

   If this is a power-on reset, the default optimized output time is restored.
   In either case, POPIO is asserted to the interface, and any read operation in
   progress is cancelled.
*/

static t_stat ptr_reset (DEVICE *dptr)
{
if (sim_switches & SWMASK ('P'))                        /* if this is a power-on reset */
    fast_read_time = PTR_FAST_TIME;                     /*   then restore the initial fast data time */

io_assert (dptr, ioa_POPIO);                            /* PRESET the device */
sim_cancel (&ptr_unit);                                 /*   and cancel any read in progress */

return SCPE_OK;                                         /* device reset always succeeds */
}


/* Attach the paper tape image file.

   The file whose name is indicated by the "cptr" parameter is attached to the
   reader unit.  This is the simulation equivalent of loading a punched paper
   tape into the reader and pressing the READ button.

   Loading a new tape clears the trailing NUL counter to enable proper EOT
   detection.
*/

static t_stat ptr_attach (UNIT *uptr, CONST char *cptr)
{
ptr_trlcnt = 0;                                         /* clear the trailing NUL counter */
return attach_unit (uptr, cptr);                        /*   and attached the indicated file */
}


/* Paper tape reader bootstrap loaders (BBL and 12992K).

   The Basic Binary Loader (BBL) performs three functions, depending on the
   setting of the S register, as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | C | -   -   -   -   -   -   -   -   -   -   -   -   -   - | V |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     C = Compare the paper tape to memory
     V = Verify checksums on the paper tape

   If bit 15 is set to 1, the loader will compare the absolute program on tape
   to the contents of memory.  If bit 0 is set to 1, the loader will verify the
   checksums of the absolute binary records on tape without altering memory.  If
   neither bit is set, the loader will read the absolute program on the paper
   tape into memory.  Loader execution ends with one of the following halt
   instructions:

     * HLT 00 - a comparison error occurred; A = the tape value.
     * HLT 11 - a checksum error occurred; A/B = the tape/calculated value.
     * HLT 55 - the program load address would overlay the loader.
     * HLT 77 - the end of tape was reached with a successful read.

   The 12992K boot loader ROM reads an absolute program on the paper tape into
   memory.  The S register setting does not affect loader operation.  Loader
   execution ends with one of the following halt instructions:

     * HLT 11 - a checksum error occurred; A/B = the calculated/tape value.
     * HLT 55 - the program load address would overlay the ROM loader.
     * HLT 77 - the end of tape was reached with a successful read.

   Note that the A/B register contents are in the opposite order of those in the
   BBL when a checksum error occurs.
*/

static const LOADER_ARRAY ptr_loaders = {
    {                               /* HP 21xx Basic Binary Loader (BBL) */
      000,                          /*   loader starting index */
      IBL_NA,                       /*   DMA index (not used) */
      072,                          /*   FWA index */
      { 0107700,                    /*   77700:  START CLC 0,C      */
        0063770,                    /*   77701:        LDA 77770    */
        0106501,                    /*   77702:        LIB 1        */
        0004010,                    /*   77703:        SLB          */
        0002400,                    /*   77704:        CLA          */
        0006020,                    /*   77705:        SSB          */
        0063771,                    /*   77706:        LDA 77771    */
        0073736,                    /*   77707:        STA 77736    */
        0006401,                    /*   77710:        CLB,RSS      */
        0067773,                    /*   77711:        LDB 77773    */
        0006006,                    /*   77712:        INB,SZB      */
        0027717,                    /*   77713:        JMP 77717    */
        0107700,                    /*   77714:        CLC 0,C      */
        0102077,                    /*   77715:        HLT 77       */
        0027700,                    /*   77716:        JMP 77700    */
        0017762,                    /*   77717:        JSB 77762    */
        0002003,                    /*   77720:        SZA,RSS      */
        0027712,                    /*   77721:        JMP 77712    */
        0003104,                    /*   77722:        CMA,CLE,INA  */
        0073774,                    /*   77723:        STA 77774    */
        0017762,                    /*   77724:        JSB 77762    */
        0017753,                    /*   77725:        JSB 77753    */
        0070001,                    /*   77726:        STA 1        */
        0073775,                    /*   77727:        STA 77775    */
        0063775,                    /*   77730:        LDA 77775    */
        0043772,                    /*   77731:        ADA 77772    */
        0002040,                    /*   77732:        SEZ          */
        0027751,                    /*   77733:        JMP 77751    */
        0017753,                    /*   77734:        JSB 77753    */
        0044000,                    /*   77735:        ADB 0        */
        0000000,                    /*   77736:        NOP          */
        0002101,                    /*   77737:        CLE,RSS      */
        0102000,                    /*   77740:        HLT 0        */
        0037775,                    /*   77741:        ISZ 77775    */
        0037774,                    /*   77742:        ISZ 77774    */
        0027730,                    /*   77743:        JMP 77730    */
        0017753,                    /*   77744:        JSB 77753    */
        0054000,                    /*   77745:        CPB 0        */
        0027711,                    /*   77746:        JMP 77711    */
        0102011,                    /*   77747:        HLT 11       */
        0027700,                    /*   77750:        JMP 77700    */
        0102055,                    /*   77751:        HLT 55       */
        0027700,                    /*   77752:        JMP 77700    */
        0000000,                    /*   77753:        NOP          */
        0017762,                    /*   77754:        JSB 77762    */
        0001727,                    /*   77755:        ALF,ALF      */
        0073776,                    /*   77756:        STA 77776    */
        0017762,                    /*   77757:        JSB 77762    */
        0033776,                    /*   77760:        IOR 77776    */
        0127753,                    /*   77761:        JMP 77753,I  */
        0000000,                    /*   77762:        NOP          */
        0103710,                    /*   77763:        STC 10,C     */
        0102310,                    /*   77764:        SFS 10       */
        0027764,                    /*   77765:        JMP 77764    */
        0102510,                    /*   77766:        LIA 10       */
        0127762,                    /*   77767:        JMP 77762,I  */
        0173775,                    /*   77770:        STA 77775,I  */
        0153775,                    /*   77771:        CPA 77775,I  */
        0100100,                    /*   77772:        RRL 16       */
        0177765,                    /*   77773:        STB 77765,I  */
        0000000,                    /*   77774:        NOP          */
        0000000,                    /*   77775:        NOP          */
        0000000,                    /*   77776:        NOP          */
        0000000 } },                /*   77777:        NOP          */

    {                               /* HP 1000 Loader ROM (12992K) */
      IBL_START,                    /*   loader starting index */
      IBL_DMA,                      /*   DMA index */
      IBL_FWA,                      /*   FWA index */
      { 0107700,                    /*   77700:  ST    CLC 0,C            ; intr off */
        0002401,                    /*   77701:        CLA,RSS            ; skip in */
        0063756,                    /*   77702:  CN    LDA M11            ; feed frame */
        0006700,                    /*   77703:        CLB,CCE            ; set E to rd byte */
        0017742,                    /*   77704:        JSB READ           ; get #char */
        0007306,                    /*   77705:        CMB,CCE,INB,SZB    ; 2's comp */
        0027713,                    /*   77706:        JMP *+5            ; non-zero byte */
        0002006,                    /*   77707:        INA,SZA            ; feed frame ctr */
        0027703,                    /*   77710:        JMP *-3            */
        0102077,                    /*   77711:        HLT 77B            ; stop */
        0027700,                    /*   77712:        JMP ST             ; next */
        0077754,                    /*   77713:        STA WC             ; word in rec */
        0017742,                    /*   77714:        JSB READ           ; get feed frame */
        0017742,                    /*   77715:        JSB READ           ; get address */
        0074000,                    /*   77716:        STB 0              ; init csum */
        0077755,                    /*   77717:        STB AD             ; save addr */
        0067755,                    /*   77720:  CK    LDB AD             ; check addr */
        0047777,                    /*   77721:        ADB MAXAD          ; below loader */
        0002040,                    /*   77722:        SEZ                ; E =0 => OK */
        0027740,                    /*   77723:        JMP H55            */
        0017742,                    /*   77724:        JSB READ           ; get word */
        0040001,                    /*   77725:        ADA 1              ; cont checksum */
        0177755,                    /*   77726:        STA AD,I           ; store word */
        0037755,                    /*   77727:        ISZ AD             */
        0000040,                    /*   77730:        CLE                ; force wd read */
        0037754,                    /*   77731:        ISZ WC             ; block done? */
        0027720,                    /*   77732:        JMP CK             ; no */
        0017742,                    /*   77733:        JSB READ           ; get checksum */
        0054000,                    /*   77734:        CPB 0              ; ok? */
        0027702,                    /*   77735:        JMP CN             ; next block */
        0102011,                    /*   77736:        HLT 11             ; bad csum */
        0027700,                    /*   77737:        JMP ST             ; next */
        0102055,                    /*   77740:  H55   HLT 55             ; bad address */
        0027700,                    /*   77741:        JMP ST             ; next */
        0000000,                    /*   77742:  RD    NOP                */
        0006600,                    /*   77743:        CLB,CME            ; E reg byte ptr */
        0103710,                    /*   77744:        STC RDR,C          ; start reader */
        0102310,                    /*   77745:        SFS RDR            ; wait */
        0027745,                    /*   77746:        JMP *-1            */
        0106410,                    /*   77747:        MIB RDR            ; get byte */
        0002041,                    /*   77750:        SEZ,RSS            ; E set? */
        0127742,                    /*   77751:        JMP RD,I           ; no, done */
        0005767,                    /*   77752:        BLF,CLE,BLF        ; shift byte */
        0027744,                    /*   77753:        JMP RD+2           ; again */
        0000000,                    /*   77754:  WC    000000             ; word count */
        0000000,                    /*   77755:  AD    000000             ; address */
        0177765,                    /*   77756:  M11   DEC -11            ; feed count */
        0000000,                    /*   77757:        NOP                */
        0000000,                    /*   77760:        NOP                */
        0000000,                    /*   77761:        NOP                */
        0000000,                    /*   77762:        NOP                */
        0000000,                    /*   77763:        NOP                */
        0000000,                    /*   77764:        NOP                */
        0000000,                    /*   77765:        NOP                */
        0000000,                    /*   77766:        NOP                */
        0000000,                    /*   77767:        NOP                */
        0000000,                    /*   77770:        NOP                */
        0000000,                    /*   77771:        NOP                */
        0000000,                    /*   77772:        NOP                */
        0000000,                    /*   77773:        NOP                */
        0000000,                    /*   77774:        NOP                */
        0000000,                    /*   77775:        NOP                */
        0000000,                    /*   77776:        NOP                */
        0100100 } }                 /*   77777:  MAXAD ABS -ST            ; max addr */
    };


/* Device boot routine.

   This routine is called directly by the BOOT PTR and LOAD PTR commands to copy
   the device bootstrap into the upper 64 words of the logical address space.
   It is also called indirectly by a BOOT CPU or LOAD CPU command when the
   specified HP 1000 loader ROM socket contains a 12992K ROM.

   When called in response to a BOOT PTR or LOAD PTR command, the "unitno"
   parameter indicates the unit number specified in the BOOT command or is zero
   for the LOAD command, and "dptr" points at the PTR device structure.
   Depending on the current CPU model, the BBL or 12992K loader ROM will be
   copied into memory and configured for the PTR select code.  If the CPU is a
   1000, the S register will be set as it would be by the front-panel microcode.

   When called for a BOOT/LOAD CPU command, the "unitno" parameter indicates the
   select code to be used for configuration, and "dptr" will be NULL.  As above,
   the BBL or 12992K loader ROM will be copied into memory and configured for
   the specified select code.  The S register is assumed to be set correctly on
   entry and is not modified.

   For the 12992K boot loader ROM, the S register will be set as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | ROM # | 0   0 |    PTR select code    | 0   0   0   0   0   0 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

static t_stat ptr_boot (int32 unitno, DEVICE *dptr)
{
uint32 start;

if (dptr == NULL)                                               /* if we are being called for a BOOT/LOAD CPU */
    start = cpu_copy_loader (ptr_loaders, unitno,               /*   then copy the boot loader to memory */
                             IBL_S_NOCLEAR, IBL_S_NOSET);       /*     but do not alter the S register */

else                                                            /* otherwise this is a BOOT/LOAD PTR */
    start = cpu_copy_loader (ptr_loaders, ptr_dib.select_code,  /*   so copy the boot loader to memory */
                             IBL_S_CLEAR, IBL_S_NOSET);         /*     and configure the S register if 1000 CPU */

if (start == 0)                                         /* if the copy failed */
    return SCPE_NOFNC;                                  /*   then reject the command */
else                                                    /* otherwise */
    return SCPE_OK;                                     /*   the boot loader was successfully copied */
}



/* Paper tape reader local utility routines */


/* Paper tape reader service routine.

   This routine is scheduled by asserting STC to the interface and is entered to
   read one byte from the paper tape image file.  If no file is attached, then
   if the diagnostic mode is active, then the interface currently has a loopback
   connector installed, so copy the data in the output register to the input
   data register and set the device flag.  If diagnostic mode is not active,
   then an attempt is made to read with no tape in the reader.  In hardware,
   this causes the interface handshake to hang.  This occurs in simulation as
   well, unless a SET CPU STOP=IOERR has been done.  If it has, then the event
   service is rescheduled, a simulation error occurs, and control returns to the
   SCP prompt.  At that point, the reader may be attached and execution resumed
   to read from the specified tape.

   Assuming that the unit is attached, the next byte from the file is read.  If
   the tape is positioned at the physical EOF, then if diagnostic mode is
   enabled, then the paper tape image is made into a physical tape loop by
   resetting the file position to the start of the file.  Otherwise, the
   trailing loop counter is incremented if it is currently less than the limit,
   and a NUL byte is returned.  If the limit has been exceeded, then the reader
   hangs or causes a simulator stop as described above; this corresponds in
   hardware to a tape that has run out of the reader.

   If a byte was successfully read, then it is placed in the input data
   register, and the device flag is set.  If the byte is not a NUL, then the
   trailing NUL counter is reset.
*/

static t_stat ptr_service (UNIT *uptr)
{
int byte;

tprintf (ptr_dev, TRACE_SERV, "Reader service entered\n");

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the reader is not attached */
    if (uptr->flags & UNIT_DIAG) {                      /*   then if the card is in diagnostic mode */
        pt [ptr].input_data = pt [ptr].output_data;     /*     then loop the data back */

        pt [ptr].flag_buffer = SET;                     /* set the flag buffer */
        io_assert (&ptr_dev, ioa_ENF);                  /*   and enable the device flag */
        return SCPE_OK;                                 /*     and return with the operation complete */
        }

    else if (cpu_io_stop (uptr)) {                      /* otherwise if the I/O error stop is enabled */
        sim_activate (uptr, uptr->wait);                /*   then reschedule the operation */
        return STOP_NOTAPE;                             /*     and report that the tape isn't loaded */
        }

    else                                                /* otherwise no tape in the reader */
        return SCPE_OK;                                 /*   just hangs the input operation */

byte = fgetc (uptr->fileref);                           /* get the next byte from the paper tape file */

if (feof (uptr->fileref))                               /* if the file is positioned at the EOF */
    if (uptr->flags & UNIT_DIAG && uptr->pos > 0) {     /*   then if DIAG mode is enabled and the tape isn't empty */
        rewind (uptr->fileref);                         /*     then rewind the tape */
        uptr->pos = 0;                                  /*       to simulate loop mode */

        byte = fgetc (uptr->fileref);                   /* get the first byte from the tape */
        }

    else                                                /* otherwise READER mode is enabled or the tape is empty */
        if (ptr_trlcnt < ptr_trllim) {                  /*   so if trailer remains to be added */
            ptr_trlcnt++;                               /*     then count the trailer byte */
            byte = NUL;                                 /*       and return a NUL */
            }

        else if (cpu_io_stop (uptr)) {                  /* otherwise trailer is complete; if the I/O stop is enabled */
            sim_activate (uptr, uptr->wait);            /*   then reschedule the operation */
            return STOP_EOT;                            /*     and report that the tape is at EOF */
            }

        else                                            /* otherwise tape exhaustion */
            return SCPE_OK;                             /*   just hangs the input operation */

if (ferror (uptr->fileref)) {                                   /* if a host file I/O error occurred */
    cprintf ("%s simulator paper tape reader I/O error: %s\n",  /*   then report the error to the console */
             sim_name, strerror (errno));

    clearerr (uptr->fileref);                           /* clear the error */
    return SCPE_IOERR;                                  /*   and stop the simulator */
    }

else {                                                  /* otherwise the read was successful */
    pt [ptr].input_data = byte & D8_MASK;               /*   so put the byte in the input register */
    uptr->pos = ftell (uptr->fileref);                  /*     and update the file position */

    if (byte != NUL)                                    /* if the byte is not a NUL */
        ptr_trlcnt = 0;                                 /*   then clear the trailing NUL counter */

    tprintf (ptr_dev, TRACE_XFER, "Data %03o character %s received from reader\n",
             pt [ptr].input_data, fmt_char (pt [ptr].input_data));

    pt [ptr].flag_buffer = SET;                         /* set the flag buffer */
    io_assert (&ptr_dev, ioa_ENF);                      /*   and enable the device flag */

    return SCPE_OK;                                     /*   and return success */
    }
}



/* Paper tape punch local SCP support routines */


/* Reset the paper tape punch.

   This routine is called for a RESET, RESET PTP, RUN, or BOOT command.  It is
   the simulation equivalent of an initial power-on condition (corresponding to
   PON, POPIO, and CRS signal assertion in the CPU) or a front-panel PRESET
   button press (corresponding to POPIO and CRS assertion).  SCP delivers a
   power-on reset to all devices when the simulator is started.

   If this is a power-on reset, the default optimized output time is restored.
   In either case, POPIO is asserted to the interface, and any punch operation
   in progress is cancelled.
*/

static t_stat ptp_reset (DEVICE *dptr)
{
if (sim_switches & SWMASK ('P'))                        /* if this is a power-on reset */
    fast_punch_time = PTP_FAST_TIME;                    /*   then restore the initial fast data time */

io_assert (dptr, ioa_POPIO);                            /* PRESET the device */
sim_cancel (&ptp_unit);                                 /*   and cancel any punch in progress */

return SCPE_OK;                                         /* device reset always succeeds */
}



/* Paper tape punch local utility routines */


/* Paper tape punch service routine.

   This routine is scheduled by asserting STC to the interface and is entered to
   punch one byte to the paper tape image file.  If the diagnostic mode is
   active, then the interface currently has a loopback connector installed, so
   copy the data in the output register to the input data register and set the
   device flag.  If diagnostic mode is not active, then if the paper tape image
   file is attached, then an attempt is made to write the data byte to the file.

   If the write succeeds, then the device flag is set.  Otherwise, the error is
   written to the simulation console, and execution stops.
*/

static t_stat ptp_service (UNIT *uptr)
{
tprintf (ptp_dev, TRACE_SERV, "Punch service entered\n");

if (uptr->flags & UNIT_DIAG) {                          /* if the card is in diagnostic mode */
    pt [ptp].input_data = pt [ptp].output_data;         /*   then loop the data back */

    pt [ptp].flag_buffer = SET;                         /* set the flag buffer */
    io_assert (&ptp_dev, ioa_ENF);                      /*   and enable the device flag */
    }

else if (uptr->flags & UNIT_ATT)                                    /* if the punch is attached */
    if (fputc (pt [ptp].output_data, uptr->fileref) == EOF) {       /*   then write the byte; if the write fails */
        cprintf ("%s simulator paper tape punch I/O error: %s\n",   /*     then report the error to the console */
                 sim_name, strerror (errno));

        clearerr (uptr->fileref);                       /* clear the error */
        return SCPE_IOERR;                              /*   and stop the simulator */
        }

    else {                                              /* otherwise the write succeeds */
        uptr->pos = ftell (uptr->fileref);              /*   so update the file position */

        tprintf (ptp_dev, TRACE_XFER, "Data %03o character %s sent to punch\n",
                 pt [ptp].output_data, fmt_char (pt [ptp].output_data));

        pt [ptp].flag_buffer = SET;                     /* set the flag buffer */
        io_assert (&ptp_dev, ioa_ENF);                  /*   and enable the device flag */
        }

return SCPE_OK;
}
