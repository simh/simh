/* hp2100_lpt.c: HP 2100 12845A line printer simulator

   Copyright (c) 1993-2004, Robert M. Supnik

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

   lpt		12845A line printer

   25-Apr-03	RMS	Revised for extended file support
   24-Oct-02	RMS	Cloned from 12653A
*/

#include "hp2100_defs.h"

#define LPT_PAGELNT	60				/* page length */

#define LPT_NBSY 	0000001				/* not busy */
#define LPT_PAPO	0040000				/* paper out */
#define LPT_RDY		0100000				/* ready */

#define LPT_CTL		0100000				/* control output */
#define LPT_CHAN	0000100				/* skip to chan */
#define LPT_SKIPM	0000077				/* line count mask */
#define LPT_CHANM	0000007				/* channel mask */

extern uint32 PC;
extern uint32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];
int32 lpt_ctime = 1000;					/* char time */
int32 lpt_stopioe = 0;					/* stop on error */
int32 lpt_lcnt = 0;					/* line count */
static int32 lpt_cct[8] = {
	1, 1, 1, 2, 3, LPT_PAGELNT/2, LPT_PAGELNT/4, LPT_PAGELNT/6 };

DEVICE lpt_dev;
int32 lptio (int32 inst, int32 IR, int32 dat);
t_stat lpt_svc (UNIT *uptr);
t_stat lpt_reset (DEVICE *dptr);
t_stat lpt_attach (UNIT *uptr, char *cptr);

/* LPT data structures

   lpt_dev	LPT device descriptor
   lpt_unit	LPT unit descriptor
   lpt_reg	LPT register list
*/

DIB lpt_dib = { LPT, 0, 0, 0, 0, &lptio };

UNIT lpt_unit = {
	UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG lpt_reg[] = {
	{ ORDATA (BUF, lpt_unit.buf, 7) },
	{ FLDATA (CMD, lpt_dib.cmd, 0) },
	{ FLDATA (CTL, lpt_dib.ctl, 0) },
	{ FLDATA (FLG, lpt_dib.flg, 0) },
	{ FLDATA (FBF, lpt_dib.fbf, 0) },
	{ DRDATA (LCNT, lpt_lcnt, 7) },
	{ DRDATA (POS, lpt_unit.pos, T_ADDR_W), PV_LEFT },
	{ DRDATA (CTIME, lpt_ctime, 31), PV_LEFT },
	{ DRDATA (PTIME, lpt_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, lpt_stopioe, 0) },
	{ ORDATA (DEVNO, lpt_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB lpt_mod[] = {
	{ MTAB_XTD | MTAB_VDV, 0, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &lpt_dev },
	{ 0 }  };

DEVICE lpt_dev = {
	"LPT", &lpt_unit, lpt_reg, lpt_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &lpt_reset,
	NULL, &lpt_attach, NULL,
	&lpt_dib, DEV_DISABLE  };

/* Line printer IOT routine */

int32 lptio (int32 inst, int32 IR, int32 dat)
{
int32 dev;

dev = IR & I_DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & I_HC) == 0) { setFLG (dev); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (dev) == 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (dev) != 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioOTX:						/* output */
	lpt_unit.buf = dat & (LPT_CTL | 0177);
	break;
case ioLIX:						/* load */
	dat = 0;					/* default sta = 0 */
case ioMIX:						/* merge */
	if (lpt_unit.flags & UNIT_ATT) {
	    dat = dat | LPT_RDY;
	    if (!sim_is_active (&lpt_unit))
		dat = dat | LPT_NBSY;  }
	else dat = dat | LPT_PAPO;
	break;
case ioCTL:						/* control clear/set */
	if (IR & I_CTL) {				/* CLC */
	    clrCMD (dev);				/* clear ctl, cmd */
	    clrCTL (dev);  }
	else {						/* STC */
	    setCMD (dev);				/* set ctl, cmd */
	    setCTL (dev);
	    sim_activate (&lpt_unit,			/* schedule op */
		(lpt_unit.buf & LPT_CTL)? lpt_unit.wait: lpt_ctime);  }
	break;
default:
	break;  }
if (IR & I_HC) { clrFLG (dev); }			/* H/C option */
return dat;
}

t_stat lpt_svc (UNIT *uptr)
{
int32 i, skip, chan, dev;

dev = lpt_dib.devno;					/* get dev no */
clrCMD (dev);						/* clear cmd */
if ((uptr->flags & UNIT_ATT) == 0)			/* attached? */
	return IORETURN (lpt_stopioe, SCPE_UNATT);
setFLG (dev);						/* set flag, fbf */
if (uptr->buf & LPT_CTL) {				/* control word? */
	if (uptr->buf & LPT_CHAN) {
	    chan = uptr->buf & LPT_CHANM;
	    if (chan == 0) {				/* top of form? */
		fputc ('\f', uptr->fileref);		/* ffeed */
		lpt_lcnt = 0;				/* reset line cnt */
		skip = 1;  }
	    else if (chan == 1) skip = LPT_PAGELNT - lpt_lcnt - 1;
	    else skip = lpt_cct[chan] - (lpt_lcnt % lpt_cct[chan]);
	    }
	else {
	    skip = uptr->buf & LPT_SKIPM;
	    if (skip == 0) fputc ('\r', uptr->fileref);
	    }
	for (i = 0; i < skip; i++) fputc ('\n', uptr->fileref);
	lpt_lcnt = (lpt_lcnt + skip) % LPT_PAGELNT;
	}
else fputc (uptr->buf & 0177, uptr->fileref);		/* no, just add char */
if (ferror (uptr->fileref)) {
	perror ("LPT I/O error");
	clearerr (uptr->fileref);
	return SCPE_IOERR;  }
lpt_unit.pos = ftell (uptr->fileref);			/* update pos */
return SCPE_OK;
}

/* Reset routine - called from SCP, flags in DIB */

t_stat lpt_reset (DEVICE *dptr)
{
lpt_dib.cmd = lpt_dib.ctl = 0;				/* clear cmd, ctl */
lpt_dib.flg = lpt_dib.fbf = 1;				/* set flg, fbf */
lpt_unit.buf = 0;
sim_cancel (&lpt_unit);					/* deactivate unit */
return SCPE_OK;
}

/* Attach routine */

t_stat lpt_attach (UNIT *uptr, char *cptr)
{
lpt_lcnt = 0;						/* top of form */
return attach_unit (uptr, cptr);
}
