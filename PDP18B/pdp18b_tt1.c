/* pdp18b_tt1.c: 18b PDP's second Teletype

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

   tti1		keyboard
   tto1		teleprinter

   22-Dec-02	RMS	Added break support
   02-Nov-02	RMS	Added 7B/8B support
   05-Oct-02	RMS	Added DIB, device number support
   22-Aug-02	RMS	Updated for changes to sim_tmxr
   30-May-02	RMS	Widened POS to 32b
   06-Jan-02	RMS	Added enable/disable support
   30-Dec-01	RMS	Added show statistics, set disconnect
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

#define UNIT_V_8B	(UNIT_V_UF + 0)			/* 8B */
#define UNIT_V_KSR	(UNIT_V_UF + 1)			/* KSR33 */
#define UNIT_8B		(1 << UNIT_V_8B)
#define UNIT_KSR	(1 << UNIT_V_KSR)

extern int32 int_hwre[API_HLVL+1], dev_enb;
extern int32 tmxr_poll;					/* calibrated poll */
TMLN tt1_ldsc = { 0 };					/* line descriptors */
TMXR tt_desc = { 1, 0, 0, &tt1_ldsc };			/* mux descriptor */

DEVICE tti1_dev, tto1_dev;
int32 tti1 (int32 pulse, int32 AC);
int32 tto1 (int32 pulse, int32 AC);
t_stat tti1_svc (UNIT *uptr);
t_stat tto1_svc (UNIT *uptr);
t_stat tti1_reset (DEVICE *dptr);
t_stat tto1_reset (DEVICE *dptr);
t_stat tti1_attach (UNIT *uptr, char *cptr);
t_stat tti1_detach (UNIT *uptr);
t_stat tti1_summ (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat tti1_show (FILE *st, UNIT *uptr, int32 val, void *desc);
void tt1_enbdis (int32 dis);
t_stat tt1_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc);

/* TTI1 data structures

   tti1_dev	TTI1 device descriptor
   tti1_unit	TTI1 unit
   tto1_mod	TTI1 modifier list
   tti1_reg	TTI1 register list
*/

DIB tti1_dib = { DEV_TTI1, 1, NULL, { &tti1 } };

UNIT tti1_unit = { UDATA (&tti1_svc, UNIT_ATTABLE+UNIT_KSR, 0), KBD_POLL_WAIT };

REG tti1_reg[] = {
	{ ORDATA (BUF, tti1_unit.buf, 8) },
	{ FLDATA (INT, int_hwre[API_TTI1], INT_V_TTI1) },
	{ FLDATA (DONE, int_hwre[API_TTI1], INT_V_TTI1) },
	{ DRDATA (POS, tt1_ldsc.rxcnt, 32), PV_LEFT },
	{ DRDATA (TIME, tti1_unit.wait, 24), REG_NZ + PV_LEFT },
	{ ORDATA (DEVNO, tti1_dib.dev, 6), REG_HRO },
	{ NULL }  };

MTAB tti1_mod[] = {
	{ UNIT_KSR+UNIT_8B, UNIT_KSR, "KSR", "KSR", &tt1_set_mode },
	{ UNIT_KSR+UNIT_8B, 0       , "7b" , "7B" , &tt1_set_mode },
	{ UNIT_KSR+UNIT_8B, UNIT_8B , "8b" , "8B" , &tt1_set_mode },
	{ UNIT_ATT, UNIT_ATT, "summary", NULL, NULL, &tti1_summ },
	{ MTAB_XTD | MTAB_VDV, 0, NULL, "DISCONNECT",
		&tmxr_dscln, NULL, &tt_desc },
	{ MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
		NULL, &tti1_show, NULL },
	{ MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
		NULL, &tti1_show, NULL },
	{ MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO", &set_devno, &show_devno },
	{ 0 }  };

DEVICE tti1_dev = {
	"TTI1", &tti1_unit, tti1_reg, tti1_mod,
	1, 10, 31, 1, 8, 8,
	&tmxr_ex, &tmxr_dep, &tti1_reset,
	NULL, &tti1_attach, &tti1_detach,
	&tti1_dib, DEV_DISABLE };

/* TTO1 data structures

   tto1_dev	TTO1 device descriptor
   tto1_unit	TTO1 unit
   tto1_mod	TTO1 modifier list
   tto1_reg	TTO1 register list
*/

DIB tto1_dib = { DEV_TTO1, 1, NULL, { &tto1 } };

UNIT tto1_unit = { UDATA (&tto1_svc, UNIT_KSR, 0), SERIAL_OUT_WAIT };

REG tto1_reg[] = {
	{ ORDATA (BUF, tto1_unit.buf, 8) },
	{ FLDATA (INT, int_hwre[API_TTO1], INT_V_TTO1) },
	{ FLDATA (DONE, int_hwre[API_TTO1], INT_V_TTO1) },
	{ DRDATA (POS, tt1_ldsc.txcnt, 32), PV_LEFT },
	{ DRDATA (TIME, tto1_unit.wait, 24), PV_LEFT },
	{ ORDATA (DEVNO, tto1_dib.dev, 6), REG_HRO },
	{ NULL }  };

MTAB tto1_mod[] = {
	{ UNIT_KSR+UNIT_8B, UNIT_KSR, "KSR", "KSR", &tt1_set_mode },
	{ UNIT_KSR+UNIT_8B, 0       , "7b" , "7B" , &tt1_set_mode },
	{ UNIT_KSR+UNIT_8B, UNIT_8B , "8b" , "8B" , &tt1_set_mode },
	{ MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO", &set_devno, &show_devno },
	{ 0 }  };

DEVICE tto1_dev = {
	"TTO1", &tto1_unit, tto1_reg, tto1_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tto1_reset,
	NULL, NULL, NULL,
	&tto1_dib, DEV_DISABLE };

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
int32 c, newln;

if (tt1_ldsc.conn) {					/* connected? */
	tmxr_poll_rx (&tt_desc);			/* poll for input */
	if (c = tmxr_getc_ln (&tt1_ldsc)) {		/* get char */ 
	    if (c & SCPE_BREAK) uptr->buf = 0;		/* break? */
	    else if (uptr->flags & UNIT_KSR) {		/* KSR? */
		c = c & 0177;
                if (islower (c)) c = toupper (c);
		uptr->buf = c | 0200;  }		/* got char */
	    else uptr->buf = c & ((tti1_unit.flags & UNIT_8B)? 0377: 0177);
	    SET_INT (TTI1);  }				/* set flag */
	sim_activate (uptr, uptr->wait);  }		/* continue poll */
if (uptr->flags & UNIT_ATT) {				/* attached? */
	newln = tmxr_poll_conn (&tt_desc);		/* poll connect */
	if (newln >= 0) {				/* got one? */
	    sim_activate (&tti1_unit, tti1_unit.wait);
	    tt1_ldsc.rcve = 1;  }			/* rcv enabled */ 
	sim_activate (uptr, tmxr_poll);  }		/* sched poll */
return SCPE_OK;
}

/* Reset routine */

t_stat tti1_reset (DEVICE *dptr)
{
tt1_enbdis (dptr->flags & DEV_DIS);			/* sync enables */
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
int32 c;

SET_INT (TTO1);						/* set flag */
c = tto1_unit.buf & 0177;
if (tt1_ldsc.conn) {					/* connected? */
	if (tt1_ldsc.xmte) {				/* tx enabled? */
	    if (tto1_unit.flags & UNIT_KSR) {		/* KSR? */
		c = c & 0177;
		if (islower (c)) c = toupper (c);
		if ((c < 007) || (c > 0137)) c = 0;  }
	    else c = c & ((tto1_unit.flags & UNIT_8B)? 0377: 0177);
	    if (c) tmxr_putc_ln (&tt1_ldsc, c);		/* output char */
	    tmxr_poll_tx (&tt_desc);  }			/* poll xmt */
	else {
	    tmxr_poll_tx (&tt_desc);			/* poll xmt */
	    sim_activate (&tto1_unit, tmxr_poll);	/* wait */
	    return SCPE_OK;  }  }
return SCPE_OK;
}

/* Reset routine */

t_stat tto1_reset (DEVICE *dptr)
{
tt1_enbdis (dptr->flags & DEV_DIS);			/* sync enables */
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

/* Show summary processor */

t_stat tti1_summ (FILE *st, UNIT *uptr, int32 val, void *desc)
{
if (tt1_ldsc.conn) fprintf (st, "connected");
else fprintf (st, "disconnected");
return SCPE_OK;
}

/* SHOW CONN/STAT processor */

t_stat tti1_show (FILE *st, UNIT *uptr, int32 val, void *desc)
{
if (val) tmxr_fconns (st, &tt1_ldsc, -1);
else tmxr_fstats (st, &tt1_ldsc, -1);
return SCPE_OK;
}

/* Enable/disable device */

void tt1_enbdis (int32 dis)
{
if (dis) {
	tti1_dev.flags = tto1_dev.flags | DEV_DIS;
	tto1_dev.flags = tto1_dev.flags | DEV_DIS;  }
else {	tti1_dev.flags = tti1_dev.flags & ~DEV_DIS;
	tto1_dev.flags = tto1_dev.flags & ~DEV_DIS;  }
return;
}

t_stat tt1_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc)
{
tti1_unit.flags = (tti1_unit.flags & ~(UNIT_KSR | UNIT_8B)) | val;
tto1_unit.flags = (tto1_unit.flags & ~(UNIT_KSR | UNIT_8B)) | val;
return SCPE_OK;
}
