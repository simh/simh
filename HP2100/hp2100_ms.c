/* hp2100_ms.c: HP 2100 13181A magnetic tape simulator

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

   ms		13181A nine track magnetic tape

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
#define UNIT_WLK	(1 << UNIT_V_WLK)
#define DB_N_SIZE	16				/* max data buf */
#define DBSIZE		(1 << DB_N_SIZE)		/* max data cmd */
#define DBMASK		(DBSIZE - 1)
#define UNIT_WPRT	(UNIT_WLK | UNIT_RO)		/* write protect */
#define FNC		u3				/* function */
#define UST		u4				/* unit status */

/* Command - msc_fnc */

#define FNC_CLR		0110				/* clear */
#define FNC_GAP		0015				/* write gap */
#define FNC_GFM		0215				/* gap+file mark */
#define FNC_RC		0023				/* read */
#define FNC_WC		0031				/* write */
#define FNC_FSR		0003				/* forward space */
#define FNC_BSR		0041				/* backward space */
#define FNC_FSF		0203				/* forward file */
#define FNC_BSF		0241				/* backward file */
#define FNC_REW		0101				/* rewind */
#define FNC_RWS		0105				/* rewind and offline */
#define FNC_WFM		0211				/* write file mark */
#define FNC_CHS		0400				/* change select */
#define FNC_V_SEL	9				/* select */
#define FNC_M_SEL	017
#define FNC_GETSEL(x)	(((x) >> FNC_V_SEL) & FNC_M_SEL)

#define FNF_MOT		0001				/* motion */
#define FNF_OFL		0004
#define FNF_WRT		0010				/* write */
#define FNF_REV		0040				/* reverse */
#define FNF_RWD		0100				/* rewind */

/* Status - stored in msc_sta, unit.UST (u), or dynamic (d) */

#define STA_ODD		04000				/* odd bytes */
#define STA_REW		02000				/* rewinding (u) */
#define STA_TBSY	01000				/* transport busy (d) */
#define STA_BUSY	00400				/* ctrl busy */
#define STA_EOF		00200				/* end of file */
#define STA_BOT		00100				/* beg of tape (d) */
#define STA_EOT		00040				/* end of tape (u) */
#define STA_TIM		00020				/* timing error */
#define STA_REJ		00010				/* programming error */
#define STA_WLK		00004				/* write locked (d) */
#define STA_PAR		00002				/* parity error */
#define STA_LOCAL	00001				/* local (d) */
#define STA_STATIC	(STA_ODD|STA_BUSY|STA_EOF|STA_TIM|STA_REJ|STA_PAR)

extern int32 PC;
extern int32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];
int32 msc_sta = 0;					/* status */
int32 msc_buf = 0;					/* buffer */
int32 msc_usl = 0;					/* unit select */
int32 msc_1st = 0;
int32 msc_ctime = 1000;					/* command wait */
int32 msc_xtime = 10;					/* data xfer time */
int32 msc_stopioe = 1;					/* stop on error */
int32 msd_buf = 0;					/* data buffer */
uint8 msxb[DBSIZE] = { 0 };				/* data buffer */
t_mtrlnt ms_ptr = 0, ms_max = 0;			/* buffer ptrs */

int32 msdio (int32 inst, int32 IR, int32 dat);
int32 mscio (int32 inst, int32 IR, int32 dat);
t_stat msc_svc (UNIT *uptr);
t_stat msc_reset (DEVICE *dptr);
t_stat msc_vlock (UNIT *uptr, int32 val, char *cptr, void *desc);
t_bool ms_forwsp (UNIT *uptr, int32 *err);
t_bool ms_backsp (UNIT *uptr, int32 *err);

/* MSD data structures

   msd_dev	MSD device descriptor
   msd_unit	MSD unit list
   msd_reg	MSD register list
*/

DIB ms_dib[] = {
	{ MSD, 1, 0, 0, 0, 0, &msdio },
	{ MSC, 1, 0, 0, 0, 0, &mscio }  };

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
	{ FLDATA (*DEVENB, msd_dib.enb, 0), REG_HRO },
	{ NULL }  };

MTAB msd_mod[] = {
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &msd_dib },
	{ 0 }  };

DEVICE msd_dev = {
	"MSD", &msd_unit, msd_reg, msd_mod,
	1, 10, DB_N_SIZE, 1, 8, 8,
	NULL, NULL, &msc_reset,
	NULL, NULL, NULL };

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
	{ URDATA (POS, msc_unit[0].pos, 8, 32, 0, MS_NUMDR, PV_LEFT) },
	{ URDATA (FNC, msc_unit[0].FNC, 8, 12, 0, MS_NUMDR, REG_HRO) },
	{ URDATA (UST, msc_unit[0].UST, 8, 12, 0, MS_NUMDR, REG_HRO) },
	{ DRDATA (CTIME, msc_ctime, 24), REG_NZ + PV_LEFT },
	{ DRDATA (XTIME, msc_xtime, 24), REG_NZ + PV_LEFT },
	{ FLDATA (STOP_IOE, msc_stopioe, 0) },
	{ URDATA (WLK, msc_unit[0].flags, 8, 1, UNIT_V_WLK, MS_NUMDR, REG_HRO) },
	{ ORDATA (DEVNO, msc_dib.devno, 6), REG_HRO },
	{ FLDATA (*DEVENB, msc_dib.enb, 0), REG_HRO },
	{ NULL }  };

MTAB msc_mod[] = {
	{ UNIT_WLK, 0, "write enabled", "WRITEENABLED", &msc_vlock },
	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", &msc_vlock }, 
	{ MTAB_XTD | MTAB_VDV, 1, NULL, "ENABLED",
		&set_enb, NULL, &msd_dib },
	{ MTAB_XTD | MTAB_VDV, 1, NULL, "DISABLED",
		&set_dis, NULL, &msd_dib },
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &msd_dib },
	{ 0 }  };

DEVICE msc_dev = {
	"MSC", msc_unit, msc_reg, msc_mod,
	MS_NUMDR, 10, 31, 1, 8, 8,
	NULL, NULL, &msc_reset,
	NULL, NULL, NULL };

/* IOT routines */

int32 msdio (int32 inst, int32 IR, int32 dat)
{
int32 devd;

devd = IR & DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & HC) == 0) { setFLG (devd); }		/* STF */
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
	if (IR & AB) {					/* CLC */
		clrCTL (devd);				/* clr ctl, cmd */
		clrCMD (devd);  }
	else {	setCTL (devd);				/* STC */
		setCMD (devd);  }			/* set ctl, cmd */
	break;
default:
	break;  }
if (IR & HC) { clrFLG (devd); }				/* H/C option */
return dat;
}

int32 mscio (int32 inst, int32 IR, int32 dat)
{
int32 i, devc, devd;
UNIT *uptr = msc_dev.units + msc_usl;
static const uint8 map_sel[16] = {
	0, 0, 1, 1, 2, 2, 2, 2,
	3, 3, 3, 3, 3, 3, 3, 3 };

devc = IR & DEVMASK;					/* get device no */
devd = devc - 1;
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & HC) == 0) { setFLG (devc); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (devc) == 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (devc) != 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioOTX:						/* output */
	msc_buf = dat = dat & 07777;
	if (dat == FNC_CLR) {				/* clear? */
		for (i = 0; i < MS_NUMDR; i++) {
			if ((msc_unit[i].UST & STA_REW) == 0)
				sim_cancel (&msc_unit[i]);  }
		clrCTL (devc);				/* init device */
		clrFLG (devc);
		clrCTL (devd);
		clrFLG (devd);
		msc_sta = msd_buf = msc_buf = msc_1st = 0;
		break;  }
	if (msc_sta & STA_BUSY) {
		msc_sta = msc_sta | STA_REJ;
		break;  }
	if (dat & FNC_CHS) {
		msc_usl = map_sel[FNC_GETSEL (dat)];
		uptr = msc_dev.units + msc_usl;  }
	if (((dat & FNF_MOT) && sim_is_active (uptr)) ||
	    ((dat & FNF_REV) && (uptr -> pos == 0)) ||
	    ((dat & FNF_WRT) && (uptr -> flags & UNIT_WPRT)))
		msc_sta = msc_sta | STA_REJ;
	break;
case ioLIX:						/* load */
	dat = 0;
case ioMIX:						/* merge */
	msc_sta = (msc_sta & STA_STATIC) | uptr -> UST;
	if (uptr -> flags & UNIT_ATT) {
		msc_sta = msc_sta & ~(STA_LOCAL | STA_WLK | STA_TBSY);
		if (sim_is_active (uptr))
			msc_sta = msc_sta | STA_TBSY;
		if (uptr -> flags & UNIT_WPRT)
			msc_sta = msc_sta | STA_WLK;  }
	else msc_sta = msc_sta | STA_TBSY | STA_LOCAL;	
	dat = dat | msc_sta;		
	break;
case ioCTL:						/* control clear/set */
	if (IR & AB) { clrCTL (devc); }			/* CLC */
	else if (!CTL (devc)) {				/* STC, not busy? */
		uptr -> FNC = msc_buf;			/* save function */
		if (uptr -> FNC & FNF_RWD) {
			uptr -> UST = STA_REW;
			sim_activate (uptr, msc_xtime);  }
		else {	uptr -> UST = 0;		/* clr unit status */
			sim_activate (uptr, msc_ctime);  }
		msc_sta = STA_BUSY;
		msc_1st = 1;
		setCTL (devc);  }
	break;
default:
	break;  }
if (IR & HC) { clrFLG (devc); }				/* H/C option */
return dat;
}

/* Unit service

   If rewind done, reposition to start of tape, set status
   else, do operation, set done, interrupt

   Can't be write locked, can only write lock detached unit
*/

t_stat msc_svc (UNIT *uptr)
{
int32 devc, devd, err, i;
static t_mtrlnt bceof = { 0 };

if ((uptr -> flags & UNIT_ATT) == 0) {			/* offline? */
	msc_sta = STA_LOCAL | STA_BUSY | STA_REJ;
	return IORETURN (msc_stopioe, SCPE_UNATT);  }
devc = msc_dib.devno;					/* get device nos */
devd = msd_dib.devno;

if (uptr -> UST & STA_REW) {				/* rewinding? */
	if (msc_sta & STA_BUSY) {			/* controller busy? */
		sim_activate (uptr, msc_ctime);		/* do real rewind */
		setFLG (devc);				/* set cch flg */
		msc_sta = msc_sta & ~STA_BUSY;  }	/* update status */
	else {	uptr -> pos = 0;			/* rewind done */
		uptr -> UST = 0;			/* offline? */
		if (uptr -> FNC & FNF_OFL) detach_unit (uptr);  }
	return SCPE_OK;  }

err = 0;						/* assume no errors */
switch (uptr -> FNC & 07777) {				/* case on function */
case FNC_GFM:						/* gap file mark */
case FNC_WFM:						/* write file mark */
	fseek (uptr -> fileref, uptr -> pos, SEEK_SET);
	fxwrite (&bceof, sizeof (t_mtrlnt), 1, uptr -> fileref);
	err = ferror (uptr -> fileref);
	uptr -> pos = uptr -> pos + sizeof (t_mtrlnt);	/* update tape pos */
	msc_sta = msc_sta | STA_EOF;			/* set EOF status */
case FNC_GAP:						/* erase gap */
	break;

case FNC_FSF:
	while (ms_forwsp (uptr, &err)) ;		/* spc until EOF/EOT */
	break;
case FNC_FSR:						/* space forward */
	ms_forwsp (uptr, &err);
	break;
case FNC_BSF:
	while (ms_backsp (uptr, &err)) ;		/* spc until EOF/EOT */
	break;
case FNC_BSR:						/* space reverse */
	ms_backsp (uptr, &err);
	break;

/* Unit service, continued */

case FNC_RC:						/* read */
	if (msc_1st) {					/* first svc? */
		msc_1st = ms_ptr = 0;				/* clr 1st flop */
		fseek (uptr -> fileref, uptr -> pos, SEEK_SET);
		fxread (&ms_max, sizeof (t_mtrlnt), 1, uptr -> fileref);
		if ((err = ferror (uptr -> fileref)) ||
		     feof (uptr -> fileref)) {		/* error or eof? */
			uptr -> UST = STA_EOT;
			break;  }
		if (ms_max == 0) {			/* tape mark? */
			uptr -> pos = uptr -> pos + sizeof (t_mtrlnt);
			msc_sta = msc_sta | STA_EOF;
			break;  }
		ms_max = MTRL (ms_max);			/* ignore errors */
		uptr -> pos = uptr -> pos + ((ms_max + 1) & ~1) +
			(2 * sizeof (t_mtrlnt));	/* update position */
		if (ms_max > DBSIZE) return SCPE_MTRLNT;/* record too long? */
		i = fxread (msxb, sizeof (int8), ms_max, uptr -> fileref);
		for ( ; i < ms_max; i++) msxb[i] = 0;	/* fill with 0's */
		err = ferror (uptr -> fileref);  }
	if (ms_ptr < ms_max) {				/* more chars? */
		if (FLG (devd)) msc_sta = msc_sta | STA_TIM;
		msd_buf = ((uint16) msxb[ms_ptr] << 8) |
			msxb[ms_ptr + 1];
		ms_ptr = ms_ptr + 2;
		setFLG (devd);				/* set dch flg */
		sim_activate (uptr, msc_xtime);		/* re-activate */
		return SCPE_OK;  }
	if (ms_max & 1) msc_sta = msc_sta | STA_ODD;
	break;
case FNC_WC:						/* write */
	if (msc_1st) msc_1st = ms_ptr = 0;
	else {	if (ms_ptr < DBSIZE) {			/* room in buffer? */
			msxb[ms_ptr] = msd_buf >> 8;
			msxb[ms_ptr + 1] = msd_buf & 0377;
			ms_ptr = ms_ptr + 2;  }
		else msc_sta = msc_sta | STA_PAR;  }
	if (CTL (devd)) {				/* xfer flop set? */
		setFLG (devd);				/* set dch flag */
		sim_activate (uptr, msc_xtime);		/* re-activate */
		return SCPE_OK;  }
	if (ms_ptr) {					/* write buffer */
		fseek (uptr -> fileref, uptr -> pos, SEEK_SET);
		fxwrite (&ms_ptr, sizeof (t_mtrlnt), 1, uptr -> fileref);
		fxwrite (msxb, sizeof (int8), ms_ptr, uptr -> fileref);
		fxwrite (&ms_ptr, sizeof (t_mtrlnt), 1, uptr -> fileref);
		err = ferror (uptr -> fileref);
		uptr -> pos = uptr -> pos + ((ms_ptr + 1) & ~1) +
			(2 * sizeof (t_mtrlnt));  }
	break;  }

/* Unit service, continued */

setFLG (devc);						/* set cch flg */
msc_sta = msc_sta & ~STA_BUSY;				/* update status */
if (err != 0) {						/* I/O error */
	perror ("MT I/O error");
	clearerr (uptr -> fileref);
	IORETURN (msc_stopioe, SCPE_IOERR);  }
return SCPE_OK;
}

t_bool ms_forwsp (UNIT *uptr, int32 *err)
{
t_mtrlnt tbc;

fseek (uptr -> fileref, uptr -> pos, SEEK_SET);		/* position */
fxread (&tbc, sizeof (t_mtrlnt), 1, uptr -> fileref);	/* get bc */
if ((*err = ferror (uptr -> fileref)) || feof (uptr -> fileref)) {
	uptr -> UST = STA_EOT;
	return FALSE;  }
if (tbc == 0) {						/* zero bc? */
	uptr -> pos = uptr -> pos + sizeof (t_mtrlnt);
	msc_sta = msc_sta | STA_EOF;			/* eof */
	return FALSE;  }
uptr -> pos = uptr -> pos + ((MTRL (tbc) + 1) & ~1) + (2 * sizeof (t_mtrlnt));
return TRUE;
}

t_bool ms_backsp (UNIT *uptr, int32 *err)
{
t_mtrlnt tbc;

if (uptr -> pos == 0) return FALSE;			/* at bot? */
fseek (uptr -> fileref, uptr -> pos - sizeof (t_mtrlnt), SEEK_SET);
fxread (&tbc, sizeof (t_mtrlnt), 1, uptr -> fileref);	/* get bc */
if ((*err = ferror (uptr -> fileref)) || feof (uptr -> fileref)) {
	uptr -> UST = STA_EOT;
	uptr -> pos = 0;
	return FALSE;  }
if (tbc == 0) {						/* zero bc? */
	uptr -> pos = uptr -> pos - sizeof (t_mtrlnt);
	msc_sta = msc_sta | STA_EOF;			/* eof */
	return FALSE;  }
uptr -> pos = uptr -> pos - ((MTRL (tbc) + 1) & ~1) - (2 * sizeof (t_mtrlnt));
return TRUE;
}

/* Reset routine */

t_stat msc_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

msc_buf = msd_buf = 0;
msc_sta = msc_usl = 0;
msc_1st = 0;
msc_dib.cmd = msd_dib.cmd = 0;				/* clear cmd */
msc_dib.ctl = msd_dib.ctl = 0;				/* clear ctl */
msc_dib.flg = msd_dib.flg = 0;				/* clear flg */
msc_dib.fbf = msd_dib.fbf = 0;				/* clear fbf */
for (i = 0; i < MS_NUMDR; i++) {
	uptr = msc_dev.units + i;
	sim_cancel (uptr);
	uptr -> UST = 0;  }
return SCPE_OK;
}

/* Write lock/enable routine */

t_stat msc_vlock (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (val && (uptr -> flags & UNIT_ATT)) return SCPE_ARG;
return SCPE_OK;
}
