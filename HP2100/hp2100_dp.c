/* hp2100_dp.c: HP 2100 12557A/13210A disk pack simulator

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

   dp		12557A/13210A disk pack subsystem

   15-Jan-02	RMS	Fixed INIT handling (found by Bill McDermith)
   10-Jan-02	RMS	Fixed f(x)write call (found by Bill McDermith)
   03-Dec-01	RMS	Changed DEVNO to use extended SET/SHOW
   24-Nov-01	RMS	Changed STA to be an array
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

#define DP_N_NUMWD	7
#define DP_NUMWD	(1 << DP_N_NUMWD)		/* words/sector */
#define DP_NUMSC2	12				/* sectors/srf 12557 */
#define DP_NUMSC3	24				/* sectors/srf 13210 */
#define DP_NUMSC	(dp_ctype? DP_NUMSC3: DP_NUMSC2)
#define DP_NUMSF	4				/* surfaces/cylinder */
#define DP_NUMCY	203				/* cylinders/disk */
#define DP_SIZE2	(DP_NUMSF * DP_NUMCY * DP_NUMSC2 * DP_NUMWD)
#define DP_SIZE3	(DP_NUMSF * DP_NUMCY * DP_NUMSC3 * DP_NUMWD)
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
#define DA_M_SC2	017
#define DA_M_SC3	037
#define DA_M_SC		(dp_ctype? DA_M_SC3: DA_M_SC2)
#define DA_GETSC(x)	(((x) >> DA_V_SC) & DA_M_SC)
#define DA_CKMASK2	037				/* check mask */
#define DA_CKMASK3	077
#define DA_CKMASK	(dp_ctype? DA_CKMASK3: DA_CKMASK2)

/* Status */

#define STA_ATN		0100000				/* attention */
#define STA_1ST		0040000				/* first seek */
#define STA_OVR		0020000				/* overrun */
#define STA_RWU		0010000				/* rw unsafe */
#define STA_ACU		0004000				/* access unsafe */
#define STA_HUNT	0002000				/* hunting NI */
#define STA_SKI		0001000				/* incomplete NI */
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
#define STA_MBZ13	(STA_ATN + STA_RWU + STA_SKI)	/* zero in 13210 */

extern int32 PC;
extern int32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];
int32 dp_ctype = 0;					/* ctrl type */
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
uint16 dpxb[DP_NUMWD];					/* sector buffer */

int32 dpdio (int32 inst, int32 IR, int32 dat);
int32 dpcio (int32 inst, int32 IR, int32 dat);
t_stat dpc_svc (UNIT *uptr);
t_stat dpc_reset (DEVICE *dptr);
t_stat dpc_vlock (UNIT *uptr, int32 val);
t_stat dpc_attach (UNIT *uptr, char *cptr);
t_stat dpc_detach (UNIT *uptr);
void dp_go (int32 fnc, int32 drv, int32 time, int32 attdev);
t_stat dp_settype (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat dp_showtype (FILE *st, UNIT *uptr, int32 val, void *desc);

/* DPD data structures

   dpd_dev	DPD device descriptor
   dpd_unit	DPD unit list
   dpd_reg	DPD register list
*/

DIB dp_dib[] = {
	{ DPD, 1, 0, 0, 0, 0, &dpdio },
	{ DPC, 1, 0, 0, 0, 0, &dpcio }  };

#define dpd_dib dp_dib[0]
#define dpc_dib dp_dib[1]

UNIT dpd_unit = { UDATA (NULL, 0, 0) };

REG dpd_reg[] = {
	{ ORDATA (IBUF, dpd_ibuf, 16) },
	{ ORDATA (OBUF, dpd_obuf, 16) },
	{ FLDATA (CMD, dpd_dib.cmd, 0) },
	{ FLDATA (CTL, dpd_dib.ctl, 0) },
	{ FLDATA (FLG, dpd_dib.flg, 0) },
	{ FLDATA (FBF, dpd_dib.fbf, 0) },
	{ BRDATA (DBUF, dpxb, 8, 16, DP_NUMWD) },
	{ DRDATA (BPTR, dp_ptr, DP_N_NUMWD) },
	{ ORDATA (DEVNO, dpd_dib.devno, 6), REG_HRO },
	{ FLDATA (*DEVENB, dpd_dib.enb, 0), REG_HRO },
	{ NULL }  };

MTAB dpd_mod[] = {
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &dpd_dib },
	{ 0 }  };

DEVICE dpd_dev = {
	"DPD", &dpd_unit, dpd_reg, dpd_mod,
	1, 10, DP_N_NUMWD, 1, 8, 16,
	NULL, NULL, &dpc_reset,
	NULL, NULL, NULL };

/* DPC data structures

   dpc_dev	DPC device descriptor
   dpc_unit	DPC unit list
   dpc_reg	DPC register list
   dpc_mod	DPC modifier list
*/

UNIT dpc_unit[] = {
	{ UDATA (&dpc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DP_SIZE2) },
	{ UDATA (&dpc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DP_SIZE2) },
	{ UDATA (&dpc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DP_SIZE2) },
	{ UDATA (&dpc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DP_SIZE2) }  };

REG dpc_reg[] = {
	{ ORDATA (OBUF, dpc_obuf, 16) },
	{ ORDATA (BUSY, dpc_busy, 3), REG_RO },
	{ ORDATA (RARC, dpc_rarc, 8) },
	{ ORDATA (RARH, dpc_rarh, 2) },
	{ ORDATA (RARS, dpc_rars, 4) },
	{ ORDATA (CNT, dpc_cnt, 5) },
	{ FLDATA (CMD, dpc_dib.cmd, 0) },
	{ FLDATA (CTL, dpc_dib.ctl, 0) },
	{ FLDATA (FLG, dpc_dib.flg, 0) },
	{ FLDATA (FBF, dpc_dib.fbf, 0) },
	{ FLDATA (EOC, dpc_eoc, 0) },
	{ DRDATA (CTIME, dpc_ctime, 24), PV_LEFT },
	{ DRDATA (STIME, dpc_stime, 24), PV_LEFT },
	{ DRDATA (XTIME, dpc_xtime, 24), REG_NZ + PV_LEFT },
	{ BRDATA (STA, dpc_sta, 8, 16, DP_NUMDRV) },
	{ FLDATA (CTYPE, dp_ctype, 0), REG_HRO },
	{ URDATA (CAPAC, dpc_unit[0].capac, 10, 31, 0,
		  DP_NUMDRV, PV_LEFT | REG_HRO) },
	{ URDATA (UFLG, dpc_unit[0].flags, 8, UNIT_W_UF, UNIT_V_UF - 1,
		  DP_NUMDRV, REG_HRO) },
	{ ORDATA (DEVNO, dpc_dib.devno, 6), REG_HRO },
	{ FLDATA (*DEVENB, dpc_dib.enb, 0), REG_HRO },
	{ NULL }  };

MTAB dpc_mod[] = {
/*	{ UNIT_WLK, 0, "write enabled", "ENABLED", &dpc_vlock }, */
/*	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", &dpc_vlock }, */
	{ MTAB_XTD | MTAB_VDV, 1, NULL, "13210A",
		&dp_settype, NULL, NULL },
	{ MTAB_XTD | MTAB_VDV, 0, NULL, "12557A",
		&dp_settype, NULL, NULL },
	{ MTAB_XTD | MTAB_VDV, 0, "TYPE", NULL,
		NULL, &dp_showtype, NULL },
	{ MTAB_XTD | MTAB_VDV, 1, NULL, "ENABLED",
		&set_enb, NULL, &dpd_dib },
	{ MTAB_XTD | MTAB_VDV, 1, NULL, "DISABLED",
		&set_dis, NULL, &dpd_dib },
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &dpd_dib },
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
	if (FLG (devd) == 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (devd) != 0) PC = (PC + 1) & VAMASK;
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
	if (FLG (devc) == 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (devc) != 0) PC = (PC + 1) & VAMASK;
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
		case FNC_INIT:				/* init */
			dp_go (fnc, drv, dpc_ctime, devc);
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
devc = dpc_dib.devno;					/* get cch devno */
devd = dpd_dib.devno;					/* get dch devno */
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
		if (uptr -> CYL >= DP_NUMCY) {		/* invalid cyl? */
			dpc_sta[drv] = dpc_sta[drv] | STA_SKE;
			uptr -> CYL = 0;  }
		if (dpc_rars >= DP_NUMSC)		/* invalid sec? */
			dpc_sta[drv] = dpc_sta[drv] | STA_SKE;
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
		dpd_ibuf = dpc_sta[drv] & ~(dp_ctype? STA_MBZ13: 0);
		if (dpc_sta[drv] & STA_ALLERR) dpd_ibuf = dpd_ibuf | STA_ERR;
		setFLG (devd);				/* set dch flg */
		clrCMD (devd);				/* clr dch cmd */
		dpc_sta[drv] = dpc_sta[drv] &		/* clr sta flags */
			~(STA_ATN | STA_DTE | STA_FLG | STA_AER | STA_EOC);
		dpc_busy = 0;  }			/* ctlr is free */
	else sim_activate (uptr, dpc_xtime);		/* wait more */
	return SCPE_OK;

case FNC_REF:						/* refine sector */
	if ((uptr -> CYL != dpc_rarc) || (dpc_rars >= DP_NUMSC))
		dpc_sta[drv] = dpc_sta[drv] | STA_AER;
	else {	for (i = 0; i < DP_NUMWD; i++) dpxb[i] = 0;
		da = GETDA (dpc_rarc, dpc_rarh, dpc_rars);	/* get addr */
		dpc_rars = dpc_rars + 1;		/* incr sector */
		if (dpc_rars >= DP_NUMSC) {		/* end of trk? */
			dpc_rars = 0;			/* wrap to */
			dpc_rarh = dpc_rarh ^ 1;  }	/* next surf */
		if (err = fseek (uptr -> fileref, da * sizeof (int16),
			SEEK_SET)) break;
		fxwrite (dpxb, sizeof (int16), DP_NUMWD, uptr -> fileref);
		err = ferror (uptr -> fileref);  }
	break;

case FNC_CHK:						/* check, need cnt */
	if (CMD (devd)) {				/* dch active? */
		dpc_cnt = dpd_obuf & DA_CKMASK;		/* get count */
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
		fxread (dpxb, sizeof (int16), DP_NUMWD, uptr -> fileref);
		if (err = ferror (uptr -> fileref)) break;  }
	dpd_ibuf = dpxb[dp_ptr++];			/* get word */
	if (dp_ptr >= DP_NUMWD) dp_ptr = 0;		/* wrap if last */
	setFLG (devd);					/* set dch flg */
	clrCMD (devd);					/* clr dch cmd */
	sim_activate (uptr, dpc_xtime);			/* sched next word */
	return SCPE_OK;

case FNC_INIT:						/* init */
case FNC_WD:						/* write */
	if (dpc_eoc) {					/* end of cyl? */
		dpc_sta[drv] = dpc_sta[drv] | STA_EOC;	/* set status */
		break;  }				/* done */
	if (FLG (devd)) dpc_sta[drv] = dpc_sta[drv] | STA_OVR;
	dpxb[dp_ptr++] = dpd_obuf;			/* store word */
	if (!CMD (devd)) {				/* dch clr? done */
		for ( ; dp_ptr < DP_NUMWD; dp_ptr++) dpxb[dp_ptr] = 0;  }
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
		fxwrite (dpxb, sizeof (int16), DP_NUMWD, uptr -> fileref);
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
dpc_dib.cmd = dpd_dib.cmd = 0;				/* clear cmd */
dpc_dib.ctl = dpd_dib.ctl = 0;				/* clear ctl */
dpc_dib.fbf = dpd_dib.fbf = 1;				/* set fbf */
dpc_dib.flg = dpd_dib.flg = 1;				/* set flg */
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

/* Set controller type */

t_stat dp_settype (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 i;

if ((val < 0) || (val > 1) || (cptr != NULL)) return SCPE_ARG;
for (i = 0; i < DP_NUMDRV; i++) {
	if (dpc_unit[i].flags & UNIT_ATT) return SCPE_ALATT;  }
for (i = 0; i < DP_NUMDRV; i++)
	dpc_unit[i].capac = (val? DP_SIZE3: DP_SIZE2);
dp_ctype = val;
return SCPE_OK;
}

/* Show controller type */

t_stat dp_showtype (FILE *st, UNIT *uptr, int32 val, void *desc)
{
if (dp_ctype) fprintf (st, "13210A");
else fprintf (st, "12557A");
return SCPE_OK;
}


