/* pdp8_dt.c: PDP-8 DECtape simulator

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

   dt		TC08/TU56 DECtape

   29-Aug-01	RMS	Added casts to PDP-18b packup routine
   17-Jul-01	RMS	Moved function prototype
   11-May-01	RMS	Fixed bug in reset
   25-Apr-01	RMS	Added device enable/disable support
   18-Apr-01	RMS	Changed to rewind tape before boot
   19-Mar-01	RMS	Changed bootstrap to support 4k disk monitor
   15-Mar-01	RMS	Added 129th word to PDP-8 format

   PDP-8 DECtapes are represented by fixed length data blocks of 12b words.  Two
   tape formats are supported:

	12b			129 words per block
	16b/18b/36b		384 words per block

   DECtape motion is measured in 3b lines.  Time between lines is 33.33us.
   Tape density is nominally 300 lines per inch.  The format of a DECtape is

	reverse end zone	36000 lines ~ 10 feet
	block 0
	 :
	block n
	forward end zone	36000 lines ~ 10 feet

   A block consists of five 18b header words, a tape-specific number of data
   words, and five 18b trailer words.  All systems except the PDP-8 use a
   standard block length of 256 words; the PDP-8 uses a standard block length
   of 86 words (x 18b = 129 words x 12b).

   Because a DECtape file only contains data, the simulator cannot support
   write timing and mark track and can only do a limited implementation
   of read all and write all.  Read all assumes that the tape has been
   conventionally written forward:

	header word 0		0
	header word 1		block number (for forward reads)
	header words 2,3	0
	header word 4		0
	:
	trailer word 4		checksum
	trailer words 3,2	0
	trailer word 1		block number (for reverse reads)
	trailer word 0		0

   Write all writes only the data words and dumps the interblock words in the
   bit bucket.
*/

#include "pdp8_defs.h"

#define DT_NUMDR	8				/* #drives */
#define UNIT_V_WLK	(UNIT_V_UF + 0)			/* write locked */
#define UNIT_WLK	(1 << UNIT_V_WLK)
#define UNIT_V_8FMT	(UNIT_V_UF + 1)			/* 12b format */
#define UNIT_8FMT	(1 << UNIT_V_8FMT)
#define UNIT_W_UF	3				/* saved flag width */
#define STATE		u3				/* unit state */
#define LASTT		u4				/* last time update */
#define DT_WC		07754				/* word count */
#define DT_CA		07755				/* current addr */

/* System independent DECtape constants */

#define DT_EZLIN	36000				/* end zone length */
#define DT_HTLIN	30				/* header/trailer lines */
#define DT_BLKLN	6				/* blk no line in h/t */
#define DT_CSMLN	24				/* checksum line in h/t */

/* 16b, 18b, 36b DECtape constants */

#define D18_WSIZE	6				/* word size in lines */
#define D18_BSIZE	384				/* block size in 12b */
#define D18_TSIZE	578				/* tape size */
#define D18_LPERB	(DT_HTLIN + (D18_BSIZE * DT_WSIZE) + DT_HTLIN)
#define D18_FWDEZ	(DT_EZLIN + (D18_LPERB * D18_TSIZE))
#define D18_CAPAC	(D18_TSIZE * D18_BSIZE)		/* tape capacity */

#define D18_NBSIZE	((D18_BSIZE * D8_WSIZE) / D18_WSIZE)
#define D18_FILSIZ	(D18_NBSIZE * D18_TSIZE * sizeof (int32))

/* 12b DECtape constants */

#define D8_WSIZE	4				/* word size in lines */
#define D8_BSIZE	129				/* block size in 12b */
#define D8_TSIZE	1474				/* tape size */
#define D8_LPERB	(DT_HTLIN + (D8_BSIZE * DT_WSIZE) + DT_HTLIN)
#define D8_FWDEZ	(DT_EZLIN + (D8_LPERB * D8_TSIZE))
#define D8_CAPAC	(D8_TSIZE * D8_BSIZE)		/* tape capacity */

/* This controller */

#define DT_CAPAC	D8_CAPAC			/* default */
#define DT_WSIZE	D8_WSIZE

/* Calculated constants, per unit */

#define DTU_BSIZE(u)	(((u) -> flags & UNIT_8FMT)? D8_BSIZE: D18_BSIZE)
#define DTU_TSIZE(u)	(((u) -> flags & UNIT_8FMT)? D8_TSIZE: D18_TSIZE)
#define DTU_LPERB(u)	(((u) -> flags & UNIT_8FMT)? D8_LPERB: D18_LPERB)
#define DTU_FWDEZ(u)	(((u) -> flags & UNIT_8FMT)? D8_FWDEZ: D18_FWDEZ)
#define DTU_CAPAC(u)	(((u) -> flags & UNIT_8FMT)? D8_CAPAC: D18_CAPAC)

#define DT_LIN2BL(p,u)	(((p) - DT_EZLIN) / DTU_LPERB (u))
#define DT_LIN2OF(p,u)	(((p) - DT_EZLIN) % DTU_LPERB (u))
#define DT_LIN2WD(p,u)	((DT_LIN2OF (p,u) - DT_HTLIN) / DT_WSIZE)
#define DT_BLK2LN(p,u)	(((p) * DTU_LPERB (u)) + DT_EZLIN)
#define DT_QREZ(u)	(((u) -> pos) < DT_EZLIN)
#define DT_QFEZ(u)	(((u) -> pos) >= ((uint32) DTU_FWDEZ (u)))
#define DT_QEZ(u)	(DT_QREZ (u) || DT_QFEZ (u))

/* Status register A */

#define DTA_V_UNIT	9				/* unit select */
#define DTA_M_UNIT	07
#define DTA_UNIT	(DTA_M_UNIT << DTA_V_UNIT)
#define DTA_V_MOT	7				/* motion */
#define DTA_M_MOT	03
#define DTA_V_MODE	6				/* mode */
#define DTA_V_FNC	3				/* function */
#define DTA_M_FNC	07
#define  FNC_MOVE	 00				/* move */
#define  FNC_SRCH	 01				/* search */
#define  FNC_READ	 02				/* read */
#define  FNC_RALL	 03				/* read all */
#define  FNC_WRIT	 04				/* write */
#define  FNC_WALL	 05				/* write all */
#define  FNC_WMRK	 06				/* write timing */
#define DTA_V_ENB	2				/* int enable */
#define DTA_V_CERF	1				/* clr error flag */
#define DTA_V_CDTF	0				/* clr DECtape flag */
#define DTA_FWDRV	(1u << (DTA_V_MOT + 1))
#define DTA_STSTP	(1u << DTA_V_MOT)
#define DTA_MODE	(1u << DTA_V_MODE)
#define DTA_ENB		(1u << DTA_V_ENB)
#define DTA_CERF	(1u << DTA_V_CERF)
#define DTA_CDTF	(1u << DTA_V_CDTF)
#define DTA_RW		(07777 & ~(DTA_CERF | DTA_CDTF))

#define DTA_GETUNIT(x)	(((x) >> DTA_V_UNIT) & DTA_M_UNIT)
#define DTA_GETMOT(x)	(((x) >> DTA_V_MOT) & DTA_M_MOT)
#define DTA_GETFNC(x)	(((x) >> DTA_V_FNC) & DTA_M_FNC)

/* Status register B */

#define DTB_V_ERF	11				/* error flag */
#define DTB_V_MRK	10				/* mark trk err */
#define DTB_V_END	9				/* end zone err */
#define DTB_V_SEL	8				/* select err */
#define DTB_V_PAR	7				/* parity err */
#define DTB_V_TIM	6				/* timing err */
#define DTB_V_MEX	3				/* memory extension */
#define DTB_M_MEX	07
#define DTB_MEX		(DTB_M_MEX << DTB_V_MEX)
#define DTB_V_DTF	0				/* DECtape flag */
#define DTB_ERF		(1u << DTB_V_ERF)
#define DTB_MRK		(1u << DTB_V_MRK)
#define DTB_END		(1u << DTB_V_END)
#define DTB_SEL		(1u << DTB_V_SEL)
#define DTB_PAR		(1u << DTB_V_PAR)
#define DTB_TIM		(1u << DTB_V_TIM)
#define DTB_DTF		(1u << DTB_V_DTF)
#define DTB_ALLERR	(DTB_ERF | DTB_MRK | DTB_END | DTB_SEL | \
			 DTB_PAR | DTB_TIM)
#define DTB_GETMEX(x)	(((x) & DTB_MEX) << (12 - DTB_V_MEX))

/* DECtape state */

#define DTS_V_MOT	3				/* motion */
#define DTS_M_MOT	07
#define  DTS_STOP	 0				/* stopped */
#define  DTS_DECF	 2				/* decel, fwd */
#define  DTS_DECR	 3				/* decel, rev */
#define  DTS_ACCF	 4				/* accel, fwd */
#define  DTS_ACCR	 5				/* accel, rev */
#define  DTS_ATSF	 6				/* @speed, fwd */
#define  DTS_ATSR	 7				/* @speed, rev */
#define DTS_DIR		01				/* dir mask */
#define DTS_V_FNC	0				/* function */
#define DTS_M_FNC	07
#define  DTS_OFR	7				/* "off reel" */
#define DTS_GETMOT(x)	(((x) >> DTS_V_MOT) & DTS_M_MOT)
#define DTS_GETFNC(x)	(((x) >> DTS_V_FNC) & DTS_M_FNC)
#define DTS_V_2ND	6				/* next state */
#define DTS_V_3RD	(DTS_V_2ND + DTS_V_2ND)		/* next next */
#define DTS_STA(y,z)	(((y) << DTS_V_MOT) | ((z) << DTS_V_FNC))
#define DTS_SETSTA(y,z) uptr -> STATE = DTS_STA (y, z)
#define DTS_SET2ND(y,z) uptr -> STATE = (uptr -> STATE & 077) | \
				((DTS_STA (y, z)) << DTS_V_2ND)
#define DTS_SET3RD(y,z) uptr -> STATE = (uptr -> STATE & 07777) | \
				((DTS_STA (y, z)) << DTS_V_3RD)
#define DTS_NXTSTA(x)	(x >> DTS_V_2ND)

/* Operation substates */

#define DTO_WCO		1				/* wc overflow */
#define DTO_SOB		2				/* start of block */

/* Logging */

#define LOG_MS		001				/* move, search */
#define LOG_RW		002				/* read, write */
#define LOG_RA		004				/* read all */
#define LOG_BL		010				/* block # lblk */

#define DT_UPDINT	if ((dtsa & DTA_ENB) && (dtsb & (DTB_ERF | DTB_DTF))) \
			int_req = int_req | INT_DTA; \
			else int_req = int_req & ~INT_DTA;
#define ABS(x)		(((x) < 0)? (-(x)): (x))

extern uint16 M[];
extern int32 int_req, dev_enb;
extern UNIT cpu_unit;
extern int32 sim_switches;
int32 dtsa = 0;						/* status A */
int32 dtsb = 0;						/* status B */
int32 dt_ltime = 12;					/* interline time */
int32 dt_actime = 54000;				/* accel time */
int32 dt_dctime = 72000;				/* decel time */
int32 dt_substate = 0;
int32 dt_log = 0;					/* debug */
int32 dt_logblk = 0;
t_stat dt_svc (UNIT *uptr);
t_stat dt_reset (DEVICE *dptr);
t_stat dt_attach (UNIT *uptr, char *cptr);
t_stat dt_detach (UNIT *uptr);
t_stat dt_boot (int32 unitno);
void dt_deselect (int32 oldf);
void dt_newsa (int32 newf);
void dt_newfnc (UNIT *uptr, int32 newsta);
t_bool dt_setpos (UNIT *uptr);
void dt_schedez (UNIT *uptr, int32 dir);
void dt_seterr (UNIT *uptr, int32 e);
int32 dt_comobv (int32 val);
int32 dt_csum (UNIT *uptr, int32 blk);
int32 dt_gethdr (UNIT *uptr, int32 blk, int32 relpos, int32 dir);
extern int32 sim_is_running;

/* DT data structures

   dt_dev	DT device descriptor
   dt_unit	DT unit list
   dt_reg	DT register list
   dt_mod	DT modifier list
*/

UNIT dt_unit[] = {
	{ UDATA (&dt_svc, UNIT_8FMT+UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svc, UNIT_8FMT+UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svc, UNIT_8FMT+UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svc, UNIT_8FMT+UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svc, UNIT_8FMT+UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svc, UNIT_8FMT+UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svc, UNIT_8FMT+UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svc, UNIT_8FMT+UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) }  };

REG dt_reg[] = {
	{ ORDATA (DTSA, dtsa, 12) },
	{ ORDATA (DTSB, dtsb, 12) },
	{ FLDATA (INT, int_req, INT_V_DTA) },
	{ FLDATA (ENB, dtsa, DTA_V_ENB) },
	{ FLDATA (DTF, dtsb, DTB_V_DTF) },
	{ FLDATA (ERF, dtsb, DTB_V_ERF) },
	{ ORDATA (WC, M[DT_WC], 18) },
	{ ORDATA (CA, M[DT_CA], 18) },
	{ DRDATA (LTIME, dt_ltime, 31), REG_NZ },
	{ DRDATA (ACTIME, dt_actime, 31), REG_NZ },
	{ DRDATA (DCTIME, dt_dctime, 31), REG_NZ },
	{ ORDATA (SUBSTATE, dt_substate, 2) },
	{ ORDATA (LOG, dt_log, 4), REG_HIDDEN },
	{ DRDATA (LBLK, dt_logblk, 12), REG_HIDDEN },
	{ DRDATA (POS0, dt_unit[0].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS1, dt_unit[1].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS2, dt_unit[2].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS3, dt_unit[3].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS4, dt_unit[4].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS5, dt_unit[5].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS6, dt_unit[6].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS7, dt_unit[7].pos, 31), PV_LEFT + REG_RO },
	{ ORDATA (STATT0, dt_unit[0].STATE, 18), REG_RO },
	{ ORDATA (STATT1, dt_unit[1].STATE, 18), REG_RO },
	{ ORDATA (STATT2, dt_unit[2].STATE, 18), REG_RO },
	{ ORDATA (STATT3, dt_unit[3].STATE, 18), REG_RO },
	{ ORDATA (STATT4, dt_unit[4].STATE, 18), REG_RO },
	{ ORDATA (STATT5, dt_unit[5].STATE, 18), REG_RO },
	{ ORDATA (STATT6, dt_unit[6].STATE, 18), REG_RO },
	{ ORDATA (STATT7, dt_unit[7].STATE, 18), REG_RO },
	{ DRDATA (LASTT0, dt_unit[0].LASTT, 32), REG_HRO },
	{ DRDATA (LASTT1, dt_unit[1].LASTT, 32), REG_HRO },
	{ DRDATA (LASTT2, dt_unit[2].LASTT, 32), REG_HRO },
	{ DRDATA (LASTT3, dt_unit[3].LASTT, 32), REG_HRO },
	{ DRDATA (LASTT4, dt_unit[4].LASTT, 32), REG_HRO },
	{ DRDATA (LASTT5, dt_unit[5].LASTT, 32), REG_HRO },
	{ DRDATA (LASTT6, dt_unit[6].LASTT, 32), REG_HRO },
	{ DRDATA (LASTT7, dt_unit[7].LASTT, 32), REG_HRO },
	{ GRDATA (FLG0, dt_unit[0].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG1, dt_unit[1].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG2, dt_unit[2].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG3, dt_unit[3].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG4, dt_unit[4].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG5, dt_unit[5].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG6, dt_unit[6].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG7, dt_unit[7].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ FLDATA (*DEVENB, dev_enb, INT_V_DTA), REG_HRO },
	{ NULL }  };

MTAB dt_mod[] = {
	{ UNIT_WLK, 0, "write enabled", "ENABLED", NULL },
	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL }, 
	{ UNIT_8FMT, 0, "16b/18b", NULL, NULL },
	{ UNIT_8FMT, UNIT_8FMT, "12b", NULL, NULL },
	{ 0 }  };

DEVICE dt_dev = {
	"DT", dt_unit, dt_reg, dt_mod,
	DT_NUMDR, 8, 24, 1, 8, 12,
	NULL, NULL, &dt_reset,
	&dt_boot, &dt_attach, &dt_detach };

/* IOT routines */

int32 dt76 (int32 pulse, int32 AC)
{
int32 old_dtsa = dtsa, fnc;
UNIT *uptr;

if (pulse & 01) AC = AC | dtsa;				/* DTRA */
if (pulse & 06) {					/* select */
	if (pulse & 02) dtsa = 0;			/* DTCA */
	if (pulse & 04) {				/* DTXA */
		if ((AC & DTA_CERF) == 0) dtsb = dtsb & ~DTB_ALLERR;
		if ((AC & DTA_CDTF) == 0) dtsb = dtsb & ~DTB_DTF;
		dtsa = dtsa ^ (AC & DTA_RW);
		AC = 0;  }				/* clr AC */
	if ((old_dtsa ^ dtsa) & DTA_UNIT) dt_deselect (old_dtsa);
	uptr = dt_dev.units + DTA_GETUNIT (dtsa);	/* get unit */
	fnc = DTA_GETFNC (dtsa);			/* get fnc */
	if (((uptr -> flags) & UNIT_DIS) ||		/* disabled? */
	     (fnc >= FNC_WMRK) ||			/* write mark? */
	    ((fnc == FNC_WALL) && (uptr -> flags & UNIT_WLK)) ||
	    ((fnc == FNC_WRIT) && (uptr -> flags & UNIT_WLK)))
		dt_seterr (uptr, DTB_SEL);		/* select err */
	else dt_newsa (dtsa);
	DT_UPDINT;  }
return AC;
}

int32 dt77 (int32 pulse, int32 AC)
{
if ((pulse & 01) && (dtsb & (DTB_ERF |DTB_DTF)))	/* DTSF */
	AC = IOT_SKP | AC;
if (pulse & 02) AC = AC | dtsb;				/* DTRB */
if (pulse & 04) {					/* DTLB */
	dtsb = (dtsb & ~DTB_MEX) | (AC & DTB_MEX);
	AC = AC & ~07777;  }				/* clear AC */
return AC;
}

/* Unit deselect */

void dt_deselect (int32 oldf)
{
int32 old_unit = DTA_GETUNIT (oldf);
UNIT *uptr = dt_dev.units + old_unit;
int32 old_mot = DTS_GETMOT (uptr -> STATE);

if (old_mot >= DTS_ATSF)				/* at speed? */
	dt_newfnc (uptr, DTS_STA (old_mot, DTS_OFR));
else if (old_mot >= DTS_ACCF)				/* accelerating? */
	DTS_SET2ND (DTS_ATSF | (old_mot & DTS_DIR), DTS_OFR);
return;  }

/* Command register change

   1. If change in motion, stop to start
	- schedule acceleration
	- set function as next state
   2. If change in motion, start to stop
	- if not already decelerating (could be reversing),
	  schedule deceleration
   3. If change in direction,
	- if not decelerating, schedule deceleration
	- set accelerating (other dir) as next state
	- set function as next next state
   4. If not accelerating or at speed,
	- schedule acceleration
	- set function as next state
   5. If not yet at speed,
	- set function as next state
   6. If at speed,
	- set function as current state, schedule function
*/

void dt_newsa (int32 newf)
{
int32 new_unit, prev_mot, prev_fnc, new_fnc;
int32 prev_mving, new_mving, prev_dir, new_dir;
UNIT *uptr;

new_unit = DTA_GETUNIT (newf);				/* new, old units */
uptr = dt_dev.units + new_unit;
if ((uptr -> flags & UNIT_ATT) == 0) {			/* new unit attached? */
	dt_seterr (uptr, DTB_SEL);			/* no, error */
	return;  }
prev_mot = DTS_GETMOT (uptr -> STATE);			/* previous motion */
prev_mving = prev_mot != DTS_STOP;			/* previous moving? */
prev_dir = prev_mot & DTS_DIR;				/* previous dir? */
prev_fnc = DTS_GETFNC (uptr -> STATE);			/* prev function? */
new_mving = (newf & DTA_STSTP) != 0;			/* new moving? */
new_dir = (newf & DTA_FWDRV) != 0;			/* new dir? */
new_fnc = DTA_GETFNC (newf);				/* new function? */

if ((prev_mving | new_mving) == 0) return;		/* stop to stop */

if (new_mving & ~prev_mving) {				/* start? */
	if (dt_setpos (uptr)) return;			/* update pos */
	sim_cancel (uptr);				/* stop current */
	sim_activate (uptr, dt_actime);			/* schedule accel */
	DTS_SETSTA (DTS_ACCF | new_dir, 0);		/* state = accel */
	DTS_SET2ND (DTS_ATSF | new_dir, new_fnc);	/* next = fnc */
	return;  }

if (prev_mving & ~new_mving) {				/* stop? */
	if ((prev_mot & ~DTS_DIR) != DTS_DECF) {	/* !already stopping? */
		if (dt_setpos (uptr)) return;		/* update pos */
		sim_cancel (uptr);			/* stop current */
		sim_activate (uptr, dt_dctime);  }	/* schedule decel */
	DTS_SETSTA (DTS_DECF | prev_dir, 0);		/* state = decel */
	return;  }

if (prev_dir ^ new_dir) {				/* dir chg? */
	if ((prev_mot & ~DTS_DIR) != DTS_DECF) {	/* !already stopping? */
		if (dt_setpos (uptr)) return;  		/* update pos */
		sim_cancel (uptr);			/* stop current */
		sim_activate (uptr, dt_dctime);  }	/* schedule decel */
	DTS_SETSTA (DTS_DECF | prev_dir, 0);		/* state = decel */
	DTS_SET2ND (DTS_ACCF | new_dir, 0);		/* next = accel */
	DTS_SET3RD (DTS_ATSF | new_dir, new_fnc);	/* next next = fnc */
	return;  }

if (prev_mot < DTS_ACCF) {				/* not accel/at speed? */
	if (dt_setpos (uptr)) return;			/* update pos */
	sim_cancel (uptr);				/* cancel cur */
	sim_activate (uptr, dt_actime);			/* schedule accel */
	DTS_SETSTA (DTS_ACCF | new_dir, 0);		/* state = accel */
	DTS_SET2ND (DTS_ATSF | new_dir, new_fnc);	/* next = fnc */
	return;  }

if (prev_mot < DTS_ATSF) {				/* not at speed? */
	DTS_SET2ND (DTS_ATSF | new_dir, new_fnc);	/* next = fnc */
	return;  }

dt_newfnc (uptr, DTS_STA (DTS_ATSF | new_dir, new_fnc));/* state = fnc */
return;	
}

/* Schedule new DECtape function

   This routine is only called if
   - the selected unit is attached
   - the selected unit is at speed (forward or backward)

   This routine
   - updates the selected unit's position
   - updates the selected unit's state
   - schedules the new operation
*/

void dt_newfnc (UNIT *uptr, int32 newsta)
{
int32 fnc, dir, blk, unum, relpos, newpos;
uint32 oldpos;

oldpos = uptr -> pos;					/* save old pos */
if (dt_setpos (uptr)) return;				/* update pos */
uptr -> STATE = newsta;					/* update state */
fnc = DTS_GETFNC (uptr -> STATE);			/* set variables */
dir = DTS_GETMOT (uptr -> STATE) & DTS_DIR;
unum = uptr - dt_dev.units;
if (oldpos == uptr -> pos)				/* bump pos */
	uptr -> pos = uptr -> pos + (dir? -1: 1);
blk = DT_LIN2BL (uptr -> pos, uptr);

if (dir? DT_QREZ (uptr): DT_QFEZ (uptr)) {		/* wrong ez? */
	dt_seterr (uptr, DTB_END);			/* set ez flag, stop */
	return;  }
sim_cancel (uptr);					/* cancel cur op */
dt_substate = DTO_SOB;					/* substate = block start */
switch (fnc) {						/* case function */
case DTS_OFR:						/* off reel */
	if (dir) newpos = -1000;			/* rev? < start */
	else newpos = DTU_FWDEZ (uptr) + DT_EZLIN + 1000;	/* fwd? > end */
	break;
case FNC_MOVE:						/* move */
	dt_schedez (uptr, dir);				/* sched end zone */
	if (dt_log & LOG_MS) printf ("[DT%d: moving %s]\n", unum, (dir?
		"backward": "forward"));
	return;						/* done */
case FNC_SRCH:						/* search */
	if (dir) newpos = DT_BLK2LN ((DT_QFEZ (uptr)?
		DTU_TSIZE (uptr): blk), uptr) - DT_BLKLN - DT_WSIZE;
	else newpos = DT_BLK2LN ((DT_QREZ (uptr)?
		0: blk + 1), uptr) + DT_BLKLN + (DT_WSIZE - 1);
	if (dt_log & LOG_MS) printf ("[DT%d: searching %s]\n", unum,
		(dir? "backward": "forward"));
	break;
case FNC_WRIT:						/* write */
case FNC_READ:						/* read */
case FNC_RALL:						/* read all */
case FNC_WALL:						/* write all */
	if (DT_QEZ (uptr)) {				/* in "ok" end zone? */
		if (dir) newpos = DTU_FWDEZ (uptr) - DT_HTLIN - DT_WSIZE;
		else newpos = DT_EZLIN + DT_HTLIN + (DT_WSIZE - 1);
		break;  }
	relpos = DT_LIN2OF (uptr -> pos, uptr);		/* cur pos in blk */
	if ((relpos >= DT_HTLIN) &&			/* in data zone? */
	    (relpos < (DTU_LPERB (uptr) - DT_HTLIN))) {
		dt_seterr (uptr, DTB_SEL);
		return;  }
	if (dir) newpos = DT_BLK2LN (((relpos >= (DTU_LPERB (uptr) - DT_HTLIN))?
		blk + 1: blk), uptr) - DT_HTLIN - DT_WSIZE;
	else newpos = DT_BLK2LN (((relpos < DT_HTLIN)?
		blk: blk + 1), uptr) + DT_HTLIN + (DT_WSIZE - 1);
	break;
default:
	dt_seterr (uptr, DTB_SEL);			/* bad state */
	return;  }
sim_activate (uptr, ABS (newpos - ((int32) uptr -> pos)) * dt_ltime);
return;
}

/* Update DECtape position

   DECtape motion is modeled as a constant velocity, with linear
   acceleration and deceleration.  The motion equations are as follows:

	t	=	time since operation started
	tmax	=	time for operation (accel, decel only)
	v	=	at speed velocity in lines (= 1/dt_ltime)

   Then:
	at speed dist =	t * v
	accel dist = (t^2 * v) / (2 * tmax)
	decel dist = (((2 * t * tmax) - t^2) * v) / (2 * tmax)

   This routine uses the relative (integer) time, rather than the absolute
   (floating point) time, to allow save and restore of the start times.
*/

t_bool dt_setpos (UNIT *uptr)
{
uint32 new_time, ut, ulin, udelt;
int32 mot = DTS_GETMOT (uptr -> STATE);
int32 unum, delta;

new_time = sim_grtime ();				/* current time */
ut = new_time - uptr -> LASTT;				/* elapsed time */
if (ut == 0) return FALSE;				/* no time gone? exit */
uptr -> LASTT = new_time;				/* update last time */
switch (mot & ~DTS_DIR) {				/* case on motion */
case DTS_STOP:						/* stop */
	delta = 0;
	break;
case DTS_DECF:						/* slowing */
	ulin = ut / (uint32) dt_ltime; udelt = dt_dctime / dt_ltime;
	delta = ((ulin * udelt * 2) - (ulin * ulin)) / (2 * udelt);
	break;
case DTS_ACCF:						/* accelerating */
	ulin = ut / (uint32) dt_ltime; udelt = dt_actime / dt_ltime;
	delta = (ulin * ulin) / (2 * udelt);
	break;
case DTS_ATSF:						/* at speed */
	delta = ut / (uint32) dt_ltime;
	break;  }
if (mot & DTS_DIR) uptr -> pos = uptr -> pos - delta;	/* update pos */
else uptr -> pos = uptr -> pos + delta;
if ((uptr -> pos < 0) ||
    (uptr -> pos > ((uint32) (DTU_FWDEZ (uptr) + DT_EZLIN)))) {
	detach_unit (uptr);				/* off reel? */
	uptr -> STATE = uptr -> pos = 0;
	unum = uptr - dt_dev.units;
	if (unum == DTA_GETUNIT (dtsa))			/* if selected, */
		dt_seterr (uptr, DTB_SEL);		/* error */
	return TRUE;  }
return FALSE;
}

/* Unit service

   Unit must be attached, detach cancels operation
*/

t_stat dt_svc (UNIT *uptr)
{
int32 mot = DTS_GETMOT (uptr -> STATE);
int32 dir = mot & DTS_DIR;
int32 fnc = DTS_GETFNC (uptr -> STATE);
int16 *bptr = uptr -> filebuf;
int32 unum = uptr - dt_dev.units;
int32 blk, wrd, ma, relpos, dat;
t_addr ba;

/* Motion cases

   Decelerating - if next state != stopped, must be accel reverse
   Accelerating - next state must be @speed, schedule function
   At speed - do functional processing
*/

switch (mot) {
case DTS_DECF: case DTS_DECR:				/* decelerating */
	if (dt_setpos (uptr)) return SCPE_OK;		/* update pos */
	uptr -> STATE = DTS_NXTSTA (uptr -> STATE);	/* advance state */
	if (uptr -> STATE)				/* not stopped? */
		sim_activate (uptr, dt_actime);		/* must be reversing */
	return SCPE_OK;
case DTS_ACCF: case DTS_ACCR:				/* accelerating */
	dt_newfnc (uptr, DTS_NXTSTA (uptr -> STATE));	/* adv state, sched */
	return SCPE_OK;
case DTS_ATSF: case DTS_ATSR:				/* at speed */
	break;						/* check function */
default:						/* other */
	dt_seterr (uptr, DTB_SEL);			/* state error */
	return SCPE_OK;  }

/* Functional cases

   Move - must be at end zone
   Search - transfer block number, schedule next block
   Off reel - detach unit (it must be deselected)
*/

if (dt_setpos (uptr)) return SCPE_OK;			/* update pos */
if (DT_QEZ (uptr)) {					/* in end zone? */
	dt_seterr (uptr, DTB_END);			/* end zone error */
	return SCPE_OK;  }
blk = DT_LIN2BL (uptr -> pos, uptr);			/* get block # */
switch (fnc) {						/* at speed, check fnc */
case FNC_MOVE:						/* move */
	dt_seterr (uptr, DTB_END);			/* end zone error */
	return SCPE_OK;
case FNC_SRCH:						/* search */
	if (dtsb & DTB_DTF) {				/* DTF set? */
		dt_seterr (uptr, DTB_TIM);		/* timing error */
		return SCPE_OK;  }
	sim_activate (uptr, DTU_LPERB (uptr) * dt_ltime);/* sched next block */
	M[DT_WC] = (M[DT_WC] + 1) & 07777;		/* incr word cnt */
	ma = DTB_GETMEX (dtsb) | M[DT_CA];		/* get mem addr */
	if (MEM_ADDR_OK (ma)) M[ma] = blk & 07777;	/* store block # */
	if (((dtsa & DTA_MODE) == 0) || (M[DT_WC] == 0))
		dtsb = dtsb | DTB_DTF;			/* set DTF */
	break;
case DTS_OFR:						/* off reel */
	detach_unit (uptr);				/* must be deselected */
	uptr -> STATE = uptr -> pos = 0;		/* no visible action */
	break;

/* Read has four subcases

   Start of block, not wc ovf - check that DTF is clear, otherwise normal
   Normal - increment MA, WC, copy word from tape to memory
	if read dir != write dir, bits must be scrambled
	if wc overflow, next state is wc overflow
	if end of block, possibly set DTF, next state is start of block
   Wc ovf, not start of block - 
	if end of block, possibly set DTF, next state is start of block
   Wc ovf, start of block - if end of block reached, timing error,
	otherwise, continue to next word
*/

case FNC_READ:						/* read */
	wrd = DT_LIN2WD (uptr -> pos, uptr);		/* get word # */
	switch (dt_substate) {				/* case on substate */
	case DTO_SOB:					/* start of block */
		if (dtsb & DTB_DTF) {			/* DTF set? */
			dt_seterr (uptr, DTB_TIM);	/* timing error */
			return SCPE_OK;  }
		if ((dt_log & LOG_RW) || ((dt_log & LOG_BL) && (blk == dt_logblk)))
			printf ("[DT%d: reading block %d %s%s\n",
			unum, blk, (dir? "backward": "forward"),
			((dtsa & DTA_MODE)? " continuous]": "]"));
		dt_substate = 0;			/* fall through */
	case 0:						/* normal read */
		M[DT_WC] = (M[DT_WC] + 1) & 07777;	/* incr WC, CA */
		M[DT_CA] = (M[DT_CA] + 1) & 07777;
		ma = DTB_GETMEX (dtsb) | M[DT_CA];	/* get mem addr */
		ba = (blk * DTU_BSIZE (uptr)) + wrd;	/* buffer ptr */
		dat = bptr[ba];				/* get tape word */
		if (dir) dat = dt_comobv (dat);		/* rev? comp obv */
		if (MEM_ADDR_OK (ma)) M[ma] = dat;	/* mem addr legal? */
		if (M[DT_WC] == 0) dt_substate = DTO_WCO;	/* wc ovf? */
	case DTO_WCO:					/* wc ovf, not sob */
		if (wrd != (dir? 0: DTU_BSIZE (uptr) - 1))	/* not last? */
			sim_activate (uptr, DT_WSIZE * dt_ltime);
		else {	dt_substate = dt_substate | DTO_SOB;
			sim_activate (uptr, ((2 * DT_HTLIN) + DT_WSIZE) * dt_ltime);
			if (((dtsa & DTA_MODE) == 0) || (M[DT_WC] == 0))
				dtsb = dtsb | DTB_DTF;  }	/* set DTF */
		break;			
	case DTO_WCO | DTO_SOB:				/* next block */	
		if (wrd == (dir? 0: DTU_BSIZE (uptr)))	/* end of block? */
			dt_seterr (uptr, DTB_TIM);	/* timing error */
		else sim_activate (uptr, DT_WSIZE * dt_ltime);
		break;  }
	break;

/* Write has four subcases

   Start of block, not wc ovf - check that DTF is clear, set block direction
   Normal - increment MA, WC, copy word from memory to tape
	if wc overflow, next state is wc overflow
	if end of block, possibly set DTF, next state is start of block
   Wc ovf, not start of block -
	copy 0 to tape
	if end of block, possibly set DTF, next state is start of block
   Wc ovf, start of block - schedule end zone
*/

case FNC_WRIT:						/* write */
	wrd = DT_LIN2WD (uptr -> pos, uptr);		/* get word # */
	switch (dt_substate) {				/* case on substate */
	case DTO_SOB:					/* start block */
		if (dtsb & DTB_DTF) {			/* DTF set? */
			dt_seterr (uptr, DTB_TIM);	/* timing error */
			return SCPE_OK;  }
		if ((dt_log & LOG_RW) || ((dt_log & LOG_BL) && (blk == dt_logblk)))
			printf ("[DT%d: writing block %d %s%s\n", unum, blk,
			(dir? "backward": "forward"),
			((dtsa & DTA_MODE)? " continuous]": "]"));
		dt_substate = 0;			/* fall through */
	case 0:						/* normal write */
		M[DT_WC] = (M[DT_WC] + 1) & 07777;	/* incr WC, CA */
		M[DT_CA] = (M[DT_CA] + 1) & 07777;
	case DTO_WCO:					/* wc ovflo */
		ma = DTB_GETMEX (dtsb) | M[DT_CA];	/* get mem addr */
		ba = (blk * DTU_BSIZE (uptr)) + wrd;	/* buffer ptr */
		dat = dt_substate? 0: M[ma];		/* get word */
		if (dir) dat = dt_comobv (dat);		/* rev? comp obv */
		bptr[ba] = dat;				/* write word */
		if (ba >= uptr -> hwmark) uptr -> hwmark = ba + 1;
		if (M[DT_WC] == 0) dt_substate = DTO_WCO;
		if (wrd != (dir? 0: DTU_BSIZE (uptr) - 1))	/* not last? */
			sim_activate (uptr, DT_WSIZE * dt_ltime);
		else {	dt_substate = dt_substate | DTO_SOB;
			sim_activate (uptr, ((2 * DT_HTLIN) + DT_WSIZE) * dt_ltime);
			if (((dtsa & DTA_MODE) == 0) || (M[DT_WC] == 0))
				dtsb = dtsb | DTB_DTF;  }	/* set DTF */
		break;			
	case DTO_WCO | DTO_SOB:				/* all done */
		dt_schedez (uptr, dir);			/* sched end zone */
		break;  }
	break;

/* Read all has two subcases

	Not word count overflow - increment MA, WC, copy word from tape to memory
	Word count overflow - schedule end zone
*/

case FNC_RALL:
	switch (dt_substate) {				/* case on substate */
	case 0: case DTO_SOB:				/* read in progress */
		if (dtsb & DTB_DTF) {			/* DTF set? */
			dt_seterr (uptr, DTB_TIM);	/* timing error */
			return SCPE_OK;  }
		relpos = DT_LIN2OF (uptr -> pos, uptr);	/* cur pos in blk */
		M[DT_WC] = (M[DT_WC] + 1) & 07777;	/* incr WC, CA */
		M[DT_CA] = (M[DT_CA] + 1) & 07777;
		ma = DTB_GETMEX (dtsb) | M[DT_CA];	/* get mem addr */
		if ((relpos >= DT_HTLIN) &&		/* in data zone? */
	    	    (relpos < (DTU_LPERB (uptr) - DT_HTLIN))) {
			wrd = DT_LIN2WD (uptr -> pos, uptr);
			ba = (blk * DTU_BSIZE (uptr)) + wrd;
			dat = bptr[ba];			/* get tape word */
			if (dir) dat = dt_comobv (dat);  }	/* rev? comp obv */
		else dat = dt_gethdr (uptr, blk, relpos, dir);	/* get hdr */
		sim_activate (uptr, DT_WSIZE * dt_ltime);
		if (MEM_ADDR_OK (ma)) M[ma] = dat;	/* mem addr legal? */
		if (M[DT_WC] == 0) dt_substate = DTO_WCO;
		if (((dtsa & DTA_MODE) == 0) || (M[DT_WC] == 0))
			dtsb = dtsb | DTB_DTF;		/* set DTF */
		break;
	case DTO_WCO: case DTO_WCO | DTO_SOB:		/* all done */
		dt_schedez (uptr, dir);			/* sched end zone */
		break;  }				/* end case substate */
	break;

/* Write all has two subcases

	Not word count overflow - increment MA, WC, copy word from memory to tape
	Word count overflow - schedule end zone
*/

case FNC_WALL:
	switch (dt_substate) {				/* case on substate */
	case 0: case DTO_SOB:				/* read in progress */
		if (dtsb & DTB_DTF) {			/* DTF set? */
			dt_seterr (uptr, DTB_TIM);	/* timing error */
			return SCPE_OK;  }
		relpos = DT_LIN2OF (uptr -> pos, uptr);	/* cur pos in blk */
		M[DT_WC] = (M[DT_WC] + 1) & 07777;	/* incr WC, CA */
		M[DT_CA] = (M[DT_CA] + 1) & 07777;
		ma = DTB_GETMEX (dtsb) | M[DT_CA];	/* get mem addr */
		if ((relpos >= DT_HTLIN) &&		/* in data zone? */
	    	    (relpos < (DTU_LPERB (uptr) - DT_HTLIN))) {
			dat = M[ma];			/* get mem word */
			if (dir) dat = dt_comobv (dat);
			wrd = DT_LIN2WD (uptr -> pos, uptr);
			ba = (blk * DTU_BSIZE (uptr)) + wrd;
			bptr[ba] = dat;			/* write word */
			if (ba >= uptr -> hwmark) uptr -> hwmark = ba + 1;  }
/*							/* ignore hdr */
		sim_activate (uptr, DT_WSIZE * dt_ltime);
		if (M[DT_WC] == 0) dt_substate = DTO_WCO;
		if (((dtsa & DTA_MODE) == 0) || (M[DT_WC] == 0))
			dtsb = dtsb | DTB_DTF;		/* set DTF */
		break;
	case DTO_WCO: case DTO_WCO | DTO_SOB:		/* all done */
		dt_schedez (uptr, dir);			/* sched end zone */
		break;  }				/* end case substate */
	break;
default:
	dt_seterr (uptr, DTB_SEL);			/* impossible state */
	break;  }
DT_UPDINT;						/* update interrupts */
return SCPE_OK;
}

/* Reading the header is complicated, because 18b words are being parsed
   out 12b at a time.  The sequence of word numbers is directionally
   sensitive

		Forward				Reverse
	Word	Word	Content		Word	Word	Content
	(abs)	(rel)			(abs)	(rel)

	137	8	rev csm'00	6	6	fwd csm'00
	138	9	0000		5	5	0000
	139	10	0000		4	4	0000
	140	11	0000		3	3	0000
	141	12	00'lo rev blk	2	2	00'lo fwd blk
	142	13	hi rev blk	1	1	hi fwd blk
	143	14	0000		0	0	0000
	0	0	0000		143	14	0000
	1	1	0000		142	13	0000
	2	2	hi fwd blk	141	12	hi rev blk
	3	3	lo fwd blk'00	140	11	lo rev blk'00
	4	4	0000		139	10	0000
	5	5	0000		138	9	0000
	6	6	0000		137	8	0000
	7	7	00'fwd csm	136	7	00'rev csm
*/

int32 dt_gethdr (UNIT *uptr, int32 blk, int32 relpos, int32 dir)
{
if (relpos >= DT_HTLIN) relpos = relpos - (DT_WSIZE * DTU_BSIZE (uptr));
if (dir) {						/* reverse */
	switch (relpos / DT_WSIZE) {
	case 6:						/* fwd csum */
		return (dt_comobv (dt_csum (uptr, blk)));
	case 2:						/* lo fwd blk */
		return dt_comobv ((blk & 077) << 6);
	case 1:						/* hi fwd blk */
		return dt_comobv (blk >> 6);
	case 12:					/* hi rev blk */
		return (blk >> 6) & 07777;
	case 11:					/* lo rev blk */
		return ((blk & 077) << 6);
	default:					/* others */
		return 077;  }  }
else {							/* forward */
	switch (relpos / DT_WSIZE) {
	case 8:						/* rev csum */
		return (dt_csum (uptr, blk) << 6);
	case 12:					/* lo rev blk */
		return dt_comobv ((blk & 077) << 6);
	case 13:					/* hi rev blk */
		return dt_comobv (blk >> 6);
	case 2:						/* hi fwd blk */
		return ((blk >> 6) & 07777);
	case 3:						/* lo fwd blk */
		return ((blk & 077) << 6);
	default:					/* others */
		break;  }  }
return 0;
}

/* Utility routines */

/* Set error flag */

void dt_seterr (UNIT *uptr, int32 e)
{
int32 mot = DTS_GETMOT (uptr -> STATE);

dtsa = dtsa & ~DTA_STSTP;				/* clear go */
dtsb = dtsb | DTB_ERF | e;				/* set error flag */
if (mot >= DTS_ACCF) {					/* ~stopped or stopping? */
	sim_cancel (uptr);				/* cancel activity */
	if (dt_setpos (uptr)) return;			/* update position */
	sim_activate (uptr, dt_dctime);			/* sched decel */
	DTS_SETSTA (DTS_DECF | (mot & DTS_DIR), 0);  }	/* state = decel */
DT_UPDINT;
return;
}

/* Schedule end zone */

void dt_schedez (UNIT *uptr, int32 dir)
{
int32 newpos;

if (dir) newpos = DT_EZLIN - DT_WSIZE;			/* rev? rev ez */
else newpos = DTU_FWDEZ (uptr) + DT_WSIZE;		/* fwd? fwd ez */
sim_activate (uptr, ABS (newpos - ((int32) uptr -> pos)) * dt_ltime);
return;
}

/* Complement obverse routine */

int32 dt_comobv (int32 dat)
{
dat = dat ^ 07777;					/* compl obverse */
dat = ((dat >> 9) & 07) | ((dat >> 3) & 070) |
	((dat & 070) << 3) | ((dat & 07) << 9);
return dat;
}

/* Checksum routine */

int32 dt_csum (UNIT *uptr, int32 blk)
{
int16 *bptr = uptr -> filebuf;
int32 ba = blk * DTU_BSIZE (uptr);
int32 i, csum, wrd;

csum = 077;						/* init csum */
for (i = 0; i < DTU_BSIZE (uptr); i++) {		/* loop thru buf */
	wrd = bptr[ba + i] ^ 07777;			/* get ~word */
	csum = csum ^ (wrd >> 6) ^ wrd;  }
return (csum & 077);
}

/* Reset routine */

t_stat dt_reset (DEVICE *dptr)
{
int32 i, prev_mot;
UNIT *uptr;

for (i = 0; i < DT_NUMDR; i++) {			/* stop all activity */
	uptr = dt_dev.units + i;
	if (sim_is_running) {				/* CAF? */
		prev_mot = DTS_GETMOT (uptr -> STATE);	/* get motion */
		if ((prev_mot & ~DTS_DIR) > DTS_DECF) {	/* accel or spd? */
			if (dt_setpos (uptr)) continue;	/* update pos */
			sim_cancel (uptr);
			sim_activate (uptr, dt_dctime); /* sched decel */
			DTS_SETSTA (DTS_DECF | (prev_mot & DTS_DIR), 0);
			}  }
	else {	sim_cancel (uptr);			/* sim reset */
		uptr -> STATE = 0;  
		uptr -> LASTT = sim_grtime ();  }  }
dtsa = dtsb = 0;					/* clear status */
DT_UPDINT;						/* reset interrupt */
return SCPE_OK;
}

/* Bootstrap routine 

   This is actually the 4K disk monitor bootstrap, which also
   works with OS/8.  The reverse is not true - the OS/8 bootstrap
   doesn't work with the disk monitor.
*/

#define BOOT_START	0200
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int))

static const int32 boot_rom[] = {
	07600,			/* 200,	CLA CLL */
	01216,			/*	TAD MVB		; move back */
	04210,			/*	JMS DO		; action */
	01217,			/*	TAD K7577	; addr */
	03620,			/*	DCA I CA */
	01222,			/*	TAD RDF		; read fwd */
	04210,			/*	JMS DO		; action */
	05600,			/*	JMP I 200	; enter boot */
	00000,			/* DO,	0 */
	06766,			/*	DTCA!DTXA	; start tape */
	03621,			/*	DCA I WC	; clear wc */
	06771,			/*	DTSF		; wait */
	05213,			/*	JMP .-1 */
	05610,			/*	JMP I DO */
	00600,			/* MVB,	0600 */
	07577,			/* K7577, 7757 */
	07755,			/* CA,	7755 */
	07754,			/* WC,	7754 */
	00220			/* RF,	0220 */
};

t_stat dt_boot (int32 unitno)
{
int32 i;
extern int32 saved_PC;

if (unitno) return SCPE_ARG;				/* only unit 0 */
dt_unit[unitno].pos = DT_EZLIN;
for (i = 0; i < BOOT_LEN; i++) M[BOOT_START + i] = boot_rom[i];
saved_PC = BOOT_START;
return SCPE_OK;
}

/* Attach routine

   Determine native or PDP11/15 format
   Allocate buffer
   If native, read data into buffer
   If PDP9/11/15, convert 18b data to 12b and read into buffer
*/

t_stat dt_attach (UNIT *uptr, char *cptr)
{
uint32 pdp18b[D18_NBSIZE];
int32 k, p;
int16 *bptr;
t_stat r;
t_addr ba;

uptr -> flags = uptr -> flags | UNIT_8FMT;
r = attach_unit (uptr, cptr);				/* attach */
if (r != SCPE_OK) return r;				/* fail? */
if (sim_switches & SWMASK ('F'))			/* att foreign? */
	uptr -> flags = uptr -> flags & ~UNIT_8FMT;	/* PDP8 = F */
else if (!(sim_switches & SWMASK ('N'))) {		/* autosize? */
	if ((fseek (uptr -> fileref, 0, SEEK_END) == 0) &&
	    (p = ftell (uptr -> fileref)) &&
	    (p == D18_FILSIZ)) uptr -> flags = uptr -> flags & ~UNIT_8FMT;  }
uptr -> capac = DTU_CAPAC (uptr);			/* set capacity */
uptr -> filebuf = calloc (uptr -> capac, sizeof (int16));
if (uptr -> filebuf == NULL) {				/* can't alloc? */
	detach_unit (uptr);
	return SCPE_MEM;  }
printf ("%DT: buffering file in memory\n");
rewind (uptr -> fileref);				/* start of file */
bptr = uptr -> filebuf;					/* file buffer */
if (uptr -> flags & UNIT_8FMT)			/* PDP8? */
	uptr -> hwmark = fxread (uptr -> filebuf, sizeof (int16),
			      uptr -> capac, uptr -> fileref);
else {							/* PDP9/11/15 */
	for (ba = 0; ba < uptr -> capac; ) {		/* loop thru file */
		k = fxread (pdp18b, sizeof (int32), D18_NBSIZE, uptr -> fileref);
		if (k == 0) break;
		for ( ; k < D18_NBSIZE; k++) pdp18b[k] = 0;
		for (k = 0; k < D18_NBSIZE; k = k + 2) {	/* loop thru blk */
			bptr[ba] = (pdp18b[k] >> 6) & 07777;
			bptr[ba + 1] = ((pdp18b[k] & 077) << 6) |
				((pdp18b[k + 1] >> 12) & 077);
			bptr[ba + 2] = pdp18b[k + 1] & 07777;
			ba = ba + 3;  }			/* end blk loop */
		}					/* end file loop */
	uptr -> hwmark = ba;  }				/* end else */
uptr -> flags = uptr -> flags | UNIT_BUF;		/* set buf flag */
uptr -> pos = DT_EZLIN;					/* beyond leader */
uptr -> LASTT = sim_grtime ();				/* last pos update */
return SCPE_OK;
}

/* Detach routine

   Cancel in progress operation
   If native, write buffer to file
   If PDP9/11/15, convert 12b buffer to 18b and write to file
   Deallocate buffer
*/

t_stat dt_detach (UNIT* uptr)
{
uint32 pdp18b[D18_NBSIZE];
int32 k;
int32 unum = uptr - dt_dev.units;
int16 *bptr;
t_addr ba;

if (!(uptr -> flags & UNIT_ATT)) return SCPE_OK;
if (sim_is_active (uptr)) {
	sim_cancel (uptr);
	if ((unum == DTA_GETUNIT (dtsa)) && (dtsa & DTA_STSTP)) {
		dtsb = dtsb | DTB_ERF | DTB_SEL | DTB_DTF;
		DT_UPDINT;  }
	uptr -> STATE = uptr -> pos = 0;  }
if (uptr -> hwmark) {					/* any data? */
	printf ("DT: writing buffer to file\n");
	rewind (uptr -> fileref);			/* start of file */
	bptr = uptr -> filebuf;				/* file buffer */
	if (uptr -> flags & UNIT_8FMT)		/* PDP8? */
		fxwrite (uptr -> filebuf, sizeof (int16),	/* write file */
		      uptr -> hwmark, uptr -> fileref);
	else {						/* PDP9/11/15 */
		for (ba = 0; ba < uptr -> hwmark; ) {	/* loop thru buf */
			for (k = 0; k < D18_NBSIZE; k = k + 2) {
				pdp18b[k] = ((uint32) (bptr[ba] & 07777) << 6) |
					((uint32) (bptr[ba + 1] >> 6) & 077);
				pdp18b[k + 1] = ((uint32) (bptr[ba + 1] & 077) << 12) |
					((uint32) (bptr[ba + 2] & 07777));
				ba = ba + 3;  }		/* end loop blk */
			fxwrite (pdp18b, sizeof (int32),
				 D18_NBSIZE, uptr -> fileref);
			}				/* end loop buf */
		}					/* end else */
	if (ferror (uptr -> fileref)) perror ("I/O error");
	}						/* end if hwmark */
free (uptr -> filebuf);					/* release buf */
uptr -> flags = uptr -> flags & ~UNIT_BUF;		/* clear buf flag */
uptr -> filebuf = NULL;					/* clear buf ptr */
uptr -> flags = uptr -> flags | UNIT_8FMT;		/* default fmt */
uptr -> capac = DT_CAPAC;				/* default size */
return detach_unit (uptr);
}
