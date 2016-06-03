/* hp2100_lpt.c: HP 2100 12845B line printer simulator

   Copyright (c) 1993-2016, Robert M. Supnik

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

   LPT          12845B 2607 line printer

   13-May-16    JDB     Modified for revised SCP API function parameter types
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
                        Changed CTIME register width to match documentation
   22-Jan-07    RMS     Added UNIT_TEXT flag
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   19-Nov-04    JDB     Added restart when set online, etc.
   29-Sep-04    JDB     Added SET OFFLINE/ONLINE, POWEROFF/POWERON
                        Fixed status returns for error conditions
                        Fixed TOF handling so form remains on line 0
   03-Jun-04    RMS     Fixed timing (found by Dave Bryan)
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Implemented DMA SRQ (follows FLG)
   25-Apr-03    RMS     Revised for extended file support
   24-Oct-02    RMS     Cloned from 12653A

   Reference:
   - 12845A Line Printer Operating and Service Manual (12845-90001, Aug-1972)


   The 2607 provides three status bits via the interface:

     bit 15 -- printer ready (online)
     bit 14 -- paper out
     bit  0 -- printer idle

   The expected status returns are:

     140001 -- power off or cable disconnected
     100001 -- power on, paper loaded, printer ready
     100000 -- power on, paper loaded, printer busy
     040000 -- power on, paper out (at bottom-of-form)
     000000 -- power on, paper out (not at BOF) / print button up / platen open

   Manual Note: "2-33. PAPER OUT SIGNAL.  [...]  The signal is asserted only
   when the format tape in the line printer has reached the bottom of form."

   These simulator commands provide the listed printer states:

     SET LPT POWEROFF --> power off or cable disconnected
     SET LPT POWERON  --> power on
     SET LPT OFFLINE  --> print button up
     SET LPT ONLINE   --> print button down
     ATT LPT <file>   --> paper loaded
     DET LPT          --> paper out
*/

#include "hp2100_defs.h"

#define LPT_PAGELNT     60                              /* page length */

#define LPT_NBSY        0000001                         /* not busy */
#define LPT_PAPO        0040000                         /* paper out */
#define LPT_RDY         0100000                         /* ready */
#define LPT_PWROFF      LPT_RDY | LPT_PAPO | LPT_NBSY   /* power-off status */

#define LPT_CTL         0100000                         /* control output */
#define LPT_CHAN        0000100                         /* skip to chan */
#define LPT_SKIPM       0000077                         /* line count mask */
#define LPT_CHANM       0000007                         /* channel mask */

#define UNIT_V_POWEROFF (UNIT_V_UF + 0)                 /* unit powered off */
#define UNIT_V_OFFLINE  (UNIT_V_UF + 1)                 /* unit offline */
#define UNIT_POWEROFF   (1 << UNIT_V_POWEROFF)
#define UNIT_OFFLINE    (1 << UNIT_V_OFFLINE)

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } lpt = { CLEAR, CLEAR, CLEAR };

int32 lpt_ctime = 4;                                    /* char time */
int32 lpt_ptime = 10000;                                /* print time */
int32 lpt_stopioe = 0;                                  /* stop on error */
int32 lpt_lcnt = 0;                                     /* line count */
static int32 lpt_cct[8] = {
    1, 1, 1, 2, 3, LPT_PAGELNT/2, LPT_PAGELNT/4, LPT_PAGELNT/6
    };

DEVICE lpt_dev;

IOHANDLER lptio;

t_stat lpt_svc (UNIT *uptr);
t_stat lpt_reset (DEVICE *dptr);
t_stat lpt_restart (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
t_stat lpt_attach (UNIT *uptr, CONST char *cptr);

/* LPT data structures

   lpt_dev      LPT device descriptor
   lpt_unit     LPT unit descriptor
   lpt_reg      LPT register list
*/

DIB lpt_dib = { &lptio, LPT };

UNIT lpt_unit = {
    UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_DISABLE+UNIT_TEXT, 0)
    };

REG lpt_reg[] = {
    { ORDATA (BUF, lpt_unit.buf, 7) },
    { FLDATA (CTL, lpt.control, 0) },
    { FLDATA (FLG, lpt.flag,    0) },
    { FLDATA (FBF, lpt.flagbuf, 0) },
    { DRDATA (LCNT, lpt_lcnt, 7) },
    { DRDATA (POS, lpt_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (CTIME, lpt_ctime, 24), PV_LEFT },
    { DRDATA (PTIME, lpt_ptime, 24), PV_LEFT },
    { FLDATA (STOP_IOE, lpt_stopioe, 0) },
    { ORDATA (SC, lpt_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, lpt_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB lpt_mod[] = {
    { UNIT_POWEROFF, UNIT_POWEROFF, "power off", "POWEROFF", NULL },
    { UNIT_POWEROFF, 0, "power on", "POWERON", lpt_restart },
    { UNIT_OFFLINE, UNIT_OFFLINE, "offline", "OFFLINE", NULL },
    { UNIT_OFFLINE, 0, "online", "ONLINE", lpt_restart },
    { MTAB_XTD | MTAB_VDV,            0, "SC",    "SC",    &hp_setsc,  &hp_showsc,  &lpt_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "DEVNO", "DEVNO", &hp_setdev, &hp_showdev, &lpt_dev },
    { 0 }
    };

DEVICE lpt_dev = {
    "LPT", &lpt_unit, lpt_reg, lpt_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &lpt_reset,
    NULL, &lpt_attach, NULL,
    &lpt_dib, DEV_DISABLE
    };


/* I/O signal handler */

uint32 lptio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
uint16 data;
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            lpt.flag = lpt.flagbuf = CLEAR;
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            lpt.flag = lpt.flagbuf = SET;
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (lpt);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (lpt);
            break;


        case ioIOI:                                         /* I/O data input */
            data = 0;

            if (lpt_unit.flags & UNIT_POWEROFF)             /* power off? */
                data = LPT_PWROFF;

            else if (!(lpt_unit.flags & UNIT_OFFLINE)) {    /* online? */
                if (lpt_unit.flags & UNIT_ATT) {            /* paper loaded? */
                    data = LPT_RDY;
                    if (!sim_is_active (&lpt_unit))         /* printer busy? */
                        data = data | LPT_NBSY;
                    }

                else if (lpt_lcnt == LPT_PAGELNT - 1)       /* paper out, at BOF? */
                    data = LPT_PAPO;
                }

            stat_data = IORETURN (SCPE_OK, data);           /* merge in return status */
            break;


        case ioIOO:                                     /* I/O data output */
            lpt_unit.buf = IODATA (stat_data) & (LPT_CTL | 0177);
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            lpt.flag = lpt.flagbuf = SET;               /* set flag and flag buffer */
            lpt_unit.buf = 0;                           /* clear output buffer */
            break;

        case ioCRS:                                     /* control reset */
        case ioCLC:                                     /* clear control flip-flop */
            lpt.control = CLEAR;
            break;


        case ioSTC:                                     /* set control flip-flop */
            lpt.control = SET;
            sim_activate (&lpt_unit,                    /* schedule op */
                (lpt_unit.buf & LPT_CTL)? lpt_ptime: lpt_ctime);
            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (lpt);                            /* set standard PRL signal */
            setstdIRQ (lpt);                            /* set standard IRQ signal */
            setstdSRQ (lpt);                            /* set standard SRQ signal */
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            lpt.flagbuf = CLEAR;
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Unit service */

t_stat lpt_svc (UNIT *uptr)
{
int32 i, skip, chan;

if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return IOERROR (lpt_stopioe, SCPE_UNATT);
else if (uptr->flags & UNIT_OFFLINE)                    /* offline? */
    return IOERROR (lpt_stopioe, STOP_OFFLINE);
else if (uptr->flags & UNIT_POWEROFF)                   /* powered off? */
    return IOERROR (lpt_stopioe, STOP_PWROFF);

lptio (&lpt_dib, ioENF, 0);                             /* set flag */

if (uptr->buf & LPT_CTL) {                              /* control word? */
    if (uptr->buf & LPT_CHAN) {
        chan = uptr->buf & LPT_CHANM;
        if (chan == 0) {                                /* top of form? */
            fputc ('\f', uptr->fileref);                /* ffeed */
            lpt_lcnt = 0;                               /* reset line cnt */
            skip = 0;
            }
        else if (chan == 1) skip = LPT_PAGELNT - lpt_lcnt - 1;
        else skip = lpt_cct[chan] - (lpt_lcnt % lpt_cct[chan]);
        }
    else {
        skip = uptr->buf & LPT_SKIPM;
        if (skip == 0) fputc ('\r', uptr->fileref);
        }
    for (i = 0; i < skip; i++) fputc ('\n', uptr->fileref);
    lpt_lcnt = (lpt_lcnt + skip) % LPT_PAGELNT;
    }
else fputc (uptr->buf & 0177, uptr->fileref);           /* no, just add char */
if (ferror (uptr->fileref)) {
    perror ("LPT I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
lpt_unit.pos = ftell (uptr->fileref);                   /* update pos */
return SCPE_OK;
}


/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
IOPRESET (&lpt_dib);                                    /* PRESET device (does not use PON) */

sim_cancel (&lpt_unit);                                 /* deactivate unit */
return SCPE_OK;
}


/* Restart I/O routine

   If I/O is started via STC, and the printer is powered off, offline,
   or out of paper, the CTL and CMD flip-flops will set, a service event
   will be scheduled, and the service routine will be entered.  If
   STOP_IOE is not set, the I/O operation will "hang" at that point
   until the printer is powered on, set online, or paper is supplied
   (attached).

   If a pending operation is "hung" when this routine is called, it is
   restarted, which clears CTL and sets FBF and FLG, completing the
   original I/O request.
 */

t_stat lpt_restart (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
if (lpt.control && !sim_is_active (uptr))
    sim_activate (uptr, 0);                             /* reschedule I/O */
return SCPE_OK;
}


/* Attach routine */

t_stat lpt_attach (UNIT *uptr, CONST char *cptr)
{
lpt_lcnt = 0;                                           /* top of form */
lpt_restart (uptr, 0, NULL, NULL);                      /* restart I/O if hung */
return attach_unit (uptr, cptr);
}
