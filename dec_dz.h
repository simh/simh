/* dec_dz.c: DZ11 terminal multiplexor simulator

   Copyright (c) 2001, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   Based on the original DZ11 simulator by Thord Nilson, as updated by
   Arthur Krewat.

   dz		DZ11 terminal multiplexor

   03-Dec-01	RMS	Modified for extended SET/SHOW
   09-Nov-01	RMS	Added VAX support
   20-Oct-01	RMS	Moved getchar from sim_tmxr, changed interrupt
			logic to use tmxr_rqln
   06-Oct-01	RMS	Fixed bug in carrier detect logic
   03-Oct-01	RMS	Added support for BSD-style "ringless" modems
   27-Sep-01	RMS	Fixed bug in xmte initialization
   17-Sep-01	RMS	Added separate autodisconnect switch
   16-Sep-01	RMS	Fixed modem control bit offsets

   This file is intended to be included in a shell routine that invokes
   a simulator definition file:

   pdp11_dz.c	= pdp11_defs.h + dec_dz.h
   pdp10_dz.c	= pdp10_defs.h + dec_dz.h
   vax_dz.c	= vax_defs.h + dec_dz.h
*/

#include "sim_sock.h"
#include "sim_tmxr.h"

#if !defined (DZ_RDX)
#define DZ_RDX		8
#endif

#define DZ_LNOMASK	(DZ_LINES - 1)			/* mask for lineno */
#define DZ_LMASK	((1 << DZ_LINES) - 1)		/* mask of lines */
#define DZ_SILO_ALM	16				/* silo alarm level */

/* DZCSR - 160100 - control/status register */

#define CSR_MAINT	0000010				/* maint - NI */
#define CSR_CLR		0000020				/* clear */
#define CSR_MSE		0000040				/* master scan enb */
#define CSR_RIE		0000100				/* rcv int enb */
#define CSR_RDONE	0000200				/* rcv done - RO */
#define CSR_V_TLINE	8				/* xmit line - RO */
#define CSR_TLINE	(DZ_LNOMASK << CSR_V_TLINE)
#define CSR_SAE		0010000				/* silo alm enb */
#define CSR_SA		0020000				/* silo alm - RO */
#define CSR_TIE		0040000				/* xmit int enb */
#define CSR_TRDY	0100000				/* xmit rdy - RO */
#define CSR_RW		(CSR_MSE | CSR_RIE | CSR_SAE | CSR_TIE)
#define CSR_MBZ		(0004003 | CSR_CLR | CSR_MAINT)

#define CSR_GETTL(x)	(((x) >> CSR_V_TLINE) & DZ_LNOMASK)
#define CSR_PUTTL(x,y)	x = ((x) & ~CSR_TLINE) | (((y) & DZ_LNOMASK) << CSR_V_TLINE)

/* DZRBUF - 160102 - receive buffer, read only */

#define RBUF_CHAR	0000377				/* rcv char */
#define RBUF_V_RLINE	8				/* rcv line */
#define RBUF_PARE	0010000				/* parity err - NI */
#define RBUF_FRME	0020000				/* frame err - NI */
#define RBUF_OVRE	0040000				/* overrun err - NI */
#define RBUF_VALID	0100000				/* rcv valid */
#define RBUF_MBZ	0004000

/* DZLPR - 160102 - line parameter register, write only, word access only */

#define LPR_V_LINE	0				/* line */
#define LPR_LPAR	0007770				/* line pars - NI */
#define LPR_RCVE	0010000				/* receive enb */
#define LPR_GETLN(x)	(((x) >> LPR_V_LINE) & DZ_LNOMASK)

/* DZTCR - 160104 - transmission control register */

#define TCR_V_XMTE	0				/* xmit enables */
#define TCR_V_DTR	8				/* DTRs */

/* DZMSR - 160106 - modem status register, read only */

#define MSR_V_RI	0				/* ring indicators */
#define MSR_V_CD	8				/* carrier detect */

/* DZTDR - 160106 - transmit data, write only */

#define TDR_CHAR	0000377				/* xmit char */
#define TDR_V_TBR	8				/* xmit break - NI */

extern int32 IREQ (HLVL);
extern int32 sim_switches;
extern FILE *sim_log;
extern int32 tmxr_poll;					/* calibrated delay */
int32 dz_csr = 0;					/* csr */
int32 dz_rbuf = 0;					/* rcv buffer */
int32 dz_lpr = 0;					/* line param */
int32 dz_tcr = 0;					/* xmit control */
int32 dz_msr = 0;					/* modem status */
int32 dz_tdr = 0;					/* xmit data */
int32 dz_mctl = 0;					/* modem ctrl enabled */
int32 dz_auto = 0;					/* autodiscon enabled */
int32 dz_sa_enb = 1;					/* silo alarm enabled */
int32 dz_enb = 1;					/* device enable */
TMLN dz_ldsc[DZ_LINES] = {				/* line descriptors */
	{ 0 }, { 0 }, { 0 }, { 0 },
	{ 0 }, { 0 }, { 0 }, { 0 }  };
TMXR dz_desc = {					/* mux descriptor */
	DZ_LINES, 0,
	&dz_ldsc[0], &dz_ldsc[1], &dz_ldsc[2], &dz_ldsc[3],
	&dz_ldsc[4], &dz_ldsc[5], &dz_ldsc[6], &dz_ldsc[7] };

t_stat dz_svc (UNIT *uptr);
t_stat dz_reset (DEVICE *dptr);
t_stat dz_attach (UNIT *uptr, char *cptr);
t_stat dz_detach (UNIT *uptr);
t_stat dz_clear (t_bool flag);
int32 dz_getchar (TMXR *mp);
void dz_update_rcvi (void);
void dz_update_xmti (void);
t_stat dz_status (FILE *st, UNIT *uptr, void *desc);

/* DZ data structures

   dz_dev	DZ device descriptor
   dz_unit	DZ unit list
   dz_reg	DZ register list
*/

UNIT dz_unit = { UDATA (&dz_svc, UNIT_ATTABLE, 0) };

REG dz_reg[] = {
	{ GRDATA (CSR, dz_csr, DZ_RDX, 16, 0) },
	{ GRDATA (RBUF, dz_rbuf, DZ_RDX, 16, 0) },
	{ GRDATA (LPR, dz_lpr, DZ_RDX, 16, 0) },
	{ GRDATA (TCR, dz_tcr, DZ_RDX, 16, 0) },
	{ GRDATA (MSR, dz_msr, DZ_RDX, 16, 0) },
	{ GRDATA (TDR, dz_tdr, DZ_RDX, 16, 0) },
	{ FLDATA (SAENB, dz_sa_enb, 0) },
	{ FLDATA (MDMCTL, dz_mctl, 0) },
	{ FLDATA (AUTODS, dz_auto, 0) },
	{ DRDATA (RPOS0, dz_ldsc[0].rxcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (TPOS0, dz_ldsc[0].txcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (RPOS1, dz_ldsc[1].rxcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (TPOS1, dz_ldsc[1].txcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (RPOS2, dz_ldsc[2].rxcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (TPOS2, dz_ldsc[2].txcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (RPOS3, dz_ldsc[3].rxcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (TPOS3, dz_ldsc[3].txcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (RPOS4, dz_ldsc[4].rxcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (TPOS4, dz_ldsc[4].txcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (RPOS5, dz_ldsc[5].rxcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (TPOS5, dz_ldsc[5].txcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (RPOS6, dz_ldsc[6].rxcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (TPOS6, dz_ldsc[6].txcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (RPOS7, dz_ldsc[7].rxcnt, 32), PV_LEFT+REG_RO },
	{ DRDATA (TPOS7, dz_ldsc[7].txcnt, 32), PV_LEFT+REG_RO },
	{ FLDATA (*DEVENB, dz_enb, 0) },
	{ NULL }  };

MTAB dz_mod[] = {
	{ UNIT_ATT, UNIT_ATT, "line status", NULL, NULL, &dz_status },
	{ MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "LINESTATUS", NULL,
		NULL, &dz_status, NULL },
	{ 0 }  };

DEVICE dz_dev = {
	"DZ", &dz_unit, dz_reg, dz_mod,
	1, DZ_RDX, 13, 1, DZ_RDX, 8,
	&tmxr_ex, &tmxr_dep, &dz_reset,
	NULL, &dz_attach, &dz_detach };

/* IO dispatch routines, I/O addresses 17760100 - 17760107 */

t_stat dz_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 03) {				/* case on PA<2:1> */
case 00:						/* CSR */
	*data = dz_csr = dz_csr & ~CSR_MBZ;
	break;
case 01:						/* RBUF */
	dz_csr = dz_csr & ~CSR_SA;			/* clr silo alarm */
	if (dz_csr & CSR_MSE) {				/* scanner on? */
		dz_rbuf = dz_getchar (&dz_desc);	/* get top of silo */
		if (!dz_rbuf) dz_sa_enb = 1;		/* empty? re-enable */
		tmxr_poll_rx (&dz_desc);		/* poll input */
		dz_update_rcvi ();  }			/* update rx intr */
	else {	dz_rbuf = 0;				/* no data */
		dz_update_rcvi ();  }			/* no rx intr */
	*data = dz_rbuf;
	break;
case 02:						/* TCR */
	*data = dz_tcr;
	break;
case 03:						/* MSR */
	*data = dz_msr;
	break;  }
return SCPE_OK;
}

t_stat dz_wr (int32 data, int32 PA, int32 access)
{
int32 i, line;
TMLN *lp;

switch ((PA >> 1) & 03) {				/* case on PA<2:1> */
case 00:						/* CSR */
	if (access == WRITEB) data = (PA & 1)?
		(dz_csr & 0377) | (data << 8): (dz_csr & ~0377) | data;
	if (data & CSR_CLR) dz_clear (FALSE);
	if (data & CSR_MSE) sim_activate (&dz_unit, tmxr_poll);
	else {	sim_cancel (&dz_unit);
		dz_csr = dz_csr & ~(CSR_SA | CSR_RDONE | CSR_TRDY);  }
	if ((data & CSR_RIE) == 0) CLR_INT (DZRX);
	else if (((dz_csr & CSR_IE) == 0) &&		/* RIE 0->1? */
	         ((dz_csr & CSR_SAE)? (dz_csr & CSR_SA): (dz_csr & CSR_RDONE)))
		SET_INT (DZRX);
	if ((data & CSR_TIE) == 0) CLR_INT (DZTX);
	else if (((dz_csr & CSR_TIE) == 0) && (dz_csr & CSR_TRDY))
		SET_INT (DZTX);
	dz_csr = (dz_csr & ~CSR_RW) | (data & CSR_RW);
	break;
case 01:						/* LPR */
	dz_lpr = data;
	line = LPR_GETLN (dz_lpr);			/* get line */
	lp = dz_desc.ldsc[line];			/* get line desc */
	if (dz_lpr & LPR_RCVE) lp -> rcve = 1;		/* rcv enb? on */
	else lp -> rcve = 0;				/* else line off */
	tmxr_poll_rx (&dz_desc);			/* poll input */
	dz_update_rcvi ();				/* update rx intr */
	break;
case 02:						/* TCR */
	if (access == WRITEB) data = (PA & 1)?
		(dz_tcr & 0377) | (data << 8): (dz_tcr & ~0377) | data;
	if (dz_mctl) {					/* modem ctl? */
		dz_msr = dz_msr | ((data & 0177400) &	/* dcd |= dtr & ring */
			((dz_msr & DZ_LMASK) << MSR_V_CD));
		dz_msr = dz_msr & ~(data >> TCR_V_DTR);	/* ring = ring & ~dtr */
		if (dz_auto) {				/* auto disconnect? */
		    int32 drop;
		    drop = (dz_tcr & ~data) >> TCR_V_DTR; /* drop = dtr & ~data */
		    for (i = 0; i < DZ_LINES; i++) {	/* drop hangups */
			lp = dz_desc.ldsc[i];		/* get line desc */
			if (lp -> conn && (drop & (1 << i))) {
			    tmxr_msg (lp -> conn, "\r\nLine hangup\r\n");
			    tmxr_reset_ln (lp);		/* reset line, cdet */
			    dz_msr = dz_msr & ~(1 << (i + MSR_V_CD));
			    }				/* end if drop */
			}				/* end for */
		    }					/* end if auto */
		}					/* end if modem */
	dz_tcr = data;
	tmxr_poll_tx (&dz_desc);			/* poll output */
	dz_update_xmti ();				/* update int */
	break;
case 03:						/* TDR */
	if (PA & 1) {					/* odd byte? */
		dz_tdr = (dz_tdr & 0377) | (data << 8);	/* just save */
		break;  }
	dz_tdr = data;
	if (dz_csr & CSR_MSE) {				/* enabled? */
		line = CSR_GETTL (dz_csr);		/* get xmit line */
		lp = dz_desc.ldsc[line];		/* get line desc */
		tmxr_putc_ln (lp, dz_tdr & 0177);	/* store char */
		tmxr_poll_tx (&dz_desc);		/* poll output */
		dz_update_xmti ();  }			/* update int */
	break;  }
return SCPE_OK;
}

/* Unit service routine

   The DZ11 polls to see if asynchronous activity has occurred and now
   needs to be processed.  The polling interval is controlled by the clock
   simulator, so for most environments, it is calibrated to real time.
   Typical polling intervals are 50-60 times per second.
*/

t_stat dz_svc (UNIT *uptr)
{
int32 newln;

if (dz_csr & CSR_MSE) {					/* enabled? */
	newln = tmxr_poll_conn (&dz_desc, uptr);	/* poll connect */
	if ((newln >= 0) && dz_mctl) {			/* got a live one? */
		if (dz_tcr & (1 << (newln + TCR_V_DTR)))	/* DTR set? */
			dz_msr = dz_msr | (1 << (newln + MSR_V_CD));
		else dz_msr = dz_msr | (1 << newln);  }	/* set ring */
	tmxr_poll_rx (&dz_desc);			/* poll input */
	dz_update_rcvi ();				/* upd rcv intr */
	tmxr_poll_tx (&dz_desc);			/* poll output */
	dz_update_xmti ();				/* upd xmt intr */
	sim_activate (uptr, tmxr_poll);  }		/* reactivate */
return SCPE_OK;
}

/* Get first available character, if any */

int32 dz_getchar (TMXR *mp)
{
int32 i, val;

for (i = val = 0; (i < mp -> lines) && (val == 0); i++) { /* loop thru lines */
	val = tmxr_getc_ln (mp -> ldsc[i]);		/* test for input */
	if (val) val = val | (i << RBUF_V_RLINE);	/* or in line # */
	}						/* end for */
return val;
}

/* Update receive interrupts */

void dz_update_rcvi (void)
{
int32 i, scnt;
TMLN *lp;

for (i = scnt = 0; i < DZ_LINES; i++) {			/* poll lines */
	lp = dz_desc.ldsc[i];				/* get line desc */
	scnt = scnt + tmxr_rqln (lp);			/* sum buffers */
	if (dz_mctl && !lp -> conn)			/* if disconn */
		dz_msr = dz_msr & ~(1 << (i + MSR_V_CD)); /* reset car det */
	}
if (scnt && (dz_csr & CSR_MSE)) {			/* input & enabled? */
	dz_csr = dz_csr | CSR_RDONE;			/* set done */
	if (dz_sa_enb && (scnt >= DZ_SILO_ALM)) {	/* alm enb & cnt hi? */
		dz_csr = dz_csr | CSR_SA;		/* set status */
		dz_sa_enb = 0;  }  }			/* disable alarm */
else dz_csr = dz_csr & ~CSR_RDONE;			/* no, clear done */
if ((dz_csr & CSR_RIE) &&				/* int enable */
    ((dz_csr & CSR_SAE)? (dz_csr & CSR_SA): (dz_csr & CSR_RDONE)))
	SET_INT (DZRX);					/* and alm/done? */
else CLR_INT (DZRX);					/* no, clear int */
return;
}

/* Update transmit interrupts */

void dz_update_xmti (void)
{
int32 linemask, i, j;

linemask = dz_tcr & DZ_LMASK;				/* enabled lines */
dz_csr = dz_csr & ~CSR_TRDY;				/* assume not rdy */
for (i = 0, j = CSR_GETTL (dz_csr); i < DZ_LINES; i++) {
	j = (j + 1) & DZ_LNOMASK;
	if ((linemask & (1 << j)) && dz_desc.ldsc[j] -> xmte) {
		CSR_PUTTL (dz_csr, j);			/* update CSR */
		dz_csr = dz_csr | CSR_TRDY;		/* set xmt rdy */
		break;  }  }
if ((dz_csr & CSR_TIE) && (dz_csr & CSR_TRDY))		/* ready plus int? */
	 SET_INT (DZTX);
else CLR_INT (DZTX);					/* no int req */
return;
}

/* Device reset */

t_stat dz_clear (t_bool flag)
{
int32 i;

dz_csr = 0;						/* clear CSR */
dz_rbuf = 0;						/* silo empty */
dz_lpr = 0;						/* no params */
if (flag) dz_tcr = 0;					/* INIT? clr all */
else dz_tcr = dz_tcr & ~0377;				/* else save dtr */
dz_tdr = 0;
dz_sa_enb = 1;
CLR_INT (DZRX);
CLR_INT (DZTX);
sim_cancel (&dz_unit);					/* no polling */
for (i = 0; i < DZ_LINES; i++) {
	if (!dz_desc.ldsc[i] -> conn) dz_desc.ldsc[i] -> xmte = 1;
	dz_desc.ldsc[i] -> rcve = 0;  }			/* clr rcv enb */
return SCPE_OK;
}

t_stat dz_reset (DEVICE *dptr)
{
return dz_clear (TRUE);
}

/* Attach */

t_stat dz_attach (UNIT *uptr, char *cptr)
{
t_stat r;
extern int32 sim_switches;

dz_mctl = dz_auto = 0;					/* modem ctl off */
r = tmxr_attach (&dz_desc, uptr, cptr);			/* attach mux */
if (r != SCPE_OK) return r;				/* error? */
if (sim_switches & SWMASK ('M')) {			/* modem control? */
	dz_mctl = 1;
	printf ("Modem control activated\n");
	if (sim_log) fprintf (sim_log, "Modem control activated\n");
	if (sim_switches & SWMASK ('A')) {		/* autodisconnect? */
		dz_auto = 1;
		printf ("Auto disconnect activated\n");
		if (sim_log) fprintf (sim_log, "Auto disconnect activated\n");
		}
	}
return SCPE_OK;
}

/* Detach */

t_stat dz_detach (UNIT *uptr)
{
return tmxr_detach (&dz_desc, uptr);
}

/* Status */

t_stat dz_status (FILE *st, UNIT *uptr, void *desc)
{
int32 i;

fprintf (st, "line status:");
for (i = 0; (i < DZ_LINES) && (dz_desc.ldsc[i] -> conn == 0); i++) ;
if (i < DZ_LINES) {
	for (i = 0; i < DZ_LINES; i++) {
		if (dz_desc.ldsc[i] -> conn) 
			tmxr_fstatus (st, dz_desc.ldsc[i], i);  }  }
else fprintf (st, " all disconnected");
return SCPE_OK;
}
