/* hp2100_tty.c: HP 12531C Buffered Teleprinter Interface simulator

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

   TTY          12531C Buffered Teleprinter Interface

   22-Sep-18    JDB     RESET -P now resets to original FASTTIME timing
   29-Jun-18    JDB     TTY may now be disabled; added "set_endis" routine
   27-Jun-18    JDB     Added REALTIME/FASTTIME modes and tracing
   05-Jun-18    JDB     Revised I/O model
   04-Jun-18    JDB     Split out from hp2100_stddev.c
                        Trimmed revisions to current file applicability
   22-Nov-17    JDB     Fixed TTY serial output buffer overflow handling
   03-Aug-17    JDB     PTP and TTY now append to existing file data
   01-May-17    JDB     Deleted ttp_stopioe, as a detached punch is no longer an error
   17-Jan-17    JDB     Changed "hp_---sc" and "hp_---dev" to "hp_---_dib"
   30-Dec-16    JDB     Modified the TTY to print if the punch is not attached
   13-May-16    JDB     Modified for revised SCP API function parameter types
   24-Dec-14    JDB     Added casts for explicit downward conversions
   18-Dec-12    MP      Now calls sim_activate_time to get remaining poll time
   09-May-12    JDB     Separated assignments from conditional expressions
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   25-Apr-08    JDB     Changed TTY output wait from 100 to 200 for MSU BASIC
   18-Apr-08    JDB     Removed redundant control char handling definitions
   14-Apr-08    JDB     Changed TTY console poll to 10 msec. real time
                        Added UNIT_IDLE to TTY and CLK
   31-Dec-07    JDB     Corrected and verified ioCRS actions
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   22-Nov-05    RMS     Revised for new terminal processing routines
   15-Aug-04    RMS     Added tab to control char set (from Dave Bryan)
   14-Jul-04    RMS     Generalized handling of control char echoing
                        (from Dave Bryan)
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Fixed input behavior during typeout for RTE-IV
                        Suppressed nulls on TTY output for RTE-IV
                        Implemented DMA SRQ (follows FLG)
   29-Mar-03    RMS     Added support for console backpressure
   22-Dec-02    RMS     Added break support
   01-Nov-02    RMS     Fixed bug in TTY reset, TTY starts in input mode
                        Fixed bug in TTY mode OTA, stores data as well
                        Added UC option to TTY output
   30-May-02    RMS     Widened POS to 32b
   22-Mar-02    RMS     Revised for dynamically allocated memory
   03-Nov-01    RMS     Changed DEVNO to use extended SET/SHOW
   29-Nov-01    RMS     Added read only unit support
   07-Sep-01    RMS     Moved function prototypes
   21-Nov-00    RMS     Fixed flag, buffer power up state
                        Added status input for ptp, tty
   15-Oct-00    RMS     Added dynamic device number support

   References:
     - 12531C Buffered Teleprinter Interface Kit Operating and Service Manual
         (12531-90033, November 1972)


   The 12531C Buffered Teleprinter Interface connects current-loop devices, such
   as the HP 2752A (ASR33) and 2754A (ASR35) teleprinters, as well as EIA RS-232
   devices, such as the HP 2749A (ASR33) teleprinter and HP 2600 terminal, to
   the CPU.  Teleprinters are often used as the system consoles for the various
   HP operating systems.  By default, the system console is connected to the
   simulation console, so that SCP and operating system commands may be entered
   from the same window.

   The interface responds to I/O instructions as follows:

   Output Data Word format (OTA and OTB):

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   - |       output character        | data
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | I | P | N | -   -   -   -   -   -   -   -   -   -   -   - | control
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     I = set the interface to output/input mode (0/1)
     P = enable the printer for output
     N = enable the punch for output


   Input Data Word format (LIA and LIB):

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | B | -   -   -   -   -   -   - |        input character        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     B = interface is idle/busy (0/1)

   To support CPU idling, the teleprinter interface polls for input by
   coscheduling with the system poll timer, a calibrated timer with a ten
   millisecond period.  Other polled-keyboard input devices (multiplexers and
   the BACI card) synchronize with the poll timer to ensure maximum available
   idle time.
*/



#include "hp2100_defs.h"
#include "hp2100_io.h"



/* TTY program constants */

#define NUL                 '\000'              /* null */
#define CR                  '\r'                /* carriage return */
#define LF                  '\n'                /* line feed */
#define MARK                '\377'              /* all bits marking */


/* TTY device flags */

#define DEV_REALTIME_SHIFT  (DEV_V_UF + 0)              /* realistic timing mode */

#define DEV_REALTIME        (1u << DEV_REALTIME_SHIFT)  /* realistic timing mode flag */


/* TTY unit flags */

#define UNIT_AUTOLF_SHIFT   (TTUF_V_UF + 0)             /* automatic line feed mode */

#define UNIT_AUTOLF         (1u << UNIT_AUTOLF_SHIFT)   /* automatic line feed mode flag */


/* TTY unit references */

typedef enum {
    keyboard,                                   /* teleprinter keyboard unit index */
    printer,                                    /* teleprinter printer unit index */
    punch                                       /* teleprinter punch unit index */
    } UNIT_INDEX;


/* TTY device properties */

#define TTY_FAST_TIME       200                 /* teleprinter optimized timing delay */
#define TTY_REAL_TIME       mS (100)            /* teleprinter realistic timing delay */


/* TTY output data and control words.

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   - |       output character        | data
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | I | P | N | -   -   -   -   -   -   -   -   -   -   -   - | control
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define CN_CONTROL      0100000u                /* output is a data/control (0/1) word */
#define CN_INPUT        0040000u                /* set the interface to output/input (0/1) mode */
#define CN_PRINT        0020000u                /* enable the printer for output */
#define CN_PUNCH        0010000u                /* enable the punch for output */

static const BITSET_NAME tty_control_names [] = {       /* Teleprinter control word names */
    "\1input\0output",                                  /*   bit 14 */
    "printer enabled",                                  /*   bit 13 */
    "punch enabled"                                     /*   bit 12 */
    };

static const BITSET_FORMAT tty_control_format =         /* names, offset, direction, alternates, bar */
    { FMT_INIT (tty_control_names, 12, msb_first, has_alt, no_bar) };


/* TTY input data word.

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | B | -   -   -   -   -   -   - |        input character        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define ST_BUSY         0100000u                /* interface is idle/busy (0/1) */

static const BITSET_NAME tty_status_names [] = {        /* Teleprinter status word names */
    "\1busy\0idle"                                      /*   bit 15 */
    };

static const BITSET_FORMAT tty_status_format =          /* names, offset, direction, alternates, bar */
    { FMT_INIT (tty_status_names, 15, msb_first, has_alt, no_bar) };


/* TTY local state declarations */

typedef struct {
    char         io_data;                       /* input/output data register */
    char         shift_in_data;                 /* shift-in data */
    t_bool       cr_seen;                       /* carriage return has been seen on input */
    HP_WORD      mode;                          /* control mode register */

    FLIP_FLOP    control;                       /* control flip-flop */
    FLIP_FLOP    flag;                          /* flag flip-flop */
    FLIP_FLOP    flag_buffer;                   /* flag buffer flip-flop */
    } CARD_STATE;

static CARD_STATE tty;                          /* per-card state */

static int32 fast_data_time = TTY_FAST_TIME;    /* fast receive/send time */


/* TTY I/O interface routine declarations */

static INTERFACE tty_interface;


/* TTY local SCP support routine declarations */

static t_stat set_filter (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat set_auto   (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat set_mode   (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat set_endis  (UNIT *uptr, int32 value, CONST char *cptr, void *desc);

static t_stat show_mode (FILE *st, UNIT *uptr, int32 value, CONST void *desc);

static t_stat tty_reset (DEVICE *dptr);


/* TTY local utility routine declarations */

static t_stat keyboard_service    (UNIT *uptr);
static t_stat print_punch_service (UNIT *uptr);
static t_stat output              (int32 character);


/* TTY SCP data declarations */

/* Unit lists.


   Implementation notes:

    1. The print unit service handles both printing and punching.  The punch
       unit is never scheduled, so its event routine is NULL.
*/

static UNIT tty_unit [] = {
/*           Event Routine         Unit Flags                            Capacity  Delay         */
/*           --------------------  ------------------------------------  --------  ------------- */
    { UDATA (&keyboard_service,    TT_MODE_UC | UNIT_IDLE,                  0),    POLL_PERIOD   },
    { UDATA (&print_punch_service, TT_MODE_UC,                              0),    TTY_FAST_TIME },
    { UDATA (NULL,                 TT_MODE_8B | UNIT_SEQ | UNIT_ATTABLE,    0),    0             }
    };

#define key_unit            tty_unit [keyboard] /* teleprinter keyboard unit  */
#define print_unit          tty_unit [printer]  /* teleprinter printer unit */
#define punch_unit          tty_unit [punch]    /* teleprinter punch unit */


/* Device information block */

static DIB tty_dib = {
    &tty_interface,                             /* the device's I/O interface function pointer */
    TTY,                                        /* the device's select code (02-77) */
    0,                                          /* the card index */
    "12531C Buffered Teleprinter Interface",    /* the card description */
    NULL                                        /* the ROM description */
    };


/* Register list */

static REG tty_reg [] = {
/*    Macro   Name    Location                  Radix   Width     Offset      Depth            Flags       */
/*    ------  ------  ------------------------  ----- ----------  ------  -------------  ----------------- */
    { ORDATA (BUF,    tty.io_data,                        8)                                               },
    { GRDATA (MODE,   tty.mode,                   2,      3,       12),                   PV_RZRO          },
    { ORDATA (SHIN,   tty.shift_in_data,                  8),                             REG_HRO          },
    { FLDATA (CTL,    tty.control,                                  0)                                     },
    { FLDATA (FLG,    tty.flag,                                     0)                                     },
    { FLDATA (FBF,    tty.flag_buffer,                              0)                                     },
    { FLDATA (KLFP,   tty.cr_seen,                                  0),                   REG_HRO          },
    { DRDATA (KPOS,   key_unit.pos,                   T_ADDR_W),                          PV_LEFT          },
    { DRDATA (TPOS,   print_unit.pos,                 T_ADDR_W),                          PV_LEFT          },
    { DRDATA (PPOS,   punch_unit.pos,                 T_ADDR_W),                          PV_LEFT          },
    { DRDATA (TTIME,  fast_data_time,                    24),                             PV_LEFT | REG_NZ },

      DIB_REGS (tty_dib),

    { NULL }
    };


/* Modifier list */

static MTAB tty_mod [] = {
/*    Mask Value   Match Value  Print String     Match String  Validation   Display  Descriptor */
/*    -----------  -----------  ---------------  ------------  -----------  -------  ---------- */
    { TT_MODE,     TT_MODE_UC,  "UC",            "UC",         &set_filter, NULL,    NULL       },
    { TT_MODE,     TT_MODE_7B,  "7b",            "7B",         &set_filter, NULL,    NULL       },
    { TT_MODE,     TT_MODE_8B,  "8b",            "8B",         &set_filter, NULL,    NULL       },
    { TT_MODE,     TT_MODE_7P,  "7p",            "7P",         &set_filter, NULL,    NULL       },

    { UNIT_AUTOLF, UNIT_AUTOLF, "auto linefeed", "AUTOLF",     &set_auto,   NULL,    NULL       },
    { UNIT_AUTOLF, 0,           NULL,            "NOAUTOLF",   &set_auto,   NULL,    NULL       },

/*    Entry Flags          Value         Print String  Match String  Validation    Display       Descriptor        */
/*    -------------------  ------------  ------------  ------------  ------------  ------------  ----------------- */
    { MTAB_XDV,            0,            NULL,         "FASTTIME",   &set_mode,    NULL,         NULL              },
    { MTAB_XDV,            DEV_REALTIME, NULL,         "REALTIME",   &set_mode,    NULL,         NULL              },
    { MTAB_XDV,            0,            "MODES",      NULL,         NULL,         &show_mode,   NULL              },

    { MTAB_XDV | MTAB_NMO,  1,           NULL,         "ENABLED",    &set_endis,   NULL,         NULL              },
    { MTAB_XDV | MTAB_NMO,  0,           NULL,         "DISABLED",   &set_endis,   NULL,         NULL              },

    { MTAB_XDV,             1u,          "SC",         "SC",         &hp_set_dib,  &hp_show_dib, (void *) &tty_dib },
    { MTAB_XDV | MTAB_NMO, ~1u,          "DEVNO",      "DEVNO",      &hp_set_dib,  &hp_show_dib, (void *) &tty_dib },
    { 0 }
    };


/* Trace list */

static DEBTAB tty_deb [] = {
    { "CSRW",  TRACE_CSRW  },                   /* trace interface control, status, read, and write actions */
    { "SERV",  TRACE_SERV  },                   /* trace unit service scheduling calls and entries */
    { "PSERV", TRACE_PSERV },                   /* trace periodic unit service scheduling calls and entries */
    { "XFER",  TRACE_XFER  },                   /* trace data transmissions */
    { "IOBUS", TRACE_IOBUS },                   /* trace I/O bus signals and data words received and returned */
    { NULL,    0           }
    };


/* Device descriptor */

DEVICE tty_dev = {
    "TTY",                                      /* device name */
    tty_unit,                                   /* unit array */
    tty_reg,                                    /* register array */
    tty_mod,                                    /* modifier array */
    3,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &tty_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    &hp_attach,                                 /* attach routine */
    NULL,                                       /* detach routine */
    &tty_dib,                                   /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    tty_deb,                                    /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* TTY I/O interface routine */


/* Teleprinter interface.

   The teleprinter interface is installed on the I/O bus and receives I/O
   commands from the CPU and DMA/DCPC channels.  In simulation, the asserted
   signals on the bus are represented as bits in the inbound_signals set.  Each
   signal is processed sequentially in ascending numerical order.

   The interface mode (output or input) determines whether STC transmits the
   content of the output register or merely enables reception interrupts.  While
   in output mode, the input shift register is active and will reflect the state
   of the serial input line while printing occurs.  RTE uses this behavior to
   detect a keypress during output on the system console and so to initiate
   command input.


   Implementation notes:

    1. The print unit service time has to be set on each output request, even
       though the time does not change unless a SET TTY REALTIME/FASTTIME
       command is issued.  We want to be able to respond immediately to a change
       in the FASTTIME register setting via a DEPOSIT TTY TTIME <new-time>
       command without requiring a subsequent SET TTY FASTTIME command to
       recognize the change.  However, there is no VM notification that the
       value was changed unless all DEPOSIT commands are intercepted.  The
       straightforward solution of having SET change a pointer to the selected
       value, i.e., to point at the REALTIME value or the FASTTIME value, and
       then using that pointer indirectly to get the service time won't work
       across a SAVE/RESTORE.  The pointer itself cannot be saved, as the
       restoration might be done on a different SIMH version, and there's no VM
       notification that a RESTORE was done unless the device was attached when
       the SAVE was done, so there's no way to set the pointer during a RESTORE.
*/

static SIGNALS_VALUE tty_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };
t_bool         irq_enabled = FALSE;

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            tty.flag_buffer = CLEAR;                    /* reset the flag buffer */
            tty.flag        = CLEAR;                    /*   and flag flip-flops */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            tty.flag_buffer = SET;                      /* set the flag buffer flip-flop */
            break;


        case ioENF:                                     /* Enable Flag */
            if (tty.flag_buffer == SET)                 /* if the flag buffer flip-flop is set */
                tty.flag = SET;                         /*   then set the flag flip-flop */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (tty.flag == CLEAR)                      /* if the flag flip-flop is clear */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (tty.flag == SET)                        /* if the flag flip-flop is set */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                     /* I/O Data Input */
            outbound.value = TO_WORD (0, tty.io_data);  /* get the data register value */

            if ((tty.mode & CN_INPUT) == 0              /* if the card is in output mode */
              && sim_is_active (&print_unit))           /*   and the printer is active */
                outbound.value |= ST_BUSY;              /*     then set the busy status bit */

            tprintf (tty_dev, TRACE_CSRW, "Status is %s | %s\n",
                     fmt_bitset (outbound.value, tty_status_format),
                     fmt_char (outbound.value));
            break;


        case ioIOO:                                     /* I/O Data Output */
            if (inbound_value & CN_CONTROL) {           /* if this is a control word */
                tty.mode = inbound_value;               /*   then set the mode register */

                tprintf (tty_dev, TRACE_CSRW, "Control is %s\n",
                         fmt_bitset (inbound_value, tty_control_format));
                }

            else
                tprintf (tty_dev, TRACE_CSRW, "Output data is %s\n",
                         fmt_char (inbound_value));

            tty.io_data = LOWER_BYTE (inbound_value);   /* set the data register from the lower byte */
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            tty.flag_buffer = SET;                      /* set the flag buffer flip-flop */
            break;


        case ioCRS:                                     /* Control Reset */
            tty.control     = CLEAR;                    /* clear the control flip-flop */
            tty.flag_buffer = SET;                      /*   and set the flag buffer flip-flop */

            tty.mode = CN_INPUT;                        /* set the input mode and clear the print and punch modes */

            tty.shift_in_data = MARK;                   /* indicate that the serial line is marking */
            tty.cr_seen = FALSE;                        /*   and that a CR character has not been seen */
            break;


        case ioCLC:                                     /* Clear Control flip-flop */
            tty.control = CLEAR;                        /* clear the control flip-flop */
            break;


        case ioSTC:                                     /* Set Control flip-flop */
            tty.control = SET;                          /* set the control flip-flop */

            if ((tty.mode & CN_INPUT) == 0) {           /* if the card is in output mode */
                if (tty_dev.flags & DEV_REALTIME)       /*   then if the device is set for REALTIME mode */
                    print_unit.wait = TTY_REAL_TIME;    /*     then use the realistic timing delay */
                else                                    /*   otherwise */
                    print_unit.wait = fast_data_time;   /*     use the optimized timing delay */

                sim_activate (&print_unit, print_unit.wait);    /* schedule the printer with selective punching */

                tprintf (tty_dev, TRACE_SERV, "Unit delay %d service scheduled\n",
                         print_unit.wait);
                }
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (tty.control & tty.flag)                 /* if the control and flag flip-flops are set */
                outbound.signals |= cnVALID;            /*   then deny PRL */
            else                                        /* otherwise */
                outbound.signals |= cnPRL | cnVALID;    /*   conditionally assert PRL */

            if (tty.control & tty.flag & tty.flag_buffer)   /* if the control, flag, and flag buffer flip-flops are set */
                outbound.signals |= cnIRQ | cnVALID;        /*   then conditionally assert IRQ */

            if (tty.flag == SET)                        /* if the flag flip-flop is set */
                outbound.signals |= ioSRQ;              /*   then assert SRQ */
            break;


        case ioIAK:                                     /* Interrupt Acknowledge */
            tty.flag_buffer = CLEAR;                    /* clear the flag buffer flip-flop */
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



/* TTY local SCP support routines */


/* Set the keyboad input and print output filters.

   This validation routine is called to configure the character input filter for
   the keyboard unit and the output filter for the print unit.  The "value"
   parameter is one of the TT_MODE flags to indicate the type of filter desired.
   "uptr" points at the unit being configured and is used to prohibit setting a
   filter on the punch unit.  The character and description pointers are not
   used.

   The routine processes commands of the form:

     SET TTYn UC
     SET TTYn 7B
     SET TTYn 8B
     SET TTYn 7P

   ...where "n" is 0 or 1 to set the filter on the keyboard or print units,
   respectively.  Mode 7P (7 bit with non-printing character suppression) isn't
   valid for the keyboard and is changed to mode 7B (7 bit) if specified.
*/

static t_stat set_filter (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
if (uptr == &punch_unit)                                /* filters are not valid */
    return SCPE_NOFNC;                                  /*   for the punch */

else {
    if (uptr == &key_unit && value == TT_MODE_7P)       /* non-printing character suppression */
        value = TT_MODE_7B;                             /*   isn't appropriate for the keyboard */

    uptr->flags = uptr->flags & ~TT_MODE | value;       /* set the filter to the requested mode */
    return SCPE_OK;                                     /*   and return success */
    }
}


/* Set the automatic line feed mode.

   This validation routine is called when configuring the automatic line feed
   mode.  Some HP software systems expect the console terminal to transmit line
   feed characters automatically following each carriage return.  As an aid to
   avoid typing LF characters after pressing ENTER, the SET TTY AUTOLF command
   may be specified for the keyboard unit.  This simulates pressing the AUTO LF
   latching key on an HP 264x terminal.  Specifying the SET TTY NOAUTOLF command
   reverts to normal keyboard operation.
*/

static t_stat set_auto (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
if (uptr == &key_unit)                                  /* if this is the keyboard unit */
    return SCPE_OK;                                     /*   then allow the setting */
else                                                    /* otherwise auto LF mode is not valid */
    return SCPE_NOFNC;                                  /*   for other units */
}


/* Set the timing mode.

   This validation routine is called to set the printer and punch timing modes
   to realistic or optimized timing.  On entry, the "value" parameter is
   DEV_REALTIME if real time mode is being set and zero if optimized ("fast")
   timing mode is being set.  The unit, character, and descriptor pointers are
   not used.
*/

static t_stat set_mode (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
if (value == DEV_REALTIME)                              /* if realistic timing mode is selected */
    tty_dev.flags |= DEV_REALTIME;                      /*   then set the real-time flag */
else                                                    /* otherwise optimized timing mode is selected */
    tty_dev.flags &= ~DEV_REALTIME;                     /*   so clear the real-time flag */

return SCPE_OK;                                         /* mode changes always succeed */
}


/* Enable or disable the TTY.

   This validation routine is entered with "value" set to 1 for an ENABLE and 0
   for a DISABLE.  The unit, character, and descriptor pointers are not used.

   If the TTY is already enabled or disabled, respectively, the routine returns
   with no further action.  Otherwise, if "value" is 1, the device is enabled by
   clearing the DEV_DIS flag.  If "value" is 0, a check is made to see if the
   punch unit is attached to an output file.  If it is, the disable request is
   rejected; the unit must be detached first.  Otherwise, the device is disabled
   by setting the DEV_DIS flag.

   In either case, the device is reset, which will restart or cancel the keyboad
   poll, as appropriate.
*/

static t_stat set_endis (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
if (value)                                              /* if this is an ENABLE request */
    if (tty_dev.flags & DEV_DIS)                        /*   then if the device is disabled */
        tty_dev.flags &= ~DEV_DIS;                      /*     then reenable it */
    else                                                /*   otherwise the device is already enabled */
        return SCPE_OK;                                 /*     so there's nothing to do */

else                                                    /* otherwise this is a DISABLE request */
    if (tty_dev.flags & DEV_DIS)                        /*   so if the device is already disabled */
        return SCPE_OK;                                 /*     so there's nothing to do */
    else if (punch_unit.flags & UNIT_ATT)               /*   otherwise if the punch is still attached */
        return SCPE_ALATT;                              /*     then it cannot be disabled */
    else                                                /*   otherwise */
        tty_dev.flags |= DEV_DIS;                       /*     disable the device */

return tty_reset (&tty_dev);                            /* reset the TTY and restart or cancel polling */
}


/* Show the timing mode.

   This display routine is called to show the current timing mode.  The output
   stream is passed in the "st" parameter, and the other parameters are ignored.
*/

static t_stat show_mode (FILE *st, UNIT *uptr, int32 value, CONST void *desc)
{
if (tty_dev.flags & DEV_REALTIME)                       /* if the current mode is real time */
    fputs ("realistic timing", st);                     /*   then report it */
else                                                    /* otherwise */
    fputs ("fast timing", st);                          /*   report that the optimized timing mode is active */

return SCPE_OK;                                         /* the display routine always succeeds */
}


/* Reset the TTY.

   This routine is called for a RESET, RESET TTY, RUN, or BOOT command.  It is
   the simulation equivalent of an initial power-on condition (corresponding to
   PON, POPIO, and CRS signal assertion in the CPU) or a front-panel PRESET
   button press (corresponding to POPIO and CRS assertion).  SCP delivers a
   power-on reset to all devices when the simulator is started.

   If this is a power-on reset, the data buffer is cleared, the input shift
   register is preset, and the default optimized output time is restored.  If
   the device is disabled, then the keyboard poll service is cancelled.
   Otherwise, the poll is (re)started and coscheduled with the master keyboard
   poll timer, and POPIO is asserted to the interface.  In either case, any
   print or punch operation in progress is cancelled.
*/

static t_stat tty_reset (DEVICE *dptr)
{
if (sim_switches & SWMASK ('P')) {                      /* if this is a power-on reset */
    tty.io_data = NUL;                                  /*   then clear the data buffer */
    tty.shift_in_data = MARK;                           /*     and preset the input shift register */

    fast_data_time = TTY_FAST_TIME;                     /* restore the initial fast data time */
    }

if (tty_dev.flags & DEV_DIS)                            /* if the device is disabled */
    sim_cancel (&key_unit);                             /*   then cancel the keyboard poll */

else {                                                  /* otherwise */
    key_unit.wait = hp_sync_poll (INITIAL);             /*   coschedule the poll with the poll timer */
    sim_activate_abs (&key_unit, key_unit.wait);        /*     and begin keyboard polling */

    io_assert (dptr, ioa_POPIO);                        /* PRESET the device */
    }

sim_cancel (&print_unit);                               /* cancel any pending print or punch output */

return SCPE_OK;
}



/* TTY local utility routines */


/* TTY input service routine.

   The console input poll routine runs continuously while the device is enabled.
   It is coscheduled with the general keyboard poll timer, which is a ten
   millisecond calibrated timer that provides event timing for all of the
   keyboard polling routines.  Synchronizing the console poll with other
   keyboard polls ensures maximum idle time.

   Several HP operating systems require a CR and LF sequence for line
   termination.  This is awkward on a PC, as there is no LF key (CTRL+J is
   needed instead).  We provide an AUTOLF mode to add a LF automatically to each
   CR input.  When this mode is set, entering CR will set a flag, which will
   cause a LF to be supplied automatically at the next input poll.

   The 12531C teleprinter interface and the later 12880A CRT interface provide a
   clever mechanism to detect a keypress during output.  This is used by DOS and
   RTE to allow the user to interrupt lengthy output operations to enter system
   commands.

   Referring to the 12531C schematic, the terminal input enters on pin X
   ("DATA FROM EIA COMPATIBLE DEVICE").  The signal passes through four
   transistor inversions (Q8, Q1, Q2, and Q3) to appear on pin 12 of NAND gate
   U104C.  If the flag flip-flop is not set, the terminal input passes to the
   (inverted) output of U104C and thence to the D input of the first of the
   flip-flops forming the data register.

   In the idle condition (no key pressed), the terminal input line is marking
   (voltage negative), so in passing through a total of five inversions, a
   logic one is presented at the serial input of the data register.  During an
   output operation, the register is parallel loaded and serially shifted,
   sending the output data through the register to the device and -- this is
   the crux -- filling the register with logic ones from U104C.

   At the end of the output operation, the card flag is set, an interrupt
   occurs, and the RTE driver is entered.  The driver then does an LIA SC to
   read the contents of the data register.  If no key has been pressed during
   the output operation, the register will read as all ones (octal 377).  If,
   however, any key was struck, at least one zero bit will be present.  If the
   register value doesn't equal 377, the driver sets the system "operator
   attention" flag, which will cause DOS or RTE to output an asterisk prompt and
   initiate a terminal read when the current output line is completed.
*/

static t_stat keyboard_service (UNIT *uptr)
{
t_stat status;
char   input;

tprintf (tty_dev, TRACE_PSERV, "Poll delay %d service entered\n",
         uptr->wait);

uptr->wait = hp_sync_poll (SERVICE);                    /* coschedule with the poll timer */
sim_activate (uptr, uptr->wait);                        /*   and continue the poll */

if (tty.cr_seen && key_unit.flags & UNIT_AUTOLF) {      /* if a CR was seen and auto-linefeed mode is active */
    input = LF;                                         /*   then simulate the input of a LF */

    tprintf (tty_dev, TRACE_XFER, "Character LF generated internally\n");
    }

else {                                                  /* otherwise */
    status = sim_poll_kbd ();                           /*   poll the simulation console keyboard for input */

    if (status < SCPE_KFLAG)                            /* if a character was not present or an error occurred */
        return status;                                  /*   then return the poll status */

    if (status & SCPE_BREAK) {                          /* if a break was detected */
        input = NUL;                                    /*   then the character reads as NUL */

        tprintf (tty_dev, TRACE_XFER, "Break detected\n");
        }

    else {                                              /* otherwise */
        input = (char) sim_tt_inpcvt ((int32) status,   /*   convert the character using the input mode */
                                      TT_GET_MODE (uptr->flags));

        tprintf (tty_dev, TRACE_XFER, "Character %s entered at keyboard\n",
                 fmt_char ((uint32) input));
        }
    }

tty.cr_seen = (input == CR);                            /* set the CR seen flag if a CR was entered */

if (tty.mode & CN_INPUT) {                              /* if the card is set for input */
    tty.io_data = input;                                /*   then store the character in the data register */
    tty.shift_in_data = MARK;                           /*     and indicate that the serial line is marking */

    uptr->pos = uptr->pos + 1;                          /* count the character */

    tty.flag_buffer = SET;                              /* set the flag buffer */
    io_assert (&tty_dev, ioa_ENF);                      /*   and the flag */

    if (tty.mode & (CN_PRINT | CN_PUNCH))               /* if the printer or punch is enabled */
        status = output ((int32) input);                /*   then scho the received character */
    else                                                /* otherwise */
        status = SCPE_OK;                               /*    silently indicate success */
    }

else {                                                  /* otherwise the card is set for output */
    tty.shift_in_data = input;                          /*   and the received character will be shifted in */
    status = SCPE_OK;
    }

return status;                                          /* return the status of the operation */
}


/* TTY output service routine.

   The output service routine is scheduled when the interface receives an STC
   signal.  On entry, the data output register contains the character to output.
   A prior control word will have enabled either the printer, the punch, both,
   or neither.  This selective output is handled by the "output" routine.

   As noted in the comments above, if no key is pressed while output is in
   progress, the input shift register will contain all ones, which is the
   marking condition of the serial input line.  However, if a key is pressed,
   the register will contain something other than all ones; the exact value is
   indeterminate, as it depends on the timing between the keypress and the print
   operation.
*/

static t_stat print_punch_service (UNIT *uptr)
{
t_stat status;

tprintf (tty_dev, TRACE_SERV, "Printer and punch service entered\n");

status = output ((int32) tty.io_data);                  /* output the character if enabled */

if (status == SCPE_OK) {                                /* if the output succeeded */
    tty.io_data = tty.shift_in_data;                    /*   then shift the input line data into the buffer */
    tty.shift_in_data = MARK;                           /*     and indicate that the serial line is marking */

    tty.flag_buffer = SET;                              /* set the flag buffer */
    io_assert (&tty_dev, ioa_ENF);                      /*   and the flag */

    return SCPE_OK;                                     /* return success */
    }

else {                                                  /* otherwise an error occurred */
    sim_activate (uptr, uptr->wait);                    /*   so schedule a retry */

    if (status == SCPE_STALL)                           /* if an output stall occurred */
        return SCPE_OK;                                 /*   then ignore it */
    else                                                /* otherwise */
        return status;                                  /*   return the operation status */
    }
}


/* TTY print/punch output routine.

   This routine outputs the supplied character to the print and/or punch unit as
   directed by a prior control word sent to the interface.  For output, the
   control word may set the print flip-flop, the punch flip-flop, or both
   flip-flops.  These flip-flops generate the PRINT COMMAND and PUNCH COMMAND
   output signals, respectively.  Setting either one enables data transmission.

   Only the HP 2754A (ASR35) teleprinter responds to the PRINT and PUNCH COMMAND
   signals.  All other terminals ignore these signals and respond only to the
   serial data out signal (the paper tape punches on the 2749A and 2752A
   teleprinters must be enabled manually at the console and operate concurrently
   with the printers).

   This routine simulates a 2754A when the punch unit (TTY unit 2) is attached
   and a generic terminal when the unit is detached.  With the punch unit
   attached, the punch flip-flop must be set to punch, and the print flip-flop
   must be set to print.  These flip-flops, and therefore their respective
   operations, are independent.  When the punch unit is detached, printing will
   occur if either the print or punch flip-flop is set.  If neither flip-flop is
   set, no output occurs.  Therefore, the logic is:

     if punch-flip-flop and punch-attached
       then punch character

     if print-flip-flop or punch-flip-flop and not punch-attached
       then print character

   Certain HP programs, e.g., HP 2000F BASIC FOR DOS-M/DOS III, depend on the
   generic (2752A et. al.) behavior.  The DOS and RTE teleprinter drivers
   support text and binary output modes.  Text mode sets the print flip-flop,
   and binary mode sets the punch flip-flop.  These programs use binary mode to
   write single characters to the teleprinter and expect that they will be
   printed.  The simulator follows this behavior.
*/

static t_stat output (int32 character)
{
int32  print_char;
t_stat status = SCPE_OK;

if (tty.mode & CN_PUNCH                                             /* if punching is enabled */
  && punch_unit.flags & UNIT_ATT)                                   /*   and the punch is attached */
    if (fputc ((int) character, punch_unit.fileref) == EOF) {       /*     then write the byte; if the write fails */
        cprintf ("%s simulator teleprinter punch I/O error: %s\n",  /*       then report the error to the console */
                 sim_name, strerror (errno));

        clearerr (punch_unit.fileref);                  /* clear the error */
        status = SCPE_IOERR;                            /*   and stop the simulator */
        }

    else {                                              /* otherwise the output succeeded */
        punch_unit.pos = ftell (punch_unit.fileref);    /*   so update the file position */

        tprintf (tty_dev, TRACE_XFER, "Data %03o character %s sent to punch\n",
                 character, fmt_char ((uint32) character));
        }

if (tty.mode & CN_PRINT                                 /* if printing is enabled */
  || tty.mode & CN_PUNCH                                /*   or punching is enabled */
  && (punch_unit.flags & UNIT_ATT) == 0) {              /*     and the punch is not attached */
    print_char = sim_tt_outcvt (character,              /*       then convert the character using the output mode */
                                TT_GET_MODE (print_unit.flags));

    if (print_char >= 0) {                              /* if the character is valid */
        status = sim_putchar_s (print_char);            /*   then output it to the simulation console */

        if (status == SCPE_OK) {                        /* if the output succeeded */
            print_unit.pos = print_unit.pos + 1;        /*   then update the file position */

            tprintf (tty_dev, TRACE_XFER, "Character %s sent to printer\n",
                     fmt_char ((uint32) print_char));
            }
        }

    else                                                /* otherwise the character was filtered out */
        tprintf (tty_dev, TRACE_XFER, "Character %s discarded by output filter\n",
                 fmt_char ((uint32) character));
    }

return status;                                          /* return the status of the operation */
}
