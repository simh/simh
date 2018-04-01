/* pdp8_ttx.c: PDP-8 additional terminals simulator

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

   ttix,ttox    PT08/KL8JA terminal input/output

   18-Sep-16    RMS     Expanded support to 16 terminals
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

   This module implements 1-16 individual serial interfaces similar in function
   to the console.  These interfaces are mapped to Telnet based connections as
   though they were the 16 lines of a terminal multiplexor.  The connection
   polling mechanism is superimposed onto the keyboard of the first interface.

   The done and enable flags are maintained locally, and only a master interrupt
   request is maintained in global register dev_done. Because this is actually
   an interrupt request flag, the corresponding bit in int_enable must always
   be set to 1.
*/

#include "pdp8_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#define TTX_MAXL        16
#define TTX_INIL        4

#define TTX_GETLN(x)    (((x) >> 4) & TTX_MASK)

extern int32 int_req, int_enable, dev_done, stop_inst;
extern int32 tmxr_poll;

uint32 ttix_done = 0;                                   /* input ready flags */
uint32 ttox_done = 0;                                   /* output ready flags */
uint32 ttx_enbl = 0;                                    /* intr enable flags */
uint8 ttix_buf[TTX_MAXL] = { 0 };                       /* input buffers */
uint8 ttox_buf[TTX_MAXL] = { 0 };                       /* output buffers */
TMLN ttx_ldsc[TTX_MAXL] = { {0} };                      /* line descriptors */
TMXR ttx_desc = { TTX_INIL, 0, 0, ttx_ldsc };           /* mux descriptor */
#define ttx_lines       ttx_desc.lines

int32 ttix (int32 IR, int32 AC);
int32 ttox (int32 IR, int32 AC);
t_stat ttix_svc (UNIT *uptr);
t_stat ttox_svc (UNIT *uptr);
int32 ttx_getln (int32 inst);
void ttx_new_flags (uint32 newi, uint32 newo, uint32 newe);
t_stat ttx_reset (DEVICE *dptr);
t_stat ttx_attach (UNIT *uptr, CONST char *cptr);
t_stat ttx_detach (UNIT *uptr);
void ttx_reset_ln (int32 i);
t_stat ttx_vlines (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ttx_show_devno (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

#define TTIX_SET_DONE(ln)       ttx_new_flags (ttix_done | (1u << (ln)), ttox_done, ttx_enbl)
#define TTIX_CLR_DONE(ln)       ttx_new_flags (ttix_done & ~(1u << (ln)), ttox_done, ttx_enbl)
#define TTIX_TST_DONE(ln)       ((ttix_done & (1u << (ln))) != 0)
#define TTOX_SET_DONE(ln)       ttx_new_flags (ttix_done, ttox_done | (1u << (ln)), ttx_enbl)
#define TTOX_CLR_DONE(ln)       ttx_new_flags (ttix_done, ttox_done & ~(1u << (ln)), ttx_enbl)
#define TTOX_TST_DONE(ln)       ((ttox_done & (1u << (ln))) != 0)
#define TTX_SET_ENBL(ln)        ttx_new_flags (ttix_done, ttox_done, ttx_enbl | (1u << (ln)))
#define TTX_CLR_ENBL(ln)        ttx_new_flags (ttix_done, ttox_done, ttx_enbl & ~(1u << (ln)))
#define TTX_TST_ENBL(ln)        ((ttx_enbl & (1u << (ln))) != 0)

/* TTIx data structures

   ttix_dev     TTIx device descriptor
   ttix_unit    TTIx unit descriptor
   ttix_reg     TTIx register list
   ttix_mod     TTIx modifiers list
*/

DIB_DSP ttx_dsp[TTX_MAXL * 2] = {
    { DEV_TTI1,  &ttix }, { DEV_TTO1,  &ttox },
    { DEV_TTI2,  &ttix }, { DEV_TTO2,  &ttox },
    { DEV_TTI3,  &ttix }, { DEV_TTO3,  &ttox },
    { DEV_TTI4,  &ttix }, { DEV_TTO4,  &ttox },
    { DEV_TTI5,  &ttix }, { DEV_TTO5,  &ttox },
    { DEV_TTI6,  &ttix }, { DEV_TTO6,  &ttox },
    { DEV_TTI7,  &ttix }, { DEV_TTO7,  &ttox },
    { DEV_TTI8,  &ttix }, { DEV_TTO8,  &ttox },
    { DEV_TTI9,  &ttix }, { DEV_TTO9,  &ttox },
    { DEV_TTI10, &ttix }, { DEV_TTO10, &ttox },
    { DEV_TTI11, &ttix }, { DEV_TTO11, &ttox },
    { DEV_TTI12, &ttix }, { DEV_TTO12, &ttox },
    { DEV_TTI13, &ttix }, { DEV_TTO13, &ttox },
    { DEV_TTI14, &ttix }, { DEV_TTO14, &ttox },
    { DEV_TTI15, &ttix }, { DEV_TTO15, &ttox },
    { DEV_TTI16, &ttix }, { DEV_TTO16, &ttox }
    };

DIB ttx_dib = { DEV_TTI1, TTX_INIL * 2, { &ttix, &ttox }, ttx_dsp };

UNIT ttix_unit = { UDATA (&ttix_svc, UNIT_IDLE|UNIT_ATTABLE, 0), SERIAL_IN_WAIT };

REG ttix_reg[] = {
    { BRDATAD (BUF, ttix_buf, 8, 8, TTX_MAXL, "input buffer, lines 0 to 15") },
    { ORDATAD (DONE, ttix_done, TTX_MAXL, "device done flag (line 0 rightmost)") },
    { ORDATAD (ENABLE, ttx_enbl, TTX_MAXL, "interrupt enable flag") },
    { FLDATA  (SUMDONE, dev_done, INT_V_TTI1), REG_HRO },
    { FLDATA  (SUMENABLE, int_enable, INT_V_TTI1), REG_HRO },
    { DRDATAD (TIME, ttix_unit.wait, 24, "initial polling interval"), REG_NZ + PV_LEFT },
    { DRDATA  (LINES, ttx_desc.lines, 6), REG_HRO },
    { NULL }
    };

MTAB ttix_mod[] = {
    { MTAB_VDV,            0,       "LINES",      "LINES", &ttx_vlines,  &tmxr_show_lines, (void *) &ttx_desc },
    { MTAB_VDV,            0,      "DEVNO",          NULL, NULL,         &ttx_show_devno, (void *) &ttx_desc },
    { UNIT_ATT,     UNIT_ATT,     "SUMMARY",         NULL, NULL,         &tmxr_show_summ,  (void *) &ttx_desc },
    { MTAB_VDV,            1,          NULL, "DISCONNECT", &tmxr_dscln,  NULL,             (void *) &ttx_desc },
    { MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS",         NULL, NULL,         &tmxr_show_cstat, (void *) &ttx_desc },
    { MTAB_VDV | MTAB_NMO, 0, "STATISTICS",          NULL, NULL,         &tmxr_show_cstat, (void *) &ttx_desc },
    { 0 }
    };

/* debugging bitmaps */
#define DBG_XMT  TMXR_DBG_XMT                           /* display Transmitted Data */
#define DBG_RCV  TMXR_DBG_RCV                           /* display Received Data */
#define DBG_RET  TMXR_DBG_RET                           /* display Returned Received Data */
#define DBG_CON  TMXR_DBG_CON                           /* display connection activities */
#define DBG_TRC  TMXR_DBG_TRC                           /* display trace routine calls */

DEBTAB ttx_debug[] = {
  {"XMT",    DBG_XMT, "Transmitted Data"},
  {"RCV",    DBG_RCV, "Received Data"},
  {"RET",    DBG_RET, "Returned Received Data"},
  {"CON",    DBG_CON, "connection activities"},
  {"TRC",    DBG_TRC, "trace routine calls"},
  {0}
};

DEVICE ttix_dev = {
    "TTIX", &ttix_unit, ttix_reg, ttix_mod,
    1, 10, 31, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &ttx_reset,
    NULL, &ttx_attach, &ttx_detach,
    &ttx_dib, DEV_MUX | DEV_DISABLE | DEV_DEBUG,
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
    { UDATA (&ttox_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC+UNIT_DIS, 0), SERIAL_OUT_WAIT },
    { UDATA (&ttox_svc, TT_MODE_UC+UNIT_DIS, 0), SERIAL_OUT_WAIT }
    };

REG ttox_reg[] = {
    { BRDATAD (BUF, ttox_buf, 8, 8, TTX_MAXL, "last data item processed, lines 0 to 3") },
    { ORDATAD  (DONE, ttox_done, TTX_MAXL, "device done flag (line 0 rightmost)") },
    { ORDATAD  (ENABLE, ttx_enbl, TTX_MAXL, "interrupt enable flag") },
    { FLDATA  (SUMDONE, dev_done, INT_V_TTO1), REG_HRO },
    { FLDATA  (SUMENABLE, int_enable, INT_V_TTO1), REG_HRO },
    { URDATAD (TIME, ttox_unit[0].wait, 10, 24, 0,
              TTX_MAXL, PV_LEFT, "line from I/O initiation to interrupt, lines 0 to 3") },
    { NULL }
    };

MTAB ttox_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", NULL },
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
    { MTAB_VDV, 0, "DEVNO", NULL, NULL, &ttx_show_devno, &ttx_desc },
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
    TTX_MAXL, 10, 31, 1, 8, 8,
    NULL, NULL, &ttx_reset, 
    NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DEBUG,
    0, ttx_debug
    };

/* Terminal input: IOT routine */

int32 ttix (int32 inst, int32 AC)
{
int32 pulse = inst & 07;                                /* IOT pulse */
int32 ln = ttx_getln (inst);                            /* line # */

if (ln < 0)                                             /* bad line #? */
    return (SCPE_IERR << IOT_V_REASON) | AC;

switch (pulse) {                                        /* case IR<9:11> */

    case 0:                                             /* KCF */
        TTIX_CLR_DONE (ln);                             /* clear flag */
        break;

    case 1:                                             /* KSF */
        return (TTIX_TST_DONE (ln))? IOT_SKP | AC: AC;

    case 2:                                             /* KCC */
        TTIX_CLR_DONE (ln);                             /* clear flag */
        sim_activate_abs (&ttix_unit, ttix_unit.wait);  /* check soon for more input */
        return 0;                                       /* clear AC */

    case 4:                                             /* KRS */
        return (AC | ttix_buf[ln]);                     /* return buf */

    case 5:                                             /* KIE */
        if (AC & 1)
            TTX_SET_ENBL (ln);
        else TTX_CLR_ENBL (ln);
        break;

    case 6:                                             /* KRB */
        TTIX_CLR_DONE (ln);                             /* clear flag */
        sim_activate_abs (&ttix_unit, ttix_unit.wait);  /* check soon for more input */
        return ttix_buf[ln];                            /* return buf */

    default:
        return (stop_inst << IOT_V_REASON) | AC;
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
if (ln >= 0)                                            /* got one? */
    ttx_ldsc[ln].rcve = 1;                              /* set rcv enable */
tmxr_poll_rx (&ttx_desc);                               /* poll for input */
for (ln = 0; ln < ttx_lines; ln++) {                    /* loop thru lines */
    if (ttx_ldsc[ln].conn) {                            /* connected? */
        if (TTIX_TST_DONE (ln))                         /* last char still pending? */
            continue;
        if ((temp = tmxr_getc_ln (&ttx_ldsc[ln]))) {    /* get char */
            if (temp & SCPE_BREAK)                      /* break? */
                c = 0;
            else c = sim_tt_inpcvt (temp, TT_GET_MODE (ttox_unit[ln].flags));
            ttix_buf[ln] = c;
            TTIX_SET_DONE (ln);                         /* set flag */
            }
        }
    }
return SCPE_OK;
}

/* Terminal output: IOT routine */

int32 ttox (int32 inst, int32 AC)
{
int32 pulse = inst & 07;                                /* pulse */
int32 ln = ttx_getln (inst);                            /* line # */

if (ln < 0)                                             /* bad line #? */
    return (SCPE_IERR << IOT_V_REASON) | AC;

switch (pulse) {                                        /* case IR<9:11> */

    case 0:                                             /* TLF */
        TTOX_SET_DONE (ln);                             /* set flag */
        break;

    case 1:                                             /* TSF */
        return (TTOX_TST_DONE (ln))? IOT_SKP | AC: AC;

    case 2:                                             /* TCF */
        TTOX_CLR_DONE (ln);                             /* clear flag */
        break;

    case 5:                                             /* SPI */
        if ((TTIX_TST_DONE (ln) || TTOX_TST_DONE (ln))  /* either done set */
            && TTX_TST_ENBL (ln))                       /* and enabled? */
            return IOT_SKP | AC;
        return AC;

    case 6:                                             /* TLS */
        TTOX_CLR_DONE (ln);                             /* clear flag */
    case 4:                                             /* TPC */
        sim_activate (&ttox_unit[ln], ttox_unit[ln].wait); /* activate */
        ttox_buf[ln] = AC & 0377;                       /* load buffer */
        break;

   default:
        return (stop_inst << IOT_V_REASON) | AC;
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
TTOX_SET_DONE (ln);                                     /* set done */
return SCPE_OK;
}

/* Flag routine

   Global dev_done is used as a master interrupt; therefore, global
   int_enable must always be set
*/

void ttx_new_flags (uint32 newidone, uint32 newodone, uint32 newenbl)
{
ttix_done = newidone;
ttox_done = newodone;
ttx_enbl = newenbl;
if ((ttix_done & ttx_enbl) != 0)
    dev_done |= INT_TTI1;
else dev_done &= ~INT_TTI1;
if ((ttox_done & ttx_enbl) != 0)
    dev_done |= INT_TTO1;
else dev_done &= ~INT_TTO1;
int_enable |= (INT_TTI1 | INT_TTO1);
int_req = INT_UPDATE;
return;
}

/* Compute relative line number, based on table of device numbers */

int32 ttx_getln (int32 inst)
{
int32 i;
int32 device = (inst >> 3) & 077;                       /* device = IR<3:8> */

for (i = 0; i < (ttx_lines * 2); i++) {                 /* loop thru disp tbl */
    if (device == ttx_dsp[i].dev)                       /* dev # match? */
        return (i >> 1);                                /* return line # */
    }
return -1;
}

/* Reset routine */

t_stat ttx_reset (DEVICE *dptr)
{
int32 ln;

if (dptr->flags & DEV_DIS) {                            /* sync enables */
    ttix_dev.flags |= DEV_DIS;
    ttox_dev.flags |= DEV_DIS;
    }
else {
    ttix_dev.flags &= ~DEV_DIS;
    ttox_dev.flags &= ~DEV_DIS;
    }
if (ttix_unit.flags & UNIT_ATT)                         /* if attached, */
    sim_activate (&ttix_unit, tmxr_poll);               /* activate */
else sim_cancel (&ttix_unit);                           /* else stop */
for (ln = 0; ln < TTX_MAXL; ln++)                       /* for all lines */
    ttx_reset_ln (ln);                                  /* reset line */
int_enable |= (INT_TTI1 | INT_TTO1);                    /* set master enable */
return SCPE_OK;
}

/* Reset line n */

void ttx_reset_ln (int32 ln)
{
uint32 mask = (1u << ln);

ttix_buf[ln] = 0;                                       /* clr buf */
ttox_buf[ln] = 0;                                       /* clr done, set enbl */
ttx_new_flags (ttix_done & ~mask, ttox_done & ~mask, ttx_enbl | mask);
sim_cancel (&ttox_unit[ln]);                            /* stop output */
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
for (i = 0; i < TTX_MAXL; i++)                         /* all lines, */
    ttx_ldsc[i].rcve = 0;                               /* disable rcv */
sim_cancel (uptr);                                      /* stop poll */
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
        ttox_unit[i].flags |= UNIT_DIS;
        ttx_reset_ln (i);
        }
    }
else {
    for (i = ttx_lines; i < newln; i++) {
        ttox_unit[i].flags &= ~UNIT_DIS;
        ttx_reset_ln (i);
        }
    }
ttx_lines = newln;
ttx_dib.num = newln * 2;
return SCPE_OK;
}

/* Show device numbers */
t_stat ttx_show_devno (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int32 i, dev_offset;
DEVICE *dptr;

if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
/* Select correct devno entry for Input or Output device */
if (dptr->name[2] == 'O')
    dev_offset = 1;
else
    dev_offset = 0;

fprintf(st, "devno=");
for (i = 0; i < ttx_lines; i++) {
    fprintf(st, "%02o%s", ttx_dsp[i*2+dev_offset].dev, i < ttx_lines-1 ? 
         "," : "");
}
return SCPE_OK;
}

