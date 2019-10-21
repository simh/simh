/* pdp18b_ttx.c: PDP-9/15 additional terminals simulator

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

   ttix,ttox    LT15/LT19 terminal input/output

   19-Sep-16    RMS     Limit connection poll to configured lines
   13-Sep-15    RMS     Added APIVEC register
   11-Oct-13    RMS     Poll TTIX immediately to pick up initial connect
   18-Apr-12    RMS     Revised to use clock coscheduling
   19-Nov-08    RMS     Revised for common TMXR show routines
   18-Jun-07    RMS     Added UNIT_IDLE flag
   30-Sep-06    RMS     Fixed handling of non-printable characters in KSR mode
   22-Nov-05    RMS     Revised for new terminal processing routines
   29-Jun-05    RMS     Added SET TTOXn DISCONNECT
   21-Jun-05    RMS     Fixed bug in SHOW CONN/STATS
   14-Jan-04    RMS     Cloned from pdp8_ttx.c

   This module implements 16 individual serial interfaces similar in function
   to the console.  These interfaces are mapped to Telnet based connections as
   though they were the four lines of a terminal multiplexor.  The connection
   polling mechanism is superimposed onto the keyboard of the first interface.
*/

#include "pdp18b_defs.h"
#include "sim_tmxr.h"

#if defined (PDP15)
#define TTX_MAXL        16                              /* max number of lines */
#elif defined (PDP9)
#define TTX_MAXL        4
#else
#define TTX_MAXL        1
#endif

uint32 ttix_done = 0;                                   /* input flags */
uint32 ttox_done = 0;                                   /* output flags */
uint8 ttix_buf[TTX_MAXL] = { 0 };                       /* input buffers */
uint8 ttox_buf[TTX_MAXL] = { 0 };                       /* output buffers */
TMLN ttx_ldsc[TTX_MAXL] = { {0} };                      /* line descriptors */
TMXR ttx_desc = { 1, 0, 0, ttx_ldsc };                  /* mux descriptor */
#define ttx_lines ttx_desc.lines                        /* current number of lines */

extern int32 int_hwre[API_HLVL+1];
extern int32 api_vec[API_HLVL][32];
extern int32 tmxr_poll;

DEVICE ttix_dev, ttox_dev;
int32 ttix (int32 dev, int32 pulse, int32 dat);
int32 ttox (int32 dev, int32 pulse, int32 dat);
t_stat ttix_svc (UNIT *uptr);
t_bool ttix_test_done (int32 ln);
void ttix_set_done (int32 ln);
void ttix_clr_done (int32 ln);
t_stat ttox_svc (UNIT *uptr);
t_bool ttox_test_done (int32 ln);
void ttox_set_done (int32 ln);
void ttox_clr_done (int32 ln);
int32 ttx_getln (int32 dev, int32 pulse);
t_stat ttx_attach (UNIT *uptr, CONST char *cptr);
t_stat ttx_detach (UNIT *uptr);
t_stat ttx_reset (DEVICE *dptr);
void ttx_reset_ln (int32 i);
t_stat ttx_vlines (UNIT *uptr, int32 val, CONST char *cptr, void *desc);

/* TTIx data structures

   ttix_dev     TTIx device descriptor
   ttix_unit    TTIx unit descriptor
   ttix_reg     TTIx register list
   ttix_mod     TTIx modifiers list
*/

DIB ttix_dib = { 
    DEV_TTO1, 8, NULL,
    { &ttox, &ttix, &ttox, &ttix, &ttox, &ttix, &ttox, &ttix }
    };

UNIT ttix_unit = { UDATA (&ttix_svc, UNIT_IDLE|UNIT_ATTABLE, 0), KBD_POLL_WAIT };

REG ttix_reg[] = {
    { BRDATAD (BUF, ttix_buf, 8, 8, TTX_MAXL, "last character received, lines 0 to 3/15") },
    { ORDATAD (DONE, ttix_done, TTX_MAXL, "input ready flags, line 0 on right") },
    { FLDATAD (INT, int_hwre[API_TTI1], INT_V_TTI1, "interrupt pending flag") },
    { DRDATAD (TIME, ttix_unit.wait, 24, "keyboard polling interval"), REG_NZ + PV_LEFT },
    { ORDATA (DEVNUM, ttix_dib.dev, 6), REG_HRO },
    { DRDATA (LINES, ttx_desc.lines, 6), REG_HRO },
#if defined (PDP15)
    { ORDATA (APIVEC, api_vec[API_TTI1][INT_V_TTI1], 6), REG_HRO },
#endif
    { NULL }
    };

MTAB ttix_mod[] = {
    { MTAB_XTD | MTAB_VDV, 0, "LINES", "LINES",
      &ttx_vlines, &tmxr_show_lines, (void *) &ttx_desc },
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &ttx_desc },
    { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, (void *) &ttx_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *) &ttx_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *) &ttx_desc },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_devno, &show_devno, NULL },
    { 0 }
    };

DEVICE tti1_dev = {
    "TTIX", &ttix_unit, ttix_reg, ttix_mod,
    1, 10, 31, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &ttx_reset,
    NULL, &ttx_attach, &ttx_detach,
    &ttix_dib, DEV_MUX | DEV_DISABLE
    };

/* TTOx data structures

   ttox_dev     TTOx device descriptor
   ttox_unit    TTOx unit descriptor
   ttox_reg     TTOx register list
*/

UNIT ttox_unit[] = {
    { UDATA (&ttox_svc, TT_MODE_KSR, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_KSR+UNIT_DIS, 0), SERIAL_OUT_WAIT }
    };

REG ttox_reg[] = {
    { BRDATAD (BUF, ttox_buf, 8, 8, TTX_MAXL, "last character transmitted, lines 0 to 3/15") },
    { ORDATAD (DONE, ttox_done, TTX_MAXL, "output ready flags, line 0 on right") },
    { FLDATAD (INT, int_hwre[API_TTO1], INT_V_TTO1, "interrupt pending flag") },
    { URDATAD (TIME, ttox_unit[0].wait, 10, 24, 0,
              TTX_MAXL, PV_LEFT, "time from initiation to interrupt, lines 0 to 3/15") },
#if defined (PDP15)
    { ORDATA (APIVEC, api_vec[API_TTO1][INT_V_TTO1], 6), REG_HRO },
#endif
    { NULL }
    };

MTAB ttox_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", NULL },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  NULL },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  NULL },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  NULL },
    { MTAB_XTD|MTAB_VUN, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &ttx_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, &ttx_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, &ttx_desc },
    { 0 }
    };

DEVICE tto1_dev = {
    "TTOX", ttox_unit, ttox_reg, ttox_mod,
    TTX_MAXL, 10, 31, 1, 8, 8,
    NULL, NULL, &ttx_reset, 
    NULL, NULL, NULL,
    NULL, DEV_DISABLE
    };

/* Terminal input: IOT routine */

int32 ttix (int32 dev, int32 pulse, int32 dat)
{
int32 ln = ttx_getln (dev, pulse);                      /* line # */

if (ln > ttx_lines)
    return dat;
if (pulse & 001) {                                      /* KSF1 */
    if (ttix_test_done (ln))
        dat = dat | IOT_SKP;
    }
if (pulse & 002) {                                      /* KRB1 */
    ttix_clr_done (ln);                                 /* clear flag */
    dat = dat | ttix_buf[ln];                           /* return buffer */
    }
return dat;
}

/* Unit service */

t_stat ttix_svc (UNIT *uptr)
{
int32 ln, c, temp;

if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return SCPE_OK;
ln = tmxr_poll_conn (&ttx_desc);                        /* look for connect */
if (ln >= 0)                                            /* got one? rcv enab */
    ttx_ldsc[ln].rcve = 1;
tmxr_poll_rx (&ttx_desc);                               /* poll for input */
for (ln = 0; ln < ttx_lines; ln++) {                    /* loop thru lines */
    if ((temp = tmxr_getc_ln (&ttx_ldsc[ln]))) {        /* get char */
        if (temp & SCPE_BREAK)                          /* break? */
            c = 0;
        else
            c = sim_tt_inpcvt (temp, TT_GET_MODE (ttox_unit[ln].flags) | TTUF_KSR);
        ttix_buf[ln] = c;
        ttix_set_done (ln);
        }
    }
sim_clock_coschedule (uptr, tmxr_poll);                 /* continue poll */
return SCPE_OK;
}

/* Interrupt handling routines */

t_bool ttix_test_done (int32 ln)
{
if (ttix_done & (1 << ln))
    return TRUE;
return FALSE;
}

void ttix_set_done (int32 ln)
{
ttix_done = ttix_done | (1 << ln);
SET_INT (TTI1);
return;
}

void ttix_clr_done (int32 ln)
{
ttix_done = ttix_done & ~(1 << ln);
if (ttix_done) {
    SET_INT (TTI1);
    }
else {
    CLR_INT (TTI1);
    }
return;
}

/* Terminal output: IOT routine */

int32 ttox (int32 dev, int32 pulse, int32 dat)
{
int32 ln = ttx_getln (dev, pulse);                      /* line # */

if (ln > ttx_lines)
    return dat;
if (pulse & 001) {                                      /* TSF */
    if (ttox_test_done (ln))
        dat = dat | IOT_SKP;
    }
if (pulse & 002)                                        /* clear flag */
    ttox_clr_done (ln);
if (pulse & 004) {                                      /* load buffer */
    sim_activate (&ttox_unit[ln], ttox_unit[ln].wait);  /* activate unit */
    ttox_buf[ln] = dat & 0377;                          /* load buffer */
    }
return dat;
}

/* Unit service */

t_stat ttox_svc (UNIT *uptr)
{
int32 c, ln = uptr - ttox_unit;                         /* line # */

if (ttx_ldsc[ln].conn) {                                /* connected? */
    if (ttx_ldsc[ln].xmte) {                            /* tx enabled? */
        TMLN *lp = &ttx_ldsc[ln];                       /* get line */
        c = sim_tt_outcvt (ttox_buf[ln], TT_GET_MODE (ttox_unit[ln].flags) | TTUF_KSR);
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
ttox_set_done (ln);                                     /* set done */
return SCPE_OK;
}

/* Interrupt handling routines */

t_bool ttox_test_done (int32 ln)
{
if (ttox_done & (1 << ln))
    return TRUE;
return FALSE;
}

void ttox_set_done (int32 ln)
{
ttox_done = ttox_done | (1 << ln);
SET_INT (TTO1);
return;
}

void ttox_clr_done (int32 ln)
{
ttox_done = ttox_done & ~(1 << ln);
if (ttox_done) {
    SET_INT (TTO1);
    }
else {
    CLR_INT (TTO1);
    }
return;
}

/* Compute relative line number

   This algorithm does not assign contiguous line numbers of ascending
   LT19's.  Rather, line numbers follow a simple progression based on
   the relative IOT number and the subdevice select */

int32 ttx_getln (int32 dev, int32 pulse)
{
int32 rdno = ((dev - ttix_dib.dev) >> 1) & 3;

#if defined (PDP15)                                     /* PDP-15? */
int32 sub = (pulse >> 4) & 3;
return (rdno * 4) + sub;                                /* use dev, subdev */
#else                                                   /* others */                    
return rdno;                                            /* use dev only */
#endif
}

/* Reset routine */

t_stat ttx_reset (DEVICE *dptr)
{
int32 ln;

if (dptr->flags & DEV_DIS) {                            /* sync enables */
    ttix_dev.flags = ttix_dev.flags | DEV_DIS;
    ttox_dev.flags = ttox_dev.flags | DEV_DIS;
    }
else {
    ttix_dev.flags = ttix_dev.flags & ~DEV_DIS;
    ttox_dev.flags = ttox_dev.flags & ~DEV_DIS;
    }
if (ttix_unit.flags & UNIT_ATT)                         /* if attached, */
    sim_activate (&ttix_unit, tmxr_poll);               /* activate */
else sim_cancel (&ttix_unit);                           /* else stop */
for (ln = 0; ln < TTX_MAXL; ln++)                       /* for all lines */
    ttx_reset_ln (ln);
return SCPE_OK;
}

/* Reset line n */

void ttx_reset_ln (int32 ln)
{
ttix_buf[ln] = 0;                                       /* clear buf, */
ttox_buf[ln] = 0;
ttix_clr_done (ln);                                     /* clear done */
ttox_clr_done (ln);
sim_cancel (&ttox_unit[ln]);                            /* stop poll */
return;
}

/* Attach master unit */

t_stat ttx_attach (UNIT *uptr, CONST char *cptr)
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
sim_cancel (uptr);                                      /* stop poll */
for (i = 0; i < TTX_MAXL; i++)                          /* disable rcv */
    ttx_ldsc[i].rcve = 0;
return r;
}

/* Change number of lines */

t_stat ttx_vlines (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 newln, i, t;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
newln = get_uint (cptr, 10, TTX_MAXL, &r);
if ((r != SCPE_OK) || (newln == ttx_lines))
    return r;
if (newln == 0)
    return SCPE_ARG;
if (newln < ttx_lines) {
    for (i = newln, t = 0; i < ttx_lines; i++)
        t = t | ttx_ldsc[i].conn;
    if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
        return SCPE_OK;
    for (i = newln; i < ttx_lines; i++) {
        if (ttx_ldsc[i].conn) {
            tmxr_linemsg (&ttx_ldsc[i], "\r\nOperator disconnected line\r\n");
            tmxr_reset_ln (&ttx_ldsc[i]);               /* reset line */
            }
        ttox_unit[i].flags = ttox_unit[i].flags | UNIT_DIS;
        ttx_reset_ln (i);
        }
    }
else {
    for (i = ttx_lines; i < newln; i++) {
        ttox_unit[i].flags = ttox_unit[i].flags & ~UNIT_DIS;
        ttx_reset_ln (i);
        }
    }
ttx_lines = newln;
return SCPE_OK;
}


