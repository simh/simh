/* pdp8_ttx.c: PDP-8 additional terminals simulator

   Copyright (c) 1993-2001, Robert M Supnik

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
#define UNIT_V_UC	(UNIT_V_UF + 0)			/* UC only */
#define UNIT_UC		(1 << UNIT_V_UC)
#define TTX_GETLN(x)	(((x) >> 4) & TTX_MASK)

extern int32 int_req, int_enable, dev_done, stop_inst;
extern int32 tmxr_poll;					/* calibrated poll */
TMLN tt1_ldsc = { 0 };					/* line descriptors */
TMLN tt2_ldsc = { 0 };					/* line descriptors */
TMLN tt3_ldsc = { 0 };					/* line descriptors */
TMLN tt4_ldsc = { 0 };					/* line descriptors */

TMXR ttx_desc = {					/* mux descriptor */
	TTX_LINES, 0, &tt1_ldsc, &tt2_ldsc, &tt3_ldsc, &tt4_ldsc };

t_stat ttix_svc (UNIT *uptr);
t_stat ttix_reset (DEVICE *dptr);
t_stat ttox_svc (UNIT *uptr);
t_stat ttox_reset (DEVICE *dptr);
t_stat ttx_attach (UNIT *uptr, char *cptr);
t_stat ttx_detach (UNIT *uptr);
t_stat ttx_status (FILE *st, UNIT *uptr, int32 val, void *desc);

/* TTIx data structures

   ttix_dev	TTIx device descriptor
   ttix_unit	TTIx unit descriptor
   ttix_reg	TTIx register list
   ttix_mod	TTIx modifiers list
*/

MTAB ttix_mod[] = {
	{ UNIT_UC, 0, "lower case", "LC", NULL },
	{ UNIT_UC, UNIT_UC, "upper case", "UC", NULL },
	{ UNIT_ATT, UNIT_ATT, "line status", NULL, NULL, &ttx_status },
	{ MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "LINESTATUS", NULL,
		NULL, &ttx_status, NULL },
	{ 0 }  };

UNIT ttix_unit[] = {
	{ UDATA (&ttix_svc, UNIT_ATTABLE+UNIT_UC, 0), KBD_POLL_WAIT },
	{ UDATA (&ttix_svc, UNIT_UC, 0), KBD_POLL_WAIT },
	{ UDATA (&ttix_svc, UNIT_UC, 0), KBD_POLL_WAIT },
	{ UDATA (&ttix_svc, UNIT_UC, 0), KBD_POLL_WAIT }  };

#define tti1_unit	ttix_unit[0]
#define tti2_unit	ttix_unit[1]
#define tti3_unit	ttix_unit[2]
#define tti4_unit	ttix_unit[3]

REG tti1_reg[] = {
	{ ORDATA (BUF, tti1_unit.buf, 8) },
	{ FLDATA (DONE, dev_done, INT_V_TTI1) },
	{ FLDATA (ENABLE, int_enable, INT_V_TTI1) },
	{ FLDATA (INT, int_req, INT_V_TTI1) },
	{ DRDATA (POS, tt1_ldsc.rxcnt, 32), PV_LEFT },
	{ DRDATA (TIME, tti1_unit.wait, 24), REG_NZ + PV_LEFT },
	{ FLDATA (UC, tti1_unit.flags, UNIT_V_UC), REG_HRO },
	{ NULL }  };

DEVICE tti1_dev = {
	"TTI1", &tti1_unit, tti1_reg, ttix_mod,
	1, 10, 31, 1, 8, 8,
	&tmxr_ex, &tmxr_dep, &ttix_reset,
	NULL, &ttx_attach, &ttx_detach };

REG tti2_reg[] = {
	{ ORDATA (BUF, tti2_unit.buf, 8) },
	{ FLDATA (DONE, dev_done, INT_V_TTI2) },
	{ FLDATA (ENABLE, int_enable, INT_V_TTI2) },
	{ FLDATA (INT, int_req, INT_V_TTI2) },
	{ DRDATA (POS, tt2_ldsc.rxcnt, 32), PV_LEFT },
	{ DRDATA (TIME, tti2_unit.wait, 24), REG_NZ + PV_LEFT },
	{ FLDATA (UC, tti2_unit.flags, UNIT_V_UC), REG_HRO },
	{ NULL }  };

DEVICE tti2_dev = {
	"TTI2", &tti2_unit, tti2_reg, ttix_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ttix_reset,
	NULL, NULL, NULL };

REG tti3_reg[] = {
	{ ORDATA (BUF, tti3_unit.buf, 8) },
	{ FLDATA (DONE, dev_done, INT_V_TTI3) },
	{ FLDATA (ENABLE, int_enable, INT_V_TTI3) },
	{ FLDATA (INT, int_req, INT_V_TTI3) },
	{ DRDATA (POS, tt3_ldsc.rxcnt, 32), PV_LEFT },
	{ DRDATA (TIME, tti3_unit.wait, 24), REG_NZ + PV_LEFT },
	{ FLDATA (UC, tti3_unit.flags, UNIT_V_UC), REG_HRO },
	{ NULL }  };

DEVICE tti3_dev = {
	"TTI3", &tti3_unit, tti3_reg, ttix_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ttix_reset,
	NULL, NULL, NULL };

REG tti4_reg[] = {
	{ ORDATA (BUF, tti4_unit.buf, 8) },
	{ FLDATA (DONE, dev_done, INT_V_TTI4) },
	{ FLDATA (ENABLE, int_enable, INT_V_TTI4) },
	{ FLDATA (INT, int_req, INT_V_TTI4) },
	{ DRDATA (POS, tt4_ldsc.rxcnt, 32), PV_LEFT },
	{ DRDATA (TIME, tti4_unit.wait, 24), REG_NZ + PV_LEFT },
	{ FLDATA (UC, tti4_unit.flags, UNIT_V_UC), REG_HRO },
	{ NULL }  };

DEVICE tti4_dev = {
	"TTI4", &tti4_unit, tti4_reg, ttix_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ttix_reset,
	NULL, NULL, NULL };

/* TTOx data structures

   ttox_dev	TTOx device descriptor
   ttox_unit	TTOx unit descriptor
   ttox_reg	TTOx register list
*/

UNIT ttox_unit[] = {
	{ UDATA (&ttox_svc, 0, 0), SERIAL_OUT_WAIT },
	{ UDATA (&ttox_svc, 0, 0), SERIAL_OUT_WAIT },
	{ UDATA (&ttox_svc, 0, 0), SERIAL_OUT_WAIT },
	{ UDATA (&ttox_svc, 0, 0), SERIAL_OUT_WAIT }  };

#define tto1_unit ttox_unit[0]
#define tto2_unit ttox_unit[0]
#define tto3_unit ttox_unit[0]
#define tto4_unit ttox_unit[0]

REG tto1_reg[] = {
	{ ORDATA (BUF, tto1_unit.buf, 8) },
	{ FLDATA (DONE, dev_done, INT_V_TTO1) },
	{ FLDATA (ENABLE, int_enable, INT_V_TTO1) },
	{ FLDATA (INT, int_req, INT_V_TTO1) },
	{ DRDATA (POS, tt1_ldsc.txcnt, 32), PV_LEFT },
	{ DRDATA (TIME, tto1_unit.wait, 24), PV_LEFT },
	{ NULL }  };

DEVICE tto1_dev = {
	"TTO1", &tto1_unit, tto1_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ttox_reset, 
	NULL, NULL, NULL };

REG tto2_reg[] = {
	{ ORDATA (BUF, tto2_unit.buf, 8) },
	{ FLDATA (DONE, dev_done, INT_V_TTO2) },
	{ FLDATA (ENABLE, int_enable, INT_V_TTO2) },
	{ FLDATA (INT, int_req, INT_V_TTO2) },
	{ DRDATA (POS, tt2_ldsc.txcnt, 32), PV_LEFT },
	{ DRDATA (TIME, tto2_unit.wait, 24), PV_LEFT },
	{ NULL }  };

DEVICE tto2_dev = {
	"TTO2", &tto2_unit, tto2_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ttox_reset, 
	NULL, NULL, NULL };

REG tto3_reg[] = {
	{ ORDATA (BUF, tto3_unit.buf, 8) },
	{ FLDATA (DONE, dev_done, INT_V_TTO3) },
	{ FLDATA (ENABLE, int_enable, INT_V_TTO3) },
	{ FLDATA (INT, int_req, INT_V_TTO3) },
	{ DRDATA (POS, tt3_ldsc.txcnt, 32), PV_LEFT },
	{ DRDATA (TIME, tto3_unit.wait, 24), PV_LEFT },
	{ NULL }  };

DEVICE tto3_dev = {
	"TTO3", &tto3_unit, tto3_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ttox_reset, 
	NULL, NULL, NULL };

REG tto4_reg[] = {
	{ ORDATA (BUF, tto4_unit.buf, 8) },
	{ FLDATA (DONE, dev_done, INT_V_TTO4) },
	{ FLDATA (ENABLE, int_enable, INT_V_TTO4) },
	{ FLDATA (INT, int_req, INT_V_TTO4) },
	{ DRDATA (POS, tt4_ldsc.txcnt, 32), PV_LEFT },
	{ DRDATA (TIME, tto4_unit.wait, 24), PV_LEFT },
	{ NULL }  };

DEVICE tto4_dev = {
	"TTO4", &tto4_unit, tto4_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ttox_reset, 
	NULL, NULL, NULL };

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
	return (AC | ttix_unit[ln].buf);		/* return buf */
case 5:							/* KIE */
	if (AC & 1) int_enable = int_enable | (itti + itto);
	else int_enable = int_enable & ~(itti + itto);
	int_req = INT_UPDATE;				/* update intr */
	break;
case 6:							/* KRB */
	dev_done = dev_done & ~itti;			/* clear flag */
	int_req = int_req & ~itti;
	return ttix_unit[ln].buf;			/* return buf */
default:
	return (stop_inst << IOT_V_REASON) + AC;  }	/* end switch */
return AC;
}

/* Unit service */

t_stat ttix_svc (UNIT *uptr)
{
int32 temp, newln;
int32 ln = uptr - ttix_unit;				/* line # */
int32 itti = (INT_TTI1 << ln);				/* rx intr */

if (ttx_desc.ldsc[ln] -> conn) {				/* connected? */
	tmxr_poll_rx (&ttx_desc);			/* poll for input */
	if (temp = tmxr_getc_ln (ttx_desc.ldsc[ln])) {	/* get char */ 
		temp = temp & 0177;			/* mask to 7b */
		if ((uptr -> flags & UNIT_UC) && islower (temp))
			temp = toupper (temp);
		uptr -> buf = temp | 0200;		/* Teletype code */
		dev_done = dev_done | itti;		/* set done */
		int_req = INT_UPDATE;  }		/* update intr */
	sim_activate (uptr, uptr -> wait);  }		/* continue poll */
if (uptr -> flags & UNIT_ATT) {				/* attached? */
	newln = tmxr_poll_conn (&ttx_desc, uptr);	/* poll connect */
	if (newln >= 0) {				/* got one? */
		sim_activate (&ttix_unit[newln], ttix_unit[newln].wait);
		ttx_desc.ldsc[newln] -> rcve = 1;  }	/* rcv enabled */ 
	sim_activate (uptr, tmxr_poll);  }		/* sched poll */
return SCPE_OK;
}

/* Reset routine */

t_stat ttix_reset (DEVICE *dptr)
{
UNIT *uptr = dptr -> units;				/* unit */
int32 ln = uptr - ttix_unit;				/* line # */
int32 itti = (INT_TTI1 << ln);				/* rx intr */

uptr -> buf = 0;					/* clr buf */
dev_done = dev_done & ~itti;				/* clr done, int */
int_req = int_req & ~itti;
int_enable = int_enable | itti;				/* set enable */
if (ttx_desc.ldsc[ln] -> conn) {			/* if conn, */
	sim_activate (uptr, uptr -> wait);		/* activate, */
	ttx_desc.ldsc[ln] -> rcve = 1;  }		/* enable */
else if (uptr -> flags & UNIT_ATT)			/* if attached, */
	sim_activate (uptr, tmxr_poll);			/* activate */
else sim_cancel (uptr);					/* else stop */
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
	ttox_unit[ln].buf = AC & 0377;			/* load buffer */
	break;
default:
	return (stop_inst << IOT_V_REASON) + AC;  }	/* end switch */
return AC;
}

/* Unit service */

t_stat ttox_svc (UNIT *uptr)
{
int32 ln = uptr - ttox_unit;				/* line # */
int32 itto = (INT_TTO1 << ln);				/* tx intr */

if (ttx_desc.ldsc[ln] -> conn) {			/* connected? */
	if (ttx_desc.ldsc[ln] -> xmte) {		/* tx enabled? */
		TMLN *lp = ttx_desc.ldsc[ln];		/* get line */
		tmxr_putc_ln (lp, uptr -> buf & 0177);	/* output char */
		tmxr_poll_tx (&ttx_desc);  }		/* poll xmt */
	else {	tmxr_poll_tx (&ttx_desc);		/* poll xmt */
		sim_activate (uptr, tmxr_poll);		/* wait */
		return SCPE_OK;  }  }
dev_done = dev_done | itto;				/* set done */
int_req = INT_UPDATE;					/* update intr */
return SCPE_OK;
}

/* Reset routine */

t_stat ttox_reset (DEVICE *dptr)
{
UNIT *uptr = dptr -> units;				/* unit */
int32 ln = uptr - ttox_unit;				/* line # */
int32 itto = (INT_TTO1 << ln);				/* tx intr */

uptr -> buf = 0;					/* clr buf */
dev_done = dev_done & ~itto;				/* clr done, int */
int_req = int_req & ~itto;
int_enable = int_enable | itto;				/* set enable */
sim_cancel (uptr);					/* deactivate */
return SCPE_OK;
}

/* Attach master unit */

t_stat ttx_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = tmxr_attach (&ttx_desc, uptr, cptr);		/* attach */
if (r != SCPE_OK) return r;				/* error */
sim_activate (uptr, tmxr_poll);				/* start poll */
return SCPE_OK;
}

/* Detach master unit */

t_stat ttx_detach (UNIT *uptr)
{
int32 i;
t_stat r;

r = tmxr_detach (&ttx_desc, uptr);			/* detach */
for (i = 0; i < TTX_LINES; i++) {			/* all lines, */
	ttx_desc.ldsc[i] -> rcve = 0;			/* disable rcv */
	sim_cancel (&ttix_unit[i]);  }			/* stop poll */
return SCPE_OK;
}

/* Status */

t_stat ttx_status (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i;

fprintf (st, "line status:");
for (i = 0; (i < TTX_LINES) && (ttx_desc.ldsc[i] -> conn == 0); i++) ;
if (i < TTX_LINES) {
	for (i = 0; i < TTX_LINES; i++) {
		if (ttx_desc.ldsc[i] -> conn) 
			tmxr_fstatus (st, ttx_desc.ldsc[i], i);  }  }
else fprintf (st, " all disconnected");
return SCPE_OK;
}
