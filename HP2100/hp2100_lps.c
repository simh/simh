/* hp2100_lps.c: HP 2100 12653A line printer simulator

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

   lps		12653A 2767 line printer
		(based on 12556B microcircuit interface)

   03-Jun-04	RMS	Fixed timing (found by Dave Bryan)
   26-Apr-04	RMS	Fixed SFS x,C and SFC x,C
			Implemented DMA SRQ (follows FLG)
   25-Apr-03	RMS	Revised for extended file support
   24-Oct-02	RMS	Added microcircuit test features
   30-May-02	RMS	Widened POS to 32b
   03-Dec-01	RMS	Changed DEVNO to use extended SET/SHOW
   07-Sep-01	RMS	Moved function prototypes
   21-Nov-00	RMS	Fixed flag, fbf power up state
			Added command flop
   15-Oct-00	RMS	Added variable device number support
*/

#include "hp2100_defs.h"

#define LPS_BUSY 	0000001				/* busy */
#define LPS_NRDY	0100000				/* not ready */
#define UNIT_V_DIAG	(UNIT_V_UF + 0)			/* diagnostic mode */
#define UNIT_DIAG	(1 << UNIT_V_DIAG)

extern uint32 PC;
extern uint32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2], dev_srq[2];
int32 lps_ctime = 4;					/* char time */
int32 lps_ptime = 10000;				/* print time */
int32 lps_stopioe = 0;					/* stop on error */
int32 lps_sta = 0;

DEVICE lps_dev;
int32 lpsio (int32 inst, int32 IR, int32 dat);
t_stat lps_svc (UNIT *uptr);
t_stat lps_reset (DEVICE *dptr);

/* LPS data structures

   lps_dev	LPS device descriptor
   lps_unit	LPS unit descriptor
   lps_reg	LPS register list
*/

DIB lps_dib = { LPS, 0, 0, 0, 0, 0, &lpsio };

UNIT lps_unit = {
	UDATA (&lps_svc, UNIT_SEQ+UNIT_ATTABLE, 0) };

REG lps_reg[] = {
	{ ORDATA (BUF, lps_unit.buf, 16) },
	{ ORDATA (STA, lps_sta, 16) },
	{ FLDATA (CMD, lps_dib.cmd, 0) },
	{ FLDATA (CTL, lps_dib.ctl, 0) },
	{ FLDATA (FLG, lps_dib.flg, 0) },
	{ FLDATA (FBF, lps_dib.fbf, 0) },
	{ FLDATA (SRQ, lps_dib.srq, 0) },
	{ DRDATA (POS, lps_unit.pos, T_ADDR_W), PV_LEFT },
	{ DRDATA (CTIME, lps_ctime, 31), PV_LEFT },
	{ DRDATA (PTIME, lps_ptime, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, lps_stopioe, 0) },
	{ ORDATA (DEVNO, lps_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB lps_mod[] = {
	{ UNIT_DIAG, UNIT_DIAG, "diagnostic mode", "DIAG", NULL },
	{ UNIT_DIAG, 0, "printer mode", "PRINTER", NULL },
	{ MTAB_XTD | MTAB_VDV, 0, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &lps_dev },
	{ 0 }  };

DEVICE lps_dev = {
	"LPS", &lps_unit, lps_reg, lps_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &lps_reset,
	NULL, NULL, NULL,
	&lps_dib, DEV_DISABLE | DEV_DIS  };

/* Line printer IOT routine */

int32 lpsio (int32 inst, int32 IR, int32 dat)
{
int32 dev;

dev = IR & I_DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & I_HC) == 0) { setFSR (dev); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (dev) == 0) PC = (PC + 1) & VAMASK;
	break;
case ioSFS:						/* skip flag set */
	if (FLG (dev) != 0) PC = (PC + 1) & VAMASK;
	break;
case ioOTX:						/* output */
	lps_unit.buf = dat;
	break;
case ioLIX:						/* load */
	dat = 0;					/* default sta = 0 */
case ioMIX:						/* merge */
	if ((lps_unit.flags & UNIT_DIAG) == 0) {	/* real lpt? */
	    lps_sta = 0;				/* create status */
	    if ((lps_unit.flags & UNIT_ATT) == 0)
		lps_sta = lps_sta | LPS_BUSY | LPS_NRDY;
	    else if (sim_is_active (&lps_unit))
		lps_sta = lps_sta | LPS_BUSY;  }
	dat = dat | lps_sta;				/* diag, rtn status */
	break;
case ioCTL:						/* control clear/set */
	if (IR & I_CTL) {				/* CLC */
	    clrCMD (dev);				/* clear ctl, cmd */
	    clrCTL (dev);  }
	else {						/* STC */
	    setCMD (dev);				/* set ctl, cmd */
	    setCTL (dev);
	    if (lps_unit.flags & UNIT_DIAG)		/* diagnostic? */
		sim_activate (&lps_unit, 1);		/* loop back */
	    else sim_activate (&lps_unit,		/* real lpt, sched */
		(lps_unit.buf < 040)? lps_ptime: lps_ctime);  }
	break;
default:
	break;  }
if (IR & I_HC) { clrFSR (dev); }			/* H/C option */
return dat;
}

t_stat lps_svc (UNIT *uptr)
{
int32 dev;
int32 c = lps_unit.buf & 0177;

dev = lps_dib.devno;					/* get dev no */
clrCMD (dev);						/* clear cmd */
setFSR (dev);						/* set flag, fbf */
if (lps_unit.flags & UNIT_DIAG) {			/* diagnostic? */
	lps_sta = lps_unit.buf;				/* loop back */
	return SCPE_OK;  }				/* done */
if ((lps_unit.flags & UNIT_ATT) == 0)			/* real lpt, att? */
	return IORETURN (lps_stopioe, SCPE_UNATT);
if (fputc (c, lps_unit.fileref) == EOF) {
	perror ("LPS I/O error");
	clearerr (lps_unit.fileref);
	return SCPE_IOERR;  }
lps_unit.pos = lps_unit.pos + 1;			/* update pos */
return SCPE_OK;
}

/* Reset routine - called from SCP, flags in DIB */

t_stat lps_reset (DEVICE *dptr)
{
lps_dib.cmd = lps_dib.ctl = 0;				/* clear cmd, ctl */
lps_dib.flg = lps_dib.fbf = lps_dib.srq = 1;		/* set flg, fbf, srq */
lps_sta = lps_unit.buf = 0;
sim_cancel (&lps_unit);					/* deactivate unit */
return SCPE_OK;
}
