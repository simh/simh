/* pdp18b_tt1.c: 18b PDP's second Teletype

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

   tti1		keyboard
   tto1		teleprinter

   30-Nov-01	RMS	Added extended SET/SHOW support
   25-Nov-01	RMS	Revised interrupt structure
   19-Sep-01	RMS	Fixed typo
   17-Sep-01	RMS	Changed to use terminal multiplexor library
   07-Sep-01	RMS	Moved function prototypes
   10-Jun-01	RMS	Cleaned up IOT decoding to reflect hardware
*/

#include "pdp18b_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#define UNIT_V_UC	(UNIT_V_UF + 0)			/* UC only */
#define UNIT_UC		(1 << UNIT_V_UC)

extern int32 int_hwre[API_HLVL+1];
extern int32 tmxr_poll;					/* calibrated poll */
TMLN tt1_ldsc = { 0 };					/* line descriptors */
TMXR tt_desc = { 1, 0, &tt1_ldsc };			/* mux descriptor */

t_stat tti1_svc (UNIT *uptr);
t_stat tto1_svc (UNIT *uptr);
t_stat tti1_reset (DEVICE *dptr);
t_stat tto1_reset (DEVICE *dptr);
t_stat tti1_attach (UNIT *uptr, char *cptr);
t_stat tti1_detach (UNIT *uptr);
t_stat tti1_status (FILE *st, UNIT *uptr, int32 val, void *desc);

/* TTI1 data structures

   tti1_dev	TTI1 device descriptor
   tti1_unit	TTI1 unit
   tto1_mod	TTI1 modifier list
   tti1_reg	TTI1 register list
*/

UNIT tti1_unit = { UDATA (&tti1_svc, UNIT_ATTABLE+UNIT_UC, 0), KBD_POLL_WAIT };

REG tti1_reg[] = {
	{ ORDATA (BUF, tti1_unit.buf, 8) },
	{ FLDATA (INT, int_hwre[API_TTI1], INT_V_TTI1) },
	{ FLDATA (DONE, int_hwre[API_TTI1], INT_V_TTI1) },
	{ FLDATA (UC, tti1_unit.flags, UNIT_V_UC), REG_HRO },
	{ DRDATA (POS, tt1_ldsc.rxcnt, 31), PV_LEFT },
	{ DRDATA (TIME, tti1_unit.wait, 24), REG_NZ + PV_LEFT },
	{ NULL }  };

MTAB tti1_mod[] = {
	{ UNIT_UC, 0, "lower case", "LC", NULL },
	{ UNIT_UC, UNIT_UC, "upper case", "UC", NULL },
	{ UNIT_ATT, UNIT_ATT, "line status", NULL, NULL, &tti1_status },
	{ MTAB_XTD | MTAB_VDV | MTAB_VUN | MTAB_NMO, 0, "LINE", NULL,
		NULL, &tti1_status, NULL },
	{ 0 }  };

DEVICE tti1_dev = {
	"TTI1", &tti1_unit, tti1_reg, tti1_mod,
	1, 10, 31, 1, 8, 8,
	&tmxr_ex, &tmxr_dep, &tti1_reset,
	NULL, &tti1_attach, &tti1_detach };

/* TTO1 data structures

   tto1_dev	TTO1 device descriptor
   tto1_unit	TTO1 unit
   tto1_mod	TTO1 modifier list
   tto1_reg	TTO1 register list
*/

UNIT tto1_unit = { UDATA (&tto1_svc, UNIT_UC, 0), SERIAL_OUT_WAIT };

REG tto1_reg[] = {
	{ ORDATA (BUF, tto1_unit.buf, 8) },
	{ FLDATA (INT, int_hwre[API_TTO1], INT_V_TTO1) },
	{ FLDATA (DONE, int_hwre[API_TTO1], INT_V_TTO1) },
	{ DRDATA (POS, tt1_ldsc.txcnt, 31), PV_LEFT },
	{ DRDATA (TIME, tto1_unit.wait, 24), PV_LEFT },
	{ NULL }  };

MTAB tto1_mod[] = {
	{ UNIT_UC, 0, "lower case", "LC", NULL },
	{ UNIT_UC, UNIT_UC, "upper case", "UC", NULL },
	{ 0 }  };

DEVICE tto1_dev = {
	"TTO1", &tto1_unit, tto1_reg, tto1_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tto1_reset,
	NULL, NULL, NULL };

/* Terminal input: IOT routine */

int32 tti1 (int32 pulse, int32 AC)
{
if (pulse & 001) {					/* KSF1 */
	if (TST_INT (TTI1)) AC = AC | IOT_SKP;  }
if (pulse & 002) {					/* KRB1 */
	CLR_INT (TTI1);					/* clear flag */
	AC= AC | tti1_unit.buf;  }			/* return buffer */
return AC;
}

/* Unit service */

t_stat tti1_svc (UNIT *uptr)
{
int32 temp, newln;

if (tt1_ldsc.conn) {					/* connected? */
	tmxr_poll_rx (&tt_desc);			/* poll for input */
	if (temp = tmxr_getc_ln (&tt1_ldsc)) {		/* get char */ 
		temp = temp & 0177;
		if ((uptr -> flags & UNIT_UC) &&
                     islower (temp)) temp = toupper (temp);
		uptr -> buf = temp | 0200;		/* got char */
		SET_INT (TTI1);  }			/* set flag */
	sim_activate (uptr, uptr -> wait);  }		/* continue poll */
if (uptr -> flags & UNIT_ATT) {				/* attached? */
	newln = tmxr_poll_conn (&tt_desc, uptr);	/* poll connect */
	if (newln >= 0) {				/* got one? */
		sim_activate (&tti1_unit, tti1_unit.wait);
		tt1_ldsc.rcve = 1;  }			/* rcv enabled */ 
	sim_activate (uptr, tmxr_poll);  }		/* sched poll */
return SCPE_OK;
}

/* Reset routine */

t_stat tti1_reset (DEVICE *dptr)
{
tti1_unit.buf = 0;					/* clear buffer */
CLR_INT (TTI1);						/* clear flag */
if (tt1_ldsc.conn) {					/* if conn, */
	sim_activate (&tti1_unit, tti1_unit.wait);	/* activate, */
	tt1_ldsc.rcve = 1;  }				/* enable */
else if (tti1_unit.flags & UNIT_ATT)			/* if attached, */
	sim_activate (&tti1_unit, tmxr_poll);		/* activate */
else sim_cancel (&tti1_unit);				/* else stop */
return SCPE_OK;
}

/* Terminal output: IOT routine */

int32 tto1 (int32 pulse, int32 AC)
{
if (pulse & 001) {					/* TSF */
	if (TST_INT (TTO1)) AC = AC | IOT_SKP;  }
if (pulse & 002) CLR_INT (TTO1);			/* clear flag */
if (pulse & 004) {					/* load buffer */
	sim_activate (&tto1_unit, tto1_unit.wait);	/* activate unit */
	tto1_unit.buf = AC & 0377;  }			/* load buffer */
return AC;
}

/* Unit service */

t_stat tto1_svc (UNIT *uptr)
{
int32 out;

SET_INT (TTO1);						/* set flag */
out = tto1_unit.buf & 0177;
if (tt1_ldsc.conn) {					/* connected? */
	if (tt1_ldsc.xmte) {				/* tx enabled? */
		if (!(tto1_unit.flags & UNIT_UC) ||
	 	     ((out >= 007) && (out <= 0137)))
			tmxr_putc_ln (&tt1_ldsc, out);	/* output char */
		tmxr_poll_tx (&tt_desc);  }		/* poll xmt */
	else {	tmxr_poll_tx (&tt_desc);		/* poll xmt */
		sim_activate (&tto1_unit, tmxr_poll);	/* wait */
		return SCPE_OK;  }  }
return SCPE_OK;
}

/* Reset routine */

t_stat tto1_reset (DEVICE *dptr)
{
tto1_unit.buf = 0;					/* clear buffer */
CLR_INT (TTO1);						/* clear flag */
sim_cancel (&tto1_unit);				/* deactivate unit */
return SCPE_OK;
}

/* Attach routine */

t_stat tti1_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = tmxr_attach (&tt_desc, uptr, cptr);			/* attach */
if (r != SCPE_OK) return r;				/* error */
sim_activate (uptr, tmxr_poll);				/* start poll */
return SCPE_OK;
}

/* Detach routine */

t_stat tti1_detach (UNIT *uptr)
{
t_stat r;

r = tmxr_detach (&tt_desc, uptr);			/* detach */
tt1_ldsc.rcve = 0;					/* disable rcv */
sim_cancel (uptr);					/* stop poll */
return r;
}

/* Status routine */

t_stat tti1_status (FILE *st, UNIT *uptr, int32 val, void *desc)
{
tmxr_fstatus (st, &tt1_ldsc, -1);
return SCPE_OK;
}
