/* hp2100_dr.c: HP 2100 12606B/12610B fixed head disk/drum simulator

   Copyright (c) 1993-2000, Robert M. Supnik

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

   fhd		12606B fixed head disk
		12619B drum

   These head-per-track devices are buffered in memory, to minimize overhead.

   The drum data channel does not have a command flip-flop.  Further, its
   control flip-flop is not wired into the interrupt chain.  Accordingly,
   the simulator uses command rather than control for the data channel.
   
   The drum control channel does not have any of the traditional flip-flops.
*/

#include "hp2100_defs.h"
#include <math.h>

/* Constants */

#define DR_NUMWD	64				/* words/sector */
#define DR_FNUMSC	90				/* fhd sec/track */
#define DR_DNUMSC	32				/* drum sec/track */
#define DR_NUMSC	((drc_unit.flags & UNIT_DR)? DR_DNUMSC: DR_FNUMSC)
#define DR_SIZE		(512 * DR_DNUMSC * DR_NUMWD)	/* initial size */
#define UNIT_V_DR	(UNIT_V_UF)			/* disk vs drum */
#define UNIT_DR		(1 << UNIT_V_DR)

/* Command word */

#define CW_WR		0100000				/* write vs read */
#define CW_V_FTRK	7
#define CW_M_FTRK	0177
#define CW_V_DTRK	5
#define CW_M_DTRK	01777
#define MAX_TRK		(((drc_unit.flags & UNIT_DR)? CW_M_DTRK: CW_M_FTRK) + 1)
#define CW_GETTRK(x)	((drc_unit.flags & UNIT_DR)? \
				(((x) >> CW_V_DTRK) & CW_M_DTRK): \
				(((x) >> CW_V_FTRK) & CW_M_FTRK))
#define CW_PUTTRK(x)	((drc_unit.flags & UNIT_DR)? \
				(((x) & CW_M_DTRK) << CW_V_DTRK): \
				(((x) & CW_M_FTRK) << CW_V_FTRK))
#define CW_V_FSEC	0
#define CW_M_FSEC	0177
#define CW_V_DSEC	0
#define CW_M_DSEC	037
#define CW_GETSEC(x)	((drc_unit.flags & UNIT_DR)? \
				(((x) >> CW_V_DSEC) & CW_M_DSEC): \
				(((x) >> CW_V_FSEC) & CW_M_FSEC))
#define CW_PUTSEC(x)	((drc_unit.flags & UNIT_DR)? \
				(((x) & CW_M_DSEC) << CW_V_DSEC): \
				(((x) & CW_M_FSEC) << CW_V_FSEC))

/* Status register */

#define DRS_V_NS	8				/* next sector */
#define DRS_M_NS	0177
#define DRS_SEC		0100000				/* sector flag */
#define DRS_RDY		0000200				/* ready */
#define DRS_RIF		0000100				/* read inhibit */
#define DRS_SAC		0000040				/* sector coincidence */
#define DRS_ABO		0000010				/* abort */
#define DRS_WEN		0000004				/* write enabled */
#define DRS_PER		0000002				/* parity error */
#define DRS_BSY		0000001				/* busy */

#define GET_CURSEC(x)	((int32) fmod (sim_gtime() / ((double) (x)), \
			((double) ((drc_unit.flags & UNIT_DR)? DR_DNUMSC: DR_FNUMSC))))

extern UNIT cpu_unit;
extern int32 PC;
extern int32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];
int32 drc_cw = 0;					/* fnc, addr */
int32 drc_sta = 0;					/* status */
int32 drd_ibuf = 0;					/* input buffer */
int32 drd_obuf = 0;					/* output buffer */
int32 drd_ptr = 0;					/* sector pointer */
int32 dr_stopioe = 1;					/* stop on error */
int32 dr_time = 5;					/* time per word*/

int32 drdio (int32 inst, int32 IR, int32 dat);
int32 drcio (int32 inst, int32 IR, int32 dat);
t_stat drc_svc (UNIT *uptr);
t_stat drc_reset (DEVICE *dptr);
int32 dr_incda (int32 trk, int32 sec, int32 ptr);
t_stat dr_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);

/* DRD data structures

   drd_dev	device descriptor
   drd_unit	unit descriptor
   drd_reg	register list
*/

DIB dr_dib[] = {
	{ DRD, 1, 0, 0, 0, 0, &drdio },
	{ DRC, 1, 0, 0, 0, 0, &drcio }  };

#define drd_dib dr_dib[0]
#define drc_dib dr_dib[1]

UNIT drd_unit =	{ UDATA (NULL, 0, 0) };

REG drd_reg[] = {
	{ ORDATA (IBUF, drd_ibuf, 16) },
	{ ORDATA (OBUF, drd_obuf, 16) },
	{ FLDATA (CMD, drd_dib.cmd, 0) },
	{ FLDATA (CTL, drd_dib.ctl, 0) },
	{ FLDATA (FLG, drd_dib.flg, 0) },
	{ FLDATA (FBF, drd_dib.fbf, 0) },
	{ ORDATA (BPTR, drd_ptr, 6) },
	{ ORDATA (DEVNO, drd_dib.devno, 6), REG_HRO },
	{ FLDATA (*DEVENB, drd_dib.enb, 0), REG_HRO },
	{ NULL }  };

MTAB drd_mod[] = {
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &drd_dib },
	{ 0 }  };

DEVICE drd_dev = {
	"DRD", &drd_unit, drd_reg, drd_mod,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, NULL,
	NULL, NULL, NULL };

/* DRC data structures

   drc_dev	device descriptor
   drc_unit	unit descriptor
   drc_mod	unit modifiers
   drc_reg	register list
*/

UNIT drc_unit =
	{ UDATA (&drc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+
		UNIT_MUSTBUF+UNIT_DR+UNIT_BINK, DR_SIZE) };

REG drc_reg[] = {
	{ ORDATA (CW, drc_cw, 16) },
	{ ORDATA (STA, drc_sta, 16) },
	{ FLDATA (CMD, drc_dib.cmd, 0) },
	{ FLDATA (CTL, drc_dib.ctl, 0) },
	{ FLDATA (FLG, drc_dib.flg, 0) },
	{ FLDATA (FBF, drc_dib.fbf, 0) },
	{ DRDATA (TIME, dr_time, 24), REG_NZ + PV_LEFT },
	{ FLDATA (STOP_IOE, dr_stopioe, 0) },
	{ ORDATA (DEVNO, drc_dib.devno, 6), REG_HRO },
	{ FLDATA (*DEVENB, drc_dib.enb, 0), REG_HRO },
	{ NULL }  };

MTAB drc_mod[] = {
	{ UNIT_DR, 0, "disk", NULL, NULL },
	{ UNIT_DR, UNIT_DR, "drum", NULL, NULL },
	{ UNIT_DR, 184320, NULL, "180K", &dr_set_size },
	{ UNIT_DR, 368640, NULL, "360K", &dr_set_size },
	{ UNIT_DR, 737280, NULL, "720K", &dr_set_size },
	{ UNIT_DR, 368640+1, NULL, "384K", &dr_set_size },
	{ UNIT_DR, 524280+1, NULL, "512K", &dr_set_size },
	{ UNIT_DR, 655360+1, NULL, "640K", &dr_set_size },
	{ UNIT_DR, 786432+1, NULL, "768K", &dr_set_size },
	{ UNIT_DR, 917504+1, NULL, "896K", &dr_set_size },
	{ UNIT_DR, 1048576+1, NULL, "1024K", &dr_set_size },
	{ UNIT_DR, 1572864+1, NULL, "1536K", &dr_set_size },
	{ MTAB_XTD | MTAB_VDV, 1, NULL, "ENABLED",
		&set_enb, NULL, &drd_dib },
	{ MTAB_XTD | MTAB_VDV, 1, NULL, "DISABLED",
		&set_dis, NULL, &drd_dib },
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &drd_dib },
	{ 0 }  };

DEVICE drc_dev = {
	"DRC", &drc_unit, drc_reg, drc_mod,
	1, 8, 21, 1, 8, 16,
	NULL, NULL, &drc_reset,
	NULL, NULL, NULL };

/* IOT routines */

int32 drdio (int32 inst, int32 IR, int32 dat)
{
int32 devd, t;

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
	drd_obuf = dat;
	break;
case ioMIX:						/* merge */
	dat = dat | drd_ibuf;
	break;
case ioLIX:						/* load */
	dat = drd_ibuf;
	break;
case ioCTL:						/* control clear/set */
	if (IR & AB) {					/* CLC */
		clrCMD (devd);				/* clr "ctl" */
		clrFLG (devd);				/* clr flg */
		drc_sta = drc_sta & ~DRS_SAC;  }	/* clear SAC flag */
	else if (!CMD (devd)) {				/* STC, not set? */
		setCMD (devd);				/* set "ctl" */
		if (drc_cw & CW_WR) setFLG (devd);	/* prime DMA */
		drc_sta = 0;				/* clear errors */
		drd_ptr = 0;				/* clear sec ptr */
		sim_cancel (&drc_unit);			/* cancel curr op */
		t = CW_GETSEC (drc_cw) - GET_CURSEC (dr_time * DR_NUMWD);
		if (t <= 0) t = t + DR_NUMSC;
		sim_activate (&drc_unit, t * DR_NUMWD * dr_time);  }
	break;
default:
	break;  }
if (IR & HC) { clrFLG (devd); }				/* H/C option */
return dat;
}

int32 drcio (int32 inst, int32 IR, int32 dat)
{
int32 devc, st;

devc = IR & DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioSFC:						/* skip flag clear */
	PC = (PC + 1) & VAMASK;
	return dat;
case ioOTX:						/* output */
	drc_cw = dat;
	break;
case ioLIX:						/* load */
	dat = 0;
case ioMIX:						/* merge */
	if (drc_unit.flags & UNIT_ATT)
		st = GET_CURSEC (dr_time) | DRS_RDY | drc_sta |
			(sim_is_active (&drc_unit)? DRS_BSY: 0);
	else st = drc_sta;
	break;
default:
	break;  }
return dat;
}

/* Unit service */

t_stat drc_svc (UNIT *uptr)
{
int32 devc, devd, trk, sec;
uint32 da;

if ((uptr -> flags & UNIT_ATT) == 0) {
	drc_sta = DRS_ABO;
	return IORETURN (dr_stopioe, SCPE_UNATT);  }

drc_sta = drc_sta | DRS_SAC;
devc = drc_dib.devno;					/* get cch devno */
devd = drd_dib.devno;					/* get dch devno */
trk = CW_GETTRK (drc_cw);
sec = CW_GETSEC (drc_cw);
da = ((trk * DR_NUMSC) + sec) * DR_NUMWD;

if (drc_cw & CW_WR) {					/* write? */
	if ((da < uptr -> capac) && (sec < DR_NUMSC)) {
		*(((uint16 *) uptr -> filebuf) + da + drd_ptr) = drd_obuf;
		if (((t_addr) (da + drd_ptr)) >= uptr -> hwmark)
			uptr -> hwmark = da + drd_ptr + 1;  }
	drd_ptr = dr_incda (trk, sec, drd_ptr);		/* inc disk addr */
	if (CMD (devd)) {				/* dch active? */
		setFLG (devd);				/* set dch flg */
		sim_activate (uptr, dr_time);  }	/* sched next word */
	else if (drd_ptr) {				/* done, need to fill? */
		for ( ; drd_ptr < DR_NUMWD; drd_ptr++)
		 	*(((uint16 *) uptr -> filebuf) + da + drd_ptr) = 0;  }
	}						/* end write */
else {							/* read */
	if (CMD (devd)) {				/* dch active? */
		if ((da >= uptr -> capac) || (sec >= DR_NUMSC)) drd_ibuf = 0;
		else drd_ibuf = *(((uint16 *) uptr -> filebuf) + da + drd_ptr);
		drd_ptr = dr_incda (trk, sec, drd_ptr);
		setFLG (devd);				/* set dch flg */
		sim_activate (uptr, dr_time);  }	/* sched next word */
	}
return SCPE_OK;
}

/* Increment current disk address */

int32 dr_incda (int32 trk, int32 sec, int32 ptr)
{
ptr = ptr + 1;						/* inc pointer */
if (ptr >= DR_NUMWD) {					/* end sector? */
	ptr = 0;					/* new sector */
	sec = sec + 1;					/* adv sector */
	if (sec >= DR_NUMSC) {				/* end track? */
		sec = 0;				/* new track */
		trk = trk + 1;				/* adv track */
		if (trk >= MAX_TRK) trk = 0;  }		/* wraps at max */
	drc_cw = (drc_cw & CW_WR) | CW_PUTTRK (trk) | CW_PUTSEC (sec);
	}
return ptr;
}

/* Reset routine */

t_stat drc_reset (DEVICE *dptr)
{
drc_sta = drc_cw = drd_ptr = 0;
drc_dib.cmd = drd_dib.cmd = 0;				/* clear cmd */
drc_dib.ctl = drd_dib.ctl = 0;				/* clear ctl */
drc_dib.fbf = drd_dib.fbf = 0;				/* clear fbf */
drc_dib.flg = drd_dib.flg = 0;				/* clear flg */
sim_cancel (&drc_unit);
return SCPE_OK;
}

/* Set size routine */

/* Set size command validation routine */

t_stat dr_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (uptr -> flags & UNIT_ATT) return SCPE_ALATT;
if (val & 1) uptr -> flags = uptr -> flags | UNIT_DR;
else uptr -> flags = uptr -> flags & ~UNIT_DR;
uptr -> capac = val & ~1;
return SCPE_OK;
}
