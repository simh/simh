/* pdp8_tt.c: PDP-8 console terminal simulator

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

   tti,tto      KL8E terminal input/output

   18-Apr-12    RMS     Revised to use clock coscheduling
   18-Jun-07    RMS     Added UNIT_IDLE flag to console input
   18-Oct-06    RMS     Synced keyboard to clock
   30-Sep-06    RMS     Fixed handling of non-printable characters in KSR mode
   22-Nov-05    RMS     Revised for new terminal processing routines
   28-May-04    RMS     Removed SET TTI CTRL-C
   29-Dec-03    RMS     Added console output backpressure support
   25-Apr-03    RMS     Revised for extended file support
   02-Mar-02    RMS     Added SET TTI CTRL-C
   22-Dec-02    RMS     Added break support
   01-Nov-02    RMS     Added 7B/8B support
   04-Oct-02    RMS     Added DIBs, device number support
   30-May-02    RMS     Widened POS to 32b
   07-Sep-01    RMS     Moved function prototypes
*/

#include "pdp8_defs.h"
#include "sim_tmxr.h"
#include <ctype.h>

extern int32 int_req, int_enable, dev_done, stop_inst;
extern int32 tmxr_poll;

int32 tti (int32 IR, int32 AC);
int32 tto (int32 IR, int32 AC);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_reset (DEVICE *dptr);
t_stat tty_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit descriptor
   tti_reg      TTI register list
   tti_mod      TTI modifiers list
*/

DIB tti_dib = { DEV_TTI, 1, { &tti } };

UNIT tti_unit = { UDATA (&tti_svc, UNIT_IDLE|TT_MODE_KSR, 0), SERIAL_IN_WAIT };

REG tti_reg[] = {
    { ORDATAD (BUF, tti_unit.buf, 8, "last data item processed") },
    { FLDATAD (DONE, dev_done, INT_V_TTI, "device done flag") },
    { FLDATAD (ENABLE, int_enable, INT_V_TTI, "interrupt enable flag") },
    { FLDATAD (INT, int_req, INT_V_TTI, "interrupt pending flag") },
    { DRDATAD (POS, tti_unit.pos, T_ADDR_W, "number of characters input"), PV_LEFT },
    { DRDATAD (TIME, tti_unit.wait, 24, "input polling interval (if 0, the keyboard is polled synchronously with the clock)"), PV_LEFT+REG_NZ },
    { NULL }
    };

MTAB tti_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", &tty_set_mode },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  &tty_set_mode },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  &tty_set_mode },
    { TT_MODE, TT_MODE_7P,  "7b",  NULL,  NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", NULL, NULL, &show_dev, NULL },
    { 0 }
    };

DEVICE tti_dev = {
    "TTI", &tti_unit, tti_reg, tti_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tti_reset,
    NULL, NULL, NULL,
    &tti_dib, 0
    };

uint32 tti_buftime;                                     /* time input character arrived */

/* TTO data structures

   tto_dev      TTO device descriptor
   tto_unit     TTO unit descriptor
   tto_reg      TTO register list
*/

DIB tto_dib = { DEV_TTO, 1, { &tto } };

UNIT tto_unit = { UDATA (&tto_svc, TT_MODE_KSR, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
    { ORDATAD (BUF, tto_unit.buf, 8, "last date item processed") },
    { FLDATAD (DONE, dev_done, INT_V_TTO, "device done flag") },
    { FLDATAD (ENABLE, int_enable, INT_V_TTO, "interrupt enable flag") },
    { FLDATAD (INT, int_req, INT_V_TTO, "interrupt pending flag") },
    { DRDATAD (POS, tto_unit.pos, T_ADDR_W, "number of characters output"), PV_LEFT },
    { DRDATAD (TIME, tto_unit.wait, 24, "time form I/O initiation to interrupt"), PV_LEFT },
    { NULL }
    };

MTAB tto_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", &tty_set_mode },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  &tty_set_mode },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  &tty_set_mode },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  &tty_set_mode },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", NULL, NULL, &show_dev },
    { 0 }
    };

DEVICE tto_dev = {
    "TTO", &tto_unit, tto_reg, tto_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tto_reset, 
    NULL, NULL, NULL,
    &tto_dib, 0
    };

/* Terminal input: IOT routine */

int32 tti (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */
    case 0:                                             /* KCF */
        dev_done = dev_done & ~INT_TTI;                 /* clear flag */
        int_req = int_req & ~INT_TTI;
        return AC;

    case 1:                                             /* KSF */
        return (dev_done & INT_TTI)? IOT_SKP + AC: AC;

    case 2:                                             /* KCC */
        dev_done = dev_done & ~INT_TTI;                 /* clear flag */
        int_req = int_req & ~INT_TTI;
        return 0;                                       /* clear AC */

    case 4:                                             /* KRS */
        return (AC | tti_unit.buf);                     /* return buffer */

    case 5:                                             /* KIE */
        if (AC & 1)
            int_enable = int_enable | (INT_TTI+INT_TTO);
        else int_enable = int_enable & ~(INT_TTI+INT_TTO);
        int_req = INT_UPDATE;                           /* update interrupts */
        return AC;

    case 6:                                             /* KRB */
        dev_done = dev_done & ~INT_TTI;                 /* clear flag */
        int_req = int_req & ~INT_TTI;
        sim_activate_abs (&tti_unit, tti_unit.wait);    /* check soon for more input */
        return (tti_unit.buf);                          /* return buffer */

    default:
        return (stop_inst << IOT_V_REASON) + AC;
        }                                               /* end switch */
}

/* Unit service */

t_stat tti_svc (UNIT *uptr)
{
int32 c;

sim_clock_coschedule (uptr, tmxr_poll);                 /* continue poll */
if ((dev_done & INT_TTI) &&                             /* prior character still pending and < 500ms? */
    ((sim_os_msec () - tti_buftime) < 500))
    return SCPE_OK;
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
if (c & SCPE_BREAK)                                     /* break? */
    uptr->buf = 0;
else uptr->buf = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags) | TTUF_KSR);
tti_buftime = sim_os_msec ();
uptr->pos = uptr->pos + 1;
dev_done = dev_done | INT_TTI;                          /* set done */
int_req = INT_UPDATE;                                   /* update interrupts */
return SCPE_OK;
}

/* Reset routine */

t_stat tti_reset (DEVICE *dptr)
{
tmxr_set_console_units (&tti_unit, &tto_unit);
tti_unit.buf = 0;
dev_done = dev_done & ~INT_TTI;                         /* clear done, int */
int_req = int_req & ~INT_TTI;
int_enable = int_enable | INT_TTI;                      /* set enable */
if (!sim_is_running)                                    /* RESET (not CAF)? */
    sim_activate (&tti_unit, KBD_WAIT (tti_unit.wait, tmxr_poll));
return SCPE_OK;
}

/* Terminal output: IOT routine */

int32 tto (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 0:                                             /* TLF */
        dev_done = dev_done | INT_TTO;                  /* set flag */
        int_req = INT_UPDATE;                           /* update interrupts */
        return AC;

    case 1:                                             /* TSF */
        return (dev_done & INT_TTO)? IOT_SKP + AC: AC;

    case 2:                                             /* TCF */
        dev_done = dev_done & ~INT_TTO;                 /* clear flag */
        int_req = int_req & ~INT_TTO;                   /* clear int req */
        return AC;

    case 5:                                             /* SPI */
        return (int_req & (INT_TTI+INT_TTO))? IOT_SKP + AC: AC;

    case 6:                                             /* TLS */
        dev_done = dev_done & ~INT_TTO;                 /* clear flag */
        int_req = int_req & ~INT_TTO;                   /* clear int req */
    case 4:                                             /* TPC */
        sim_activate (&tto_unit, tto_unit.wait);        /* activate unit */
        tto_unit.buf = AC;                              /* load buffer */
        return AC;

    default:
        return (stop_inst << IOT_V_REASON) + AC;
        }                                               /* end switch */
}

/* Unit service */

t_stat tto_svc (UNIT *uptr)
{
int32 c;
t_stat r;

c = sim_tt_outcvt (uptr->buf, TT_GET_MODE (uptr->flags) | TTUF_KSR);
if (c >= 0) {
    if ((r = sim_putchar_s (c)) != SCPE_OK) {           /* output char; error? */
        sim_activate (uptr, uptr->wait);                /* try again */
        return ((r == SCPE_STALL)? SCPE_OK: r);         /* if !stall, report */
        }
    }
dev_done = dev_done | INT_TTO;                          /* set done */
int_req = INT_UPDATE;                                   /* update interrupts */
uptr->pos = uptr->pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat tto_reset (DEVICE *dptr)
{
tto_unit.buf = 0;
dev_done = dev_done & ~INT_TTO;                         /* clear done, int */
int_req = int_req & ~INT_TTO;
int_enable = int_enable | INT_TTO;                      /* set enable */
sim_cancel (&tto_unit);                                 /* deactivate unit */
return SCPE_OK;
}

t_stat tty_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
tti_unit.flags = (tti_unit.flags & ~TT_MODE) | val;
tto_unit.flags = (tto_unit.flags & ~TT_MODE) | val;
return SCPE_OK;
}
