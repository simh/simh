/* hp2100_pif.c: HP 12620A/12936A Privileged Interrupt Fence simulator

   Copyright (c) 2008-2018, J. David Bryan

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

   PIF          12620A/12936A Privileged Interrupt Fence

   11-Jun-18    JDB     Revised I/O model
   15-Mar-17    JDB     Trace flags are now global
   11-Mar-17    JDB     Revised the trace outputs
   17-Jan-17    JDB     Changed "hp_---sc" and "hp_---dev" to "hp_---_dib"
   13-May-16    JDB     Modified for revised SCP API function parameter types
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   18-Jun-08    JDB     Created PIF device

   References:
     - 12620A Breadboard Interface Kit Operating and Service Manual
         (12620-90001, May 1978)
     - 12936A Privileged Interrupt Fence Accessory Installation and Service Manual
         (12936-90001, March 1974)


   The Privileged Interupt Fence (PIF) was used in DOS and RTE systems to
   provide privileged interrupt capability.  In non-privileged systems, DOS and
   RTE vectored all interrupts though the Central Interrupt Control (CIC)
   routine.  Within CIC, the interrupt system was turned off, the interrupt was
   categorized, the associated driver was identified and mapped into logical
   memory (if necessary), and the driver entered to handle the device service.
   When the driver exited, the interrupt system was turned on before returning
   to the point of interruption in the user's program.  In addition, the DOS and
   RTE operating systems themselves executed with the interrupt system off, as
   they were not reentrant.

   This process proved too lengthy for certain devices, which would lose
   interrupts or be forced to limit I/O speeds as a result.  To allow faster
   service, a driver could be written as a "privileged" driver and generated
   into a privileged system.  A privileged system operated with the interrupt
   system on when handling unprivileged device interrupts or executing within
   the operating system.  The PIF card was installed in the I/O backplane to
   separate privileged from unprivileged devices by controlling the interrupt
   priority chain signal (PRL) to lower-priority devices.  The privileged cards
   located below the fence were allowed to interrupt the service routines of the
   unprivileged cards that were located above the fence.

   When an unprivileged device interrupted, CIC would be entered as usual, and
   the interrupt system would be turned off.  However, after the system state
   was saved, the PIF would be configured to break the priority chain (deny
   PRL), so that subsequent interrupts from all unprivileged devices would be
   deferred.  Then the interrupt system would be turned on before normal CIC
   processing continued.  Interrupts from additional unprivileged devices would
   be held off by the PIF until the driver completed and CIC returned, just as
   in a non-privileged system.

   However, if a privileged device interrupted, the interrupt would be allowed,
   because the interrupt system was on, and the priority chain was intact for
   the devices below the fence.  A privileged device bypassed CIC and entered
   the associated device driver directly, and this would occur even if an
   unprivileged device driver or the operating system itself were executing.
   This provided very fast interrupt service time.

   HP produced two PIF cards: the 12936A Privileged Interrupt Fence Accessory
   for DOS, and the 12620A Breadboard Interface for RTE.  They behaved quite
   differently and were not interchangeable.

   The 12620A had the standard control and flag circuitry.  It behaved as most
   cards did; setting control and flag together lowered PRL and generated an
   interrupt.  The control and flag flip-flops were set and cleared with STC/CLC
   and STF/CLF instructions.  The SFS/SFC instructions could be used to test the
   flag state.

   The 12936A had a unique behavior.  Setting either control or flag lowered
   PRL.  An interrupt occurred when flag was set and control was clear.  The
   control flip-flop was controlled with STC/CLC.  The flag flip-flop was set
   with OTA/B and cleared with CLF.  SFC and SFS were not implemented and never
   skipped.
*/



#include "hp2100_defs.h"
#include "hp2100_io.h"



/* Device flags */

#define DEV_V_12936     (DEV_V_UF + 0)                  /* 12936A card */

#define DEV_12936       (1 << DEV_V_12936)


/* Interface state */

typedef struct {
    FLIP_FLOP  control;                         /* control flip-flop */
    FLIP_FLOP  flag;                            /* flag flip-flop */
    FLIP_FLOP  flag_buffer;                     /* flag buffer flip-flop */
    } CARD_STATE;

static CARD_STATE pif;                          /* per-card state */


/* Interface local SCP support routines */

static INTERFACE pif_interface;


/* Interface local SCP support routines */

static t_stat pif_reset     (DEVICE *dptr);
static t_stat pif_set_card  (UNIT   *uptr, int32  val,  CONST char *cptr, void *desc);
static t_stat pif_show_card (FILE   *st,   UNIT  *uptr, int32 val,        CONST void *desc);


/* Interface SCP data structures */


/* Device information block */

static DIB pif_dib = {
    &pif_interface,                             /* the device's I/O interface function pointer */
    PIF,                                        /* the device's select code (02-77) */
    0,                                          /* the card index */
    "12620A/12936A Privileged Interrupt Fence", /* the card description */
    NULL                                        /* the ROM description */
    };


/* Unit list.


   Implementation notes:

    1. The SIMH developer's manual says that a device's unit list may be NULL.
       However, if this is done, the register state cannot be examined or
       altered via SCP.  To work around this problem, we define a dummy unit
       that is not used otherwise.
*/

static UNIT pif_unit [] = {
    { UDATA (NULL, 0, 0) }
    };


/* Register list */

static REG pif_reg [] = {
/*    Macro   Name    Location             Offset */
/*    ------  ------  -------------------  ------ */
    { FLDATA (CTL,    pif.control,           0)   },
    { FLDATA (FLG,    pif.flag,              0)   },
    { FLDATA (FBF,    pif.flag_buffer,       0)   },

      DIB_REGS (pif_dib),

    { NULL }
    };


/* Modifier list */

static MTAB pif_mod [] = {
/*    Entry Flags          Value  Print String  Match String  Validation      Display         Descriptor        */
/*    -------------------  -----  ------------  ------------  --------------  --------------  ----------------- */
    { MTAB_XDV,              0,   NULL,         "12620A",     &pif_set_card, NULL,            NULL              },
    { MTAB_XDV,              1,   NULL,         "12936A",     &pif_set_card, NULL,            NULL              },
    { MTAB_XDV,              0,   "TYPE",       NULL,         NULL,          &pif_show_card,  NULL              },

    { MTAB_XDV,              1u,  "SC",         "SC",         &hp_set_dib,   &hp_show_dib,    (void *) &pif_dib },
    { MTAB_XDV | MTAB_NMO,  ~1u,  "DEVNO",      "DEVNO",      &hp_set_dib,   &hp_show_dib,    (void *) &pif_dib },
    { 0 }
    };


/* Debugging trace list */

static DEBTAB pif_deb [] = {
    { "CMD",   TRACE_CMD   },                   /* interface commands */
    { "IOBUS", TRACE_IOBUS },                   /* interface I/O bus signals and data words */
    { NULL,    0           }
    };


/* Device descriptor */

DEVICE pif_dev = {
    "PIF",                                      /* device name */
    pif_unit,                                   /* unit array */
    pif_reg,                                    /* register array */
    pif_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &pif_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &pif_dib,                                   /* device information block */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    pif_deb,                                    /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL };                                     /* logical device name */



/* Interface local SCP support routines */



/* Privileged interrupt fence interface.

   Operation of the 12620A and the 12936A is different.  The I/O responses of
   the two cards are summarized below:

     Signal   12620A Action          12936A Action
     ------   --------------------   --------------------
     POPIO    Set FBF, FLG           Clear FBF, FLG
      CRS     Clear CTL              Clear CTL
      CLC     Clear CTL              Clear CTL
      STC     Set CTL                Set CTL
      CLF     Clear FBF, FLG         Clear FBF, FLG
      STF     Set FBF, FLG           none
      SFC     Skip if FLG clear      none
      SFS     Skip if FLG set        none
      IOI     none                   none
      IOO     none                   Set FBF, FLG
      PRL     ~(CTL * FLG)           ~(CTL + FLG)
      IRQ     CTL * FLG * FBF        ~CTL * FLG * FBF
      IAK     Clear FBF              Clear FBF
      SRQ     Follows FLG            Not driven

   Note that PRL and IRQ are non-standard for the 12936A.
*/

static SIGNALS_VALUE pif_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
const t_bool   is_rte_pif  = (pif_dev.flags & DEV_12936) == 0;  /* TRUE if 12620A card */
INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };
t_bool         irq_enabled = FALSE;

while (working_set) {
    signal = IONEXTSIG (working_set);                   /* isolate the next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            pif.flag_buffer = CLEAR;                    /* clear flag buffer and flag */
            pif.flag        = CLEAR;
            break;


        case ioSTF:                                     /* set flag flip-flop */
            if (is_rte_pif)                             /* RTE PIF? */
                pif.flag_buffer = SET;                  /* set flag buffer */
            break;


        case ioENF:                                     /* enable flag */
            if (pif.flag_buffer == SET)                 /* if the flag buffer flip-flop is set */
                pif.flag = SET;                         /*   then set the flag flip-flop */
            break;


        case ioSFC:                                     /* skip if flag is clear */
            if (is_rte_pif && pif.flag == CLEAR)        /* only the 12620A card */
                outbound.signals |= ioSKF;              /*   responds to SFC */
            break;


        case ioSFS:                                     /* skip if flag is set */
            if (is_rte_pif && pif.flag == SET)          /* only the 12620A card */
                outbound.signals |= ioSKF;              /*   responds to SFS */
            break;


        case ioIOO:                                     /* I/O data output */
            if (is_rte_pif == FALSE) {                  /* DOS PIF? */
                pif.flag_buffer = SET;                  /* set flag buffer */
                working_set |= ioENF | ioSIR;           /* set ENF and SIR (not normally done for IOO) */
                }
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            if (is_rte_pif)
                pif.flag_buffer = SET;

            else {
                pif.flag_buffer = CLEAR;
                pif.flag        = CLEAR;
                }

            tprintf (pif_dev, TRACE_CMD, "Power-on reset\n");
            break;


        case ioCRS:                                     /* control reset */
            pif.control = CLEAR;                        /* clear control */
            tprintf (pif_dev, TRACE_CMD, "Control reset\n");
            break;


        case ioCLC:                                     /* clear control flip-flop */
            pif.control = CLEAR;                        /* clear control */
            break;


        case ioSTC:                                     /* set control flip-flop */
            pif.control = SET;                          /* set control */
            break;


        case ioSIR:                                         /* set interrupt request */
            if (is_rte_pif & pif.control & pif.flag         /* if control and flag are set (12620A) */
              || !is_rte_pif & (pif.control | pif.flag))    /*   or control or flag are clear (12936A) */
                outbound.signals |= cnVALID;                /*     then deny PRL */
            else                                            /*   otherwise */
                outbound.signals |= cnPRL | cnVALID;        /*     conditionally assert PRL */

            if (~(is_rte_pif ^ pif.control)                 /* if control is set (12620A) or clear (12936A) */
              & pif.flag & pif.flag_buffer)                 /*   and flag and flag buffer are set */
                outbound.signals |= cnIRQ | cnVALID;        /*     then conditionally assert IRQ */

            if (is_rte_pif && pif.flag == SET)              /* if 12620A and flag is set */
                outbound.signals |= ioSRQ;                  /*   then assert SRQ */

            tprintf (pif_dev, TRACE_CMD, "Fence %s%s lower-priority interrupts\n",
                     (outbound.signals & cnIRQ ? "requests an interrupt and " : ""),
                     (outbound.signals & cnPRL ? "allows" : "inhibits"));
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            pif.flag_buffer = CLEAR;
            break;


        case ioIEN:                                     /* interrupt enable */
            irq_enabled = TRUE;
            break;


        case ioPRH:                                         /* priority high */
            if (irq_enabled && outbound.signals & cnIRQ)    /* if IRQ is enabled and conditionally asserted */
                outbound.signals |= ioIRQ | ioFLG;          /*   then assert IRQ and FLG */

            if (!irq_enabled || outbound.signals & cnPRL)   /* if IRQ is disabled or PRL is conditionally asserted */
                outbound.signals |= ioPRL;                  /*   then assert it unconditionally */
            break;


        case ioIOI:                                     /* not used by this interface */
        case ioEDT:                                     /* not used by this interface */
        case ioPON:                                     /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }

return outbound;                                        /* return the outbound signals and value */
}


/* Simulator reset routine */

static t_stat pif_reset (DEVICE *dptr)
{
io_assert (dptr, ioa_POPIO);                            /* PRESET the device */
return SCPE_OK;
}



/* Privileged interrupt fence local utility routines */


/* Set card type.

   val == 0 --> set to 12936A (DOS PIF)
   val == 1 --> set to 12620A (RTE PIF)
*/

static t_stat pif_set_card (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if ((val < 0) || (val > 1) || (cptr != NULL))           /* sanity check */
    return SCPE_ARG;                                    /* bad argument */

if (val)                                                /* DOS PIF selected? */
    pif_dev.flags = pif_dev.flags | DEV_12936;          /* set to 12936A */
else                                                    /* RTE PIF selected */
    pif_dev.flags = pif_dev.flags & ~DEV_12936;         /* set to 12620A */

return SCPE_OK;
}


/* Show card type */

static t_stat pif_show_card (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (pif_dev.flags & DEV_12936)
    fputs ("12936A", st);
else
    fputs ("12620A", st);

return SCPE_OK;
}
