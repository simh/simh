/* pdp18b_lp.c: 18b PDP's line printer simulator

   Copyright (c) 1993-2003, Robert M Supnik

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

   lp62		(PDP-4)    Type 62 line printer
   lp647	(PDP-7,9)  Type 647 line printer
   lp09		(PDP-9,15) LP09 line printer
   lp15		(PDP-15)   LP15 line printer

   05-Feb-03	RMS	Added LP09, fixed conditionalization
   05-Oct-02	RMS	Added DIB, device number support
   30-May-02	RMS	Widened POS to 32b
   03-Feb-02	RMS	Fixed typo (found by Robert Alan Byer)
   25-Nov-01	RMS	Revised interrupt structure
   19-Sep-01	RMS	Fixed bug in 647
   13-Feb-01	RMS	Revised for register arrays
   15-Feb-01	RMS	Fixed 3 cycle data break sequence
   30-Oct-00	RMS	Standardized register naming
   20-Aug-98	RMS	Fixed compilation problem in BeOS
   03-Jan-97	RMS	Fixed bug in Type 62 state handling
*/

#include "pdp18b_defs.h"
extern int32 int_hwre[API_HLVL+1];

#if defined (TYPE62)

/* Type 62 line printer */

#define LP62_BSIZE	120				/* line size */
#define BPTR_MAX	40				/* pointer max */
#define BPTR_MASK	077				/* buf ptr max */

int32 lp62_iot = 0;					/* saved state */
int32 lp62_stopioe = 0;
int32 lp62_bp = 0;					/* buffer ptr */
char lp62_buf[LP62_BSIZE + 1] = { 0 };
static const char lp62_trans[64] = {
	' ','1','2','3','4','5','6','7','8','9','\'','~','#','V','^','<',
	'0','/','S','T','U','V','W','X','Y','Z','"',',','>','^','-','?',
	'o','J','K','L','M','N','O','P','Q','R','$','=','-',')','-','(',
	'_','A','B','C','D','E','F','G','H','I','*','.','+',']','|','[' };
static const char *lp62_cc[] = {
	"\n",
	"\n\n",
	"\n\n\n",
	"\n\n\n\n\n\n",
	"\n\n\n\n\n\n\n\n\n\n\n",
	"\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",
	"\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",
	"\f" };

DEVICE lp62_dev;
int32 lp62_65 (int32 pulse, int32 AC);
int32 lp62_66 (int32 pulse, int32 AC);
int32 lp62_iors (void);
t_stat lp62_svc (UNIT *uptr);
t_stat lp62_reset (DEVICE *dptr);

/* Type 62 LPT data structures

   lp62_dev	LPT device descriptor
   lp62_unit	LPT unit
   lp62_reg	LPT register list
*/

DIB lp62_dib = { DEV_LPT, 2, &lp62_iors, { &lp62_65, &lp62_66 } };

UNIT lp62_unit = {
	UDATA (&lp62_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG lp62_reg[] = {
	{ ORDATA (BUF, lp62_unit.buf, 8) },
	{ FLDATA (INT, int_hwre[API_LPT], INT_V_LPT) },
	{ FLDATA (DONE, int_hwre[API_LPT], INT_V_LPT) },
	{ FLDATA (SPC, int_hwre[API_LPTSPC], INT_V_LPTSPC) },
	{ DRDATA (BPTR, lp62_bp, 6) },
	{ ORDATA (STATE, lp62_iot, 6), REG_HRO },
	{ DRDATA (POS, lp62_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, lp62_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, lp62_stopioe, 0) },
	{ BRDATA (LBUF, lp62_buf, 8, 8, LP62_BSIZE) },
	{ ORDATA (DEVNO, lp62_dib.dev, 6), REG_HRO },
	{ NULL }  };

MTAB lp62_mod[] = {
	{ MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO", &set_devno, &show_devno },
	{ 0 } };

DEVICE lp62_dev = {
	"LPT", &lp62_unit, lp62_reg, lp62_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &lp62_reset,
	NULL, NULL, NULL,
	&lp62_dib, DEV_DISABLE };

/* IOT routines */

int32 lp62_65 (int32 pulse, int32 AC)
{
int32 i;

if ((pulse & 01) && TST_INT (LPT)) AC = IOT_SKP | AC;	/* LPSF */
if (pulse & 02) {
	int32 sb = pulse & 060;				/* subopcode */
	if (sb == 000) CLR_INT (LPT);			/* LPCF */
	if ((sb == 040) && (lp62_bp < BPTR_MAX)) {	/* LPLD */
	    i = lp62_bp * 3;				/* cvt to chr ptr */
	    lp62_buf[i] = lp62_trans[(AC >> 12) & 077];
	    lp62_buf[i + 1] = lp62_trans[(AC >> 6) & 077];
	    lp62_buf[i + 2] = lp62_trans[AC & 077];
	    lp62_bp = (lp62_bp + 1) & BPTR_MASK;  }
	}
if (pulse & 04)						/* LPSE */
	sim_activate (&lp62_unit, lp62_unit.wait);	/* activate */
return AC;
}

int32 lp62_66 (int32 pulse, int32 AC)
{
if ((pulse & 01) && TST_INT (LPTSPC))			/* LSSF */
	AC = IOT_SKP | AC;
if (pulse & 02) CLR_INT (LPTSPC);			/* LSCF */
if (pulse & 04) {					/* LSPR */
	lp62_iot = 020 | (AC & 07);			/* space, no print */
	sim_activate (&lp62_unit, lp62_unit.wait);  }	/* activate */
return AC;
}

/* Unit service, printer is in one of three states

   lp62_iot = 0		write buffer to file, set state to
   lp62_iot = 10	write cr, then write buffer to file
   lp62_iot = 2x	space command x, then set state to 0
*/

t_stat lp62_svc (UNIT *uptr)
{
int32 i;

if (lp62_iot & 020) {					/* space? */
	SET_INT (LPTSPC);				/* set flag */
	if ((uptr->flags & UNIT_ATT) == 0)		/* attached? */
	    return IORETURN (lp62_stopioe, SCPE_UNATT);
	fputs (lp62_cc[lp62_iot & 07], uptr->fileref);	/* print cctl */
	if (ferror (uptr->fileref)) {			/* error? */
	    perror ("LPT I/O error");
	    clearerr (uptr->fileref);
	    return SCPE_IOERR;  }
	lp62_iot = 0;  }				/* clear state */
else {	SET_INT (LPT);					/* print */
	if ((uptr->flags & UNIT_ATT) == 0)		/* attached? */
	    return IORETURN (lp62_stopioe, SCPE_UNATT);
	if (lp62_iot & 010) fputc ('\r', uptr->fileref);
	fputs (lp62_buf, uptr->fileref);		/* print buffer */
	if (ferror (uptr->fileref)) {			/* test error */
	    perror ("LPT I/O error");
	    clearerr (uptr->fileref);
	    return SCPE_IOERR;  }
	lp62_bp = 0;
	for (i = 0; i <= LP62_BSIZE; i++) lp62_buf[i] = 0; /* clear buffer */
	lp62_iot = 010;  }				/* set state */
uptr->pos = ftell (uptr->fileref);			/* update position */
return SCPE_OK;
}

/* Reset routine */

t_stat lp62_reset (DEVICE *dptr)
{
int32 i;

CLR_INT (LPT);						/* clear intrs */
CLR_INT (LPTSPC);
sim_cancel (&lp62_unit);				/* deactivate unit */
lp62_bp = 0;						/* clear buffer ptr */
for (i = 0; i <= LP62_BSIZE; i++) lp62_buf[i] = 0;	/* clear buffer */
lp62_iot = 0;						/* clear state */
return SCPE_OK;
}

/* IORS routine */

int32 lp62_iors (void)
{
return	(TST_INT (LPT)? IOS_LPT: 0) |
	(TST_INT (LPTSPC)? IOS_LPT1: 0);
}

#endif

#if defined (TYPE647)

/* Type 647 line printer */

#define LP647_BSIZE	120				/* line size */

int32 lp647_don = 0;					/* ready */
int32 lp647_ie = 1;					/* int enable */
int32 lp647_err = 0;					/* error */
int32 lp647_iot = 0;					/* saved state */
int32 lp647_stopioe = 0;
int32 lp647_bp = 0;					/* buffer ptr */
char lp647_buf[LP647_BSIZE] = { 0 };
static const char *lp647_cc[] = {
	"\n",
	"\n\n",
	"\n\n\n",
	"\n\n\n\n\n\n",
	"\n\n\n\n\n\n\n\n\n\n\n",
	"\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",
	"\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",
	"\f" };

DEVICE lp647_dev;
int32 lp647_65 (int32 pulse, int32 AC);
int32 lp647_66 (int32 pulse, int32 AC);
int32 lp647_iors (void);
t_stat lp647_svc (UNIT *uptr);
t_stat lp647_reset (DEVICE *dptr);
t_stat lp647_attach (UNIT *uptr, char *cptr);
t_stat lp647_detach (UNIT *uptr);

/* Type 647 LPT data structures

   lp647_dev	LPT device descriptor
   lp647_unit	LPT unit
   lp647_reg	LPT register list
*/

DIB lp647_dib = { DEV_LPT, 2, &lp647_iors, { &lp647_65, &lp647_66 } };

UNIT lp647_unit = {
	UDATA (&lp647_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG lp647_reg[] = {
	{ ORDATA (BUF, lp647_unit.buf, 8) },
	{ FLDATA (INT, int_hwre[API_LPT], INT_V_LPT) },
	{ FLDATA (DONE, lp647_don, 0) },
#if defined (PDP9)
	{ FLDATA (ENABLE, lp647_ie, 0) },
#endif
	{ FLDATA (ERR, lp647_err, 0) },
	{ DRDATA (BPTR, lp647_bp, 7) },
	{ ORDATA (SCMD, lp647_iot, 6), REG_HRO },
	{ DRDATA (POS, lp647_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, lp647_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, lp647_stopioe, 0) },
	{ BRDATA (LBUF, lp647_buf, 8, 8, LP647_BSIZE) },
	{ ORDATA (DEVNO, lp647_dib.dev, 6), REG_HRO },
	{ NULL }  };

MTAB lp647_mod[] = {
	{ MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO", &set_devno, &show_devno },
	{ 0 } };

DEVICE lp647_dev = {
	"LPT", &lp647_unit, lp647_reg, lp647_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &lp647_reset,
	NULL, &lp647_attach, &lp647_detach,
	&lp647_dib, DEV_DISABLE };

/* IOT routines */

int32 lp647_65 (int32 pulse, int32 AC)
{
int32 i, sb;

sb = pulse & 060;					/* subcode */
if ((pulse & 01) && lp647_don) AC = IOT_SKP | AC;	/* LPSF */
if (pulse & 02) {					/* pulse 02 */
	lp647_don = 0;					/* clear done */
	CLR_INT (LPT);					/* clear int req */
	if (sb == 000) {				/* LPCB */
	    for (i = 0; i < LP647_BSIZE; i++) lp647_buf[i] = 0;
	    lp647_bp = 0;				/* reset buf ptr */
	    lp647_don = 1;				/* set done */
	    if (lp647_ie) SET_INT (LPT);  }		/* set int */
	}
if (pulse & 004) {					/* LPDI */
	switch (sb) {					/* case on subcode */
	case 000:					/* LPDI */
#if defined (PDP9)
	    lp647_ie = 0;				/* clear int enable */
	    CLR_INT (LPT);				/* clear int req */
#endif
	    break;
	case 040:					/* LPB3 */
	    if (lp647_bp < LP647_BSIZE) {
		lp647_buf[lp647_bp] = lp647_buf[lp647_bp] | ((AC >> 12) & 077);
		lp647_bp = lp647_bp + 1;  }
	case 020:					/* LPB2 */
	    if (lp647_bp < LP647_BSIZE) {
		lp647_buf[lp647_bp] = lp647_buf[lp647_bp] | ((AC >> 6) & 077);
		lp647_bp = lp647_bp + 1;  }
	case 060:					/* LPB1 */
	    if (lp647_bp < LP647_BSIZE) {
		lp647_buf[lp647_bp] = lp647_buf[lp647_bp] | (AC & 077);
		lp647_bp = lp647_bp + 1;  }
	    lp647_don = 1;				/* set done */
	    if (lp647_ie) SET_INT (LPT);		/* set int */
	    break;  }					/* end case */
	}
return AC;
}

int32 lp647_66 (int32 pulse, int32 AC)
{
if ((pulse & 01) && lp647_err) AC = IOT_SKP | AC;	/* LPSE */
if (pulse & 02) {					/* LPCF */
	lp647_don = 0;					/* clear done, int */
	CLR_INT (LPT);  }
if (pulse & 04) {
	if ((pulse & 060) < 060) {			/* LPLS, LPPB, LPPS */
	    lp647_iot = (pulse & 060) | (AC & 07);	/* save parameters */
	    sim_activate (&lp647_unit, lp647_unit.wait);  }	/* activate */
#if defined (PDP9)
	else {						/* LPEI */
	    lp647_ie = 1;				/* set int enable */
	    if (lp647_don) SET_INT (LPT);  }
#endif
	}
return AC;
}

/* Unit service.  lp647_iot specifies the action to be taken

   lp647_iot = 0x		print only
   lp647_iot = 2x		space only, x is spacing command
   lp647_iot = 4x		print then space, x is spacing command
*/

t_stat lp647_svc (UNIT *uptr)
{
int32 i;
char pbuf[LP647_BSIZE + 1];

lp647_don = 1;
if (lp647_ie) SET_INT (LPT);				/* set flag */
if ((uptr->flags & UNIT_ATT) == 0) {			/* not attached? */
	lp647_err = 1;					/* set error */
	return IORETURN (lp647_stopioe, SCPE_UNATT);  }
if ((lp647_iot & 020) == 0) {				/* print? */
	for (i = 0; i < lp647_bp; i++) 			/* translate buffer */
	    pbuf[i] = lp647_buf[i] | ((lp647_buf[i] >= 040)? 0: 0100);
	if ((lp647_iot & 060) == 0) pbuf[lp647_bp++] = '\r';
	for (i = 0; i < LP647_BSIZE; i++) lp647_buf[i] = 0;	/* clear buffer */
	fwrite (pbuf, 1, lp647_bp, uptr->fileref);	/* print buffer */
	if (ferror (uptr->fileref)) {			/* error? */
	    perror ("LPT I/O error");
	    clearerr (uptr->fileref);
	    lp647_bp = 0;
	    return SCPE_IOERR;  }
	lp647_bp = 0;  }				/* clear buffer ptr */
if (lp647_iot & 060) {					/* space? */
	fputs (lp647_cc[lp647_iot & 07], uptr->fileref);	/* write cctl */
	if (ferror (uptr->fileref)) {			/* error? */
	    perror ("LPT I/O error");
	    clearerr (uptr->fileref);
	    return SCPE_IOERR;  }  }
uptr->pos = ftell (uptr->fileref);			/* update position */
return SCPE_OK;
}

/* Reset routine */

t_stat lp647_reset (DEVICE *dptr)
{
int32 i;

lp647_don = 0;						/* clear done */
lp647_err = (lp647_unit.flags & UNIT_ATT)? 0: 1;	/* clr/set error */
lp647_ie = 1;						/* set enable */
CLR_INT (LPT);						/* clear int */
sim_cancel (&lp647_unit);				/* deactivate unit */
lp647_bp = 0;						/* clear buffer ptr */
lp647_iot = 0;						/* clear state */
for (i = 0; i < LP647_BSIZE; i++) lp647_buf[i] = 0;	/* clear buffer */
return SCPE_OK;
}

/* IORS routine */

int32 lp647_iors (void)
{
return (lp647_don? IOS_LPT: 0) | (lp647_err? IOS_LPT1: 0);
}

/* Attach routine */

t_stat lp647_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
lp647_err = (lp647_unit.flags & UNIT_ATT)? 0: 1;	/* clr/set error */
return reason;
}

/* Detach routine */

t_stat lp647_detach (UNIT *uptr)
{
lp647_err = 1;
return detach_unit (uptr);
}

#endif

#if defined (LP09)

/* LP09 line printer */

#define LP09_BSIZE	132				/* line size */

int32 lp09_don = 0;					/* ready */
int32 lp09_err = 0;					/* error */
int32 lp09_ie = 1;					/* int enable */
int32 lp09_stopioe = 0;
DEVICE lp09_dev;

int32 lp09_66 (int32 pulse, int32 AC);
int32 lp09_iors (void);
t_stat lp09_svc (UNIT *uptr);
t_stat lp09_reset (DEVICE *dptr);
t_stat lp09_attach (UNIT *uptr, char *cptr);
t_stat lp09_detach (UNIT *uptr);

/* LP09 LPT data structures

   lp09_dev	LPT device descriptor
   lp09_unit	LPT unit
   lp09_reg	LPT register list
*/

DIB lp09_dib = { DEV_LPT, 2, &lp09_iors, { NULL, &lp09_66 } };

UNIT lp09_unit = {
	UDATA (&lp09_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG lp09_reg[] = {
	{ ORDATA (BUF, lp09_unit.buf, 7) },
	{ FLDATA (INT, int_hwre[API_LPT], INT_V_LPT) },
	{ FLDATA (DONE, lp09_don, 0) },
	{ FLDATA (ENABLE, lp09_ie, 0) },
	{ FLDATA (ERR, lp09_err, 0) },
	{ DRDATA (POS, lp09_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, lp09_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, lp09_stopioe, 0) },
	{ ORDATA (DEVNO, lp09_dib.dev, 6), REG_HRO },
	{ NULL }  };

MTAB lp09_mod[] = {
	{ MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO", &set_devno, &show_devno },
	{ 0 } };

DEVICE lp09_dev = {
	"LP9", &lp09_unit, lp09_reg, lp09_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &lp09_reset,
	NULL, &lp09_attach, &lp09_detach,
	&lp09_dib, DEV_DISABLE | DEV_DIS };

/* IOT routines */

int32 lp09_66 (int32 pulse, int32 AC)
{
int32 sb = pulse & 060;					/* subopcode */

if (pulse & 001) {
	if ((sb == 000) && lp09_don) AC = IOT_SKP | AC;	/* LSDF */
	if ((sb == 020) && lp09_err) AC = IOT_SKP | AC;	/* LSEF */
	}
if (pulse & 002) {
	if (sb == 000) {				/* LSCF */
	    lp09_don = 0;				/* clear done, int */
	    CLR_INT (LPT);  }
	else if (sb == 020) {				/* LPLD */
	    lp09_don = 0;				/* clear done, int */
	    CLR_INT (LPT);
	    lp09_unit.buf = AC & 0177;			/* load char */
	    if ((lp09_unit.buf == 015) || (lp09_unit.buf == 014) ||
		(lp09_unit.buf == 012))
		sim_activate (&lp09_unit, lp09_unit.wait);
	    else AC = AC | (lp09_svc (&lp09_unit) << IOT_V_REASON);  }
	}
if (pulse & 004) {
	if (sb == 000) {				/* LIOF */
	    lp09_ie = 0;				/* clear int enab */
	    CLR_INT (LPT);  }				/* clear int */
	else if (sb == 040) {				/* LION */
	    lp09_ie = 1;				/* set int enab */
	    if (lp09_don) SET_INT (LPT);  }		/* if done, set int */
	}
return AC;
}

/* Unit service */

t_stat lp09_svc (UNIT *uptr)
{
int32 c;

lp09_don = 1;						/* set done */
if (lp09_ie) SET_INT (LPT);				/* int enb? req int */
if ((uptr->flags & UNIT_ATT) == 0) {			/* not attached? */
	lp09_err = 1;					/* set error */
	return IORETURN (lp09_stopioe, SCPE_UNATT);  }
c = uptr->buf & 0177;					/* get char */
if ((c == 0) || (c == 0177)) return SCPE_OK;		/* skip NULL, DEL */
if (fputc (c, uptr->fileref) == EOF) {			/* print char */
	perror ("LPT I/O error");
	clearerr (uptr->fileref);
	return SCPE_IOERR;  }
uptr->pos = uptr->pos + 1;				/* update position */
return SCPE_OK;
}

/* Reset routine */

t_stat lp09_reset (DEVICE *dptr)
{
lp09_don = 0;						/* clear done */
lp09_err = (lp09_unit.flags & UNIT_ATT)? 0: 1;		/* compute error */
lp09_ie = 1;						/* set enable */
CLR_INT (LPT);						/* clear int */
return SCPE_OK;
}

/* IORS routine */

int32 lp09_iors (void)
{
return (lp09_don? IOS_LPT: 0);
}

/* Attach routine */

t_stat lp09_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
lp09_err = (lp09_unit.flags & UNIT_ATT)? 0: 1;		/* clr/set error */
return reason;
}

/* Detach routine */

t_stat lp09_detach (UNIT *uptr)
{
lp09_err = 1;
return detach_unit (uptr);
}

#endif

#if defined (LP15)

/* LP15 line printer */

#define LP15_BSIZE	132				/* line size */
#define LPT_WC		034				/* word count */
#define LPT_CA		035				/* current addr */

/* Status register */

#define STA_ERR		0400000				/* error */
#define STA_ALM		0200000				/* alarm */
#define STA_OVF		0100000				/* line overflow */
#define STA_IHT		0040000				/* illegal HT */
#define STA_BUSY	0020000				/* busy */
#define STA_DON		0010000				/* done */
#define STA_ILK		0004000				/* interlock */
#define STA_EFLGS	(STA_ALM | STA_OVF | STA_IHT | STA_ILK)
#define STA_CLR		0003777				/* always clear */

extern int32 M[];
int32 lp15_sta = 0;
int32 lp15_ie = 1;
int32 lp15_stopioe = 0;
int32 lp15_mode = 0;
int32 lp15_lc = 0;
int32 lp15_bp = 0;
char lp15_buf[LP15_BSIZE] = { 0 };

DEVICE lp15_dev;
int32 lp15_65 (int32 pulse, int32 AC);
int32 lp15_66 (int32 pulse, int32 AC);
int32 lp15_iors (void);
t_stat lp15_svc (UNIT *uptr);
t_stat lp15_reset (DEVICE *dptr);

int32 lp15_updsta (int32 new);

/* LP15 LPT data structures

   lp15_dev	LPT device descriptor
   lp15_unit	LPT unit
   lp15_reg	LPT register list
*/

DIB lp15_dib = { DEV_LPT, 2, &lp15_iors, { &lp15_65, &lp15_66 } };

UNIT lp15_unit = {
	UDATA (&lp15_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG lp15_reg[] = {
	{ ORDATA (STA, lp15_sta, 18) },
	{ ORDATA (CA, M[LPT_CA], 18) },
	{ FLDATA (INT, int_hwre[API_LPT], INT_V_LPT) },
	{ FLDATA (ENABLE, lp15_ie, 0) },
	{ DRDATA (LCNT, lp15_lc, 9) },
	{ DRDATA (BPTR, lp15_bp, 8) },
	{ FLDATA (MODE, lp15_mode, 0) },
	{ DRDATA (POS, lp15_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, lp15_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, lp15_stopioe, 0) },
	{ BRDATA (LBUF, lp15_buf, 8, 8, LP15_BSIZE) },
	{ ORDATA (DEVNO, lp15_dib.dev, 6), REG_HRO },
	{ NULL }  };

MTAB lp15_mod[] = {
	{ MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO", &set_devno, &show_devno },
	{ 0 } };

DEVICE lp15_dev = {
	"LPT", &lp15_unit, lp15_reg, lp15_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &lp15_reset,
	NULL, NULL, NULL,
	&lp15_dib, DEV_DISABLE };

/* IOT routines */

int32 lp15_65 (int32 pulse, int32 AC)
{
int32 header, sb;

sb = pulse & 060;					/* subopcode */
if (pulse & 01) {
	if ((sb == 000) && (lp15_sta & (STA_ERR | STA_DON)))	/* LPSF */
	    AC = IOT_SKP | AC;
	else if ((sb == 020) || (sb == 040)) {		/* LPP1, LPPM */
	    sim_activate (&lp15_unit, lp15_unit.wait);	/* activate */
	    header = M[(M[LPT_CA] + 1) & ADDRMASK];	/* get first word */
	    M[LPT_CA] = (M[LPT_CA] + 2) & 0777777;
	    lp15_mode = header & 1;			/* mode */
	    if (sb == 040) lp15_lc = 1;			/* line count */
	    else lp15_lc = (header >> 9) & 0377;
	    if (lp15_lc == 0) lp15_lc = 256;
	    lp15_bp = 0;  }				/* reset buf ptr */
	else if (sb == 060) lp15_ie = 0;		/* LPDI */
	}
if ((pulse & 02) && (sb == 040)) AC = AC | lp15_updsta (0);	/* LPOS, LPRS */
if ((pulse & 04) && (sb == 040)) lp15_ie = 1;		/* LPEI */
lp15_updsta (0);					/* update status */
return AC;
}

int32 lp15_66 (int32 pulse, int32 AC)
{
if (pulse == 021) lp15_sta = lp15_sta & ~STA_DON;	/* LPCD */
if (pulse == 041) lp15_sta = 0;				/* LPCF */
lp15_updsta (0);					/* update status */
return AC;
}

/* Unit service */

t_stat lp15_svc (UNIT *uptr)
{
int32 i, ccnt, more, w0, w1;
char c[5];
static const char *ctrl[040] = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, "\n", "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",
	"\f", "\r", NULL, NULL,
	"\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",
	"\n\n", "\n\n\n", "\n",
	"\n\n\n\n\n\n\n\n\n\n", NULL, NULL, NULL,
	NULL, NULL, NULL, "\r", NULL, NULL, NULL, NULL };

if ((uptr->flags & UNIT_ATT) == 0) {			/* not attached? */
	lp15_updsta (STA_DON | STA_ALM);		/* set done, err */
	return IORETURN (lp15_stopioe, SCPE_UNATT);  }

for (more = 1; more != 0; ) {				/* loop until ctrl */
	w0 = M[(M[LPT_CA] + 1) & ADDRMASK];		/* get first word */
	w1 = M[(M[LPT_CA] + 2) & ADDRMASK];		/* get second word */
	M[LPT_CA] = (M[LPT_CA] + 2) & 0777777;		/* advance mem addr */
	if (lp15_mode) {				/* unpacked? */
	    c[0] = w0 & 0177;
	    c[1] = w1 & 0177;
	    ccnt = 2;  }
	else {
	    c[0] = (w0 >> 11) & 0177;			/* packed */
	    c[1] = (w0 >> 4) & 0177;
	    c[2] = (((w0 << 3) | (w1 >> 15))) & 0177;
	    c[3] = (w1 >> 8) & 0177;
	    c[4] = (w1 >> 1) & 0177;
	    ccnt = 5;  }
	for (i = 0; i < ccnt; i++) {			/* loop through */
	    if ((c[i] <= 037) && ctrl[c[i]]) {		/* control char? */
		fwrite (lp15_buf, 1, lp15_bp, uptr->fileref);
		fputs (ctrl[c[i]], uptr->fileref);
		if (ferror (uptr->fileref)) {		/* error? */
		    perror ("LPT I/O error");
		    clearerr (uptr->fileref);
		    lp15_bp = 0;
		    lp15_updsta (STA_DON | STA_ALM);
		    return SCPE_IOERR;  }
		uptr->pos = ftell (uptr->fileref);
		lp15_bp = more = 0;  }
	    else {
	    	if (lp15_bp < LP15_BSIZE) lp15_buf[lp15_bp++] = c[i];
		else lp15_sta = lp15_sta | STA_OVF;  }  }  }

lp15_lc = lp15_lc - 1;					/* decr line count */
if (lp15_lc) sim_activate (&lp15_unit, uptr->wait);	/* more to do? */
else lp15_updsta (STA_DON);				/* no, set done */
return SCPE_OK;
}

/* Update status */

int32 lp15_updsta (int32 new)
{
lp15_sta = (lp15_sta | new) & ~(STA_CLR | STA_ERR | STA_BUSY);
if (lp15_sta & STA_EFLGS) lp15_sta = lp15_sta | STA_ERR;	/* update errors */
if (sim_is_active (&lp15_unit)) lp15_sta = lp15_sta | STA_BUSY;
if (lp15_ie && (lp15_sta & STA_DON)) SET_INT (LPT);
else CLR_INT (LPT);					/* update int */
return lp15_sta;
}

/* Reset routine */

t_stat lp15_reset (DEVICE *dptr)
{
lp15_mode = lp15_lc = lp15_bp = 0;			/* clear controls */
sim_cancel (&lp15_unit);				/* deactivate unit */
lp15_sta = 0;						/* clear status */
lp15_ie = 1;						/* enable interrupts */
lp15_updsta (0);					/* update status */
return SCPE_OK;
}

/* IORS routine */

int32 lp15_iors (void)
{
return ((lp15_sta & STA_DON)? IOS_LPT: 0);
}

#endif
