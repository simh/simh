/* pdp8_ttx.c: PDP-8 additional terminals simulator

   Copyright (c) 1993-2002, Robert M Supnik

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

   ttix,ttox	PT08/KL8JA terminal input/output

   22-Dec-02	RMS	Added break support
   02-Nov-02	RMS	Added 7B/8B support
   04-Oct-02	RMS	Added DIB, device number support
   22-Aug-02	RMS	Updated for changes to sim_tmxr.c
   06-Jan-02	RMS	Added device enable/disable support
   30-Dec-01	RMS	Complete rebuild
   30-Nov-01	RMS	Added extended SET/SHOW support

   This module implements four individual serial interfaces similar in function
   to the console.  These interfaces are mapped to Telnet based connections as
   though they were the four lines of a terminal multiplexor.  The connection
   polling mechanism is superimposed onto the keyboard of the first interface.
*/

#include "pdp8_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#define TTX_LINES	4
#define TTX_MASK	(TTX_LINES - 1)

#define UNIT_V_8B	(UNIT_V_UF + 0)			/* 8B */
#define UNIT_V_UC	(UNIT_V_UF + 1)			/* upper case */
#define UNIT_8B		(1 << UNIT_V_8B)
#define UNIT_UC		(1 << UNIT_V_UC)

#define TTX_GETLN(x)	(((x) >> 4) & TTX_MASK)

extern int32 int_req, int_enable, dev_done, stop_inst;

uint8 ttix_buf[TTX_LINES] = { 0 };			/* input buffers */
uint8 ttox_buf[TTX_LINES] = { 0 };			/* output buffers */
int32 ttx_tps = 100;					/* polls per second */
TMLN ttx_ldsc[TTX_LINES] = { 0 };			/* line descriptors */
TMXR ttx_desc = {					/* mux descriptor */
	TTX_LINES, 0, 0, &ttx_ldsc[0], &ttx_ldsc[1], &ttx_ldsc[2], &ttx_ldsc[3] };

DEVICE ttix_dev, ttox_dev;
int32 ttix (int32 IR, int32 AC);
int32 ttox (int32 IR, int32 AC);
t_stat ttix_svc (UNIT *uptr);
t_stat ttix_reset (DEVICE *dptr);
t_stat ttox_svc (UNIT *uptr);
t_stat ttox_reset (DEVICE *dptr);
t_stat ttx_attach (UNIT *uptr, char *cptr);
t_stat ttx_detach (UNIT *uptr);
t_stat ttx_summ (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat ttx_show (FILE *st, UNIT *uptr, int32 val, void *desc);
void ttx_enbdis (int32 dis);

/* TTIx data structures

   ttix_dev	TTIx device descriptor
   ttix_unit	TTIx unit descriptor
   ttix_reg	TTIx register list
   ttix_mod	TTIx modifiers list
*/

DIB ttix_dib = { DEV_KJ8, 8,
		 { &ttix, &ttox, &ttix, &ttox, &ttix, &ttox, &ttix, &ttox } };

UNIT ttix_unit = { UDATA (&ttix_svc, UNIT_ATTABLE, 0), KBD_POLL_WAIT };

REG ttix_reg[] = {
	{ BRDATA (BUF, ttix_buf, 8, 8, TTX_LINES) },
	{ GRDATA (DONE, dev_done, 8, TTX_LINES, INT_V_TTI1) },
	{ GRDATA (ENABLE, int_enable, 8, TTX_LINES, INT_V_TTI1) },
	{ GRDATA (INT, int_req, 8, TTX_LINES, INT_V_TTI1) },
	{ DRDATA (TIME, ttix_unit.wait, 24), REG_NZ + PV_LEFT },
	{ DRDATA (TPS, ttx_tps, 10), REG_NZ + PV_LEFT },
	{ ORDATA (DEVNUM, ttix_dib.dev, 6), REG_HRO },
	{ NULL }  };

MTAB ttix_mod[] = {
	{ UNIT_ATT, UNIT_ATT, "summary", NULL, NULL, &ttx_summ },
	{ MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
		&tmxr_dscln, NULL, &ttx_desc },
	{ MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
		NULL, &ttx_show, NULL },
	{ MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
		NULL, &ttx_show, NULL },
	{ MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
		&set_dev, &show_dev, NULL },
	{ 0 }  };

DEVICE ttix_dev = {
	"TTIX", &ttix_unit, ttix_reg, ttix_mod,
	1, 10, 31, 1, 8, 8,
	&tmxr_ex, &tmxr_dep, &ttix_reset,
	NULL, &ttx_attach, &ttx_detach,
	&ttix_dib, DEV_DISABLE };

/* TTOx data structures

   ttox_dev	TTOx device descriptor
   ttox_unit	TTOx unit descriptor
   ttox_reg	TTOx register list
*/

UNIT ttox_unit[] = {
	{ UDATA (&ttox_svc, UNIT_UC, 0), SERIAL_OUT_WAIT },
	{ UDATA (&ttox_svc, UNIT_UC, 0), SERIAL_OUT_WAIT },
	{ UDATA (&ttox_svc, UNIT_UC, 0), SERIAL_OUT_WAIT },
	{ UDATA (&ttox_svc, UNIT_UC, 0), SERIAL_OUT_WAIT }  };

REG ttox_reg[] = {
	{ BRDATA (BUF, ttox_buf, 8, 8, TTX_LINES) },
	{ GRDATA (DONE, dev_done, 8, TTX_LINES, INT_V_TTO1) },
	{ GRDATA (ENABLE, int_enable, 8, TTX_LINES, INT_V_TTO1) },
	{ GRDATA (INT, int_req, 8, TTX_LINES, INT_V_TTO1) },
	{ URDATA (TIME, ttox_unit[0].wait, 10, 24, 0,
		  TTX_LINES, PV_LEFT) },
	{ NULL }  };

MTAB ttox_mod[] = {
	{ UNIT_UC+UNIT_8B, UNIT_UC, "UC", "UC", NULL },
	{ UNIT_UC+UNIT_8B, 0       , "7b" , "7B" , NULL },
	{ UNIT_UC+UNIT_8B, UNIT_8B , "8b" , "8B" , NULL },
	{ 0 }  };

DEVICE ttox_dev = {
	"TTOX", ttox_unit, ttox_reg, ttox_mod,
	4, 10, 31, 1, 8, 8,
	NULL, NULL, &ttox_reset, 
	NULL, NULL, NULL,
	NULL, DEV_DISABLE };

/* Terminal input: IOT routine */

int32 ttix (int32 inst, int32 AC)
{
int32 pulse = inst & 07;				/* IOT pulse */
int32 ln = TTX_GETLN (inst);				/* line # */
int32 itti = (INT_TTI1 << ln);				/* rx intr */
int32 itto = (INT_TTO1 << ln);				/* tx intr */

switch (pulse) {					/* case IR<9:11> */
case 0: 						/* KCF */
	dev_done = dev_done & ~itti;			/* clear flag */
	int_req = int_req & ~itti;
	break;
case 1:							/* KSF */
	return (dev_done & itti)? IOT_SKP + AC: AC;
case 2:							/* KCC */
	dev_done = dev_done & ~itti;			/* clear flag */
	int_req = int_req & ~itti;
	return 0;					/* clear AC */
case 4:							/* KRS */
	return (AC | ttix_buf[ln]);			/* return buf */
case 5:							/* KIE */
	if (AC & 1) int_enable = int_enable | (itti + itto);
	else int_enable = int_enable & ~(itti + itto);
	int_req = INT_UPDATE;				/* update intr */
	break;
case 6:							/* KRB */
	dev_done = dev_done & ~itti;			/* clear flag */
	int_req = int_req & ~itti;
	return ttix_buf[ln];				/* return buf */
default:
	return (stop_inst << IOT_V_REASON) + AC;  }	/* end switch */
return AC;
}

/* Unit service */

t_stat ttix_svc (UNIT *uptr)
{
int32 ln, c, temp;

if ((uptr->flags & UNIT_ATT) == 0) return SCPE_OK;	/* attached? */
temp = sim_rtcn_calb (ttx_tps, TMR_TTX);		/* calibrate */
sim_activate (uptr, temp);				/* continue poll */
ln = tmxr_poll_conn (&ttx_desc);			/* look for connect */
if (ln >= 0) {						/* got one? */
    ttx_ldsc[ln].rcve = 1;  }				/* rcv enabled */ 
tmxr_poll_rx (&ttx_desc);				/* poll for input */
for (ln = 0; ln < TTX_LINES; ln++) {			/* loop thru lines */
	if (ttx_ldsc[ln].conn) {			/* connected? */
	    if (temp = tmxr_getc_ln (&ttx_ldsc[ln])) {	/* get char */
		if (temp & SCPE_BREAK) c = 0;		/* break? */
		else if (ttox_unit[ln].flags & UNIT_UC) {	/* UC? */
		    c = temp & 0177;
		    if (islower (c)) c = toupper (c);  }
		else c = temp & ((ttox_unit[ln].flags & UNIT_8B)? 0377: 0177);
		ttix_buf[ln] = c;
		dev_done = dev_done | (INT_TTI1 << ln);
		int_req = INT_UPDATE;  }  }  }
return SCPE_OK;
}

/* Reset routine */

t_stat ttix_reset (DEVICE *dptr)
{
int32 t, ln, itto;

ttx_enbdis (dptr->flags & DEV_DIS);			/* sync enables */
if (ttix_unit.flags & UNIT_ATT) {			/* if attached, */
	if (!sim_is_active (&ttix_unit)) {
	    t = sim_rtcn_init (ttix_unit.wait, TMR_TTX);
	    sim_activate (&ttix_unit, t);  }  }		/* activate */
else sim_cancel (&ttix_unit);				/* else stop */
for (ln = 0; ln < TTX_LINES; ln++) {			/* for all lines */
	ttix_buf[ln] = 0;				/* clear buf, */
	itto = (INT_TTI1 << ln);			/* interrupt */
	dev_done = dev_done & ~itto;			/* clr done, int */
	int_req = int_req & ~itto;
	int_enable = int_enable | itto;  }		/* set enable */
return SCPE_OK;
}

/* Terminal output: IOT routine */

int32 ttox (int32 inst, int32 AC)
{
int32 pulse = inst & 07;				/* pulse */
int32 ln = TTX_GETLN (inst);				/* line # */
int32 itti = (INT_TTI1 << ln);				/* rx intr */
int32 itto = (INT_TTO1 << ln);				/* tx intr */

switch (pulse) {					/* case IR<9:11> */
case 0: 						/* TLF */
	dev_done = dev_done | itto;			/* set flag */
	int_req = INT_UPDATE;				/* update intr */
	break;
case 1:							/* TSF */
	return (dev_done & itto)? IOT_SKP + AC: AC;
case 2:							/* TCF */
	dev_done = dev_done & ~itto;			/* clear flag */
	int_req = int_req & ~itto;			/* clear intr */
	break;
case 5:							/* SPI */
	return (int_req & (itti | itto))? IOT_SKP + AC: AC;
case 6:							/* TLS */
	dev_done = dev_done & ~itto;			/* clear flag */
	int_req = int_req & ~itto;			/* clear int req */
case 4:							/* TPC */
	sim_activate (&ttox_unit[ln], ttox_unit[ln].wait);	/* activate */
	ttox_buf[ln] = AC & 0377;			/* load buffer */
	break;
default:
	return (stop_inst << IOT_V_REASON) + AC;  }	/* end switch */
return AC;
}

/* Unit service */

t_stat ttox_svc (UNIT *uptr)
{
int32 c, ln = uptr - ttox_unit;				/* line # */

if (ttx_desc.ldsc[ln]->conn) {				/* connected? */
	if (ttx_desc.ldsc[ln]->xmte) {			/* tx enabled? */
	    TMLN *lp = ttx_desc.ldsc[ln];		/* get line */
	    if (ttox_unit[ln].flags & UNIT_UC) {	/* UC mode? */
		c = ttox_buf[ln] & 0177;		/* get char */
		if (islower (c)) c = toupper (c);  }
	    else c = ttox_buf[ln] & ((ttox_unit[ln].flags & UNIT_8B)? 0377: 0177);
	    tmxr_putc_ln (lp, c);			/* output char */
	    tmxr_poll_tx (&ttx_desc);  }		/* poll xmt */
	else {
	    tmxr_poll_tx (&ttx_desc);			/* poll xmt */
	    sim_activate (uptr, ttox_unit[ln].wait);	/* wait */
	    return SCPE_OK;  }  }
dev_done = dev_done | (INT_TTO1 << ln);			/* set done */
int_req = INT_UPDATE;					/* update intr */
return SCPE_OK;
}

/* Reset routine */

t_stat ttox_reset (DEVICE *dptr)
{
int32 ln, itto;

ttx_enbdis (dptr->flags & DEV_DIS);			/* sync enables */
for (ln = 0; ln < TTX_LINES; ln++) {			/* for all lines */
	ttox_buf[ln] = 0;				/* clear buf */
	itto = (INT_TTO1 << ln);			/* interrupt */
	dev_done = dev_done & ~itto;			/* clr done, int */
	int_req = int_req & ~itto;
	int_enable = int_enable | itto;			/* set enable */
	sim_cancel (&ttox_unit[ln]);  }			/* deactivate */
return SCPE_OK;
}

/* Attach master unit */

t_stat ttx_attach (UNIT *uptr, char *cptr)
{
int32 t;
t_stat r;

r = tmxr_attach (&ttx_desc, uptr, cptr);		/* attach */
if (r != SCPE_OK) return r;				/* error */
t = sim_rtcn_init (ttix_unit.wait, TMR_TTX);		/* init calib */
sim_activate (uptr, t);					/* start poll */
return SCPE_OK;
}

/* Detach master unit */

t_stat ttx_detach (UNIT *uptr)
{
int32 i;
t_stat r;

r = tmxr_detach (&ttx_desc, uptr);			/* detach */
for (i = 0; i < TTX_LINES; i++) {			/* all lines, */
	ttx_desc.ldsc[i]->rcve = 0;			/* disable rcv */
	sim_cancel (&ttox_unit[i]);  }			/* stop poll */
return r;
}

/* Show summary processor */

t_stat ttx_summ (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i, t;

for (i = t = 0; i < TTX_LINES; i++) t = t + (ttx_ldsc[i].conn != 0);
if (t == 1) fprintf (st, "1 connection");
else fprintf (st, "%d connections", t);
return SCPE_OK;
}

/* SHOW CONN/STAT processor */

t_stat ttx_show (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i;

for (i = 0; (i < TTX_LINES) && (ttx_ldsc[i].conn == 0); i++) ;
if (i < TTX_LINES) {
	for (i = 0; i < TTX_LINES; i++) {
	    if (ttx_ldsc[i].conn) 
		if (val) tmxr_fconns (st, &ttx_ldsc[i], i);
		else tmxr_fstats (st, &ttx_ldsc[i], i);  }  }
else fprintf (st, "all disconnected\n");
return SCPE_OK;
}

/* Enable/disable device */

void ttx_enbdis (int32 dis)
{
if (dis) {
	ttix_dev.flags = ttox_dev.flags | DEV_DIS;
	ttox_dev.flags = ttox_dev.flags | DEV_DIS;  }
else {	ttix_dev.flags = ttix_dev.flags & ~DEV_DIS;
	ttox_dev.flags = ttox_dev.flags & ~DEV_DIS;  }
return;
}
