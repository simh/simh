/* hp2100_dp.c: HP 2100 disk pack simulator

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

   dp		12557A cartridge disk system

   07-Sep-01	RMS	Moved function prototypes
   29-Nov-00	RMS	Made variable names unique
   21-Nov-00	RMS	Fixed flag, buffer power up state
*/

#include "hp2100_defs.h"

#define UNIT_V_WLK	(UNIT_V_UF + 0)			/* write locked */
#define UNIT_WLK	(1 << UNIT_V_WLK)
#define UNIT_W_UF	2				/* # flags */
#define FNC		u3				/* saved function */
#define CYL		u4				/* cylinder */

#define DP_W_NUMWD	7
#define DP_NUMWD	(1 << DP_W_NUMWD)		/* words/sector */
#define DP_NUMSC	12				/* sectors/track */
#define DP_NUMTR	203				/* tracks/surface */
#define DP_NUMSF	4				/* surfaces/track */
#define DP_SIZE		(DP_NUMSF * DP_NUMTR * DP_NUMSC * DP_NUMWD)
#define DP_NUMDRV	4				/* # drives */

/* Command word */

#define CW_V_FNC	12				/* function */
#define CW_M_FNC	017
#define CW_GETFNC(x)	(((x) >> CW_V_FNC) & CW_M_FNC)
#define  FNC_STA	000				/* status check */
#define  FNC_WD		001				/* write */
#define  FNC_RD		002				/* read */
#define  FNC_SEEK	003				/* seek */
#define  FNC_REF	005				/* refine */
#define  FNC_CHK	006				/* check */
#define  FNC_INIT	011				/* init */
#define  FNC_AR		013				/* address */
#define	 FNC_SEEK1	020				/* fake - seek1 */
#define  FNC_SEEK2	021				/* fake - seek2 */
#define  FNC_CHK1	022				/* fake - check1 */
#define  FNC_AR1	023				/* fake - arec1 */
#define CW_V_DRV	0				/* drive */
#define CW_M_DRV	03
#define CW_GETDRV(x)	(((x) >> CW_V_DRV) & CW_M_DRV)

/* Disk address words */

#define DA_V_CYL	0				/* cylinder */
#define DA_M_CYL	0377
#define DA_GETCYL(x)	(((x) >> DA_V_CYL) & DA_M_CYL)
#define DA_V_HD		8				/* head */
#define DA_M_HD		03
#define DA_GETHD(x)	(((x) >> DA_V_HD) & DA_M_HD)
#define DA_V_SC		0				/* sector */
#define DA_M_SC		017
#define DA_GETSC(x)	(((x) >> DA_V_SC) & DA_M_SC)

/* Status */

#define STA_ATN		0100000				/* attention */
#define STA_1ST		0040000				/* first seek */
#define STA_OVR		0020000				/* overrun */
#define STA_RWU		0010000				/* rw unsafe */
#define STA_ACU		0004000				/* access unsafe */
#define STA_HUNT	0002000				/* hunting */
#define STA_SKI		0001000				/* incomplete */
#define STA_SKE		0000400				/* seek error */
/*			0000200				/* unused */
#define STA_NRDY	0000100				/* not ready */
#define STA_EOC		0000040				/* end of cylinder */
#define STA_AER		0000020				/* addr error */
#define STA_FLG		0000010				/* flagged */
#define STA_BSY		0000004				/* seeking */
#define STA_DTE		0000002				/* data error */
#define STA_ERR		0000001				/* any error */
#define STA_ALLERR	(STA_ATN + STA_1ST + STA_OVR + STA_RWU + STA_ACU + \
			 STA_HUNT + STA_SKI + STA_SKE + STA_NRDY + STA_EOC + \
			 STA_FLG + STA_DTE)

extern uint16 M[];
extern struct hpdev infotab[];
extern int32 PC;
extern int32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];
int32 dpc_busy = 0;					/* cch busy */
int32 dpc_cnt = 0;					/* check count */
int32 dpc_eoc = 0;					/* end of cyl */
int32 dpc_sta[DP_NUMDRV] = { 0 };			/* status regs */
int32 dpc_stime = 10;					/* seek time */
int32 dpc_ctime = 10;					/* command time */
int32 dpc_xtime = 5;					/* xfer time */
int32 dpc_rarc = 0, dpc_rarh = 0, dpc_rars = 0;		/* record addr */
int32 dpd_obuf = 0, dpd_ibuf = 0;			/* dch buffers */
int32 dpc_obuf = 0;					/* cch buffers */
int32 dp_ptr = 0;					/* buffer ptr */
uint16 dp_buf[DP_NUMWD];				/* sector buffer */

t_stat dpc_svc (UNIT *uptr);
t_stat dpc_reset (DEVICE *dptr);
t_stat dpc_vlock (UNIT *uptr, int32 val);
t_stat dpc_attach (UNIT *uptr, char *cptr);
t_stat dpc_detach (UNIT *uptr);
t_stat dpd_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat dpd_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
void dp_go (int32 fnc, int32 drv, int32 time, int32 attdev);

/* DPD data structures

   dpd_dev	DPD device descriptor
   dpd_unit	DPD unit list
   dpd_reg	DPD register list
*/

UNIT dpd_unit = { UDATA (NULL, UNIT_FIX, DP_NUMWD) };

REG dpd_reg[] = {
	{ ORDATA (IBUF, dpd_ibuf, 16) },
	{ ORDATA (OBUF, dpd_obuf, 16) },
	{ FLDATA (CMD, infotab[inDPD].cmd, 0) },
	{ FLDATA (CTL, infotab[inDPD].ctl, 0) },
	{ FLDATA (FLG, infotab[inDPD].flg, 0) },
	{ FLDATA (FBF, infotab[inDPD].fbf, 0) },
	{ DRDATA (BPTR, dp_ptr, DP_W_NUMWD) },
	{ ORDATA (DEVNO, infotab[inDPD].devno, 6), REG_RO },
	{ NULL }  };

DEVICE dpd_dev = {
	"DPD", &dpd_unit, dpd_reg, NULL,
	1, 10, DP_W_NUMWD, 1, 8, 16,
	&dpd_ex, &dpd_dep, &dpc_reset,
	NULL, NULL, NULL };

/* DPC data structures

   dpc_dev	DPC device descriptor
   dpc_unit	DPC unit list
   dpc_reg	DPC register list
   dpc_mod	DPC modifier list
*/

UNIT dpc_unit[] = {
	{ UDATA (&dpc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DP_SIZE) },
	{ UDATA (&dpc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DP_SIZE) },
	{ UDATA (&dpc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DP_SIZE) },
	{ UDATA (&dpc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DP_SIZE) }  };

REG dpc_reg[] = {
	{ ORDATA (OBUF, dpc_obuf, 16) },
	{ ORDATA (BUSY, dpc_busy, 3), REG_RO },
	{ ORDATA (RARC, dpc_rarc, 8) },
	{ ORDATA (RARH, dpc_rarh, 2) },
	{ ORDATA (RARS, dpc_rars, 4) },
	{ ORDATA (CNT, dpc_cnt, 5) },
	{ FLDATA (CMD, infotab[inDPC].cmd, 0) },
	{ FLDATA (CTL, infotab[inDPC].ctl, 0) },
	{ FLDATA (FLG, infotab[inDPC].flg, 0) },
	{ FLDATA (FBF, infotab[inDPC].fbf, 0) },
	{ FLDATA (EOC, dpc_eoc, 0) },
	{ DRDATA (CTIME, dpc_ctime, 24), PV_LEFT },
	{ DRDATA (STIME, dpc_stime, 24), PV_LEFT },
	{ DRDATA (XTIME, dpc_xtime, 24), REG_NZ + PV_LEFT },
	{ ORDATA (STA0, dpc_sta[0], 16) },
	{ ORDATA (STA1, dpc_sta[1], 16) },
	{ ORDATA (STA2, dpc_sta[2], 16) },
	{ ORDATA (STA3, dpc_sta[3], 16) },
	{ GRDATA (UFLG0, dpc_unit[0].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (UFLG1, dpc_unit[1].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (UFLG2, dpc_unit[2].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (UFLG3, dpc_unit[3].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ ORDATA (DEVNO, infotab[inDPC].devno, 6), REG_RO },
	{ NULL }  };

MTAB dpc_mod[] = {
/*	{ UNIT_WLK, 0, "write enabled", "ENABLED", &dpc_vlock }, */
/*	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", &dpc_vlock }, */
	{ UNIT_DEVNO, inDPD, NULL, "DEVNO", &hp_setdev2 },
	{ 0 }  };

DEVICE dpc_dev = {
	"DPC", dpc_unit, dpc_reg, dpc_mod,
	DP_NUMDRV, 8, 24, 1, 8, 16,
	NULL, NULL, &dpc_reset,
	NULL, &dpc_attach, &dpc_detach };

/* IOT routines */

int32 dpdio (int32 inst, int32 IR, int32 dat)
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
	dpd_obuf = dat;
	break;
case ioMIX:						/* merge */
	dat = dat | dpd_ibuf;
	break;
case ioLIX:						/* load */
	dat = dpd_ibuf;
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

int32 dpcio (int32 inst, int32 IR, int32 dat)
{
int32 i, devc, fnc, drv;

devc = IR & DEVMASK;					/* get device no */
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
	dpc_obuf = dat;
	break;
case ioLIX:						/* load */
	dat = 0;
case ioMIX:						/* merge */
	for (i = 0; i < DP_NUMDRV; i++)
		if (dpc_sta[i] & STA_ATN) dat = dat | (1 << i);
	break;
case ioCTL:						/* control clear/set */
	if (IR & AB) { 					/* CLC? */
		clrCMD (devc);				/* clr cmd, ctl */
		clrCTL (devc);				/* cancel non-seek */
		if (dpc_busy) sim_cancel (&dpc_unit[dpc_busy - 1]);
		dpc_busy = 0;  }			/* clr busy */
	else if (!CTL (devc)) {				/* set and now clr? */
		setCMD (devc);				/* set cmd, ctl */
		setCTL (devc);
		drv = CW_GETDRV (dpc_obuf);		/* get fnc, drv */
		fnc = CW_GETFNC (dpc_obuf);		/* from cmd word */
		switch (fnc) {				/* case on fnc */
		case FNC_SEEK:				/* seek */
			dpc_sta[drv] = (dpc_sta[drv] | STA_BSY) &
				~(STA_SKE | STA_SKI | STA_HUNT | STA_1ST);
			dp_go (fnc, drv, dpc_xtime, devc);
			break;
		case FNC_STA: case FNC_AR:		/* rd sta, addr rec */
			dp_go (fnc, drv, dpc_xtime, 0);
			break;
		case FNC_CHK:				/* check */
			dp_go (fnc, drv, dpc_xtime, devc);
			break;
		case FNC_REF: case FNC_RD: case FNC_WD:	/* ref, read, write */
			dp_go (fnc, drv, dpc_ctime, devc);
			break;
		case FNC_INIT:				/* init */
			dpc_sta[drv] = dpc_sta[drv] | STA_FLG;
			setFLG (devc);			/* set cch flg */
			clrCMD (devc);			/* clr cch cmd */
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
   Address record
	ar	-	transfer cylinder
	ar1	-	transfer head/surface, finish operation
   Status check	-	transfer status, finish operation
   Refine sector -	erase sector, finish operation
   Check data
	chk	-	transfer sector count
	chk1	-	finish operation
   Read
   Write
*/

#define GETDA(x,y,z) \
	(((((x) * DP_NUMSF) + (y)) * DP_NUMSC) + (z)) * DP_NUMWD

t_stat dpc_svc (UNIT *uptr)
{
int32 i, da, drv, devc, devd, err, st, maxsc;

err = 0;						/* assume no err */
drv = uptr - dpc_dev.units;				/* get drive no */
devc = infotab[inDPC].devno;				/* get cch devno */
devd = infotab[inDPD].devno;				/* get dch devno */
switch (uptr -> FNC) {					/* case function */

case FNC_SEEK:						/* seek, need cyl */
	if (CMD (devd)) {				/* dch active? */
		dpc_rarc = DA_GETCYL (dpd_obuf);	/* take cyl word */
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);				/* clr dch cmd */
		uptr -> FNC = FNC_SEEK1;  }		/* advance state */
	sim_activate (uptr, dpc_xtime);			/* no, wait more */
	return SCPE_OK;
case FNC_SEEK1:						/* seek, need hd/sec */
	if (CMD (devd)) {				/* dch active? */
		dpc_rarh = DA_GETHD (dpd_obuf);		/* get head */
		dpc_rars = DA_GETSC (dpd_obuf);		/* get sector */
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);				/* clr dch cmd */
		st = abs (dpc_rarc - uptr -> CYL) * dpc_stime; /* calc diff */
		if (st == 0) st = dpc_xtime;		/* min time */
		sim_activate (uptr, st);		/* schedule op */
		uptr -> CYL = dpc_rarc;			/* on cylinder */
		dpc_busy = 0;				/* ctrl is free */
		uptr -> FNC = FNC_SEEK2;  }		/* advance state */
	else sim_activate (uptr, dpc_xtime);		/* no, wait more */
	return SCPE_OK;
case FNC_SEEK2:						/* seek done */
	if (dpc_busy) sim_activate (uptr, dpc_xtime);	/* ctrl busy? wait */
	else {	dpc_sta[drv] = (dpc_sta[drv] | STA_ATN) & ~STA_BSY;
		if (uptr -> CYL >= DP_NUMTR) {		/* error? */
			dpc_sta[drv] = dpc_sta[drv] | STA_SKE;
			uptr -> CYL = 0;  }
		setFLG (devc);				/* set cch flg */
		clrCMD (devc);  }			/* clr cch cmd */
	return SCPE_OK;

case FNC_AR:						/* arec, need cyl */
	if (CMD (devd)) {				/* dch active? */
		dpc_rarc = DA_GETCYL (dpd_obuf);	/* take cyl word */
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);				/* clr dch cmd */
		uptr -> FNC = FNC_AR1;  }		/* advance state */
	sim_activate (uptr, dpc_xtime);			/* no, wait more */
	return SCPE_OK;
case FNC_AR1:						/* arec, need hd/sec */
	if (CMD (devd)) {				/* dch active? */
		dpc_rarh = DA_GETHD (dpd_obuf);		/* get head */
		dpc_rars = DA_GETSC (dpd_obuf);		/* get sector */
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);  }			/* clr dch cmd */
	else {	sim_activate (uptr, dpc_xtime);		/* no, wait more */
		return SCPE_OK;  }
	break;						/* done */

case FNC_STA:						/* read status */
	if (CMD (devd)) {				/* dch active? */
		dpd_ibuf = dpc_sta[drv] | ((dpc_sta[drv] & STA_ALLERR)? STA_ERR: 0);
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);				/* clr dch cmd */
		dpc_sta[drv] = dpc_sta[drv] & 	/* clr sta flags */
			~(STA_ATN | STA_DTE | STA_FLG | STA_AER | STA_EOC);
		dpc_busy = 0;  }			/* ctlr is free */
	else sim_activate (uptr, dpc_xtime);		/* wait more */
	return SCPE_OK;

case FNC_REF:						/* refine sector */
	if ((uptr -> CYL != dpc_rarc) || (dpc_rars >= DP_NUMSC))
		dpc_sta[drv] = dpc_sta[drv] | STA_AER;
	else {	for (i = 0; i < DP_NUMWD; i++) dp_buf[i] = 0;
		da = GETDA (dpc_rarc, dpc_rarh, dpc_rars);	/* get addr */
		dpc_rars = dpc_rars + 1;		/* incr sector */
		if (dpc_rars >= DP_NUMSC) {		/* end of trk? */
			dpc_rars = 0;			/* wrap to */
			dpc_rarh = dpc_rarh ^ 1;  }	/* next surf */
		if (err = fseek (uptr -> fileref, da * sizeof (int16),
			SEEK_SET)) break;
		fxwrite (dp_buf, sizeof (int16), DP_NUMWD, uptr -> fileref);
		err = ferror (uptr -> fileref);  }
	break;

case FNC_CHK:						/* check, need cnt */
	if (CMD (devd)) {				/* dch active? */
		dpc_cnt = dpd_obuf & 037;		/* get count */
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);				/* clr dch cmd */
		sim_activate (uptr, dpc_ctime);		/* schedule op */
		uptr -> FNC = FNC_CHK1;  }		/* advance state */
	else sim_activate (uptr, dpc_xtime);		/* wait more */
	return SCPE_OK;
case FNC_CHK1:
	if ((uptr -> CYL != dpc_rarc) || (dpc_rars >= DP_NUMSC))
		dpc_sta[drv] = dpc_sta[drv] | STA_AER;
	else {	maxsc = ((2 - (dpc_rarh & 1)) * DP_NUMSC) - dpc_rars;
		if (dpc_cnt > maxsc) {			/* too many sec? */
			dpc_sta[drv] = dpc_sta[drv] | STA_EOC;
			dpc_rarh = dpc_rarh & ~1;	/* rar = 0/2, 0 */
			dpc_rars = 0;  }
		else {	i = dpc_rars + dpc_cnt;		/* final sector */
			dpc_rars = i % DP_NUMSC;	/* reposition */
			dpc_rarh = dpc_rarh ^ ((i / DP_NUMSC) & 1);  }  }
	break;						/* done */

case FNC_RD:						/* read */
	if (!CMD (devd)) break;				/* dch clr? done */
	if (FLG (devd)) dpc_sta[drv] = dpc_sta[drv] | STA_OVR;
	if (dp_ptr == 0) {				/* new sector? */
		if ((uptr -> CYL != dpc_rarc) || (dpc_rars >= DP_NUMSC)) {
			dpc_sta[drv] = dpc_sta[drv] | STA_AER;
			break;  }
		if (dpc_eoc) {				/* end of cyl? */
			dpc_sta[drv] = dpc_sta[drv] | STA_EOC;
			break;  }
		da = GETDA (dpc_rarc, dpc_rarh, dpc_rars);	/* get addr */
		dpc_rars = dpc_rars + 1;		/* incr address */
		if (dpc_rars >= DP_NUMSC) {		/* end of trk? */
			dpc_rars = 0;			/* wrap to */
			dpc_rarh = dpc_rarh ^ 1;	/* next cyl */
			dpc_eoc = ((dpc_rarh & 1) == 0);  }	/* calc eoc */
		if (err = fseek (uptr -> fileref, da * sizeof (int16),
			SEEK_SET)) break;
		fxread (dp_buf, sizeof (int16), DP_NUMWD, uptr -> fileref);
		if (err = ferror (uptr -> fileref)) break;  }
	dpd_ibuf = dp_buf[dp_ptr++];			/* get word */
	if (dp_ptr >= DP_NUMWD) dp_ptr = 0;		/* wrap if last */
	setFLG (devd);					/* set dch flg */
	clrCMD (devd);					/* clr dch cmd */
	sim_activate (uptr, dpc_xtime);			/* sched next word */
	return SCPE_OK;

case FNC_WD:						/* write */
	if (dpc_eoc) {					/* end of cyl? */
		dpc_sta[drv] = dpc_sta[drv] | STA_EOC;	/* set status */
		break;  }				/* done */
	if (FLG (devd)) dpc_sta[drv] = dpc_sta[drv] | STA_OVR;
	dp_buf[dp_ptr++] = dpd_obuf;			/* store word */
	if (!CMD (devd)) {				/* dch clr? done */
		for ( ; dp_ptr < DP_NUMWD; dp_ptr++) dp_buf[dp_ptr] = 0;  }
	if (dp_ptr >= DP_NUMWD) {			/* buffer full? */
		if ((uptr -> CYL != dpc_rarc) || (dpc_rars >= DP_NUMSC)) {
			dpc_sta[drv] = dpc_sta[drv] | STA_AER;
			break;  }
		da = GETDA (dpc_rarc, dpc_rarh, dpc_rars);	/* get addr */
		dpc_rars = dpc_rars + 1;		/* incr address */
		if (dpc_rars >= DP_NUMSC) {		/* end of trk? */
			dpc_rars = 0;			/* wrap to */
			dpc_rarh = dpc_rarh ^ 1;	/* next cyl */
			dpc_eoc = ((dpc_rarh & 1) == 0);  }	/* calc eoc */
		if (err = fseek (uptr -> fileref, da * sizeof (int16),
			SEEK_SET)) return TRUE;
		fwrite (dp_buf, sizeof (int16), DP_NUMWD, uptr -> fileref);
		if (err = ferror (uptr -> fileref)) break;
		dp_ptr = 0;  }
	if (CMD (devd)) {				/* dch active? */
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);				/* clr dch cmd */
		sim_activate (uptr, dpc_xtime);		/* sched next word */
		return SCPE_OK;  }
	break;  }					/* end case fnc */

dpc_sta[drv] = dpc_sta[drv] | STA_ATN;			/* request attn */
setFLG (devc);						/* set cch flg */
clrCMD (devc);						/* clr cch cmd */
dpc_busy = 0;						/* ctlr is free */
if (err != 0) {						/* error? */
	perror ("DP I/O error");
	clearerr (uptr -> fileref);
	return SCPE_IOERR;  }
return SCPE_OK;
}

/* Start disk operation */

void dp_go (int32 fnc, int32 drv, int32 time, int32 dev)
{
if (dev && ((dpc_unit[drv].flags & UNIT_ATT) == 0)) {	/* attach check? */
	dpc_sta[drv] = STA_NRDY;			/* not attached */
	setFLG (dev);					/* set cch flag */
	clrCMD (dev);  }				/* clr cch cmd */
else {	dpc_busy = drv + 1;				/* set busy */
	dp_ptr = 0;					/* init buf ptr */
	dpc_eoc = 0;					/* clear end cyl */
	dpc_unit[drv].FNC = fnc;			/* save function */
	sim_activate (&dpc_unit[drv], time);  }		/* activate unit */
return;
}

/* Reset routine */

t_stat dpc_reset (DEVICE *dptr)
{
int32 i;

dpd_ibuf = dpd_obuf = 0;				/* clear buffers */
dpc_busy = dpc_obuf = 0;
dpc_eoc = 0;
dp_ptr = 0;
dpc_rarc = dpc_rarh = dpc_rars = 0;			/* clear rar */
infotab[inDPC].cmd = infotab[inDPD].cmd = 0;		/* clear cmd */
infotab[inDPC].ctl = infotab[inDPD].ctl = 0;		/* clear ctl */
infotab[inDPC].fbf = infotab[inDPD].fbf = 1;		/* set fbf */
infotab[inDPC].flg = infotab[inDPD].flg = 1;		/* set flg */
for (i = 0; i < DP_NUMDRV; i++) {			/* loop thru drives */
	sim_cancel (&dpc_unit[i]);			/* cancel activity */
	dpc_unit[i].FNC = 0;				/* clear function */
	dpc_unit[i].CYL = 0;
	dpc_sta[i] = (dpc_sta[i] & STA_1ST) |
		((dpc_unit[i].flags & UNIT_ATT)? 0: STA_NRDY);  }
return SCPE_OK;
}

/* Attach routine */

t_stat dpc_attach (UNIT *uptr, char *cptr)
{
int32 drv;
t_stat r;

drv = uptr - dpc_dev.units;				/* get drive no */
r = attach_unit (uptr, cptr);				/* attach unit */
if (r != SCPE_OK) return r;
dpc_sta[drv] = (dpc_sta[drv] | STA_1ST) & ~STA_NRDY;	/* update status */
return r;
}

/* Detach routine */

t_stat dpc_detach (UNIT* uptr)
{
int32 drv;

drv = uptr - dpc_dev.units;				/* get drive no */
dpc_sta[drv] = (dpc_sta[drv] | STA_NRDY) & ~STA_1ST;	/* update status */
if (drv == (dpc_busy + 1)) dpc_busy = 0;		/* update busy */
sim_cancel (uptr);					/* cancel op */
return detach_unit (uptr);				/* detach unit */
}

/* Write lock/enable routine */

t_stat dpc_vlock (UNIT *uptr, int32 val)
{
if (uptr -> flags & UNIT_ATT) return SCPE_ARG;
return SCPE_OK;
}

/* Buffer examine */

t_stat dpd_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= DP_NUMWD) return SCPE_NXM;
if (vptr != NULL) *vptr = dp_buf[addr] & DMASK;
return SCPE_OK;
}

/* Buffer deposit */

t_stat dpd_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= DP_NUMWD) return SCPE_NXM;
dp_buf[addr] = val & DMASK;
return SCPE_OK;
}
