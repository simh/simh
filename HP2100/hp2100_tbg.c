/* hp2100_tbg.c: HP 12539C Time Base Generator Interface simulator

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

   TBG          12539C Time Base Generator Interface

   10-Jun-18    JDB     Revised I/O model
   04-Jun-18    JDB     Split out from hp2100_stddev.c
                        Trimmed revisions to current file applicability
   08-Mar-17    JDB     Added REALTIME, W1A, W1B, W2A, and W2B options to the TBG
                        Replaced IPTICK with a CPU speed calculation
   17-Jan-17    JDB     Changed "hp_---sc" and "hp_---dev" to "hp_---_dib"
   13-May-16    JDB     Modified for revised SCP API function parameter types
   24-Dec-14    JDB     Added casts for explicit downward conversions
   28-Dec-12    JDB     Allocate the TBG logical name during power-on reset
   09-May-12    JDB     Separated assignments from conditional expressions
   12-Feb-12    JDB     Add TBG as a logical name for the CLK device
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   14-Apr-08    JDB     Synchronized CLK with TTY if set for 10 msec.
                        Added UNIT_IDLE to TTY and CLK
   31-Dec-07    JDB     Added IPTICK register to CLK to display CPU instr/tick
                        Corrected and verified ioCRS actions
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   13-Sep-04    JDB     Modified CLK to permit disable
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
   01-Nov-02    RMS     Fixed clock to add calibration, proper start/stop
   22-Mar-02    RMS     Revised for dynamically allocated memory
   03-Nov-01    RMS     Changed DEVNO to use extended SET/SHOW
   24-Nov-01    RMS     Changed TIME to an array
   07-Sep-01    RMS     Moved function prototypes
   21-Nov-00    RMS     Fixed flag, buffer power up state
   15-Oct-00    RMS     Added dynamic device number support

   References:
     - 12539C Time Base Generator Interface Kit Operating and Service Manual
         (12539-90008, January 1975)


   The time base generator interface responds to I/O instructions as follows:

   Output Data Word format (OTA and OTB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   -   -   - | tick rate |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Tick Rate Selection:

     000 = 100 microseconds
     001 = 1 millisecond
     010 = 10 milliseconds
     011 = 100 milliseconds
     100 = 1 second
     101 = 10 seconds
     110 = 100 seconds
     111 = 1000 seconds

   If jumper W2 is in position B, the last four rates are divided by 1000,
   producing rates of 1, 10, 100, and 1000 milliseconds, respectively.


   Input Data Word format (LIA, LIB, MIA, and MIB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   - | e | E | -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     E = At least one tick has been lost

   If jumper W1 is in position B, bit 5 also indicates a lost tick.


   In hardware, the two configuration jumpers perform these functions:

     Jumper  Interpretation in position A  Interpretation in position B
     ------  ----------------------------  ---------------------------------
       W1    Input bit 5 is always zero    Input bit 5 indicates a lost tick

       W2    Last four rates are seconds   Last four rates are milliseconds

   The time base generator autocalibrates.  If the TBG is set to a ten
   millisecond period (e.g., as under RTE), it is synchronized to the console
   poll.  Otherwise (e.g., as under DOS or TSB, which use 100 millisecond
   periods), it runs asynchronously.  If the specified clock frequency is below
   10Hz, the clock service routine runs at 10Hz and counts down a repeat counter
   before generating an interrupt.  Autocalibration will not work if the clock
   is running at 1Hz or less.
*/



#include "hp2100_defs.h"
#include "hp2100_io.h"



/* Program constants */

static const int32 delay [8] = {                /* clock delays, in event ticks per interval */
    uS (100),                                   /*   000 = 100 microseconds */
    mS (1),                                     /*   001 = 1 millisecond */
    mS (10),                                    /*   010 = 10 milliseconds */
    mS (100),                                   /*   011 = 100 milliseconds */
    S (1),                                      /*   100 = 1 second */
    S (10),                                     /*   101 = 10 seconds */
    S (100),                                    /*   110 = 100 seconds */
    S (1000)                                    /*   111 = 1000 seconds */
    };

static const int32 ticks [8] = {                /* clock ticks per second */
    10000,                                      /*   000 = 100 microseconds */
    1000,                                       /*   001 = 1 millisecond */
    100,                                        /*   010 = 10 milliseconds */
    10,                                         /*   011 = 100 milliseconds */
    10,                                         /*   100 = 1 second */
    10,                                         /*   101 = 10 seconds */
    10,                                         /*   110 = 100 seconds */
    10                                          /*   111 = 1000 seconds */
    };

static const int32 scale [8] = {                /* prescaler counts per clock tick */
    1,                                          /*   000 = 100 microseconds */
    1,                                          /*   001 = 1 millisecond */
    1,                                          /*   010 = 10 milliseconds */
    1,                                          /*   011 = 100 milliseconds */
    10,                                         /*   100 = 1 second */
    100,                                        /*   101 = 10 seconds */
    1000,                                       /*   110 = 100 seconds */
    10000                                       /*   111 = 1000 seconds */
    };


/* Unit flags */

#define UNIT_CALTIME_SHIFT  (UNIT_V_UF + 0)     /* calibrated timing mode */
#define UNIT_W1B_SHIFT      (UNIT_V_UF + 1)     /* jumper W1 in position B */
#define UNIT_W2B_SHIFT      (UNIT_V_UF + 2)     /* jumper W2 in position B */

#define UNIT_CALTIME        (1u << UNIT_CALTIME_SHIFT)
#define UNIT_W1B            (1u << UNIT_W1B_SHIFT)
#define UNIT_W2B            (1u << UNIT_W2B_SHIFT)


/* Control word.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   -   -   - | tick rate |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define CN_RATE_MASK        0000007u            /* clock rate selector mask */

#define CN_RATE_SHIFT       0                   /* clock rate alignment shift */

#define CN_RATE(c)          (((c) & CN_RATE_MASK) >> CN_RATE_SHIFT)

static const char * const rate_name [8] = {     /* clock rate selector names */
    "100 microsecond",                          /*   000 = 100 microseconds */
    "1 millisecond",                            /*   001 = 1 millisecond */
    "10 millisecond",                           /*   010 = 10 milliseconds */
    "100 millisecond",                          /*   011 = 100 milliseconds */
    "1 second",                                 /*   100 = 1 second */
    "10 second",                                /*   101 = 10 seconds */
    "100 second",                               /*   110 = 100 seconds */
    "1000 second"                               /*   111 = 1000 seconds */
    };


/* Status word.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   - | e | E | -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define ST_ERROR            0000020u            /* lost tick error */
#define ST_ERROR_W1B        0000040u            /* lost tick error if W1 in position B */

static const BITSET_NAME status_names [] = {    /* Status word names */
    "lost tick"                                 /*   bit  4 */
    };

static const BITSET_FORMAT status_format =      /* names, offset, direction, alternates, bar */
    { FMT_INIT (status_names, 4, msb_first, no_alt, no_bar) };


/* Interface state */

typedef struct {
    HP_WORD    output_data;                     /* output data register */
    HP_WORD    input_data;                      /* input data register */
    FLIP_FLOP  control;                         /* control flip-flop */
    FLIP_FLOP  flag;                            /* flag flip-flop */
    FLIP_FLOP  flag_buffer;                     /* flag buffer flip-flop */
    } CARD_STATE;

static CARD_STATE tbg;                          /* per-card state */


/* Time base generator state */

static int32     rate;                          /* clock rate */
static int32     prescaler;                     /* clock rate prescaler */
static FLIP_FLOP lost_tick;                     /* lost tick error flip-flop */


/* Interface local SCP support routines */

static INTERFACE tbg_interface;


/* Time base generator local SCP support routines */

static t_stat tbg_service (UNIT   *uptr);
static t_stat tbg_reset   (DEVICE *dptr);


/* Time base generator local utility routines */

typedef enum {
    Clock_Time,
    Prescaler_Count
    } DELAY_TYPE;

static int32 get_delay (DELAY_TYPE selector);


/* Interface SCP data structures */


/* Device information block */

static DIB tbg_dib = {
    &tbg_interface,                             /* the device's I/O interface function pointer */
    TBG,                                        /* the device's select code (02-77) */
    0,                                          /* the card index */
    "12539C Time Base Generator Interface",     /* the card description */
    NULL                                        /* the ROM description */
    };


/* Unit list */

static UNIT tbg_unit [] = {
    { UDATA (&tbg_service, UNIT_IDLE | UNIT_CALTIME, 0) }
    };


/* Register list */

static REG tbg_reg [] = {
/*    Macro   Name    Location             Width  Offset  Flags   */
/*    ------  ------  -------------------  -----  ------  ------- */
    { ORDATA (SEL,    rate,                  3)                   },
    { DRDATA (CTR,    prescaler,            14)                   },
    { FLDATA (CTL,    tbg.control,                  0)            },
    { FLDATA (FLG,    tbg.flag,                     0)            },
    { FLDATA (FBF,    tbg.flag_buffer,              0)            },
    { FLDATA (ERR,    lost_tick,                    0)            },

      DIB_REGS (tbg_dib),

    { NULL }
    };


/* Modifier list */

static MTAB tbg_mod [] = {
/*    Mask Value    Match Value   Print String         Match String  Validation  Display  Descriptor */
/*    ------------  ------------  -------------------  ------------  ----------  -------  ---------- */
    { UNIT_CALTIME, UNIT_CALTIME, "calibrated timing", "CALTIME",    NULL,       NULL,    NULL       },
    { UNIT_CALTIME, 0,            "realistic timing",  "REALTIME",   NULL,       NULL,    NULL       },
    { UNIT_W1B,     UNIT_W1B,     "W1 position B",     "W1B",        NULL,       NULL,    NULL       },
    { UNIT_W1B,     0,            "W1 position A",     "W1A",        NULL,       NULL,    NULL       },
    { UNIT_W2B,     UNIT_W2B,     "W2 position B",     "W2B",        NULL,       NULL,    NULL       },
    { UNIT_W2B,     0,            "W2 position A",     "W2A",        NULL,       NULL,    NULL       },

/*    Entry Flags          Value  Print String  Match String  Validation    Display        Descriptor        */
/*    -------------------  -----  ------------  ------------  ------------  -------------  ----------------- */
    { MTAB_XDV,             1u,   "SC",         "SC",         &hp_set_dib,  &hp_show_dib,  (void *) &tbg_dib },
    { MTAB_XDV | MTAB_NMO, ~1u,   "DEVNO",      "DEVNO",      &hp_set_dib,  &hp_show_dib,  (void *) &tbg_dib },

    { 0 }
    };


/* Debugging trace list */

static DEBTAB tbg_deb [] = {
    { "CSRW",  TRACE_CSRW  },                   /* interface control, status, read, and write actions */
    { "PSERV", TRACE_PSERV },                   /* clock unit service scheduling calls */
    { "IOBUS", TRACE_IOBUS },                   /* interface I/O bus signals and data words */
    { NULL,    0           }
    };


/* Device descriptor */

DEVICE tbg_dev = {
    "CLK",                                      /* device name (deprecated) */
    tbg_unit,                                   /* unit array */
    tbg_reg,                                    /* register array */
    tbg_mod,                                    /* modifier array */
    1,                                          /* number of units */
    0,                                          /* address radix */
    0,                                          /* address width */
    0,                                          /* address increment */
    0,                                          /* data radix */
    0,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &tbg_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &tbg_dib,                                   /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    tbg_deb,                                    /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* Interface local SCP support routines */



/* Time base generator interface.

   The time base generator (TBG) provides periodic interrupts from 100
   microseconds to 1000 seconds.  The TBG uses a calibrated timer to provide the
   time base.  For periods ranging from 1 to 1000 seconds, a 100 millisecond
   timer is used, and 10 to 10000 ticks are counted before setting the device
   flag to indicate that the period has expired.

   If the period is set to ten milliseconds, the console poll timer is used
   instead of an independent timer.  This is to maximize the idle period.

   In diagnostic mode, the clock period is set to the expected number of CPU
   instructions, rather than wall-clock time, so that the diagnostic executes as
   expected.
*/

static SIGNALS_VALUE tbg_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };
t_bool         irq_enabled = FALSE;
int32          tick_count;

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            tbg.flag_buffer = CLEAR;                    /* reset the flag buffer */
            tbg.flag        = CLEAR;                    /*   and flag flip-flops */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            tbg.flag_buffer = SET;                      /* set the flag buffer flip-flop */
            break;


        case ioENF:                                     /* Enable Flag */
            if (tbg.flag_buffer == SET)                 /* if the flag buffer flip-flop is set */
                tbg.flag = SET;                         /*   then set the flag flip-flop */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (tbg.flag == CLEAR)                      /* if the flag flip-flop is clear */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (tbg.flag == SET)                        /* if the flag flip-flop is set */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                     /* I/O Data Input */
            if (lost_tick == SET) {                     /* if the lost-tick flip-flop is set */
                outbound.value = ST_ERROR;              /*   then indicate an error */

                if (tbg_unit [0].flags & UNIT_W1B)      /* if W1 is in position B */
                    outbound.value |= ST_ERROR_W1B;     /*   then set the status in bit 5 as well */
                }

            else                                        /* otherwise the error flip-flop is clear */
                outbound.value = 0;                     /*   so clear the error status */

            tprintf (tbg_dev, TRACE_CSRW, "Status is %s\n",
                     fmt_bitset (outbound.value, status_format));
            break;


        case ioIOO:                                     /* I/O Data Output */
            rate = CN_RATE (inbound_value);             /* save select */

            tbg.control = CLEAR;                        /* clear control */
            sim_cancel (&tbg_unit [0]);                 /* stop the clock */

            working_set = working_set | ioSIR;          /* set interrupt request (IOO normally doesn't) */

            tprintf (tbg_dev, TRACE_CSRW, "Control is %s rate\n",
                     rate_name [rate]);
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            tbg.flag_buffer = SET;                      /* set the flag buffer flip-flop */
            break;


        case ioCRS:                                     /* Control Reset */
        case ioCLC:                                     /* Clear Control flip-flop */
            tbg.control = CLEAR;                        /* clear the control flip-flop */
            sim_cancel (&tbg_unit [0]);                 /*   and stop the clock */
            break;


        case ioSTC:                                     /* Set Control flip-flop */
            tbg.control = SET;                          /* set the control flip-flop */

            if (!sim_is_active (&tbg_unit [0])) {       /* if the TBG is not running */
                tick_count = get_delay (Clock_Time);    /*   then get the programmed tick count */

                if (tbg_unit [0].flags & UNIT_CALTIME)          /* if the TBG is calibrated */
                    if (rate == 2)                              /*   then if the rate is 10 milliseconds */
                        tick_count = hp_sync_poll (INITIAL);    /*     then synchronize with the poll timer */
                    else                                        /*   otherwise */
                        sim_rtcn_init (tick_count, TMR_TBG);    /*     calibrate the TBG timer independently */

                tprintf (tbg_dev, TRACE_PSERV, "Rate %s delay %d service rescheduled\n",
                         rate_name [rate], tick_count);

                sim_activate (&tbg_unit [0], tick_count);   /* start the TBG */
                prescaler = get_delay (Prescaler_Count);    /*   and set the prescaler */
                }

            lost_tick = CLEAR;                          /* clear the lost tick flip-flop */
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (tbg.control & tbg.flag)                 /* if the control and flag flip-flops are set */
                outbound.signals |= cnVALID;            /*   then deny PRL */
            else                                        /* otherwise */
                outbound.signals |= cnPRL | cnVALID;    /*   conditionally assert PRL */

            if (tbg.control & tbg.flag & tbg.flag_buffer)   /* if the control, flag, and flag buffer flip-flops are set */
                outbound.signals |= cnIRQ | cnVALID;        /*   then conditionally assert IRQ */

            if (tbg.flag == SET)                        /* if the flag flip-flop is set */
                outbound.signals |= ioSRQ;              /*   then assert SRQ */
            break;


        case ioIAK:                                     /* Interrupt Acknowledge */
            tbg.flag_buffer = CLEAR;                    /* clear the flag buffer flip-flop */
            break;


        case ioIEN:                                     /* Interrupt Enable */
            irq_enabled = TRUE;                         /* permit IRQ to be asserted */
            break;


        case ioPRH:                                         /* Priority High */
            if (irq_enabled && outbound.signals & cnIRQ)    /* if IRQ is enabled and conditionally asserted */
                outbound.signals |= ioIRQ | ioFLG;          /*   then assert IRQ and FLG */

            if (!irq_enabled || outbound.signals & cnPRL)   /* if IRQ is disabled or PRL is conditionally asserted */
                outbound.signals |= ioPRL;              /*   then assert it unconditionally */
            break;


        case ioEDT:                                     /* not used by this interface */
        case ioPON:                                     /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }

return outbound;                                        /* return the outbound signals and value */
}


/* TBG unit service.

   As with the I/O handler, if the time base period is set to ten milliseconds,
   the console poll timer is used instead of an independent timer.


   Implementation notes:

    1. If the TBG is calibrated, it is synchronized with the TTY keyboard poll
       service to permit idling.
*/

static t_stat tbg_service (UNIT *uptr)
{
int32 tick_count;

tprintf (tbg_dev, TRACE_PSERV, "Service entered with prescaler %d\n",
         prescaler);

if (tbg.control == CLEAR)                               /* control clear? */
    return SCPE_OK;                                     /* done */

if (tbg_unit [0].flags & UNIT_CALTIME)                  /* cal mode? */
    if (rate == 2)                                      /* 10 msec period? */
        tick_count = hp_sync_poll (SERVICE);            /* sync poll */
    else
        tick_count = sim_rtcn_calb (ticks [rate],       /* calibrate delay */
                                    TMR_TBG);

else                                                    /* otherwise the TBG is in real-time mode */
    tick_count = get_delay (Clock_Time);                /*   so get the delay directly */

prescaler = prescaler - 1;                              /* decrement the prescaler count */

if (prescaler <= 0) {                                   /* if the prescaler count has expired */
    if (tbg.flag) {
        lost_tick = SET;                                /* overrun? error */

        tprintf (tbg_dev, TRACE_PSERV, "Clock tick lost\n");
        }

    else {                                              /* otherwise */
        tbg.flag_buffer = SET;                          /*   set the flag buffer */
        io_assert (&tbg_dev, ioa_ENF);                  /*     and the flag */
        }

    prescaler = get_delay (Prescaler_Count);            /* reset the prescaler */
    }

tprintf (tbg_dev, TRACE_PSERV, "Rate %s delay %d service %s\n",
         rate_name [rate], tick_count,
         (rate == 2 ? "coscheduled" : "scheduled"));

return sim_activate (uptr, tick_count);                 /* reactivate */
}


/* Reset routine */

static t_stat tbg_reset (DEVICE *dptr)
{
if (sim_switches & SWMASK ('P')) {                      /* initialization reset? */
    lost_tick = CLEAR;                                  /* clear error */
    rate = 0;                                           /* clear rate */
    prescaler = 0;                                      /* clear prescaler */

    if (tbg_dev.lname == NULL)                          /* logical name unassigned? */
        tbg_dev.lname = strdup ("TBG");                 /* allocate and initialize the name */
    }

io_assert (dptr, ioa_POPIO);                            /* PRESET the device */

return SCPE_OK;
}



/* Time base generator local utility routines */


/* Clock delay routine */

static int32 get_delay (DELAY_TYPE selector)
{
int32 rate_index;

if (tbg_unit [0].flags & UNIT_W2B && rate >= 4)         /* if jumper W2 is in position B */
    rate_index = rate - 3;                              /*   then rates 4-7 rescale to 1-4 */
else                                                    /* otherwise */
    rate_index = rate;                                  /*   the rate selector is used as is */

if (selector == Clock_Time)                             /* if the clock time is wanted */
    return delay [rate_index];                          /*   return the tick delay count */
else                                                    /* otherwise */
    return scale [rate_index];                          /*   return the prescale count */
}
