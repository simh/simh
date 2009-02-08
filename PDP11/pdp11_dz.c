/* pdp11_dz.c: DZ11 terminal multiplexor simulator

   Copyright (c) 2001-2008, Robert M Supnik

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

   dz           DZ11 terminal multiplexor

   29-Dec-08    RMS     Added MTAB_NC to SET LOG command (found by Walter Mueller)
   19-Nov-08    RMS     Revised for common TMXR show routines
   18-Jun-07    RMS     Added UNIT_IDLE flag
   29-Oct-06    RMS     Synced poll and clock
   22-Nov-05    RMS     Revised for new terminal processing routines
   07-Jul-05    RMS     Removed extraneous externs
   15-Jun-05    RMS     Revised for new autoconfigure interface
   04-Apr-04    RMS     Added per-line logging
   05-Jan-04    RMS     Revised for tmxr library changes
   19-May-03    RMS     Revised for new conditional compilation scheme
   09-May-03    RMS     Added network device flag
   22-Dec-02    RMS     Added break (framing error) support
   31-Oct-02    RMS     Added 8b support
   12-Oct-02    RMS     Added autoconfigure support
   29-Sep-02    RMS     Fixed bug in set number of lines routine
                        Added variable vector support
                        New data structures
   22-Apr-02    RMS     Updated for changes in sim_tmxr
   28-Apr-02    RMS     Fixed interrupt acknowledge, fixed SHOW DZ ADDRESS
   14-Jan-02    RMS     Added multiboard support
   30-Dec-01    RMS     Added show statistics, set disconnect
                        Removed statistics registers
   03-Dec-01    RMS     Modified for extended SET/SHOW
   09-Nov-01    RMS     Added VAX support
   20-Oct-01    RMS     Moved getchar from sim_tmxr, changed interrupt
                        logic to use tmxr_rqln
   06-Oct-01    RMS     Fixed bug in carrier detect logic
   03-Oct-01    RMS     Added support for BSD-style "ringless" modems
   27-Sep-01    RMS     Fixed bug in xmte initialization
   17-Sep-01    RMS     Added separate autodisconnect switch
   16-Sep-01    RMS     Fixed modem control bit offsets
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#include "pdp10_defs.h"
#define RANK_DZ         0                               /* no autoconfig */
#define DZ_8B_DFLT      0
extern int32 int_req;

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"
#define DZ_8B_DFLT      TT_MODE_8B
extern int32 int_req[IPL_HLVL];

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#define DZ_8B_DFLT      TT_MODE_8B
extern int32 int_req[IPL_HLVL];
#endif

#include "sim_sock.h"
#include "sim_tmxr.h"

#if !defined (DZ_MUXES)
#define DZ_MUXES        1
#endif
#if !defined (DZ_LINES)
#define DZ_LINES        8
#endif

#define DZ_MNOMASK      (DZ_MUXES - 1)                  /* mask for mux no */
#define DZ_LNOMASK      (DZ_LINES - 1)                  /* mask for lineno */
#define DZ_LMASK        ((1 << DZ_LINES) - 1)           /* mask of lines */
#define DZ_SILO_ALM     16                              /* silo alarm level */

/* DZCSR - 160100 - control/status register */

#define CSR_MAINT       0000010                         /* maint - NI */
#define CSR_CLR         0000020                         /* clear */
#define CSR_MSE         0000040                         /* master scan enb */
#define CSR_RIE         0000100                         /* rcv int enb */
#define CSR_RDONE       0000200                         /* rcv done - RO */
#define CSR_V_TLINE     8                               /* xmit line - RO */
#define CSR_TLINE       (DZ_LNOMASK << CSR_V_TLINE)
#define CSR_SAE         0010000                         /* silo alm enb */
#define CSR_SA          0020000                         /* silo alm - RO */
#define CSR_TIE         0040000                         /* xmit int enb */
#define CSR_TRDY        0100000                         /* xmit rdy - RO */
#define CSR_RW          (CSR_MSE | CSR_RIE | CSR_SAE | CSR_TIE)
#define CSR_MBZ         (0004003 | CSR_CLR | CSR_MAINT)

#define CSR_GETTL(x)    (((x) >> CSR_V_TLINE) & DZ_LNOMASK)
#define CSR_PUTTL(x,y)  x = ((x) & ~CSR_TLINE) | (((y) & DZ_LNOMASK) << CSR_V_TLINE)

/* DZRBUF - 160102 - receive buffer, read only */

#define RBUF_CHAR       0000377                         /* rcv char */
#define RBUF_V_RLINE    8                               /* rcv line */
#define RBUF_PARE       0010000                         /* parity err - NI */
#define RBUF_FRME       0020000                         /* frame err */
#define RBUF_OVRE       0040000                         /* overrun err - NI */
#define RBUF_VALID      0100000                         /* rcv valid */
#define RBUF_MBZ        0004000

/* DZLPR - 160102 - line parameter register, write only, word access only */

#define LPR_V_LINE      0                               /* line */
#define LPR_LPAR        0007770                         /* line pars - NI */
#define LPR_RCVE        0010000                         /* receive enb */
#define LPR_GETLN(x)    (((x) >> LPR_V_LINE) & DZ_LNOMASK)

/* DZTCR - 160104 - transmission control register */

#define TCR_V_XMTE      0                               /* xmit enables */
#define TCR_V_DTR       8                               /* DTRs */

/* DZMSR - 160106 - modem status register, read only */

#define MSR_V_RI        0                               /* ring indicators */
#define MSR_V_CD        8                               /* carrier detect */

/* DZTDR - 160106 - transmit data, write only */

#define TDR_CHAR        0000377                         /* xmit char */
#define TDR_V_TBR       8                               /* xmit break - NI */

extern int32 IREQ (HLVL);
extern int32 sim_switches;
extern FILE *sim_log;
extern int32 tmxr_poll;                                 /* calibrated delay */

uint16 dz_csr[DZ_MUXES] = { 0 };                        /* csr */
uint16 dz_rbuf[DZ_MUXES] = { 0 };                       /* rcv buffer */
uint16 dz_lpr[DZ_MUXES] = { 0 };                        /* line param */
uint16 dz_tcr[DZ_MUXES] = { 0 };                        /* xmit control */
uint16 dz_msr[DZ_MUXES] = { 0 };                        /* modem status */
uint16 dz_tdr[DZ_MUXES] = { 0 };                        /* xmit data */
uint8 dz_sae[DZ_MUXES] = { 0 };                         /* silo alarm enabled */
uint32 dz_rxi = 0;                                      /* rcv interrupts */
uint32 dz_txi = 0;                                      /* xmt interrupts */
int32 dz_mctl = 0;                                      /* modem ctrl enabled */
int32 dz_auto = 0;                                      /* autodiscon enabled */
TMLN dz_ldsc[DZ_MUXES * DZ_LINES] = { 0 };              /* line descriptors */
TMXR dz_desc = { DZ_MUXES * DZ_LINES, 0, 0, dz_ldsc };  /* mux descriptor */

DEVICE dz_dev;
t_stat dz_rd (int32 *data, int32 PA, int32 access);
t_stat dz_wr (int32 data, int32 PA, int32 access);
int32 dz_rxinta (void);
int32 dz_txinta (void);
t_stat dz_svc (UNIT *uptr);
t_stat dz_reset (DEVICE *dptr);
t_stat dz_attach (UNIT *uptr, char *cptr);
t_stat dz_detach (UNIT *uptr);
t_stat dz_clear (int32 dz, t_bool flag);
int32 dz_getc (int32 dz);
void dz_update_rcvi (void);
void dz_update_xmti (void);
void dz_clr_rxint (int32 dz);
void dz_set_rxint (int32 dz);
void dz_clr_txint (int32 dz);
void dz_set_txint (int32 dz);
t_stat dz_setnl (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat dz_set_log (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat dz_set_nolog (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat dz_show_log (FILE *st, UNIT *uptr, int32 val, void *desc);

/* DZ data structures

   dz_dev       DZ device descriptor
   dz_unit      DZ unit list
   dz_reg       DZ register list
*/

DIB dz_dib = {
    IOBA_DZ, IOLN_DZ * DZ_MUXES, &dz_rd, &dz_wr,
    2, IVCL (DZRX), VEC_DZRX, { &dz_rxinta, &dz_txinta }
    };

UNIT dz_unit = { UDATA (&dz_svc, UNIT_IDLE|UNIT_ATTABLE|DZ_8B_DFLT, 0) };

REG dz_reg[] = {
    { BRDATA (CSR, dz_csr, DEV_RDX, 16, DZ_MUXES) },
    { BRDATA (RBUF, dz_rbuf, DEV_RDX, 16, DZ_MUXES) },
    { BRDATA (LPR, dz_lpr, DEV_RDX, 16, DZ_MUXES) },
    { BRDATA (TCR, dz_tcr, DEV_RDX, 16, DZ_MUXES) },
    { BRDATA (MSR, dz_msr, DEV_RDX, 16, DZ_MUXES) },
    { BRDATA (TDR, dz_tdr, DEV_RDX, 16, DZ_MUXES) },
    { BRDATA (SAENB, dz_sae, DEV_RDX, 1, DZ_MUXES) },
    { GRDATA (RXINT, dz_rxi, DEV_RDX, DZ_MUXES, 0) },
    { GRDATA (TXINT, dz_txi, DEV_RDX, DZ_MUXES, 0) },
    { FLDATA (MDMCTL, dz_mctl, 0) },
    { FLDATA (AUTODS, dz_auto, 0) },
    { GRDATA (DEVADDR, dz_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVVEC, dz_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

MTAB dz_mod[] = {
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
    { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &dz_desc },
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &dz_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *) &dz_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *) &dz_desc },
    { MTAB_XTD|MTAB_VDV, 010, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, DZ_LINES, "VECTOR", "VECTOR",
      &set_vec, &show_vec_mux, (void *) &dz_desc },
#if !defined (VM_PDP10)
    { MTAB_XTD | MTAB_VDV, 0, NULL, "AUTOCONFIGURE",
      &set_addr_flt, NULL, NULL },
#endif
    { MTAB_XTD | MTAB_VDV, 0, "LINES", "LINES",
      &dz_setnl, &tmxr_show_lines, (void *) &dz_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NC, 0, NULL, "LOG",
      &dz_set_log, NULL, &dz_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NC, 0, NULL, "NOLOG",
      &dz_set_nolog, NULL, &dz_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "LOG", NULL,
      NULL, &dz_show_log, &dz_desc },
    { 0 }
    };

DEVICE dz_dev = {
    "DZ", &dz_unit, dz_reg, dz_mod,
    1, DEV_RDX, 8, 1, DEV_RDX, 8,
    &tmxr_ex, &tmxr_dep, &dz_reset,
    NULL, &dz_attach, &dz_detach,
    &dz_dib, DEV_FLTA | DEV_DISABLE | DEV_NET | DEV_UBUS | DEV_QBUS
    };

/* IO dispatch routines, I/O addresses 177601x0 - 177601x7 */

t_stat dz_rd (int32 *data, int32 PA, int32 access)
{
int32 dz = ((PA - dz_dib.ba) >> 3) & DZ_MNOMASK;        /* get mux num */

switch ((PA >> 1) & 03) {                               /* case on PA<2:1> */

    case 00:                                            /* CSR */
        *data = dz_csr[dz] = dz_csr[dz] & ~CSR_MBZ;
        break;

    case 01:                                            /* RBUF */
        dz_csr[dz] = dz_csr[dz] & ~CSR_SA;              /* clr silo alarm */
        if (dz_csr[dz] & CSR_MSE) {                     /* scanner on? */
            dz_rbuf[dz] = dz_getc (dz);                 /* get top of silo */
            if (!dz_rbuf[dz])                           /* empty? re-enable */
                dz_sae[dz] = 1;
            tmxr_poll_rx (&dz_desc);                    /* poll input */
            dz_update_rcvi ();                          /* update rx intr */
            }
        else {
            dz_rbuf[dz] = 0;                            /* no data */
            dz_update_rcvi ();                          /* no rx intr */
            }
        *data = dz_rbuf[dz];
        break;

    case 02:                                            /* TCR */
        *data = dz_tcr[dz];
        break;

    case 03:                                            /* MSR */
        *data = dz_msr[dz];
        break;
        }

return SCPE_OK;
}

t_stat dz_wr (int32 data, int32 PA, int32 access)
{
int32 dz = ((PA - dz_dib.ba) >> 3) & DZ_MNOMASK;        /* get mux num */
int32 i, c, line;
TMLN *lp;

switch ((PA >> 1) & 03) {                               /* case on PA<2:1> */

    case 00:                                            /* CSR */
        if (access == WRITEB) data = (PA & 1)?          /* byte? merge */
            (dz_csr[dz] & 0377) | (data << 8):
            (dz_csr[dz] & ~0377) | data;
        if (data & CSR_CLR)                             /* clr? reset */
            dz_clear (dz, FALSE);
        if (data & CSR_MSE)                             /* MSE? start poll */
            sim_activate (&dz_unit, clk_cosched (tmxr_poll));
        else dz_csr[dz] &= ~(CSR_SA | CSR_RDONE | CSR_TRDY);
        if ((data & CSR_RIE) == 0)                      /* RIE = 0? */
            dz_clr_rxint (dz);
        else if (((dz_csr[dz] & CSR_IE) == 0) &&        /* RIE 0->1? */
             ((dz_csr[dz] & CSR_SAE)?
             (dz_csr[dz] & CSR_SA): (dz_csr[dz] & CSR_RDONE)))
            dz_set_rxint (dz);
        if ((data & CSR_TIE) == 0)                      /* TIE = 0? */
            dz_clr_txint (dz);
        else if (((dz_csr[dz] & CSR_TIE) == 0) && (dz_csr[dz] & CSR_TRDY))
            dz_set_txint (dz);
        dz_csr[dz] = (dz_csr[dz] & ~CSR_RW) | (data & CSR_RW);
        break;

    case 01:                                            /* LPR */
        dz_lpr[dz] = data;
        line = (dz * DZ_LINES) + LPR_GETLN (data);      /* get line num */
        lp = &dz_ldsc[line];                            /* get line desc */
        if (dz_lpr[dz] & LPR_RCVE)                      /* rcv enb? on */
            lp->rcve = 1;
        else lp->rcve = 0;                              /* else line off */
        tmxr_poll_rx (&dz_desc);                        /* poll input */
        dz_update_rcvi ();                              /* update rx intr */
        break;

    case 02:                                            /* TCR */
        if (access == WRITEB) data = (PA & 1)?          /* byte? merge */
            (dz_tcr[dz] & 0377) | (data << 8):
            (dz_tcr[dz] & ~0377) | data;
        if (dz_mctl) {                                  /* modem ctl? */
            dz_msr[dz] |= ((data & 0177400) &           /* dcd |= dtr & ring */
                ((dz_msr[dz] & DZ_LMASK) << MSR_V_CD));
            dz_msr[dz] &=  ~(data >> TCR_V_DTR);        /* ring &= ~dtr */
            if (dz_auto) {                              /* auto disconnect? */
                int32 drop;
                drop = (dz_tcr[dz] & ~data) >> TCR_V_DTR; /* drop = dtr & ~data */
                for (i = 0; i < DZ_LINES; i++) {        /* drop hangups */
                    line = (dz * DZ_LINES) + i;         /* get line num */
                    lp = &dz_ldsc[line];                /* get line desc */
                    if (lp->conn && (drop & (1 << i))) {
                        tmxr_linemsg (lp, "\r\nLine hangup\r\n");
                        tmxr_reset_ln (lp);             /* reset line, cdet */
                        dz_msr[dz] &= ~(1 << (i + MSR_V_CD));
                        }                               /* end if drop */
                    }                                   /* end for */
                }                                       /* end if auto */
            }                                           /* end if modem */
        dz_tcr[dz] = data;
        tmxr_poll_tx (&dz_desc);                        /* poll output */
        dz_update_xmti ();                              /* update int */
        break;

    case 03:                                            /* TDR */
        if (PA & 1) {                                   /* odd byte? */
            dz_tdr[dz] = (dz_tdr[dz] & 0377) | (data << 8);     /* just save */
            break;
            }
        dz_tdr[dz] = data;
        if (dz_csr[dz] & CSR_MSE) {                     /* enabled? */
            line = (dz * DZ_LINES) + CSR_GETTL (dz_csr[dz]);
            lp = &dz_ldsc[line];                        /* get line desc */
            c = sim_tt_outcvt (dz_tdr[dz], TT_GET_MODE (dz_unit.flags));
            if (c >= 0)                                 /* store char */
                tmxr_putc_ln (lp, c);
            tmxr_poll_tx (&dz_desc);                    /* poll output */
            dz_update_xmti ();                          /* update int */
            }
        break;
        }

return SCPE_OK;
}

/* Unit service routine

   The DZ11 polls to see if asynchronous activity has occurred and now
   needs to be processed.  The polling interval is controlled by the clock
   simulator, so for most environments, it is calibrated to real time.
   Typical polling intervals are 50-60 times per second.

   The simulator assumes that software enables all of the multiplexors,
   or none of them.
*/

t_stat dz_svc (UNIT *uptr)
{
int32 dz, t, newln;

for (dz = t = 0; dz < DZ_MUXES; dz++)                   /* check enabled */
    t = t | (dz_csr[dz] & CSR_MSE);
if (t) {                                                /* any enabled? */
    newln = tmxr_poll_conn (&dz_desc);                  /* poll connect */
    if ((newln >= 0) && dz_mctl) {                      /* got a live one? */
        dz = newln / DZ_LINES;                          /* get mux num */
        if (dz_tcr[dz] & (1 << (newln + TCR_V_DTR)))    /* DTR set? */
            dz_msr[dz] |= (1 << (newln + MSR_V_CD));    /* set cdet */
        else dz_msr[dz] |= (1 << newln);                /* set ring */
        }
    tmxr_poll_rx (&dz_desc);                            /* poll input */
    dz_update_rcvi ();                                  /* upd rcv intr */
    tmxr_poll_tx (&dz_desc);                            /* poll output */
    dz_update_xmti ();                                  /* upd xmt intr */
    sim_activate (uptr, tmxr_poll);                     /* reactivate */
    }
return SCPE_OK;
}

/* Get first available character for mux, if any */

int32 dz_getc (int32 dz)
{
uint32 i, line, c;

for (i = c = 0; (i < DZ_LINES) && (c == 0); i++) {      /* loop thru lines */
    line = (dz * DZ_LINES) + i;                         /* get line num */
    c = tmxr_getc_ln (&dz_ldsc[line]);                  /* test for input */
    if (c & SCPE_BREAK)                                 /* break? frame err */
        c = RBUF_VALID | RBUF_FRME;
    if (c)                                              /* or in line # */
        c = c | (i << RBUF_V_RLINE);
    }                                                   /* end for */
return c;
}

/* Update receive interrupts */

void dz_update_rcvi (void)
{
int32 i, dz, line, scnt[DZ_MUXES];
TMLN *lp;

for (dz = 0; dz < DZ_MUXES; dz++) {                     /* loop thru muxes */
    scnt[dz] = 0;                                       /* clr input count */
    for (i = 0; i < DZ_LINES; i++) {                    /* poll lines */
        line = (dz * DZ_LINES) + i;                     /* get line num */
        lp = &dz_ldsc[line];                            /* get line desc */
        scnt[dz] = scnt[dz] + tmxr_rqln (lp);           /* sum buffers */
        if (dz_mctl && !lp->conn)                       /* if disconn */
            dz_msr[dz] &= ~(1 << (i + MSR_V_CD));       /* reset car det */
        }
    }
for (dz = 0; dz < DZ_MUXES; dz++) {                     /* loop thru muxes */
    if (scnt[dz] && (dz_csr[dz] & CSR_MSE)) {           /* input & enabled? */
        dz_csr[dz] |= CSR_RDONE;                        /* set done */
        if (dz_sae[dz] && (scnt[dz] >= DZ_SILO_ALM)) {  /* alm enb & cnt hi? */
            dz_csr[dz] |= CSR_SA;                       /* set status */
            dz_sae[dz] = 0;                             /* disable alarm */
            }
        }
    else dz_csr[dz] &= ~CSR_RDONE;                      /* no, clear done */
    if ((dz_csr[dz] & CSR_RIE) &&                       /* int enable */
        ((dz_csr[dz] & CSR_SAE)?
         (dz_csr[dz] & CSR_SA): (dz_csr[dz] & CSR_RDONE)))
        dz_set_rxint (dz);                              /* and alm/done? */
    else dz_clr_rxint (dz);                             /* no, clear int */
    }
return;
}

/* Update transmit interrupts */

void dz_update_xmti (void)
{
int32 dz, linemask, i, j, line;

for (dz = 0; dz < DZ_MUXES; dz++) {                     /* loop thru muxes */
    linemask = dz_tcr[dz] & DZ_LMASK;                   /* enabled lines */
    dz_csr[dz] &= ~CSR_TRDY;                            /* assume not rdy */
    j = CSR_GETTL (dz_csr[dz]);                         /* start at current */
    for (i = 0; i < DZ_LINES; i++) {                    /* loop thru lines */
        j = (j + 1) & DZ_LNOMASK;                       /* next line */
        line = (dz * DZ_LINES) + j;                     /* get line num */
        if ((linemask & (1 << j)) && dz_ldsc[line].xmte) {
            CSR_PUTTL (dz_csr[dz], j);                  /* put ln in csr */
            dz_csr[dz] |= CSR_TRDY;                     /* set xmt rdy */
            break;
            }
        }
    if ((dz_csr[dz] & CSR_TIE) && (dz_csr[dz] & CSR_TRDY)) /* ready plus int? */
         dz_set_txint (dz);
    else dz_clr_txint (dz);                             /* no int req */
    }
return;
}

/* Interrupt routines */

void dz_clr_rxint (int32 dz)
{
dz_rxi = dz_rxi & ~(1 << dz);                           /* clr mux rcv int */
if (dz_rxi == 0)                                        /* all clr? */
    CLR_INT (DZRX);
else SET_INT (DZRX);                                    /* no, set intr */
return;
}

void dz_set_rxint (int32 dz)
{
dz_rxi = dz_rxi | (1 << dz);                            /* set mux rcv int */
SET_INT (DZRX);                                         /* set master intr */
return;
}

int32 dz_rxinta (void)
{
int32 dz;

for (dz = 0; dz < DZ_MUXES; dz++) {                     /* find 1st mux */
    if (dz_rxi & (1 << dz)) {
        dz_clr_rxint (dz);                              /* clear intr */
        return (dz_dib.vec + (dz * 010));               /* return vector */
        }
    }
return 0;
}

void dz_clr_txint (int32 dz)
{
dz_txi = dz_txi & ~(1 << dz);                           /* clr mux xmt int */
if (dz_txi == 0)                                        /* all clr? */
    CLR_INT (DZTX);
else SET_INT (DZTX);                                    /* no, set intr */
return;
}

void dz_set_txint (int32 dz)
{
dz_txi = dz_txi | (1 << dz);                            /* set mux xmt int */
SET_INT (DZTX);                                         /* set master intr */
return;
}

int32 dz_txinta (void)
{
int32 dz;

for (dz = 0; dz < DZ_MUXES; dz++) {                     /* find 1st mux */
    if (dz_txi & (1 << dz)) {
        dz_clr_txint (dz);                              /* clear intr */
        return (dz_dib.vec + 4 + (dz * 010));           /* return vector */
        }
    }
return 0;
}

/* Device reset */

t_stat dz_clear (int32 dz, t_bool flag)
{
int32 i, line;

dz_csr[dz] = 0;                                         /* clear CSR */
dz_rbuf[dz] = 0;                                        /* silo empty */
dz_lpr[dz] = 0;                                         /* no params */
if (flag)                                               /* INIT? clr all */
    dz_tcr[dz] = 0;
else dz_tcr[dz] &= ~0377;                               /* else save dtr */
dz_tdr[dz] = 0;
dz_sae[dz] = 1;                                         /* alarm on */
dz_clr_rxint (dz);                                      /* clear int */
dz_clr_txint (dz);
for (i = 0; i < DZ_LINES; i++) {                        /* loop thru lines */
    line = (dz * DZ_LINES) + i;
    if (!dz_ldsc[line].conn)                            /* set xmt enb */
        dz_ldsc[line].xmte = 1;
    dz_ldsc[line].rcve = 0;                             /* clr rcv enb */
    }
return SCPE_OK;
}

t_stat dz_reset (DEVICE *dptr)
{
int32 i, ndev;

for (i = 0; i < DZ_MUXES; i++)                          /* init muxes */
    dz_clear (i, TRUE);
dz_rxi = dz_txi = 0;                                    /* clr master int */
CLR_INT (DZRX);
CLR_INT (DZTX);
sim_cancel (&dz_unit);                                  /* stop poll */
ndev = ((dptr->flags & DEV_DIS)? 0: (dz_desc.lines / DZ_LINES));
return auto_config (dptr->name, ndev);                  /* auto config */
}

/* Attach */

t_stat dz_attach (UNIT *uptr, char *cptr)
{
t_stat r;
extern int32 sim_switches;

dz_mctl = dz_auto = 0;                                  /* modem ctl off */
r = tmxr_attach (&dz_desc, uptr, cptr);                 /* attach mux */
if (r != SCPE_OK)                                       /* error? */
    return r;
if (sim_switches & SWMASK ('M')) {                      /* modem control? */
    dz_mctl = 1;
    printf ("Modem control activated\n");
    if (sim_log)
        fprintf (sim_log, "Modem control activated\n");
    if (sim_switches & SWMASK ('A')) {                  /* autodisconnect? */
        dz_auto = 1;
        printf ("Auto disconnect activated\n");
        if (sim_log)
            fprintf (sim_log, "Auto disconnect activated\n");
        }
    }
return SCPE_OK;
}

/* Detach */

t_stat dz_detach (UNIT *uptr)
{
return tmxr_detach (&dz_desc, uptr);
}

/* SET LINES processor */

t_stat dz_setnl (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 newln, i, t, ndev;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
newln = (int32) get_uint (cptr, 10, (DZ_MUXES * DZ_LINES), &r);
if ((r != SCPE_OK) || (newln == dz_desc.lines))
    return r;
if ((newln == 0) || (newln % DZ_LINES))
    return SCPE_ARG;
if (newln < dz_desc.lines) {
    for (i = newln, t = 0; i < dz_desc.lines; i++)
        t = t | dz_ldsc[i].conn;
    if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
        return SCPE_OK;
    for (i = newln; i < dz_desc.lines; i++) {
        if (dz_ldsc[i].conn) {
            tmxr_linemsg (&dz_ldsc[i], "\r\nOperator disconnected line\r\n");
            tmxr_reset_ln (&dz_ldsc[i]);                /* reset line */
            }
        if ((i % DZ_LINES) == (DZ_LINES - 1))
            dz_clear (i / DZ_LINES, TRUE);              /* reset mux */
        }
    }
dz_dib.lnt = (newln / DZ_LINES) * IOLN_DZ;              /* set length */
dz_desc.lines = newln;
ndev = ((dz_dev.flags & DEV_DIS)? 0: (dz_desc.lines / DZ_LINES));
return auto_config (dz_dev.name, ndev);                 /* auto config */
}

/* SET LOG processor */

t_stat dz_set_log (UNIT *uptr, int32 val, char *cptr, void *desc)
{
char *tptr;
t_stat r;
int32 ln;

if (cptr == NULL)
    return SCPE_ARG;
tptr = strchr (cptr, '=');
if ((tptr == NULL) || (*tptr == 0))
    return SCPE_ARG;
*tptr++ = 0;
ln = (int32) get_uint (cptr, 10, (DZ_MUXES * DZ_LINES), &r);
if ((r != SCPE_OK) || (ln >= dz_desc.lines))
    return SCPE_ARG;
return tmxr_set_log (NULL, ln, tptr, desc);
}

/* SET NOLOG processor */

t_stat dz_set_nolog (UNIT *uptr, int32 val, char *cptr, void *desc)
{
t_stat r;
int32 ln;

if (cptr == NULL)
    return SCPE_ARG;
ln = (int32) get_uint (cptr, 10, (DZ_MUXES * DZ_LINES), &r);
if ((r != SCPE_OK) || (ln >= dz_desc.lines))
    return SCPE_ARG;
return tmxr_set_nolog (NULL, ln, NULL, desc);
}

/* SHOW LOG processor */

t_stat dz_show_log (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i;

for (i = 0; i < dz_desc.lines; i++) {
    fprintf (st, "line %d: ", i);
    tmxr_show_log (st, NULL, i, desc);
    fprintf (st, "\n");
    }
return SCPE_OK;
}



