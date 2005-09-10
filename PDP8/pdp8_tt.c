/* pdp8_tt.c: PDP-8 console terminal simulator

   Copyright (c) 1993-2005, Robert M Supnik

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
#include <ctype.h>

#define UNIT_V_8B       (UNIT_V_UF + 0)                 /* 8B */
#define UNIT_V_KSR      (UNIT_V_UF + 1)                 /* KSR33 */
#define UNIT_8B         (1 << UNIT_V_8B)
#define UNIT_KSR        (1 << UNIT_V_KSR)

extern int32 int_req, int_enable, dev_done, stop_inst;

int32 tti (int32 IR, int32 AC);
int32 tto (int32 IR, int32 AC);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_reset (DEVICE *dptr);
t_stat tty_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc);

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit descriptor
   tti_reg      TTI register list
   tti_mod      TTI modifiers list
*/

DIB tti_dib = { DEV_TTI, 1, { &tti } };

UNIT tti_unit = { UDATA (&tti_svc, UNIT_KSR, 0), KBD_POLL_WAIT };

REG tti_reg[] = {
    { ORDATA (BUF, tti_unit.buf, 8) },
    { FLDATA (DONE, dev_done, INT_V_TTI) },
    { FLDATA (ENABLE, int_enable, INT_V_TTI) },
    { FLDATA (INT, int_req, INT_V_TTI) },
    { DRDATA (POS, tti_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tti_unit.wait, 24), REG_NZ + PV_LEFT },
    { FLDATA (UC, tti_unit.flags, UNIT_V_KSR), REG_HRO },
    { NULL }
    };

MTAB tti_mod[] = {
    { UNIT_KSR+UNIT_8B, UNIT_KSR, "KSR", "KSR", &tty_set_mode },
    { UNIT_KSR+UNIT_8B, 0       , "7b" , "7B" , &tty_set_mode },
    { UNIT_KSR+UNIT_8B, UNIT_8B , "8b" , "8B" , &tty_set_mode },
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

/* TTO data structures

   tto_dev      TTO device descriptor
   tto_unit     TTO unit descriptor
   tto_reg      TTO register list
*/

DIB tto_dib = { DEV_TTO, 1, { &tto } };

UNIT tto_unit = { UDATA (&tto_svc, UNIT_KSR, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
    { ORDATA (BUF, tto_unit.buf, 8) },
    { FLDATA (DONE, dev_done, INT_V_TTO) },
    { FLDATA (ENABLE, int_enable, INT_V_TTO) },
    { FLDATA (INT, int_req, INT_V_TTO) },
    { DRDATA (POS, tto_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tto_unit.wait, 24), PV_LEFT },
    { NULL }
    };

MTAB tto_mod[] = {
    { UNIT_KSR+UNIT_8B, UNIT_KSR, "KSR", "KSR", &tty_set_mode },
    { UNIT_KSR+UNIT_8B, 0       , "7b" , "7B" , &tty_set_mode },
    { UNIT_KSR+UNIT_8B, UNIT_8B , "8b" , "8B" , &tty_set_mode },
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
        if (AC & 1) int_enable = int_enable | (INT_TTI+INT_TTO);
        else int_enable = int_enable & ~(INT_TTI+INT_TTO);
        int_req = INT_UPDATE;                           /* update interrupts */
        return AC;

    case 6:                                             /* KRB */
        dev_done = dev_done & ~INT_TTI;                 /* clear flag */
        int_req = int_req & ~INT_TTI;
        return (tti_unit.buf);                          /* return buffer */

    default:
        return (stop_inst << IOT_V_REASON) + AC;
        }                                               /* end switch */
}

/* Unit service */

t_stat tti_svc (UNIT *uptr)
{
int32 c;

sim_activate (&tti_unit, tti_unit.wait);                /* continue poll */
if ((c = sim_poll_kbd ()) < SCPE_KFLAG) return c;       /* no char or error? */
if (c & SCPE_BREAK) tti_unit.buf = 0;                   /* break? */
else if (tti_unit.flags & UNIT_KSR) {                   /* KSR? */
    c = c & 0177;
    if (islower (c)) c = toupper (c);
    tti_unit.buf = c | 0200;                            /* add TTY bit */
    }
else tti_unit.buf = c & ((tti_unit.flags & UNIT_8B)? 0377: 0177);
tti_unit.pos = tti_unit.pos + 1;
dev_done = dev_done | INT_TTI;                          /* set done */
int_req = INT_UPDATE;                                   /* update interrupts */
return SCPE_OK;
}

/* Reset routine */

t_stat tti_reset (DEVICE *dptr)
{
tti_unit.buf = 0;
dev_done = dev_done & ~INT_TTI;                         /* clear done, int */
int_req = int_req & ~INT_TTI;
int_enable = int_enable | INT_TTI;                      /* set enable */
sim_activate (&tti_unit, tti_unit.wait);                /* activate unit */
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

if (tto_unit.flags & UNIT_KSR) {                        /* UC only? */
    c = tto_unit.buf & 0177;
    if (islower (c)) c = toupper (c);
    }
else c = tto_unit.buf & ((tto_unit.flags & UNIT_8B)? 0377: 0177);
if ((r = sim_putchar_s (c)) != SCPE_OK) {               /* output char; error? */
    sim_activate (uptr, uptr->wait);                    /* try again */
    return ((r == SCPE_STALL)? SCPE_OK: r);             /* if !stall, report */
    }
dev_done = dev_done | INT_TTO;                          /* set done */
int_req = INT_UPDATE;                                   /* update interrupts */
tto_unit.pos = tto_unit.pos + 1;
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

t_stat tty_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc)
{
tti_unit.flags = (tti_unit.flags & ~(UNIT_KSR | UNIT_8B)) | val;
tto_unit.flags = (tto_unit.flags & ~(UNIT_KSR | UNIT_8B)) | val;
return SCPE_OK;
}
