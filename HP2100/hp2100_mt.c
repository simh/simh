/* hp2100_mt.c: HP 2100 12559A magnetic tape simulator

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

   mt		12559A 3030 nine track magnetic tape

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

#define UNIT_V_WLK	(UNIT_V_UF + 0)			/* write locked */
#define UNIT_V_PNU	(UNIT_V_UF + 1)			/* pos not updated */
#define UNIT_WLK	(1 << UNIT_V_WLK)
#define UNIT_PNU	(1 << UNIT_V_PNU)
#define DB_V_SIZE	16				/* max data buf */
#define DBSIZE		(1 << DB_V_SIZE)		/* max data cmd */
#define DBMASK		(DBSIZE - 1)
#define UNIT_WPRT	(UNIT_WLK | UNIT_RO)		/* write protect */

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
extern uint32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];
int32 mtc_fnc = 0;					/* function */
int32 mtc_sta = 0;					/* status register */
int32 mtc_dtf = 0;					/* data xfer flop */
int32 mtc_1st = 0;					/* first svc flop */
int32 mtc_ctime = 1000;					/* command wait */
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
t_stat mtc_vlock (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat mtc_attach (UNIT *uptr, char *cptr);
t_stat mtc_detach (UNIT *uptr);
t_bool mt_rdlntf (UNIT *uptr, t_mtrlnt *tbc, int32 *err);
t_bool mt_rdlntr (UNIT *uptr, t_mtrlnt *tbc, int32 *err);
int32 mt_wrtrec (UNIT *uptr, t_mtrlnt lnt);

/* MTD data structures

   mtd_dev	MTD device descriptor
   mtd_unit	MTD unit list
   mtd_reg	MTD register list
*/

DIB mt_dib[] = {
	{ MTD, 0, 0, 0, 0, &mtdio },
	{ MTC, 0, 0, 0, 0, &mtcio }  };

#define mtd_dib mt_dib[0]
#define mtc_dib mt_dib[1]

UNIT mtd_unit = { UDATA (NULL, 0, 0) };

REG mtd_reg[] = {
	{ FLDATA (CMD, mtd_dib.cmd, 0), REG_HRO },
	{ FLDATA (CTL, mtd_dib.ctl, 0), REG_HRO },
	{ FLDATA (FLG, mtd_dib.flg, 0) },
	{ FLDATA (FBF, mtd_dib.fbf, 0), REG_HRO },
	{ BRDATA (DBUF, mtxb, 8, 8, DBSIZE) },
	{ DRDATA (BPTR, mt_ptr, DB_V_SIZE + 1) },
	{ DRDATA (BMAX, mt_max, DB_V_SIZE + 1) },
	{ ORDATA (DEVNO, mtd_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB mtd_mod[] = {
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
	{ FLDATA (DTF, mtc_dtf, 0) },
	{ FLDATA (FSVC, mtc_1st, 0) },
	{ DRDATA (POS, mtc_unit.pos, 32), PV_LEFT },
	{ DRDATA (CTIME, mtc_ctime, 24), REG_NZ + PV_LEFT },
	{ DRDATA (GTIME, mtc_gtime, 24), REG_NZ + PV_LEFT },
	{ DRDATA (XTIME, mtc_xtime, 24), REG_NZ + PV_LEFT },
	{ FLDATA (STOP_IOE, mtc_stopioe, 0) },
	{ ORDATA (DEVNO, mtc_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB mtc_mod[] = {
	{ UNIT_WLK, 0, "write enabled", "WRITEENABLED", &mtc_vlock },
	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", &mtc_vlock }, 
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
	if ((IR & I_HC) == 0) { setFLG (devd); }	/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (devd) == 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (devd) != 0) PC = (PC + 1) & VAMASK;
	return dat;
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
if (IR & I_HC) { clrFLG (devd); }			/* H/C option */
return dat;
}

int32 mtcio (int32 inst, int32 IR, int32 dat)
{
int32 i, devc, devd, valid;

devc = IR & I_DEVMASK;					/* get device no */
devd = devc - 1;
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & I_HC) == 0) { setFLG (devc); }	/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (devc) == 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (devc) != 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioOTX:						/* output */
	dat = dat & 0377;
	mtc_sta = mtc_sta & ~STA_REJ;			/* clear reject */
	if (dat == FNC_CLR) {				/* clear? */
	    if (sim_is_active (&mtc_unit) &&		/* write in prog? */
		(mtc_fnc == FNC_WC) && (mt_ptr > 0))	/* yes, bad rec */
		mt_wrtrec (&mtc_unit, mt_ptr | MTR_ERF);
	    if (((mtc_fnc == FNC_REW) || (mtc_fnc == FNC_RWS)) &&
		sim_is_active (&mtc_unit)) sim_cancel (&mtc_unit);
	    mtc_1st = mtc_dtf = 0;
	    mtc_sta = mtc_sta & STA_BOT;
	    clrCTL (devc);				/* init device */
	    clrFLG (devc);
	    clrCTL (devd);
	    clrFLG (devd);
	    return SCPE_OK;  }
	for (i = valid = 0; i < sizeof (mtc_cmd); i++)	/* is fnc valid? */
	    if (dat == mtc_cmd[i]) valid = 1;
	if (!valid || sim_is_active (&mtc_unit) ||	/* is cmd valid? */
	   ((mtc_sta & STA_BOT) && (dat == FNC_BSR)) ||
	   ((mtc_unit.flags & UNIT_WPRT) && 
	     ((dat == FNC_WC) || (dat == FNC_GAP) || (dat == FNC_WFM))))
	    mtc_sta = mtc_sta | STA_REJ;
	else {
	    sim_activate (&mtc_unit, mtc_ctime);	/* start tape */
	    mtc_fnc = dat;				/* save function */
	    mtc_sta = STA_BUSY;				/* unit busy */
	    mt_ptr = 0;					/* init buffer ptr */
	    clrFLG (devc);				/* clear flags */
	    clrFLG (devd);
	    mtc_1st = 1;				/* set 1st flop */
	    mtc_dtf = 1;  }				/* set xfer flop */
	break;
case ioLIX:						/* load */
	dat = 0;
case ioMIX:						/* merge */
	dat = dat | (mtc_sta & ~(STA_LOCAL | STA_WLK | STA_BUSY));
	if (mtc_unit.flags & UNIT_ATT) {		/* construct status */
	    if (sim_is_active (&mtc_unit)) dat = dat | STA_BUSY;
	    if (mtc_unit.flags & UNIT_WPRT) dat = dat | STA_WLK;  }
	else dat = dat | STA_BUSY | STA_LOCAL;	
	break;
case ioCTL:						/* control clear/set */
	if (IR & I_CTL) { clrCTL (devc); }		/* CLC */
	else { setCTL (devc); }				/* STC */
	break;
default:
	break;  }
if (IR & I_HC) { clrFLG (devc); }			/* H/C option */
return dat;
}

/* Unit service

   If rewind done, reposition to start of tape, set status
   else, do operation, set done, interrupt

   Can't be write locked, can only write lock detached unit
*/

t_stat mtc_svc (UNIT *uptr)
{
int32 devc, devd, err, pnu;
static t_mtrlnt i, bceof = { MTR_TMK };

err = 0;						/* assume no errors */
devc = mtc_dib.devno;					/* get device nos */
devd = mtd_dib.devno;
if ((mtc_unit.flags & UNIT_ATT) == 0) {			/* offline? */
	mtc_sta = STA_LOCAL | STA_REJ;			/* rejected */
	setFLG (devc);					/* set cch flg */
	return IORETURN (mtc_stopioe, SCPE_UNATT);  }

pnu = MT_TST_PNU (uptr);				/* get pos not upd */
MT_CLR_PNU (uptr);					/* and clear */
switch (mtc_fnc) {					/* case on function */

case FNC_REW:						/* rewind */
	mtc_unit.pos = 0;				/* BOT */
	mtc_sta = STA_BOT;				/* update status */
	break;

case FNC_RWS:						/* rewind and offline */
	mtc_unit.pos = 0;				/* clear position */
	return detach_unit (uptr);			/* don't set cch flg */

case FNC_WFM:						/* write file mark */
	fseek (mtc_unit.fileref, mtc_unit.pos, SEEK_SET);
	fxwrite (&bceof, sizeof (t_mtrlnt), 1, mtc_unit.fileref);
	mtc_sta = STA_EOF;				/* set EOF status */
	if (err = ferror (mtc_unit.fileref)) MT_SET_PNU (uptr);
	else mtc_unit.pos = mtc_unit.pos + sizeof (t_mtrlnt); /* update tape pos */
	break;

case FNC_GAP:						/* erase gap */
	break;

case FNC_FSR:						/* space forward */
	if (mt_rdlntf (uptr, &mt_max, &err)) break;	/* read rec lnt, err? */
	mtc_unit.pos = mtc_unit.pos + ((mt_max + 1) & ~1) +
		(2 * sizeof (t_mtrlnt));		/* update position */
	break;

case FNC_BSR:						/* space reverse */
	if (pnu) break;					/* pnu? do nothing */
	if (mt_rdlntr (uptr, &mt_max, &err)) break;	/* read rec lnt, err? */
	mtc_unit.pos = mtc_unit.pos - ((mt_max + 1) & ~1) -
	    (2 * sizeof (t_mtrlnt));			/* update position */
	break;

/* Unit service, continued */

case FNC_RC:						/* read */
	if (mtc_1st) {					/* first svc? */
	    mtc_1st = 0;				/* clr 1st flop */
	    if (mt_rdlntf (uptr, &mt_max, &err)) {	/* read rec lnt */
		if (!err) {				/* tmk or eom? */
		    sim_activate (uptr, mtc_gtime);	/* sched IRG */
		    mtc_fnc = 0;			/* NOP func */
		    return SCPE_OK;  }
		break;  }				/* error, done */
	    if (mt_max > DBSIZE) return SCPE_MTRLNT;	/* record too long? */
	    if (mt_max < 12) {				/* record too short? */
		mtc_sta = mtc_sta | STA_PAR;		/* set flag */
		break;  }
	    i = fxread (mtxb, sizeof (int8), mt_max, mtc_unit.fileref);
	    for ( ; i < mt_max; i++) mtxb[i] = 0;	/* fill with 0's */
	    if (err = ferror (mtc_unit.fileref)) {	/* error? */
		mtc_sta = mtc_sta | STA_PAR;		/* set flag */
		MT_SET_PNU (uptr);			/* pos not upd */
		break;  }
	    mtc_unit.pos = mtc_unit.pos + ((mt_max + 1) & ~1) +
		(2 * sizeof (t_mtrlnt));  }		/* update position */
	if (mt_ptr < mt_max) {				/* more chars? */
	    if (FLG (devd)) mtc_sta = mtc_sta | STA_TIM;
	    mtc_unit.buf = mtxb[mt_ptr++];		/* fetch next */
	    setFLG (devd);				/* set dch flg */
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
	    setFLG (devd);				/* set dch flag */
	    sim_activate (uptr, mtc_xtime);		/* re-activate */
	    return SCPE_OK;  }
	if (mt_ptr) {					/* write buffer */
	    if (err = mt_wrtrec (uptr, mt_ptr)) break;  }
	sim_activate (uptr, mtc_gtime);			/* schedule gap */
	mtc_fnc = 0;					/* nop */
	return SCPE_OK;

default:						/* unknown */
	break;  }

setFLG (devc);						/* set cch flg */
mtc_sta = mtc_sta & ~STA_BUSY;				/* not busy */
if (err != 0) {						/* I/O error */
	perror ("MT I/O error");
	clearerr (mtc_unit.fileref);
	if (mtc_stopioe) return SCPE_IOERR;  }
return SCPE_OK;
}

/* Tape motion routines */

t_bool mt_rdlntf (UNIT *uptr, t_mtrlnt *tbc, int32 *err)
{
fseek (uptr->fileref, uptr->pos, SEEK_SET);		/* position */
fxread (tbc, sizeof (t_mtrlnt), 1, uptr->fileref);	/* get bc */
if ((*err = ferror (uptr->fileref)) ||			/* error or eom? */ 
     feof (uptr->fileref) || (*tbc == MTR_EOM)) {
	mtc_sta = mtc_sta | STA_PAR;			/* error */
	MT_SET_PNU (uptr);				/* pos not upd */
	return TRUE;  }
if (*tbc == MTR_TMK) {					/* tape mark? */
	mtc_sta = mtc_sta | STA_EOF;			/* eof */
	uptr->pos = uptr->pos + sizeof (t_mtrlnt);	/* spc over tmk */
	return TRUE;  }
if (MTRF (*tbc)) mtc_sta = mtc_sta | STA_PAR;		/* error in rec? */
*tbc = MTRL (*tbc);					/* clear err flag */
return FALSE;
}

t_bool mt_rdlntr (UNIT *uptr, t_mtrlnt *tbc, int32 *err)
{
if (uptr->pos < sizeof (t_mtrlnt)) {			/* no data to read? */
	mtc_sta = mtc_sta | STA_BOT;			/* update status */
	return TRUE;  }
fseek (uptr->fileref, uptr->pos - sizeof (t_mtrlnt), SEEK_SET);
fxread (tbc, sizeof (t_mtrlnt), 1, uptr->fileref);	/* get bc */
if ((*err = ferror (uptr->fileref)) ||			/* error or eof? */ 
     feof (uptr->fileref)) {
	mtc_sta = mtc_sta | STA_PAR;			/* error */
	return TRUE;  }
if (*tbc == MTR_EOM) {					/* eom? */
	mtc_sta = mtc_sta | STA_PAR;			/* error */
	uptr->pos = uptr->pos - sizeof (t_mtrlnt);	/* spc over eom */
	return TRUE;  }
if (*tbc == MTR_TMK) {					/* tape mark? */
	mtc_sta = mtc_sta | STA_EOF;			/* eof */
	uptr->pos = uptr->pos - sizeof (t_mtrlnt);	/* spc over tmk */
	return TRUE;  }
if (MTRF (*tbc)) mtc_sta = mtc_sta | STA_PAR;		/* error in rec? */
*tbc = MTRL (*tbc);					/* clear err flag */
return FALSE;
}

int32 mt_wrtrec (UNIT *uptr, t_mtrlnt lnt)
{
int32 elnt = MTRL ((lnt + 1) & ~1);			/* even lnt, no err */

fseek (uptr->fileref, uptr->pos, SEEK_SET);		/* seek to record */
fxwrite (&lnt, sizeof (t_mtrlnt), 1, uptr->fileref);	/* write rec lnt */
fxwrite (mtxb, sizeof (int8), elnt, uptr->fileref);	/* write data */
fxwrite (&lnt, sizeof (t_mtrlnt), 1, uptr->fileref);	/* write rec lnt */
if (ferror (uptr->fileref)) {				/* error? */
	MT_SET_PNU (uptr);				/* pos not updated */
	return 1;  }
else uptr->pos = uptr->pos + elnt + (2 * sizeof (t_mtrlnt)); /* no, upd pos */
return 0;
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
sim_cancel (&mtc_unit);					/* cancel activity */
mtc_unit.flags = mtc_unit.flags & ~UNIT_PNU;		/* clear pos flag */
if (mtc_unit.flags & UNIT_ATT) mtc_sta = ((mtc_unit.pos)? 0: STA_BOT) |
	((mtc_unit.flags & UNIT_WPRT)? STA_WLK: 0);
else mtc_sta = STA_LOCAL | STA_BUSY;
return SCPE_OK;
}

/* Attach routine */

t_stat mtc_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = attach_unit (uptr, cptr);				/* attach unit */
if (r != SCPE_OK) return r;				/* update status */
MT_CLR_PNU (uptr);
mtc_sta = STA_BOT;
return r;
}

/* Detach routine */

t_stat mtc_detach (UNIT* uptr)
{
mtc_sta = 0;						/* update status */
MT_CLR_PNU (uptr);
return detach_unit (uptr);				/* detach unit */
}

/* Write lock/enable routine */

t_stat mtc_vlock (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (val && (uptr->flags & UNIT_ATT)) return SCPE_ARG;
return SCPE_OK;
}
