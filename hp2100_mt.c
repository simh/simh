/* hp2100_mt.c: HP 2100 magnetic tape simulator

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

   mt		12559A nine track magnetic tape

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
#define UNIT_WLK	(1 << UNIT_V_WLK)
#define DB_V_SIZE	16				/* max data buf */
#define DBSIZE		(1 << DB_V_SIZE)		/* max data cmd */
#define DBMASK		(DBSIZE - 1)

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

/* Status - stored in mtc_sta */

#define STA_LOCAL	0400				/* local */
#define STA_EOF		0200				/* end of file */
#define STA_BOT		0100				/* beginning of tape */
#define STA_EOT		0040				/* end of tape */
#define STA_TIM		0020				/* timing error */
#define STA_REJ		0010				/* programming error */
#define STA_WLK		0004				/* write locked */
#define STA_PAR		0002				/* parity error */
#define STA_BUSY	0001				/* busy */

extern uint16 M[];
extern struct hpdev infotab[];
extern int32 PC;
extern int32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];
int32 mtc_fnc = 0;					/* function */
int32 mtc_sta = 0;					/* status register */
int32 mtc_dtf = 0;					/* data xfer flop */
int32 mtc_1st = 0;					/* first svc flop */
int32 mtc_ctime = 1000;					/* command wait */
int32 mtc_xtime = 10;					/* data xfer time */
int32 mtc_stopioe = 1;					/* stop on error */
uint8 mt_buf[DBSIZE] = { 0 };				/* data buffer */
t_mtrlnt mt_ptr = 0, mt_max = 0;			/* buffer ptrs */
static const int32 mtc_cmd[] = {
 FNC_WC, FNC_RC, FNC_GAP, FNC_FSR, FNC_BSR, FNC_REW, FNC_RWS, FNC_WFM };

t_stat mtc_svc (UNIT *uptr);
t_stat mtc_reset (DEVICE *dptr);
t_stat mtc_vlock (UNIT *uptr, int32 val);
t_stat mtc_attach (UNIT *uptr, char *cptr);
t_stat mtc_detach (UNIT *uptr);
t_stat mtd_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat mtd_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);

/* MTD data structures

   mtd_dev	MTD device descriptor
   mtd_unit	MTD unit list
   mtd_reg	MTD register list
*/

UNIT mtd_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, DBSIZE) };

REG mtd_reg[] = {
	{ FLDATA (CMD, infotab[inMTD].cmd, 0), REG_HRO },
	{ FLDATA (CTL, infotab[inMTD].ctl, 0), REG_HRO },
	{ FLDATA (FLG, infotab[inMTD].flg, 0) },
	{ FLDATA (FBF, infotab[inMTD].fbf, 0), REG_HRO },
	{ DRDATA (BPTR, mt_ptr, DB_V_SIZE + 1) },
	{ DRDATA (BMAX, mt_max, DB_V_SIZE + 1) },
	{ ORDATA (DEVNO, infotab[inMTD].devno, 6), REG_RO },
	{ NULL }  };

DEVICE mtd_dev = {
	"MTD", &mtd_unit, mtd_reg, NULL,
	1, 10, 16, 1, 8, 8,
	&mtd_ex, &mtd_dep, &mtc_reset,
	NULL, NULL, NULL };

/* MTC data structures

   mtc_dev	MTC device descriptor
   mtc_unit	MTC unit list
   mtc_reg	MTC register list
   mtc_mod	MTC modifier list
*/

UNIT mtc_unit = { UDATA (&mtc_svc, UNIT_ATTABLE, 0) };

REG mtc_reg[] = {
	{ ORDATA (FNC, mtc_fnc, 8) },
	{ ORDATA (STA, mtc_sta, 9) },
	{ ORDATA (BUF, mtc_unit.buf, 8) },
	{ FLDATA (CMD, infotab[inMTC].cmd, 0), REG_HRO },
	{ FLDATA (CTL, infotab[inMTC].ctl, 0) },
	{ FLDATA (FLG, infotab[inMTC].flg, 0) },
	{ FLDATA (FBF, infotab[inMTC].fbf, 0) },
	{ FLDATA (DTF, mtc_dtf, 0) },
	{ FLDATA (FSVC, mtc_1st, 0) },
	{ DRDATA (POS, mtc_unit.pos, 31), PV_LEFT },
	{ DRDATA (CTIME, mtc_ctime, 24), REG_NZ + PV_LEFT },
	{ DRDATA (XTIME, mtc_xtime, 24), REG_NZ + PV_LEFT },
	{ FLDATA (STOP_IOE, mtc_stopioe, 0) },
	{ FLDATA (WLK, mtc_unit.flags, UNIT_V_WLK), REG_HRO },
	{ ORDATA (CDEVNO, infotab[inMTC].devno, 6), REG_RO },
	{ NULL }  };

MTAB mtc_mod[] = {
	{ UNIT_WLK, 0, "write enabled", "ENABLED", &mtc_vlock },
	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", &mtc_vlock }, 
	{ UNIT_DEVNO, inMTD, NULL, "DEVNO", &hp_setdev2 },
	{ 0 }  };

DEVICE mtc_dev = {
	"MTC", &mtc_unit, mtc_reg, mtc_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &mtc_reset,
	NULL, &mtc_attach, &mtc_detach };

/* IOT routines */

int32 mtdio (int32 inst, int32 IR, int32 dat)
{
int32 devd;

devd = IR & DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & HC) == 0) { setFLG (devd); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (devd) == 0) PC = (PC + 1) & AMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (devd) != 0) PC = (PC + 1) & AMASK;
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
	if (IR & AB) mtc_dtf = 0;			/* CLC: clr xfer flop */
	break;
default:
	break;  }
if (IR & HC) { clrFLG (devd); }				/* H/C option */
return dat;
}

int32 mtcio (int32 inst, int32 IR, int32 dat)
{
int32 i, devc, devd, valid;

devc = IR & DEVMASK;					/* get device no */
devd = devc - 1;
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & HC) == 0) { setFLG (devc); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (devc) == 0) PC = (PC + 1) & AMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (devc) != 0) PC = (PC + 1) & AMASK;
	return dat;
case ioOTX:						/* output */
	dat = dat & 0377;
	if (dat == FNC_CLR) {				/* clear? */
		if (((mtc_fnc == FNC_REW) || (mtc_fnc == FNC_RWS)) &&
			sim_is_active (&mtc_unit)) break;
		mtc_reset (&mtc_dev);			/* if not rewind, */
		clrCTL (devc);				/* init device */
		clrFLG (devc);
		clrCTL (devd);
		clrFLG (devd);
		break;  }
	for (i = valid = 0; i < sizeof (mtc_cmd); i++)	/* is fnc valid? */
		if (dat == mtc_cmd[i]) valid = 1;
	if (!valid || sim_is_active (&mtc_unit) ||	/* is cmd valid? */
          ((mtc_unit.flags & UNIT_ATT) == 0) ||
	    ((mtc_sta & STA_BOT) &&
		((dat == FNC_BSR) || (dat == FNC_REW) || (dat == FNC_RWS))) ||
	    ((mtc_unit.flags & UNIT_WLK) && 
		((dat == FNC_WC) || (dat == FNC_GAP) || (dat == FNC_WFM))))
		mtc_sta = mtc_sta | STA_REJ;
	else {	sim_activate (&mtc_unit, mtc_ctime);	/* start tape */
		mtc_fnc = dat;				/* save function */
		mtc_sta = STA_BUSY;			/* unit busy */
		mt_ptr = 0;				/* init buffer ptr */
		clrFLG (devc);				/* clear flags */
		clrFLG (devd);
		mtc_1st = 1;				/* set 1st flop */
		mtc_dtf = 1;  }				/* set xfer flop */
	break;
case ioLIX:						/* load */
	dat = 0;
case ioMIX:						/* merge */
	if (mtc_unit.flags & UNIT_ATT) {		/* construct status */
		mtc_sta = mtc_sta & ~(STA_LOCAL | STA_WLK | STA_BUSY);
		if (sim_is_active (&mtc_unit)) mtc_sta = mtc_sta | STA_BUSY;
		if (mtc_unit.flags & UNIT_WLK) mtc_sta = mtc_sta | STA_WLK;  }
	else mtc_sta = STA_BUSY | STA_LOCAL;	
	dat = dat | mtc_sta;		
	break;
case ioCTL:						/* control clear/set */
	if (IR & AB) { clrCTL (devc); }			/* CLC */
	else { setCTL (devc); }				/* STC */
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

t_stat mtc_svc (UNIT *uptr)
{
int32 devc, devd, err, i;
static t_mtrlnt bceof = { 0 };

if ((mtc_unit.flags & UNIT_ATT) == 0) {			/* offline? */
	mtc_sta = STA_LOCAL | STA_BUSY | STA_REJ;
	return IORETURN (mtc_stopioe, SCPE_UNATT);  }
devc = infotab[inMTC].devno;				/* get device nos */
devd = infotab[inMTD].devno;

err = 0;						/* assume no errors */
switch (mtc_fnc & 0377) {				/* case on function */
case FNC_REW:						/* rewind */
	mtc_unit.pos = 0;				/* BOT */
	setFLG (devc);					/* set cch flg */
	mtc_sta = (mtc_sta | STA_BOT) & ~STA_BUSY;	/* update status */
	break;
case FNC_RWS:						/* rewind and offline */
	mtc_unit.pos = 0;				/* BOT */
	mtc_sta = STA_LOCAL | STA_BUSY;			/* set status */
	return detach_unit (uptr);			/* don't set cch flg */
case FNC_WFM:						/* write file mark */
	fseek (mtc_unit.fileref, mtc_unit.pos, SEEK_SET);
	fxwrite (&bceof, sizeof (t_mtrlnt), 1, mtc_unit.fileref);
	err = ferror (mtc_unit.fileref);
	mtc_unit.pos = mtc_unit.pos + sizeof (t_mtrlnt); /* update tape pos */
	mtc_sta = mtc_sta | STA_EOF;			/* set EOF status */
case FNC_GAP:						/* erase gap */
	setFLG (devc);					/* set cch flg */
	mtc_sta = mtc_sta & ~STA_BUSY;			/* update status */
	break;

/* Unit service, continued */

case FNC_FSR:						/* space forward */
	setFLG (devc);					/* set cch flg */
	mtc_sta = mtc_sta & ~STA_BUSY;			/* update status */
	fseek (mtc_unit.fileref, mtc_unit.pos, SEEK_SET);
	fxread (&mt_max, sizeof (t_mtrlnt), 1, mtc_unit.fileref);
	if ((err = ferror (mtc_unit.fileref)) ||	/* error or eof? */
	     feof (mtc_unit.fileref)) mtc_sta = mtc_sta | STA_EOT;
	else if (mt_max == 0) {				/* zero bc? */
		mtc_sta = mtc_sta | STA_EOF;		/* EOF */
		mtc_unit.pos = mtc_unit.pos + sizeof (t_mtrlnt);  }
	else mtc_unit.pos = mtc_unit.pos + ((MTRL (mt_max) + 1) & ~1) +
			(2 * sizeof (t_mtrlnt));	/* update position */
	break;
case FNC_BSR:						/* space reverse */
	setFLG (devc);					/* set cch flg */
	mtc_sta = mtc_sta & ~STA_BUSY;			/* update status */
	if (mtc_unit.pos == 0) {			/* at BOT? */
		mtc_sta = mtc_sta | STA_BOT;		/* update status */
		break;  }
	fseek (mtc_unit.fileref, mtc_unit.pos - sizeof (t_mtrlnt), SEEK_SET);
	fxread (&mt_max, sizeof (t_mtrlnt), 1, mtc_unit.fileref);
	if ((err = ferror (mtc_unit.fileref)) || 	/* error or eof? */
	     feof (mtc_unit.fileref)) mtc_unit.pos = 0;
	else if (mt_max == 0) {				/* zero bc? */
		mtc_sta = mtc_sta | STA_EOF;		/* EOF */
		mtc_unit.pos = mtc_unit.pos - sizeof (t_mtrlnt);  }
	else mtc_unit.pos = mtc_unit.pos - ((MTRL (mt_max) + 1) & ~1) -
		(2 * sizeof (t_mtrlnt));		/* update position */
	if (mtc_unit.pos == 0) mtc_sta = mtc_sta | STA_BOT;
	break;

/* Unit service, continued */

case FNC_RC:						/* read */
	if (mtc_1st) {					/* first svc? */
		mtc_1st = 0;				/* clr 1st flop */
		fseek (mtc_unit.fileref, mtc_unit.pos, SEEK_SET);
		fxread (&mt_max, sizeof (t_mtrlnt), 1, mtc_unit.fileref);
		if ((err = ferror (mtc_unit.fileref)) ||
			feof (mtc_unit.fileref)) {	/* error or eof? */
			setFLG (devc);			/* set cch flg */
			mtc_sta = (mtc_sta | STA_EOT) & ~STA_BUSY;
			break;  }
		if (mt_max == 0) {			/* tape mark? */
			mtc_unit.pos = mtc_unit.pos + sizeof (t_mtrlnt);
			setFLG (devc);			/* set cch flg */
			mtc_sta = (mtc_sta | STA_EOF) & ~STA_BUSY;
			break;  }
		mt_max = MTRL (mt_max);			/* ignore errors */
		mtc_unit.pos = mtc_unit.pos + ((mt_max + 1) & ~1) +
			(2 * sizeof (t_mtrlnt));	/* update position */
		if ((mt_max > DBSIZE) || (mt_max < 12)) {
			setFLG (devc);			/* set cch flg */
			mtc_sta = (mtc_sta | STA_PAR) & ~STA_BUSY;
			break;  }
		i = fxread (mt_buf, sizeof (int8), mt_max, mtc_unit.fileref);
		for ( ; i < mt_max; i++) mt_buf[i] = 0;	/* fill with 0's */
		err = ferror (mtc_unit.fileref);  }
	if (mt_ptr < mt_max) {				/* more chars? */
		if (FLG (devd)) mtc_sta = mtc_sta | STA_TIM;
		mtc_unit.buf = mt_buf[mt_ptr++];		/* fetch next */
		setFLG (devd);				/* set dch flg */
		sim_activate (uptr, mtc_xtime);  }	/* re-activate */
	else {	setFLG (devc);				/* set cch flg */
		mtc_sta = mtc_sta & ~STA_BUSY;  }	/* update status */
	break;
case FNC_WC:						/* write */
	if (mtc_dtf) {					/* xfer flop set? */
		if (!mtc_1st) {				/* not first? */
			if (mt_ptr < DBSIZE)		/* room in buffer? */
				mt_buf[mt_ptr++] = mtc_unit.buf;
			else mtc_sta = mtc_sta | STA_PAR;  }
		mtc_1st = 0;				/* clr 1st flop */
		setFLG (devd);				/* set dch flag */
		sim_activate (uptr, mtc_xtime);		/* re-activate */
		break;  }
	if (mt_ptr) {					/* write buffer */
		fseek (mtc_unit.fileref, mtc_unit.pos, SEEK_SET);
		fxwrite (&mt_ptr, sizeof (t_mtrlnt), 1, mtc_unit.fileref);
		fxwrite (mt_buf, sizeof (int8), mt_ptr, mtc_unit.fileref);
		fxwrite (&mt_ptr, sizeof (t_mtrlnt), 1, mtc_unit.fileref);
		err = ferror (mtc_unit.fileref);
		mtc_unit.pos = mtc_unit.pos + ((mt_ptr + 1) & ~1) +
			(2 * sizeof (t_mtrlnt));  }
	setFLG (devc);					/* set cch flg */
	mtc_sta = mtc_sta & ~STA_BUSY;			/* update status */
	break;  }

/* Unit service, continued */

if (err != 0) {						/* I/O error */
	perror ("MT I/O error");
	clearerr (mtc_unit.fileref);
	IORETURN (mtc_stopioe, SCPE_IOERR);  }
return SCPE_OK;
}

/* Reset routine */

t_stat mtc_reset (DEVICE *dptr)
{
mtc_fnc = 0;
infotab[inMTC].cmd = infotab[inMTD].cmd = 0;		/* clear cmd */
infotab[inMTC].ctl = infotab[inMTD].ctl = 0;		/* clear ctl */
infotab[inMTC].flg = infotab[inMTD].flg = 0;		/* clear flg */
infotab[inMTC].fbf = infotab[inMTD].fbf = 0;		/* clear fbf */
sim_cancel (&mtc_unit);					/* cancel activity */
if (mtc_unit.flags & UNIT_ATT) mtc_sta = ((mtc_unit.pos)? 0: STA_BOT) |
	((mtc_unit.flags & UNIT_WLK)? STA_WLK: 0);
else mtc_sta = STA_LOCAL | STA_BUSY;
return SCPE_OK;
}

/* Attach routine */

t_stat mtc_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = attach_unit (uptr, cptr);				/* attach unit */
if (r != SCPE_OK) return r;				/* update status */
mtc_sta = STA_BOT | ((uptr -> flags & UNIT_WLK)? STA_WLK: 0);
return r;
}

/* Detach routine */

t_stat mtc_detach (UNIT* uptr)
{
mtc_sta = STA_LOCAL | STA_BUSY;				/* update status */
return detach_unit (uptr);				/* detach unit */
}

/* Write lock/enable routine */

t_stat mtc_vlock (UNIT *uptr, int32 val)
{
if (uptr -> flags & UNIT_ATT) return SCPE_ARG;
return SCPE_OK;
}

/* Buffer examine */

t_stat mtd_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= DBSIZE) return SCPE_NXM;
if (vptr != NULL) *vptr = mt_buf[addr] & 0377;
return SCPE_OK;
}

/* Buffer deposit */

t_stat mtd_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= DBSIZE) return SCPE_NXM;
mt_buf[addr] = val & 0377;
return SCPE_OK;
}
