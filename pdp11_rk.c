/* RK11 cartridge disk simulator

   Copyright (c) 1993-1999, Robert M Supnik

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

   29-Jun-96	RMS	Added unit disable support.

   The RK11 is an eight drive cartridge disk subsystem.  An RK05 drive
   consists of 203 cylinders, each with 2 surfaces containing 12 sectors
   of 512 bytes.

   The most complicated part of the RK11 controller is the concept of
   interrupt "polling".  While only one read or write can occur at a
   time, the controller supports multiple seeks.  When a seek completes,
   if done is set the drive attempts to interrupt.  If an interrupt is
   already pending, the interrupt is "queued" until it can be processed.
   When an interrupt occurs, RKDS<15:13> is loaded with the number of the
   interrupting drive.

   To implement this structure, and to assure that read/write interrupts
   take priority over seek interrupts, the controller contains an
   interrupt queue, rkintq, with a bit for a controller interrupt and
   then one for each drive.  In addition, the drive number of the last
   non-seeking drive is recorded in last_drv.
*/

#include "pdp11_defs.h"

/* Constants */

#define RK_NUMWD	256				/* words/sector */
#define RK_NUMSC	12				/* sectors/surface */
#define RK_NUMSF	2				/* surfaces/cylinder */
#define RK_NUMCY	203				/* cylinders/drive */
#define RK_NUMTR	(RK_NUMCY * RK_NUMSF)		/* tracks/drive */
#define RK_NUMDR	8				/* drives/controller */
#define RK_M_NUMDR	07
#define RK_SIZE (RK_NUMCY * RK_NUMSF * RK_NUMSC * RK_NUMWD)  /* words/drive */
#define RK_MAXMEM	((int32) (MEMSIZE / sizeof (int16)))	/* words/memory */
#define RK_CTLI		1				/* controller int */
#define RK_SCPI(x)	(2u << (x))			/* drive int */

/* Flags in the unit flags word */

#define UNIT_V_HWLK	(UNIT_V_UF + 0)			/* hwre write lock */
#define UNIT_V_SWLK	(UNIT_V_UF + 1)			/* swre write lock */
#define UNIT_W_UF	3				/* user flags width */
#define UNIT_HWLK	(1u << UNIT_V_HWLK)
#define UNIT_SWLK	(1u << UNIT_V_SWLK)

/* Parameters in the unit descriptor */

#define CYL		u3				/* current cylinder */
#define FUNC		u4				/* function */

/* RKDS */

#define RKDS_SC		0000017				/* sector counter */
#define RKDS_ON_SC	0000020				/* on sector */
#define RKDS_WLK	0000040				/* write locked */
#define RKDS_RWS	0000100				/* rd/wr/seek ready */
#define RKDS_RDY	0000200				/* drive ready */
#define RKDS_SC_OK	0000400				/* SC valid */
#define RKDS_INC	0001000				/* seek incomplete */
#define RKDS_UNSAFE	0002000				/* unsafe */
#define RKDS_RK05	0004000				/* RK05 */
#define RKDS_PWR	0010000				/* power low */
#define RKDS_ID		0160000				/* drive ID */
#define RKDS_V_ID	13

/* RKER */

#define RKER_WCE	0000001				/* write check */
#define RKER_CSE	0000002				/* checksum */
#define RKER_NXS	0000040				/* nx sector */
#define RKER_NXC	0000100				/* nx cylinder */
#define RKER_NXD	0000200				/* nx drive */
#define RKER_TE		0000400				/* timing error */
#define RKER_DLT	0001000				/* data late */
#define RKER_NXM	0002000				/* nx memory */
#define RKER_PGE	0004000				/* programming error */
#define RKER_SKE	0010000				/* seek error */
#define RKER_WLK	0020000				/* write lock */
#define RKER_OVR	0040000				/* overrun */
#define RKER_DRE	0100000				/* drive error */
#define RKER_IMP	0177743				/* implemented */
#define RKER_SOFT	(RKER_WCE+RKER_CSE)		/* soft errors */
#define RKER_HARD	0177740				/* hard errors */

/* RKCS */

#define RKCS_M_FUNC	0000007				/* function */
#define  RKCS_CTLRESET	0
#define  RKCS_WRITE	1
#define  RKCS_READ	2
#define  RKCS_WCHK	3
#define  RKCS_SEEK	4
#define	 RKCS_RCHK	5
#define  RKCS_DRVRESET	6
#define  RKCS_WLK	7
#define RKCS_V_FUNC	1
#define RKCS_MEX	0000060				/* memory extension */
#define RKCS_V_MEX	4
#define RKCS_SSE	0000400				/* stop on soft err */
#define RKCS_FMT	0002000				/* format */
#define RKCS_INH	0004000				/* inhibit increment */
#define RKCS_SCP	0020000				/* search complete */
#define RKCS_HERR	0040000				/* hard error */
#define RKCS_ERR	0100000				/* error */
#define RKCS_REAL	0026776				/* kept here */
#define RKCS_RW		0006576				/* read/write */
#define GET_FUNC(x)	(((x) >> RKCS_V_FUNC) & RKCS_M_FUNC)

/* RKDA */

#define RKDA_V_SECT	0				/* sector */
#define RKDA_M_SECT	017
#define RKDA_V_TRACK	4				/* track */
#define RKDA_M_TRACK	0777
#define RKDA_V_CYL	5				/* cylinder */
#define RKDA_M_CYL	0377
#define RKDA_V_DRIVE	13				/* drive */
#define RKDA_M_DRIVE	07
#define RKDA_DRIVE	(RKDA_M_DRIVE << RKDA_V_DRIVE)
#define GET_SECT(x)	(((x) >> RKDA_V_SECT) & RKDA_M_SECT)
#define GET_CYL(x)	(((x) >> RKDA_V_CYL) & RKDA_M_CYL)
#define GET_TRACK(x)	(((x) >> RKDA_V_TRACK) & RKDA_M_TRACK)
#define GET_DRIVE(x)	(((x) >> RKDA_V_DRIVE) & RKDA_M_DRIVE)
#define GET_DA(x)	((GET_TRACK (x) * RK_NUMSC) + GET_SECT (x))

/* RKBA */

#define RKBA_IMP	0177776				/* implemented */

#define RK_MIN 10
#define MAX(x,y) (((x) > (y))? (x): (y))

extern int32 int_req;
extern unsigned int16 *M;				/* memory */
extern UNIT cpu_unit;
int32 rkcs = 0;						/* control/status */
int32 rkds = 0;						/* drive status */
int32 rkba = 0;						/* memory address */
int32 rkda = 0;						/* disk address */
int32 rker = 0;						/* error status */
int32 rkwc = 0;						/* word count */
int32 rkintq = 0;					/* interrupt queue */
int32 last_drv = 0;					/* last r/w drive */
int32 rk_stopioe = 1;					/* stop on error */
int32 rk_swait = 10;					/* seek time */
int32 rk_rwait = 10;					/* rotate time */
t_stat rk_svc (UNIT *uptr);
t_stat rk_reset (DEVICE *dptr);
void rk_go (void);
void rk_set_done (int32 error);
void rk_clr_done (void);
t_stat rk_boot (int32 unitno);
extern t_stat sim_activate (UNIT *uptr, int32 delay);
extern t_stat sim_cancel (UNIT *uptr);
extern int32 sim_is_active (UNIT *uptr);
extern size_t fxread (void *bptr, size_t size, size_t count, FILE *fptr);
extern size_t fxwrite (void *bptr, size_t size, size_t count, FILE *fptr);

/* RK11 data structures

   rk_dev	RK device descriptor
   rk_unit	RK unit list
   rk_reg	RK register list
   rk_mod	RK modifier list
*/

UNIT rk_unit[] = {
	{ UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RK_SIZE) },
	{ UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RK_SIZE) },
	{ UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RK_SIZE) },
	{ UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RK_SIZE) },
	{ UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RK_SIZE) },
	{ UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RK_SIZE) },
	{ UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RK_SIZE) },
	{ UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RK_SIZE) } };

REG rk_reg[] = {
	{ ORDATA (RKCS, rkcs, 16) },
	{ ORDATA (RKDA, rkda, 16) },
	{ ORDATA (RKBA, rkba, 16) },
	{ ORDATA (RKWC, rkwc, 16) },
	{ ORDATA (RKDS, rkds, 16) },
	{ ORDATA (RKER, rker, 16) },
	{ ORDATA (INTQ, rkintq, 9) },
	{ ORDATA (DRVN, last_drv, 3) },
	{ FLDATA (INT, int_req, INT_V_RK) },
	{ FLDATA (ERR, rkcs, CSR_V_ERR) },
	{ FLDATA (DONE, rkcs, CSR_V_DONE) },
	{ FLDATA (IE, rkcs, CSR_V_IE) },
	{ DRDATA (STIME, rk_swait, 24), PV_LEFT },
	{ DRDATA (RTIME, rk_rwait, 24), PV_LEFT },
	{ GRDATA (FLG0, rk_unit[0].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG1, rk_unit[1].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG2, rk_unit[2].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG3, rk_unit[3].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG4, rk_unit[4].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG5, rk_unit[5].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG6, rk_unit[6].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG7, rk_unit[7].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ FLDATA (STOP_IOE, rk_stopioe, 0) },
	{ NULL }  };

MTAB rk_mod[] = {
	{ (UNIT_HWLK+UNIT_SWLK), 0, "write enabled", "ENABLED", NULL },
	{ (UNIT_HWLK+UNIT_SWLK), UNIT_HWLK, "write locked", "LOCKED", NULL },
	{ (UNIT_HWLK+UNIT_SWLK), UNIT_SWLK, "write locked", NULL, NULL },
	{ (UNIT_HWLK+UNIT_SWLK), (UNIT_HWLK+UNIT_SWLK), "write locked",
		NULL, NULL }, 
	{ 0 }  };

DEVICE rk_dev = {
	"RK", rk_unit, rk_reg, rk_mod,
	RK_NUMDR, 8, 24, 1, 8, 16,
	NULL, NULL, &rk_reset,
	&rk_boot, NULL, NULL };

/* I/O dispatch routine, I/O addresses 17777400 - 17777416

   17777400	RKDS	read only, constructed from "id'd drive"
			plus current drive status flags
   17777402	RKER	read only, set as operations progress,
			cleared by INIT or CONTROL RESET
   17777404	RKCS	read/write
   17777406	RKWC	read/write
   17777410	RKBA	read/write
   17777412	RKDA	read/write
   17777414	RKMR	read/write, unimplemented
   17777416	RKDB	read only, unimplemented
*/

t_stat rk_rd (int32 *data, int32 PA, int32 access)
{
UNIT *uptr;

switch ((PA >> 1) & 07) {				/* decode PA<3:1> */
case 0:							/* RKDS: read only */
	rkds = (rkds & RKDS_ID) | RKDS_RK05 | RKDS_SC_OK |
		(rand () % RK_NUMSC);			/* random sector */
	uptr = rk_dev.units + GET_DRIVE (rkda);		/* selected unit */
	if (uptr -> flags & UNIT_ATT) rkds = rkds | RKDS_RDY;	/* attached? */
	if (!sim_is_active (uptr)) rkds = rkds | RKDS_RWS;	/* idle? */
	if (uptr -> flags & (UNIT_HWLK + UNIT_SWLK)) rkds = rkds | RKDS_WLK;
	if (GET_SECT (rkda) == (rkds & RKDS_SC)) rkds = rkds | RKDS_ON_SC;
	*data = rkds;
	return SCPE_OK;
case 1:							/* RKER: read only */
	*data = rker & RKER_IMP;
	return SCPE_OK;
case 2:							/* RKCS */
	rkcs = rkcs & RKCS_REAL;
	if (rker) rkcs = rkcs | RKCS_ERR;		/* update err flags */
	if (rker & RKER_HARD) rkcs = rkcs | RKCS_HERR;
	*data = rkcs;
	return SCPE_OK;
case 3:							/* RKWC */
	*data = rkwc;
	return SCPE_OK;
case 4:							/* RKBA */
	*data = rkba & RKBA_IMP;
	return SCPE_OK;
case 5:							/* RKDA */
	*data = rkda;
	return SCPE_OK;
default:
	*data = 0;
	return SCPE_OK;  }				/* end switch */
}

t_stat rk_wr (int32 data, int32 PA, int32 access)
{
switch ((PA >> 1) & 07) {				/* decode PA<3:1> */
case 0:							/* RKDS: read only */
	return SCPE_OK;
case 1:							/* RKER: read only */
	return SCPE_OK;
case 2:							/* RKCS */
	rkcs = rkcs & RKCS_REAL;
	if (access == WRITEB) data = (PA & 1)?
		(rkcs & 0377) | (data << 8): (rkcs & ~0377) | data;
	if ((data & CSR_IE) == 0) {			/* int disable? */
		rkintq = 0;				/* clr int queue */
		int_req = int_req & ~INT_RK;  }		/* clr int request */
	else if ((rkcs & (CSR_DONE + CSR_IE)) == CSR_DONE) {
		rkintq = rkintq | RK_CTLI;		/* queue ctrl int */
		int_req = int_req | INT_RK;  }		/* set int request */
	rkcs = (rkcs & ~RKCS_RW) | (data & RKCS_RW);
	if ((rkcs & CSR_DONE) && (data & CSR_GO)) rk_go (); /* new function? */
	return SCPE_OK;
case 3:							/* RKWC */
	if (access == WRITEB) data = (PA & 1)?
		(rkwc & 0377) | (data << 8): (rkwc & ~0377) | data;
	rkwc = data;
	return SCPE_OK;
case 4:							/* RKBA */
	if (access == WRITEB) data = (PA & 1)?
		(rkba & 0377) | (data << 8): (rkba & ~0377) | data;
	rkba = data & RKBA_IMP;
	return SCPE_OK;
case 5:							/* RKDA */
	if ((rkcs & CSR_DONE) == 0) return SCPE_OK;
	if (access == WRITEB) data = (PA & 1)?
		(rkda & 0377) | (data << 8): (rkda & ~0377) | data;
	rkda = data;
	return SCPE_OK;
default:
	return SCPE_OK;  }				/* end switch */
}

/* Initiate new function */

void rk_go (void)
{
int32 i, sect, cyl, func;
UNIT *uptr;

func = GET_FUNC (rkcs);					/* get function */
if (func == RKCS_CTLRESET) {				/* control reset? */
	rker = 0;					/* clear errors */
	rkda = 0;
	rkba = 0;
	rkcs = CSR_DONE;
	rkintq = 0;					/* clr int queue */
	int_req = int_req & ~INT_RK;			/* clr int request */
	return;  }
rker = rker & ~RKER_SOFT;				/* clear soft errors */
if (rker == 0) rkcs = rkcs & ~RKCS_ERR;			/* redo summary */
rkcs = rkcs & ~RKCS_SCP;				/* clear sch compl*/
rk_clr_done ();						/* clear done */
last_drv = GET_DRIVE (rkda);				/* get drive no */
uptr = rk_dev.units + last_drv;				/* select unit */
if (uptr -> flags & UNIT_DIS) {				/* not present? */
	rk_set_done (RKER_NXD);
	return;  }
if (((uptr -> flags & UNIT_ATT) == 0) || sim_is_active (uptr)) {
	rk_set_done (RKER_DRE);				/* not att or busy */
	return;  }
if (rkcs & (RKCS_INH + RKCS_FMT)) {			/* format? */
	rk_set_done (RKER_PGE);
	return;  }
if ((func == RKCS_WRITE) && (uptr -> flags & (UNIT_HWLK + UNIT_SWLK))) {
	rk_set_done (RKER_WLK);				/* write and locked? */
	return;  }
if (func == RKCS_WLK) {					/* write lock? */
	uptr -> flags = uptr -> flags | UNIT_SWLK;
	rk_set_done (0);
	return;  }
if (func == RKCS_DRVRESET) {				/* drive reset? */
	uptr -> flags = uptr -> flags & ~UNIT_SWLK;
	cyl = sect = 0;
	func = RKCS_SEEK;  }
else {	sect = GET_SECT (rkda);
	cyl = GET_CYL (rkda);  }
if (sect >= RK_NUMSC) {					/* bad sector? */
	rk_set_done (RKER_NXS);
	return;  }
if (cyl >= RK_NUMCY) {					/* bad cyl? */
	rk_set_done (RKER_NXC);
	return;  }
i = abs (cyl - uptr -> CYL) * rk_swait;			/* seek time */
if (func == RKCS_SEEK) {				/* seek? */
	rk_set_done (0);				/* set done */
	sim_activate (uptr, MAX (RK_MIN, i));  }	/* schedule */
else sim_activate (uptr, i + rk_rwait);
uptr -> FUNC = func;					/* save func */
uptr -> CYL = cyl;					/* put on cylinder */
return;
}

/* Service unit timeout

   If seek in progress, complete seek command
   Else complete data transfer command

   The unit control block contains the function and disk address for
   the current command.
*/

static unsigned int16 fill[RK_NUMWD] = { 0 };
t_stat rk_svc (UNIT *uptr)
{
int32 comp, drv, err, awc, twc, wc;
int32 pa, da, fillc, track, sect;

drv = uptr - rk_dev.units;				/* get drv number */
if (uptr -> FUNC == RKCS_SEEK) {			/* seek */
	rkcs = rkcs | RKCS_SCP;				/* set seek done */
	if (rkcs & CSR_IE) {				/* ints enabled? */
		rkintq = rkintq | RK_SCPI (drv);	/* queue request */
		if (rkcs & CSR_DONE) int_req = int_req | INT_RK;  }
	else {	rkintq = 0;				/* clear queue */
		int_req = int_req & ~INT_RK;  }		/* clear interrupt */
	return SCPE_OK;  }

if ((uptr -> flags & UNIT_ATT) == 0) {			/* attached? */
	rk_set_done (RKER_DRE);
	return IORETURN (rk_stopioe, SCPE_UNATT);  }
pa = (((rkcs & RKCS_MEX) << (16 - RKCS_V_MEX)) | rkba) >> 1;
da = GET_DA (rkda) * RK_NUMWD;				/* get disk addr */
twc = 0200000 - rkwc;					/* get true wc */
if ((pa + twc) > RK_MAXMEM) {				/* mem overrun? */
	rker = rker | RKER_NXM;
	wc = (RK_MAXMEM - pa);  }
else wc = twc;
if (wc < 0) {						/* abort transfer? */
	rk_set_done (0);
	return SCPE_OK;  }
if ((da + twc) > RK_SIZE) {				/* disk overrun? */
	rker = rker | RKER_OVR;
	if (wc > (RK_SIZE - da)) wc = RK_SIZE - da;  }

err = fseek (uptr -> fileref, da * sizeof (int16), SEEK_SET);

if ((uptr -> FUNC == RKCS_READ) && (err == 0)) {	/* read? */
	awc = fxread (&M[pa], sizeof (int16), wc, uptr -> fileref);
	for ( ; awc < wc; awc++) M[pa + awc] = 0;
	err = ferror (uptr -> fileref);  }

if ((uptr -> FUNC == RKCS_WRITE) && (err == 0)) {	/* write? */
	fxwrite (&M[pa], sizeof (int16), wc, uptr -> fileref);
	err = ferror (uptr -> fileref);
	if ((err == 0) && (fillc = (wc & (RK_NUMWD - 1)))) {
		fxwrite (fill, sizeof (int16), fillc, uptr -> fileref);
		err = ferror (uptr -> fileref);  }  }

if ((uptr -> FUNC == RKCS_WCHK) && (err == 0)) {	/* write check? */
	twc = wc;					/* xfer length */
	for (wc = 0; (err == 0) && (wc < twc); wc++)  {
		awc = fxread (&comp, sizeof (int16), 1, uptr -> fileref);
		if (awc == 0) comp = 0;
		if (comp != M[pa + wc])  {
			rker = rker | RKER_WCE;
			if (rkcs & RKCS_SSE) break;  }  }
	err = ferror (uptr -> fileref);  }

rkwc = (rkwc + wc) & 0177777;				/* final word count */
pa = (pa + wc) << 1;					/* final byte addr */
rkba = pa & RKBA_IMP;					/* lower 16b */
rkcs = (rkcs & ~RKCS_MEX) | ((pa >> (16 - RKCS_V_MEX)) & RKCS_MEX);
da = da + wc + (RK_NUMWD - 1);
track = (da / RK_NUMWD) / RK_NUMSC;
sect = (da / RK_NUMWD) % RK_NUMSC;
rkda = (rkda & RKDA_DRIVE) | (track << RKDA_V_TRACK) | (sect << RKDA_V_SECT);
rk_set_done (0);

if (err != 0) {						/* error? */
	perror ("RK I/O error");
	clearerr (uptr -> fileref);
	return SCPE_IOERR;  }
return SCPE_OK;
}

/* Interrupt state change routines

   rk_set_done		set done and possibly errors
   rk_clr_done		clear done
   rk_inta		acknowledge intererupt
*/

void rk_set_done (int32 error)
{
	rkcs = rkcs | CSR_DONE;				/* set done */
	if (error != 0) {
		rker = rker | error;			/* update error */
		if (rker) rkcs = rkcs | RKCS_ERR;	/* update err flags */
		if (rker & RKER_HARD) rkcs = rkcs | RKCS_HERR;  }
	if (rkcs & CSR_IE) {				/* int enable? */
		rkintq = rkintq | RK_CTLI;		/* set ctrl int */
		int_req = int_req | INT_RK;  }		/* request int */
	else {	rkintq = 0;				/* clear queue */
		int_req = int_req & ~INT_RK;  }
	return;
}

void rk_clr_done (void)
{
	rkcs = rkcs & ~CSR_DONE;			/* clear done */
	rkintq = rkintq & ~RK_CTLI;			/* clear ctl int */
	int_req = int_req & ~INT_RK;			/* clear int req */
	return;
}

int32 rk_inta (void)
{
int32 i;

for (i = 0; i <= RK_NUMDR; i++) {			/* loop thru intq */
	if (rkintq & (1u << i)) {			/* bit i set? */
		rkintq = rkintq & ~(1u << i);		/* clear bit i */
		if (rkintq) int_req = int_req | INT_RK;	/* queue next */
		rkds = (rkds & ~RKDS_ID) |		/* id drive */
			(((i == 0)? last_drv: i - 1) << RKDS_V_ID);
		return VEC_RK;  }  }			/* return vector */
rkintq = 0;						/* clear queue */
return 0;						/* passive release */
}

/* Device reset */

t_stat rk_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

rkcs = CSR_DONE;
rkda = rkba = rker = rkds = 0;
rkintq = last_drv = 0;
int_req = int_req & ~INT_RK;
for (i = 0; i < RK_NUMDR; i++) {
	uptr = rk_dev.units + i;
	sim_cancel (uptr);
	uptr -> CYL = uptr -> FUNC = 0;
	uptr -> flags = uptr -> flags & ~UNIT_SWLK;  }
return SCPE_OK;
}

/* Device bootstrap */

#define BOOT_START 02000		/* start */
#define BOOT_UNIT 02006			/* where to store unit number */
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int32))

static const int32 boot_rom[] = {
	0012706, 0002000,		/* MOV #2000, SP */
	0012700, 0000000,		/* MOV #unit, R0	; unit number */
	0010003,			/* MOV R0, R3 */
	0000303,			/* SWAB R3 */
	0006303,			/* ASL R3 */
	0006303,			/* ASL R3 */
	0006303,			/* ASL R3 */
	0006303,			/* ASL R3 */
	0006303,			/* ASL R3 */
	0012701, 0177412,		/* MOV #RKDA, R1	; csr */
	0010311,			/* MOV R3, (R1)		; load da */
	0005041,			/* CLR -(R1)		; clear ba */
	0012741, 0177000,		/* MOV #-256.*2, -(R1)	; load wc */
	0012741, 0000005,		/* MOV #READ+GO, -(R1)	; read & go */
	0005002,			/* CLR R2 */
	0005003,			/* CLR R3 */
	0005004,			/* CLR R4 */
	0012705, 0062153,		/* MOV #"DK, R5 */
	0105711,			/* TSTB (R1) */
	0100376,			/* BPL .-2 */
	0105011,			/* CLRB (R1) */
	0005007				/* CLR PC */
};

t_stat rk_boot (int32 unitno)
{
int32 i;
extern int32 saved_PC;

for (i = 0; i < BOOT_LEN; i++) M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & RK_M_NUMDR;
saved_PC = BOOT_START;
return SCPE_OK;
}
