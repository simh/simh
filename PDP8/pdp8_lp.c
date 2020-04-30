/* pdp8_lp.c: PDP-8 line printer simulator

   Copyright (c) 1993-2016, Robert M Supnik

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

   lpt          LP8E line printer

   16-Dec-16    DJG     Added IOT 6660 to allow WPS WS78 3.4 to print
   19-Jan-07    RMS     Added UNIT_TEXT
   25-Apr-03    RMS     Revised for extended file support
   04-Oct-02    RMS     Added DIB, enable/disable, device number support
   30-May-02    RMS     Widened POS to 32b
*/

#include "pdp8_defs.h"

extern int32 int_req, int_enable, dev_done, stop_inst;

int32 lpt_err = 0;                                      /* error flag */
int32 lpt_stopioe = 0;                                  /* stop on error */

int32 lpt (int32 IR, int32 AC);
t_stat lpt_svc (UNIT *uptr);
t_stat lpt_reset (DEVICE *dptr);
t_stat lpt_attach (UNIT *uptr, CONST char *cptr);
t_stat lpt_detach (UNIT *uptr);
const char *lpt_description (DEVICE *dptr);

/* LPT data structures

   lpt_dev      LPT device descriptor
   lpt_unit     LPT unit descriptor
   lpt_reg      LPT register list
*/

DIB lpt_dib = { DEV_LPT, 1, { &lpt } };

UNIT lpt_unit = {
    UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0), SERIAL_OUT_WAIT
    };

REG lpt_reg[] = {
    { ORDATAD (BUF, lpt_unit.buf, 8,"last data item processed") },
    { FLDATAD (ERR, lpt_err, 0, "error status flag") },
    { FLDATAD (DONE, dev_done, INT_V_LPT, "device done flag") },
    { FLDATAD (ENABLE, int_enable, INT_V_LPT, "interrupt enable flag") },
    { FLDATAD (INT, int_req, INT_V_LPT, "interrupt pending flag") },
    { DRDATAD (POS, lpt_unit.pos, T_ADDR_W, "position in the output file"), PV_LEFT },
    { DRDATAD (TIME, lpt_unit.wait, 24, "time from I/O initiation to interrupt"), PV_LEFT },
    { FLDATAD (STOP_IOE, lpt_stopioe, 0, "stop on I/O error") },
    { ORDATA (DEVNUM, lpt_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB lpt_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { 0 }
    };

DEVICE lpt_dev = {
    "LPT", &lpt_unit, lpt_reg, lpt_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &lpt_reset,
    NULL, &lpt_attach, &lpt_detach,
    &lpt_dib, DEV_DISABLE, 0,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &lpt_description
    };

/* IOT routine */

int32 lpt (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 0:                                             /* PKSTF */
        dev_done = dev_done | INT_LPT;                  /* set flag */
        int_req = INT_UPDATE;                           /* update interrupts */
        return AC;

    case 1:                                             /* PSKF */
        return (dev_done & INT_LPT)? IOT_SKP + AC: AC;

    case 2:                                             /* PCLF */
        dev_done = dev_done & ~INT_LPT;                 /* clear flag */
        int_req = int_req & ~INT_LPT;                   /* clear int req */
        return AC;

    case 3:                                             /* PSKE */
        return (lpt_err)? IOT_SKP + AC: AC;

    case 6:                                             /* PCLF!PSTB */
        dev_done = dev_done & ~INT_LPT;                 /* clear flag */
        int_req = int_req & ~INT_LPT;                   /* clear int req */

    case 4:                                             /* PSTB */
        lpt_unit.buf = AC & 0177;                       /* load buffer */
        if ((lpt_unit.buf == 015) || (lpt_unit.buf == 014) ||
            (lpt_unit.buf == 012)) {
            sim_activate (&lpt_unit, lpt_unit.wait);
            return AC;
            }
        return (lpt_svc (&lpt_unit) << IOT_V_REASON) + AC;

    case 5:                                             /* PSIE */
        int_enable = int_enable | INT_LPT;              /* set enable */
        int_req = INT_UPDATE;                           /* update interrupts */
        return AC;

    case 7:                                             /* PCIE */
        int_enable = int_enable & ~INT_LPT;             /* clear enable */
        int_req = int_req & ~INT_LPT;                   /* clear int req */
        return AC;

    default:
        return (stop_inst << IOT_V_REASON) + AC;
        }                                               /* end switch */
}

/* Unit service */

t_stat lpt_svc (UNIT *uptr)
{
dev_done = dev_done | INT_LPT;                          /* set done */
int_req = INT_UPDATE;                                   /* update interrupts */
if ((uptr->flags & UNIT_ATT) == 0) {
    lpt_err = 1;
    return IORETURN (lpt_stopioe, SCPE_UNATT);
    }
fputc (uptr->buf, uptr->fileref);                       /* print char */
uptr->pos = ftell (uptr->fileref);
if (ferror (uptr->fileref)) {                           /* error? */
    sim_perror ("LPT I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
lpt_unit.buf = 0;
dev_done = dev_done & ~INT_LPT;                         /* clear done, int */
int_req = int_req & ~INT_LPT;
int_enable = int_enable | INT_LPT;                      /* set enable */
lpt_err = (lpt_unit.flags & UNIT_ATT) == 0;
sim_cancel (&lpt_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Attach routine */

t_stat lpt_attach (UNIT *uptr, CONST char *cptr)
{
t_stat reason;

sim_switches |= SWMASK ('A');   /* Default to Append to existing file */
reason = attach_unit (uptr, cptr);
lpt_err = (lpt_unit.flags & UNIT_ATT) == 0;
return reason;
}

/* Detach routine */

t_stat lpt_detach (UNIT *uptr)
{
lpt_err = 1;
return detach_unit (uptr);
}

const char *lpt_description (DEVICE *dptr)
{
return "LP8E line printer";
}
