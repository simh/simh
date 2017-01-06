/* pdp1_dcs.c: PDP-1D terminal multiplexor simulator

   Copyright (c) 2006-2013, Robert M Supnik

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

   dcs          Type 630 data communications subsystem

   11-Oct-2013  RMS     Poll DCS immediately after attach to pick up connect
   19-Nov-2008  RMS     Revised for common TMXR show routines

   This module implements up to 32 individual serial interfaces.
*/

#include "pdp1_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

#define DCS_LINES       32                              /* lines */
#define DCS_LINE_MASK   (DCS_LINES - 1)
#define DCSL_WAIT       1000                            /* output wait */
#define DCS_NUMLIN      dcs_desc.lines

int32 dcs_sbs = 0;                                      /* SBS level */
uint32 dcs_send = 0;                                    /* line for send */
uint32 dcs_scan = 0;                                    /* line for scanner */
uint8 dcs_flg[DCS_LINES];                               /* line flags */
uint8 dcs_buf[DCS_LINES];                               /* line bufffers */

extern int32 iosta, stop_inst;
extern int32 tmxr_poll;

TMLN dcs_ldsc[DCS_LINES] = { {0} };                     /* line descriptors */
TMXR dcs_desc = { DCS_LINES, 0, 0, dcs_ldsc };          /* mux descriptor */

t_stat dcsi_svc (UNIT *uptr);
t_stat dcso_svc (UNIT *uptr);
t_stat dcs_reset (DEVICE *dptr);
t_stat dcs_attach (UNIT *uptr, CONST char *cptr);
t_stat dcs_detach (UNIT *uptr);
t_stat dcs_vlines (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
void dcs_reset_ln (int32 ln);
void dcs_scan_next (t_bool unlk);

/* DCS data structures

   dcs_dev      DCS device descriptor
   dcs_unit     DCS unit descriptor
   dcs_reg      DCS register list
   dcs_mod      DCS modifiers list
*/

UNIT dcs_unit = { UDATA (&dcsi_svc, UNIT_ATTABLE, 0) };

REG dcs_reg[] = {
    { BRDATAD (BUF, dcs_buf, 8, 8, DCS_LINES, "input buffer, lines 0 to 31") },
    { BRDATAD (FLAGS, dcs_flg, 8, 1, DCS_LINES, "line ready flag, lines 0 to 31") },
    { FLDATAD (SCNF, iosta, IOS_V_DCS, "scanner ready flag") },
    { ORDATAD (SCAN, dcs_scan, 5, "scanner line number") },
    { ORDATAD (SEND, dcs_send, 5, "output line number") },
    { DRDATA (SBSLVL, dcs_sbs, 4), REG_HRO },
    { NULL }
    };

MTAB dcs_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "SBSLVL", "SBSLVL",
      &dev_set_sbs, &dev_show_sbs, (void *) &dcs_sbs },
    { MTAB_XTD | MTAB_VDV, 0, "LINES", "LINES",
      &dcs_vlines, &tmxr_show_lines, (void *) &dcs_desc },
    { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, (void *) &dcs_desc },
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &dcs_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *) &dcs_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *) &dcs_desc },
    { 0 }
    };

DEVICE dcs_dev = {
    "DCS", &dcs_unit, dcs_reg, dcs_mod,
    1, 10, 31, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &dcs_reset,
    NULL, &dcs_attach, &dcs_detach,
    NULL, DEV_MUX | DEV_DISABLE | DEV_DIS
    };

/* DCSL data structures

   dcsl_dev     DCSL device descriptor
   dcsl_unit    DCSL unit descriptor
   dcsl_reg     DCSL register list
   dcsl_mod     DCSL modifiers list
*/

UNIT dcsl_unit[] = {
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT },
    { UDATA (&dcso_svc, TT_MODE_UC, 0), DCSL_WAIT }
    };

MTAB dcsl_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", NULL },
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
    { MTAB_XTD|MTAB_VUN, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &dcs_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, &dcs_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, &dcs_desc },
    { 0 }
    };

REG dcsl_reg[] = {
    { URDATAD (TIME, dcsl_unit[0].wait, 10, 24, 0,
              DCS_LINES, REG_NZ + PV_LEFT, "time from I/O initiation to interrupt, lines 0 to 31") },
    { NULL }
    };

DEVICE dcsl_dev = {
    "DCSL", dcsl_unit, dcsl_reg, dcsl_mod,
    DCS_LINES, 10, 31, 1, 8, 8,
    NULL, NULL, &dcs_reset,
    NULL, NULL, NULL,
    NULL, DEV_DIS | DEV_MUX
    };

/* DCS IOT routine */

int32 dcs (int32 inst, int32 dev, int32 dat)
{
int32 pls = (inst >> 6) & 077;

if (dcs_dev.flags & DEV_DIS)                            /* disabled? */
    return (stop_inst << IOT_V_REASON) | dat;           /* illegal inst */
if (pls & 020)                                          /* pulse 20? clr IO */
    dat = 0;

switch (pls & 057) {                                    /* case IR<6,8:11> */

    case 000:                                           /* RCH */
        dat |= dcs_buf[dcs_scan];                       /* return line buf */
        dcs_flg[dcs_scan] = 0;                          /* clr line flag */
        break;

    case 001:                                           /* RRC */
        dat |= dcs_scan;                                /* return line num */
        break;

    case 010:                                           /* RCC */
        dat |= dcs_buf[dcs_scan];                       /* return line buf */
        dcs_flg[dcs_scan] = 0;                          /* clr line flag */
                                                        /* fall through */
    case 011:                                           /* RSC */
        dcs_scan_next (TRUE);                           /* unlock scanner */
        break;

    case 040:                                           /* TCB */
        dcs_buf[dcs_send] = dat & 0377;                 /* load buffer */
        dcs_flg[dcs_send] = 0;                          /* clr line flag */
        sim_activate (&dcsl_unit[dcs_send], dcsl_unit[dcs_send].wait);
        break;

    case 041:                                           /* SSB */
        dcs_send = dat & DCS_LINE_MASK;                 /* load line num */
        break;

    case 050:                                           /* TCC */
        dcs_buf[dcs_scan] = dat & 0377;                 /* load buffer */
        dcs_flg[dcs_scan] = 0;                          /* clr line flag */
        sim_activate (&dcsl_unit[dcs_scan], dcsl_unit[dcs_scan].wait);
        dcs_scan_next (TRUE);                           /* unlock scanner */
        break;

    default:
        return (stop_inst << IOT_V_REASON) | dat;       /* illegal inst */
        }                                               /* end case */

return dat;
}

/* Unit service - receive side

   Poll all active lines for input
   Poll for new connections
*/

t_stat dcsi_svc (UNIT *uptr)
{
int32 ln, c, out;

if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return SCPE_OK;
if (dcs_dev.flags & DEV_DIS)
    return SCPE_OK;
sim_activate (uptr, tmxr_poll);                         /* continue poll */
ln = tmxr_poll_conn (&dcs_desc);                        /* look for connect */
if (ln >= 0) {                                          /* got one? */
    dcs_ldsc[ln].rcve = 1;                              /* set rcv enable */
    }
tmxr_poll_rx (&dcs_desc);                               /* poll for input */
for (ln = 0; ln < DCS_NUMLIN; ln++) {                   /* loop thru lines */
    if (dcs_ldsc[ln].conn) {                            /* connected? */
        if ((c = tmxr_getc_ln (&dcs_ldsc[ln]))) {       /* get char */
            if (c & SCPE_BREAK)                         /* break? */
                c = 0;
            else c = sim_tt_inpcvt (c, TT_GET_MODE (dcsl_unit[ln].flags)|TTUF_KSR);
            dcs_buf[ln] = c;                            /* save char */
            dcs_flg[ln] = 1;                            /* set line flag */
            dcs_scan_next (FALSE);                      /* kick scanner */
            out = sim_tt_outcvt (c & 0177, TT_GET_MODE (dcsl_unit[ln].flags));
            if (out >= 0) {
                tmxr_putc_ln (&dcs_ldsc[ln], out);      /* echo char */
                tmxr_poll_tx (&dcs_desc);               /* poll xmt */
                }
            }
        }
    else dcs_ldsc[ln].rcve = 0;                         /* disconnected */
    }                                                   /* end for */
return SCPE_OK;
}

/* Unit service - transmit side */

t_stat dcso_svc (UNIT *uptr)
{
int32 c;
uint32 ln = uptr - dcsl_unit;                           /* line # */

if (dcs_dev.flags & DEV_DIS)
    return SCPE_OK;
if (dcs_ldsc[ln].conn) {                                /* connected? */
    if (dcs_ldsc[ln].xmte) {                            /* xmt enabled? */
        c = sim_tt_outcvt (dcs_buf[ln] & 0177, TT_GET_MODE (uptr->flags));
        if (c >= 0)                                     /* output char */
            tmxr_putc_ln (&dcs_ldsc[ln], c);
        tmxr_poll_tx (&dcs_desc);                       /* poll xmt */
        }
    else {                                              /* buf full */
        tmxr_poll_tx (&dcs_desc);                       /* poll xmt */
        sim_activate (uptr, uptr->wait);                /* reschedule */
        return SCPE_OK;
        }
    }
dcs_flg[ln] = 1;                                        /* set line flag */
dcs_scan_next (FALSE);                                  /* kick scanner */
return SCPE_OK;
}

/* Kick scanner */

void dcs_scan_next (t_bool unlk)
{
int32 i;

if (unlk)                                               /* unlock? */
    iosta &= ~IOS_DCS;
else if (iosta & IOS_DCS)                               /* no, locked? */
    return;
for (i = 0; i < DCS_LINES; i++) {                       /* scan flags */
    dcs_scan = (dcs_scan + 1) & DCS_LINE_MASK;          /* next flag */
    if (dcs_flg[dcs_scan] != 0) {                       /* flag set? */
        iosta |= IOS_DCS;                               /* lock scanner */
        dev_req_int (dcs_sbs);                          /* request intr */
        return;
        }
    }
return;
}

/* Reset routine */

t_stat dcs_reset (DEVICE *dptr)
{
int32 i;

if (dcs_dev.flags & DEV_DIS)                            /* master disabled? */
    dcsl_dev.flags = dcsl_dev.flags | DEV_DIS;          /* disable lines */
else dcsl_dev.flags = dcsl_dev.flags & ~DEV_DIS;
if (dcs_unit.flags & UNIT_ATT)                          /* master att? */
    sim_activate_abs (&dcs_unit, tmxr_poll);            /* activate */
else sim_cancel (&dcs_unit);                            /* else stop */
for (i = 0; i < DCS_LINES; i++)                         /* reset lines */
    dcs_reset_ln (i);
dcs_send = 0;
dcs_scan = 0;
iosta &= ~IOS_DCS;                                      /* clr intr req */
return SCPE_OK;
}

/* Attach master unit */

t_stat dcs_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

r = tmxr_attach (&dcs_desc, uptr, cptr);                /* attach */
if (r != SCPE_OK)                                       /* error */
    return r;
sim_activate_abs (uptr, 0);                             /* start poll at once */
return SCPE_OK;
}

/* Detach master unit */

t_stat dcs_detach (UNIT *uptr)
{
int32 i;
t_stat r;

r = tmxr_detach (&dcs_desc, uptr);                      /* detach */
for (i = 0; i < DCS_LINES; i++)                         /* disable rcv */
    dcs_ldsc[i].rcve = 0;
sim_cancel (uptr);                                      /* stop poll */
return r;
}

/* Change number of lines */

t_stat dcs_vlines (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 newln, i, t;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
newln = get_uint (cptr, 10, DCS_LINES, &r);
if ((r != SCPE_OK) || (newln == DCS_NUMLIN))
    return r;
if (newln == 0)
    return SCPE_ARG;
if (newln < DCS_LINES) {
    for (i = newln, t = 0; i < DCS_NUMLIN; i++)
        t = t | dcs_ldsc[i].conn;
    if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
            return SCPE_OK;
    for (i = newln; i < DCS_NUMLIN; i++) {
        if (dcs_ldsc[i].conn) {
            tmxr_linemsg (&dcs_ldsc[i], "\r\nOperator disconnected line\r\n");
            tmxr_reset_ln (&dcs_ldsc[i]);               /* reset line */
            }
        dcsl_unit[i].flags = dcsl_unit[i].flags | UNIT_DIS;
        dcs_reset_ln (i);
        }
    }
else {
    for (i = DCS_NUMLIN; i < newln; i++) {
        dcsl_unit[i].flags = dcsl_unit[i].flags & ~UNIT_DIS;
        dcs_reset_ln (i);
        }
    }
DCS_NUMLIN = newln;
return SCPE_OK;
}

/* Reset an individual line */

void dcs_reset_ln (int32 ln)
{
sim_cancel (&dcsl_unit[ln]);
dcs_buf[ln] = 0;
dcs_flg[ln] = 0;
return;
}
