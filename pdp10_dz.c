/* pdp10_dz_stub.c: DZ11 terminal multiplexor simulator stub 

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

   dz		DZ11 terminal multiplexor (stub)

   This version of the DZ11 is a stub to allow operating systems to play
   with the device registers.  It is required for ITS, and not harmful to
   TOPS-10 or TOPS-20.
*/

#include "pdp10_defs.h"

#define DZ_LINES	8				/* lines per DZ11 */
#define DZ_LMASK	(DZ_LINES - 1)
#define DZ_SILO_ALM	16				/* silo alarm level */
#define MAXBUF		128				/* buffer size */

/* DZCSR - 160100 - control/status register */

#define CSR_MAINT	0000010				/* maint - NI */
#define CSR_CLR		0000020				/* clear */
#define CSR_MSE		0000040				/* master scan enb */
#define CSR_RIE		0000100				/* rcv int enb */
#define CSR_RDONE	0000200				/* rcv done - RO */
#define CSR_V_TLINE	8				/* xmit line - RO */
#define CSR_TLINE	(DZ_LMASK << CSR_V_TLINE)
#define CSR_SAE		0010000				/* silo alm enb */
#define CSR_SA		0020000				/* silo alm - RO */
#define CSR_TIE		0040000				/* xmit int enb */
#define CSR_TRDY	0100000				/* xmit rdy - RO */
#define CSR_RW		(CSR_MSE | CSR_RIE | CSR_SAE | CSR_TIE)
#define CSR_MBZ		(0004003 | CSR_CLR | CSR_MAINT)

#define CSR_GETTL(x)	(((x) >> CSR_V_TLINE) & DZ_LMASK)
#define CSR_PUTTL(x,y)	x = ((x) & ~CSR_TLINE) | (((y) & DZ_LMASK) << CSR_V_TLINE)

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
#define LPR_GETLN(x)	(((x) >> LPR_V_LINE) & DZ_LMASK)

/* DZTCR - 160104 - transmission control register */

#define TCR_V_XMTE	0				/* xmit enables */
#define TCR_V_DTR	7				/* DTRs */

/* DZMSR - 160106 - modem status register, read only */

#define MSR_V_RI	0				/* ring indicators */
#define MSR_V_CD	7				/* carrier detect */

/* DZTDR - 160106 - transmit data, write only */

#define TDR_CHAR	0000377				/* xmit char */
#define TDR_V_TBR	7				/* xmit break - NI */

extern int32 int_req, dev_enb;
int32 dz_csr = 0;					/* csr */
int32 dz_rbuf = 0;					/* rcv buffer */
int32 dz_lpr = 0;					/* line param */
int32 dz_tcr = 0;					/* xmit control */
int32 dz_msr = 0;					/* modem status */
int32 dz_tdr = 0;					/* xmit data */
int32 dz_mctl = 0;					/* modem ctrl enab */
int32 dz_sa_enb = 1;					/* silo alarm enabled */

t_stat dz_reset (DEVICE *dptr);
t_stat dz_clear (t_bool flag);

/* DZ data structures

   dz_dev	DZ device descriptor
   dz_unit	DZ unit list
   dz_reg	DZ register list
*/

UNIT dz_unit = { UDATA (NULL, 0, 0) };

REG dz_reg[] = {
	{ ORDATA (CSR, dz_csr, 16) },
	{ ORDATA (RBUF, dz_rbuf, 16) },
	{ ORDATA (LPR, dz_lpr, 16) },
	{ ORDATA (TCR, dz_tcr, 16) },
	{ ORDATA (MSR, dz_msr, 16) },
	{ ORDATA (TDR, dz_tdr, 16) },
	{ FLDATA (MDMCTL, dz_mctl, 0) },
	{ FLDATA (SAENB, dz_sa_enb, 0) },
	{ FLDATA (*DEVENB, dev_enb, INT_V_DZ0RX), REG_HRO },
	{ NULL }  };

DEVICE dz_dev = {
	"DZ", &dz_unit, dz_reg, NULL,
	1, 8, 13, 1, 8, 8,
	NULL, NULL, &dz_reset,
	NULL, NULL, NULL };

/* IO dispatch routines, I/O addresses 17760100 - 17760107 */

t_stat dz0_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 03) {				/* case on PA<2:1> */
case 00:						/* CSR */
	*data = dz_csr = dz_csr & ~CSR_MBZ;
	break;
case 01:						/* RBUF */
	dz_csr = dz_csr & ~CSR_SA;			/* clr silo alarm */
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

t_stat dz0_wr (int32 data, int32 PA, int32 access)
{
switch ((PA >> 1) & 03) {				/* case on PA<2:1> */
case 00:						/* CSR */
	if (access == WRITEB) data = (PA & 1)?
		(dz_csr & 0377) | (data << 8): (dz_csr & ~0377) | data;
	dz_csr = (dz_csr & ~CSR_RW) | (data & CSR_RW);
	break;
case 01:						/* LPR */
	dz_lpr = data;
	break;
case 02:						/* TCR */
	if (access == WRITEB) data = (PA & 1)?
		(dz_tcr & 0377) | (data << 8): (dz_tcr & ~0377) | data;
	dz_tcr = data;
	break;
case 03:						/* TDR */
	if (PA & 1) {					/* odd byte? */
		dz_tdr = (dz_tdr & 0377) | (data << 8);	/* just save */
		break;  }
	dz_tdr = data;
	break;  }
return SCPE_OK;
}

/* Device reset */

t_stat dz_reset (DEVICE *dptr)
{
dz_csr = 0;						/* clear CSR */
dz_rbuf = 0;						/* silo empty */
dz_lpr = 0;						/* no params */
dz_tcr = 0;						/* clr all */
dz_tdr = 0;
dz_sa_enb = 1;
int_req = int_req & ~(INT_DZ0RX | INT_DZ0TX);		/* clear int */
sim_cancel (&dz_unit);					/* no polling */
return SCPE_OK;
}
