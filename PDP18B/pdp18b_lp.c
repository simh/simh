/* pdp18b_lp.c: 18b PDP's line printer simulator

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

   lpt		(PDP-4) Type 62 line printer
		(PDP-7,9) Type 647 line printer
		(PDP-15) LP15 line printer

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

DEVICE lpt_dev;
int32 lpt65 (int32 pulse, int32 AC);
int32 lpt66 (int32 pulse, int32 AC);
int32 lpt_iors (void);
t_stat lpt_svc (UNIT *uptr);
t_stat lpt_reset (DEVICE *dptr);

extern int32 int_hwre[API_HLVL+1];

DIB lpt_dib = { DEV_LPT, 2, &lpt_iors, { &lpt65, &lpt66 } };

UNIT lpt_unit = {
	UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

MTAB lpt_mod[] = {
	{ MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO", &set_devno, &show_devno },
	{ 0 } };

#if defined (TYPE62)

#define BPTR_MAX	40				/* pointer max */
#define LPT_BSIZE	120				/* line size */
#define BPTR_MASK	077				/* buf ptr max */
int32 lpt_iot = 0, lpt_stopioe = 0, bptr = 0;
char lpt_buf[LPT_BSIZE + 1] = { 0 };

/* Type 62 LPT data structures

   lpt_dev	LPT device descriptor
   lpt_unit	LPT unit
   lpt_reg	LPT register list
*/

REG lpt_reg[] = {
	{ ORDATA (BUF, lpt_unit.buf, 8) },
	{ FLDATA (INT, int_hwre[API_LPT], INT_V_LPT) },
	{ FLDATA (DONE, int_hwre[API_LPT], INT_V_LPT) },
	{ FLDATA (SPC, int_hwre[API_LPTSPC], INT_V_LPTSPC) },
	{ DRDATA (BPTR, bptr, 6) },
	{ ORDATA (STATE, lpt_iot, 6), REG_HRO },
	{ DRDATA (POS, lpt_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, lpt_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, lpt_stopioe, 0) },
	{ BRDATA (LBUF, lpt_buf, 8, 8, LPT_BSIZE) },
	{ ORDATA (DEVNO, lpt_dib.dev, 6), REG_HRO },
	{ NULL }  };

DEVICE lpt_dev = {
	"LPT", &lpt_unit, lpt_reg, lpt_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &lpt_reset,
	NULL, NULL, NULL,
	&lpt_dib, DEV_DISABLE };

/* Type 62 line printer: IOT routines */

int32 lpt65 (int32 pulse, int32 AC)
{
int32 i;

static const char lpt_trans[64] = {
	' ','1','2','3','4','5','6','7','8','9','\'','~','#','V','^','<',
	'0','/','S','T','U','V','W','X','Y','Z','"',',','>','^','-','?',
	'o','J','K','L','M','N','O','P','Q','R','$','=','-',')','-','(',
	'_','A','B','C','D','E','F','G','H','I','*','.','+',']','|','[' };

if ((pulse & 001) && TST_INT (LPT)) AC = IOT_SKP | AC;	/* LPSF */
if ((pulse & 042) == 002) CLR_INT (LPT);		/* LPCF */
if (((pulse & 042) == 042) && (bptr < BPTR_MAX)) {	/* LPLD */
	i = bptr * 3;					/* cvt to chr ptr */
	lpt_buf[i] = lpt_trans[(AC >> 12) & 077];
	lpt_buf[i + 1] = lpt_trans[(AC >> 6) & 077];
	lpt_buf[i + 2] = lpt_trans[AC & 077];
	bptr = (bptr + 1) & BPTR_MASK;  }
if (pulse & 004) {					/* LPSE */
	sim_activate (&lpt_unit, lpt_unit.wait);  }	/* activate */
return AC;
}

int32 lpt66 (int32 pulse, int32 AC)
{
if ((pulse & 001) && TST_INT (LPTSPC))			/* LSSF */
	AC = IOT_SKP | AC;
if (pulse & 002) CLR_INT (LPTSPC);			/* LSCF */
if (pulse & 004) {					/* LSPR */
	lpt_iot = 020 | (AC & 07);			/* space, no print */
	sim_activate (&lpt_unit, lpt_unit.wait);  }	/* activate */
return AC;
}

/* Unit service, printer is in one of three states

   lpt_iot = 0		write buffer to file, set state to
   lpt_iot = 10		write cr, then write buffer to file
   lpt_iot = 2x		space command x, then set state to 0
*/

t_stat lpt_svc (UNIT *uptr)
{
int32 i;
static const char *lpt_cc[] = {
	"\n",
	"\n\n",
	"\n\n\n",
	"\n\n\n\n\n\n",
	"\n\n\n\n\n\n\n\n\n\n\n",
	"\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",
	"\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",
	"\f" };

if (lpt_iot & 020) {					/* space? */
	SET_INT (LPTSPC);				/* set flag */
	if ((lpt_unit.flags & UNIT_ATT) == 0)		/* attached? */
		return IORETURN (lpt_stopioe, SCPE_UNATT);
	fputs (lpt_cc[lpt_iot & 07], lpt_unit.fileref);	/* print cctl */
	if (ferror (lpt_unit.fileref)) {		/* error? */
		perror ("LPT I/O error");
		clearerr (lpt_unit.fileref);
		return SCPE_IOERR;  }
	lpt_iot = 0;  }					/* clear state */
else {	SET_INT (LPT);					/* print */
	if ((lpt_unit.flags & UNIT_ATT) == 0)		/* attached? */
		return IORETURN (lpt_stopioe, SCPE_UNATT);
	if (lpt_iot & 010) fputc ('\r', lpt_unit.fileref);
	fputs (lpt_buf, lpt_unit.fileref);		/* print buffer */
	if (ferror (lpt_unit.fileref)) {		/* test error */
		perror ("LPT I/O error");
		clearerr (lpt_unit.fileref);
		return SCPE_IOERR;  }
	bptr = 0;
	for (i = 0; i <= LPT_BSIZE; i++) lpt_buf[i] = 0; /* clear buffer */
	lpt_iot = 010;  }				/* set state */
lpt_unit.pos = ftell (lpt_unit.fileref);		/* update position */
return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
int32 i;

CLR_INT (LPT);						/* clear intrs */
CLR_INT (LPTSPC);
sim_cancel (&lpt_unit);					/* deactivate unit */
bptr = 0;						/* clear buffer ptr */
for (i = 0; i <= LPT_BSIZE; i++) lpt_buf[i] = 0;	/* clear buffer */
lpt_iot = 0;						/* clear state */
return SCPE_OK;
}

/* IORS routine */

int32 lpt_iors (void)
{
return	(TST_INT (LPT)? IOS_LPT: 0) |
	(TST_INT (LPTSPC)? IOS_LPT1: 0);
}

#elif defined (TYPE647)

#define LPT_BSIZE	120				/* line size */
int32 lpt_done = 0, lpt_ie = 1, lpt_err = 0;
int32 lpt_iot = 0, lpt_stopioe = 0, bptr = 0;
char lpt_buf[LPT_BSIZE] = { 0 };

t_stat lpt_attach (UNIT *uptr, char *cptr);
t_stat lpt_detach (UNIT *uptr);

/* Type 647 LPT data structures

   lpt_dev	LPT device descriptor
   lpt_unit	LPT unit
   lpt_reg	LPT register list
*/

REG lpt_reg[] = {
	{ ORDATA (BUF, lpt_unit.buf, 8) },
	{ FLDATA (INT, int_hwre[API_LPT], INT_V_LPT) },
	{ FLDATA (DONE, lpt_done, 0) },
#if defined (PDP9)
	{ FLDATA (ENABLE, lpt_ie, 0) },
#endif
	{ FLDATA (ERR, lpt_err, 0) },
	{ DRDATA (BPTR, bptr, 7) },
	{ ORDATA (SCMD, lpt_iot, 6), REG_HRO },
	{ DRDATA (POS, lpt_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, lpt_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, lpt_stopioe, 0) },
	{ BRDATA (LBUF, lpt_buf, 8, 8, LPT_BSIZE) },
	{ ORDATA (DEVNO, lpt_dib.dev, 6), REG_HRO },
	{ NULL }  };

DEVICE lpt_dev = {
	"LPT", &lpt_unit, lpt_reg, lpt_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &lpt_reset,
	NULL, &lpt_attach, &lpt_detach,
	&lpt_dib, DEV_DISABLE };

/* Type 647 line printer: IOT routines */

int32 lpt65 (int32 pulse, int32 AC)
{
int32 i, subp;

subp = (pulse >> 4) & 03;				/* subcode */
if ((pulse & 001) && lpt_done) AC = IOT_SKP | AC;	/* LPSF */
if (pulse & 002) {					/* pulse 02 */
	lpt_done = 0;					/* clear done */
	CLR_INT (LPT);					/* clear int req */
	if (subp == 0) {				/* LPCB */
		for (i = 0; i < LPT_BSIZE; i++) lpt_buf[i] = 0;
		bptr = 0;				/* reset buf ptr */
		lpt_done = 1;				/* set done */
		if (lpt_ie) SET_INT (LPT);  }  }	/* set int */
if (pulse & 004) {					/* LPDI */
	switch (subp) {					/* case on subcode */
	case 0:						/* LPDI */
#if defined (PDP9)
		lpt_ie = 0;				/* clear int enable */
		CLR_INT (LPT);				/* clear int req */
#endif
		break;
	case 2:						/* LPB3 */
		if (bptr < LPT_BSIZE) {
			lpt_buf[bptr] = lpt_buf[bptr] | ((AC >> 12) & 077);
			bptr = bptr + 1;  }
	case 1:						/* LPB2 */
		if (bptr < LPT_BSIZE) {
			lpt_buf[bptr] = lpt_buf[bptr] | ((AC >> 6) & 077);
			bptr = bptr + 1;  }
	case 3:						/* LPB1 */
		if (bptr < LPT_BSIZE) {
			lpt_buf[bptr] = lpt_buf[bptr] | (AC & 077);
			bptr = bptr + 1;  }
		lpt_done = 1;				/* set done */
		if (lpt_ie) SET_INT (LPT);		/* set int */
		break;  }				/* end case */
	}						/* end if pulse 4 */
return AC;
}

int32 lpt66 (int32 pulse, int32 AC)
{
if ((pulse & 001) && lpt_err) AC = IOT_SKP | AC;	/* LPSE */
if (pulse & 002) {					/* LPCF */
	lpt_done = 0;					/* clear done, int */
	CLR_INT (LPT);  }
if (pulse & 004) {
	int32 subp = (pulse >> 4) & 03;			/* get subpulse */
	if (subp < 3) {					/* LPLS, LPPB, LPPS */
		lpt_iot = (pulse & 060) | (AC & 07);	/* save parameters */
		sim_activate (&lpt_unit, lpt_unit.wait);  }	/* activate */
#if defined (PDP9)
	else {						/* LPEI */
		lpt_ie = 1;				/* set int enable */
		if (lpt_done) SET_INT (LPT);  }
#endif
	}						/* end if pulse 4 */
return AC;
}

/* Unit service.  lpt_iot specifies the action to be taken

   lpt_iot = 0x		print only
   lpt_iot = 2x		space only, x is spacing command
   lpt_iot = 4x		print then space, x is spacing command
*/

t_stat lpt_svc (UNIT *uptr)
{
int32 i;
char pbuf[LPT_BSIZE + 1];
static const char *lpt_cc[] = {
	"\n",
	"\n\n",
	"\n\n\n",
	"\n\n\n\n\n\n",
	"\n\n\n\n\n\n\n\n\n\n\n",
	"\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",
	"\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",
	"\f" };

lpt_done = 1;
if (lpt_ie) SET_INT (LPT);				/* set flag */
if ((lpt_unit.flags & UNIT_ATT) == 0) {			/* not attached? */
	lpt_err = 1;					/* set error */
	return IORETURN (lpt_stopioe, SCPE_UNATT);  }
if ((lpt_iot & 020) == 0) {				/* print? */
	for (i = 0; i < bptr; i++) 			/* translate buffer */
		pbuf[i] = lpt_buf[i] | ((lpt_buf[i] >= 040)? 0: 0100);
	if ((lpt_iot & 060) == 0) pbuf[bptr++] = '\r';
	for (i = 0; i < LPT_BSIZE; i++) lpt_buf[i] = 0;	/* clear buffer */
	fwrite (pbuf, 1, bptr, lpt_unit.fileref);	/* print buffer */
	if (ferror (lpt_unit.fileref)) {		/* error? */
		perror ("LPT I/O error");
		clearerr (lpt_unit.fileref);
		bptr = 0;
		return SCPE_IOERR;  }
	bptr = 0;  }					/* clear buffer ptr */
if (lpt_iot & 060) {					/* space? */
	fputs (lpt_cc[lpt_iot & 07], lpt_unit.fileref);	/* write cctl */
	if (ferror (lpt_unit.fileref)) {		/* error? */
		perror ("LPT I/O error");
		clearerr (lpt_unit.fileref);
		return SCPE_IOERR;  }  }
lpt_unit.pos = ftell (lpt_unit.fileref);		/* update position */
return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
int32 i;

lpt_done = 0;						/* clear done */
lpt_err = (lpt_unit.flags & UNIT_ATT)? 0: 1;		/* compute error */
lpt_ie = 1;						/* set enable */
CLR_INT (LPT);						/* clear int */
sim_cancel (&lpt_unit);					/* deactivate unit */
bptr = 0;						/* clear buffer ptr */
lpt_iot = 0;						/* clear state */
for (i = 0; i < LPT_BSIZE; i++) lpt_buf[i] = 0;		/* clear buffer */
return SCPE_OK;
}

/* IORS routine */

int32 lpt_iors (void)
{
return	(lpt_done? IOS_LPT: 0) | (lpt_err? IOS_LPT1: 0);
}

/* Attach routine */

t_stat lpt_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
lpt_err = (lpt_unit.flags & UNIT_ATT)? 0: 1;		/* compute error */
return reason;
}

/* Detach routine */

t_stat lpt_detach (UNIT *uptr)
{
lpt_err = 1;
return detach_unit (uptr);
}

#elif defined (LP15)

#define LPT_BSIZE	132				/* line size */
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
int32 lpt_sta = 0, lpt_ie = 1, lpt_stopioe = 0;
int32 mode = 0, lcnt = 0, bptr = 0;
char lpt_buf[LPT_BSIZE] = { 0 };

int32 lpt_updsta (int32 new);

/* LP15 LPT data structures

   lpt_dev	LPT device descriptor
   lpt_unit	LPT unit
   lpt_reg	LPT register list
*/

REG lpt_reg[] = {
	{ ORDATA (STA, lpt_sta, 18) },
	{ ORDATA (CA, M[LPT_CA], 18) },
	{ FLDATA (INT, int_hwre[API_LPT], INT_V_LPT) },
	{ FLDATA (ENABLE, lpt_ie, 0) },
	{ DRDATA (LCNT, lcnt, 9) },
	{ DRDATA (BPTR, bptr, 8) },
	{ FLDATA (MODE, mode, 0) },
	{ DRDATA (POS, lpt_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, lpt_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, lpt_stopioe, 0) },
	{ BRDATA (LBUF, lpt_buf, 8, 8, LPT_BSIZE) },
	{ ORDATA (DEVNO, lpt_dib.dev, 6), REG_HRO },
	{ NULL }  };

DEVICE lpt_dev = {
	"LPT", &lpt_unit, lpt_reg, lpt_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &lpt_reset,
	NULL, NULL, NULL,
	&lpt_dib, DEV_DISABLE };

/* LP15 line printer: IOT routines */

int32 lpt65 (int32 pulse, int32 AC)
{
int32 header;

if (pulse == 001)					/* LPSF */
	return (lpt_sta & (STA_ERR | STA_DON))? IOT_SKP + AC: AC;
if ((pulse == 021) || (pulse == 041)) {			/* LPP1, LPPM */
	header = M[(M[LPT_CA] + 1) & ADDRMASK];		/* get first word */
	M[LPT_CA] = (M[LPT_CA] + 2) & 0777777;
	mode = header & 1;				/* mode */
	if (pulse == 041) lcnt = 1;			/* line count */
	else lcnt = (header >> 9) & 0377;
	if (lcnt == 0) lcnt = 256;
	bptr = 0;					/* reset buf ptr */
	sim_activate (&lpt_unit, lpt_unit.wait);  }	/* activate */
if (pulse == 061) lpt_ie = 0;				/* LPDI */
if (pulse == 042) return lpt_updsta (0);		/* LPOS, LPRS */
if (pulse == 044) lpt_ie = 1;				/* LPEI */
lpt_updsta (0);						/* update status */
return AC;
}

int32 lpt66 (int32 pulse, int32 AC)
{
if (pulse == 021) lpt_sta = lpt_sta & ~STA_DON;		/* LPCD */
if (pulse == 041) lpt_sta = 0;				/* LPCF */
lpt_updsta (0);						/* update status */
return AC;
}

/* Unit service */

t_stat lpt_svc (UNIT *uptr)
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

if ((lpt_unit.flags & UNIT_ATT) == 0) {			/* not attached? */
	lpt_updsta (STA_DON | STA_ALM);			/* set done, err */
	return IORETURN (lpt_stopioe, SCPE_UNATT);  }

for (more = 1; more != 0; ) {				/* loop until ctrl */
	w0 = M[(M[LPT_CA] + 1) & ADDRMASK];		/* get first word */
	w1 = M[(M[LPT_CA] + 2) & ADDRMASK];		/* get second word */
	M[LPT_CA] = (M[LPT_CA] + 2) & 0777777;		/* advance mem addr */
	if (mode) {					/* unpacked? */
		c[0] = w0 & 0177;
		c[1] = w1 & 0177;
		ccnt = 2;  }
	else {	c[0] = (w0 >> 11) & 0177;		/* packed */
		c[1] = (w0 >> 4) & 0177;
		c[2] = (((w0 << 3) | (w1 >> 15))) & 0177;
		c[3] = (w1 >> 8) & 0177;
		c[4] = (w1 >> 1) & 0177;
		ccnt = 5;  }
	for (i = 0; i < ccnt; i++) {			/* loop through */
		if ((c[i] <= 037) && ctrl[c[i]]) {	/* control char? */
			fwrite (lpt_buf, 1, bptr, lpt_unit.fileref);
			fputs (ctrl[c[i]], lpt_unit.fileref);
			if (ferror (lpt_unit.fileref)) {	/* error? */
				perror ("LPT I/O error");
				clearerr (lpt_unit.fileref);
				bptr = 0;
				lpt_updsta (STA_DON | STA_ALM);
				return SCPE_IOERR;  }
			lpt_unit.pos = ftell (lpt_unit.fileref);
			bptr = more = 0;  }
		else {	if (bptr < LPT_BSIZE) lpt_buf[bptr++] = c[i];
			else lpt_sta = lpt_sta | STA_OVF;  }  }  }

lcnt = lcnt - 1;					/* decr line count */
if (lcnt) sim_activate (&lpt_unit, lpt_unit.wait);	/* more to do? */
else lpt_updsta (STA_DON);				/* no, set done */
return SCPE_OK;
}

/* Update status */

int32 lpt_updsta (int32 new)
{
lpt_sta = (lpt_sta | new) & ~(STA_CLR | STA_ERR | STA_BUSY);
if (lpt_sta & STA_EFLGS) lpt_sta = lpt_sta | STA_ERR;	/* update errors */
if (sim_is_active (&lpt_unit)) lpt_sta = lpt_sta | STA_BUSY;
if (lpt_ie && (lpt_sta & STA_DON)) SET_INT (LPT);
else CLR_INT (LPT);					/* update int */
return lpt_sta;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
mode = lcnt = bptr = 0;					/* clear controls */
sim_cancel (&lpt_unit);					/* deactivate unit */
lpt_sta = 0;						/* clear status */
lpt_ie = 1;						/* enable interrupts */
lpt_updsta (0);						/* update status */
return SCPE_OK;
}

/* IORS routine */

int32 lpt_iors (void)
{
return ((lpt_sta & STA_DON)? IOS_LPT: 0);
}

#endif
