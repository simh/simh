/* hp2100_ms.c: HP 2100 13181A/13183A magnetic tape simulator

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

   ms		13181A 7970B 800bpi nine track magnetic tape
		13183A 7970E 1600bpi nine track magnetic tape

   18-Oct-02	RMS	Added BOOT command, added 13183A support
   30-Sep-02	RMS	Revamped error handling
   29-Aug-02	RMS	Added end of medium support
   30-May-02	RMS	Widened POS to 32b
   22-Apr-02	RMS	Added maximum record length test

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

#define MS_NUMDR	4				/* number of drives */
#define UNIT_V_WLK	(UNIT_V_UF + 0)			/* write locked */
#define UNIT_V_PNU	(UNIT_V_UF + 1)			/* pos not updated */
#define UNIT_WLK	(1 << UNIT_V_WLK)
#define UNIT_PNU	(1 << UNIT_V_PNU)
#define DB_N_SIZE	16				/* max data buf */
#define DBSIZE		(1 << DB_N_SIZE)		/* max data cmd */
#define DBMASK		(DBSIZE - 1)
#define UNIT_WPRT	(UNIT_WLK | UNIT_RO)		/* write protect */
#define FNC		u3				/* function */
#define UST		u4				/* unit status */

/* Command - msc_fnc */

#define FNC_CLR		00110				/* clear */
#define FNC_GAP		00015				/* write gap */
#define FNC_GFM		00215				/* gap+file mark */
#define FNC_RC		00023				/* read */
#define FNC_WC		00031				/* write */
#define FNC_FSR		00003				/* forward space */
#define FNC_BSR		00041				/* backward space */
#define FNC_FSF		00203				/* forward file */
#define FNC_BSF		00241				/* backward file */
#define FNC_REW		00101				/* rewind */
#define FNC_RWS		00105				/* rewind and offline */
#define FNC_WFM		00211				/* write file mark */
#define FNC_RFF		00223				/* "read file fwd" */
#define FNC_V_SEL	9				/* select */
#define FNC_M_SEL	017
#define FNC_GETSEL(x)	(((x) >> FNC_V_SEL) & FNC_M_SEL)

#define FNF_MOT		00001				/* motion */
#define FNF_OFL		00004
#define FNF_WRT		00010				/* write */
#define FNF_REV		00040				/* reverse */
#define FNF_RWD		00100				/* rewind */
#define FNF_CHS		00400				/* change select */

/* Status - stored in msc_sta, unit.UST (u), or dynamic (d) */

#define STA_PE		0100000				/* 1600 bpi (d) */
#define STA_V_SEL	13				/* unit sel (d) */
#define STA_M_SEL	03
#define STA_SEL		(STA_M_SEL << STA_V_SEL)
#define STA_ODD		0004000				/* odd bytes */
#define STA_REW		0002000				/* rewinding (u) */
#define STA_TBSY	0001000				/* transport busy (d) */
#define STA_BUSY	0000400				/* ctrl busy */
#define STA_EOF		0000200				/* end of file */
#define STA_BOT		0000100				/* beg of tape (u) */
#define STA_EOT		0000040				/* end of tape (u) */
#define STA_TIM		0000020				/* timing error */
#define STA_REJ		0000010				/* programming error */
#define STA_WLK		0000004				/* write locked (d) */
#define STA_PAR		0000002				/* parity error */
#define STA_LOCAL	0000001				/* local (d) */
#define STA_DYN		(STA_PE|STA_SEL|STA_TBSY|STA_WLK|STA_LOCAL)

extern uint16 *M;
extern uint32 PC, SR;
extern uint32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];
extern int32 sim_switches;
extern UNIT cpu_unit;

int32 ms_ctype = 0;					/* ctrl type */
int32 msc_sta = 0;					/* status */
int32 msc_buf = 0;					/* buffer */
int32 msc_usl = 0;					/* unit select */
int32 msc_1st = 0;
int32 msc_ctime = 1000;					/* command wait */
int32 msc_gtime = 1000;					/* gap stop time */
int32 msc_rtime = 1000;					/* rewind wait */
int32 msc_xtime = 15;					/* data xfer time */
int32 msc_stopioe = 1;					/* stop on error */
int32 msd_buf = 0;					/* data buffer */
uint8 msxb[DBSIZE] = { 0 };				/* data buffer */
t_mtrlnt ms_ptr = 0, ms_max = 0;			/* buffer ptrs */

DEVICE msd_dev, msc_dev;
int32 msdio (int32 inst, int32 IR, int32 dat);
int32 mscio (int32 inst, int32 IR, int32 dat);
t_stat msc_svc (UNIT *uptr);
t_stat msc_reset (DEVICE *dptr);
t_stat msc_attach (UNIT *uptr, char *cptr);
t_stat msc_detach (UNIT *uptr);
t_stat msc_boot (int32 unitno, DEVICE *dptr);
t_stat msc_vlock (UNIT *uptr, int32 val, char *cptr, void *desc);
t_bool ms_rdlntf (UNIT *uptr, t_mtrlnt *tbc, int32 *err);
t_bool ms_rdlntr (UNIT *uptr, t_mtrlnt *tbc, int32 *err);
t_bool ms_forwsp (UNIT *uptr, int32 *err);
t_bool ms_backsp (UNIT *uptr, int32 *err);
int32 ms_wrtrec (UNIT *uptr, t_mtrlnt lnt);
t_stat ms_settype (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat ms_showtype (FILE *st, UNIT *uptr, int32 val, void *desc);

/* MSD data structures

   msd_dev	MSD device descriptor
   msd_unit	MSD unit list
   msd_reg	MSD register list
*/

DIB ms_dib[] = {
	{ MSD, 0, 0, 0, 0, &msdio },
	{ MSC, 0, 0, 0, 0, &mscio }  };

#define msd_dib ms_dib[0]
#define msc_dib ms_dib[1]

UNIT msd_unit = { UDATA (NULL, 0, 0) };

REG msd_reg[] = {
	{ ORDATA (BUF, msd_buf, 16) },
	{ FLDATA (CMD, msd_dib.cmd, 0), REG_HRO },
	{ FLDATA (CTL, msd_dib.ctl, 0) },
	{ FLDATA (FLG, msd_dib.flg, 0) },
	{ FLDATA (FBF, msd_dib.fbf, 0) },
	{ BRDATA (DBUF, msxb, 8, 8, DBSIZE) },
	{ DRDATA (BPTR, ms_ptr, DB_N_SIZE + 1) },
	{ DRDATA (BMAX, ms_max, DB_N_SIZE + 1) },
	{ ORDATA (DEVNO, msd_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB msd_mod[] = {
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &msd_dev },
	{ 0 }  };

DEVICE msd_dev = {
	"MSD", &msd_unit, msd_reg, msd_mod,
	1, 10, DB_N_SIZE, 1, 8, 8,
	NULL, NULL, &msc_reset,
	NULL, NULL, NULL,
	&msd_dib, 0 };

/* MSC data structures

   msc_dev	MSC device descriptor
   msc_unit	MSC unit list
   msc_reg	MSC register list
   msc_mod	MSC modifier list
*/

UNIT msc_unit[] = {
	{ UDATA (&msc_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
	{ UDATA (&msc_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
	{ UDATA (&msc_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
	{ UDATA (&msc_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) }  };

REG msc_reg[] = {
	{ ORDATA (STA, msc_sta, 12) },
	{ ORDATA (BUF, msc_buf, 16) },
	{ ORDATA (USEL, msc_usl, 2) },
	{ FLDATA (FSVC, msc_1st, 0) },
	{ FLDATA (CMD, msc_dib.cmd, 0), REG_HRO },
	{ FLDATA (CTL, msc_dib.ctl, 0) },
	{ FLDATA (FLG, msc_dib.flg, 0) },
	{ FLDATA (FBF, msc_dib.fbf, 0) },
	{ URDATA (POS, msc_unit[0].pos, 10, 32, 0, MS_NUMDR, PV_LEFT) },
	{ URDATA (FNC, msc_unit[0].FNC, 8, 8, 0, MS_NUMDR, REG_HRO) },
	{ URDATA (UST, msc_unit[0].UST, 8, 12, 0, MS_NUMDR, REG_HRO) },
	{ DRDATA (CTIME, msc_ctime, 24), REG_NZ + PV_LEFT },
	{ DRDATA (GTIME, msc_gtime, 24), REG_NZ + PV_LEFT },
	{ DRDATA (RTIME, msc_rtime, 24), REG_NZ + PV_LEFT },
	{ DRDATA (XTIME, msc_xtime, 24), REG_NZ + PV_LEFT },
	{ FLDATA (STOP_IOE, msc_stopioe, 0) },
	{ FLDATA (CTYPE, ms_ctype, 0), REG_HRO },
	{ ORDATA (DEVNO, msc_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB msc_mod[] = {
	{ UNIT_WLK, 0, "write enabled", "WRITEENABLED", &msc_vlock },
	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", &msc_vlock }, 
	{ MTAB_XTD | MTAB_VDV, 0, NULL, "13181A",
		&ms_settype, NULL, NULL },
	{ MTAB_XTD | MTAB_VDV, 1, NULL, "13183A",
		&ms_settype, NULL, NULL },
	{ MTAB_XTD | MTAB_VDV, 0, "TYPE", NULL,
		NULL, &ms_showtype, NULL },
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &msd_dev },
	{ 0 }  };

DEVICE msc_dev = {
	"MSC", msc_unit, msc_reg, msc_mod,
	MS_NUMDR, 10, 31, 1, 8, 8,
	NULL, NULL, &msc_reset,
	&msc_boot, &msc_attach, &msc_detach,
	&msc_dib, DEV_DISABLE };

/* IOT routines */

int32 msdio (int32 inst, int32 IR, int32 dat)
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
	msd_buf = dat;					/* store data */
	break;
case ioMIX:						/* merge */
	dat = dat | msd_buf;
	break;
case ioLIX:						/* load */
	dat = msd_buf;
	break;
case ioCTL:						/* control clear/set */
	if (IR & I_CTL) {				/* CLC */
	    clrCTL (devd);				/* clr ctl, cmd */
	    clrCMD (devd);  }
	else {						/* STC */
	    setCTL (devd);				/* set ctl, cmd */
	    setCMD (devd);  }
	break;
default:
	break;  }
if (IR & I_HC) { clrFLG (devd); }			/* H/C option */
return dat;
}

int32 mscio (int32 inst, int32 IR, int32 dat)
{
int32 i, devc, devd;
UNIT *uptr = msc_dev.units + msc_usl;
static const uint8 map_sel[16] = {
	0, 0, 1, 1, 2, 2, 2, 2,
	3, 3, 3, 3, 3, 3, 3, 3 };

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
	msc_buf = dat;
	msc_sta = msc_sta & ~STA_REJ;			/* clear reject */
	if ((dat & 0377) == FNC_CLR) break;		/* clear always ok */
	if (msc_sta & STA_BUSY) {			/* busy? reject */
	    msc_sta = msc_sta | STA_REJ;		/* dont chg select */
	    break;  }
	if (dat & FNF_CHS) {				/* select change */
	    msc_usl = map_sel[FNC_GETSEL (dat)];	/* is immediate */
	    uptr = msc_dev.units + msc_usl;  }
	if (((dat & FNF_MOT) && sim_is_active (uptr)) ||
	    ((dat & FNF_REV) && (uptr->UST & STA_BOT)) ||
	    ((dat & FNF_WRT) && (uptr->flags & UNIT_WPRT)))
	    msc_sta = msc_sta | STA_REJ;		/* reject? */
	break;
case ioLIX:						/* load */
	dat = 0;
case ioMIX:						/* merge */
	dat = dat | ((msc_sta | uptr->UST) & ~STA_DYN);
	if (uptr->flags & UNIT_ATT) {			/* online? */
	    if (sim_is_active (uptr))			/* busy */
		dat = dat | STA_TBSY;
	    if (uptr->flags & UNIT_WPRT)		/* write prot? */
		dat = dat | STA_WLK;  }
	else dat = dat | STA_TBSY | STA_LOCAL;
	if (ms_ctype) dat = dat | STA_PE |		/* 13183A? */
	    (msc_usl << STA_V_SEL);	
	break;
case ioCTL:						/* control clear/set */
	if (IR & I_CTL) { clrCTL (devc); }		/* CLC */
	else {						/* STC */
	    if ((msc_buf & 0377) == FNC_CLR) {		/* clear? */
		for (i = 0; i < MS_NUMDR; i++) {	/* loop thru units */
		    if (sim_is_active (&msc_unit[i]) &&	/* write in prog? */
			(msc_unit[i].FNC == FNC_WC) && (ms_ptr > 0))
			ms_wrtrec (uptr, ms_ptr | MTR_ERF);
		    if ((msc_unit[i].UST & STA_REW) == 0)
			sim_cancel (&msc_unit[i]);  }	/* stop if now rew */
		clrCTL (devc);				/* init device */
		setFLG (devc);
		clrCTL (devd);
		setFLG (devd);
		msc_sta = msd_buf = msc_buf = msc_1st = 0;
		return SCPE_OK;  }
	    uptr->FNC = msc_buf & 0377;			/* save function */
	    if (uptr->FNC & FNF_RWD)			/* rewind? */
	        sim_activate (uptr, msc_rtime);		/* fast response */
	    else sim_activate (uptr, msc_ctime);	/* schedule op */
	    uptr->UST = 0;				/* clear status */
	    msc_sta = STA_BUSY;				/* ctrl is busy */
	    msc_1st = 1;
	    setCTL (devc);  }				/* go */
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

t_stat msc_svc (UNIT *uptr)
{
int32 devc, devd, err, pnu;
static t_mtrlnt i, bceof = { MTR_TMK };

err = 0;						/* assume no errors */
devc = msc_dib.devno;					/* get device nos */
devd = msd_dib.devno;

if ((uptr->flags & UNIT_ATT) == 0) {			/* offline? */
	msc_sta = (msc_sta | STA_REJ) & ~STA_BUSY;	/* reject */
	setFLG (devc);					/* set cch flg */
	return IORETURN (msc_stopioe, SCPE_UNATT);  }

pnu = MT_TST_PNU (uptr);				/* get pos not upd */
MT_CLR_PNU (uptr);					/* and clear */

switch (uptr->FNC) {					/* case on function */
case FNC_REW:						/* rewind */
case FNC_RWS:						/* rewind offline */
	if (uptr->UST & STA_REW) {			/* rewind in prog? */
	    uptr->pos = 0;				/* done */
	    uptr->UST = STA_BOT;			/* set BOT status */
	    if (uptr->FNC & FNF_OFL) detach_unit (uptr);
	    return SCPE_OK;  }
	uptr->UST = STA_REW;				/* set rewinding */
	sim_activate (uptr, msc_ctime);			/* sched completion */
	break;						/* "done" */

case FNC_GFM:						/* gap file mark */
case FNC_WFM:						/* write file mark */
	fseek (uptr->fileref, uptr->pos, SEEK_SET);
	fxwrite (&bceof, sizeof (t_mtrlnt), 1, uptr->fileref);
	msc_sta = STA_EOF;				/* set EOF status */
	if (err = ferror (uptr->fileref)) MT_SET_PNU (uptr);
	else uptr->pos = uptr->pos + sizeof (t_mtrlnt);	/* update tape pos */
	break;

case FNC_GAP:						/* erase gap */
	break;

case FNC_FSF:
	while (ms_forwsp (uptr, &err)) ;		/* spc until EOF/EOT */
	break;

case FNC_FSR:						/* space forward */
	ms_forwsp (uptr, &err);
	break;

case FNC_BSF:
	while (ms_backsp (uptr, &err)) ;		/* spc until EOF/BOT */
	break;

case FNC_BSR:						/* space reverse */
	if (!pnu) {					/* position ok? */
	    ms_backsp (uptr, &err);			/* backspace */
	    if (msc_sta & STA_ODD) msc_sta = msc_sta | STA_PAR;  }
	break;

/* Unit service, continued */

case FNC_RFF:						/* diagnostic read */
case FNC_RC:						/* read */
	if (msc_1st) {					/* first svc? */
	    msc_1st = ms_ptr = 0;			/* clr 1st flop */
	    if (ms_rdlntf (uptr, &ms_max, &err)) {	/* read rec lnt */
		if (!err) {				/* tmk or eom? */
		    sim_activate (uptr, msc_gtime);	/* sched IRG */
		    uptr->FNC = 0;			/* NOP func */
		    return SCPE_OK;  }
		break;  }				/* err, done */
	    if (ms_max > DBSIZE) return SCPE_MTRLNT;	/* record too long? */
	    i = fxread (msxb, sizeof (int8), ms_max, uptr->fileref);
	    if (err = ferror (uptr->fileref)) {		/* error? */
		msc_sta = msc_sta | STA_PAR;		/* set flag */
		MT_SET_PNU (uptr);			/* pos not upd */
		break;  }
	    for ( ; i < ms_max; i++) msxb[i] = 0;	/* fill with 0's */
	    uptr->pos = uptr->pos + ((ms_max + 1) & ~1) +
		(2 * sizeof (t_mtrlnt));  }		/* update position */
	if (ms_ptr < ms_max) {				/* more chars? */
	    if (FLG (devd)) msc_sta = msc_sta | STA_TIM | STA_PAR;
	    msd_buf = ((uint16) msxb[ms_ptr] << 8) | msxb[ms_ptr + 1];
	    ms_ptr = ms_ptr + 2;
	    setFLG (devd);				/* set dch flg */
	    sim_activate (uptr, msc_xtime);		/* re-activate */
	    return SCPE_OK;  }
	sim_activate (uptr, msc_gtime);			/* sched IRG */
	if (uptr->FNC == FNC_RFF) msc_1st = 1;		/* diagnostic? */
	else uptr->FNC = 0;				/* NOP func */
	return SCPE_OK;

case FNC_WC:						/* write */
	if (msc_1st) msc_1st = ms_ptr = 0;		/* no xfer on first */
	else {						/* not 1st, next char */
	    if (ms_ptr < DBSIZE) {			/* room in buffer? */
		msxb[ms_ptr] = msd_buf >> 8;		/* store 2 char */
		msxb[ms_ptr + 1] = msd_buf & 0377;
		ms_ptr = ms_ptr + 2;
		uptr->UST = 0;  }
	    else msc_sta = msc_sta | STA_PAR;  }
	if (CTL (devd)) {				/* xfer flop set? */
	    setFLG (devd);				/* set dch flag */
	    sim_activate (uptr, msc_xtime);		/* re-activate */
	    return SCPE_OK;  }
	if (ms_ptr) {					/* any data? write */
	    if (err = ms_wrtrec (uptr, ms_ptr)) break;  }
	sim_activate (uptr, msc_gtime);			/* sched IRG */
	uptr->FNC = 0;					/* NOP func */
	return SCPE_OK;

default:						/* unknown */
	break;  }

setFLG (devc);						/* set cch flg */
msc_sta = msc_sta & ~STA_BUSY;				/* update status */
if (err != 0) {						/* I/O error */
	perror ("MT I/O error");
	clearerr (uptr->fileref);
	if (msc_stopioe) return SCPE_IOERR;  }
return SCPE_OK;
}

/* Tape motion routines */

t_bool ms_rdlntf (UNIT *uptr, t_mtrlnt *tbc, int32 *err)
{
fseek (uptr->fileref, uptr->pos, SEEK_SET);		/* position */
fxread (tbc, sizeof (t_mtrlnt), 1, uptr->fileref);	/* get bc */
if ((*err = ferror (uptr->fileref)) ||			/* error or eom? */ 
     feof (uptr->fileref) || (*tbc == MTR_EOM)) {
	msc_sta = msc_sta | STA_PAR;			/* error */
	MT_SET_PNU (uptr);				/* pos not upd */
	return TRUE;  }
if (*tbc == MTR_TMK) {					/* tape mark? */
	uptr->pos = uptr->pos + sizeof (t_mtrlnt);
	msc_sta = msc_sta | STA_EOF | STA_ODD;		/* eof (also sets odd) */
	return TRUE;  }
if (MTRF (*tbc)) msc_sta = msc_sta | STA_PAR;		/* error in rec? */
*tbc = MTRL (*tbc);					/* clear err flag */
if (*tbc & 1) msc_sta = msc_sta | STA_ODD;
else msc_sta = msc_sta & ~STA_ODD;
return FALSE;
}

t_bool ms_rdlntr (UNIT *uptr, t_mtrlnt *tbc, int32 *err)
{
if (uptr->pos < sizeof (t_mtrlnt)) {			/* at bot? */
	uptr->UST = STA_BOT;				/* set status */
	return TRUE;  }					/* error */
fseek (uptr->fileref, uptr->pos - sizeof (t_mtrlnt), SEEK_SET);
fxread (tbc, sizeof (t_mtrlnt), 1, uptr->fileref);	/* get bc */
if ((*err = ferror (uptr->fileref)) ||			/* error or eof? */ 
     feof (uptr->fileref)) {
	msc_sta = msc_sta | STA_PAR;			/* error */
	return TRUE;  }
if (*tbc == MTR_EOM) {					/* eom? */
	msc_sta = msc_sta | STA_PAR;			/* error */
	uptr->pos = uptr->pos - sizeof (t_mtrlnt);	/* spc over eom */
	return TRUE;  }
if (*tbc == MTR_TMK) {					/* tape mark? */
	msc_sta = msc_sta | STA_EOF;			/* eof */
	uptr->pos = uptr->pos - sizeof (t_mtrlnt);	/* spc over tmk */
	return TRUE;  }
if (MTRF (*tbc)) msc_sta = msc_sta | STA_PAR;		/* error in rec? */
*tbc = MTRL (*tbc);					/* clear err flag */
if (*tbc & 1) msc_sta = msc_sta | STA_ODD;
else msc_sta = msc_sta & ~STA_ODD;
return FALSE;
}

t_bool ms_forwsp (UNIT *uptr, int32 *err)
{
t_mtrlnt tbc;

if (ms_rdlntf (uptr, &tbc, err)) return FALSE;		/* read rec lnt, err? */
uptr->pos = uptr->pos + ((tbc + 1) & ~1) +		/* incr tape position */
	(2 * sizeof (t_mtrlnt));
return TRUE;
}

t_bool ms_backsp (UNIT *uptr, int32 *err)
{
t_mtrlnt tbc;

if (ms_rdlntr (uptr, &tbc, err)) return FALSE;		/* read rec lnt, err? */
uptr->pos = uptr->pos - ((MTRL (tbc) + 1) & ~1) -	/* decr tape position */
	(2 * sizeof (t_mtrlnt));
return TRUE;
}

int32 ms_wrtrec (UNIT *uptr, t_mtrlnt lnt)
{
int32 elnt = MTRL ((lnt + 1) & ~1);			/* even lnt, no err */

fseek (uptr->fileref, uptr->pos, SEEK_SET);		/* seek to record */
fxwrite (&lnt, sizeof (t_mtrlnt), 1, uptr->fileref);	/* write rec lnt */
fxwrite (msxb, sizeof (int8), elnt, uptr->fileref);	/* write data */
fxwrite (&lnt, sizeof (t_mtrlnt), 1, uptr->fileref);	/* write rec lnt */
if (ferror (uptr->fileref)) {				/* error? */
	MT_SET_PNU (uptr);				/* pos not updated */
	return 1;  }
else uptr->pos = uptr->pos + elnt + (2 * sizeof (t_mtrlnt)); /* no, upd pos */
return 0;
}

/* Reset routine */

t_stat msc_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

hp_enbdis_pair (&msc_dev, &msd_dev);			/* make pair cons */
msc_buf = msd_buf = 0;
msc_sta = msc_usl = 0;
msc_1st = 0;
msc_dib.cmd = msd_dib.cmd = 0;				/* clear cmd */
msc_dib.ctl = msd_dib.ctl = 0;				/* clear ctl */
msc_dib.flg = msd_dib.flg = 1;				/* set flg */
msc_dib.fbf = msd_dib.fbf = 1;				/* set fbf */
for (i = 0; i < MS_NUMDR; i++) {
	uptr = msc_dev.units + i;
	MT_CLR_PNU (uptr);
	sim_cancel (uptr);
	uptr->UST = 0;  }
return SCPE_OK;
}

/* Attach routine */

t_stat msc_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = attach_unit (uptr, cptr);				/* attach unit */
if (r != SCPE_OK) return r;				/* update status */
MT_CLR_PNU (uptr);
uptr->UST = STA_BOT;
return r;
}

/* Detach routine */

t_stat msc_detach (UNIT* uptr)
{
uptr->UST = 0;						/* update status */
MT_CLR_PNU (uptr);
return detach_unit (uptr);				/* detach unit */
}

/* Write lock/enable routine */

t_stat msc_vlock (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (val && (uptr->flags & UNIT_ATT)) return SCPE_ARG;
return SCPE_OK;
}

/* Set controller type */

t_stat ms_settype (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 i;

if ((val < 0) || (val > 1) || (cptr != NULL)) return SCPE_ARG;
for (i = 0; i < MS_NUMDR; i++) {
	if (msc_unit[i].flags & UNIT_ATT) return SCPE_ALATT;  }
ms_ctype = val;
return SCPE_OK;
}

/* Show controller type */

t_stat ms_showtype (FILE *st, UNIT *uptr, int32 val, void *desc)
{
if (ms_ctype) fprintf (st, "13183A");
else fprintf (st, "13181A");
return SCPE_OK;
}

/* 7970B/7970E bootstrap routine (HP 12992D ROM) */

#define CHANGE_DEV	(1 << 24)

static const int32 mboot[IBL_LNT] = {
	0106501,		/*ST LIB 1		; read sw */
	0006011,		/*   SLB,RSS		; bit 0 set? */
	0027714,		/*   JMP RD		; no read */
	0003004,		/*   CMA,INA		; A is ctr */
	0073775,		/*   STA WC		; save */
	0067772,		/*   LDA SL0RW		; sel 0, rew */
	0017762,		/*FF JSB CMD		; do cmd */
	0102301+CHANGE_DEV,	/*   SFS CC		; done? */
	0027707,		/*   JMP *-1		; wait */
	0067774,		/*   LDB FFC		; get file fwd */
	0037775,		/*   ISZ WC		; done files? */
	0027706,		/*   JMP FF		; no */
	0067773,		/*RD LDB RDCMD		; read cmd */
	0017762,		/*   JSB CMD		; do cmd */
	0103700+CHANGE_DEV,	/*   STC DC,C		; start dch */
	0102201+CHANGE_DEV,	/*   SFC CC		; read done? */
	0027752,		/*   JMP STAT		; no, get stat */
	0102300+CHANGE_DEV,	/*   SFS DC		; any data? */
	0027717,		/*   JMP *-3		; wait */
	0107500+CHANGE_DEV,	/*   LIB DC,C		; get rec cnt */
	0005727,		/*   BLF,BLF		; move to lower */
	0007000,		/*   CMB		; make neg */
	0077775,		/*   STA WC		; save */
	0102201+CHANGE_DEV,	/*   SFC CC		; read done? */
	0027752,		/*   JMP STAT		; no, get stat */
	0102300+CHANGE_DEV,	/*   SFS DC		; any data? */
	0027727,		/*   JMP *-3		; wait */
	0107500+CHANGE_DEV,	/*   LIB DC,C		; get load addr */
	0074000,		/*   STB 0		; start csum */
	0077762,		/*   STA CMD		; save address */
	0027742,		/*   JMP *+4 */
	0177762,		/*NW STB CMD,I		; store data */
	0040001,		/*   ADA 1		; add to csum */
	0037762,		/*   ISZ CMD		; adv addr ptr */
	0102300+CHANGE_DEV,	/*   SFS DC		; any data? */
	0027742,		/*   JMP *-1		; wait */
	0107500+CHANGE_DEV,	/*   LIB DC,C		; get word */
	0037775,		/*   ISZ WC		; done? */
	0027737,		/*   JMP NW		; no */
	0054000,		/*   CPB 0		; csum ok? */
	0027717,		/*   JMP RD+3		; yes, cont */
	0102011,		/*   HLT 11		; no, halt */
	0102501+CHANGE_DEV,	/*ST LIA CC		; get status */
	0001727,		/*   ALF,ALF		; get eof bit */
	0002020,		/*   SSA		; set? */
	0102077,		/*   HLT 77		; done */
	0001727,		/*   ALF,ALF		; put status back */
	0001310,		/*   RAR,SLA		; read ok? */
	0102000,		/*   HLT 0		; no */
	0027714,		/*   JMP RD		; read next */
	0000000,		/*CMD 0 */
	0106601+CHANGE_DEV,	/*   OTB CC		; output cmd */
	0102501+CHANGE_DEV,	/*   LIA CC		; check for reject */
	0001323,		/*   RAR,RAR */
	0001310,		/*   RAR,SLA */
	0027763,		/*   JMP CMD+1		; try again */
	0103701+CHANGE_DEV,	/*   STC CC,C		; start command */
	0127762,		/*   JMP CMD,I		; exit */
	0001501,		/*SL0RW 001501		; select 0, rewind */
	0001423,		/*RDCMD 001423		; read record */
	0000203,		/*FFC   000203		; space forward file */
	0000000,		/*WC    000000 */
	0000000,
	0000000 };

t_stat msc_boot (int32 unitno, DEVICE *dptr)
{
int32 i, dev;

if (unitno != 0) return SCPE_NOFNC;			/* only unit 0 */
dev = msd_dib.devno;					/* get data chan dev */
PC = ((MEMSIZE - 1) & ~IBL_MASK) & VAMASK;		/* start at mem top */
SR = IBL_MS + (dev << IBL_V_DEV);			/* set SR */
if ((sim_switches & SWMASK ('S')) && AR) SR = SR | 1;	/* skip? */
for (i = 0; i < IBL_LNT; i++) {				/* copy bootstrap */
	if (mboot[i] & CHANGE_DEV)			/* IO instr? */
		M[PC + i] = (mboot[i] + dev) & DMASK;
	else M[PC + i] = mboot[i];  }	
return SCPE_OK;
}
