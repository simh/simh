/* pdp1_lp.c: PDP-1 line printer simulator

   Copyright (c) 1993-2003, Robert M. Supnik

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

   07-Sep-03	RMS	Changed ioc to ios
   23-Jul-03	RMS	Fixed bugs in instruction decoding, overprinting
			Revised to detect I/O wait hang
   25-Apr-03	RMS	Revised for extended file support
   30-May-02	RMS	Widened POS to 32b
   13-Apr-01	RMS	Revised for register arrays
*/

#include "pdp1_defs.h"

#define BPTR_MAX	40				/* pointer max */
#define LPT_BSIZE	(BPTR_MAX * 3)			/* line size */
#define BPTR_MASK	077				/* buf ptr mask */

int32 lpt_spc = 0;					/* print (0) vs spc */
int32 lpt_ovrpr = 0;					/* overprint */
int32 lpt_stopioe = 0;					/* stop on error */
int32 lpt_bptr = 0;					/* buffer ptr */
char lpt_buf[LPT_BSIZE + 1] = { 0 };
static const unsigned char lpt_trans[64] = {
	' ','1','2','3','4','5','6','7','8','9','\'','~','#','V','^','<',
	'0','/','S','T','U','V','W','X','Y','Z','"',',','>','^','-','?',
	'@','J','K','L','M','N','O','P','Q','R','$','=','-',')','-','(',
	'_','A','B','C','D','E','F','G','H','I','*','.','+',']','|','[' };

extern int32 ios, cpls, sbs, iosta;
extern int32 stop_inst;

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
	{ FLDATA (RPLS, cpls, CPLS_V_LPT) },
	{ DRDATA (BPTR, lpt_bptr, 6) },
	{ ORDATA (LPT_STATE, lpt_spc, 6), REG_HRO },
	{ FLDATA (LPT_OVRPR, lpt_ovrpr, 0), REG_HRO },
	{ DRDATA (POS, lpt_unit.pos, T_ADDR_W), PV_LEFT },
	{ DRDATA (TIME, lpt_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, lpt_stopioe, 0) },
	{ BRDATA (LBUF, lpt_buf, 8, 8, LPT_BSIZE) },
	{ NULL }  };

DEVICE lpt_dev = {
	"LPT", &lpt_unit, lpt_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &lpt_reset,
	NULL, NULL, NULL,
	NULL, DEV_DISABLE };

/* Line printer IOT routine */

int32 lpt (int32 inst, int32 dev, int32 dat)
{
int32 i;

if (lpt_dev.flags & DEV_DIS)				/* disabled? */
	return (stop_inst << IOT_V_REASON) | dat;	/* stop if requested */
if ((inst & 07000) == 01000) {				/* fill buf */
	if (lpt_bptr < BPTR_MAX) {			/* limit test ptr */
	    i = lpt_bptr * 3;				/* cvt to chr ptr */
	    lpt_buf[i] = lpt_trans[(dat >> 12) & 077];
	    lpt_buf[i + 1] = lpt_trans[(dat >> 6) & 077];
	    lpt_buf[i + 2] = lpt_trans[dat & 077];  }
	lpt_bptr = (lpt_bptr + 1) & BPTR_MASK;
	return dat;  }
if ((inst & 07000) == 02000) {				/* space */
	iosta = iosta & ~IOS_SPC;			/* space, clear flag */
	lpt_spc = (inst >> 6) & 077;  }			/* state = space n */
else if ((inst & 07000) == 00000) {			/* print */
	iosta = iosta & ~IOS_PNT;			/* clear flag */
	lpt_spc = 0;  }					/* state = print */
else return (stop_inst << IOT_V_REASON) | dat;		/* not implemented */
if (GEN_CPLS (inst)) {					/* comp pulse? */
	ios = 0;					/* clear flop */
	cpls = cpls | CPLS_LPT;  }			/* request completion */
else cpls = cpls & ~CPLS_LPT;
sim_activate (&lpt_unit, lpt_unit.wait);		/* activate */
return dat;
}

/* Unit service, printer is in one of three states

   lpt_spc = 000		write buffer to file, set overprint
   lpt_iot = 02x		space command x, clear overprint
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

if (cpls & CPLS_LPT) {					/* completion pulse? */
	ios = 1;					/* restart */
	cpls = cpls & ~CPLS_LPT;  }			/* clr pulse pending */
sbs = sbs | SB_RQ;					/* req seq break */
if (lpt_spc) {						/* space? */
	iosta = iosta | IOS_SPC;			/* set flag */
	if ((lpt_unit.flags & UNIT_ATT) == 0)		/* attached? */
	    return IORETURN (lpt_stopioe, SCPE_UNATT);
	fputs (lpt_cc[lpt_spc & 07], lpt_unit.fileref);	/* print cctl */
	if (ferror (lpt_unit.fileref)) {		/* error? */
	    perror ("LPT I/O error");
	    clearerr (lpt_unit.fileref);
	    return SCPE_IOERR;  }
	lpt_ovrpr = 0;  }				/* dont overprint */
else {	iosta = iosta | IOS_PNT;			/* print */
	if ((lpt_unit.flags & UNIT_ATT) == 0)		/* attached? */
	    return IORETURN (lpt_stopioe, SCPE_UNATT);
	if (lpt_ovrpr) fputc ('\r', lpt_unit.fileref);	/* overprint? */
	fputs (lpt_buf, lpt_unit.fileref);		/* print buffer */
	if (ferror (lpt_unit.fileref)) {		/* test error */
	    perror ("LPT I/O error");
	    clearerr (lpt_unit.fileref);
	    return SCPE_IOERR;  }
	lpt_bptr = 0;
	for (i = 0; i <= LPT_BSIZE; i++) lpt_buf[i] = 0; /* clear buffer */
	lpt_ovrpr = 1;  }				/* set overprint */
lpt_unit.pos = ftell (lpt_unit.fileref);		/* update position */
return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
int32 i;

lpt_bptr = 0;						/* clear buffer ptr */
for (i = 0; i <= LPT_BSIZE; i++) lpt_buf[i] = 0;	/* clear buffer */
lpt_spc = 0;						/* clear state */
lpt_ovrpr = 0;						/* clear overprint */
cpls = cpls & ~CPLS_LPT;
iosta = iosta & ~(IOS_PNT | IOS_SPC);			/* clear flags */
sim_cancel (&lpt_unit);					/* deactivate unit */
return SCPE_OK;
}
