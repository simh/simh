/* pdp8_ttx.c: PDP-8 additional terminals simulator

   Copyright (c) 1993-2013, Robert M Supnik

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

   ttix,ttox    PT08/KL8JA terminal input/output

   11-Oct-13    RMS     Poll TTIX immediately to pick up initial connect (Mark Pizzolato)
   18-Apr-12    RMS     Revised to use clock coscheduling
   19-Nov-08    RMS     Revised for common TMXR show routines
   07-Jun-06    RMS     Added UNIT_IDLE flag
   06-Jul-06    RMS     Fixed bug in DETACH routine
   22-Nov-05    RMS     Revised for new terminal processing routines
   29-Jun-05    RMS     Added SET TTOXn DISCONNECT
                        Fixed bug in SET LOG/NOLOG
   21-Jun-05    RMS     Fixed bug in SHOW CONN/STATS
   05-Jan-04    RMS     Revised for tmxr library changes
   09-May-03    RMS     Added network device flag
   25-Apr-03    RMS     Revised for extended file support
   22-Dec-02    RMS     Added break support
   02-Nov-02    RMS     Added 7B/8B support
   04-Oct-02    RMS     Added DIB, device number support
   22-Aug-02    RMS     Updated for changes to sim_tmxr.c
   06-Jan-02    RMS     Added device enable/disable support
   30-Dec-01    RMS     Complete rebuild
   30-Nov-01    RMS     Added extended SET/SHOW support

   This module implements four individual serial interfaces similar in function
   to the console.  These interfaces are mapped to Telnet based connections as
   though they were the four lines of a terminal multiplexor.  The connection
   polling mechanism is superimposed onto the keyboard of the first interface.
*/

#include "pdp8_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#define TTX_LINES       4
#define TTX_MASK        (TTX_LINES - 1)

#define TTX_GETLN(x)    (((x) >> 4) & TTX_MASK)

extern int32 int_req, int_enable, dev_done, stop_inst;
extern int32 tmxr_poll;

uint8 ttix_buf[TTX_LINES] = { 0 };                      /* input buffers */
uint8 ttox_buf[TTX_LINES] = { 0 };                      /* output buffers */
int32 ttx_tps = 100;                                    /* polls per second */
TMLN ttx_ldsc[TTX_LINES] = { {0} };                     /* line descriptors */
TMXR ttx_desc = { TTX_LINES, 0, 0, ttx_ldsc };          /* mux descriptor */

DEVICE ttix_dev, ttox_dev;
int32 ttix (int32 IR, int32 AC);
int32 ttox (int32 IR, int32 AC);
t_stat ttix_svc (UNIT *uptr);
t_stat ttix_reset (DEVICE *dptr);
t_stat ttox_svc (UNIT *uptr);
t_stat ttox_reset (DEVICE *dptr);
t_stat ttx_attach (UNIT *uptr, char *cptr);
t_stat ttx_detach (UNIT *uptr);
void ttx_enbdis (int32 dis);

/* TTIx data structures

   ttix_dev     TTIx device descriptor
   ttix_unit    TTIx unit descriptor
   ttix_reg     TTIx register list
   ttix_mod     TTIx modifiers list
*/

DIB ttix_dib = { DEV_KJ8, 8,
             { &ttix, &ttox, &ttix, &ttox, &ttix, &ttox, &ttix, &ttox } };

UNIT ttix_unit = { UDATA (&ttix_svc, UNIT_IDLE|UNIT_ATTABLE, 0), SERIAL_IN_WAIT };

REG ttix_reg[] = {
    { BRDATA (BUF, ttix_buf, 8, 8, TTX_LINES) },
    { GRDATA (DONE, dev_done, 8, TTX_LINES, INT_V_TTI1) },
    { GRDATA (ENABLE, int_enable, 8, TTX_LINES, INT_V_TTI1) },
    { GRDATA (INT, int_req, 8, TTX_LINES, INT_V_TTI1) },
    { DRDATA (TIME, ttix_unit.wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (TPS, ttx_tps, 10), REG_NZ + PV_LEFT },
    { ORDATA (DEVNUM, ttix_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB ttix_mod[] = {
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &ttx_desc },
    { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, (void *) &ttx_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *) &ttx_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *) &ttx_desc },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { 0 }
    };

/* debugging bitmaps */
#define DBG_XMT  TMXR_DBG_XMT                           /* display Transmitted Data */
#define DBG_RCV  TMXR_DBG_RCV                           /* display Received Data */
#define DBG_RET  TMXR_DBG_RET                           /* display Returned Received Data */
#define DBG_CON  TMXR_DBG_CON                           /* display connection activities */
#define DBG_TRC  TMXR_DBG_TRC                           /* display trace routine calls */

DEBTAB ttx_debug[] = {
  {"XMT",    DBG_XMT},
  {"RCV",    DBG_RCV},
  {"RET",    DBG_RET},
  {"CON",    DBG_CON},
  {"TRC",    DBG_TRC},
  {0}
};

DEVICE ttix_dev = {
    "TTIX", &ttix_unit, ttix_reg, ttix_mod,
    1, 10, 31, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &ttix_reset,
    NULL, &ttx_attach, &ttx_detach,
    &ttix_dib, DEV_MUX | DEV_DISABLE | DEV_DEBUG,
    0, ttx_debug
    };

/* TTOx data structures

   ttox_dev     TTOx device descriptor
   ttox_unit    TTOx unit descriptor
   ttox_reg     TTOx register list
*/

UNIT ttox_unit[] = {
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT }
    };

REG ttox_reg[] = {
    { BRDATA (BUF, ttox_buf, 8, 8, TTX_LINES) },
    { GRDATA (DONE, dev_done, 8, TTX_LINES, INT_V_TTO1) },
    { GRDATA (ENABLE, int_enable, 8, TTX_LINES, INT_V_TTO1) },
    { GRDATA (INT, int_req, 8, TTX_LINES, INT_V_TTO1) },
    { URDATA (TIME, ttox_unit[0].wait, 10, 24, 0,
              TTX_LINES, PV_LEFT) },
    { NULL }
    };

MTAB ttox_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", NULL },
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
    { MTAB_XTD|MTAB_VUN, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &ttx_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, &ttx_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, &ttx_desc },
    { 0 }
    };

DEVICE ttox_dev = {
    "TTOX", ttox_unit, ttox_reg, ttox_mod,
    4, 10, 31, 1, 8, 8,
    NULL, NULL, &ttox_reset, 
    NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DEBUG,
    0, ttx_debug
    };

/* Terminal input: IOT routine */

int32 ttix (int32 inst, int32 AC)
{
int32 pulse = inst & 07;                                /* IOT pulse */
int32 ln = TTX_GETLN (inst);                            /* line # */
int32 itti = (INT_TTI1 << ln);                          /* rx intr */
int32 itto = (INT_TTO1 << ln);                          /* tx intr */

switch (pulse) {                                        /* case IR<9:11> */

    case 0:                                             /* KCF */
        dev_done = dev_done & ~itti;                    /* clear flag */
        int_req = int_req & ~itti;
        break;

    case 1:                                             /* KSF */
        return (dev_done & itti)? IOT_SKP + AC: AC;

    case 2:                                             /* KCC */
        dev_done = dev_done & ~itti;                    /* clear flag */
        int_req = int_req & ~itti;
        sim_activate_abs (&ttix_unit, ttix_unit.wait);  /* check soon for more input */
        return 0;                                       /* clear AC */

    case 4:                                             /* KRS */
        return (AC | ttix_buf[ln]);                     /* return buf */

    case 5:                                             /* KIE */
        if (AC & 1)
            int_enable = int_enable | (itti + itto);
        else int_enable = int_enable & ~(itti + itto);
        int_req = INT_UPDATE;                           /* update intr */
        break;

    case 6:                                             /* KRB */
        dev_done = dev_done & ~itti;                    /* clear flag */
        int_req = int_req & ~itti;
        sim_activate_abs (&ttix_unit, ttix_unit.wait);  /* check soon for more input */
        return ttix_buf[ln];                            /* return buf */

    default:
        return (stop_inst << IOT_V_REASON) + AC;
        }                                               /* end switch */

return AC;
}

/* Unit service */

t_stat ttix_svc (UNIT *uptr)
{
int32 ln, c, temp;

if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return SCPE_OK;
sim_clock_coschedule (uptr, tmxr_poll);                 /* continue poll */
ln = tmxr_poll_conn (&ttx_desc);                        /* look for connect */
if (ln >= 0)                                            /* got one? rcv enb*/
    ttx_ldsc[ln].rcve = 1;
tmxr_poll_rx (&ttx_desc);                               /* poll for input */
for (ln = 0; ln < TTX_LINES; ln++) {                    /* loop thru lines */
    if (ttx_ldsc[ln].conn) {                            /* connected? */
        if (dev_done & (INT_TTI1 << ln))                /* Last character still pending? */
            continue;
        if ((temp = tmxr_getc_ln (&ttx_ldsc[ln]))) {    /* get char */
            if (temp & SCPE_BREAK)                      /* break? */
                c = 0;
            else c = sim_tt_inpcvt (temp, TT_GET_MODE (ttox_unit[ln].flags));
            ttix_buf[ln] = c;
            dev_done = dev_done | (INT_TTI1 << ln);
            int_req = INT_UPDATE;
            }
        }
    }
return SCPE_OK;
}

/* Reset routine */

t_stat ttix_reset (DEVICE *dptr)
{
int32 ln, itto;

ttx_enbdis (dptr->flags & DEV_DIS);                     /* sync enables */
if (ttix_unit.flags & UNIT_ATT)                         /* if attached, */
    sim_activate (&ttix_unit, tmxr_poll);               /* activate */
else sim_cancel (&ttix_unit);                           /* else stop */
for (ln = 0; ln < TTX_LINES; ln++) {                    /* for all lines */
    ttix_buf[ln] = 0;                                   /* clear buf, */
    itto = (INT_TTI1 << ln);                            /* interrupt */
    dev_done = dev_done & ~itto;                        /* clr done, int */
    int_req = int_req & ~itto;
    int_enable = int_enable | itto;                     /* set enable */
    }
return SCPE_OK;
}

/* Terminal output: IOT routine */

int32 ttox (int32 inst, int32 AC)
{
int32 pulse = inst & 07;                                /* pulse */
int32 ln = TTX_GETLN (inst);                            /* line # */
int32 itti = (INT_TTI1 << ln);                          /* rx intr */
int32 itto = (INT_TTO1 << ln);                          /* tx intr */

switch (pulse) {                                        /* case IR<9:11> */

    case 0:                                             /* TLF */
        dev_done = dev_done | itto;                     /* set flag */
        int_req = INT_UPDATE;                           /* update intr */
        break;

    case 1:                                             /* TSF */
        return (dev_done & itto)? IOT_SKP + AC: AC;

    case 2:                                             /* TCF */
        dev_done = dev_done & ~itto;                    /* clear flag */
        int_req = int_req & ~itto;                      /* clear intr */
        break;

    case 5:                                             /* SPI */
        return (int_req & (itti | itto))? IOT_SKP + AC: AC;

    case 6:                                             /* TLS */
        dev_done = dev_done & ~itto;                    /* clear flag */
        int_req = int_req & ~itto;                      /* clear int req */
    case 4:                                             /* TPC */
        sim_activate (&ttox_unit[ln], ttox_unit[ln].wait); /* activate */
        ttox_buf[ln] = AC & 0377;                       /* load buffer */
        break;

   default:
        return (stop_inst << IOT_V_REASON) + AC;
        }                                               /* end switch */

return AC;
}

/* Unit service */

t_stat ttox_svc (UNIT *uptr)
{
int32 c, ln = uptr - ttox_unit;                         /* line # */

if (ttx_ldsc[ln].conn) {                                /* connected? */
    if (ttx_ldsc[ln].xmte) {                            /* tx enabled? */
        TMLN *lp = &ttx_ldsc[ln];                       /* get line */
        c = sim_tt_outcvt (ttox_buf[ln], TT_GET_MODE (ttox_unit[ln].flags));
        if (c >= 0)                                     /* output char */
            tmxr_putc_ln (lp, c);
        tmxr_poll_tx (&ttx_desc);                       /* poll xmt */
        }
    else {
        tmxr_poll_tx (&ttx_desc);                       /* poll xmt */
        sim_activate (uptr, ttox_unit[ln].wait);        /* wait */
        return SCPE_OK;
        }
    }
dev_done = dev_done | (INT_TTO1 << ln);                 /* set done */
int_req = INT_UPDATE;                                   /* update intr */
return SCPE_OK;
}

/* Reset routine */

t_stat ttox_reset (DEVICE *dptr)
{
int32 ln, itto;

ttx_enbdis (dptr->flags & DEV_DIS);                     /* sync enables */
for (ln = 0; ln < TTX_LINES; ln++) {                    /* for all lines */
    ttox_buf[ln] = 0;                                   /* clear buf */
    itto = (INT_TTO1 << ln);                            /* interrupt */
    dev_done = dev_done & ~itto;                        /* clr done, int */
    int_req = int_req & ~itto;
    int_enable = int_enable | itto;                     /* set enable */
    sim_cancel (&ttox_unit[ln]);                        /* deactivate */
    }
return SCPE_OK;
}

/* Attach master unit */

t_stat ttx_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = tmxr_attach (&ttx_desc, uptr, cptr);                /* attach */
if (r != SCPE_OK)                                       /* error */
    return r;
sim_activate (uptr, 0);                                 /* start poll at once */
return SCPE_OK;
}

/* Detach master unit */

t_stat ttx_detach (UNIT *uptr)
{
int32 i;
t_stat r;

r = tmxr_detach (&ttx_desc, uptr);                      /* detach */
for (i = 0; i < TTX_LINES; i++)                         /* all lines, */
    ttx_ldsc[i].rcve = 0;                               /* disable rcv */
sim_cancel (uptr);                                      /* stop poll */
return r;
}

/* Enable/disable device */

void ttx_enbdis (int32 dis)
{
if (dis) {
    ttix_dev.flags = ttix_dev.flags | DEV_DIS;
    ttox_dev.flags = ttox_dev.flags | DEV_DIS;
    }
else {
    ttix_dev.flags = ttix_dev.flags & ~DEV_DIS;
    ttox_dev.flags = ttox_dev.flags & ~DEV_DIS;
    }
return;
}
