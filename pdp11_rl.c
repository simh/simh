/* pdp11_rl.c: RL11 (RLV12) cartridge disk simulator

   Copyright (c) 1993-2001, Robert M Supnik

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

   rl		RL11(RLV12)/RL01/RL02 cartridge disk

   07-Sep-01	RMS	Revised device disable and interrupt mechanisms
   20-Aug-01	RMS	Added bad block option in attach
   17-Jul-01	RMS	Fixed warning from VC++ 6.0
   26-Apr-01	RMS	Added device enable/disable support
   25-Mar-01	RMS	Fixed block fill calculation
   15-Feb-01	RMS	Corrected bootstrap string
   12-Nov-97	RMS	Added bad block table command
   25-Nov-96	RMS	Default units to autosize
   29-Jun-96	RMS	Added unit disable support

   The RL11 is a four drive cartridge disk subsystem.  An RL01 drive
   consists of 256 cylinders, each with 2 surfaces containing 40 sectors
   of 256 bytes.  An RL02 drive has 512 cylinders.  The RLV12 is a
   controller variant which supports 22b direct addressing.

   The most complicated part of the RL11 controller is the way it does
   seeks.  Seeking is relative to the current disk address; this requires
   keeping accurate track of the current cylinder.  The RL01 will not
   switch heads or cross cylinders during transfers.
*/

#include "pdp11_defs.h"

/* Constants */

#define RL_NUMWD	128				/* words/sector */
#define RL_NUMSC	40				/* sectors/surface */
#define RL_NUMSF	2				/* surfaces/cylinder */
#define RL_NUMCY	256				/* cylinders/drive */
#define RL_NUMDR	4				/* drives/controller */
#define RL01_SIZE (RL_NUMCY * RL_NUMSF * RL_NUMSC * RL_NUMWD)  /* words/drive */
#define RL02_SIZE	(RL01_SIZE * 2)			/* words/drive */
#define RL_MAXMEM	((int) (MEMSIZE / sizeof (int16)))	/* words/memory */

/* Flags in the unit flags word */

#define UNIT_V_HWLK	(UNIT_V_UF)			/* hwre write lock */
#define UNIT_V_RL02	(UNIT_V_UF+1)			/* RL01 vs RL02 */
#define UNIT_V_AUTO	(UNIT_V_UF+2)			/* autosize enable */
#define UNIT_W_UF	4				/* saved flags width */
#define UNIT_V_DUMMY	(UNIT_V_UF + UNIT_W_UF)	/* dummy flag */
#define UNIT_DUMMY	(1 << UNIT_V_DUMMY)
#define UNIT_HWLK	(1u << UNIT_V_HWLK)
#define UNIT_RL02	(1u << UNIT_V_RL02)
#define UNIT_AUTO	(1u << UNIT_V_AUTO)

/* Parameters in the unit descriptor */

#define TRK		u3				/* current track */
#define STAT		u4				/* status */

/* RLDS */

#define RLDS_LOAD	0				/* no cartridge */
#define RLDS_LOCK	5				/* lock on */
#define RLDS_BHO	0000010				/* brushes home */
#define RLDS_HDO	0000020				/* heads out */
#define RLDS_CVO	0000040				/* cover open */
#define RLDS_HD		0000100				/* head select */
#define RLDS_DSE	0000400				/* drive select err */
#define RLDS_RL02	0000200				/* RL02 */
#define RLDS_VCK	0001000				/* volume check */
#define RLDS_WGE	0002000				/* write gate err */
#define RLDS_SPE	0004000				/* spin err */
#define RLDS_STO	0010000				/* seek time out */
#define RLDS_WLK	0020000				/* write locked */
#define RLDS_HCE	0040000				/* head current err */
#define RLDS_WDE	0100000				/* write data err */
#define RLDS_ATT	(RLDS_HDO+RLDS_BHO+RLDS_LOCK)	/* attached status */
#define RLDS_UNATT	(RLDS_CVO+RLDS_LOAD)		/* unattached status */
#define RLDS_ERR 	(RLDS_WDE+RLDS_HCE+RLDS_STO+RLDS_SPE+RLDS_WGE+ \
			RLDS_VCK+RLDS_DSE)		/* errors bits */

/* RLCS */

#define RLCS_DRDY	0000001				/* drive ready */
#define RLCS_M_FUNC	0000007				/* function */
#define  RLCS_NOP	0
#define  RLCS_WCHK	1
#define  RLCS_GSTA	2
#define  RLCS_SEEK	3
#define  RLCS_RHDR	4
#define  RLCS_WRITE	5
#define  RLCS_READ	6
#define  RLCS_RNOHDR	7
#define RLCS_V_FUNC	1
#define RLCS_M_MEX	03				/* memory extension */
#define RLCS_V_MEX	4
#define RLCS_MEX	(RLCS_M_MEX << RLCS_V_MEX)
#define RLCS_M_DRIVE	03
#define RLCS_V_DRIVE	8
#define RLCS_INCMP	0002000				/* incomplete */
#define RLCS_CRC	0004000				/* CRC error */
#define RLCS_HDE	0010000				/* header error */
#define RLCS_NXM	0020000				/* non-exist memory */
#define RLCS_DRE	0040000				/* drive error */
#define RLCS_ERR	0100000				/* error summary */
#define RLCS_ALLERR (RLCS_ERR+RLCS_DRE+RLCS_NXM+RLCS_HDE+RLCS_CRC+RLCS_INCMP)
#define RLCS_RW		0001776				/* read/write */
#define GET_FUNC(x)	(((x) >> RLCS_V_FUNC) & RLCS_M_FUNC)
#define GET_DRIVE(x)	(((x) >> RLCS_V_DRIVE) & RLCS_M_DRIVE)

/* RLDA */

#define RLDA_SK_DIR	0000004				/* direction */
#define RLDA_GS_CLR	0000010				/* clear errors */
#define RLDA_SK_HD	0000020				/* head select */

#define RLDA_V_SECT	0				/* sector */
#define RLDA_M_SECT	077
#define RLDA_V_TRACK	6				/* track */
#define RLDA_M_TRACK	01777
#define RLDA_HD0	(0 << RLDA_V_TRACK)
#define RLDA_HD1	(1u << RLDA_V_TRACK)
#define RLDA_V_CYL	7				/* cylinder */
#define RLDA_M_CYL	0777
#define RLDA_TRACK	(RLDA_M_TRACK << RLDA_V_TRACK)
#define RLDA_CYL	(RLDA_M_CYL << RLDA_V_CYL)
#define GET_SECT(x)	(((x) >> RLDA_V_SECT) & RLDA_M_SECT)
#define GET_CYL(x)	(((x) >> RLDA_V_CYL) & RLDA_M_CYL)
#define GET_TRACK(x)	(((x) >> RLDA_V_TRACK) & RLDA_M_TRACK)
#define GET_DA(x)	((GET_TRACK (x) * RL_NUMSC) + GET_SECT (x))

/* RLBA */

#define RLBA_IMP	0177776				/* implemented */

/* RLBAE */

#define RLBAE_IMP	0000077				/* implemented */

extern uint16 *M;					/* memory */
extern int32 int_req[IPL_HLVL];
extern UNIT cpu_unit;
int32 rlcs = 0;						/* control/status */
int32 rlba = 0;						/* memory address */
int32 rlbae = 0;					/* mem addr extension */
int32 rlda = 0;						/* disk addr */
int32 rlmp = 0, rlmp1 = 0, rlmp2 = 0;			/* mp register queue */
int32 rl_swait = 10;					/* seek wait */
int32 rl_rwait = 10;					/* rotate wait */
int32 rl_stopioe = 1;					/* stop on error */
int32 rl_enb = 1;					/* device enable */
t_stat rl_svc (UNIT *uptr);
t_stat rl_reset (DEVICE *dptr);
void rl_set_done (int32 error);
t_stat rl_boot (int32 unitno);
t_stat rl_attach (UNIT *uptr, char *cptr);
t_stat rl_set_size (UNIT *uptr, int32 value);
t_stat rl_set_bad (UNIT *uptr, int32 value);
extern t_stat pdp11_bad_block (UNIT *uptr, int32 sec, int32 wds);

/* RL11 data structures

   rl_dev	RL device descriptor
   rl_unit	RL unit list
   rl_reg	RL register list
   rl_mod	RL modifier list
*/

UNIT rl_unit[] = {
	{ UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO,
		RL01_SIZE) },
	{ UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO,
		RL01_SIZE) },
	{ UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO,
		RL01_SIZE) },
	{ UDATA (&rl_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO,
		RL01_SIZE) } };

REG rl_reg[] = {
	{ ORDATA (RLCS, rlcs, 16) },
	{ ORDATA (RLDA, rlda, 16) },
	{ ORDATA (RLBA, rlba, 16) },
	{ ORDATA (RLBAE, rlbae, 6) },
	{ ORDATA (RLMP, rlmp, 16) },
	{ ORDATA (RLMP1, rlmp1, 16) },
	{ ORDATA (RLMP2, rlmp2, 16) },
	{ FLDATA (INT, IREQ (RL), INT_V_RL) },
	{ FLDATA (ERR, rlcs, CSR_V_ERR) },
	{ FLDATA (DONE, rlcs, CSR_V_DONE) },
	{ FLDATA (IE, rlcs, CSR_V_IE) },
	{ DRDATA (STIME, rl_swait, 24), PV_LEFT },
	{ DRDATA (RTIME, rl_rwait, 24), PV_LEFT },
	{ GRDATA (FLG0, rl_unit[0].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG1, rl_unit[1].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG2, rl_unit[2].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG3, rl_unit[3].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ DRDATA (CAPAC0, rl_unit[0].capac, 32), PV_LEFT + REG_HRO },
	{ DRDATA (CAPAC1, rl_unit[1].capac, 32), PV_LEFT + REG_HRO },
	{ DRDATA (CAPAC2, rl_unit[2].capac, 32), PV_LEFT + REG_HRO },
	{ DRDATA (CAPAC3, rl_unit[3].capac, 32), PV_LEFT + REG_HRO },
	{ FLDATA (STOP_IOE, rl_stopioe, 0) },
	{ FLDATA (*DEVENB, rl_enb, 0), REG_HRO },
	{ NULL }  };

MTAB rl_mod[] = {
	{ UNIT_HWLK, 0, "write enabled", "ENABLED", NULL },
	{ UNIT_HWLK, UNIT_HWLK, "write locked", "LOCKED", NULL },
	{ UNIT_DUMMY, 0, NULL, "BADBLOCK", &rl_set_bad },
	{ (UNIT_RL02+UNIT_ATT), UNIT_ATT, "RL01", NULL, NULL },
	{ (UNIT_RL02+UNIT_ATT), (UNIT_RL02+UNIT_ATT), "RL02", NULL, NULL },
	{ (UNIT_AUTO+UNIT_RL02+UNIT_ATT), 0, "RL01", NULL, NULL },
	{ (UNIT_AUTO+UNIT_RL02+UNIT_ATT), UNIT_RL02, "RL02", NULL, NULL },
	{ (UNIT_AUTO+UNIT_ATT), UNIT_AUTO, "autosize", NULL, NULL },
	{ UNIT_AUTO, UNIT_AUTO, NULL, "AUTOSIZE", NULL },
	{ (UNIT_AUTO+UNIT_RL02), 0, NULL, "RL01", &rl_set_size },
	{ (UNIT_AUTO+UNIT_RL02), UNIT_RL02, NULL, "RL02", &rl_set_size },
	{ 0 }  };

DEVICE rl_dev = {
	"RL", rl_unit, rl_reg, rl_mod,
	RL_NUMDR, 8, 24, 1, 8, 16,
	NULL, NULL, &rl_reset,
	&rl_boot, &rl_attach, NULL };

/* I/O dispatch routine, I/O addresses 17774400 - 17774407

   17774400	RLCS	read/write
   17774402	RLBA	read/write
   17774404	RLDA	read/write
   17774406	RLMP	read/write
   17774410	RLBAE	read/write
*/

t_stat rl_rd (int32 *data, int32 PA, int32 access)
{
UNIT *uptr;

switch ((PA >> 1) & 07) {				/* decode PA<2:1> */
case 0:							/* RLCS */
	rlcs = (rlcs & ~RLCS_MEX) | ((rlbae & RLCS_M_MEX) << RLCS_V_MEX);
	if (rlcs & RLCS_ALLERR) rlcs = rlcs | RLCS_ERR;
	uptr = rl_dev.units + GET_DRIVE (rlcs);
	if (sim_is_active (uptr)) rlcs = rlcs & ~RLCS_DRDY;
	else rlcs = rlcs | RLCS_DRDY;			/* see if ready */
	*data = rlcs;
	break;
case 1:							/* RLBA */
	*data = rlba & RLBA_IMP;
	break;
case 2:							/* RLDA */
	*data = rlda;
	break;
case 3:							/* RLMP */
	*data = rlmp;
	rlmp = rlmp1;					/* ripple data */
	rlmp1 = rlmp2;
	break;
case 4:							/* RLBAE */
	*data = rlbae & RLBAE_IMP;
	break;  }					/* end switch */
return SCPE_OK;
}

t_stat rl_wr (int32 data, int32 PA, int32 access)
{
int32 curr, offs, newc, maxc;
UNIT *uptr;

switch ((PA >> 1) & 07) {				/* decode PA<2:1> */
case 0:							/* RLCS */
	rlcs = (rlcs & ~RLCS_MEX) | ((rlbae & RLCS_M_MEX) << RLCS_V_MEX);
	if (rlcs & RLCS_ALLERR) rlcs = rlcs | RLCS_ERR;
	uptr = rl_dev.units + GET_DRIVE (data);		/* get new drive */
	if (sim_is_active (uptr)) rlcs = rlcs & ~RLCS_DRDY;
	else rlcs = rlcs | RLCS_DRDY;			/* see if ready */

	if (access == WRITEB) data = (PA & 1)?
		(rlcs & 0377) | (data << 8): (rlcs & ~0377) | data;
	rlcs = (rlcs & ~RLCS_RW) | (data & RLCS_RW);
	rlbae = (rlbae & ~RLCS_M_MEX) | ((rlcs >> RLCS_V_MEX) & RLCS_M_MEX);
	if (data & CSR_DONE) {				/* ready set? */
		if ((data & CSR_IE) == 0) CLR_INT (RL);
		else if ((rlcs & (CSR_DONE + CSR_IE)) == CSR_DONE)
			SET_INT (RL);	
		return SCPE_OK;  }

	CLR_INT (RL);					/* clear interrupt */
	rlcs = rlcs & ~RLCS_ALLERR;			/* clear errors */
	switch (GET_FUNC (rlcs)) {			/* case on RLCS<3:1> */
	case RLCS_NOP:					/* nop */
		rl_set_done (0);
		break;
	case RLCS_SEEK:					/* seek */
		curr = GET_CYL (uptr -> TRK);		/* current cylinder */
		offs = GET_CYL (rlda);			/* offset */
		if (rlda & RLDA_SK_DIR) {		/* in or out? */
			newc = curr + offs;		/* out */
			maxc = (uptr -> flags & UNIT_RL02)?
				RL_NUMCY * 2: RL_NUMCY;
			if (newc >= maxc) newc = maxc - 1;  }
		else {	newc = curr - offs;		/* in */
			if (newc < 0) newc = 0;  }
		uptr -> TRK = (newc << RLDA_V_CYL) |	/* put on track */
			((rlda & RLDA_SK_HD)? RLDA_HD1: RLDA_HD0);
		sim_activate (uptr, rl_swait * abs (newc - curr));
		break;
	default:					/* data transfer */
		sim_activate (uptr, rl_swait);		/* activate unit */
		break;  }				/* end switch func */
	break;						/* end case RLCS */

case 1:							/* RLBA */
	if (access == WRITEB) data = (PA & 1)?
		(rlba & 0377) | (data << 8): (rlba & ~0377) | data;
	rlba = data & RLBA_IMP;
	break;
case 2:							/* RLDA */
	if (access == WRITEB) data = (PA & 1)?
		(rlda & 0377) | (data << 8): (rlda & ~0377) | data;
	rlda = data;
	break;
case 3:							/* RLMP */
	if (access == WRITEB) data = (PA & 1)?
		(rlmp & 0377) | (data << 8): (rlmp & ~0377) | data;
	rlmp = rlmp1 = rlmp2 = data;
	break;
case 4:							/* RLBAE */
	if (PA & 1) return SCPE_OK;
	rlbae = data & RLBAE_IMP;
	rlcs = (rlcs & ~RLCS_MEX) | ((rlbae & RLCS_M_MEX) << RLCS_V_MEX);
	break;  }					/* end switch */
return SCPE_OK;
}

/* Service unit timeout

   If seek in progress, complete seek command
   Else complete data transfer command

   The unit control block contains the function and cylinder for
   the current command.
*/

t_stat rl_svc (UNIT *uptr)
{
int32 comp, err, awc, wc, maxwc;
int32 func, pa, da, remc;
static uint16 fill[RL_NUMWD] = { 0 };

func = GET_FUNC (rlcs);					/* get function */
if (func == RLCS_GSTA) {				/* get status */
	rlmp = uptr -> STAT | (uptr -> TRK & RLDS_HD) |
		((uptr -> flags & UNIT_ATT)? RLDS_ATT: RLDS_UNATT);
	if (rlda & RLDA_GS_CLR) rlmp = rlmp & ~RLDS_ERR;
	if (uptr -> flags & UNIT_RL02) rlmp = rlmp | RLDS_RL02;
	if (uptr -> flags & UNIT_HWLK) rlmp = rlmp | RLDS_WLK;
	uptr -> STAT = rlmp2 = rlmp1 = rlmp;
	rl_set_done (0);				/* done */
	return SCPE_OK;  }

if ((uptr -> flags & UNIT_ATT) == 0) {			/* attached? */
	rlcs = rlcs & ~RLCS_DRDY;			/* clear drive ready */
	rl_set_done (RLCS_ERR | RLCS_INCMP);		/* flag error */
	return IORETURN (rl_stopioe, SCPE_UNATT);  }

if ((func == RLCS_WRITE) && (uptr -> flags & UNIT_HWLK)) {
	uptr -> STAT = uptr -> STAT | RLDS_WGE;		/* write and locked */
	rl_set_done (RLCS_ERR | RLCS_DRE);
	return SCPE_OK;  }

if (func == RLCS_SEEK) {				/* seek? */
	rl_set_done (0);				/* done */
	return SCPE_OK;  }

if (func == RLCS_RHDR) {				/* read header? */
	rlmp = (uptr -> TRK & RLDA_TRACK) | GET_SECT (rlda);
	rlmp1 = 0;
	rl_set_done (0);				/* done */
	return SCPE_OK;  }

if (((func != RLCS_RNOHDR) && ((uptr -> TRK & RLDA_CYL) != (rlda & RLDA_CYL)))
   || (GET_SECT (rlda) >= RL_NUMSC)) {			/* bad cyl or sector? */
	rl_set_done (RLCS_ERR | RLCS_HDE | RLCS_INCMP);	/* wrong cylinder? */
	return SCPE_OK;  }
	
pa = ((rlbae << 16) | rlba) >> 1;			/* form phys addr */
da = GET_DA (rlda) * RL_NUMWD;				/* get disk addr */
wc = 0200000 - rlmp;					/* get true wc */

maxwc = (RL_NUMSC - GET_SECT (rlda)) * RL_NUMWD;	/* max transfer */
if (wc > maxwc) wc = maxwc;				/* track overrun? */
if ((pa + wc) > RL_MAXMEM) {				/* mem overrun? */
	rlcs = rlcs | RLCS_ERR | RLCS_NXM;
	wc = (RL_MAXMEM - pa);  }
if (wc < 0) {						/* abort transfer? */
	rl_set_done (RLCS_INCMP);
	return SCPE_OK;  }

err = fseek (uptr -> fileref, da * sizeof (int16), SEEK_SET);
if ((func >= RLCS_READ) && (err == 0)) {		/* read (no hdr)? */
	awc = fxread (&M[pa], sizeof (int16), wc, uptr -> fileref);
	for ( ; awc < wc; awc++) M[pa + awc] = 0;
	err = ferror (uptr -> fileref);  }

if ((func == RLCS_WRITE) && (err == 0)) {		/* write? */
	fxwrite (&M[pa], sizeof (int16), wc, uptr -> fileref);
	err = ferror (uptr -> fileref);
	if ((err == 0) && (remc = (wc & (RL_NUMWD - 1)))) {
		fxwrite (fill, sizeof (int16), RL_NUMWD - remc, uptr -> fileref);
		err = ferror (uptr -> fileref);  }  }

if ((func == RLCS_WCHK) && (err == 0)) {		/* write check? */
	remc = wc;					/* xfer length */
	for (wc = 0; (err == 0) && (wc < remc); wc++)  {
		awc = fxread (&comp, sizeof (int16), 1, uptr -> fileref);
		if (awc == 0) comp = 0;
		if (comp != M[pa + wc]) rlcs = rlcs | RLCS_ERR | RLCS_CRC;  }
	err = ferror (uptr -> fileref);  }

rlmp = (rlmp + wc) & 0177777;				/* final word count */
if (rlmp != 0) rlcs = rlcs | RLCS_ERR | RLCS_INCMP;	/* completed? */
pa = (pa + wc) << 1;					/* final byte addr */
rlbae = (pa >> 16) & RLBAE_IMP;				/* upper 6b */
rlba = pa & RLBA_IMP;					/* lower 16b */
rlcs = (rlcs & ~RLCS_MEX) | ((rlbae & RLCS_M_MEX) << RLCS_V_MEX);
rlda = rlda + ((wc + (RL_NUMWD - 1)) / RL_NUMWD);
rl_set_done (0);

if (err != 0) {						/* error? */
	perror ("RL I/O error");
	clearerr (uptr -> fileref);
	return SCPE_IOERR;  }
return SCPE_OK;
}

/* Set done and possibly errors */

void rl_set_done (int32 status)
{
	rlcs = rlcs | status | CSR_DONE;		/* set done */
	if (rlcs & CSR_IE) SET_INT (RL);
	else CLR_INT (RL);
	return;
}

/* Device reset

   Note that the RL11 does NOT recalibrate its drives on RESET
*/

t_stat rl_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

rlcs = CSR_DONE;
rlda = rlba = rlbae = rlmp = rlmp1 = rlmp2 = 0;
CLR_INT (RL);
for (i = 0; i < RL_NUMDR; i++) {
	uptr = rl_dev.units + i;
	sim_cancel (uptr);
	uptr -> STAT = 0;  }
return SCPE_OK;
}

/* Attach routine */

t_stat rl_attach (UNIT *uptr, char *cptr)
{
int32 p;
t_stat r;

uptr -> capac = (uptr -> flags & UNIT_RL02)? RL02_SIZE: RL01_SIZE;
r = attach_unit (uptr, cptr);
if ((r != SCPE_OK) || ((uptr -> flags & UNIT_AUTO) == 0)) return r;
if (fseek (uptr -> fileref, 0, SEEK_END)) return SCPE_OK;
if ((p = ftell (uptr -> fileref)) == 0)
	return pdp11_bad_block (uptr, RL_NUMSC, RL_NUMWD);
if (p > (RL01_SIZE * sizeof (int16))) {
	uptr -> flags = uptr -> flags | UNIT_RL02;
	uptr -> capac = RL02_SIZE;  }
else {	uptr -> flags = uptr -> flags & ~UNIT_RL02;
	uptr -> capac = RL01_SIZE;  }
return SCPE_OK;
}

/* Set size routine */

t_stat rl_set_size (UNIT *uptr, int32 value)
{
if (uptr -> flags & UNIT_ATT) return SCPE_ALATT;
uptr -> capac = (value & UNIT_RL02)? RL02_SIZE: RL01_SIZE;
return SCPE_OK;
}

/* Set bad block routine */

t_stat rl_set_bad (UNIT *uptr, int32 value)
{
return pdp11_bad_block (uptr, RL_NUMSC, RL_NUMWD);
}

/* Device bootstrap */

#define BOOT_START 02000				/* start */
#define BOOT_UNIT 02006					/* unit number */
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int32))

static const int32 boot_rom[] = {
	0012706, 0002000,		/* MOV #2000, SP */
	0012700, 0000000,		/* MOV #UNIT, R0 */
	0010003,			/* MOV R0, R3 */
	0000303,			/* SWAB R3 */
	0012701, 0174400,		/* MOV #RLCS, R1 	; csr */
	0012761, 0000013, 0000004,	/* MOV #13, 4(R1)	; clr err */
	0052703, 0000004,		/* BIS #4, R3		; unit+gstat */
	0010311,			/* MOV R3, (R1)		; issue cmd */
	0105711,			/* TSTB (R1)		; wait */
	0100376,			/* BPL .-2 */
	0105003,			/* CLRB R3 */
	0052703, 0000010,		/* BIS #10, R3		; unit+rdhdr */
	0010311,			/* MOV R3, (R1)		; issue cmd */
	0105711,			/* TSTB (R1)		; wait */
	0100376,			/* BPL .-2 */
	0016102, 0000006,		/* MOV 6(R1), R2	; get hdr */
	0042702, 0000077,		/* BIC #77, R2		; clr sector */
	0005202,			/* INC R2		; magic bit */
	0010261, 0000004,		/* MOV R2, 4(R1)	; seek to 0 */
	0105003,			/* CLRB R3 */
	0052703, 0000006,		/* BIS #6, R3		; unit+seek */
	0010311,			/* MOV R3, (R1)		; issue cmd */
	0105711,			/* TSTB (R1)		; wait */
	0100376,			/* BPL .-2 */
	0005061, 0000002,		/* CLR 2(R1)		; clr ba */
	0005061, 0000004,		/* CLR 4(R1)		; clr da */
	0012761, 0177000, 0000006,	/* MOV #-512., 6(R1)	; set wc */
	0105003,			/* CLRB R3 */
	0052703, 0000014,		/* BIS #14, R3		; unit+read */
	0010311,			/* MOV R3, (R1)		; issue cmd */
	0105711,			/* TSTB (R1)		; wait */
	0100376,			/* BPL .-2 */
	0042711, 0000377,		/* BIC #377, (R1) */
	0005002,			/* CLR R2 */
	0005003,			/* CLR R3 */
	0005004,			/* CLR R4 */
	0012705, 0046104,		/* MOV "DL, R5 */
	0005007				/* CLR PC */
};

t_stat rl_boot (int32 unitno)
{
int32 i;
extern int32 saved_PC;

for (i = 0; i < BOOT_LEN; i++) M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & RLCS_M_DRIVE;
saved_PC = BOOT_START;
return SCPE_OK;
}
