/* hp2100_mc.c: HP 12566B Microcircuit Interface simulator

   Copyright (c) 2018, J. David Bryan

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

   MC           12566B Microcircuit Interface

   09-Jul-18    JDB     Created

   References:
     - 12566B[-001/2/3] Microcircuit Interface Kits Operating and Service Manual
         (12566-90015,  April 1976)


   The 12566B Microcircuit Interface provides a general-purpose 16-bit
   bidirectional data path between the CPU and an I/O device that supports both
   programmed I/O and DMA transfers.  A simple, two-wire handshake provides the
   control signals to coordinate data transfers.  All device signals are
   TTL-compatible, and transfer rates up to one-half of the DMA bandwidth are
   possible.

   The 12566B supplies 16 data bits and asserts a Device Command signal to the
   device to indicate the start of a transfer.  The device returns 16 data bits
   and asserts a Device Flag signal to indicate completion of the transfer.
   Assertion of Device Flag causes the interface to set its Flag flip-flop and
   assert an interupt request to the CPU and a service request to the DMA
   controller.

   This simulation primarily provides a target interface for several diagnostic
   programs.  In the DIAGNOSTIC mode, a loopback connector is installed in place
   of the usual device cable, and the interface provides a general data source
   and sink, as well as a source of interrupts and a break in the I/O priority
   chain.  In the DEVICE mode, the simulation provides a model to illustrate the
   required interface to the CPU's I/O backplane, as no peripheral device is
   simulated.

   Befitting its general purpose, the hardware interface has nine jumpers,
   labelled W1-W9, that may be positioned to configure the electrical polarity
   and behavior of the Device Command and Device Flag signals.  The lettered
   jumper positions and their effects are:

     Jumper  Pos  Action
     ------  ---  ------------------------------------------------------------
       W1     A   Device Command signal is ground true asserted with STC
              B   Device Command signal is positive true asserted with STC
              C   Device Command signal is ground true asserted for T6 and T2

       W2     A   Device Command flip-flop clears on positive edge of Device Flag
              B   Device Command flip-flop clears on negative edge of Device Flag
              C   Device Command flip-flop clears on ENF (T2)

       W3     A   Device Flag signal sets Flag Buffer and strobes data on positive edge
              B   Device Flag signal sets Flag Buffer and strobes data on negative edge

       W4     A   Output Data Register is gated by the data flip-flop
              B   Output Data Register is continuously available

       W5    IN   Input Data Register bits 0-3 are latched by Device Flag
             OUT  Input Data Register bits 0-3 are transparent

       W6    IN   Input Data Register bits 4-7 are latched by Device Flag
             OUT  Input Data Register bits 4-7 are transparent

       W7    IN   Input Data Register bits 8-11 are latched by Device Flag
             OUT  Input Data Register bits 8-11 are transparent

       W8    IN   Input Data Register bits 12-15 are latched by Device Flag
             OUT  Input Data Register bits 12-15 are transparent

       W9     A   Device Command flip-flop cleared by CLC, CRS, and Device Flag
              B   Device Command flip-flop cleared by CRS and Device Flag

   The electrical characteristics of the device being interfaced dictates the
   jumper settings used.  The jumper settings required for the standard HP
   peripherals that use the microcircuit card are:

     W1  W2  W3  W4  W5  W6  W7  W8  W9  Device
     --- --- --- --- --- --- --- --- --- ----------------------------------------
      A   B   A   B  OUT IN  IN  IN   A  12566B-004 Line Printer Interface (9866)

      B   A   B   B  OUT IN  IN  OUT  A  12653A Line Printer Interface (2767)

      A   B   B   B  IN  IN  IN  OUT  B  12732A Flexible Disc Subsystem (Control)
      A   A   B   B  IN  IN  IN  IN   B  12732A Flexible Disc Subsystem (Data)

      A   B   B   B  IN  IN  IN  IN   A  12875A Processor Interconnect Kit

   For diagnostic use, the required jumper settings are:

     W1  W2  W3  W4  W5  W6  W7  W8  W9   DSN    Diagnostic
     --- --- --- --- --- --- --- --- --- ------  ---------------------------------
      C   B   B   B  IN  IN  IN  IN   A  143300  General Purpose Register

      C   B   B   B  IN  IN  IN  IN   A  141203  I/O Instruction Group

      C   B   B   B  IN  IN  IN  IN   A  102103  Memory Expansion Unit

      C   B   B   B  IN  IN  IN  IN   A  101220  DMA/DCPC for 2100/1000

      B   A   A   B  IN  IN  IN  IN   A    --    DMA for 2100 (HP 24195)

      B   A   A   B  IN  IN  IN  IN   A  101105  DMA for 2114/2115/2116 (HP 24322)
      B   C   A  A/B IN  IN  IN  IN   A  101105  DMA for 2114/2115/2116 (HP 24322)

      B   C   B   B  IN  IN  IN  IN   A    --    DMA for 2115/2116 (HP 24185)

      (not relevant; interrupt only)     101112  Extended Instruction Group

      (not relevant; interrupt only)     101213  M/E-Series Fast FORTRAN Package 1

      (not relevant; interrupt only)     101115  M/E-Series Fast FORTRAN Package 2

      (not relevant; interrupt only)     101121  F-Series FPP-SIS-FFP

      (not relevant; interrupt only)     102305  Memory Protect/Parity Error

   The diagnostics that specify jumper settings above test data writing and
   reading and so require the installation of the HP 1251-0332 diagnostic test
   (loopback) connector in place of the normal device cable connector.  This
   test connector connects each data output bit with its corresponding data
   input bit and connects the Device Command output signal to the Device Flag
   input signal.

   The diagnostics that test the HP 12607B DMA card for the 2115 and 2116 CPUs
   require an unusual jumper configuration.  The card provides hardware byte
   packing and unpacking during DMA transfers.  The diagnostics test this
   hardware by strapping the microcircuit interface so that the Device Flag
   signal sets the Flag flip-flop for a CPU cycle but not for a DMA cycle.  This
   allows the diagnostic to advance the DMA byte transfer hardware sequence
   cycle-by-cycle under program control.

   In hardware, this works only because the 2115/2116 DMA cycle asserts the STC
   and CLF signals for two staggered T-periods, with CLF remaining asserted for
   one T-period after STC denies.  The 2115/2116 CPU cycle, as well as the CPU
   and DMA cycles of all other 21xx/1000 machines, assert STC and CLF
   coincidently for one T-period.

   Under simulation, this action cannot be derived by simulating the jumper
   behaviors directly, because the I/O backplane signal timing relationships are
   not simulated.  Instead, Device Flag assertion is omitted for 2115/2116 DMA
   cycles when DIAGNOSTIC mode is selected.

   This module does not simulate the individual jumper settings, for two
   reasons.  First, with no peripheral device connected to the interface, the
   jumper settings are irrelevant.  Should this module be used as the basis for
   a specific device interface, that device would dictate the jumper settings
   required.  As the settings would be fixed, having a user-configurable set of
   jumpers would serve no purpose.  Second, while user-configurable jumpers
   would be useful to configure the card for diagnostics, the fact that the I/O
   backplane signal timing is not simulated means that the interface behavior
   cannot be derived from the jumper settings alone.  Therefore, entering the
   DIAGNOSTIC mode simulates the proper jumper settings for the various
   diagnostics listed above.


   Implementation notes:

     1. Two identical interfaces are provided: MC1 and MC2.  Both interfaces are
        used by the 12936A Privileged Interrupt Fence diagnostic.  They also
        serve as an illustration of how to model multiple interfaces in the HP
        2100 simulator.

     2. The microcircuit interfaces are disabled by default, as they are only
        used during diagnostic execution.
*/



#include "hp2100_defs.h"
#include "hp2100_io.h"



/* Program limits */

#define CARD_COUNT          2                   /* count of cards supported */


/* Device property constant declarations */

#define LOOPBACK_DELAY      (int32) uS (1)      /* diagnostic loopback flag assertion delay */


/* Unit flags */

#define UNIT_V_DIAG         (UNIT_V_UF + 0)     /* diagnostic mode is selected */

#define UNIT_DIAG           (1 << UNIT_V_DIAG)


/* Unit references */

typedef enum {
    mc1,                                        /* first microcircuit card index */
    mc2                                         /* second microcircuit card index */
    } CARD_INDEX;


/* Interface state */

typedef struct {
    HP_WORD      output_data;                   /* output data register */
    HP_WORD      input_data;                    /* input data register */
    FLIP_FLOP    command;                       /* command flip-flop */
    FLIP_FLOP    control;                       /* control flip-flop */
    FLIP_FLOP    flag;                          /* flag flip-flop */
    FLIP_FLOP    flag_buffer;                   /* flag buffer flip-flop */
    } CARD_STATE;

static CARD_STATE mc [CARD_COUNT];              /* per-card state */


/* Interface local SCP support routines */

static INTERFACE mc_interface;


/* Interface local SCP support routines */

static t_stat mc_service (UNIT *uptr);
static t_stat mc_reset   (DEVICE *dptr);


/* Interface SCP data structures */


/* Device information block */

static DIB mc_dib [CARD_COUNT] = {
    { &mc_interface,                            /* the device's I/O interface function pointer */
      MC1,                                      /* the device's select code (02-77) */
      0,                                        /* the card index */
      "12566B Microcircuit Interface",          /* the card description */
      NULL },                                   /* the ROM description */

    { &mc_interface,                            /* the device's I/O interface function pointer */
      MC2,                                      /* the device's select code (02-77) */
      1,                                        /* the card index */
      "12566B Microcircuit Interface",          /* the card description */
      NULL }                                    /* the ROM description */
    };


/* Unit list */

#define UNIT_FLAGS          (0)

static UNIT mc_unit [CARD_COUNT] = {
/*           Event Routine  Unit Flags  Capacity  Delay */
/*           -------------  ----------  --------  ----- */
    { UDATA (&mc_service,   UNIT_FLAGS,    0),      0   },
    { UDATA (&mc_service,   UNIT_FLAGS,    0),      0   }
    };


/* Register lists.


   Implementation notes:

    1. The two REG arrays would be more conveniently declared as:

         static REG mc_reg [CARD_COUNT] [] = { { ... }, { ... } };

       Unfortunately, this is illegal in C ("a multidimensional array must have
       bounds for all dimensions except the first").
*/

static REG mc1_reg [] = {
/*    Macro   Name    Location                    Width     Offset        Flags       */
/*    ------  ------  ------------------------  ----------  ------  ----------------- */
    { ORDATA (IN,     mc [mc1].input_data,         16)                                },
    { ORDATA (OUT,    mc [mc1].output_data,        16)                                },
    { FLDATA (CTL,    mc [mc1].control,                       0)                      },
    { FLDATA (FLG,    mc [mc1].flag,                          0)                      },
    { FLDATA (FBF,    mc [mc1].flag_buffer,                   0)                      },
    { FLDATA (CMD,    mc [mc1].command,                       0)                      },

      DIB_REGS (mc_dib [mc1]),

    { NULL }
    };

static REG mc2_reg [] = {
/*    Macro   Name    Location                    Width     Offset        Flags       */
/*    ------  ------  ------------------------  ----------  ------  ----------------- */
    { ORDATA (IN,     mc [mc2].input_data,         16)                                },
    { ORDATA (OUT,    mc [mc2].output_data,        16)                                },
    { FLDATA (CTL,    mc [mc2].control,                       0)                      },
    { FLDATA (FLG,    mc [mc2].flag,                          0)                      },
    { FLDATA (FBF,    mc [mc2].flag_buffer,                   0)                      },
    { FLDATA (CMD,    mc [mc2].command,                       0)                      },

      DIB_REGS (mc_dib [mc2]),

    { NULL }
    };


/* Modifier lists */

static MTAB mc1_mod [] = {
/*    Mask Value  Match Value  Print String       Match String  Validation   Display  Descriptor */
/*    ----------  -----------  -----------------  ------------  -----------  -------  ---------- */
    { UNIT_DIAG,  UNIT_DIAG,   "diagnostic mode", "DIAGNOSTIC", NULL,        NULL,    NULL       },
    { UNIT_DIAG,  0,           "device mode",     "DEVICE",     NULL,        NULL,    NULL       },

/*    Entry Flags  Value  Print String  Match String  Validation    Display        Descriptor             */
/*    -----------  -----  ------------  ------------  ------------  -------------  ---------------------- */
    { MTAB_XDV,     1u,   "SC",         "SC",         &hp_set_dib,  &hp_show_dib,  (void *) &mc_dib [mc1] },
    { 0 }
    };

static MTAB mc2_mod [] = {
/*    Mask Value  Match Value  Print String       Match String  Validation   Display  Descriptor */
/*    ----------  -----------  -----------------  ------------  -----------  -------  ---------- */
    { UNIT_DIAG,  UNIT_DIAG,   "diagnostic mode", "DIAGNOSTIC", NULL,        NULL,    NULL       },
    { UNIT_DIAG,  0,           "device mode",     "DEVICE",     NULL,        NULL,    NULL       },

/*    Entry Flags  Value  Print String  Match String  Validation    Display        Descriptor             */
/*    -----------  -----  ------------  ------------  ------------  -------------  ---------------------- */
    { MTAB_XDV,     1u,   "SC",         "SC",         &hp_set_dib,  &hp_show_dib,  (void *) &mc_dib [mc2] },
    { 0 }
    };


/* Debugging trace list */

static DEBTAB mc_deb [] = {
    { "XFER",  TRACE_XFER  },                   /* trace data transmissions and receptions */
    { "IOBUS", TRACE_IOBUS },                   /* trace I/O bus signals and data words received and returned */
    { NULL,    0           }
    };


/* Device descriptors */

DEVICE mc1_dev = {
    "MC1",                                      /* device name */
    &mc_unit [mc1],                             /* unit array */
    mc1_reg,                                    /* register array */
    mc1_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &mc_reset,                                  /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &mc_dib [mc1],                              /* device information block pointer */
    DEV_DISABLE | DEV_DIS | DEV_DEBUG,          /* device flags */
    0,                                          /* debug control flags */
    mc_deb,                                     /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };

DEVICE mc2_dev = {
    "MC2",                                      /* device name */
    &mc_unit [mc2],                             /* unit array */
    mc2_reg,                                    /* register array */
    mc2_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &mc_reset,                                  /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &mc_dib [mc2],                              /* device information block pointer */
    DEV_DISABLE | DEV_DIS | DEV_DEBUG,          /* device flags */
    0,                                          /* debug control flags */
    mc_deb,                                     /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };

static DEVICE *dptrs [CARD_COUNT] = {           /* device pointer array, indexed by CARD_INDEX */
    &mc1_dev,
    &mc2_dev
    };



/* Microcircuit interface.

   The microcircuit interface is installed on the I/O bus and receives I/O
   commands from the CPU and DMA/DCPC channels.  In simulation, the asserted
   signals on the bus are represented as bits in the inbound_signals set.  Each
   signal is processed sequentially in ascending numerical order.  The outbound
   signals and optional data value are returned after inbound signal processing
   is complete.

   In DIAGNOSTIC mode, the interface behaves as though a loopback connector
   is installed.  In addition, for all accesses other than DMA cycles for a 2115 or
   2116 CPU, it behaves as though jumpers W1-W3 are installed in locations
   C-B-B, respectively.  In this case, the Flag flip-flop sets one I/O cycle
   after STC signal assertion.  For 2115/2116 DMA cycles, it behaves as though
   the jumpers are installed in locations B-C-A, which suppresses setting the
   Flag flip-flop.

   Because there is no attached peripheral, the Flag flip-flop never sets in
   DEVICE mode in response to a programmed STC instruction.


   Implementation notes:

    1. The B-C-B jumper setting used by the HP 24185 DMA diagnostic causes the
       Flag flip-flop to set two I/O cycles after STC assertion.  However, the
       diagnostic executes an STC,C instruction at that point that clears the
       Flag flip-flop explictly.  This has the same effect as if the Flag had
       never set and so is functionally identical to the B-C-A jumper setting.

    2. The 12195 DMA diagnostic depends on the input data register being clocked
       by an STC instruction.  W1 = B and W3 = A asserts Device Command positive
       true and strobes the input register on the positive edge of Device Flag.
       This is simulated by copying the output data register to the input data
       register in the STC handler if DIAGNOSTIC mode is enabled.
*/

static SIGNALS_VALUE mc_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
const CARD_INDEX card = (CARD_INDEX) dibptr->card_index;    /* the card selector */
UNIT * const     uptr = &(mc_unit [card]);                  /* the associated unit pointer */
INBOUND_SIGNAL   signal;
INBOUND_SET      working_set = inbound_signals;
SIGNALS_VALUE    outbound    = { ioNONE, 0 };
t_bool           irq_enabled = FALSE;

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            mc [card].flag_buffer = CLEAR;              /* clear the flag buffer */
            mc [card].flag        = CLEAR;              /*   and flag flip-flops */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            mc [card].flag_buffer = SET;                /* set the flag buffer flip-flop */
            break;


        case ioENF:                                     /* Enable Flag */
            if (mc [card].flag_buffer == SET)           /* if the flag buffer flip-flop is set */
                mc [card].flag = SET;                   /*   then set the flag flip-flop */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (mc [card].flag == CLEAR)                /* if the flag flip-flop is clear */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (mc [card].flag == SET)                  /* if the flag flip-flop is set */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                     /* I/O data input */
            outbound.value = mc [card].input_data;      /* set the outbound data value from the input buffer */
            break;


        case ioIOO:                                     /* I/O data output */
            mc [card].output_data = inbound_value;      /* store the inbound data value in the output buffer */
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            mc [card].flag_buffer = SET;                /* set the flag buffer flip-flop */
            mc [card].output_data = 0;                  /*   and clear the output register */
            break;


        case ioCRS:                                     /* Control Reset */
            mc [card].control = CLEAR;                  /* clear the control flip-flop */
            mc [card].command = CLEAR;                  /*   and the command flip-flop */

            sim_cancel (uptr);                          /* cancel any operation in progress */
            break;


        case ioCLC:                                     /* Clear Control flip-flop */
            mc [card].control = CLEAR;                  /* clear the control flip-flop */
            mc [card].command = CLEAR;                  /*   and the command flip-flop */

            if (sim_activate_time (uptr) > LOOPBACK_DELAY)  /* if Device Flag assertion is still in the future */
                sim_cancel (uptr);                          /*   then cancel it */
            break;


        case ioSTC:                                     /* Set Control flip-flop */
            mc [card].control = SET;                    /* set the control flip-flop */
            mc [card].command = SET;                    /*   and the command flip-flop */

            if (uptr->flags & UNIT_DIAG                             /* if the card is in diagnostic mode */
              && (cpu_configuration & ~(CPU_2116 | CPU_2115)        /*   and this is not a 2116 or 2115 CPU */
              || (inbound_signals & (ioIOI | ioIOO)) == ioNONE)) {  /*     or not a DMA cycle */
                mc [card].input_data = mc [card].output_data;       /*       then loop the data back */

                tpprintf (dptrs [card], TRACE_XFER, "Output data word %06o looped back to input\n",
                          mc [card].output_data);

                sim_activate_abs (uptr, LOOPBACK_DELAY);            /* schedule Device Flag assertion */
                }
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (mc [card].control & mc [card].flag)     /* if the control and flag flip-flops are set */
                outbound.signals |= cnVALID;            /*   then deny PRL */
            else                                        /* otherwise */
                outbound.signals |= cnPRL | cnVALID;    /*   conditionally assert PRL */

            if (mc [card].control & mc [card].flag      /* if the control and flag */
              & mc [card].flag_buffer)                  /*   and flag buffer flip-flops are set */
                outbound.signals |= cnIRQ | cnVALID;    /*     then conditionally assert IRQ */

            if (mc [card].flag == SET)                  /* if the flag flip-flop is set */
                outbound.signals |= ioSRQ;              /*   then assert SRQ */
            break;


        case ioIAK:                                     /* Interrupt Acknowledge */
            mc [card].flag_buffer = CLEAR;              /* clear the flag buffer flip-flop */
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


/* Unit service */

static t_stat mc_service (UNIT *uptr)
{
const CARD_INDEX card = (CARD_INDEX) (uptr - mc_unit);  /* the card selector */

if (uptr->flags & UNIT_DIAG) {                          /* if the card is in diagnostic mode */
    mc [card].command     = CLEAR;                      /*   then clear the command flip-flop */
    mc [card].flag_buffer = SET;                        /*     and set the flag buffer */
    io_assert (dptrs [card], ioa_ENF);                  /*       and flag flip-flops */
    }

return SCPE_OK;
}


/* Reset routine */

static t_stat mc_reset (DEVICE *dptr)
{
UNIT * const uptr = dptr->units;                        /* a pointer to the device's unit */

if (sim_switches & SWMASK ('P')) {                      /* if this is the power-on reset */
    }                                                   /*   then perform any power-up processing */

io_assert (dptr, ioa_POPIO);                            /* PRESET the device */

sim_cancel (uptr);                                      /* cancel any I/O in progress */

return SCPE_OK;
}
