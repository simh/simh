/* hp2100_pif.c: HP 12620A/12936A privileged interrupt fence simulator

   Copyright (c) 2008-2016, J. David Bryan

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

   PIF          12620A/12936A privileged interrupt fence

   13-May-16    JDB     Modified for revised SCP API function parameter types
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   18-Jun-08    JDB     Created PIF device

   References:
   - 12620A Breadboard Interface Kit Operating and Service Manual
     (12620-90001, May-1978)
   - 12936A Privileged Interrupt Fence Accessory Installation and Service Manual
     (12936-90001, Mar-1974)


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


/* Device flags */

#define DEV_V_12936     (DEV_V_UF + 0)                  /* 12936A card */

#define DEV_12936       (1 << DEV_V_12936)


/* PIF state variables */

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } pif = { CLEAR, CLEAR, CLEAR };


/* PIF global routines */

IOHANDLER pif_io;

t_stat pif_reset     (DEVICE *dptr);
t_stat pif_set_card  (UNIT   *uptr, int32  val,  CONST char *cptr, void *desc);
t_stat pif_show_card (FILE   *st,   UNIT  *uptr, int32 val,        CONST void *desc);


/* PIF data structures.

   pif_dib     PIF device information block
   pif_unit    PIF unit list
   pif_reg     PIF register list
   pif_mod     PIF modifier list
   pif_deb     PIF debug list
   pif_dev     PIF device descriptor

   Implementation note:

    1. The SIMH developer's manual says that a device's unit list may be NULL.
       However, if this is done, the register state cannot be examined or
       altered via SCP.  To work around this problem, we define a dummy unit
       that is not used otherwise.
*/

DEVICE pif_dev;

DIB pif_dib = { &pif_io, PIF };

UNIT pif_unit = {
    UDATA (NULL, 0, 0)                                  /* dummy unit */
    };

REG pif_reg [] = {
    { FLDATA (CTL,   pif.control,         0)  },
    { FLDATA (FLG,   pif.flag,            0)  },
    { FLDATA (FBF,   pif.flagbuf,         0)  },
    { ORDATA (SC,    pif_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, pif_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB pif_mod [] = {
    { MTAB_XTD | MTAB_VDV,            0, NULL,    "12620A", &pif_set_card, NULL,           NULL     },
    { MTAB_XTD | MTAB_VDV,            1, NULL,    "12936A", &pif_set_card, NULL,           NULL     },
    { MTAB_XTD | MTAB_VDV,            0, "TYPE",  NULL,     NULL,          &pif_show_card, NULL     },
    { MTAB_XTD | MTAB_VDV,            0, "SC",    "SC",     &hp_setsc,     &hp_showsc,     &pif_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "DEVNO", "DEVNO",  &hp_setdev,    &hp_showdev,    &pif_dev },
    { 0 }
    };

DEVICE pif_dev = {
    "PIF",                                  /* device name */
    &pif_unit,                              /* unit array */
    pif_reg,                                /* register array */
    pif_mod,                                /* modifier array */
    1,                                      /* number of units */
    10,                                     /* address radix */
    31,                                     /* address width */
    1,                                      /* address increment */
    8,                                      /* data radix */
    8,                                      /* data width */
    NULL,                                   /* examine routine */
    NULL,                                   /* deposit routine */
    &pif_reset,                             /* reset routine */
    NULL,                                   /* boot routine */
    NULL,                                   /* attach routine */
    NULL,                                   /* detach routine */
    &pif_dib,                               /* device information block */
    DEV_DEBUG | DEV_DISABLE,                /* device flags */
    0,                                      /* debug control flags */
    NULL,                                   /* debug flag name table */
    NULL,                                   /* memory size change routine */
    NULL };                                 /* logical device name */


/* I/O signal handler.

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

uint32 pif_io (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
const char *hold_or_clear = (signal_set & ioCLF ? ",C" : "");
const t_bool is_rte_pif = (pif_dev.flags & DEV_12936) == 0;

IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            pif.flag = pif.flagbuf = CLEAR;             /* clear flag buffer and flag */

            if (DEBUG_PRS (pif_dev))
                fputs (">>PIF: [CLF] Flag cleared\n", sim_deb);
            break;


        case ioSTF:                                     /* set flag flip-flop */
            if (is_rte_pif) {                           /* RTE PIF? */
                pif.flag = pif.flagbuf = SET;           /* set flag buffer and flag */

                if (DEBUG_PRS (pif_dev))
                    fputs (">>PIF: [STF] Flag set\n", sim_deb);
                }
            break;


        case ioSFC:                                     /* skip if flag is clear */
            if (is_rte_pif)                             /* RTE PIF? */
                setstdSKF (pif);                        /* card responds to SFC */
            break;


        case ioSFS:                                     /* skip if flag is set */
            if (is_rte_pif)                             /* RTE PIF? */
                setstdSKF (pif);                        /* card responds to SFS */
            break;


        case ioIOO:                                     /* I/O data output */
            if (!is_rte_pif) {                          /* DOS PIF? */
                pif.flag = pif.flagbuf = SET;           /* set flag buffer and flag */
                working_set = working_set | ioSIR;      /* set SIR (not normally done for IOO) */

                if (DEBUG_PRS (pif_dev))
                    fprintf (sim_deb, ">>PIF: [OTx%s] Flag set\n", hold_or_clear);
                }
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            pif.flag = pif.flagbuf =                    /* set or clear flag and flag buffer */
                (is_rte_pif ? SET : CLEAR);

            if (DEBUG_PRS (pif_dev))
                fprintf (sim_deb, ">>PIF: [POPIO] Flag %s\n",
                                  (is_rte_pif ? "set" : "cleared"));
            break;


        case ioCRS:                                     /* control reset */
        case ioCLC:                                     /* clear control flip-flop */
            pif.control = CLEAR;                        /* clear control */

            if (DEBUG_PRS (pif_dev))
                fprintf (sim_deb, ">>PIF: [%s%s] Control cleared\n",
                                  (signal == ioCRS ? "CRS" : "CLC"), hold_or_clear);
            break;


        case ioSTC:                                     /* set control flip-flop */
            pif.control = SET;                          /* set control */

            if (DEBUG_PRS (pif_dev))
                fprintf (sim_deb, ">>PIF: [STC%s] Control set\n", hold_or_clear);
            break;


        case ioSIR:                                         /* set interrupt request */
            if (is_rte_pif) {                               /* RTE PIF? */
                setstdPRL (pif);                            /* set standard PRL signal */
                setstdIRQ (pif);                            /* set standard IRQ signal */
                setstdSRQ (pif);                            /* set standard SRQ signal */
                }

            else {                                          /* DOS PIF */
                setPRL (dibptr->select_code, !(pif.control | pif.flag));
                setIRQ (dibptr->select_code, !pif.control & pif.flag & pif.flagbuf);
                }

            if (DEBUG_PRS (pif_dev))
                fprintf (sim_deb, ">>PIF: [SIR] PRL = %d, IRQ = %d\n",
                                  PRL (dibptr->select_code),
                                  IRQ (dibptr->select_code));
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            pif.flagbuf = CLEAR;
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Simulator reset routine */

t_stat pif_reset (DEVICE *dptr)
{
IOPRESET (&pif_dib);                                    /* PRESET device (does not use PON) */
return SCPE_OK;
}


/* Set card type.

   val == 0 --> set to 12936A (DOS PIF)
   val == 1 --> set to 12620A (RTE PIF)
*/

t_stat pif_set_card (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
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

t_stat pif_show_card (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (pif_dev.flags & DEV_12936)
    fputs ("12936A", st);
else
    fputs ("12620A", st);

return SCPE_OK;
}
