/* hp3000_clk.c: HP 3000 30135A System Clock/Fault Logging Interface simulator

   Copyright (c) 2016, J. David Bryan

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

   CLK          HP 30135A System Clock/Fault Logging Interface

   12-Sep-16    JDB     Changed DIB register macro usage from SRDATA to DIB_REG
   11-Jul-16    JDB     Change "clk_unit" from a UNIT to an array of one UNIT
   08-Jul-16    JDB     Added REG entry to save the unit wait field
   09-Jun-16    JDB     Clarified the IRQ FF set code in DRESETINT
   08-Jun-16    JDB     Corrected %d format to %u for unsigned values
   21-Mar-16    JDB     Changed inbound_value and outbound_value types to HP_WORD
   08-Jun-15    JDB     First release version
   12-Aug-14    JDB     Passed the system clock diagnostic (D426A)
   05-Jul-14    JDB     Created

   References:
     - Stand-Alone System Clock Diagnostic
         (32230-90005, January 1979)
     - HP 3000 Series III Engineering Diagrams Set
         (30000-90141, April 1980)


   The HP 30135A System Clock/Fault Logging Interface is used with Series II and
   III systems and provides two devices on a single I/O card: a programmable
   interval clock employed as the MPE system clock and an interface to the ECC
   fault logging RAMs on the semiconductor main memory arrays.  This replaced
   the earlier 30031A System Clock/Console Interface that had been used with the
   CX and Series I machines, which used core memory.  As part of this change,
   the system console moved from the dedicated card to ATC port 0.

   The clock provides programmable periods of 10 microseconds to 10 seconds in
   decade increments.  Each "tick" of the clock increments a presettable counter
   that may be compared to a selected limit value.  The clock may request an
   interrupt when the values are equal, and a status indication is provided if
   the counter reaches the limit a second time without acknowledgement.

   The clock simulation provides both a REALTIME mode that establishes periods
   in terms of event intervals, based on an average instruction time of 2.5
   microseconds, and a CALTIME mode that calibrates the time delays to match
   wall-clock time.  As an example, in the former mode, a 1 millisecond period
   will elapse after 400 instructions are executed, whereas in the latter mode,
   the same period will elapse after 1 millisecond of wall-clock time.  As the
   simulator is generally one or two orders of magnitude faster than a real HP
   3000, the real-time mode will satisfy the expectations of software that times
   external events, such as a disc seek, via a delay loop, whereas the
   calibrated mode will update a time-of-day clock as expected by users of the
   system.  In practice, this means that setting REALTIME mode is necessary to
   satisfy the hardware diagnostics, and setting CALTIME mode is necessary when
   running MPE.

   Currently, the Fault Logging Interface simulator is not implemented.  This
   interface is accessed via DRT 2 by the MPE memory logging process, MEMLOGP,
   but the process is smart enough to terminate if DRT 2 does not respond.  As
   the simulator relies on a host memory array to simulate RAM and does not
   simulate the ECC check bits, an FLI implementation would always return a "no
   errors detected" condition.


   The clock interface responds only to direct I/O instructions, as follows:

   Control Word Format (CIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M / rate  | E | - | irq reset | C | L | A | -   -   -   - | I |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     M = master reset (if bit 3 = 0)
     E = master reset enable/load count rate (0/1)
     C = reset count register after LR=CR interrupt
     L = a WIO addresses the limit/count (0/1) register
     A = reset all interrupts
     I = enable clock interrupts

   Count Rate Selection:

     000 = unused
     001 = 10 microseconds
     010 = 100 microseconds
     011 = 1 millisecond
     100 = 10 milliseconds
     101 = 100 milliseconds
     110 = 1 second
     111 = 10 seconds

   IRQ Reset:

     000 = none
     001 = clear LR = CR interrupt
     010 = clear LR = CR overflow interrupt
     011 = clear I/O system interrupt (SIN)
     100 = unused
     101 = unused
     110 = unused
     111 = unused


   Status Word Format (TIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | D |   rate    | -   -   -   -   - | C | F | - | I | L | R |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     S = SIO OK (always 0)
     D = direct read/write I/O OK (always 1)
     C = limit register = count register
     F = limit register = count register overflow (lost tick)
     I = I/O system interrupt request (SIN)
     L = limit/count (0/1) register selected
     R = reset count register after interrupt


   Output Data Word Format (WIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |           limit register value/count register reset           |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   If control word bit 9 is 0, the value is written to the limit register.  If
   control word bit 9 is 1, the count register is cleared to zero; the output
   value is ignored.


   Input Data Word Format (RIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     count register value                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   Implementation notes:

    1. MPE sets the system clock to a 1 millisecond period and a 100 millisecond
       limit to achieve the 10 interrupts per second rate required by the
       time-of-day clock maintained by the OS.  The short period precludes
       idling.  Therefore, this configuration is detected and implemented
       internally as a 10 millisecond service time with the counter incremented
       by 10 for each event service.  In addition, the clock service is
       synchronized with the CPU process clock service and the ATC poll service
       to improve idling.

    2. If the clock is calibrated, a prescaler is used to achieve the 1 second
       and 10 second periods while the event service time remains at 100
       milliseconds.  For periods shorter than 1 second, and for all realtime
       periods, the prescaler is not used.  The prescaler is necessary because
       the "sim_rtcn_calb" routine in the sim_timer library requires an integer
       ticks-per-second parameter.
*/



#include "hp3000_defs.h"
#include "hp3000_io.h"



/* Program constants */

#define CLK_MULTIPLIER      10                          /* number of MPE clock ticks per service */
#define CLK_RATE            (1000 / CLK_MULTIPLIER)     /* MPE clock rate in ticks per second */


static const int32 delay [8] = {                /* clock delays, in event ticks per interval */
    0,                                          /*   000 = unused */
    uS (10),                                    /*   001 = 10 microseconds */
    uS (100),                                   /*   010 = 100 microseconds */
    mS (1),                                     /*   011 = 1 millisecond */
    mS (10),                                    /*   100 = 10 milliseconds */
    mS (100),                                   /*   101 = 100 milliseconds */
    S (1),                                      /*   110 = 1 second */
    S (10)                                      /*   111 = 10 seconds */
    };

static const int32 ticks [8] = {                /* clock ticks per second */
    0,                                          /*   000 = unused */
    100000,                                     /*   001 = 10 microseconds */
    10000,                                      /*   010 = 100 microseconds */
    1000,                                       /*   011 = 1 millisecond */
    100,                                        /*   100 = 10 milliseconds */
    10,                                         /*   101 = 100 milliseconds */
    10,                                         /*   110 = 1 second */
    10                                          /*   111 = 10 seconds */
    };

static const int32 scale [8] = {                /* prescaler counts per clock tick */
    1,                                          /*   000 = unused */
    1,                                          /*   001 = 10 microseconds */
    1,                                          /*   010 = 100 microseconds */
    1,                                          /*   011 = 1 millisecond */
    1,                                          /*   100 = 10 milliseconds */
    1,                                          /*   101 = 100 milliseconds */
    10,                                         /*   110 = 1 second */
    100                                         /*   111 = 10 seconds */
    };


/* Unit flags */

#define UNIT_CALTIME_SHIFT  (UNIT_V_UF + 0)     /* calibrated timing mode */

#define UNIT_CALTIME        (1u << UNIT_CALTIME_SHIFT)


/* Debug flags */

#define DEB_CSRW            (1u << 0)           /* trace commands received and status returned */
#define DEB_PSERV           (1u << 1)           /* trace unit service scheduling calls */
#define DEB_IOB             (1u << 2)           /* trace I/O bus signals and data words exchanged */


/* Control word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M / rate  | E | - | irq reset | C | L | A | -   -   -   - | I |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define CN_MR               0100000u            /* (M) master reset (if bit 3 = 0) */
#define CN_RATE_MASK        0160000u            /* clock rate selector mask (if bit 3 = 1) */
#define CN_RESET_LOAD_SEL   0010000u            /* (E) select reset/load rate (0/1) */
#define CN_IRQ_RESET_MASK   0003400u            /* interrupt request reset selector mask */
#define CN_COUNT_RESET      0000200u            /* (C) reset count register after LR=CR interrupt */
#define CN_LIMIT_COUNT_SEL  0000100u            /* (L) select limit/count (0/1) register */
#define CN_IRQ_RESET_ALL    0000040u            /* (A) reset all interrupt requests */
#define CN_IRQ_ENABLE       0000001u            /* (I) enable clock interrupts */

#define CN_RATE_SHIFT       13                  /* clock rate alignment shift */
#define CN_IRQ_RESET_SHIFT  8                   /* interrupt request reset alignment shift */

#define CN_RATE(c)          (((c) & CN_RATE_MASK) >> CN_RATE_SHIFT)
#define CN_RESET(c)         (((c) & CN_IRQ_RESET_MASK) >> CN_IRQ_RESET_SHIFT)

static const char *const rate_name [8] = {      /* clock rate selector names */
    "unused",                                   /*   000 = unused */
    "10 microsecond",                           /*   001 = 10 microseconds */
    "100 microsecond",                          /*   010 = 100 microseconds */
    "1 millisecond",                            /*   011 = 1 millisecond */
    "10 millisecond",                           /*   100 = 10 milliseconds */
    "100 millisecond",                          /*   101 = 100 milliseconds */
    "1 second",                                 /*   110 = 1 second */
    "10 second"                                 /*   111 = 10 seconds */
    };

static const char *const irq_reset_name [8] = { /* IRQ reset selector names */
    "",                                         /*   000 = none */
    " | reset LR = CR irq",                     /*   001 = LR equal CR */
    " | reset LR = CR overflow irq",            /*   010 = LR equal CR overflow */
    " | reset SIN irq",                         /*   011 = I/O system */
    "",                                         /*   100 = unused */
    "",                                         /*   101 = unused */
    "",                                         /*   110 = unused */
    ""                                          /*   111 = unused */
    };

static const BITSET_NAME control_names [] = {   /* Control word names */
    "master reset",                             /*   bit  0 */
    NULL,                                       /*   bit  1 */
    NULL,                                       /*   bit  2 */
    "load rate",                                /*   bit  3 */
    NULL,                                       /*   bit  4 */
    NULL,                                       /*   bit  5 */
    NULL,                                       /*   bit  6 */
    NULL,                                       /*   bit  7 */
    "reset count",                              /*   bit  8 */
    "\1select count\0select limit",             /*   bit  9 */
    "reset interrupts",                         /*   bit 10 */
    NULL,                                       /*   bit 11 */
    NULL,                                       /*   bit 12 */
    NULL,                                       /*   bit 13 */
    NULL,                                       /*   bit 14 */
    "enable interrupts"                         /*   bit 15 */
    };

static const BITSET_FORMAT control_format =     /* names, offset, direction, alternates, bar */
    { FMT_INIT (control_names, 0, msb_first, has_alt, no_bar) };


/* Status word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - | D |   rate    | -   -   -   -   - | C | F | - | I | L | R |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define ST_DIO_OK           0040000u            /* (D) direct I/O OK to use */
#define ST_RATE_MASK        0034000u            /* clock rate mask */
#define ST_LR_EQ_CR         0000040u            /* (C) limit register = count register */
#define ST_LR_EQ_CR_OVFL    0000020u            /* (F) limit register = count register overflow */
#define ST_SYSTEM_IRQ       0000004u            /* (I) I/O system interrupt request */
#define ST_LIMIT_COUNT_SEL  0000002u            /* (L) limit/count (0/1) register selected */
#define ST_COUNT_RESET      0000001u            /* (R) count register is reset after LR=CR interrupt */

#define ST_RATE_SHIFT       11                  /* clock rate alignment shift */

#define ST_RATE(r)          ((r) << ST_RATE_SHIFT & ST_RATE_MASK)

#define ST_TO_RATE(s)       (((s) & ST_RATE_MASK) >> ST_RATE_SHIFT)

static const BITSET_NAME status_names [] = {    /* Status word names */
    "DIO OK",                                   /*   bit  1 */
    NULL,                                       /*   bit  2 */
    NULL,                                       /*   bit  3 */
    NULL,                                       /*   bit  4 */
    NULL,                                       /*   bit  5 */
    NULL,                                       /*   bit  6 */
    NULL,                                       /*   bit  7 */
    NULL,                                       /*   bit  8 */
    NULL,                                       /*   bit  9 */
    "LR = CR",                                  /*   bit 10 */
    "LR = CR overflow",                         /*   bit 11 */
    NULL,                                       /*   bit 12 */
    "system interrupt",                         /*   bit 13 */
    "\1count selected\0limit selected",         /*   bit 14 */
    "reset after interrupt"                     /*   bit 15 */
    };

static const BITSET_FORMAT status_format =      /* names, offset, direction, alternates, bar */
    { FMT_INIT (status_names, 0, msb_first, has_alt, append_bar) };


/* System clock state */

static FLIP_FLOP system_irq    = CLEAR;         /* SIN interrupt request flip-flop */
static FLIP_FLOP limit_irq     = CLEAR;         /* limit = count interrupt request flip-flop */
static FLIP_FLOP lost_tick_irq = CLEAR;         /* limit = count overflow interrupt request flip-flop */

static HP_WORD control_word;                    /* control word */
static HP_WORD status_word;                     /* status word */
static HP_WORD count_register;                  /* counter register */
static HP_WORD limit_register;                  /* limit register */
static uint32  rate;                            /* clock rate */
static uint32  prescaler;                       /* clock rate prescaler */

static uint32 increment     = 1;                /* count register increment */
static t_bool coschedulable = FALSE;            /* TRUE if the clock can be coscheduled with PCLK */
static t_bool coscheduled   = FALSE;            /* TRUE if the clock is coscheduled with PCLK */


/* System clock local SCP support routines */

static CNTLR_INTRF clk_interface;
static t_stat      clk_service   (UNIT   *uptr);
static t_stat      clk_reset     (DEVICE *dptr);


/* System clock local utility routines */

static void resync_clock (void);


/* System clock SCP interface data structures */


/* Device information block */

static DIB clk_dib = {
    &clk_interface,                             /* device interface */
    3,                                          /* device number */
    SRNO_UNUSED,                                /* service request number */
    1,                                          /* interrupt priority */
    INTMASK_UNUSED                              /* interrupt mask */
    };

/* Unit list */

static UNIT clk_unit [] = {
    { UDATA (&clk_service, UNIT_IDLE | UNIT_CALTIME, 0) }
    };

/* Register list */

static REG clk_reg [] = {
/*    Macro   Name    Location           Width  Offset        Flags        */
/*    ------  ------  -----------------  -----  ------  ------------------ */
    { ORDATA (CNTL,   control_word,       16)                              },
    { ORDATA (STAT,   status_word,        16)                              },
    { ORDATA (COUNT,  count_register,     16)                              },
    { ORDATA (LIMIT,  limit_register,     16)                              },
    { ORDATA (RATE,   rate,                3)                              },
    { FLDATA (SYSIRQ, system_irq,                 0)                       },
    { FLDATA (LIMIRQ, limit_irq,                  0)                       },
    { FLDATA (OVFIRQ, lost_tick_irq,              0)                       },

    { DRDATA (SCALE,  prescaler,          16),                    REG_HRO  },
    { DRDATA (INCR,   increment,          16),                    REG_HRO  },
    { FLDATA (COSOK,  coschedulable,              0),             REG_HRO  },
    { FLDATA (COSCH,  coscheduled,                0),             REG_HRO  },
    { DRDATA (UWAIT,  clk_unit [0].wait,  32),          PV_LEFT | REG_HRO  },

      DIB_REGS (clk_dib),

    { NULL }
    };

/* Modifier list */

static MTAB clk_mod [] = {
/*    Mask Value    Match Value   Print String         Match String  Validation  Display  Descriptor */
/*    ------------  ------------  -------------------  ------------  ----------  -------  ---------- */
    { UNIT_CALTIME, UNIT_CALTIME, "calibrated timing", "CALTIME",    NULL,       NULL,    NULL       },
    { UNIT_CALTIME, 0,            "realistic timing",  "REALTIME",   NULL,       NULL,    NULL       },

/*    Entry Flags  Value        Print String  Match String  Validation    Display        Descriptor        */
/*    -----------  -----------  ------------  ------------  ------------  -------------  ----------------- */
    { MTAB_XDV,    VAL_DEVNO,   "DEVNO",      "DEVNO",      &hp_set_dib,  &hp_show_dib,  (void *) &clk_dib },
    { MTAB_XDV,    VAL_INTPRI,  "INTPRI",     "INTPRI",     &hp_set_dib,  &hp_show_dib,  (void *) &clk_dib },
    { 0 }
    };

/* Debugging trace list */

static DEBTAB clk_deb [] = {
    { "CSRW",  DEB_CSRW  },                     /* interface control, status, read, and write actions */
    { "PSERV", DEB_PSERV },                     /* clock unit service scheduling calls */
    { "IOBUS", DEB_IOB   },                     /* interface I/O bus signals and data words */
    { NULL,    0         }
    };

/* Device descriptor */

DEVICE clk_dev = {
    "CLK",                                      /* device name */
    clk_unit,                                   /* unit array */
    clk_reg,                                    /* register array */
    clk_mod,                                    /* modifier array */
    1,                                          /* number of units */
    8,                                          /* address radix */
    PA_WIDTH,                                   /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    DV_WIDTH,                                   /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &clk_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &clk_dib,                                   /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    clk_deb,                                    /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* System clock global routines */



/* Update the counter register.

   If the clock is currently coscheduled with the CPU process clock, then the
   service interval is actually ten times the programmed rate.  To present the
   correct value when the counter register is read, this routine is called to
   increment the count by an amount proportional to the fraction of the service
   interval that has elapsed.  In addition, it's called by the CPU instruction
   postlude, so that the counter will have the correct value if it's examined
   from the SCP command prompt.

   This routine is also called when the counter is to be reset.  This ensures
   that the increment is reduced by the time elapsed before the counter is
   zeroed.
*/

void clk_update_counter (void)
{
int32 elapsed, ticks;

if (coscheduled) {                                      /* if the clock is coscheduled, then adjust the count */
    elapsed = clk_unit [0].wait                         /* the elapsed time is the original wait time */
                - sim_activate_time (&clk_unit [0]);    /*   less the time remaining before the next service */

    ticks = (elapsed * CLK_MULTIPLIER) / clk_unit [0].wait  /* the adjustment is the elapsed fraction of the multiplier */
              - (CLK_MULTIPLIER - increment);               /*   less the amount of any adjustment already made */

    count_register = count_register + ticks & R_MASK;   /* update the clock counter with rollover */
    increment = increment - ticks;                      /*   and reduce the amount remaining to add at service */
    }

return;
}



/* System clock local SCP support routines */



/* System clock interface.

   The system clock is installed on the IOP bus and receives direct I/O commands
   from the IOP.  It does not respond to Programmed I/O (SIO) commands.

   In simulation, the asserted signals on the bus are represented as bits in the
   inbound_signals set.  Each signal is processed sequentially in numerical
   order, and a set of similar outbound_signals is assembled and returned to the
   caller, simulating assertion of the corresponding bus signals.

   There is no interrupt mask; interrupts are always unmasked, and the interface
   does not respond to the SMSK I/O order.


   Implementation notes:

    1. In hardware, setting the tick rate in the control word addresses a
       multiplexer that selects one of the 10 MHz clock division counter outputs
       as the clock source for the count register.  Setting the rate bits to 0
       inhibits the count register, although the division counter continues to
       run.  In simulation, setting a new rate stops and then restarts the event
       service with the new delay time, equivalent in hardware to clearing the
       clock division counter.

    2. Receipt of a DRESETINT signal clears the interrupt request and active
       flip-flops but does not cancel a request that is pending but not yet
       serviced by the IOP.  However, when the IOP does service the request by
       asserting INTPOLLIN, the interface routine returns INTPOLLOUT, which will
       cancel the request.

    3. The "%.0s" print specification in the DCONTSTB trace call absorbs the
       rate name parameter without printing when the rate is not specified.
*/

static SIGNALS_DATA clk_interface (DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
INBOUND_SIGNAL signal;
INBOUND_SET    working_set      = inbound_signals;
HP_WORD        outbound_value   = 0;
OUTBOUND_SET   outbound_signals = NO_SIGNALS;

dprintf (clk_dev, DEB_IOB, "Received data %06o with signals %s\n",
         inbound_value, fmt_bitset (inbound_signals, inbound_format));

while (working_set) {
    signal = IONEXTSIG (working_set);                   /* isolate the next signal */

    switch (signal) {                                   /* dispatch an I/O signal */

        case DCONTSTB:
            control_word = inbound_value;               /* save the control word */

            if (control_word & CN_RESET_LOAD_SEL) {     /* if the reset/load selector is set */
                rate = CN_RATE (control_word);          /*   then load the clock rate */

                if (clk_unit [0].flags & UNIT_CALTIME)  /* if in calibrated timing mode */
                    prescaler = scale [rate];           /*   then set the prescaler */
                else                                    /* otherwise */
                    prescaler = 1;                      /*   the prescaler isn't used */

                sim_cancel (&clk_unit [0]);             /* changing the rate restarts the timing divider */

                if (rate > 0) {                                 /* if the rate is valid */
                    clk_unit [0].wait = delay [rate];           /*   then set the initial service delay */
                    sim_rtcn_init (clk_unit [0].wait, TMR_CLK); /* initialize the clock */
                    resync_clock ();                            /*   and reschedule the service */
                    }
                }

            else if (control_word & CN_MR) {            /* otherwise, if the master reset bit is set */
                clk_reset (&clk_dev);                   /*   then reset the interface */
                control_word = 0;                       /*     (which clears the other settings) */
                }

            if (control_word & CN_IRQ_RESET_ALL) {      /* if a reset of all interrupts is requested */
                limit_irq     = CLEAR;                  /*   then clear the limit = count, */
                lost_tick_irq = CLEAR;                  /*     limit = count overflow, */
                system_irq    = CLEAR;                  /*       and system flip-flops */
                }

            else if (control_word & CN_IRQ_RESET_MASK)  /* otherwise if any single resets are requested */
                switch (CN_RESET (control_word)) {      /*   then reset the specified flip-flop */
                    case 1:
                        limit_irq = CLEAR;              /* clear the limit = count interrupt request */
                        break;

                    case 2:
                        lost_tick_irq = CLEAR;          /* clear the limit = count overflow interrupt request */
                        break;

                    case 3:
                        system_irq = CLEAR;             /* clear the system interrupt request */
                        break;

                    default:                            /* the rest of the values do nothing */
                        break;
                    }

            if (dibptr->interrupt_active == CLEAR)      /* if no interrupt is active */
                working_set |= DRESETINT;               /*   then recalculate interrupt requests */

            dprintf (clk_dev, DEB_CSRW, (inbound_value & CN_RESET_LOAD_SEL
                                           ? "Control is %s | %s rate%s\n"
                                           : "Control is %s%.0s%s\n"),
                     fmt_bitset (inbound_value, control_format),
                     rate_name [CN_RATE (inbound_value)],
                     irq_reset_name [CN_RESET (inbound_value)]);
            break;


        case DSTATSTB:
            status_word = ST_DIO_OK | ST_RATE (rate);   /* set the clock rate */

            if (limit_irq)                              /* if the limit = count flip-flop is set */
                status_word |= ST_LR_EQ_CR;             /*   set the corresponding status bit */

            if (lost_tick_irq)                          /* if the limit = count overflow flip-flop is set */
                status_word |= ST_LR_EQ_CR_OVFL;        /*   set the corresponding status bit */

            if (system_irq)                             /* if the system interrupt request flip-flop is set */
                status_word |= ST_SYSTEM_IRQ;           /*   set the corresponding status bit */

            if (control_word & CN_LIMIT_COUNT_SEL)      /* if the limit/count selector is set */
                status_word |= ST_LIMIT_COUNT_SEL;      /*   set the corresponding status bit */

            if (control_word & CN_COUNT_RESET)          /* if the reset-after-interrupt selector is set */
                status_word |= ST_COUNT_RESET;          /*   set the corresponding status bit */

            outbound_value = status_word;               /* return the status word */

            dprintf (clk_dev, DEB_CSRW, "Status is %s%s rate\n",
                     fmt_bitset (outbound_value, status_format),
                     rate_name [ST_TO_RATE (outbound_value)]);
            break;


        case DREADSTB:
            clk_update_counter ();                          /* update the clock counter register */
            outbound_value = LOWER_WORD (count_register);   /*   and then read it */

            dprintf (clk_dev, DEB_CSRW, "Count register value %u returned\n",
                     count_register);
            break;


        case DWRITESTB:
            if (control_word & CN_LIMIT_COUNT_SEL) {    /* if the limit/count selector is set */
                clk_update_counter ();                  /*   then update the clock counter register */
                count_register = 0;                     /*     and then clear it */

                dprintf (clk_dev, DEB_CSRW, "Count register cleared\n");
                }

            else {                                      /* otherwise */
                limit_register = inbound_value;         /*   set the limit register to the supplied value */

                dprintf (clk_dev, DEB_CSRW, "Limit register value %u set\n",
                         limit_register);

                coschedulable = (ticks [rate] == 1000           /* the clock can be coscheduled if the rate */
                                  && limit_register == 100);    /*   is 1 msec and the limit is 100 ticks */
                }
            break;


        case DSETINT:
            system_irq = SET;                           /* set the system interrupt request flip-flop */

            dibptr->interrupt_request = SET;            /* request an interrupt */
            outbound_signals |= INTREQ;                 /*   and notify the IOP */
            break;


        case DRESETINT:
            dibptr->interrupt_active = CLEAR;           /* clear the Interrupt Active flip-flop */

            if ((limit_irq == SET || lost_tick_irq == SET)  /* if the limit or lost tick flip-flops are set */
              && control_word & CN_IRQ_ENABLE)              /*   and interrupts are enabled */
                dibptr->interrupt_request = SET;            /*     then set the interrupt request flip-flop */
            else                                            /* otherwise */
                dibptr->interrupt_request = system_irq;     /*   request an interrupt if the system flip-flop is set */

            if (dibptr->interrupt_request)              /* if a request is pending */
                outbound_signals |= INTREQ;             /*   then notify the IOP */
            break;


        case INTPOLLIN:
            if (dibptr->interrupt_request) {            /* if a request is pending */
                dibptr->interrupt_request = CLEAR;      /*   then clear it */
                dibptr->interrupt_active  = SET;        /*     and mark it as now active */

                outbound_signals |= INTACK;             /* acknowledge the interrupt */
                outbound_value = dibptr->device_number; /*   and return our device number */
                }

            else                                        /* otherwise the request has been reset */
                outbound_signals |= INTPOLLOUT;         /*   so let the IOP know to cancel it */
            break;


        case DSTARTIO:                                  /* not used by this interface */
        case DSETMASK:                                  /* not used by this interface */
        case ACKSR:                                     /* not used by this interface */
        case TOGGLESR:                                  /* not used by this interface */
        case SETINT:                                    /* not used by this interface */
        case PCMD1:                                     /* not used by this interface */
        case PCONTSTB:                                  /* not used by this interface */
        case SETJMP:                                    /* not used by this interface */
        case PSTATSTB:                                  /* not used by this interface */
        case PWRITESTB:                                 /* not used by this interface */
        case PREADSTB:                                  /* not used by this interface */
        case EOT:                                       /* not used by this interface */
        case TOGGLEINXFER:                              /* not used by this interface */
        case TOGGLEOUTXFER:                             /* not used by this interface */
        case READNEXTWD:                                /* not used by this interface */
        case TOGGLESIOOK:                               /* not used by this interface */
        case DEVNODB:                                   /* not used by this interface */
        case XFERERROR:                                 /* not used by this interface */
        case CHANSO:                                    /* not used by this interface */
        case PFWARN:                                    /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }

dprintf (clk_dev, DEB_IOB, "Returned data %06o with signals %s\n",
         outbound_value, fmt_bitset (outbound_signals, outbound_format));

return IORETURN (outbound_signals, outbound_value);     /* return the outbound signals and value */
}


/* Service the system clock unit.

   At each "tick" of the clock, the count register is incremented and compared
   to the limit register.  If they are equal, then the counter is cleared (if
   enabled) and an interrupt is generated (if enabled).

   If the clock is calibrated, a prescaler is used to achieve the 1 second and
   10 second periods while the event time remains at 100 milliseconds.  For
   periods shorter than 1 second, and for all realtime periods, the prescaler is
   not used (by setting the value to 1).

   If the clock is currently coscheduled with the CPU process clock, then the
   service interval is actually ten times the programmed rate, so the count
   register increment per service entry is 10 instead of 1.


   Implementation notes:

    1. The count/limit comparison hardware provides only an equal condition.  If
       the limit register is set to a value below the current count, or the
       LR=CR interrupt is not enabled until after the count register value has
       exceeded the limit, comparison will not occur until the count register
       overflows and again reaches the limit.
*/

static t_stat clk_service (UNIT *uptr)
{
dprintf (clk_dev, DEB_PSERV, "Service entered with counter %u increment %u limit %u\n",
         count_register, increment, limit_register);

prescaler = prescaler - 1;                              /* decrement the prescaler count */

if (prescaler == 0) {                                       /* if the prescaler count has expired */
    count_register = count_register + increment & R_MASK;   /*   then the count register counts up */

    if (count_register == limit_register) {             /* if the limit has been reached */
        if (limit_irq == SET)                           /*   then if the last limit interrupt wasn't serviced */
            lost_tick_irq = SET;                        /*     then set the overflow interrupt */
        else                                            /*   otherwise */
            limit_irq = SET;                            /*     set the limit interrupt */

        if (control_word & CN_COUNT_RESET)              /* if the counter reset option is selected */
            count_register = 0;                         /*   then clear the count register */

        if (control_word & CN_IRQ_ENABLE                /* if clock interrupts are enabled */
          && clk_dib.interrupt_active == CLEAR) {       /*   and the interrupt active flip-flop is clear */
            clk_dib.interrupt_request = SET;            /*     then request an interrupt */
            iop_assert_INTREQ (&clk_dib);               /*       and notify the IOP of the INTREQ signal */
            }
        }

    if (uptr->flags & UNIT_CALTIME)                     /* if in calibrated timing mode */
        prescaler = scale [rate];                       /*   then reset the prescaler */
    else                                                /* otherwise */
        prescaler = 1;                                  /*   the prescaler isn't used */
    }

if (!(uptr->flags & UNIT_CALTIME)) {                    /* if the clock is in real timing mode */
    uptr->wait = delay [rate];                          /*   then set an event-based delay */
    increment = 1;                                      /*     equal to the selected period */
    coscheduled = FALSE;                                /* the clock is not coscheduled with the process clock */
    }

else if (coschedulable && cpu_is_calibrated) {          /* otherwise if the process clock is calibrated */
    uptr->wait = sim_activate_time (cpu_pclk_uptr);     /*   then synchronize with it */
    increment = CLK_MULTIPLIER;                         /*     at one-tenth of the selected period */
    coscheduled = TRUE;                                 /* the clock is coscheduled with the process clock */
    }

else {                                                  /* otherwise */
    uptr->wait = sim_rtcn_calb (ticks [rate], TMR_CLK); /*   calibrate the clock to a delay */
    increment = 1;                                      /*     equal to the selected period */
    coscheduled = FALSE;                                /* the clock is not coscheduled with the process clock */
    }

dprintf (clk_dev, DEB_PSERV, "Rate %s delay %d service %s\n",
         rate_name [rate], uptr->wait,
         (coscheduled ? "coscheduled" : "scheduled"));

return sim_activate (uptr, uptr->wait);                 /* activate the unit and return the status */
}


/* Device reset.

   This routine is called for a RESET or RESET CLK command.  It is the
   simulation equivalent of the IORESET signal, which is asserted by the front
   panel LOAD and DUMP switches.

   For this interface, IORESET is identical to a Programmed Master Reset
   (control word bit 0 set with bit 3 clear).

   A master reset is generated either by an IORESET signal or a Direct I/O
   Master Reset (control word bit 0 set with bit 3 clear).


   Implementation notes:

    1. In simulation, the Enable Clock Interrupts flip-flop, the Reset Count
       Register after LR=CR Interrupt flip-flop, and the Address Limit/Count
       Register flip-flop are maintained in the control word rather than as
       separate values.

    2. The hardware interrupt circuitry contains an Interrupt Active flip-flop
       and an Interrupt Priority latch but no Interrupt Request flip-flop.
       Instead, the INTREQ signal is the logical OR of the LR=CR Interrupt and
       LR=CR Overflow Interrupt flip-flops (if enabled by the Enable Clock
       Interrupts flip-flop) with the the System Interrupt flip-flop.  In
       simulation, the interrupt_request flip-flop in the Device Information
       Block is set explicitly to reflect this logic.  Clearing the three
       interrupt source flip-flops therefore clears the interrupt_request
       flip-flop as well.

   3.  In simulation, the clock division counters are represented by the event
       service delay.  Stopping and restarting the delay is equivalent to
       clearing the division counters.
*/

static t_stat clk_reset (DEVICE *dptr)
{
count_register = 0;                                     /* clear the count */
limit_register = 0;                                     /*   and limit registers */

rate = 0;                                               /* clear the clock rate */
prescaler = 1;                                          /*   and set the clock prescaler */

sim_cancel (dptr->units);                               /* clearing the rate stops the clock */

clk_dib.interrupt_request = CLEAR;                      /* clear any current */
clk_dib.interrupt_active  = CLEAR;                      /*   interrupt request */

system_irq    = CLEAR;                                  /* clear the system, */
limit_irq     = CLEAR;                                  /*    limit = count, */
lost_tick_irq = CLEAR;                                  /*      and limit = count overflow flip-flops */

control_word = 0;                                       /* clear the enable, write select, and count reset actions */

return SCPE_OK;
}



/* System clock local utility routines */



/* Resynchronize the clock.

   After changing the rate or the limit, the new values are examined to see if
   the clock may be coscheduled with the process clock to permit idling.  If
   coscheduling is possible and both the system clock and the CPU process clock
   are calibrated, then the clock event service is synchronized with the process
   clock service.  Otherwise, the service time is set up but is otherwise
   asynchronous with the process clock.


   Implementation notes:

    1. To synchronize events, the clock must be activated absolutely, as a
       service event may already be scheduled, and normal activation will not
       disturb an existing event.
*/

static void resync_clock (void)
{
coschedulable = (ticks [rate] == 1000                   /* the clock can be coscheduled if the rate */
                  && limit_register == 100);            /*   is 1 msec and the limit is 100 ticks */

if (clk_unit [0].flags & UNIT_CALTIME                       /* if the clock is in calibrated timing mode */
  && coschedulable                                          /*   and may be coscheduled with the process clock */
  && cpu_is_calibrated) {                                   /*   and the process clock is calibrated */
    clk_unit [0].wait = sim_activate_time (cpu_pclk_uptr);  /*     then synchronize with it */
    coscheduled = TRUE;                                     /* the clock is coscheduled with the process clock */
    }

else {                                                  /* otherwise */
    clk_unit [0].wait = delay [rate];                   /*   set up an independent clock */
    coscheduled = FALSE;                                /* the clock is not coscheduled with the process clock */
    }

dprintf (clk_dev, DEB_PSERV, "Rate %s delay %d service rescheduled\n",
         rate_name [rate], clk_unit [0].wait);

sim_activate_abs (&clk_unit [0], clk_unit [0].wait);    /* restart the clock */

return;
}
