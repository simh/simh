/* pdp1_lp.c: PDP-1 line printer simulator

   Copyright (c) 1993-2002, Robert M. Supnik

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

   lpt		Type 62 line printer for the PDP-1

   30-May-02	RMS	Widened POS to 32b
   13-Apr-01	RMS	Revised for register arrays
*/

#include "pdp1_defs.h"
#define BPTR_MAX	40				/* pointer max */
#define LPT_BSIZE	(BPTR_MAX * 3)			/* line size */
#define BPTR_MASK	077				/* buf ptr mask */
extern int32 ioc, sbs, iosta;
int32 lpt_rpls = 0, lpt_iot = 0, lpt_stopioe = 0, bptr = 0;
char lpt_buf[LPT_BSIZE + 1] = { 0 };
t_stat lpt_svc (UNIT *uptr);
t_stat lpt_reset (DEVICE *dptr);

/* LPT data structures

   lpt_dev	LPT device descriptor
   lpt_unit	LPT unit
   lpt_reg	LPT register list
*/

UNIT lpt_unit = {
	UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG lpt_reg[] = {
	{ ORDATA (BUF, lpt_unit.buf, 8) },
	{ FLDATA (PNT, iosta, IOS_V_PNT) },
	{ FLDATA (SPC, iosta, IOS_V_SPC) },
	{ FLDATA (RPLS, lpt_rpls, 0) },
	{ DRDATA (BPTR, bptr, 6) },
	{ ORDATA (LPT_STATE, lpt_iot, 6), REG_HRO },
	{ DRDATA (POS, lpt_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, lpt_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, lpt_stopioe, 0) },
	{ BRDATA (LBUF, lpt_buf, 8, 8, LPT_BSIZE) },
	{ NULL }  };

DEVICE lpt_dev = {
	"LPT", &lpt_unit, lpt_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &lpt_reset,
	NULL, NULL, NULL };

/* Line printer IOT routine */

int32 lpt (int32 inst, int32 dev, int32 data)
{
int32 i;

static const unsigned char lpt_trans[64] = {
	' ','1','2','3','4','5','6','7','8','9','\'','~','#','V','^','<',
	'0','/','S','T','U','V','W','X','Y','Z','"',',','>','^','-','?',
	'@','J','K','L','M','N','O','P','Q','R','$','=','-',')','-','(',
	'_','A','B','C','D','E','F','G','H','I','*','.','+',']','|','[' };

if ((inst & 0700) == 0100) {				/* fill buf */
	if (bptr < BPTR_MAX) {				/* limit test ptr */
		i = bptr * 3;				/* cvt to chr ptr */
		lpt_buf[i++] = lpt_trans[(data >> 12) & 077];
		lpt_buf[i++] = lpt_trans[(data >> 6) & 077];
		lpt_buf[i++] = lpt_trans[data & 077];  }
	bptr = (bptr + 1) & BPTR_MASK;
	return data;  }
lpt_rpls = 0;
if ((inst & 0700) == 0200) {				/* space */
	iosta = iosta & ~IOS_SPC;			/* space, clear flag */
	lpt_iot = (inst >> 6) & 077;  }			/* state = space n */
else {	iosta = iosta & ~IOS_PNT;			/* clear flag */
	lpt_iot = 0;  }					/* state = print */
if (GEN_CPLS (inst)) {					/* comp pulse? */
	ioc = 0;					/* clear flop */
	lpt_rpls = 1;  }				/* request completion */
sim_activate (&lpt_unit, lpt_unit.wait);		/* activate */
return data;
}

/* Unit service, printer is in one of three states

   lpt_iot = 000		write buffer to file, set state to
   lpt_iot = 010		write cr, then write buffer to file
   lpt_iot = 02x		space command x, then set state to 0
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

sbs = sbs | SB_RQ;					/* req seq break */
ioc = ioc | lpt_rpls;					/* restart */
if (lpt_iot & 020) {					/* space? */
	iosta = iosta | IOS_SPC;			/* set flag */
	if ((lpt_unit.flags & UNIT_ATT) == 0)		/* attached? */
		return IORETURN (lpt_stopioe, SCPE_UNATT);
	fputs (lpt_cc[lpt_iot & 07], lpt_unit.fileref);	/* print cctl */
	if (ferror (lpt_unit.fileref)) {		/* error? */
		perror ("LPT I/O error");
		clearerr (lpt_unit.fileref);
		return SCPE_IOERR;  }
	lpt_iot = 0;  }					/* clear state */
else {	iosta = iosta | IOS_PNT;			/* print */
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

iosta = iosta & ~(IOS_PNT | IOS_SPC);			/* clear flags */
bptr = 0;						/* clear buffer ptr */
for (i = 0; i <= LPT_BSIZE; i++) lpt_buf[i] = 0;	/* clear buffer */
lpt_iot = 0;						/* clear state */
lpt_rpls = 0;
sim_cancel (&lpt_unit);					/* deactivate unit */
return SCPE_OK;
}
