/* hp2100_lp.c: HP 2100 line printer simulator

   Copyright (c) 1993-2001, Robert M. Supnik

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

   lpt		12653A line printer

   07-Sep-01	RMS	Moved function prototypes
   21-Nov-00	RMS	Fixed flag, fbf power up state
			Added command flop
   15-Oct-00	RMS	Added variable device number support
*/

#include "hp2100_defs.h"

#define LPT_BUSY 	0000001				/* busy */
#define LPT_NRDY	0100000				/* not ready */

extern int32 PC;
extern int32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];
int32 lpt_ctime = 10;					/* char time */
int32 lpt_stopioe = 0;					/* stop on error */
t_stat lpt_svc (UNIT *uptr);
t_stat lpt_reset (DEVICE *dptr);
extern struct hpdev infotab[];

/* LPT data structures

   lpt_dev	LPT device descriptor
   lpt_unit	LPT unit descriptor
   lpt_reg	LPT register list
*/

UNIT lpt_unit = {
	UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG lpt_reg[] = {
	{ ORDATA (BUF, lpt_unit.buf, 7) },
	{ FLDATA (CMD, infotab[inLPT].cmd, 0) },
	{ FLDATA (CTL, infotab[inLPT].ctl, 0) },
	{ FLDATA (FLG, infotab[inLPT].flg, 0) },
	{ FLDATA (FBF, infotab[inLPT].fbf, 0) },
	{ DRDATA (POS, lpt_unit.pos, 31), PV_LEFT },
	{ DRDATA (CTIME, lpt_ctime, 31), PV_LEFT },
	{ DRDATA (PTIME, lpt_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, lpt_stopioe, 0) },
	{ ORDATA (DEVNO, infotab[inLPT].devno, 6), REG_RO },
	{ NULL }  };

MTAB lpt_mod[] = {
	{ UNIT_DEVNO, inLPT, NULL, "DEVNO", &hp_setdev },
	{ 0 }  };

DEVICE lpt_dev = {
	"LPT", &lpt_unit, lpt_reg, lpt_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &lpt_reset,
	NULL, NULL, NULL  };

/* Line printer IOT routine */

int32 lptio (int32 inst, int32 IR, int32 dat)
{
int32 dev;

dev = IR & DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & HC) == 0) { setFLG (dev); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (dev) == 0) PC = (PC + 1) & AMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (dev) != 0) PC = (PC + 1) & AMASK;
	return dat;
case ioOTX:						/* output */
	lpt_unit.buf = dat & 0177;
	break;
case ioLIX:						/* load */
	dat = 0;					/* default sta = 0 */
case ioMIX:						/* merge */
	if ((lpt_unit.flags & UNIT_ATT) == 0) dat = dat | LPT_BUSY | LPT_NRDY;
	else if (sim_is_active (&lpt_unit)) dat = dat | LPT_BUSY;
	break;
case ioCTL:						/* control clear/set */
	if (IR & AB) {					/* CLC */
		clrCMD (dev);				/* clear ctl, cmd */
		clrCTL (dev);  }
	else {	setCMD (dev);				/* STC */
		setCTL (dev);				/* set ctl, cmd */
		sim_activate (&lpt_unit,		/* schedule op */
			(lpt_unit.buf < 040)? lpt_unit.wait: lpt_ctime);  }
	break;
default:
	break;  }
if (IR & HC) { clrFLG (dev); }				/* H/C option */
return dat;
}

t_stat lpt_svc (UNIT *uptr)
{
int32 dev;

dev = infotab[inLPT].devno;				/* get dev no */
clrCMD (dev);						/* clear cmd */
if ((lpt_unit.flags & UNIT_ATT) == 0)			/* attached? */
	return IORETURN (lpt_stopioe, SCPE_UNATT);
setFLG (dev);						/* set flag, fbf */
if (putc (lpt_unit.buf & 0177, lpt_unit.fileref) == EOF) {
	perror ("LPT I/O error");
	clearerr (lpt_unit.fileref);
	return SCPE_IOERR;  }
lpt_unit.pos = ftell (lpt_unit.fileref);		/* update pos */
return SCPE_OK;
}

/* Reset routine - called from SCP, flags in infotab */

t_stat lpt_reset (DEVICE *dptr)
{
infotab[inLPT].cmd = infotab[inLPT].ctl = 0;		/* clear cmd, ctl */
infotab[inLPT].flg = infotab[inLPT].fbf = 1;		/* set flg, fbf */
lpt_unit.buf = 0;
sim_cancel (&lpt_unit);					/* deactivate unit */
return SCPE_OK;
}
