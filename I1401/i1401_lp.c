/* i1401_lp.c: IBM 1403 line printer simulator

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

   lpt		1403 line printer

   30-May-02	RMS	Widened POS to 32b
   13-Apr-01	RMS	Revised for register arrays
*/

#include "i1401_defs.h"

extern uint8 M[];
extern char bcd_to_ascii[64];
extern int32 iochk, ind[64];

int32 cct[CCT_LNT] = { 03 };
int32 cctlnt = 66, cctptr = 0, lines = 0, lflag = 0;

t_stat lpt_reset (DEVICE *dptr);
t_stat lpt_attach (UNIT *uptr, char *cptr);
t_stat space (int32 lines, int32 lflag);

char bcd_to_pca[64] = {
	' ', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', '0', '#', '@', ' ', ' ', ' ',
	' ', '/', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', ' ', ',', '%', ' ', ' ', ' ',
	'-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', '-', '$', '*', ' ', ' ', ' ',
	'&', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
	'H', 'I', '&', '.', ')', ' ', ' ', ' ' };
char bcd_to_pch[64] = {
	' ', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', '0', '=', '\'', ' ', ' ', ' ',
	' ', '/', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', ' ', ',', '(', ' ', ' ', ' ',
	'-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', '-', '$', '*', ' ', ' ', ' ',
	'&', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
	'H', 'I', '&', '.', ')', ' ', ' ', ' ' };
char *pch_table[4] = {
	bcd_to_ascii, bcd_to_pca, bcd_to_pch, bcd_to_ascii };

#define	UNIT_V_PCHAIN	(UNIT_V_UF + 0)
#define UNIT_M_PCHAIN	03
#define M_PCF		00				/* full */
#define M_PCA		01				/* business */
#define M_PCH		02				/* Fortran */
#define UNIT_PCHAIN	(UNIT_M_PCHAIN << UNIT_V_PCHAIN)
#define PCF		(M_PCF << UNIT_V_PCHAIN)
#define PCA		(M_PCA << UNIT_V_PCHAIN)
#define PCH		(M_PCH << UNIT_V_PCHAIN)
#define GET_PCHAIN(x)	(((x) >> UNIT_V_PCHAIN) & UNIT_M_PCHAIN)
#define CHP(ch,val)	((val) & (1 << (ch)))

/* LPT data structures

   lpt_dev	LPT device descriptor
   lpt_unit	LPT unit descriptor
   lpt_reg	LPT register list
*/

UNIT lpt_unit = {
	UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) };

REG lpt_reg[] = {
	{ FLDATA (ERR, ind[IN_LPT], 0) },
	{ DRDATA (POS, lpt_unit.pos, 32), PV_LEFT },
	{ BRDATA (CCT, cct, 8, 32, CCT_LNT) },
	{ DRDATA (LINES, lines, 8), PV_LEFT },
	{ DRDATA (CCTP, cctptr, 8), PV_LEFT },
	{ DRDATA (CCTL, cctlnt, 8), REG_RO + PV_LEFT },
	{ NULL }  };

MTAB lpt_mod[] = {
	{ UNIT_PCHAIN, PCF, "F chain", "PCF", NULL },
	{ UNIT_PCHAIN, PCA, "A chain", "PCA", NULL },
	{ UNIT_PCHAIN, PCH, "H chain", "PCH", NULL },
	{ 0 }  };

DEVICE lpt_dev = {
	"LPT", &lpt_unit, lpt_reg, lpt_mod,
	1, 10, 31, 1, 8, 7,
	NULL, NULL, &lpt_reset,
	NULL, &lpt_attach, NULL };

/* Print routine

   Modifiers have been checked by the caller
	SQUARE	=	word mark mode
	S	=	suppress automatic newline
*/

t_stat write_line (int32 ilnt, int32 mod)
{
int32 i, t, wm, sup;
char *pch;
static char lbuf[LPT_WIDTH + 1];			/* + null */

if ((lpt_unit.flags & UNIT_ATT) == 0) return SCPE_UNATT;	/* attached? */
wm = ((ilnt == 2) || (ilnt == 5)) && (mod == BCD_SQUARE);
sup = ((ilnt == 2) || (ilnt == 5)) && (mod == BCD_S);
ind[IN_LPT] = 0;					/* clear error */
pch = pch_table[GET_PCHAIN (lpt_unit.flags)];		/* get print chain */
for (i = 0; i < LPT_WIDTH; i++) {			/* convert print buf */
	t = M[LPT_BUF + i];
	if (wm) lbuf[i] = (t & WM)? '1': ' ';		/* wmarks -> 1 or sp */
	else lbuf[i] = pch[t & CHAR];  }		/* normal */
M[LPT_BUF + 1] = 0;					/* trailing null */
for (i = LPT_WIDTH - 1; (i >= 0) && (lbuf[i] == ' '); i--) lbuf[i] = 0;
fputs (lbuf, lpt_unit.fileref);				/* write line */
if (lines) space (lines, lflag);			/* cc action? do it */
else if (sup == 0) space (1, FALSE);			/* default? 1 line */
else {	fputc ('\r', lpt_unit.fileref);			/* sup -> overprint */
	lpt_unit.pos = ftell (lpt_unit.fileref);  }	/* update position */
lines = lflag = 0;					/* clear cc action */
if (ferror (lpt_unit.fileref)) {			/* error? */
	perror ("Line printer I/O error");
	clearerr (lpt_unit.fileref);
	if (iochk) return SCPE_IOERR;
	ind[IN_LPT] = 1;  }
return SCPE_OK;
}

/* Carriage control routine

   The modifier has not been checked, its format is
	<5:4>	=	00, skip to channel now
		=	01, space lines after
		=	10, space lines now
		=	11, skip to channel after
	<3:0>	=	number of lines or channel number
*/

t_stat carriage_control (int32 mod)
{
int32 i, action;

action = (mod & ZONE) >> V_ZONE;			/* get mod type */
mod = mod & DIGIT;					/* isolate value */
switch (action) {
case 0:							/* to channel now */
	if ((mod == 0) || (mod > 12) || CHP (mod, cct[cctptr])) return SCPE_OK;
	for (i = 1; i < cctlnt + 1; i++) {		/* sweep thru cct */
	    if (CHP (mod, cct[(cctptr + i) % cctlnt]))
		return space (i, TRUE);  }
	return STOP_CCT;				/* runaway channel */
case 1:							/* space after */
	if (mod <= 3) {
	    lines = mod;				/* save # lines */
	    lflag = FALSE;				/* flag spacing */
	    ind[IN_CC9] = ind[IN_CC12] = 0;  }
	return SCPE_OK;
case 2:							/* space now */
	if (mod <= 3) return space (mod, FALSE);
	return SCPE_OK;
case 3:							/* to channel after */
	if ((mod == 0) || (mod > 12)) return SCPE_OK;	/* check channel */
	ind[IN_CC9] = ind[IN_CC12] = 0;
	for (i = 1; i < cctlnt + 1; i++) {		/* sweep thru cct */
	    if (CHP (mod, cct[(cctptr + i) % cctlnt])) {
		lines = i;				/* save # lines */
		lflag = TRUE;				/* flag skipping */
		return SCPE_OK;  }  }
	return STOP_CCT;  }				/* runaway channel */
return SCPE_OK;
}

/* Space routine - space or skip n lines
   
   Inputs:
	count	=	number of lines to space or skip
	sflag	=	skip (TRUE) or space (FALSE)
*/

t_stat space (int32 count, int32 sflag)
{
int32 i;

if ((lpt_unit.flags & UNIT_ATT) == 0) return SCPE_UNATT;
cctptr = (cctptr + count) % cctlnt;			/* adv cct, mod lnt */
if (sflag && CHP (0, cct[cctptr]))			/* skip, top of form? */
	fputs ("\n\f", lpt_unit.fileref);		/* nl, ff */
else {	for (i = 0; i < count; i++)
	    fputc ('\n', lpt_unit.fileref);  }
lpt_unit.pos = ftell (lpt_unit.fileref);		/* update position */
ind[IN_CC9] = CHP (9, cct[cctptr]) != 0;		/* set indicators */
ind[IN_CC12] = CHP (12, cct[cctptr]) != 0;
return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
cctptr = 0;						/* clear cct ptr */
lines = lflag = 0;					/* no cc action */
ind[IN_LPT] = 0;
return SCPE_OK;
}

/* Attach routine */

t_stat lpt_attach (UNIT *uptr, char *cptr)
{
cctptr = 0;						/* clear cct ptr */
lines = 0;						/* no cc action */
ind[IN_LPT] = 0;
return attach_unit (uptr, cptr);
}
