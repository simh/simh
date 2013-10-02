/* pdp11_dl.c: PDP-11 multiple terminal interface simulator

   Copyright (c) 1993-2012, Robert M Supnik

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

   dli,dlo      DL11 terminal input/output

   18-Apr-2012  RMS     Modified to use clock coscheduling
   17-Aug-2011  RMS     Added AUTOCONFIGURE modifier
   19-Nov-2008  RMS     Revised for common TMXR show routines
                        Revised to autoconfigure vectors
   20-May-2008  RMS     Added modem control support
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#error "DL11 is not supported on the PDP-10!"

#elif defined (VM_VAX)                                  /* VAX version */
#error "DL11 is not supported on the VAX!"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif
#include "sim_sock.h"
#include "sim_tmxr.h"

#define DLX_MAXMUX      (dlx_desc.lines - 1)
#define DLI_RCI         0                               /* rcv ints */
#define DLI_DSI         1                               /* dset ints */

/* Modem control */

#define DLX_V_MDM       (TTUF_V_UF + 0)
#define DLX_MDM         (1u << DLX_V_MDM)

/* registers */

#define DLICSR_DSI      0100000                         /* dataset int, RO */
#define DLICSR_RNG      0040000                         /* ring, RO */
#define DLICSR_CTS      0020000                         /* CTS, RO */
#define DLICSR_CDT      0010000                         /* CDT, RO */
#define DLICSR_SEC      0002000                         /* sec rcv, RONI */
#define DLICSR_DSIE     0000040                         /* DSI ie, RW */
#define DLICSR_SECX     0000010                         /* sec xmt, RWNI */
#define DLICSR_RTS      0000004                         /* RTS, RW */
#define DLICSR_DTR      0000002                         /* DTR, RW */
#define DLICSR_RD       (CSR_DONE|CSR_IE)               /* DL11C */
#define DLICSR_WR       (CSR_IE)
#define DLICSR_RD_M     (DLICSR_DSI|DLICSR_RNG|DLICSR_CTS|DLICSR_CDT|DLICSR_SEC| \
                         CSR_DONE|CSR_IE|DLICSR_DSIE|DLICSR_SECX|DLICSR_RTS|DLICSR_DTR)
#define DLICSR_WR_M     (CSR_IE|DLICSR_DSIE|DLICSR_SECX|DLICSR_RTS|DLICSR_DTR)
#define DLIBUF_ERR      0100000
#define DLIBUF_OVR      0040000
#define DLIBUF_RBRK     0020000
#define DLIBUF_RD       (DLIBUF_ERR|DLIBUF_OVR|DLIBUF_RBRK|0377)
#define DLOCSR_MNT      0000004                         /* maint, RWNI */
#define DLOCSR_XBR      0000001                         /* xmit brk, RWNI */
#define DLOCSR_RD       (CSR_DONE|CSR_IE|DLOCSR_MNT|DLOCSR_XBR)
#define DLOCSR_WR       (CSR_IE|DLOCSR_MNT|DLOCSR_XBR)

extern int32 int_req[IPL_HLVL];
extern int32 tmxr_poll;

uint16 dli_csr[DLX_LINES] = { 0 };                      /* control/status */
uint16 dli_buf[DLX_LINES] = { 0 };
uint32 dli_ireq[2] = { 0, 0};
uint16 dlo_csr[DLX_LINES] = { 0 };                      /* control/status */
uint8 dlo_buf[DLX_LINES] = { 0 };
uint32 dlo_ireq = 0;
TMLN dlx_ldsc[DLX_LINES] = { {0} };                     /* line descriptors */
TMXR dlx_desc = { DLX_LINES, 0, 0, dlx_ldsc };          /* mux descriptor */

t_stat dlx_rd (int32 *data, int32 PA, int32 access);
t_stat dlx_wr (int32 data, int32 PA, int32 access);
t_stat dlx_reset (DEVICE *dptr);
t_stat dli_svc (UNIT *uptr);
t_stat dlo_svc (UNIT *uptr);
t_stat dlx_attach (UNIT *uptr, char *cptr);
t_stat dlx_detach (UNIT *uptr);
t_stat dlx_set_lines (UNIT *uptr, int32 val, char *cptr, void *desc);
void dlx_enbdis (int32 dis);
void dli_clr_int (int32 ln, uint32 wd);
void dli_set_int (int32 ln, uint32 wd);
int32 dli_iack (void);
void dlo_clr_int (int32 ln);
void dlo_set_int (int32 ln);
int32 dlo_iack (void);
void dlx_reset_ln (int32 ln);

/* DLI data structures

   dli_dev      DLI device descriptor
   dli_unit     DLI unit descriptor
   dli_reg      DLI register list
*/

#define IOLN_DL         010

DIB dli_dib = {
    IOBA_AUTO, IOLN_DL * DLX_LINES, &dlx_rd, &dlx_wr,
    2, IVCL (DLI), VEC_AUTO, { &dli_iack, &dlo_iack }, IOLN_DL,
    };

UNIT dli_unit = { UDATA (&dli_svc, 0, 0), KBD_POLL_WAIT };

REG dli_reg[] = {
    { BRDATA (BUF, dli_buf, DEV_RDX, 16, DLX_LINES) },
    { BRDATA (CSR, dli_csr, DEV_RDX, 16, DLX_LINES) },
    { GRDATA (IREQ, dli_ireq[DLI_RCI], DEV_RDX, DLX_LINES, 0) },
    { GRDATA (DSI, dli_ireq[DLI_DSI], DEV_RDX, DLX_LINES, 0) },
    { DRDATA (LINES, dlx_desc.lines, 6), REG_HRO },
    { GRDATA (DEVADDR, dli_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVIOLN, dli_dib.lnt, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVVEC, dli_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

MTAB dli_mod[] = {
    { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &dlx_desc },
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &dlx_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *) &dlx_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *) &dlx_desc },
    { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,
      &set_addr, &show_addr, NULL },
    { MTAB_XTD | MTAB_VDV, 0, NULL, "AUTOCONFIGURE",
      &set_addr_flt, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 1, "VECTOR", NULL,
      &set_vec, &show_vec_mux, (void *) &dlx_desc },
    { MTAB_XTD | MTAB_VDV, 0, "LINES", "LINES",
      &dlx_set_lines, &tmxr_show_lines, (void *) &dlx_desc },
    { 0 }
    };

DEVICE dli_dev = {
    "DLI", &dli_unit, dli_reg, dli_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &dlx_reset,
    NULL, &dlx_attach, &dlx_detach,
    &dli_dib, DEV_UBUS | DEV_QBUS | DEV_DISABLE | DEV_DIS | DEV_MUX
    };

/* DLO data structures

   dlo_dev      DLO device descriptor
   dlo_unit     DLO unit descriptor
   dlo_reg      DLO register list
*/

UNIT dlo_unit[] = {
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT },
    { UDATA (&dlo_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT }
    };

REG dlo_reg[] = {
    { BRDATA (BUF, dlo_buf, DEV_RDX, 8, DLX_LINES) },
    { BRDATA (CSR, dlo_csr, DEV_RDX, 16, DLX_LINES) },
    { GRDATA (IREQ, dlo_ireq, DEV_RDX, DLX_LINES, 0) },
    { URDATA (TIME, dlo_unit[0].wait, 10, 31, 0,
              DLX_LINES, PV_LEFT) },
    { NULL }
    };

MTAB dlo_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", NULL },
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
    { DLX_MDM, 0,       "no dataset", "NODATASET", NULL },
    { DLX_MDM, DLX_MDM, "dataset",    "DATASET",   NULL },
    { MTAB_XTD|MTAB_VUN, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &dlx_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, &dlx_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, &dlx_desc },
    { 0 }
    };

DEVICE dlo_dev = {
    "DLO", dlo_unit, dlo_reg, dlo_mod,
    DLX_LINES, 10, 31, 1, 8, 8,
    NULL, NULL, &dlx_reset,
    NULL, NULL, NULL,
    NULL, DEV_UBUS | DEV_QBUS | DEV_DISABLE | DEV_DIS
    };

/* Terminal input routines */

t_stat dlx_rd (int32 *data, int32 PA, int32 access)
{
int32 ln = ((PA - dli_dib.ba) >> 3);

if (ln > DLX_MAXMUX)                                    /* validate line number */
    return SCPE_IERR;

switch ((PA >> 1) & 03) {                               /* decode PA<2:1> */

    case 00:                                            /* tti csr */
        *data = dli_csr[ln] &
            ((dlo_unit[ln].flags & DLX_MDM)? DLICSR_RD_M: DLICSR_RD);
        dli_csr[ln] &= ~DLICSR_DSI;                     /* clr DSI flag */
        dli_clr_int (ln, DLI_DSI);                      /* clr dset int req */
        return SCPE_OK;

    case 01:                                            /* tti buf */
        *data = dli_buf[ln] & DLIBUF_RD;
        dli_csr[ln] &= ~CSR_DONE;                       /* clr rcv done */
        dli_clr_int (ln, DLI_RCI);                      /* clr rcv int req */
        return SCPE_OK;

    case 02:                                            /* tto csr */
        *data = dlo_csr[ln] & DLOCSR_RD;
        return SCPE_OK;

    case 03:                                            /* tto buf */
        *data = dlo_buf[ln];
        return SCPE_OK;
        }                                               /* end switch PA */

return SCPE_NXM;
}

t_stat dlx_wr (int32 data, int32 PA, int32 access)
{
int32 ln = ((PA - dli_dib.ba) >> 3);
TMLN *lp = &dlx_ldsc[ln];

if (ln > DLX_MAXMUX)                                    /* validate line number */
    return SCPE_IERR;

switch ((PA >> 1) & 03) {                               /* decode PA<2:1> */

    case 00:                                            /* tti csr */
        if (PA & 1)                                     /* odd byte RO */
            return SCPE_OK;
        if ((data & CSR_IE) == 0)
            dli_clr_int (ln, DLI_RCI);
        else if ((dli_csr[ln] & (CSR_DONE + CSR_IE)) == CSR_DONE)
            dli_set_int (ln, DLI_RCI);
        if (dlo_unit[ln].flags & DLX_MDM) {             /* modem control */
            if ((data & DLICSR_DSIE) == 0)
                dli_clr_int (ln, DLI_DSI);
            else if ((dli_csr[ln] & (DLICSR_DSI|DLICSR_DSIE)) == DLICSR_DSI)
                dli_set_int (ln, DLI_DSI);
            if ((data ^ dli_csr[ln]) & DLICSR_DTR) {    /* DTR change? */
                if ((data & DLICSR_DTR) && lp->conn) {  /* setting DTR? */
                    dli_csr[ln] = (dli_csr[ln] & ~DLICSR_RNG) |
                        (DLICSR_CDT|DLICSR_CTS|DLICSR_DSI);
                    if (data & DLICSR_DSIE)             /* if ie, req int */
                        dli_set_int (ln, DLI_DSI);
                    }                                   /* end DTR 0->1 + ring */
                else {                                  /* clearing DTR */
                    if (lp->conn) {                     /* connected? */
                        tmxr_linemsg (lp, "\r\nLine hangup\r\n");
                        tmxr_reset_ln (lp);             /* reset line */
                        if (dli_csr[ln] & DLICSR_CDT) { /* carrier det? */
                            dli_csr[ln] |= DLICSR_DSI;
                            if (data & DLICSR_DSIE)     /* if ie, req int */
                                dli_set_int (ln, DLI_DSI);
                            }
                        }
                    dli_csr[ln] &= ~(DLICSR_CDT|DLICSR_RNG|DLICSR_CTS);
                                                        /* clr CDT,RNG,CTS */
                    }                                   /* end DTR 1->0 */
                }                                       /* end DTR chg */
            dli_csr[ln] = (uint16) ((dli_csr[ln] & ~DLICSR_WR_M) | (data & DLICSR_WR_M));
            }                                           /* end modem */
        dli_csr[ln] = (uint16) ((dli_csr[ln] & ~DLICSR_WR) | (data & DLICSR_WR));
        return SCPE_OK;

    case 01:                                            /* tti buf */
        return SCPE_OK;

    case 02:                                            /* tto csr */
        if (PA & 1)
            return SCPE_OK;
        if ((data & CSR_IE) == 0)
            dlo_clr_int (ln);
        else if ((dlo_csr[ln] & (CSR_DONE + CSR_IE)) == CSR_DONE)
            dlo_set_int (ln);
        dlo_csr[ln] = (uint16) ((dlo_csr[ln] & ~DLOCSR_WR) | (data & DLOCSR_WR));
        return SCPE_OK;

    case 03:                                            /* tto buf */
        if ((PA & 1) == 0)
            dlo_buf[ln] = data & 0377;
        dlo_csr[ln] &= ~CSR_DONE;
        dlo_clr_int (ln);
        sim_activate (&dlo_unit[ln], dlo_unit[ln].wait);
        return SCPE_OK;
        }                                               /* end switch PA */

return SCPE_NXM;
}

/* Terminal input service */

t_stat dli_svc (UNIT *uptr)
{
int32 ln, c, temp;

if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return SCPE_OK;
sim_clock_coschedule (uptr, tmxr_poll);                 /* continue poll */
ln = tmxr_poll_conn (&dlx_desc);                        /* look for connect */
if (ln >= 0) {                                          /* got one? rcv enb */
    dlx_ldsc[ln].rcve = 1;
    if (dlo_unit[ln].flags & DLX_MDM) {                 /* modem control? */
        if (dli_csr[ln] & DLICSR_DTR)                   /* DTR already set? */
            dli_csr[ln] |= (DLICSR_CDT|DLICSR_CTS|DLICSR_DSI);
        else dli_csr[ln] |= (DLICSR_RNG|DLICSR_DSI);    /* no, ring */
        if (dli_csr[ln] & DLICSR_DSIE)                  /* if ie, */
            dli_set_int (ln, DLI_DSI);                  /* req int */
        }                                               /* end modem */
    }                                                   /* end new conn */
tmxr_poll_rx (&dlx_desc);                               /* poll for input */
for (ln = 0; ln < DLX_LINES; ln++) {                    /* loop thru lines */
    if (dlx_ldsc[ln].conn) {                            /* connected? */
        if ((temp = tmxr_getc_ln (&dlx_ldsc[ln]))) {    /* get char */
            if (temp & SCPE_BREAK)                      /* break? */
                c = DLIBUF_ERR|DLIBUF_RBRK;
            else c = sim_tt_inpcvt (temp, TT_GET_MODE (dlo_unit[ln].flags));
            if (dli_csr[ln] & CSR_DONE)
                c |= DLIBUF_ERR|DLIBUF_OVR;
            else dli_csr[ln] |= CSR_DONE;
            if (dli_csr[ln] & CSR_IE)
                dli_set_int (ln, DLI_RCI);
            dli_buf[ln] = c;
            }
        }
    else if (dlo_unit[ln].flags & DLX_MDM) {            /* discpnn & modem? */
        if (dli_csr[ln] & DLICSR_CDT) {                 /* carrier detect? */
            dli_csr[ln] |= DLICSR_DSI;                  /* dataset change */
            if (dli_csr[ln] & DLICSR_DSIE)              /* if ie, */
                dli_set_int (ln, DLI_DSI);              /* req int */
            }
        dli_csr[ln] &= ~(DLICSR_CDT|DLICSR_RNG|DLICSR_CTS);
                                                        /* clr CDT,RNG,CTS */
        }
    }
return SCPE_OK;
}

/* Terminal output service */

t_stat dlo_svc (UNIT *uptr)
{
int32 c;
int32 ln = uptr - dlo_unit;                             /* line # */

if (dlx_ldsc[ln].conn) {                                /* connected? */
    if (dlx_ldsc[ln].xmte) {                            /* tx enabled? */
        TMLN *lp = &dlx_ldsc[ln];                       /* get line */
        c = sim_tt_outcvt (dlo_buf[ln], TT_GET_MODE (dlo_unit[ln].flags));
        if (c >= 0)                                     /* output char */
            tmxr_putc_ln (lp, c);
        tmxr_poll_tx (&dlx_desc);                       /* poll xmt */
        }
    else {
        tmxr_poll_tx (&dlx_desc);                       /* poll xmt */
        sim_activate (uptr, dlo_unit[ln].wait);         /* wait */
        return SCPE_OK;
        }
    }
dlo_csr[ln] |= CSR_DONE;                                /* set done */
if (dlo_csr[ln] & CSR_IE)
    dlo_set_int (ln);
return SCPE_OK;
}

/* Interrupt routines */

void dli_clr_int (int32 ln, uint32 wd)
{
dli_ireq[wd] &= ~(1 << ln);                             /* clr rcv/dset int */
if ((dli_ireq[DLI_RCI] | dli_ireq[DLI_DSI]) == 0)       /* all clr? */
    CLR_INT (DLI);                                      /* all clr? */
else SET_INT (DLI);                                     /* no, set intr */
return;
}

void dli_set_int (int32 ln, uint32 wd)
{
dli_ireq[wd] |= (1 << ln);                              /* set rcv/dset int */
SET_INT (DLI);                                          /* set master intr */
return;
}

int32 dli_iack (void)
{
int32 ln;

for (ln = 0; ln < DLX_LINES; ln++) {                    /* find 1st line */
    if ((dli_ireq[DLI_RCI] | dli_ireq[DLI_DSI]) & (1 << ln)) {
        dli_clr_int (ln, DLI_RCI);                      /* clr both req */
        dli_clr_int (ln, DLI_DSI);
        return (dli_dib.vec + (ln * 010));              /* return vector */
        }
    }
return 0;
}

void dlo_clr_int (int32 ln)
{
dlo_ireq &= ~(1 << ln);                                 /* clr xmit int */
if (dlo_ireq == 0)                                      /* all clr? */
    CLR_INT (DLO);
else SET_INT (DLO);                                     /* no, set intr */
return;
}

void dlo_set_int (int32 ln)
{
dlo_ireq |= (1 << ln);                                  /* set xmit int */
SET_INT (DLO);                                          /* set master intr */
return;
}

int32 dlo_iack (void)
{
int32 ln;

for (ln = 0; ln < DLX_LINES; ln++) {                    /* find 1st line */
    if (dlo_ireq & (1 << ln)) {
        dlo_clr_int (ln);                               /* clear intr */
        return (dli_dib.vec + (ln * 010) + 4);          /* return vector */
        }
    }
return 0;
}

/* Reset */

t_stat dlx_reset (DEVICE *dptr)
{
int32 ln;

dlx_enbdis (dptr->flags & DEV_DIS);                     /* sync enables */
sim_cancel (&dli_unit);                                 /* assume stop */
if (dli_unit.flags & UNIT_ATT)                          /* if attached, */
    sim_activate (&dli_unit, tmxr_poll);                /* activate */
for (ln = 0; ln < DLX_LINES; ln++)                      /* for all lines */
    dlx_reset_ln (ln);
return auto_config (dli_dev.name, dlx_desc.lines);      /* auto config */
}

/* Reset individual line */

void dlx_reset_ln (int32 ln)
{
dli_buf[ln] = 0;                                        /* clear buf */
if (dlo_unit[ln].flags & DLX_MDM)                       /* modem */
    dli_csr[ln] &= DLICSR_DTR;                          /* dont clr DTR */
else dli_csr[ln] = 0;
dlo_buf[ln] = 0;                                        /* clear buf */
dlo_csr[ln] = CSR_DONE;
sim_cancel (&dlo_unit[ln]);                             /* deactivate */
dli_clr_int (ln, DLI_RCI);
dli_clr_int (ln, DLI_DSI);
dlo_clr_int (ln);
return;
}

/* Attach master unit */

t_stat dlx_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = tmxr_attach (&dlx_desc, uptr, cptr);                /* attach */
if (r != SCPE_OK)                                       /* error */
    return r;
sim_activate (uptr, tmxr_poll);                         /* start poll */
return SCPE_OK;
}

/* Detach master unit */

t_stat dlx_detach (UNIT *uptr)
{
int32 i;
t_stat r;

r = tmxr_detach (&dlx_desc, uptr);                      /* detach */
for (i = 0; i < DLX_LINES; i++)                         /* all lines, */
    dlx_ldsc[i].rcve = 0;                               /* disable rcv */
sim_cancel (uptr);                                      /* stop poll */
return r;
}

/* Enable/disable device */

void dlx_enbdis (int32 dis)
{
if (dis) {
    dli_dev.flags = dli_dev.flags | DEV_DIS;
    dlo_dev.flags = dlo_dev.flags | DEV_DIS;
    }
else {
    dli_dev.flags = dli_dev.flags & ~DEV_DIS;
    dlo_dev.flags = dlo_dev.flags & ~DEV_DIS;
    }
return;
}

/* Change number of lines */

t_stat dlx_set_lines (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 newln, i, t;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
newln = get_uint (cptr, 10, DLX_LINES, &r);
if ((r != SCPE_OK) || (newln == dlx_desc.lines))
    return r;
if (newln == 0)
    return SCPE_ARG;
if (newln < dlx_desc.lines) {
    for (i = newln, t = 0; i < dlx_desc.lines; i++)
        t = t | dlx_ldsc[i].conn;
    if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
        return SCPE_OK;
    for (i = newln; i < dlx_desc.lines; i++) {
        if (dlx_ldsc[i].conn) {
            tmxr_linemsg (&dlx_ldsc[i], "\r\nOperator disconnected line\r\n");
            tmxr_reset_ln (&dlx_ldsc[i]);               /* reset line */
            }
        dlo_unit[i].flags |= UNIT_DIS;
        dlx_reset_ln (i);
        }
    }
else {
    for (i = dlx_desc.lines; i < newln; i++) {
        dlo_unit[i].flags &= ~UNIT_DIS;
        dlx_reset_ln (i);
        }
    }
dlx_desc.lines = newln;
dli_dib.lnt = newln * 010;                             /* upd IO page lnt */
return auto_config (dli_dev.name, newln);              /* auto config */
}
