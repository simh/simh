/* nova_tt1.c: NOVA second terminal simulator

   Copyright (c) 1993-2008, Robert M. Supnik
   Written by Bruce Ray and used with his gracious permission.

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

   tti1         second terminal input
   tto1         second terminal output

   19-Nov-08    RMS     Revised for common TMXR show routines
   09-May-03    RMS     Added network device flag
   05-Jan-03    RMS     Fixed calling sequence for setmod
   03-Oct-02    RMS     Added DIBs
   22-Aug-02    RMS     Updated for changes in sim_tmxr
   30-May-02    RMS     Widened POS to 32b
   06-Jan-02    RMS     Revised enable/disable support
   30-Dec-01    RMS     Added show statistics, set disconnect
   30-Nov-01    RMS     Added extended SET/SHOW support
   17-Sep-01    RMS     Changed to use terminal multiplexor library
   07-Sep-01    RMS     Moved function prototypes
   31-May-01    RMS     Added multiconsole support
   26-Apr-01    RMS     Added device enable/disable support
*/

#include "nova_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

#define UNIT_V_DASHER   (UNIT_V_UF + 0)                 /* Dasher mode */
#define UNIT_DASHER     (1 << UNIT_V_DASHER)

extern int32 int_req, dev_busy, dev_done, dev_disable;
extern int32 tmxr_poll;                                 /* calibrated poll */
TMLN tt1_ldsc = { 0 };                                  /* line descriptors */
TMXR tt_desc = { 1, 0, 0, &tt1_ldsc };                  /* mux descriptor */

int32 tti1 (int32 pulse, int32 code, int32 AC);
int32 tto1 (int32 pulse, int32 code, int32 AC);
t_stat tti1_svc (UNIT *uptr);
t_stat tto1_svc (UNIT *uptr);
t_stat tti1_reset (DEVICE *dptr);
t_stat tto1_reset (DEVICE *dptr);
t_stat ttx1_setmod (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tti1_attach (UNIT *uptr, CONST char *cptr);
t_stat tti1_detach (UNIT *uptr);
void ttx1_enbdis (int32 dis);

/* TTI1 data structures

   tti1_dev     TTI1 device descriptor
   tti1_unit    TTI1 unit descriptor
   tti1_reg     TTI1 register list
   ttx1_mod     TTI1/TTO1 modifiers list
*/

DIB tti1_dib = { DEV_TTI1, INT_TTI1, PI_TTI1, &tti1 };

UNIT tti1_unit = { UDATA (&tti1_svc, UNIT_ATTABLE, 0), KBD_POLL_WAIT };

REG tti1_reg[] = {
    { ORDATA (BUF, tti1_unit.buf, 8) },
    { FLDATA (BUSY, dev_busy, INT_V_TTI1) },
    { FLDATA (DONE, dev_done, INT_V_TTI1) },
    { FLDATA (DISABLE, dev_disable, INT_V_TTI1) },
    { FLDATA (INT, int_req, INT_V_TTI1) },
    { DRDATA (POS, tt1_ldsc.rxcnt, 32), PV_LEFT },
    { DRDATA (TIME, tti1_unit.wait, 24), REG_NZ + PV_LEFT },
    { NULL }
    };

MTAB tti1_mod[] = {
    { UNIT_DASHER, 0, "ANSI", "ANSI", &ttx1_setmod },
    { UNIT_DASHER, UNIT_DASHER, "Dasher", "DASHER", &ttx1_setmod },
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &tt_desc },
    { MTAB_XTD | MTAB_VDV, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, (void *) &tt_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *) &tt_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *) &tt_desc },
    { 0 }
    };

DEVICE tti1_dev = {
    "TTI1", &tti1_unit, tti1_reg, tti1_mod,
    1, 10, 31, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &tti1_reset,
    NULL, &tti1_attach, &tti1_detach,
    &tti1_dib, DEV_MUX | DEV_DISABLE
    };

/* TTO1 data structures

   tto1_dev     TTO1 device descriptor
   tto1_unit    TTO1 unit descriptor
   tto1_reg     TTO1 register list
*/

DIB tto1_dib = { DEV_TTO1, INT_TTO1, PI_TTO1, &tto1 };

UNIT tto1_unit = { UDATA (&tto1_svc, 0, 0), SERIAL_OUT_WAIT };

REG tto1_reg[] = {
    { ORDATA (BUF, tto1_unit.buf, 8) },
    { FLDATA (BUSY, dev_busy, INT_V_TTO1) },
    { FLDATA (DONE, dev_done, INT_V_TTO1) },
    { FLDATA (DISABLE, dev_disable, INT_V_TTO1) },
    { FLDATA (INT, int_req, INT_V_TTO1) },
    { DRDATA (POS, tt1_ldsc.txcnt, 32), PV_LEFT },
    { DRDATA (TIME, tto1_unit.wait, 24), PV_LEFT },
    { NULL }
    };

MTAB tto1_mod[] = {
    { UNIT_DASHER, 0, "ANSI", "ANSI", &ttx1_setmod },
    { UNIT_DASHER, UNIT_DASHER, "Dasher", "DASHER", &ttx1_setmod },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, &tt_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, &tt_desc },
    { 0 }
    };

DEVICE tto1_dev = {
    "TTO1", &tto1_unit, tto1_reg, tto1_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tto1_reset,
    NULL, NULL, NULL,
    &tto1_dib, DEV_DISABLE | DEV_MUX
    };

/* Terminal input: IOT routine */

int32 tti1 (int32 pulse, int32 code, int32 AC)
{
int32 iodata;

iodata = (code == ioDIA)? tti1_unit.buf & 0377: 0;
switch (pulse) {                                        /* decode IR<8:9> */

    case iopS:                                          /* start */
        dev_busy = dev_busy | INT_TTI1;                 /* set busy */
        dev_done = dev_done & ~INT_TTI1;                /* clear done, int */
        int_req = int_req & ~INT_TTI1;
        break;

    case iopC:                                          /* clear */
        dev_busy = dev_busy & ~INT_TTI1;                /* clear busy */
        dev_done = dev_done & ~INT_TTI1;                /* clear done, int */
        int_req = int_req & ~INT_TTI1;
        break;
        }                                               /* end switch */

return iodata;
}

/* Unit service */

t_stat tti1_svc (UNIT *uptr)
{
int32 temp, newln;

if (tt1_ldsc.conn) {                                    /* connected? */
    tmxr_poll_rx (&tt_desc);                            /* poll for input */
    if ((temp = tmxr_getc_ln (&tt1_ldsc))) {            /* get char */ 
        uptr->buf = temp & 0177;
        if ((uptr->flags & UNIT_DASHER) &&
            (uptr->buf == '\r'))
            uptr->buf = '\n';                           /* Dasher: cr->nl */
        dev_busy = dev_busy & ~INT_TTI1;                /* clear busy */
        dev_done = dev_done | INT_TTI1;                 /* set done */
        int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
        }
    sim_activate (uptr, uptr->wait);                    /* continue poll */
    }
if (uptr->flags & UNIT_ATT) {                           /* attached? */
    newln = tmxr_poll_conn (&tt_desc);                  /* poll connect */
    if (newln >= 0) {                                   /* got one? */
        sim_activate (&tti1_unit, tti1_unit.wait);
        tt1_ldsc.rcve = 1;                              /* rcv enabled */
        }
    sim_activate (uptr, tmxr_poll);                     /* sched poll */
    }
return SCPE_OK;
}

/* Reset routine */

t_stat tti1_reset (DEVICE *dptr)
{
ttx1_enbdis (dptr->flags & DEV_DIS);                    /* sync devices */
tti1_unit.buf = 0;                                      /* <not DG compatible>  */
dev_busy = dev_busy & ~INT_TTI1;                        /* clear busy */
dev_done = dev_done & ~INT_TTI1;                        /* clear done, int */
int_req = int_req & ~INT_TTI1;
if (tt1_ldsc.conn) {                                    /* if conn, */
    sim_activate (&tti1_unit, tti1_unit.wait);          /* activate, */
    tt1_ldsc.rcve = 1;                                  /* enable */
    }
else if (tti1_unit.flags & UNIT_ATT)                    /* if attached, */
    sim_activate (&tti1_unit, tmxr_poll);               /* activate */
else sim_cancel (&tti1_unit);                           /* else stop */
return SCPE_OK;
}

/* Terminal output: IOT routine */

int32 tto1 (int32 pulse, int32 code, int32 AC)
{
if (code == ioDOA)
    tto1_unit.buf = AC & 0377;
switch (pulse) {                                        /* decode IR<8:9> */

    case iopS:                                          /* start */
        dev_busy = dev_busy | INT_TTO1;                 /* set busy */
        dev_done = dev_done & ~INT_TTO1;                /* clear done, int */
        int_req = int_req & ~INT_TTO1;
        sim_activate (&tto1_unit, tto1_unit.wait);      /* activate unit */
        break;

    case iopC:                                          /* clear */
        dev_busy = dev_busy & ~INT_TTO1;                /* clear busy */
        dev_done = dev_done & ~INT_TTO1;                /* clear done, int */
        int_req = int_req & ~INT_TTO1;
        sim_cancel (&tto1_unit);                        /* deactivate unit */
        break;
        }                                               /* end switch */

return 0;
}

/* Unit service */

t_stat tto1_svc (UNIT *uptr)
{
int32 c;

dev_busy = dev_busy & ~INT_TTO1;                        /* clear busy */
dev_done = dev_done | INT_TTO1;                         /* set done */
int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
c = tto1_unit.buf & 0177;
if ((tto1_unit.flags & UNIT_DASHER) && (c == 031))
    c = '\b';
if (tt1_ldsc.conn) {                                    /* connected? */
    if (tt1_ldsc.xmte) {                                /* tx enabled? */
        tmxr_putc_ln (&tt1_ldsc, c);                    /* output char */
        tmxr_poll_tx (&tt_desc);                        /* poll xmt */
        }
    else {
        tmxr_poll_tx (&tt_desc);                        /* poll xmt */
        sim_activate (&tto1_unit, tmxr_poll);           /* wait */
        }
    }
return SCPE_OK;
}

/* Reset routine */

t_stat tto1_reset (DEVICE *dptr)
{
ttx1_enbdis (dptr->flags & DEV_DIS);                    /* sync devices */
tto1_unit.buf = 0;                                      /* <not DG compatible>  */
dev_busy = dev_busy & ~INT_TTO1;                        /* clear busy */
dev_done = dev_done & ~INT_TTO1;                        /* clear done, int */
int_req = int_req & ~INT_TTO1;
sim_cancel (&tto1_unit);                                /* deactivate unit */
return SCPE_OK;
}

t_stat ttx1_setmod (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
tti1_unit.flags = (tti1_unit.flags & ~UNIT_DASHER) | val;
tto1_unit.flags = (tto1_unit.flags & ~UNIT_DASHER) | val;
return SCPE_OK;
}

/* Attach routine */

t_stat tti1_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

r = tmxr_attach (&tt_desc, uptr, cptr);                 /* attach */
if (r != SCPE_OK)                                       /* error */
    return r;
sim_activate (uptr, tmxr_poll);                         /* start poll */
return SCPE_OK;
}

/* Detach routine */

t_stat tti1_detach (UNIT *uptr)
{
t_stat r;

r = tmxr_detach (&tt_desc, uptr);                       /* detach */
tt1_ldsc.rcve = 0;                                      /* disable rcv */
sim_cancel (uptr);                                      /* stop poll */
return r;
}

/* Enable/disable device */

void ttx1_enbdis (int32 dis)
{
if (dis) {
    tti1_dev.flags = tti1_dev.flags | DEV_DIS;
    tto1_dev.flags = tto1_dev.flags | DEV_DIS;
    }
else {
    tti1_dev.flags = tti1_dev.flags & ~DEV_DIS;
    tto1_dev.flags = tto1_dev.flags & ~DEV_DIS;
    }
return;
}
