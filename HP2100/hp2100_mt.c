/* hp2100_mt.c: HP 2100 12559A magnetic tape simulator

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

   mt		12559A 3030 nine track magnetic tape

   06-Jul-04	RMS	Fixed spurious timing error after CLC (found by Dave Bryan)
   26-Apr-04	RMS	Fixed SFS x,C and SFC x,C
			Implemented DMA SRQ (follows FLG)
   21-Dec-03	RMS	Adjusted msc_ctime for TSB (from Mike Gemeny)
   25-Apr-03	RMS	Revised for extended file support
   28-Mar-03	RMS	Added multiformat support
   28-Feb-03	RMS	Revised for magtape library
   30-Sep-02	RMS	Revamped error handling
   28-Aug-02	RMS	Added end of medium support
   30-May-02	RMS	Widened POS to 32b
   22-Apr-02	RMS	Added maximum record length test
   20-Jan-02	RMS	Fixed bug on last character write
   03-Dec-01	RMS	Added read only unit, extended SET/SHOW support
   07-Sep-01	RMS	Moved function prototypes
   30-Nov-00	RMS	Made variable names unique
   04-Oct-98	RMS	V2.4 magtape format

   Magnetic tapes are represented as a series of variable records
   of the form:

	32b byte count
	byte 0
	byte 1
	:
	byte n-2
	byte n-1
	32b byte count

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a byte count of 0.

   Unusually among HP peripherals, the 12559 does not have a command flop,
   and its flag and flag buffer power up as clear rather than set.
*/

#include "hp2100_defs.h"
#include "sim_tape.h"

#define DB_V_SIZE	16				/* max data buf */
#define DBSIZE		(1 << DB_V_SIZE)		/* max data cmd */

/* Command - mtc_fnc */

#define FNC_CLR		0300				/* clear */
#define FNC_WC		0031				/* write */
#define FNC_RC		0023				/* read */
#define FNC_GAP		0011				/* write gap */
#define FNC_FSR		0003				/* forward space */
#define FNC_BSR		0041				/* backward space */
#define FNC_REW		0201				/* rewind */
#define FNC_RWS		0101				/* rewind and offline */
#define FNC_WFM		0035				/* write file mark */

/* Status - stored in mtc_sta, (d) = dynamic */

#define STA_LOCAL	0400				/* local (d) */
#define STA_EOF		0200				/* end of file */
#define STA_BOT		0100				/* beginning of tape */
#define STA_EOT		0040				/* end of tape */
#define STA_TIM		0020				/* timing error */
#define STA_REJ		0010				/* programming error */
#define STA_WLK		0004				/* write locked (d) */
#define STA_PAR		0002				/* parity error */
#define STA_BUSY	0001				/* busy (d) */

extern uint32 PC;
extern uint32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2], dev_srq[2];

int32 mtc_fnc = 0;					/* function */
int32 mtc_sta = 0;					/* status register */
int32 mtc_dtf = 0;					/* data xfer flop */
int32 mtc_1st = 0;					/* first svc flop */
int32 mtc_ctime = 40;					/* command wait */
int32 mtc_gtime = 1000;					/* gap stop time */
int32 mtc_xtime = 15;					/* data xfer time */
int32 mtc_stopioe = 1;					/* stop on error */
uint8 mtxb[DBSIZE] = { 0 };				/* data buffer */
t_mtrlnt mt_ptr = 0, mt_max = 0;			/* buffer ptrs */
static const int32 mtc_cmd[] = {
 FNC_WC, FNC_RC, FNC_GAP, FNC_FSR, FNC_BSR, FNC_REW, FNC_RWS, FNC_WFM };

DEVICE mtd_dev, mtc_dev;
int32 mtdio (int32 inst, int32 IR, int32 dat);
int32 mtcio (int32 inst, int32 IR, int32 dat);
t_stat mtc_svc (UNIT *uptr);
t_stat mtc_reset (DEVICE *dptr);
t_stat mtc_attach (UNIT *uptr, char *cptr);
t_stat mtc_detach (UNIT *uptr);
t_stat mt_map_err (UNIT *uptr, t_stat st);

/* MTD data structures

   mtd_dev	MTD device descriptor
   mtd_unit	MTD unit list
   mtd_reg	MTD register list
*/

DIB mt_dib[] = {
	{ MTD, 0, 0, 0, 0, 0, &mtdio },
	{ MTC, 0, 0, 0, 0, 0, &mtcio }  };

#define mtd_dib mt_dib[0]
#define mtc_dib mt_dib[1]

UNIT mtd_unit = { UDATA (NULL, 0, 0) };

REG mtd_reg[] = {
	{ FLDATA (CMD, mtd_dib.cmd, 0), REG_HRO },
	{ FLDATA (CTL, mtd_dib.ctl, 0), REG_HRO },
	{ FLDATA (FLG, mtd_dib.flg, 0) },
	{ FLDATA (FBF, mtd_dib.fbf, 0) },
	{ FLDATA (SRQ, mtd_dib.srq, 0) },
	{ BRDATA (DBUF, mtxb, 8, 8, DBSIZE) },
	{ DRDATA (BPTR, mt_ptr, DB_V_SIZE + 1) },
	{ DRDATA (BMAX, mt_max, DB_V_SIZE + 1) },
	{ ORDATA (DEVNO, mtd_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB mtd_mod[] = {
	{ MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL },
	{ MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL }, 
	{ MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
		&sim_tape_set_fmt, &sim_tape_show_fmt, NULL },
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &mtd_dev },
	{ 0 }  };

DEVICE mtd_dev = {
	"MTD", &mtd_unit, mtd_reg, mtd_mod,
	1, 10, 16, 1, 8, 8,
	NULL, NULL, &mtc_reset,
	NULL, NULL, NULL,
	&mtd_dib, DEV_DISABLE | DEV_DIS };

/* MTC data structures

   mtc_dev	MTC device descriptor
   mtc_unit	MTC unit list
   mtc_reg	MTC register list
   mtc_mod	MTC modifier list
*/

UNIT mtc_unit = { UDATA (&mtc_svc, UNIT_ATTABLE + UNIT_ROABLE, 0) };

REG mtc_reg[] = {
	{ ORDATA (FNC, mtc_fnc, 8) },
	{ ORDATA (STA, mtc_sta, 9) },
	{ ORDATA (BUF, mtc_unit.buf, 8) },
	{ FLDATA (CMD, mtc_dib.cmd, 0), REG_HRO },
	{ FLDATA (CTL, mtc_dib.ctl, 0) },
	{ FLDATA (FLG, mtc_dib.flg, 0) },
	{ FLDATA (FBF, mtc_dib.fbf, 0) },
	{ FLDATA (SRQ, mtc_dib.srq, 0) },
	{ FLDATA (DTF, mtc_dtf, 0) },
	{ FLDATA (FSVC, mtc_1st, 0) },
	{ DRDATA (POS, mtc_unit.pos, T_ADDR_W), PV_LEFT },
	{ DRDATA (CTIME, mtc_ctime, 24), REG_NZ + PV_LEFT },
	{ DRDATA (GTIME, mtc_gtime, 24), REG_NZ + PV_LEFT },
	{ DRDATA (XTIME, mtc_xtime, 24), REG_NZ + PV_LEFT },
	{ FLDATA (STOP_IOE, mtc_stopioe, 0) },
	{ ORDATA (DEVNO, mtc_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB mtc_mod[] = {
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &mtd_dev },
	{ 0 }  };

DEVICE mtc_dev = {
	"MTC", &mtc_unit, mtc_reg, mtc_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &mtc_reset,
	NULL, &mtc_attach, &mtc_detach,
	&mtc_dib, DEV_DISABLE | DEV_DIS };

/* IOT routines */

int32 mtdio (int32 inst, int32 IR, int32 dat)
{
int32 devd;

devd = IR & I_DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & I_HC) == 0) { setFSR (devd); }	/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (devd) == 0) PC = (PC + 1) & VAMASK;
	break;
case ioSFS:						/* skip flag set */
	if (FLG (devd) != 0) PC = (PC + 1) & VAMASK;
	break;
case ioOTX:						/* output */
	mtc_unit.buf = dat & 0377;			/* store data */
	break;
case ioMIX:						/* merge */
	dat = dat | mtc_unit.buf;
	break;
case ioLIX:						/* load */
	dat = mtc_unit.buf;
	break;
case ioCTL:						/* control clear/set */
	if (IR & I_CTL) mtc_dtf = 0;			/* CLC: clr xfer flop */
	break;
default:
	break;  }
if (IR & I_HC) { clrFSR (devd); }			/* H/C option */
return dat;
}

int32 mtcio (int32 inst, int32 IR, int32 dat)
{
int32 i, devc, devd, valid;
t_stat st;

devc = IR & I_DEVMASK;					/* get device no */
devd = devc - 1;
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & I_HC) == 0) { setFSR (devc); }	/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (devc) == 0) PC = (PC + 1) & VAMASK;
	break;
case ioSFS:						/* skip flag set */
	if (FLG (devc) != 0) PC = (PC + 1) & VAMASK;
	break;
case ioOTX:						/* output */
	dat = dat & 0377;
	mtc_sta = mtc_sta & ~STA_REJ;			/* clear reject */
	if (dat == FNC_CLR) {				/* clear? */
	    if (sim_is_active (&mtc_unit) &&		/* write in prog? */
		(mtc_fnc == FNC_WC) && (mt_ptr > 0)) {	/* yes, bad rec */
		    if (st = sim_tape_wrrecf (&mtc_unit, mtxb, mt_ptr | MTR_ERF))
			mt_map_err (&mtc_unit, st);  }
	    if (((mtc_fnc == FNC_REW) || (mtc_fnc == FNC_RWS)) &&
		sim_is_active (&mtc_unit)) sim_cancel (&mtc_unit);
	    mtc_1st = mtc_dtf = 0;
	    mtc_sta = mtc_sta & STA_BOT;
	    clrCTL (devc);				/* init device */
	    clrFSR (devc);
	    clrCTL (devd);
	    clrFSR (devd);
	    return SCPE_OK;  }
	for (i = valid = 0; i < sizeof (mtc_cmd); i++)	/* is fnc valid? */
	    if (dat == mtc_cmd[i]) valid = 1;
	if (!valid || sim_is_active (&mtc_unit) ||	/* is cmd valid? */
	   ((mtc_sta & STA_BOT) && (dat == FNC_BSR)) ||
	   (sim_tape_wrp (&mtc_unit) && 
	     ((dat == FNC_WC) || (dat == FNC_GAP) || (dat == FNC_WFM))))
	    mtc_sta = mtc_sta | STA_REJ;
	else {
	    sim_activate (&mtc_unit, mtc_ctime);	/* start tape */
	    mtc_fnc = dat;				/* save function */
	    mtc_sta = STA_BUSY;				/* unit busy */
	    mt_ptr = 0;					/* init buffer ptr */
	    clrFSR (devc);				/* clear flags */
	    clrFSR (devd);
	    mtc_1st = 1;				/* set 1st flop */
	    mtc_dtf = 1;  }				/* set xfer flop */
	break;
case ioLIX:						/* load */
	dat = 0;
case ioMIX:						/* merge */
	dat = dat | (mtc_sta & ~(STA_LOCAL | STA_WLK | STA_BUSY));
	if (mtc_unit.flags & UNIT_ATT) {		/* construct status */
	    if (sim_is_active (&mtc_unit)) dat = dat | STA_BUSY;
	    if (sim_tape_wrp (&mtc_unit)) dat = dat | STA_WLK;  }
	else dat = dat | STA_BUSY | STA_LOCAL;	
	break;
case ioCTL:						/* control clear/set */
	if (IR & I_CTL) { clrCTL (devc); }		/* CLC */
	else { setCTL (devc); }				/* STC */
	break;
default:
	break;  }
if (IR & I_HC) { clrFSR (devc); }			/* H/C option */
return dat;
}

/* Unit service

   If rewind done, reposition to start of tape, set status
   else, do operation, set done, interrupt

   Can't be write locked, can only write lock detached unit
*/

t_stat mtc_svc (UNIT *uptr)
{
int32 devc, devd;
t_mtrlnt tbc;
t_stat st, r = SCPE_OK;

devc = mtc_dib.devno;					/* get device nos */
devd = mtd_dib.devno;
if ((mtc_unit.flags & UNIT_ATT) == 0) {			/* offline? */
	mtc_sta = STA_LOCAL | STA_REJ;			/* rejected */
	setFSR (devc);					/* set cch flg */
	return IORETURN (mtc_stopioe, SCPE_UNATT);  }

switch (mtc_fnc) {					/* case on function */

case FNC_REW:						/* rewind */
	sim_tape_rewind (uptr);				/* BOT */
	mtc_sta = STA_BOT;				/* update status */
	break;

case FNC_RWS:						/* rewind and offline */
	sim_tape_rewind (uptr);				/* clear position */
	return sim_tape_detach (uptr);			/* don't set cch flg */

case FNC_WFM:						/* write file mark */
	if (st = sim_tape_wrtmk (uptr))			/* write tmk, err? */
	    r = mt_map_err (uptr, st);			/* map error */
	mtc_sta = STA_EOF;				/* set EOF status */
	break;

case FNC_GAP:						/* erase gap */
	break;

case FNC_FSR:						/* space forward */
	if (st = sim_tape_sprecf (uptr, &tbc))		/* space rec fwd, err? */
	    r = mt_map_err (uptr, st);			/* map error */
	break;

case FNC_BSR:						/* space reverse */
	if (st = sim_tape_sprecr (uptr, &tbc))		/* space rec rev, err? */
	    r = mt_map_err (uptr, st);			/* map error */
	break;

/* Unit service, continued */

case FNC_RC:						/* read */
	if (mtc_1st) {					/* first svc? */
	    mtc_1st = mt_ptr = 0;			/* clr 1st flop */
	    st = sim_tape_rdrecf (uptr, mtxb, &mt_max, DBSIZE);	/* read rec */
	    if (st == MTSE_RECE) mtc_sta = mtc_sta | STA_PAR;	/* rec in err? */
	    else if (st != MTSE_OK) {			/* other error? */
		r = mt_map_err (uptr, st);		/* map error */
		if (r == SCPE_OK) {			/* recoverable? */
		    sim_activate (uptr, mtc_gtime);	/* sched IRG */
		    mtc_fnc = 0;			/* NOP func */
		    return SCPE_OK;  }
		break;  }				/* non-recov, done */
	    if (mt_max < 12) {				/* record too short? */
		mtc_sta = mtc_sta | STA_PAR;		/* set flag */
		break;  }
	    }
	if (mtc_dtf && (mt_ptr < mt_max)) {		/* more chars? */
	    if (FLG (devd)) mtc_sta = mtc_sta | STA_TIM;
	    mtc_unit.buf = mtxb[mt_ptr++];		/* fetch next */
	    setFSR (devd);				/* set dch flg */
	    sim_activate (uptr, mtc_xtime);		/* re-activate */
	    return SCPE_OK;  }
	sim_activate (uptr, mtc_gtime);			/* schedule gap */
	mtc_fnc = 0;					/* nop */
	return SCPE_OK;

case FNC_WC:						/* write */
	if (mtc_1st) mtc_1st = 0;			/* no xfr on first */
	else {
	    if (mt_ptr < DBSIZE) {			/* room in buffer? */
		mtxb[mt_ptr++] = mtc_unit.buf;
		mtc_sta = mtc_sta & ~STA_BOT;  }	/* clear BOT */
	    else mtc_sta = mtc_sta | STA_PAR;  }
	if (mtc_dtf) {					/* xfer flop set? */
	    setFSR (devd);				/* set dch flag */
	    sim_activate (uptr, mtc_xtime);		/* re-activate */
	    return SCPE_OK;  }
	if (mt_ptr) {					/* write buffer */
	    if (st = sim_tape_wrrecf (uptr, mtxb, mt_ptr)) {	/* write, err? */
		r = mt_map_err (uptr, st);		/* map error */
		break;  }  }				/* done */
	sim_activate (uptr, mtc_gtime);			/* schedule gap */
	mtc_fnc = 0;					/* nop */
	return SCPE_OK;

default:						/* unknown */
	break;  }

setFSR (devc);						/* set cch flg */
mtc_sta = mtc_sta & ~STA_BUSY;				/* not busy */
return SCPE_OK;
}

/* Map tape error status */

t_stat mt_map_err (UNIT *uptr, t_stat st)
{
switch (st) {
case MTSE_FMT:						/* illegal fmt */
case MTSE_UNATT:					/* unattached */
	mtc_sta = mtc_sta | STA_REJ;			/* reject */
case MTSE_OK:						/* no error */
	return SCPE_IERR;				/* never get here! */
case MTSE_TMK:						/* end of file */
	mtc_sta = mtc_sta | STA_EOF;			/* eof */
	break;
case MTSE_IOERR:					/* IO error */
	mtc_sta = mtc_sta | STA_PAR;			/* error */
	if (mtc_stopioe) return SCPE_IOERR;
	break;
case MTSE_INVRL:					/* invalid rec lnt */
	mtc_sta = mtc_sta | STA_PAR;
	return SCPE_MTRLNT;
case MTSE_RECE:						/* record in error */
case MTSE_EOM:						/* end of medium */
	mtc_sta = mtc_sta | STA_PAR;			/* error */
	break;
case MTSE_BOT:						/* reverse into BOT */
	mtc_sta = mtc_sta | STA_BOT;			/* set status */
	break;
case MTSE_WRP:						/* write protect */
	mtc_sta = mtc_sta | STA_REJ;			/* reject */
	break;  }
return SCPE_OK;
}

/* Reset routine */

t_stat mtc_reset (DEVICE *dptr)
{
hp_enbdis_pair (&mtc_dev, &mtd_dev);			/* make pair cons */
mtc_fnc = 0;
mtc_1st = mtc_dtf = 0;
mtc_dib.cmd = mtd_dib.cmd = 0;				/* clear cmd */
mtc_dib.ctl = mtd_dib.ctl = 0;				/* clear ctl */
mtc_dib.flg = mtd_dib.flg = 0;				/* clear flg */
mtc_dib.fbf = mtd_dib.fbf = 0;				/* clear fbf */
mtc_dib.srq = mtd_dib.srq = 0;				/* srq follows flg */
sim_cancel (&mtc_unit);					/* cancel activity */
sim_tape_reset (&mtc_unit);
if (mtc_unit.flags & UNIT_ATT) mtc_sta =
	(sim_tape_bot (&mtc_unit)? STA_BOT: 0) |
	(sim_tape_wrp (&mtc_unit)? STA_WLK: 0);
else mtc_sta = STA_LOCAL | STA_BUSY;
return SCPE_OK;
}

/* Attach routine */

t_stat mtc_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = sim_tape_attach (uptr, cptr);			/* attach unit */
if (r != SCPE_OK) return r;				/* update status */
mtc_sta = STA_BOT;
return r;
}

/* Detach routine */

t_stat mtc_detach (UNIT* uptr)
{
mtc_sta = 0;						/* update status */
return sim_tape_detach (uptr);				/* detach unit */
}
