/* hp2100_dp.c: HP 2100 12557A/13210A disk simulator

   Copyright (c) 1993-2003, Robert M. Supnik

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

   dp		12557A 2871 disk subsystem
		13210A 7900 disk subsystem

   25-Apr-03	RMS	Revised for extended file support
			Fixed bug(s) in boot (found by Terry Newton)
   10-Nov-02	RMS	Added BOOT command, fixed numerous bugs
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
#define FNC		u3				/* saved function */
#define CYL		u4				/* cylinder */
#define UNIT_WPRT	(UNIT_WLK | UNIT_RO)		/* write prot */

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
#define  FNC_SEEK3	022				/* fake - seek3 */
#define  FNC_CHK1	023				/* fake - check1 */
#define  FNC_AR1	024				/* fake - arec1 */
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

/* Status in dpc_sta[drv], (u) = unused in 13210, (d) = dynamic */

#define STA_ATN		0100000				/* attention (u) */
#define STA_1ST		0040000				/* first status */
#define STA_OVR		0020000				/* overrun */
#define STA_RWU		0010000				/* rw unsafe NI (u) */
#define STA_ACU		0004000				/* access unsafe NI */
#define STA_HUNT	0002000				/* hunting NI (12557) */
#define STA_PROT	0002000				/* protected (13210) */
#define STA_SKI		0001000				/* incomplete NI (u) */
#define STA_SKE		0000400				/* seek error */
/*			0000200				/* unused */
#define STA_NRDY	0000100				/* not ready (d) */
#define STA_EOC		0000040				/* end of cylinder */
#define STA_AER		0000020				/* addr error */
#define STA_FLG		0000010				/* flagged */
#define STA_BSY		0000004				/* seeking */
#define STA_DTE		0000002				/* data error */
#define STA_ERR		0000001				/* any error (d) */
#define STA_ALLERR	(STA_ATN + STA_1ST + STA_OVR + STA_RWU + STA_ACU + \
			 STA_SKI + STA_SKE + STA_NRDY + STA_EOC + STA_AER + \
			 STA_FLG + STA_BSY + STA_DTE)
#define STA_MBZ13	(STA_ATN + STA_RWU + STA_SKI)	/* zero in 13210 */

extern uint16 *M;
extern uint32 PC, SR;
extern uint32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];
extern int32 sim_switches;
extern UNIT cpu_unit;

int32 dp_ctype = 1;					/* ctrl type */
int32 dpc_busy = 0;					/* cch unit */
int32 dpc_cnt = 0;					/* check count */
int32 dpc_eoc = 0;					/* end of cyl */
int32 dpc_stime = 100;					/* seek time */
int32 dpc_ctime = 100;					/* command time */
int32 dpc_xtime = 5;					/* xfer time */
int32 dpc_dtime = 2;					/* dch time */
int32 dpd_obuf = 0, dpd_ibuf = 0;			/* dch buffers */
int32 dpc_obuf = 0;					/* cch buffers */
int32 dpd_xfer = 0;					/* xfer in prog */
int32 dpd_wval = 0;					/* write data valid */
int32 dp_ptr = 0;					/* buffer ptr */
uint8 dpc_rarc[DP_NUMDRV] = { 0 };			/* cylinder */
uint8 dpc_rarh[DP_NUMDRV] = { 0 };			/* head */
uint8 dpc_rars[DP_NUMDRV] = { 0 };			/* sector */
uint16 dpc_sta[DP_NUMDRV] = { 0 };			/* status regs */
uint16 dpxb[DP_NUMWD];					/* sector buffer */

DEVICE dpd_dev, dpc_dev;
int32 dpdio (int32 inst, int32 IR, int32 dat);
int32 dpcio (int32 inst, int32 IR, int32 dat);
t_stat dpc_svc (UNIT *uptr);
t_stat dpd_svc (UNIT *uptr);
t_stat dpc_reset (DEVICE *dptr);
t_stat dpc_vlock (UNIT *uptr, int32 val);
t_stat dpc_attach (UNIT *uptr, char *cptr);
t_stat dpc_boot (int32 unitno, DEVICE *dptr);
void dp_god (int32 fnc, int32 drv, int32 time);
void dp_goc (int32 fnc, int32 drv, int32 time);
t_stat dp_settype (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat dp_showtype (FILE *st, UNIT *uptr, int32 val, void *desc);

/* DPD data structures

   dpd_dev	DPD device descriptor
   dpd_unit	DPD unit list
   dpd_reg	DPD register list
*/

DIB dp_dib[] = {
	{ DPD, 0, 0, 0, 0, &dpdio },
	{ DPC, 0, 0, 0, 0, &dpcio }  };

#define dpd_dib dp_dib[0]
#define dpc_dib dp_dib[1]

UNIT dpd_unit = { UDATA (&dpd_svc, 0, 0) };

REG dpd_reg[] = {
	{ ORDATA (IBUF, dpd_ibuf, 16) },
	{ ORDATA (OBUF, dpd_obuf, 16) },
	{ FLDATA (CMD, dpd_dib.cmd, 0) },
	{ FLDATA (CTL, dpd_dib.ctl, 0) },
	{ FLDATA (FLG, dpd_dib.flg, 0) },
	{ FLDATA (FBF, dpd_dib.fbf, 0) },
	{ FLDATA (XFER, dpd_xfer, 0) },
	{ FLDATA (WVAL, dpd_wval, 0) },
	{ BRDATA (DBUF, dpxb, 8, 16, DP_NUMWD) },
	{ DRDATA (BPTR, dp_ptr, DP_N_NUMWD) },
	{ ORDATA (DEVNO, dpd_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB dpd_mod[] = {
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &dpd_dev },
	{ 0 }  };

DEVICE dpd_dev = {
	"DPD", &dpd_unit, dpd_reg, dpd_mod,
	1, 10, DP_N_NUMWD, 1, 8, 16,
	NULL, NULL, &dpc_reset,
	NULL, NULL, NULL,
	&dpd_dib, 0 };

/* DPC data structures

   dpc_dev	DPC device descriptor
   dpc_unit	DPC unit list
   dpc_reg	DPC register list
   dpc_mod	DPC modifier list
*/

UNIT dpc_unit[] = {
	{ UDATA (&dpc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
		UNIT_ROABLE, DP_SIZE3) },
	{ UDATA (&dpc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
		UNIT_ROABLE, DP_SIZE3) },
	{ UDATA (&dpc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
		UNIT_ROABLE, DP_SIZE3) },
	{ UDATA (&dpc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
		UNIT_ROABLE, DP_SIZE3) }  };

REG dpc_reg[] = {
	{ ORDATA (OBUF, dpc_obuf, 16) },
	{ ORDATA (BUSY, dpc_busy, 3), REG_RO },
	{ ORDATA (CNT, dpc_cnt, 5) },
	{ FLDATA (CMD, dpc_dib.cmd, 0) },
	{ FLDATA (CTL, dpc_dib.ctl, 0) },
	{ FLDATA (FLG, dpc_dib.flg, 0) },
	{ FLDATA (FBF, dpc_dib.fbf, 0) },
	{ FLDATA (EOC, dpc_eoc, 0) },
	{ BRDATA (RARC, dpc_rarc, 8, 8, DP_NUMDRV) },
	{ BRDATA (RARH, dpc_rarh, 8, 2, DP_NUMDRV) },
	{ BRDATA (RARS, dpc_rars, 8, 4, DP_NUMDRV) },
	{ BRDATA (STA, dpc_sta, 8, 16, DP_NUMDRV) },
	{ DRDATA (CTIME, dpc_ctime, 24), PV_LEFT },
	{ DRDATA (DTIME, dpc_dtime, 24), PV_LEFT },
	{ DRDATA (STIME, dpc_stime, 24), PV_LEFT },
	{ DRDATA (XTIME, dpc_xtime, 24), REG_NZ + PV_LEFT },
	{ FLDATA (CTYPE, dp_ctype, 0), REG_HRO },
	{ URDATA (UCYL, dpc_unit[0].CYL, 10, 8, 0,
		  DP_NUMDRV, PV_LEFT | REG_HRO) },
	{ URDATA (UFNC, dpc_unit[0].FNC, 8, 8, 0,
		  DP_NUMDRV, REG_HRO) },
	{ URDATA (CAPAC, dpc_unit[0].capac, 10, T_ADDR_W, 0,
		  DP_NUMDRV, PV_LEFT | REG_HRO) },
	{ ORDATA (DEVNO, dpc_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB dpc_mod[] = {
	{ UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
	{ MTAB_XTD | MTAB_VDV, 1, NULL, "13210A",
		&dp_settype, NULL, NULL },
	{ MTAB_XTD | MTAB_VDV, 0, NULL, "12557A",
		&dp_settype, NULL, NULL },
	{ MTAB_XTD | MTAB_VDV, 0, "TYPE", NULL,
		NULL, &dp_showtype, NULL },
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &dpd_dev },
	{ 0 }  };

DEVICE dpc_dev = {
	"DPC", dpc_unit, dpc_reg, dpc_mod,
	DP_NUMDRV, 8, 24, 1, 8, 16,
	NULL, NULL, &dpc_reset,
	&dpc_boot, &dpc_attach, NULL,
	&dpc_dib, DEV_DISABLE };

/* IOT routines */

int32 dpdio (int32 inst, int32 IR, int32 dat)
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
	dpd_obuf = dat;
	if (!dpc_busy || dpd_xfer) dpd_wval = 1;	/* if !overrun, valid */
	break;
case ioMIX:						/* merge */
	dat = dat | dpd_ibuf;
	break;
case ioLIX:						/* load */
	dat = dpd_ibuf;
	break;
case ioCTL:						/* control clear/set */
	if (IR & I_CTL) {				/* CLC */
	    clrCTL (devd);				/* clr ctl, cmd */
	    clrCMD (devd);
	    dpd_xfer = 0;  }				/* clr xfer */
	else {						/* STC */
	    if (!dp_ctype) setCTL (devd);		/* 12557: set ctl */
	    setCMD (devd);				/* set cmd */
	    if (dpc_busy && !dpd_xfer)			/* overrun? */
		dpc_sta[dpc_busy - 1] |= STA_OVR;  }
	break;
default:
	break;  }
if (IR & I_HC) { clrFLG (devd); }			/* H/C option */
return dat;
}

int32 dpcio (int32 inst, int32 IR, int32 dat)
{
int32 i, devc, fnc, drv;
int32 devd = dpd_dib.devno;

devc = IR & I_DEVMASK;					/* get device no */
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
	dpc_obuf = dat;
	break;
case ioLIX:						/* load */
	dat = 0;
case ioMIX:						/* merge */
	for (i = 0; i < DP_NUMDRV; i++)
	    if (dpc_sta[i] & STA_ATN) dat = dat | (1 << i);
	break;
case ioCTL:						/* control clear/set */
	if (IR & I_CTL) {				/* CLC? */
	    clrCTL (devc);				/* clr cmd, ctl */
	    clrCMD (devc);				/* cancel non-seek */
	    if (dpc_busy) sim_cancel (&dpc_unit[dpc_busy - 1]);
	    sim_cancel (&dpd_unit);			/* cancel dch */
	    dpd_xfer = 0;				/* clr dch xfer */
	    dpc_busy = 0;  }				/* clr cch busy */
	else {						/* STC */
	    setCTL (devc);				/* set ctl */
	    if (!CMD (devc)) {				/* is cmd clr? */
		setCMD (devc);				/* set cmd */
		drv = CW_GETDRV (dpc_obuf);		/* get fnc, drv */
		fnc = CW_GETFNC (dpc_obuf);		/* from cmd word */
		switch (fnc) {				/* case on fnc */
		case FNC_STA:				/* rd sta */
		    if (dp_ctype) { clrFLG (devd); }	/* 13210? clr dch flag */
		case FNC_SEEK: case FNC_CHK:		/* seek, check */
		case FNC_AR:				/* addr rec */
		    dp_god (fnc, drv, dpc_dtime);	/* sched dch xfr */
		    break;
		case FNC_RD: case FNC_WD:		/* read, write */
		case FNC_REF: case FNC_INIT:		/* refine, init */
		    dp_goc (fnc, drv, dpc_ctime);	/* sched drive */
		    break;  }				/* end case */
		}
	    }						/* end else */
	break;
default:
	break;  }
if (IR & I_HC) { clrFLG (devc); }			/* H/C option */
return dat;
}

/* Start data channel operation */

void dp_god (int32 fnc, int32 drv, int32 time)
{
dpd_unit.CYL = drv;					/* save unit */
dpd_unit.FNC = fnc;					/* save function */
sim_activate (&dpd_unit, time);
return;
}

/* Start controller operation */

void dp_goc (int32 fnc, int32 drv, int32 time)
{
if (sim_is_active (&dpc_unit[drv])) {			/* still seeking? */
	sim_cancel (&dpc_unit[drv]);			/* stop seek */
	dpc_sta[drv] = dpc_sta[drv] & ~STA_BSY;		/* clear busy */
	time = time + dpc_stime;  }			/* take longer */
dp_ptr = 0;						/* init buf ptr */
dpc_eoc = 0;						/* clear end cyl */
dpc_busy = drv + 1;					/* set busy */
dpd_xfer = 1;						/* xfer in prog */
dpc_unit[drv].FNC = fnc;				/* save function */
sim_activate (&dpc_unit[drv], time);			/* activate unit */
return;
}

/* Data channel unit service

   This routine handles the data channel transfers.  It also handles
   data transfers that are blocked by seek in progress.

   uptr->CYL	=	target drive
   uptr->FNC	=	target function

   Seek substates
	seek	-	transfer cylinder
	seek1	-	transfer head/surface
   Address record
	ar	-	transfer cylinder
	ar1	-	transfer head/surface, finish operation
   Status check	-	transfer status, finish operation
   Check data
	chk	-	transfer sector count
*/

t_stat dpd_svc (UNIT *uptr)
{
int32 drv, devc, devd, st;

drv = uptr->CYL;					/* get drive no */
devc = dpc_dib.devno;					/* get cch devno */
devd = dpd_dib.devno;					/* get dch devno */
switch (uptr->FNC) {					/* case function */

case FNC_SEEK:						/* seek, need cyl */
	if (CMD (devd)) {				/* dch active? */
	    dpc_rarc[drv] = DA_GETCYL (dpd_obuf);	/* take cyl word */
	    dpd_wval = 0;				/* clr data valid */
	    setFLG (devd);				/* set dch flg */
	    clrCMD (devd);				/* clr dch cmd */
	    uptr->FNC = FNC_SEEK1;  }			/* advance state */
	sim_activate (uptr, dpc_xtime);			/* no, wait more */
	break;
case FNC_SEEK1:						/* seek, need hd/sec */
	if (CMD (devd)) {				/* dch active? */
	    dpc_rarh[drv] = DA_GETHD (dpd_obuf);	/* get head */
	    dpc_rars[drv] = DA_GETSC (dpd_obuf);	/* get sector */
	    dpd_wval = 0;				/* clr data valid */
	    setFLG (devd);				/* set dch flg */
	    clrCMD (devd);				/* clr dch cmd */
	    if (sim_is_active (&dpc_unit[drv])) {	/* if busy, */
		dpc_sta[drv] = dpc_sta[drv] | STA_SKE;
		break;  }				/* error, ignore */
	    st = abs (dpc_rarc[drv] - dpc_unit[drv].CYL) * dpc_stime;
	    if (st == 0) st = dpc_stime;		/* min time */
	    sim_activate (&dpc_unit[drv], st);		/* schedule drive */
	    dpc_sta[drv] = (dpc_sta[drv] | STA_BSY) &
		~(STA_SKE | STA_SKI | STA_HUNT);
	    dpc_unit[drv].CYL = dpc_rarc[drv];		/* on cylinder */
	    dpc_unit[drv].FNC = FNC_SEEK2;  }		/* set operation */
	else sim_activate (uptr, dpc_xtime);		/* no, wait more */
	break;

case FNC_AR:						/* arec, need cyl */
	if (CMD (devd)) {				/* dch active? */
	    dpc_rarc[drv] = DA_GETCYL (dpd_obuf);	/* take cyl word */
	    dpd_wval = 0;				/* clr data valid */
	    setFLG (devd);				/* set dch flg */
	    clrCMD (devd);				/* clr dch cmd */
	    uptr->FNC = FNC_AR1;  }			/* advance state */
	sim_activate (uptr, dpc_xtime);			/* no, wait more */
	break;
case FNC_AR1:						/* arec, need hd/sec */
	if (CMD (devd)) {				/* dch active? */
	    dpc_rarh[drv] = DA_GETHD (dpd_obuf);	/* get head */
	    dpc_rars[drv] = DA_GETSC (dpd_obuf);	/* get sector */
	    dpd_wval = 0;				/* clr data valid */
	    setFLG (devc);				/* set cch flg */
	    clrCMD (devc);				/* clr cch cmd */
	    setFLG (devd);				/* set dch flg */
	    clrCMD (devd);  }				/* clr dch cmd */
	else sim_activate (uptr, dpc_xtime);		/* no, wait more */
	break;

case FNC_STA:						/* read status */
	if (CMD (devd) || dp_ctype) {			/* dch act or 13210? */
	    if (dpc_unit[drv].flags & UNIT_ATT) {	/* attached? */
		dpd_ibuf = dpc_sta[drv] & ~STA_ERR;	/* clear err */
		if (dp_ctype) dpd_ibuf =		/* 13210? */
		    (dpd_ibuf & ~(STA_MBZ13 | STA_PROT)) |
		    (uptr->flags & UNIT_WPRT? STA_PROT: 0);  }
	    else dpd_ibuf = STA_NRDY;			/* not ready */
	    if (dpd_ibuf & STA_ALLERR)			/* errors? set flg */
		dpd_ibuf = dpd_ibuf | STA_ERR;
	    setFLG (devd);				/* set dch flg */
	    clrCMD (devd);				/* clr dch cmd */
	    clrCMD (devc);				/* clr cch cmd */
	    dpc_sta[drv] = dpc_sta[drv] &		/* clr sta flags */
		~(STA_ATN | STA_1ST | STA_OVR |
		STA_RWU | STA_ACU | STA_EOC |
		STA_AER | STA_FLG | STA_DTE);  }
	else sim_activate (uptr, dpc_xtime);		/* wait more */
	break;

case FNC_CHK:						/* check, need cnt */
	if (CMD (devd)) {				/* dch active? */
	    dpc_cnt = dpd_obuf & DA_CKMASK;		/* get count */
	    dpd_wval = 0;				/* clr data valid */
/*	    setFLG (devd);				/* set dch flg */
/*	    clrCMD (devd);				/* clr dch cmd */
	    dp_goc (FNC_CHK1, drv, dpc_xtime);  }	/* sched drv */
	else sim_activate (uptr, dpc_xtime);		/* wait more */
	break;

default:
	return SCPE_IERR;  }

return SCPE_OK;
}

/* Drive unit service

   This routine handles the data transfers.

   Seek substates
	seek2	-	done
   Refine sector -	erase sector, finish operation
   Check data
	chk1	-	finish operation
   Read
   Write
*/

#define GETDA(x,y,z) \
	(((((x) * DP_NUMSF) + (y)) * DP_NUMSC) + (z)) * DP_NUMWD

t_stat dpc_svc (UNIT *uptr)
{
int32 da, drv, devc, devd, err;

err = 0;						/* assume no err */
drv = uptr - dpc_dev.units;				/* get drive no */
devc = dpc_dib.devno;					/* get cch devno */
devd = dpd_dib.devno;					/* get dch devno */
if ((uptr->flags & UNIT_ATT) == 0) {			/* not attached? */
	setFLG (devc);					/* set cch flg */
	clrCMD (devc);					/* clr cch cmd */
	dpc_sta[drv] = 0;				/* clr status */
	dpc_busy = 0;					/* ctlr is free */
	dpd_xfer = dpd_wval = 0;
	return SCPE_OK;  }
switch (uptr->FNC) {					/* case function */

case FNC_SEEK2:						/* seek done */
	dpc_sta[drv] = (dpc_sta[drv] | STA_ATN) & ~STA_BSY;
	if (uptr->CYL >= DP_NUMCY) {			/* invalid cyl? */
	    dpc_sta[drv] = dpc_sta[drv] | STA_SKE;
	    uptr->CYL = DP_NUMCY - 1;  }
case FNC_SEEK3:						/* waiting for flag */
	if (dpc_busy || FLG (devc)) {			/* ctrl busy? wait */
	    uptr->FNC = FNC_SEEK3;			/* next state */
	    sim_activate (uptr, dpc_xtime);  }
	else {
	    setFLG (devc);				/* set cch flg */
	    clrCMD (devc);  }				/* clear cmd */
	return SCPE_OK;

case FNC_REF:						/* refine sector */
	break;						/* just a NOP */

case FNC_RD:						/* read */
case FNC_CHK1:						/* check */
	if (dp_ptr == 0) {				/* new sector? */
	    if (!CMD (devd) && (uptr->FNC != FNC_CHK1)) break;
	    if (uptr->CYL != dpc_rarc[drv])		/* wrong cyl? */
		dpc_sta[drv] = dpc_sta[drv] | STA_AER;	/* set flag, read */
	    if (dpc_rars[drv] >= DP_NUMSC) {		/* bad sector? */
		dpc_sta[drv] = dpc_sta[drv] | STA_AER;	/* set flag, stop */
		break;  }
	    if (dpc_eoc) {				/* end of cyl? */
		dpc_sta[drv] = dpc_sta[drv] | STA_EOC;
		break;  }
	    da = GETDA (dpc_rarc[drv], dpc_rarh[drv], dpc_rars[drv]);
	    dpc_rars[drv] = dpc_rars[drv] + 1;		/* incr address */
	    if (dpc_rars[drv] >= DP_NUMSC) {		/* end of surf? */
		dpc_rars[drv] = 0;			/* wrap to */
		dpc_rarh[drv] = dpc_rarh[drv] ^ 1;	/* next head */
		dpc_eoc = ((dpc_rarh[drv] & 1) == 0);  }/* calc eoc */
	    if (err = fseek (uptr->fileref, da * sizeof (int16),
		SEEK_SET)) break;
	    fxread (dpxb, sizeof (int16), DP_NUMWD, uptr->fileref);
	    if (err = ferror (uptr->fileref)) break;  }
	dpd_ibuf = dpxb[dp_ptr++];			/* get word */
	if (dp_ptr >= DP_NUMWD) {			/* end of sector? */
	    if (uptr->FNC == FNC_CHK1) {		/* check? */
		dpc_cnt = (dpc_cnt - 1) & DA_CKMASK;	/* decr count */
		if (dpc_cnt == 0) break;  }		/* stop at zero */
	    dp_ptr = 0;  }				/* wrap buf ptr */
	if (CMD (devd) && dpd_xfer) {			/* dch on, xfer? */
	    setFLG (devd); }				/* set flag */
	clrCMD (devd);					/* clr dch cmd */
	sim_activate (uptr, dpc_xtime);			/* sched next word */
	return SCPE_OK;

case FNC_INIT:						/* init */
case FNC_WD:						/* write */
	if (dp_ptr == 0) {				/* start sector? */
	    if (!CMD (devd) && !dpd_wval) break;	/* xfer done? */
	    if (uptr->flags & UNIT_WPRT) {		/* wr prot? */
		dpc_sta[drv] = dpc_sta[drv] | STA_FLG;	/* set status */
		break;  }				/* done */
	    if ((uptr->CYL != dpc_rarc[drv]) ||		/* wrong cyl or */
		(dpc_rars[drv] >= DP_NUMSC)) {		/* bad sector? */
		dpc_sta[drv] = dpc_sta[drv] | STA_AER;	/* set flag, stop */
		break;  }
	    if (dpc_eoc) {				/* end of cyl? */
	        dpc_sta[drv] = dpc_sta[drv] | STA_EOC;	/* set status */
	        break;  }  }				/* done */
	dpxb[dp_ptr++] = dpd_wval? dpd_obuf: 0;		/* store word/fill */
	dpd_wval = 0;					/* clr data valid */
	if (dp_ptr >= DP_NUMWD) {			/* buffer full? */
	    da = GETDA (dpc_rarc[drv], dpc_rarh[drv], dpc_rars[drv]);
	    dpc_rars[drv] = dpc_rars[drv] + 1;		/* incr address */
	    if (dpc_rars[drv] >= DP_NUMSC) {		/* end of surf? */
		dpc_rars[drv] = 0;			/* wrap to */
		dpc_rarh[drv] = dpc_rarh[drv] ^ 1;	/* next head */
		dpc_eoc = ((dpc_rarh[drv] & 1) == 0);  }/* calc eoc */
	    if (err = fseek (uptr->fileref, da * sizeof (int16),
		SEEK_SET)) break;
	    fxwrite (dpxb, sizeof (int16), DP_NUMWD, uptr->fileref);
	    if (err = ferror (uptr->fileref)) break;	/* error? */
	    dp_ptr = 0;  }				/* next sector */
	if (CMD (devd) && dpd_xfer) {			/* dch on, xfer? */
	    setFLG (devd); }				/* set flag */
	clrCMD (devd);					/* clr dch cmd */
	sim_activate (uptr, dpc_xtime);			/* sched next word */
	return SCPE_OK;

default:
	return SCPE_IERR;  }				/* end case fnc */

if (!dp_ctype) dpc_sta[drv] = dpc_sta[drv] | STA_ATN;	/* 12559 sets ATN */
setFLG (devc);						/* set cch flg */
clrCMD (devc);						/* clr cch cmd */
dpc_busy = 0;						/* ctlr is free */
dpd_xfer = dpd_wval = 0;
if (err != 0) {						/* error? */
	perror ("DP I/O error");
	clearerr (uptr->fileref);
	return SCPE_IOERR;  }
return SCPE_OK;
}

/* Reset routine */

t_stat dpc_reset (DEVICE *dptr)
{
int32 i;

hp_enbdis_pair (&dpc_dev, &dpd_dev);			/* make pair cons */
dpd_ibuf = dpd_obuf = 0;				/* clear buffers */
dpc_busy = dpc_obuf = 0;
dpc_eoc = 0;
dpd_xfer = dpd_wval = 0;
dp_ptr = 0;
dpc_dib.cmd = dpd_dib.cmd = 0;				/* clear cmd */
dpc_dib.ctl = dpd_dib.ctl = 0;				/* clear ctl */
dpc_dib.fbf = dpd_dib.fbf = 1;				/* set fbf */
dpc_dib.flg = dpd_dib.flg = 1;				/* set flg */
sim_cancel (&dpd_unit);					/* cancel dch */
for (i = 0; i < DP_NUMDRV; i++) {			/* loop thru drives */
	sim_cancel (&dpc_unit[i]);			/* cancel activity */
	dpc_unit[i].FNC = 0;				/* clear function */
	dpc_unit[i].CYL = 0;
	dpc_rarc[i] = dpc_rarh[i] = dpc_rars[i] = 0;
	if (dpc_unit[i].flags & UNIT_ATT)
	    dpc_sta[i] = dpc_sta[i] & STA_1ST;
	else dpc_sta[i] = 0;  }
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
dpc_sta[drv] = dpc_sta[drv] | STA_1ST;			/* update status */
return r;
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

/* 7900/7901 bootstrap routine (HP 12992F ROM) */

#define LDR_BASE	077
#define CHANGE_DEV	(1 << 24)

static const int32 dboot[IBL_LNT] = {
	0106700+CHANGE_DEV,	/*ST CLC DC		; clr dch */
	0106701+CHANGE_DEV,	/*   CLC CC		; clr cch */
	0017757,		/*   JSB STAT		; get status */
	0067746,		/*SK LDB SKCMD		; seek cmd */
	0106600+CHANGE_DEV,	/*   OTB DC		; cyl # */
	0103700+CHANGE_DEV,	/*   STC DC,C		; to dch */
	0106601+CHANGE_DEV,	/*   OTB CC		; seek cmd */
	0103701+CHANGE_DEV,	/*   STC CC,C		; to cch */
	0102300+CHANGE_DEV,	/*   SFS DC		; addr wd ok? */
	0027710,		/*   JMP *-1		; no, wait */
	0006400,		/*   CLB */
	0102501,		/*   LIA 1		; read switches */
	0002011,		/*   SLA,RSS		; <0> set? */
	0047747,		/*   ADB BIT9		; head 2 = fixed */
	0106600+CHANGE_DEV,	/*   OTB DC		; head/sector */
	0103700+CHANGE_DEV,	/*   STC DC,C		; to dch */
	0102301+CHANGE_DEV,	/*   SFS CC		; seek done? */
	0027720,		/*   JMP *-1		; no, wait */
	0017757,		/*   JSB STAT		; get status */
	0067776,		/*   LDB DMACW		; DMA control */
	0106606,		/*   OTB 6 */
	0067750,		/*   LDB ADDR1		; memory addr */
	0106602,		/*   OTB 2 */
	0102702,		/*   STC 2		; flip DMA ctrl */
	0067752,		/*   LDB CNT		; word count */
	0106602,		/*   OTB 2 */
	0063745,		/*   LDB RDCMD		; read cmd */
	0102601+CHANGE_DEV,	/*   OTA CC		; to cch */
	0103700+CHANGE_DEV,	/*   STC DC,C		; start dch */
	0103606,		/*   STC 6,C		; start DMA */
	0103701+CHANGE_DEV,	/*   STC CC,C		; start cch */
	0102301+CHANGE_DEV,	/*   SFS CC		; done? */
	0027737,		/*   JMP *-1		; no, wait */
	0017757,		/*   JSB STAT		; get status */
	0027775,		/*   JMP XT		; done */
	0037766,		/*FSMSK 037766		; status mask */
	0004000,		/*STMSK 004000		; unsafe mask */
	0020000,		/*RDCMD 020000		; read cmd */
	0030000,		/*SKCMD 030000		; seek cmd */
	0001000,		/*BIT9  001000		; head 2 select */
	0102011,		/*ADDR1 102011 */
	0102055,		/*ADDR2 102055 */
	0164000,		/*CNT   -6144. */
	0, 0, 0, 0,		/* unused */
	0000000,		/*STAT 0 */
	0002400,		/*   CLA		; status request */
	0102601+CHANGE_DEV,	/*   OTC CC		; to cch */
	0103701+CHANGE_DEV,	/*   STC CC,C		; start cch */
	0102300+CHANGE_DEV,	/*   SFS DC		; done? */
	0027763,		/*   JMP *-1 */
	0102500+CHANGE_DEV,	/*   LIA DC		; get status */
	0013743,		/*   AND FSMSK		; mask 15,14,3,0 */
	0002003,		/*   SZA,RSS		; drive ready? */
	0127757,		/*   JMP STAT,I		; yes */
	0013744,		/*   AND STMSK		; fault? */
	0002002,		/*   SZA */
	0102030,		/*   HLT 30		; yes */
	0027700,		/*   JMP ST		; no, retry */
	0117751,		/*XT JSB ADDR2,I	; start program */
	0120000+CHANGE_DEV,	/*DMACW 120000+DC */
	0000000 };		/*   -ST */

t_stat dpc_boot (int32 unitno, DEVICE *dptr)
{
int32 i, dev;

if (unitno != 0) return SCPE_NOFNC;			/* only unit 0 */
dev = dpd_dib.devno;					/* get data chan dev */
PC = ((MEMSIZE - 1) & ~IBL_MASK) & VAMASK;		/* start at mem top */
SR = IBL_DP + (dev << IBL_V_DEV);			/* set SR */
if (sim_switches & SWMASK ('F')) SR = SR | IBL_FIX;	/* boot from fixed? */
for (i = 0; i < IBL_LNT; i++) {				/* copy bootstrap */
	if (dboot[i] & CHANGE_DEV)			/* IO instr? */
	    M[PC + i] = (dboot[i] + dev) & DMASK;
	else M[PC + i] = dboot[i];  }
M[PC + LDR_BASE] = (~PC + 1) & DMASK;	
return SCPE_OK;
}

