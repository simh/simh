/* hp2100_dq.c: HP 2100 12565A disk simulator

   Copyright (c) 1993-2002, Robert M. Supnik
   Modified from hp2100_dp.c by Bill McDermith; used by permission

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

   dq		12565A 2883/2884 disk system

   09-Jan-02	WOM	Copied dp driver and mods for 2883
*/

#include "hp2100_defs.h"

#define UNIT_V_WLK	(UNIT_V_UF + 0)			/* write locked */
#define UNIT_WLK	(1 << UNIT_V_WLK)
#define UNIT_W_UF	2				/* # flags */
#define FNC		u3				/* saved function */
#define CYL		u4				/* cylinder */

#define DQ_N_NUMWD	7
#define DQ_NUMWD	(1 << DQ_N_NUMWD)		/* words/sector */
#define DQ_NUMSC	23				/* sectors/track */
#define DQ_NUMSF	20				/* tracks/cylinder */
#define DQ_NUMCY	203				/* cylinders/disk */
#define DQ_SIZE		(DQ_NUMSF * DQ_NUMCY * DQ_NUMSC * DQ_NUMWD)
#define DQ_NUMDRV	2				/* # drives */

/* Command word */

#define CW_V_FNC	12				/* function */
#define CW_M_FNC	017
#define CW_GETFNC(x)	(((x) >> CW_V_FNC) & CW_M_FNC)
/*			000				/* unused */
#define  FNC_STA	001				/* status check */
#define  FNC_RCL	002				/* recalibrate */
#define  FNC_SEEK	003				/* seek */
#define  FNC_RD		004				/* read */
#define  FNC_WD		005				/* write */
#define  FNC_RA		006				/* read address */
#define  FNC_WA		007				/* write address */
#define  FNC_CHK	010				/* check */
#define  FNC_LA		013				/* load address */
#define  FNC_AS		014				/* address skip */

#define	 FNC_SEEK1	020				/* fake - seek1 */
#define  FNC_SEEK2	021				/* fake - seek2 */
#define  FNC_CHK1	022				/* fake - check1 */
#define  FNC_LA1	023				/* fake - arec1 */
#define  FNC_RCL1	024				/* fake - recal1 */

#define CW_V_DRV	0				/* drive */
#define CW_M_DRV	01
#define CW_GETDRV(x)	(((x) >> CW_V_DRV) & CW_M_DRV)

/* Disk address words */

#define DA_V_CYL	0				/* cylinder */
#define DA_M_CYL	0377
#define DA_GETCYL(x)	(((x) >> DA_V_CYL) & DA_M_CYL)
#define DA_V_HD		8				/* head */
#define DA_M_HD		037
#define DA_GETHD(x)	(((x) >> DA_V_HD) & DA_M_HD)
#define DA_V_SC		0				/* sector */
#define DA_M_SC		037
#define DA_GETSC(x)	(((x) >> DA_V_SC) & DA_M_SC)
#define DA_CKMASK	0777				/* check mask */

/* Status */

#define STA_DID		0000200				/* drive ID */
#define STA_NRDY	0000100				/* not ready */
#define STA_EOC		0000040				/* end of cylinder */
#define STA_AER		0000020				/* addr error */
#define STA_FLG		0000010				/* flagged */
#define STA_BSY		0000004				/* seeking */
#define STA_DTE		0000002				/* data error */
#define STA_ERR		0000001				/* any error */
#define STA_ALLERR	(STA_DID + STA_NRDY + STA_EOC + \
			 STA_FLG + STA_DTE)

extern int32 PC;
extern int32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];
int32 dqc_busy = 0;					/* cch busy */
int32 dqc_cnt = 0;					/* check count */
int32 dqc_eoc = 0;					/* end of cyl */
int32 dqc_sta[DQ_NUMDRV] = { 0 };			/* status regs */
int32 dqc_stime = 10;					/* seek time */
int32 dqc_ctime = 10;					/* command time */
int32 dqc_xtime = 5;					/* xfer time */
int32 dqc_rarc = 0, dqc_rarh = 0, dqc_rars = 0;		/* record addr */
int32 dqd_obuf = 0, dqd_ibuf = 0;			/* dch buffers */
int32 dqc_obuf = 0;					/* cch buffers */
int32 dq_ptr = 0;					/* buffer ptr */
uint16 dqxb[DQ_NUMWD];					/* sector buffer */

int32 dqdio (int32 inst, int32 IR, int32 dat);
int32 dqcio (int32 inst, int32 IR, int32 dat);
t_stat dqc_svc (UNIT *uptr);
t_stat dqc_reset (DEVICE *dptr);
t_stat dqc_vlock (UNIT *uptr, int32 val);
t_stat dqc_attach (UNIT *uptr, char *cptr);
t_stat dqc_detach (UNIT *uptr);
void dq_go (int32 fnc, int32 drv, int32 time, int32 attdev);

/* DQD data structures

   dqd_dev	DQD device descriptor
   dqd_unit	DQD unit list
   dqd_reg	DQD register list
*/

DIB dq_dib[] = {
	{ DQD, 1, 0, 0, 0, 0, &dqdio },
	{ DQC, 1, 0, 0, 0, 0, &dqcio }  };

#define dqd_dib dq_dib[0]
#define dqc_dib dq_dib[1]

UNIT dqd_unit = { UDATA (NULL, 0, 0) };

REG dqd_reg[] = {
	{ ORDATA (IBUF, dqd_ibuf, 16) },
	{ ORDATA (OBUF, dqd_obuf, 16) },
	{ FLDATA (CMD, dqd_dib.cmd, 0) },
	{ FLDATA (CTL, dqd_dib.ctl, 0) },
	{ FLDATA (FLG, dqd_dib.flg, 0) },
	{ FLDATA (FBF, dqd_dib.fbf, 0) },
	{ BRDATA (DBUF, dqxb, 8, 16, DQ_NUMWD) },
	{ DRDATA (BPTR, dq_ptr, DQ_N_NUMWD) },
	{ ORDATA (DEVNO, dqd_dib.devno, 6), REG_HRO },
	{ FLDATA (*DEVENB, dqd_dib.enb, 0), REG_HRO },
	{ NULL }  };

MTAB dqd_mod[] = {
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &dqd_dib },
	{ 0 }  };

DEVICE dqd_dev = {
	"DQD", &dqd_unit, dqd_reg, dqd_mod,
	1, 10, DQ_N_NUMWD, 1, 8, 16,
	NULL, NULL, &dqc_reset,
	NULL, NULL, NULL };

/* DQC data structures

   dqc_dev	DQC device descriptor
   dqc_unit	DQC unit list
   dqc_reg	DQC register list
   dqc_mod	DQC modifier list
*/

UNIT dqc_unit[] = {
	{ UDATA (&dqc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DQ_SIZE) },
	{ UDATA (&dqc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DQ_SIZE) }  };

REG dqc_reg[] = {
	{ ORDATA (OBUF, dqc_obuf, 16) },
	{ ORDATA (BUSY, dqc_busy, 2), REG_RO },
	{ ORDATA (RARC, dqc_rarc, 8) },
	{ ORDATA (RARH, dqc_rarh, 5) },
	{ ORDATA (RARS, dqc_rars, 5) },
	{ ORDATA (CNT, dqc_cnt, 5) },
	{ FLDATA (CMD, dqc_dib.cmd, 0) },
	{ FLDATA (CTL, dqc_dib.ctl, 0) },
	{ FLDATA (FLG, dqc_dib.flg, 0) },
	{ FLDATA (FBF, dqc_dib.fbf, 0) },
	{ FLDATA (EOC, dqc_eoc, 0) },
	{ DRDATA (CTIME, dqc_ctime, 24), PV_LEFT },
	{ DRDATA (STIME, dqc_stime, 24), PV_LEFT },
	{ DRDATA (XTIME, dqc_xtime, 24), REG_NZ + PV_LEFT },
	{ BRDATA (STA, dqc_sta, 8, 16, DQ_NUMDRV) },
	{ URDATA (UFLG, dqc_unit[0].flags, 8, UNIT_W_UF, UNIT_V_UF - 1,
		   DQ_NUMDRV, REG_HRO) },
	{ ORDATA (DEVNO, dqc_dib.devno, 6), REG_HRO },
	{ FLDATA (*DEVENB, dqc_dib.enb, 0), REG_HRO },
	{ NULL }  };

MTAB dqc_mod[] = {
/*	{ UNIT_WLK, 0, "write enabled", "ENABLED", &dqc_vlock }, */
/*	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", &dqc_vlock }, */
	{ MTAB_XTD | MTAB_VDV, 1, NULL, "ENABLED",
		&set_enb, NULL, &dqd_dib },
	{ MTAB_XTD | MTAB_VDV, 1, NULL, "DISABLED",
		&set_dis, NULL, &dqd_dib },
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &dqd_dib },
	{ 0 }  };

DEVICE dqc_dev = {
	"DQC", dqc_unit, dqc_reg, dqc_mod,
	DQ_NUMDRV, 8, 24, 1, 8, 16,
	NULL, NULL, &dqc_reset,
	NULL, &dqc_attach, &dqc_detach };

/* IOT routines */

int32 dqdio (int32 inst, int32 IR, int32 dat)
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
	dqd_obuf = dat;
	break;
case ioMIX:						/* merge */
	dat = dat | dqd_ibuf;
	break;
case ioLIX:						/* load */
	dat = dqd_ibuf;
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

int32 dqcio (int32 inst, int32 IR, int32 dat)
{
int32 devc, fnc, drv;

devc = IR & DEVMASK;					/* get device no */
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
	dqc_obuf = dat;
	break;
case ioLIX:						/* load */
	dat = 0;
case ioMIX:						/* merge */
	break;						/* no data */
case ioCTL:						/* control clear/set */
	if (IR & AB) { 					/* CLC? */
		clrCMD (devc);				/* clr cmd, ctl */
		clrCTL (devc);				/* cancel non-seek */
		if (dqc_busy) sim_cancel (&dqc_unit[dqc_busy - 1]);
		dqc_busy = 0;  }			/* clr busy */
	else if (!CTL (devc)) {				/* set and now clr? */
		setCMD (devc);				/* set cmd, ctl */
		setCTL (devc);
		drv = CW_GETDRV (dqc_obuf);		/* get fnc, drv */
		fnc = CW_GETFNC (dqc_obuf);		/* from cmd word */
		switch (fnc) {				/* case on fnc */
		case FNC_SEEK: case FNC_RCL:		/* seek, recal */
			dqc_sta[drv] = dqc_sta[drv] | STA_BSY;
			dq_go (fnc, drv, dqc_xtime, devc);
			break;
		case FNC_STA: case FNC_LA:		/* rd sta, load addr */
			dq_go (fnc, drv, dqc_xtime, 0);
			break;
		case FNC_CHK:				/* check */
			dq_go (fnc, drv, dqc_xtime, devc);
			break;
		case FNC_RD: case FNC_WD: case FNC_WA:	/* read, write, wr addr */
			dq_go (fnc, drv, dqc_ctime, devc);
			break;
		}					/* end case */
	}						/* end else */
	break;
default:
	break;  }
if (IR & HC) { clrFLG (devc); }				/* H/C option */
return dat;
}

/* Unit service

   Unit must be attached; detach cancels operation.

   Seek substates
	seek	-	transfer cylinder
	seek1	-	transfer head/surface
	seek2	-	done
   Load address
	la	-	transfer cylinder
	la1	-	transfer head/surface, finish operation
   Status check	-	transfer status, finish operation
   Check data
	chk	-	transfer sector count
	chk1	-	finish operation
   Read
   Write
*/

#define GETDA(x,y,z) \
	(((((x) * DQ_NUMSF) + (y)) * DQ_NUMSC) + (z)) * DQ_NUMWD

t_stat dqc_svc (UNIT *uptr)
{
int32 i, da, drv, devc, devd, err, st, maxsc;

err = 0;						/* assume no err */
drv = uptr - dqc_dev.units;				/* get drive no */
devc = dqc_dib.devno;					/* get cch devno */
devd = dqd_dib.devno;					/* get dch devno */
switch (uptr -> FNC) {					/* case function */

case FNC_SEEK:						/* seek, need cyl */
	if (CMD (devd)) {				/* dch active? */
		dqc_rarc = DA_GETCYL (dqd_obuf);	/* take cyl word */
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);				/* clr dch cmd */
		uptr -> FNC = FNC_SEEK1;  }		/* advance state */
	sim_activate (uptr, dqc_xtime);			/* no, wait more */
	return SCPE_OK;
case FNC_SEEK1:						/* seek, need hd/sec */
	if (CMD (devd)) {				/* dch active? */
		dqc_rarh = DA_GETHD (dqd_obuf);		/* get head */
		dqc_rars = DA_GETSC (dqd_obuf);		/* get sector */
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);				/* clr dch cmd */
		st = abs (dqc_rarc - uptr -> CYL) * dqc_stime; /* calc diff */
		if (st == 0) st = dqc_xtime;		/* min time */
		sim_activate (uptr, st);		/* schedule op */
		uptr -> CYL = dqc_rarc;			/* on cylinder */
		dqc_busy = 0;				/* ctrl is free */
		uptr -> FNC = FNC_SEEK2;  }		/* advance state */
	else sim_activate (uptr, dqc_xtime);		/* no, wait more */
	return SCPE_OK;
case FNC_SEEK2:						/* seek done */
	if (dqc_busy) sim_activate (uptr, dqc_xtime);	/* ctrl busy? wait */
	else {	dqc_sta[drv] = dqc_sta[drv] & ~STA_BSY;
		if (uptr -> CYL >= DQ_NUMCY) {		/* invalid cyl? */
			dqc_sta[drv] = dqc_sta[drv] | STA_AER;
			uptr -> CYL = 0;  }
		if (dqc_rars >= DQ_NUMSC)		/* invalid sec? */
			dqc_sta[drv] = dqc_sta[drv] | STA_AER;
		setFLG (devc);				/* set cch flg */
		clrCMD (devc);  }			/* clr cch cmd */
	return SCPE_OK;
case FNC_RCL:						/* recalibrate */
	if (dqc_busy) sim_activate (uptr, dqc_xtime);	/* ctrl busy? wait */
	else {	st = uptr -> CYL * dqc_stime;		/* calc diff */
		if (st == 0) st = dqc_xtime;		/* min time */
		sim_activate (uptr, st);		/* schedule op */
		uptr -> CYL = 0;			/* on cylinder */
		dqc_busy = 0;				/* ctrl is free */
		uptr -> FNC = FNC_RCL1;  }		/* advance state */
	return SCPE_OK;
case FNC_RCL1:						/* recal done */
	if (dqc_busy) sim_activate (uptr, dqc_xtime);	/* ctrl busy? wait */
	else {	dqc_sta[drv] = dqc_sta[drv] & ~STA_BSY;
		setFLG (devc);				/* set cch flg */
		clrCMD (devc);  }			/* clr cch cmd */
	return SCPE_OK;

case FNC_LA:						/* arec, need cyl */
	if (CMD (devd)) {				/* dch active? */
		dqc_rarc = DA_GETCYL (dqd_obuf);	/* take cyl word */
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);				/* clr dch cmd */
		uptr -> FNC = FNC_LA1;  }		/* advance state */
	sim_activate (uptr, dqc_xtime);			/* no, wait more */
	return SCPE_OK;
case FNC_LA1:						/* arec, need hd/sec */
	if (CMD (devd)) {				/* dch active? */
		dqc_rarh = DA_GETHD (dqd_obuf);		/* get head */
		dqc_rars = DA_GETSC (dqd_obuf);		/* get sector */
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);  }			/* clr dch cmd */
	else {	sim_activate (uptr, dqc_xtime);		/* no, wait more */
		return SCPE_OK;  }
	break;						/* done */

case FNC_STA:						/* read status */
	if (CMD (devd)) {				/* dch active? */
		dqd_ibuf = dqc_sta[drv] | ((dqc_sta[drv] & STA_ALLERR)? STA_ERR: 0);
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);				/* clr dch cmd */
		dqc_sta[drv] = dqc_sta[drv] & 		/* clr sta flags */
			~(STA_DTE | STA_FLG | STA_AER | STA_EOC);
		dqc_busy = 0;  }			/* ctlr is free */
	else sim_activate (uptr, dqc_xtime);		/* wait more */
	return SCPE_OK;

case FNC_CHK:						/* check, need cnt */
	if (CMD (devd)) {				/* dch active? */
		dqc_cnt = dqd_obuf & DA_CKMASK;		/* get count */
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);				/* clr dch cmd */
		sim_activate (uptr, dqc_ctime);		/* schedule op */
		uptr -> FNC = FNC_CHK1;  }		/* advance state */
	else sim_activate (uptr, dqc_xtime);		/* wait more */
	return SCPE_OK;
case FNC_CHK1:
	if ((uptr -> CYL != dqc_rarc) || (dqc_rars >= DQ_NUMSC))
		dqc_sta[drv] = dqc_sta[drv] | STA_AER;
	else {	maxsc = ((2 - (dqc_rarh & 1)) * DQ_NUMSC) - dqc_rars;
		if (dqc_cnt > maxsc) {			/* too many sec? */
			dqc_sta[drv] = dqc_sta[drv] | STA_EOC;
			dqc_rarh = dqc_rarh & ~1;	/* rar = 0/2, 0 */
			dqc_rars = 0;  }
		else {	i = dqc_rars + dqc_cnt;		/* final sector */
			dqc_rars = i % DQ_NUMSC;	/* reposition */
			dqc_rarh = dqc_rarh ^ ((i / DQ_NUMSC) & 1);  }  }
	break;						/* done */

case FNC_RD:						/* read */
	if (!CMD (devd)) break;				/* dch clr? done */
	if (FLG (devd)) dqc_sta[drv] = dqc_sta[drv] | STA_DTE;
	if (dq_ptr == 0) {				/* new sector? */
		if ((uptr -> CYL != dqc_rarc) || (dqc_rars >= DQ_NUMSC)) {
			dqc_sta[drv] = dqc_sta[drv] | STA_AER;
			break;  }
		if (dqc_eoc) {				/* end of cyl? */
			dqc_sta[drv] = dqc_sta[drv] | STA_EOC;
			break;  }
		da = GETDA (dqc_rarc, dqc_rarh, dqc_rars);	/* get addr */
		dqc_rars = dqc_rars + 1;		/* incr address */
		if (dqc_rars >= DQ_NUMSC) {		/* end of trk? */
			dqc_rars = 0;			/* wrap to */
			dqc_rarh = dqc_rarh ^ 1;	/* next cyl */
			dqc_eoc = ((dqc_rarh & 1) == 0);  }	/* calc eoc */
		if (err = fseek (uptr -> fileref, da * sizeof (int16),
			SEEK_SET)) break;
		fxread (dqxb, sizeof (int16), DQ_NUMWD, uptr -> fileref);
		if (err = ferror (uptr -> fileref)) break;  }
	dqd_ibuf = dqxb[dq_ptr++];			/* get word */
	if (dq_ptr >= DQ_NUMWD) dq_ptr = 0;		/* wrap if last */
	setFLG (devd);					/* set dch flg */
	clrCMD (devd);					/* clr dch cmd */
	sim_activate (uptr, dqc_xtime);			/* sched next word */
	return SCPE_OK;

case FNC_WA:						/* write address */
case FNC_WD:						/* write */
	if (dqc_eoc) {					/* end of cyl? */
		dqc_sta[drv] = dqc_sta[drv] | STA_EOC;	/* set status */
		break;  }				/* done */
	if (FLG (devd)) dqc_sta[drv] = dqc_sta[drv] | STA_DTE;
	dqxb[dq_ptr++] = dqd_obuf;			/* store word */
	if (!CMD (devd)) {				/* dch clr? done */
		for ( ; dq_ptr < DQ_NUMWD; dq_ptr++) dqxb[dq_ptr] = 0;  }
	if (dq_ptr >= DQ_NUMWD) {			/* buffer full? */
		if ((uptr -> CYL != dqc_rarc) || (dqc_rars >= DQ_NUMSC)) {
			dqc_sta[drv] = dqc_sta[drv] | STA_AER;
			break;  }
		da = GETDA (dqc_rarc, dqc_rarh, dqc_rars);	/* get addr */
		dqc_rars = dqc_rars + 1;		/* incr address */
		if (dqc_rars >= DQ_NUMSC) {		/* end of trk? */
			dqc_rars = 0;			/* wrap to */
			dqc_rarh = dqc_rarh ^ 1;	/* next cyl */
			dqc_eoc = ((dqc_rarh & 1) == 0);  }	/* calc eoc */
		if (err = fseek (uptr -> fileref, da * sizeof (int16),
			SEEK_SET)) return TRUE;
		fxwrite (dqxb, sizeof (int16), DQ_NUMWD, uptr -> fileref);
		if (err = ferror (uptr -> fileref)) break;
		dq_ptr = 0;  }
	if (CMD (devd)) {				/* dch active? */
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);				/* clr dch cmd */
		sim_activate (uptr, dqc_xtime);		/* sched next word */
		return SCPE_OK;  }
	break;  }					/* end case fnc */

setFLG (devc);						/* set cch flg */
clrCMD (devc);						/* clr cch cmd */
dqc_busy = 0;						/* ctlr is free */
if (err != 0) {						/* error? */
	perror ("DQ I/O error");
	clearerr (uptr -> fileref);
	return SCPE_IOERR;  }
return SCPE_OK;
}

/* Start disk operation */

void dq_go (int32 fnc, int32 drv, int32 time, int32 dev)
{
if (dev && ((dqc_unit[drv].flags & UNIT_ATT) == 0)) {	/* attach check? */
	dqc_sta[drv] = STA_NRDY;			/* not attached */
	setFLG (dev);					/* set cch flag */
	clrCMD (dev);  }				/* clr cch cmd */
else {	dqc_busy = drv + 1;				/* set busy */
	dq_ptr = 0;					/* init buf ptr */
	dqc_eoc = 0;					/* clear end cyl */
	dqc_unit[drv].FNC = fnc;			/* save function */
	sim_activate (&dqc_unit[drv], time);  }		/* activate unit */
return;
}

/* Reset routine */

t_stat dqc_reset (DEVICE *dptr)
{
int32 i;

dqd_ibuf = dqd_obuf = 0;				/* clear buffers */
dqc_busy = dqc_obuf = 0;
dqc_eoc = 0;
dq_ptr = 0;
dqc_rarc = dqc_rarh = dqc_rars = 0;			/* clear rar */
dqc_dib.cmd = dqd_dib.cmd = 0;				/* clear cmd */
dqc_dib.ctl = dqd_dib.ctl = 0;				/* clear ctl */
dqc_dib.fbf = dqd_dib.fbf = 1;				/* set fbf */
dqc_dib.flg = dqd_dib.flg = 1;				/* set flg */
for (i = 0; i < DQ_NUMDRV; i++) {			/* loop thru drives */
	sim_cancel (&dqc_unit[i]);			/* cancel activity */
	dqc_unit[i].FNC = 0;				/* clear function */
	dqc_unit[i].CYL = 0;
	dqc_sta[i] = (dqc_unit[i].flags & UNIT_ATT)? 0: STA_NRDY;  }
return SCPE_OK;
}

/* Attach routine */

t_stat dqc_attach (UNIT *uptr, char *cptr)
{
int32 drv;
t_stat r;

drv = uptr - dqc_dev.units;				/* get drive no */
r = attach_unit (uptr, cptr);				/* attach unit */
if (r != SCPE_OK) return r;
dqc_sta[drv] = dqc_sta[drv] & ~STA_NRDY;		/* update status */
return r;
}

/* Detach routine */

t_stat dqc_detach (UNIT* uptr)
{
int32 drv;

drv = uptr - dqc_dev.units;				/* get drive no */
dqc_sta[drv] = dqc_sta[drv] | STA_NRDY;			/* update status */
if (drv == (dqc_busy + 1)) dqc_busy = 0;		/* update busy */
sim_cancel (uptr);					/* cancel op */
return detach_unit (uptr);				/* detach unit */
}

/* Write lock/enable routine */

t_stat dqc_vlock (UNIT *uptr, int32 val)
{
if (uptr -> flags & UNIT_ATT) return SCPE_ARG;
return SCPE_OK;
}
