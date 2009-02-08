/* pdp11_dc.c: PDP-11 DC11 multiple terminal interface simulator

   Copyright (c) 1993-2008, Robert M Supnik

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

   dci,dco    DC11 terminal input/output

   19-Nov-2008  RMS     Revised for common TMXR show routines
                        Revised to autoconfigure vectors

   The simulator supports both hardwired and modem-like behavior.  If modem
   control is not enabled, carrier detect, ring, and carrier change are
   never set.
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#error "DC11 is not supported on the PDP-10!"

#elif defined (VM_VAX)                                  /* VAX version */
#error "DC11 is not supported on the VAX!"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif
#include "sim_sock.h"
#include "sim_tmxr.h"

#define DCX_MASK        (DCX_LINES - 1)

/* Parity and modem control */

#define DCX_V_OPAR      (TTUF_V_UF + 0)
#define DCX_V_EPAR      (TTUF_V_UF + 1)
#define DCX_V_MDM       (TTUF_V_UF + 2)
#define DCX_OPAR        (1u << DCX_V_OPAR)
#define DCX_EPAR        (1u << DCX_V_EPAR)
#define DCX_MDM         (1u << DCX_V_MDM)

/* registers */

#define DCICSR_RD       0173777
#define DCICSR_WR       0003533
#define DCICSR_DTR      0000001                         /* DTR (RW) */
#define DCICSR_XBR      0000002                         /* xmit brk (RWNI) */
#define DCICSR_CDT      0000004                         /* car det (RO) */
#define DCICSR_PAR      0000040                         /* odd par (RO) */
#define DCICSR_OVR      0010000                         /* overrun (RO) */
#define DCICSR_RNG      0020000                         /* ring (RO) */
#define DCICSR_CCH      0040000                         /* car change (RO) */
#define DCICSR_ALLERR  (DCICSR_OVR|DCICSR_RNG|DCICSR_CCH)
#define DCICSR_ERR      0100000                         /* error */
#define DCOCSR_RD       0100737
#define DCOCSR_WR       0000535
#define DCOCSR_RTS      0000001                         /* req to send (RW) */
#define DCOCSR_CTS      0000002                         /* clr to send (RO) */
#define DCOCSR_MNT      0000004                         /* maint (RWNI) */

extern int32 int_req[IPL_HLVL];
extern int32 tmxr_poll;

uint16 dci_csr[DCX_LINES] = { 0 };                      /* control/status */
uint8 dci_buf[DCX_LINES] = { 0 };
uint32 dci_ireq = 0;
uint16 dco_csr[DCX_LINES] = { 0 };                      /* control/status */
uint8 dco_buf[DCX_LINES] = { 0 };
uint32 dco_ireq = 0;
TMLN dcx_ldsc[DCX_LINES] = { 0 };                       /* line descriptors */
TMXR dcx_desc = { DCX_LINES, 0, 0, dcx_ldsc };          /* mux descriptor */

static const uint8 odd_par[] = {
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* 00 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* 10 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* 20 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* 30 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* 40 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* 50 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* 60 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* 70 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* 80 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* 90 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* A0 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* B0 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* C0 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* D0 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80,                 /* E0 */
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,
    0x80, 0, 0, 0x80, 0, 0x80, 0x80, 0,                 /* F0 */
    0, 0x80, 0x80, 0, 0x80, 0, 0, 0x80
    };

t_stat dcx_rd (int32 *data, int32 PA, int32 access);
t_stat dcx_wr (int32 data, int32 PA, int32 access);
t_stat dcx_reset (DEVICE *dptr);
t_stat dci_svc (UNIT *uptr);
t_stat dco_svc (UNIT *uptr);
t_stat dcx_attach (UNIT *uptr, char *cptr);
t_stat dcx_detach (UNIT *uptr);
t_stat dcx_set_lines (UNIT *uptr, int32 val, char *cptr, void *desc);
void dcx_enbdis (int32 dis);
void dci_clr_int (int32 ln);
void dci_set_int (int32 ln);
int32 dci_iack (void);
void dco_clr_int (int32 ln);
void dco_set_int (int32 ln);
int32 dco_iack (void);
void dcx_reset_ln (int32 ln);

/* DCI data structures

   dci_dev      DCI device descriptor
   dci_unit     DCI unit descriptor
   dci_reg      DCI register list
*/

DIB dci_dib = {
    IOBA_DC, IOLN_DC, &dcx_rd, &dcx_wr,
    2, IVCL (DCI), VEC_DCI, { &dci_iack, &dco_iack }
    };

UNIT dci_unit = { UDATA (&dci_svc, 0, 0), KBD_POLL_WAIT };

REG dci_reg[] = {
    { BRDATA (BUF, dci_buf, DEV_RDX, 8, DCX_LINES) },
    { BRDATA (CSR, dci_csr, DEV_RDX, 16, DCX_LINES) },
    { GRDATA (IREQ, dci_ireq, DEV_RDX, DCX_LINES, 0) },
    { DRDATA (LINES, dcx_desc.lines, 6), REG_HRO },
    { GRDATA (DEVADDR, dci_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVIOLN, dci_dib.lnt, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVVEC, dci_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

MTAB dci_mod[] = {
    { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &dcx_desc },
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &dcx_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *) &dcx_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *) &dcx_desc },
    { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,
      &set_addr, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 1, "VECTOR", NULL,
      &set_vec, &show_vec_mux, (void *) &dcx_desc },
    { MTAB_XTD | MTAB_VDV, 0, "LINES", "LINES",
      &dcx_set_lines, &tmxr_show_lines, (void *) &dcx_desc },
    { 0 }
    };

DEVICE dci_dev = {
    "DCI", &dci_unit, dci_reg, dci_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &dcx_reset,
    NULL, &dcx_attach, &dcx_detach,
    &dci_dib, DEV_FLTA | DEV_UBUS | DEV_QBUS | DEV_DISABLE | DEV_DIS
    };

/* DCO data structures

   dco_dev      DCO device descriptor
   dco_unit     DCO unit descriptor
   dco_reg      DCO register list
*/

UNIT dco_unit[] = {
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT },
    { UDATA (&dco_svc, TT_MODE_7P+DCX_EPAR+DCX_MDM, 0), SERIAL_OUT_WAIT }
    };

REG dco_reg[] = {
    { BRDATA (BUF, dco_buf, DEV_RDX, 8, DCX_LINES) },
    { BRDATA (CSR, dco_csr, DEV_RDX, 16, DCX_LINES) },
    { GRDATA (IREQ, dco_ireq, DEV_RDX, DCX_LINES, 0) },
    { URDATA (TIME, dco_unit[0].wait, 10, 31, 0,
              DCX_LINES, PV_LEFT) },
    { NULL }
    };

MTAB dco_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", NULL },
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
    { DCX_OPAR+DCX_EPAR, 0,        "no parity",   "NOPARITY",   NULL },
    { DCX_OPAR+DCX_EPAR, DCX_OPAR, "odd parity",  "ODDPARITY",  NULL },
    { DCX_OPAR+DCX_EPAR, DCX_EPAR, "even parity", "EVENPARITY", NULL },
    { DCX_MDM, 0,       "no dataset", "NODATASET", NULL },
    { DCX_MDM, DCX_MDM, "dataset",    "DATASET",   NULL },
    { MTAB_XTD|MTAB_VUN, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &dcx_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, &dcx_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, &dcx_desc },
    { 0 }
    };

DEVICE dco_dev = {
    "DCO", dco_unit, dco_reg, dco_mod,
    DCX_LINES, 10, 31, 1, 8, 8,
    NULL, NULL, &dcx_reset,
    NULL, NULL, NULL,
    NULL, DEV_UBUS | DEV_DISABLE | DEV_DIS
    };

/* Terminal input routines */

t_stat dcx_rd (int32 *data, int32 PA, int32 access)
{
int32 ln = ((PA - dci_dib.ba) >> 3) & DCX_MASK;

switch ((PA >> 1) & 03) {                               /* decode PA<2:1> */

    case 00:                                            /* dci csr */
	if (dci_csr[ln] & DCICSR_ALLERR)
            dci_csr[ln] |= DCICSR_ERR;
        else dci_csr[ln] &= ~DCICSR_ERR;
        *data = dci_csr[ln] & DCICSR_RD;
        dci_csr[ln] &= ~(CSR_DONE|DCICSR_ALLERR|DCICSR_ERR);
        return SCPE_OK;

    case 01:                                            /* dci buf */
        dci_clr_int (ln);
        *data = dci_buf[ln];
        return SCPE_OK;

    case 02:                                            /* dco csr */
        *data = dco_csr[ln] & DCOCSR_RD;
        return SCPE_OK;

    case 03:                                            /* dco buf */
        *data = dco_buf[ln];
        return SCPE_OK;
        }                                               /* end switch PA */

return SCPE_NXM;
}

t_stat dcx_wr (int32 data, int32 PA, int32 access)
{
int32 ln = ((PA - dci_dib.ba) >> 3) & DCX_MASK;
TMLN *lp = &dcx_ldsc[ln];

switch ((PA >> 1) & 03) {                               /* decode PA<2:1> */

    case 00:                                            /* dci csr */
        if (access == WRITEB)                           /* byte write? */
            data = (PA & 1)?
            (dci_csr[ln] & 0377) | (data << 8):
            (dci_csr[ln] & ~0377) | data;
        if ((data & CSR_IE) == 0)                       /* clr ie? */
            dci_clr_int (ln);                           /* clr int req */
        else if ((dci_csr[ln] & (CSR_DONE + CSR_IE)) == CSR_DONE)
            dci_set_int (ln);
        if (((data ^ dci_csr[ln]) & DCICSR_DTR) &&      /* DTR change? */
            (dco_unit[ln].flags & DCX_MDM)) {           /* modem ctl? */
            if (data & DCICSR_DTR) {                    /* setting DTR? */
                if (lp->conn) {                         /* ringing? */
                    dci_csr[ln] = (dci_csr[ln] & ~DCICSR_RNG) |
                        (DCICSR_CDT|DCICSR_CCH|DCICSR_ERR);
                    dco_csr[ln] |= DCOCSR_CTS;          /* set CDT,CCH,CTS */
                    if (data & CSR_IE)                  /* if ie, req int */
                        dci_set_int (ln);
                    }
                }                                       /* end DTR 0->1 */
            else {                                      /* clearing DTR */
                if (lp->conn) {                         /* connected? */
                    tmxr_linemsg (lp, "\r\nLine hangup\r\n");
                    tmxr_reset_ln (lp);                 /* reset line */
                    if (dci_csr[ln] & DCICSR_CDT) {     /* carrier det? */
                        dci_csr[ln] |= (DCICSR_CCH|DCICSR_ERR);
                        if (data & CSR_IE)              /* if ie, req int */
                            dci_set_int (ln);
                        }
                    }
                 dci_csr[ln] &= ~(DCICSR_CDT|DCICSR_RNG);
                 dco_csr[ln] &= ~DCOCSR_CTS;            /* clr CDT,RNG,CTS */
                 }                                      /* end DTR 1->0 */
            }                                           /* end DTR chg+modem */
        dci_csr[ln] = (uint16) ((dci_csr[ln] & ~DCICSR_WR) | (data & DCICSR_WR));
        return SCPE_OK;

    case 01:                                            /* dci buf */
        return SCPE_OK;

    case 02:                                            /* dco csr */
        if (access == WRITEB)                           /* byte write? */
            data = (PA & 1)?
            (dco_csr[ln] & 0377) | (data << 8):
            (dco_csr[ln] & ~0377) | data;
        if ((data & CSR_IE) == 0)                       /* clr ie? */
            dco_clr_int (ln);                           /* clr int req */
        else if ((dco_csr[ln] & (CSR_DONE + CSR_IE)) == CSR_DONE)
            dco_set_int (ln);
        dco_csr[ln] = (uint16) ((dco_csr[ln] & ~DCOCSR_WR) | (data & DCOCSR_WR));
        return SCPE_OK;

    case 03:                                            /* dco buf */
        if ((PA & 1) == 0)
            dco_buf[ln] = data & 0377;
        dco_csr[ln] &= ~CSR_DONE;                       /* clr done */
        dco_clr_int (ln);                               /* clr int req */
        sim_activate (&dco_unit[ln], dco_unit[ln].wait);
        return SCPE_OK;
        }                                               /* end switch PA */

return SCPE_NXM;
}

/* Terminal input service */

t_stat dci_svc (UNIT *uptr)
{
int32 ln, c, temp;

if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return SCPE_OK;
sim_activate (uptr, tmxr_poll);                         /* continue poll */
ln = tmxr_poll_conn (&dcx_desc);                        /* look for connect */
if (ln >= 0) {                                          /* got one? */
    dcx_ldsc[ln].rcve = 1;                              /* set rcv enb */
    if (dco_unit[ln].flags & DCX_MDM) {                 /* modem control? */
        if (dci_csr[ln] & DCICSR_DTR)                   /* DTR already set? */
            dci_csr[ln] |= (DCICSR_CDT|DCICSR_CCH|DCICSR_ERR);
        else dci_csr[ln] |= (DCICSR_RNG|DCICSR_ERR);    /* no, ring */
        if (dci_csr[ln] & CSR_IE)                       /* if ie, */
            dci_set_int (ln);                           /* req int */
        }
    else dco_csr[ln] |= DCOCSR_CTS;                     /* just connect */
    }
tmxr_poll_rx (&dcx_desc);                               /* poll for input */
for (ln = 0; ln < DCX_LINES; ln++) {                    /* loop thru lines */
    if (dcx_ldsc[ln].conn) {                            /* connected? */
        if ((temp = tmxr_getc_ln (&dcx_ldsc[ln])) &&    /* get char */
            !(temp & SCPE_BREAK)) {                     /* not break? */
            c = sim_tt_inpcvt (temp, TT_GET_MODE (dco_unit[ln].flags));
            if (dci_csr[ln] & CSR_DONE)                 /* overrun? */
                dci_csr[ln] |= DCICSR_OVR;
            else dci_csr[ln] |= CSR_DONE;               /* set done */
            if (dci_csr[ln] & CSR_IE)                   /* if ie, */
                dci_set_int (ln);                       /* req int */
            if (dco_unit[ln].flags & DCX_OPAR)          /* odd parity */
                c = (c & 0177) | odd_par[c & 0177];
            else if (dco_unit[ln].flags & DCX_EPAR)     /* even parity */
                c = (c & 0177) | (odd_par[c & 0177] ^ 0200);
            dci_buf[ln] = c;
            if ((c & 0200) == odd_par[c & 0177])        /* odd par? */
                dci_csr[ln] |= DCICSR_PAR;
            else dci_csr[ln] &= ~DCICSR_PAR;
            }
        }
    else {                                              /* disconnected */
        if ((dco_unit[ln].flags & DCX_MDM) &&           /* modem control? */
            (dci_csr[ln] & DCICSR_CDT)) {               /* carrier detect? */
            dci_csr[ln] |= (DCICSR_CCH|DCICSR_ERR);     /* carrier change */
            if (dci_csr[ln] & CSR_IE)                   /* if ie, */
                dci_set_int (ln);                       /* req int */
            }
        dci_csr[ln] &= ~(DCICSR_CDT|DCICSR_RNG);        /* clr CDT,RNG,CTS */
        dco_csr[ln] &= ~DCOCSR_CTS;
        }
    }
return SCPE_OK;
}

/* Terminal output service */

t_stat dco_svc (UNIT *uptr)
{
int32 c;
int32 ln = uptr - dco_unit;                             /* line # */

if (dcx_ldsc[ln].conn) {                                /* connected? */
    if (dcx_ldsc[ln].xmte) {                            /* tx enabled? */
        TMLN *lp = &dcx_ldsc[ln];                       /* get line */
        c = sim_tt_outcvt (dco_buf[ln], TT_GET_MODE (dco_unit[ln].flags));
        if (c >= 0)                                     /* output char */
            tmxr_putc_ln (lp, c);
        tmxr_poll_tx (&dcx_desc);                       /* poll xmt */
        }
    else {
        tmxr_poll_tx (&dcx_desc);                       /* poll xmt */
        sim_activate (uptr, dco_unit[ln].wait);         /* wait */
        return SCPE_OK;
        }
    }
dco_csr[ln] |= CSR_DONE;                                /* set done */
if (dco_csr[ln] & CSR_IE)                               /* ie set? */
    dco_set_int (ln);                                   /* req int */
return SCPE_OK;
}

/* Interrupt routines */

void dci_clr_int (int32 ln)
{
dci_ireq &= ~(1 << ln);                                 /* clr mux rcv int */
if (dci_ireq == 0)                                      /* all clr? */
    CLR_INT (DCI);
else SET_INT (DCI);                                     /* no, set intr */
return;
}

void dci_set_int (int32 ln)
{
dci_ireq |= (1 << ln);                                  /* clr mux rcv int */
SET_INT (DCI);                                          /* set master intr */
return;
}

int32 dci_iack (void)
{
int32 ln;

for (ln = 0; ln < DCX_LINES; ln++) {                    /* find 1st line */
    if (dci_ireq & (1 << ln)) {
        dci_clr_int (ln);                               /* clear intr */
        return (dci_dib.vec + (ln * 010));              /* return vector */
        }
    }
return 0;
}

void dco_clr_int (int32 ln)
{
dco_ireq &= ~(1 << ln);                                 /* clr mux rcv int */
if (dco_ireq == 0)                                      /* all clr? */
    CLR_INT (DCO);
else SET_INT (DCO);                                     /* no, set intr */
return;
}

void dco_set_int (int32 ln)
{
dco_ireq |= (1 << ln);                                  /* clr mux rcv int */
SET_INT (DCO);                                          /* set master intr */
return;
}

int32 dco_iack (void)
{
int32 ln;

for (ln = 0; ln < DCX_LINES; ln++) {                    /* find 1st line */
    if (dco_ireq & (1 << ln)) {
        dco_clr_int (ln);                               /* clear intr */
        return (dci_dib.vec + (ln * 010) + 4);          /* return vector */
        }
    }
return 0;
}

/* Reset */

t_stat dcx_reset (DEVICE *dptr)
{
int32 ln;

dcx_enbdis (dptr->flags & DEV_DIS);                     /* sync enables */
sim_cancel (&dci_unit);                                 /* assume stop */
if (dci_unit.flags & UNIT_ATT)                          /* if attached, */
    sim_activate (&dci_unit, tmxr_poll);                /* activate */
for (ln = 0; ln < DCX_LINES; ln++)                      /* for all lines */
    dcx_reset_ln (ln);
return auto_config (dci_dev.name, dcx_desc.lines);      /* auto config */
}

/* Reset individual line */

void dcx_reset_ln (int32 ln)
{
dci_buf[ln] = 0;                                        /* clear buf */
dci_csr[ln] = 0;
dco_buf[ln] = 0;                                        /* clear buf */
dco_csr[ln] = CSR_DONE;
sim_cancel (&dco_unit[ln]);                             /* deactivate */
dci_clr_int (ln);
dco_clr_int (ln);
return;
}

/* Attach master unit */

t_stat dcx_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = tmxr_attach (&dcx_desc, uptr, cptr);                /* attach */
if (r != SCPE_OK)                                       /* error? */
    return r;
sim_activate (uptr, tmxr_poll);                         /* start poll */
return SCPE_OK;
}

/* Detach master unit */

t_stat dcx_detach (UNIT *uptr)
{
int32 i;
t_stat r;

r = tmxr_detach (&dcx_desc, uptr);                      /* detach */
for (i = 0; i < DCX_LINES; i++)                         /* all lines, */
    dcx_ldsc[i].rcve = 0;                               /* disable rcv */
sim_cancel (uptr);                                      /* stop poll */
return r;
}

/* Enable/disable device */

void dcx_enbdis (int32 dis)
{
if (dis) {
    dci_dev.flags = dci_dev.flags | DEV_DIS;
    dco_dev.flags = dco_dev.flags | DEV_DIS;
    }
else {
    dci_dev.flags = dci_dev.flags & ~DEV_DIS;
    dco_dev.flags = dco_dev.flags & ~DEV_DIS;
    }
return;
}

/* Change number of lines */

t_stat dcx_set_lines (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 newln, i, t;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
newln = get_uint (cptr, 10, DCX_LINES, &r);
if ((r != SCPE_OK) || (newln == dcx_desc.lines))
    return r;
if (newln == 0)
    return SCPE_ARG;
if (newln < dcx_desc.lines) {
    for (i = newln, t = 0; i < dcx_desc.lines; i++)
        t = t | dcx_ldsc[i].conn;
    if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
        return SCPE_OK;
    for (i = newln; i < dcx_desc.lines; i++) {
        if (dcx_ldsc[i].conn) {
            tmxr_linemsg (&dcx_ldsc[i], "\r\nOperator disconnected line\r\n");
            tmxr_reset_ln (&dcx_ldsc[i]);               /* reset line */
            }
        dco_unit[i].flags |= UNIT_DIS;
        dcx_reset_ln (i);
        }
    }
else {
    for (i = dcx_desc.lines; i < newln; i++) {
        dco_unit[i].flags &= ~UNIT_DIS;
        dcx_reset_ln (i);
        }
    }
dcx_desc.lines = newln;
dci_dib.lnt = newln * 010;                             /* upd IO page lnt */
return auto_config (dci_dev.name, newln);              /* auto config */
}
