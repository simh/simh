/* hp2100_dr.c: HP 2100 12606B/12610B fixed head disk/drum simulator

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

   fhd		12606B 2770/2771 fixed head disk
		12610B 2773/2774/2775 drum

   These head-per-track devices are buffered in memory, to minimize overhead.

   The drum data channel does not have a command flip-flop.  Its control
   flip-flop is not wired into the interrupt chain; accordingly, the
   simulator uses command rather than control for the data channel.  Its
   flag does not respond to SFS, SFC, or STF.
   
   The drum control channel does not have any of the traditional flip-flops.

   27-Jul-03	RMS	Fixed drum sizes
			Fixed variable capacity interaction with SAVE/RESTORE
   10-Nov-02	RMS	Added BOOT command
*/

#include "hp2100_defs.h"
#include <math.h>

/* Constants */

#define DR_NUMWD	64				/* words/sector */
#define DR_FNUMSC	90				/* fhd sec/track */
#define DR_DNUMSC	32				/* drum sec/track */
#define DR_NUMSC	((drc_unit.flags & UNIT_DR)? DR_DNUMSC: DR_FNUMSC)
#define DR_SIZE		(512 * DR_DNUMSC * DR_NUMWD)	/* initial size */
#define UNIT_V_SZ	(UNIT_V_UF)			/* disk vs drum */
#define UNIT_M_SZ	017				/* size */
#define UNIT_SZ		(UNIT_M_SZ << UNIT_V_SZ)
#define UNIT_DR		(1 << UNIT_V_SZ)		/* low order bit */
#define  SZ_180K	000				/* disks */
#define  SZ_360K	002
#define  SZ_720K	004
#define  SZ_1024K	001				/* drums: default size */
#define  SZ_1536K	003
#define  SZ_384K	005
#define  SZ_512K	007
#define  SZ_640K	011
#define  SZ_768K	013
#define  SZ_896K	015
#define DR_GETSZ(x)	(((x) >> UNIT_V_SZ) & UNIT_M_SZ)

/* Command word */

#define CW_WR		0100000				/* write vs read */
#define CW_V_FTRK	7				/* fhd track */
#define CW_M_FTRK	0177
#define CW_V_DTRK	5				/* drum track */
#define CW_M_DTRK	01777
#define MAX_TRK		(((drc_unit.flags & UNIT_DR)? CW_M_DTRK: CW_M_FTRK) + 1)
#define CW_GETTRK(x)	((drc_unit.flags & UNIT_DR)? \
				(((x) >> CW_V_DTRK) & CW_M_DTRK): \
				(((x) >> CW_V_FTRK) & CW_M_FTRK))
#define CW_PUTTRK(x)	((drc_unit.flags & UNIT_DR)? \
				(((x) & CW_M_DTRK) << CW_V_DTRK): \
				(((x) & CW_M_FTRK) << CW_V_FTRK))
#define CW_V_FSEC	0				/* fhd sector */
#define CW_M_FSEC	0177
#define CW_V_DSEC	0				/* drum sector */
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
extern uint16 *M;
extern uint32 PC;
extern uint32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];

int32 drc_cw = 0;					/* fnc, addr */
int32 drc_sta = 0;					/* status */
int32 drd_ibuf = 0;					/* input buffer */
int32 drd_obuf = 0;					/* output buffer */
int32 drd_ptr = 0;					/* sector pointer */
int32 dr_stopioe = 1;					/* stop on error */
int32 dr_time = 10;					/* time per word */

static int32 sz_tab[16] = {
 184320, 1048576, 368640, 1572864, 737280, 393216, 0, 524288,
 0, 655360, 0, 786432,  0, 917504, 0, 0 };

DEVICE drd_dev, drc_dev;
int32 drdio (int32 inst, int32 IR, int32 dat);
int32 drcio (int32 inst, int32 IR, int32 dat);
t_stat drc_svc (UNIT *uptr);
t_stat drc_reset (DEVICE *dptr);
t_stat drc_attach (UNIT *uptr, char *cptr);
t_stat drc_boot (int32 unitno, DEVICE *dptr);
int32 dr_incda (int32 trk, int32 sec, int32 ptr);
t_stat dr_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);

/* DRD data structures

   drd_dev	device descriptor
   drd_unit	unit descriptor
   drd_reg	register list
*/

DIB dr_dib[] = {
	{ DRD, 0, 0, 0, 0, &drdio },
	{ DRC, 0, 0, 0, 0, &drcio }  };

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
	{ NULL }  };

MTAB drd_mod[] = {
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &drd_dev },
	{ 0 }  };

DEVICE drd_dev = {
	"DRD", &drd_unit, drd_reg, drd_mod,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, NULL,
	NULL, NULL, NULL,
	&drd_dib, DEV_DISABLE };

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
	{ DRDATA (CAPAC, drc_unit.capac, 24), REG_HRO },
	{ NULL }  };

MTAB drc_mod[] = {
	{ UNIT_DR, 0, "disk", NULL, NULL },
	{ UNIT_DR, UNIT_DR, "drum", NULL, NULL },
	{ UNIT_SZ, (SZ_180K << UNIT_V_SZ), NULL, "180K", &dr_set_size },
	{ UNIT_SZ, (SZ_360K << UNIT_V_SZ), NULL, "360K", &dr_set_size },
	{ UNIT_SZ, (SZ_720K << UNIT_V_SZ), NULL, "720K", &dr_set_size },
	{ UNIT_SZ, (SZ_384K << UNIT_V_SZ), NULL, "384K", &dr_set_size },
	{ UNIT_SZ, (SZ_512K << UNIT_V_SZ), NULL, "512K", &dr_set_size },
	{ UNIT_SZ, (SZ_640K << UNIT_V_SZ), NULL, "640K", &dr_set_size },
	{ UNIT_SZ, (SZ_768K << UNIT_V_SZ), NULL, "768K", &dr_set_size },
	{ UNIT_SZ, (SZ_896K << UNIT_V_SZ), NULL, "896K", &dr_set_size },
	{ UNIT_SZ, (SZ_1024K << UNIT_V_SZ), NULL, "1024K", &dr_set_size },
	{ UNIT_SZ, (SZ_1536K << UNIT_V_SZ), NULL, "1536K", &dr_set_size },
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &drd_dev },
	{ 0 }  };

DEVICE drc_dev = {
	"DRC", &drc_unit, drc_reg, drc_mod,
	1, 8, 21, 1, 8, 16,
	NULL, NULL, &drc_reset,
	&drc_boot, &drc_attach, NULL,
	&drc_dib, DEV_DISABLE };

/* IOT routines */

int32 drdio (int32 inst, int32 IR, int32 dat)
{
int32 devd, t;

devd = IR & I_DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
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
	if (IR & I_AB) {				/* CLC */
	    clrCMD (devd);				/* clr "ctl" */
	    clrFLG (devd);				/* clr flg */
	    drc_sta = drc_sta & ~DRS_SAC;  }		/* clear SAC flag */
	else if (!CMD (devd)) {				/* STC, not set? */
	    setCMD (devd);				/* set "ctl" */
	    if (drc_cw & CW_WR) { setFLG (devd); }	/* prime DMA */
	    drc_sta = 0;				/* clear errors */
	    drd_ptr = 0;				/* clear sec ptr */
	    sim_cancel (&drc_unit);			/* cancel curr op */
	    t = CW_GETSEC (drc_cw) - GET_CURSEC (dr_time * DR_NUMWD);
	    if (t <= 0) t = t + DR_NUMSC;
	    sim_activate (&drc_unit, t * DR_NUMWD * dr_time);  }
	break;
default:
	break;  }
if (IR & I_HC) { clrFLG (devd); }			/* H/C option */
return dat;
}

int32 drcio (int32 inst, int32 IR, int32 dat)
{
int32 st;

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
	if (drc_unit.flags & UNIT_ATT)			/* attached? */
	    st = GET_CURSEC (dr_time) | DRS_RDY | drc_sta |
		(sim_is_active (&drc_unit)? DRS_BSY: 0);
	else st = drc_sta;
	dat = dat | st;					/* merge status */
	break;
default:
	break;  }
return dat;
}

/* Unit service */

t_stat drc_svc (UNIT *uptr)
{
int32 devd, trk, sec;
uint32 da;
uint16 *bptr = uptr->filebuf;

if ((uptr->flags & UNIT_ATT) == 0) {
	drc_sta = DRS_ABO;
	return IORETURN (dr_stopioe, SCPE_UNATT);  }

drc_sta = drc_sta | DRS_SAC;
devd = drd_dib.devno;					/* get dch devno */
trk = CW_GETTRK (drc_cw);
sec = CW_GETSEC (drc_cw);
da = ((trk * DR_NUMSC) + sec) * DR_NUMWD;

if (drc_cw & CW_WR) {					/* write? */
	if ((da < uptr->capac) && (sec < DR_NUMSC)) {
	    bptr[da + drd_ptr] = drd_obuf;
	    if (((uint32) (da + drd_ptr)) >= uptr->hwmark)
		uptr->hwmark = da + drd_ptr + 1;  }
	drd_ptr = dr_incda (trk, sec, drd_ptr);		/* inc disk addr */
	if (CMD (devd)) {				/* dch active? */
	    setFLG (devd);				/* set dch flg */
	    sim_activate (uptr, dr_time);  }		/* sched next word */
	else if (drd_ptr) {				/* done, need to fill? */
	    for ( ; drd_ptr < DR_NUMWD; drd_ptr++)
		 bptr[da + drd_ptr] = 0;  }
	}						/* end write */
else {							/* read */
	if (CMD (devd)) {				/* dch active? */
	    if ((da >= uptr->capac) || (sec >= DR_NUMSC)) drd_ibuf = 0;
	    else drd_ibuf = bptr[da + drd_ptr];
	    drd_ptr = dr_incda (trk, sec, drd_ptr);
	    setFLG (devd);				/* set dch flg */
	    sim_activate (uptr, dr_time);  }		/* sched next word */
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
	    sec = 0;					/* new track */
	    trk = trk + 1;				/* adv track */
	    if (trk >= MAX_TRK) trk = 0;  }		/* wraps at max */
	drc_cw = (drc_cw & CW_WR) | CW_PUTTRK (trk) | CW_PUTSEC (sec);
	}
return ptr;
}

/* Reset routine */

t_stat drc_reset (DEVICE *dptr)
{
hp_enbdis_pair (&drc_dev, &drd_dev);			/* make pair cons */
drc_sta = drc_cw = drd_ptr = 0;
drc_dib.cmd = drd_dib.cmd = 0;				/* clear cmd */
drc_dib.ctl = drd_dib.ctl = 0;				/* clear ctl */
drc_dib.fbf = drd_dib.fbf = 0;				/* clear fbf */
drc_dib.flg = drd_dib.flg = 0;				/* clear flg */
sim_cancel (&drc_unit);
return SCPE_OK;
}

/* Attach routine */

t_stat drc_attach (UNIT *uptr, char *cptr)
{
int32 sz = sz_tab[DR_GETSZ (uptr->flags)];

if (sz == 0) return SCPE_IERR;
uptr->capac = sz;
return attach_unit (uptr, cptr);
}

/* Set size routine */

t_stat dr_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 sz;

if (val < 0) return SCPE_IERR;
if ((sz = sz_tab[DR_GETSZ (val)]) == 0) return SCPE_IERR;
if (uptr->flags & UNIT_ATT) return SCPE_ALATT;
uptr->capac = sz;
return SCPE_OK;
}

/* Fixed head disk/drum bootstrap routine (disc subset of disc/paper tape loader) */

#define CHANGE_DEV	(1 << 24)
#define BOOT_BASE	056
#define BOOT_START	060

static const int32 dboot[IBL_LNT - BOOT_BASE] = {
	0020000+CHANGE_DEV,	/*DMA 20000+DC */
	0000000,		/*    0 */
	0107700,		/*    CLC 0,C */
	0063756,		/*    LDA DMA		; DMA ctrl */
	0102606,		/*    OTA 6 */
	0002700,		/*    CLA,CCE */
	0102601+CHANGE_DEV,	/*    OTA CC		; trk = sec = 0 */
	0001500,		/*    ERA		; A = 100000 */
	0102602,		/*    OTA 2		; DMA in, addr */
	0063777,		/*    LDA M64 */
	0102702,		/*    STC 2 */
	0102602,		/*    OTA 2		; DMA wc = -64 */
	0103706,		/*    STC 6,C		; start DMA */
	0067776,		/*    LDB JSF		; get JMP . */
	0074077,		/*    STB 77		; in base page */
	0102700+CHANGE_DEV,	/*    STC DC		; start disc */
	0024077,		/*JSF JMP 77		; go wait */
	0177700 };		/*M64 -100 */

t_stat drc_boot (int32 unitno, DEVICE *dptr)
{
int32 i, dev, ad;

if (unitno != 0) return SCPE_NOFNC;			/* only unit 0 */
dev = drd_dib.devno;					/* get data chan dev */
ad = ((MEMSIZE - 1) & ~IBL_MASK) & VAMASK;		/* start at mem top */
for (i = 0; i < (IBL_LNT - BOOT_BASE); i++) {		/* copy bootstrap */
	if (dboot[i] & CHANGE_DEV)			/* IO instr? */
	    M[ad + BOOT_BASE + i] = (dboot[i] + dev) & DMASK;
	else M[ad + BOOT_BASE + i] = dboot[i];  }
PC = ad + BOOT_START;	
return SCPE_OK;
}
