/* pdp11_tc.c: PDP-11 DECtape simulator

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

   tc		TC11/TU56 DECtape

   15-Sep-01	RMS 	Integrated debug logging
   27-Sep-01	RMS	Fixed interrupt after stop for RSTS/E
   07-Sep-01	RMS	Revised device disable and interrupt mechanisms
   29-Aug-01	RMS	Added casts to PDP-8 unpack routine
   17-Jul-01	RMS	Moved function prototype
   11-May-01	RMS	Fixed bug in reset
   26-Apr-01	RMS	Added device enable/disable support
   18-Apr-01	RMS	Changed to rewind tape before boot
   16-Mar-01	RMS	Fixed bug in interrupt after stop
   15-Mar-01	RMS	Added 129th word to PDP-8 format

   PDP-11 DECtapes are represented by fixed length data blocks of 18b words.  Two
   tape formats are supported:

	16b/18b/36b		256 words per block
	12b			86 words per block [129 x 12b]

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

#include "pdp11_defs.h"

#define DT_NUMDR	8				/* #drives */
#define DT_M_NUMDR	(DT_NUMDR - 1)
#define UNIT_V_WLK	(UNIT_V_UF + 0)			/* write locked */
#define UNIT_WLK	1 << UNIT_V_WLK
#define UNIT_V_8FMT	(UNIT_V_UF + 1)			/* 12b format */
#define UNIT_8FMT	(1 << UNIT_V_8FMT)
#define UNIT_W_UF	3				/* saved flag width */
#define STATE		u3				/* unit state */
#define LASTT		u4				/* last time update */

/* System independent DECtape constants */

#define DT_EZLIN	36000				/* end zone length */
#define DT_HTLIN	30				/* header/trailer lines */
#define DT_BLKLN	6				/* blk no line in h/t */
#define DT_CSMLN	24				/* checksum line in h/t */
#define DT_HTWRD	(DT_HTLIN / DT_WSIZE)		/* header/trailer words */
#define DT_BLKWD	(DT_BLKLN / DT_WSIZE)		/* blk no word in h/t */
#define DT_CSMWD	(DT_CSMLN / DT_WSIZE)		/* checksum word in h/t */

/* 16b, 18b, 36b DECtape constants */

#define D18_WSIZE	6				/* word size in lines */
#define D18_BSIZE	256				/* block size in 18b */
#define D18_TSIZE	578				/* tape size */
#define D18_LPERB	(DT_HTLIN + (D18_BSIZE * DT_WSIZE) + DT_HTLIN)
#define D18_FWDEZ	(DT_EZLIN + (D18_LPERB * D18_TSIZE))
#define D18_CAPAC	(D18_TSIZE * D18_BSIZE)		/* tape capacity */

/* 12b DECtape constants */

#define D8_WSIZE	4				/* word size in lines */
#define D8_BSIZE	86				/* block size in 18b */
#define D8_TSIZE	1474				/* tape size */
#define D8_LPERB	(DT_HTLIN + (D8_BSIZE * DT_WSIZE) + DT_HTLIN)
#define D8_FWDEZ	(DT_EZLIN + (D8_LPERB * D8_TSIZE))
#define D8_CAPAC	(D8_TSIZE * D8_BSIZE)		/* tape capacity */

#define D8_NBSIZE	((D8_BSIZE * D18_WSIZE) / D8_WSIZE)
#define D8_FILSIZ	(D8_NBSIZE * D8_TSIZE * sizeof (int16))

/* This controller */

#define DT_CAPAC	D18_CAPAC			/* default */
#define DT_WSIZE	D18_WSIZE

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

/* TCST - 177340 - status register */

#define STA_END		0100000				/* end zone */
#define STA_PAR		0040000				/* parity err */
#define STA_MRK		0020000				/* mark trk err */
#define STA_ILO		0010000				/* illegal op */
#define STA_SEL		0004000				/* select err */
#define STA_BLKM	0002000				/* block miss err */
#define STA_DATM	0001000				/* data miss err */
#define STA_NXM		0000400				/* nx mem err */
#define STA_UPS		0000200				/* up to speed */
#define STA_V_XD	0				/* extended data */
#define STA_M_XD	03
#define STA_ALLERR	(STA_END | STA_PAR | STA_MRK | STA_ILO | \
			 STA_SEL | STA_BLKM | STA_DATM | STA_NXM )
#define STA_RWERR	(STA_END | STA_PAR | STA_MRK | \
			 STA_BLKM | STA_DATM | STA_NXM )
#define STA_RW		0000003
#define STA_GETXD(x)	(((x) >> STA_V_XD) & STA_M_XD)

/* TCCM - 177342 - command register */

/* #define CSR_ERR 	0100000 */
#define CSR_MNT		0020000				/* maint (unimpl) */
#define CSR_INH		0010000				/* delay inhibit */
#define CSR_DIR		0004000				/* reverse */
#define CSR_V_UNIT	8				/* unit select */
#define CSR_M_UNIT	07
#define CSR_UNIT	(CSR_M_UNIT << CSR_V_UNIT)
/* #define CSR_DONE 	0000200 */
/* #define CSR_IE 	0000100 */
#define CSR_V_MEX	4				/* mem extension */
#define CSR_M_MEX	03
#define CSR_MEX		(CSR_M_MEX << CSR_V_MEX)
#define CSR_V_FNC	1				/* function */
#define CSR_M_FNC	07
#define  FNC_STOP	 00				/* stop all */
#define  FNC_SRCH	 01				/* search */
#define  FNC_READ	 02				/* read */
#define  FNC_RALL	 03				/* read all */
#define  FNC_SSEL	 04				/* stop selected */
#define  FNC_WMRK	 05				/* write */
#define  FNC_WRIT	 06				/* write all */
#define  FNC_WALL	 07				/* write timing */
/* define CSR_GO 	0000001 */
#define CSR_RW		0117576				/* read/write */

#define CSR_GETUNIT(x)	(((x) >> CSR_V_UNIT) & CSR_M_UNIT)
#define CSR_GETMEX(x)	(((x) >> CSR_V_MEX) & CSR_M_MEX)
#define CSR_GETFNC(x)	(((x) >> CSR_V_FNC) & CSR_M_FNC)
#define CSR_INCMEX(x)	(((x) & ~CSR_MEX) | (((x) + (1 << CSR_V_MEX)) & CSR_MEX))

/* TCWC - 177344 - word count */

/* TCBA - 177346 - bus address */

/* TCDT - 177350 - data */

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
#define  DTS_OFR	FNC_WMRK			/* "off reel" */
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

/* Logging */

#define LOG_MS		001				/* move, search */
#define LOG_RW		002				/* read, write */
#define LOG_RA		004				/* read all */
#define LOG_BL		010				/* block # lblk */

#define DT_SETDONE	tccm = tccm | CSR_DONE; \
			if (tccm & CSR_IE) SET_INT (DTA)
#define DT_CLRDONE	tccm = tccm & ~CSR_DONE; \
			CLR_INT (DTA)
#define ABS(x)		(((x) < 0)? (-(x)): (x))

extern uint16 *M;					/* memory */
extern int32 int_req[IPL_HLVL];
extern UNIT cpu_unit;
extern int32 sim_switches;
extern int32 pdp11_log;
extern FILE *sim_log;
int32 tcst = 0;						/* status */
int32 tccm = 0;						/* command */
int32 tcwc = 0;						/* word count */
int32 tcba = 0;						/* bus address */
int32 tcdt = 0;						/* data */
int32 dt_ctime = 100;					/* fast cmd time */
int32 dt_ltime = 12;					/* interline time */
int32 dt_actime = 54000;				/* accel time */
int32 dt_dctime = 72000;				/* decel time */
int32 dt_substate = 0;
int32 dt_logblk = 0;
int32 dt_enb = 1;					/* device enable */

t_stat dt_svc (UNIT *uptr);
t_stat dt_svcdone (UNIT *uptr);
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
void dt_stopunit (UNIT *uptr);
int32 dt_comobv (int32 val);
int32 dt_csum (UNIT *uptr, int32 blk);
int32 dt_gethdr (UNIT *uptr, int32 blk, int32 relpos);
extern int32 sim_is_running;

/* DT data structures

   dt_dev	DT device descriptor
   dt_unit	DT unit list
   dt_reg	DT register list
   dt_mod	DT modifier list
*/

UNIT dt_unit[] = {
	{ UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DT_CAPAC) },
	{ UDATA (&dt_svcdone, UNIT_DIS, 0) }  };

#define DT_TIMER	(DT_NUMDR)

REG dt_reg[] = {
	{ ORDATA (TCST, tcst, 16) },
	{ ORDATA (TCCM, tccm, 16) },
	{ ORDATA (TCWC, tcwc, 16) },
	{ ORDATA (TCBA, tcba, 16) },
	{ ORDATA (TCDT, tcdt, 16) },
	{ FLDATA (INT, IREQ (DTA), INT_V_DTA) },
	{ FLDATA (ERR, tccm, CSR_V_ERR) },
	{ FLDATA (DONE, tccm, CSR_V_DONE) },
	{ FLDATA (IE, tccm, CSR_V_DONE) },
	{ DRDATA (CTIME, dt_ctime, 31), REG_NZ },
	{ DRDATA (LTIME, dt_ltime, 31), REG_NZ },
	{ DRDATA (ACTIME, dt_actime, 31), REG_NZ },
	{ DRDATA (DCTIME, dt_dctime, 31), REG_NZ },
	{ ORDATA (SUBSTATE, dt_substate, 1) },
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
	{ FLDATA (*DEVENB, dt_enb, 0), REG_HRO },
	{ NULL }  };

MTAB dt_mod[] = {
	{ UNIT_WLK, 0, "write enabled", "ENABLED", NULL },
	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL }, 
	{ UNIT_8FMT, 0, "16b/18b", NULL, NULL },
	{ UNIT_8FMT, UNIT_8FMT, "12b", NULL, NULL },
	{ 0 }  };

DEVICE dt_dev = {
	"TC", dt_unit, dt_reg, dt_mod,
	DT_NUMDR + 1, 8, 24, 1, 8, 18,
	NULL, NULL, &dt_reset,
	&dt_boot, &dt_attach, &dt_detach };

/* IO dispatch routines, I/O addresses 17777340 - 17777350 */

t_stat dt_rd (int32 *data, int32 PA, int32 access)
{
int32 j, unum, mot, fnc;

j = (PA >> 1) & 017;					/* get reg offset */
unum = CSR_GETUNIT (tccm);				/* get drive */
switch (j) {
case 000:						/* TCST */
	mot = DTS_GETMOT (dt_unit[unum].STATE);		/* get motion */
	if (mot >= DTS_ATSF) tcst = tcst | STA_UPS;	/* set/clr speed */
	else tcst = tcst & ~STA_UPS;
	*data = tcst;
	break;
case 001:						/* TCCM */
	if (tcst & STA_ALLERR) tccm = tccm | CSR_ERR;	/* set/clr error */
	else tccm = tccm & ~CSR_ERR;
	*data = tccm;
	break;
case 002:						/* TCWC */
	*data = tcwc;
	break;
case 003:						/* TCBA */
	*data = tcba;
	break;
case 004:						/* TCDT */
	fnc = DTS_GETFNC (dt_unit[unum].STATE);		/* get function */
	if (fnc == FNC_RALL) {				/* read all? */
		DT_CLRDONE;  }				/* clear done */
	*data = tcdt;
	break;  }
return SCPE_OK;
}

t_stat dt_wr (int32 data, int32 PA, int32 access)
{
int32 i, j, unum, old_tccm, fnc;
UNIT *uptr;

j = (PA >> 1) & 017;					/* get reg offset */
switch (j) {
case 000:						/* TCST */
	if ((access == WRITEB) && (PA & 1)) break;
	tcst = (tcst & ~STA_RW) | (data & STA_RW);
	break;
case 001:						/* TCCM */
	old_tccm = tccm;				/* save prior */
	if (access == WRITEB) data = (PA & 1)?
		(tccm & 0377) | (data << 8): (tccm & ~0377) | data;
	if ((data & CSR_IE) == 0) CLR_INT (DTA);
	else if ((((tccm & CSR_IE) == 0) && (tccm & CSR_DONE)) ||
		(data & CSR_DONE)) SET_INT (DTA);
	tccm = (tccm & ~CSR_RW) | (data & CSR_RW);
	if ((data & CSR_GO) && (tccm & CSR_DONE)) {	/* new cmd? */
		tcst = tcst & ~STA_ALLERR;		/* clear errors */
		tccm = tccm & ~(CSR_ERR | CSR_DONE);	/* clear done, err */
		CLR_INT (DTA);				/* clear int */
		if ((old_tccm ^ tccm) & CSR_UNIT) dt_deselect (old_tccm);
		unum = CSR_GETUNIT (tccm);		/* get drive */
		fnc = CSR_GETFNC (tccm);		/* get function */
		if (fnc == FNC_STOP) {			/* stop all? */
			sim_activate (&dt_dev.units[DT_TIMER], dt_ctime);
			for (i = 0; i < DT_NUMDR; i++)
				dt_stopunit (dt_dev.units + i);	/* stop unit */
			break;  }
		uptr = dt_dev.units + unum;
		if (uptr -> flags & UNIT_DIS)		/* disabled? */
			dt_seterr (uptr, STA_SEL);	/* select err */
		if ((fnc == FNC_WMRK) ||		/* write mark? */
		   ((fnc == FNC_WALL) && (uptr -> flags & UNIT_WLK)) ||
		   ((fnc == FNC_WRIT) && (uptr -> flags & UNIT_WLK)))
			dt_seterr (uptr, STA_ILO);	/* illegal op */
		if (!(tccm & CSR_ERR)) dt_newsa (tccm);  }
	else if ((tccm & CSR_ERR) == 0) {		/* clear err? */
		tcst = tcst & ~STA_RWERR;
		if (tcst & STA_ALLERR) tccm = tccm | CSR_ERR;  }
	break;
case 002:						/* TCWC */
	tcwc = data;					/* word write only! */
	break;
case 003:						/* TCBA */
	tcba = data;					/* word write only! */
	break;		
case 004:						/* TCDT */
	unum = CSR_GETUNIT (tccm);			/* get drive */
	fnc = DTS_GETFNC (dt_unit[unum].STATE);		/* get function */
	if (fnc == FNC_WALL) {				/* write all? */
		DT_CLRDONE;  }				/* clear done */
	tcdt = data;					/* word write only! */
	break;  }
return SCPE_OK;
}

/* Unit deselect */

void dt_deselect (int32 oldf)
{
int32 old_unit = CSR_GETUNIT (oldf);
UNIT *uptr = dt_dev.units + old_unit;
int32 old_mot = DTS_GETMOT (uptr -> STATE);

if (old_mot >= DTS_ATSF)				/* at speed? */
	dt_newfnc (uptr, DTS_STA (old_mot, DTS_OFR));
else if (old_mot >= DTS_ACCF)				/* accelerating? */
	DTS_SET2ND (DTS_ATSF | (old_mot & DTS_DIR), DTS_OFR);
return;  }

/* New operation

   1. If function = stop
	- if not already stopped or decelerating, schedule deceleration
	- schedule command completion
   2. If change in direction,
	- if not decelerating, schedule deceleration
	- set accelerating (other dir) as next state
	- set function as next next state
   3. If not accelerating or at speed,
	- schedule acceleration
	- set function as next state
   4. If not yet at speed,
	- set function as next state
   5. If at speed,
	- set function as current state, schedule function
*/

void dt_newsa (int32 newf)
{
int32 new_unit, prev_mot, prev_fnc, new_fnc;
int32 prev_dir, new_dir;
UNIT *uptr;

new_unit = CSR_GETUNIT (newf);				/* new, old units */
uptr = dt_dev.units + new_unit;
if ((uptr -> flags & UNIT_ATT) == 0) {			/* new unit attached? */
	dt_seterr (uptr, STA_SEL);			/* no, error */
	return;  }
prev_mot = DTS_GETMOT (uptr -> STATE);			/* previous motion */
prev_fnc = DTS_GETFNC (uptr -> STATE);			/* prev function */
prev_dir = prev_mot & DTS_DIR;				/* previous dir */
new_fnc = CSR_GETFNC (newf);				/* new function */
new_dir = (newf & CSR_DIR) != 0;			/* new di? */

if (new_fnc == FNC_SSEL) {				/* stop unit? */
	sim_activate (&dt_dev.units[DT_TIMER], dt_ctime);	/* sched done */
	dt_stopunit (uptr);				/* stop unit */
	return;  }

if (prev_mot == DTS_STOP) {				/* start? */
	if (dt_setpos (uptr)) return;			/* update pos */
	sim_cancel (uptr);				/* stop current */
	sim_activate (uptr, dt_actime);			/* schedule accel */
	DTS_SETSTA (DTS_ACCF | new_dir, 0);		/* state = accel */
	DTS_SET2ND (DTS_ATSF | new_dir, new_fnc);	/* next = fnc */
	return;  }

if (prev_dir ^ new_dir) {				/* dir chg? */
	dt_stopunit (uptr);				/* stop unit */
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
if (oldpos == uptr -> pos)
	uptr -> pos = uptr -> pos + (dir? -1: 1);
blk = DT_LIN2BL (uptr -> pos, uptr);

if (dir? DT_QREZ (uptr): DT_QFEZ (uptr)) {		/* wrong ez? */
	dt_seterr (uptr, STA_END);			/* set ez flag, stop */
	return;  }
dt_substate = 0;					/* substate = normal */
sim_cancel (uptr);					/* cancel cur op */
switch (fnc) {						/* case function */
case DTS_OFR:						/* off reel */
	if (dir) newpos = -1000;			/* rev? < start */
	else newpos = DTU_FWDEZ (uptr) + DT_EZLIN + 1000;	/* fwd? > end */
	break;
case FNC_SRCH:						/* search */
	if (dir) newpos = DT_BLK2LN ((DT_QFEZ (uptr)?
		DTU_TSIZE (uptr): blk), uptr) - DT_BLKLN - DT_WSIZE;
	else newpos = DT_BLK2LN ((DT_QREZ (uptr)?
		0: blk + 1), uptr) + DT_BLKLN + (DT_WSIZE - 1);
	if (DBG_LOG (LOG_TC_MS)) fprintf (sim_log, ">>DT%d: searching %s\n",
		unum, (dir? "backward": "forward"));
	break;
case FNC_WRIT:						/* write */
case FNC_READ:						/* read */
	if (DT_QEZ (uptr)) {				/* in "ok" end zone? */
		if (dir) newpos = DTU_FWDEZ (uptr) - DT_HTLIN - DT_WSIZE;
		else newpos = DT_EZLIN + DT_HTLIN + (DT_WSIZE - 1);
		break;  }
	relpos = DT_LIN2OF (uptr -> pos, uptr);		/* cur pos in blk */
	if ((relpos >= DT_HTLIN) &&			/* in data zone? */
	    (relpos < (DTU_LPERB (uptr) - DT_HTLIN))) {
		dt_seterr (uptr, STA_BLKM);
		return;  }
	if (dir) newpos = DT_BLK2LN (((relpos >= (DTU_LPERB (uptr) - DT_HTLIN))?
		blk + 1: blk), uptr) - DT_HTLIN - DT_WSIZE;
	else newpos = DT_BLK2LN (((relpos < DT_HTLIN)?
		blk: blk + 1), uptr) + DT_HTLIN + (DT_WSIZE - 1);
	if (DBG_LOG (LOG_TC_RW) || (DBG_LOG (LOG_TC_BL) && (blk == dt_logblk)))
		fprintf (sim_log, ">>DT%d: %s block %d %s\n",
			unum, ((fnc == FNC_READ)? "read": "write"),
			blk, (dir? "backward": "forward"));
	break;
case FNC_RALL:						/* read all */
case FNC_WALL:						/* write all */
	if (DT_QEZ (uptr)) {				/* in "ok" end zone? */
		if (dir) newpos = DTU_FWDEZ (uptr) - DT_WSIZE;
		else newpos = DT_EZLIN + (DT_WSIZE - 1);  }
	else {	relpos = DT_LIN2OF (uptr -> pos, uptr);		/* cur pos in blk */
		if (dir? (relpos < (DTU_LPERB (uptr) - DT_CSMLN)): /* switch in time? */
			 (relpos >= DT_CSMLN)) {
			dt_seterr (uptr, STA_BLKM);
			return;  }
		if (dir) newpos = DT_BLK2LN (blk + 1, uptr) - DT_CSMLN - DT_WSIZE;
		else newpos = DT_BLK2LN (blk, uptr) + DT_CSMLN + (DT_WSIZE - 1);  }
	if (fnc == FNC_WALL) sim_activate 		/* write all? */
		(&dt_dev.units[DT_TIMER], dt_ctime);	/* sched done */
	if (DBG_LOG (LOG_TC_RW) || (DBG_LOG (LOG_TC_BL) && (blk == dt_logblk)))
		fprintf (sim_log, ">>DT%d: read all block %d %s\n",
			unum, blk, (dir? "backward": "forward"));
	break;
default:
	dt_seterr (uptr, STA_SEL);			/* bad state */
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
	if ((unum == CSR_GETUNIT (tccm)) && (CSR_GETFNC (tccm) != FNC_STOP))
		dt_seterr (uptr, STA_SEL);		/* error */
	return TRUE;  }
return FALSE;
}

/* Command timer service after stop - set done */

t_stat dt_svcdone (UNIT *uptr)
{
DT_SETDONE;
return SCPE_OK;
}

/* Unit service

   Unit must be attached, detach cancels operation
*/

t_stat dt_svc (UNIT *uptr)
{
int32 mot = DTS_GETMOT (uptr -> STATE);
int32 dir = mot & DTS_DIR;
int32 fnc = DTS_GETFNC (uptr -> STATE);
int32 *bptr = uptr -> filebuf;
int32 unum = uptr - dt_dev.units;
int32 blk, wrd, relpos, dat;
t_addr ma, ba;

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
	dt_seterr (uptr, STA_SEL);			/* state error */
	return SCPE_OK;  }

/* Functional cases

   Search - transfer block number, schedule next block
   Off reel - detach unit (it must be deselected)
*/

if (dt_setpos (uptr)) return SCPE_OK;			/* update pos */
if (DT_QEZ (uptr)) {					/* in end zone? */
	dt_seterr (uptr, STA_END);			/* end zone error */
	return SCPE_OK;  }
blk = DT_LIN2BL (uptr -> pos, uptr);			/* get block # */

switch (fnc) {						/* at speed, check fnc */
case FNC_SRCH:						/* search */
	tcdt = blk;					/* set block # */
	dt_schedez (uptr, dir);				/* sched end zone */
	DT_SETDONE;					/* set done */
	break;
case DTS_OFR:						/* off reel */
	detach_unit (uptr);				/* must be deselected */
	uptr -> STATE = uptr -> pos = 0;		/* no visible action */
	break;

/* Read

   If wc ovf has not occurred, inc ma, wc and copy word from tape to memory
   If wc ovf, set flag
   If not end of block, schedule next word
   If end of block and not wc ovf, schedule next block
   If end of block and wc ovf, set done, schedule end zone
*/

case FNC_READ:						/* read */
	wrd = DT_LIN2WD (uptr -> pos, uptr);		/* get word # */
	if (!dt_substate) {				/* !wc ovf? */
		tcwc = tcwc & DMASK;			/* incr MA, WC */
		tcba = tcba & DMASK;
		ma = (CSR_GETMEX (tccm) << 16) | tcba;	/* form 18b addr */
		if (ma >= MEMSIZE) {			/* nx mem? */
			dt_seterr (uptr, STA_NXM);
			break;  }
		ba = (blk * DTU_BSIZE (uptr)) + wrd;	/* buffer ptr */
		M[ma >> 1] = tcdt = bptr[ba] & DMASK;	/* read word */
		tcwc = (tcwc + 1) & DMASK;		/* incr MA, WC */
		tcba = (tcba + 2) & DMASK;
		if (tcba <= 1) tccm = CSR_INCMEX (tccm);
		if (tcwc == 0) dt_substate = 1;  }
	if (wrd != (dir? 0: DTU_BSIZE (uptr) - 1))	/* not end blk? */
		sim_activate (uptr, DT_WSIZE * dt_ltime);
	else if (dt_substate) {				/* wc ovf? */
		dt_schedez (uptr, dir);			/* sched end zone */
		DT_SETDONE;  }				/* set done */
	else sim_activate (uptr, ((2 * DT_HTLIN) + DT_WSIZE) * dt_ltime);
	break;			

/* Write

   If wc ovf has not occurred, inc ma, wc
   Copy word from memory (or 0, to fill block) to tape
   If wc ovf, set flag
   If not end of block, schedule next word
   If end of block and not wc ovf, schedule next block
   If end of block and wc ovf, set done, schedule end zone
*/

case FNC_WRIT:						/* write */
	wrd = DT_LIN2WD (uptr -> pos, uptr);		/* get word # */
	if (dt_substate) tcdt = 0;			/* wc ovf? fill */
	else {	ma = (CSR_GETMEX (tccm) << 16) | tcba;	/* form 18b addr */
		if (ma >= MEMSIZE) {			/* nx mem? */
			dt_seterr (uptr, STA_NXM);
			break;  }
		else tcdt = M[ma >> 1];			/* get word */
		tcwc = (tcwc + 1) & DMASK;		/* incr MA, WC */
		tcba = (tcba + 2) & DMASK;
		if (tcba <= 1) tccm = CSR_INCMEX (tccm);  }
	ba = (blk * DTU_BSIZE (uptr)) + wrd;		/* buffer ptr */
	bptr[ba] = tcdt;				/* write word */
	if (ba >= uptr -> hwmark) uptr -> hwmark = ba + 1;
	if (tcwc == 0) dt_substate = 1;
	if (wrd != (dir? 0: DTU_BSIZE (uptr) - 1))	/* not end blk? */
		sim_activate (uptr, DT_WSIZE * dt_ltime);
	else if (dt_substate) {				/* wc ovf? */
		dt_schedez (uptr, dir);			/* sched end zone */
		DT_SETDONE;  }
	else sim_activate (uptr, ((2 * DT_HTLIN) + DT_WSIZE) * dt_ltime);
	break;			

/* Read all - read current header or data word */

case FNC_RALL:
	if (tccm & CSR_DONE) {				/* done set? */
		dt_seterr (uptr, STA_DATM);		/* data miss */
		break;  }
	relpos = DT_LIN2OF (uptr -> pos, uptr);		/* cur pos in blk */
	if ((relpos >= DT_HTLIN) &&			/* in data zone? */
	    	(relpos < (DTU_LPERB (uptr) - DT_HTLIN))) {
		wrd = DT_LIN2WD (uptr -> pos, uptr);
		ba = (blk * DTU_BSIZE (uptr)) + wrd;	/* buffer ptr */
		dat = bptr[ba];  }			/* get tape word */
	else dat = dt_gethdr (uptr, blk, relpos);	/* get hdr */
	if (dir) dat = dt_comobv (dat);			/* rev? comp obv */
	tcdt = dat & DMASK;				/* low 16b */
	tcst = (tcst & ~STA_M_XD) | ((dat >> 16) & STA_M_XD);
	sim_activate (uptr, DT_WSIZE * dt_ltime);
	DT_SETDONE;					/* set done */
	break;

/* Write all - write current header or data word */

case FNC_WALL:
	if (tccm & CSR_DONE) {				/* done set? */
		dt_seterr (uptr, STA_DATM);		/* data miss */
		break;  }
	relpos = DT_LIN2OF (uptr -> pos, uptr);		/* cur pos in blk */
	if ((relpos >= DT_HTLIN) &&			/* in data zone? */
	    	(relpos < (DTU_LPERB (uptr) - DT_HTLIN))) {
		wrd = DT_LIN2WD (uptr -> pos, uptr);
		dat = (STA_GETXD (tcst) << 16) | tcdt;	/* get data word */
		if (dir) dat = dt_comobv (dat);		/* rev? comp obv */
		ba = (blk * DTU_BSIZE (uptr)) + wrd;	/* buffer ptr */
		bptr[ba] = dat;				/* write word */
		if (ba >= uptr -> hwmark) uptr -> hwmark = ba + 1;  }
/*	else						/* ignore hdr */ 
	sim_activate (uptr, DT_WSIZE * dt_ltime);
	DT_SETDONE;					/* set done */
	break;
default:
	dt_seterr (uptr, STA_SEL);			/* impossible state */
	break;  }
return SCPE_OK;
}

/* Utility routines */

/* Set error flag */

void dt_seterr (UNIT *uptr, int32 e)
{
int32 mot = DTS_GETMOT (uptr -> STATE);

tcst = tcst | e;					/* set error flag */
tccm = tccm | CSR_ERR;
if (!(tccm & CSR_DONE)) {				/* not done? */
	DT_SETDONE;  }
if (mot >= DTS_ACCF) {					/* ~stopped or stopping? */
	sim_cancel (uptr);				/* cancel activity */
	if (dt_setpos (uptr)) return;			/* update position */
	sim_activate (uptr, dt_dctime);			/* sched decel */
	DTS_SETSTA (DTS_DECF | (mot & DTS_DIR), 0);  }	/* state = decel */
return;
}

/* Stop unit */

void dt_stopunit (UNIT *uptr)
{
int32 mot = DTS_GETMOT (uptr -> STATE);
int32 dir = mot & DTS_DIR;

if (mot == DTS_STOP) return;				/* already stopped? */
if ((mot & ~DTS_DIR) != DTS_DECF) {			/* !already stopping? */
	if (dt_setpos (uptr)) return;			/* update pos */
	sim_cancel (uptr);				/* stop current */
	sim_activate (uptr, dt_dctime);  }		/* schedule decel */
DTS_SETSTA (DTS_DECF | dir, 0);				/* state = decel */
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

/* Complement obverse routine (18b) */

int32 dt_comobv (int32 dat)
{
dat = dat ^ 0777777;					/* compl obverse */
dat = ((dat >> 15) & 07) | ((dat >> 9) & 070) |
	((dat >> 3) & 0700) | ((dat & 0700) << 3) |
	((dat & 070) << 9) | ((dat & 07) << 15);
return dat;
}

/* Checksum routine */

int32 dt_csum (UNIT *uptr, int32 blk)
{
int32 *bptr = uptr -> filebuf;
int32 ba = blk * DTU_BSIZE (uptr);
int32 i, csum, wrd;

csum = 077;						/* init csum */
for (i = 0; i < DTU_BSIZE (uptr); i++) {		/* loop thru buf */
	wrd = bptr[ba + i] ^ 0777777;			/* get ~word */
	csum = csum ^ (wrd >> 12) ^ (wrd >> 6) ^ wrd;  }
return (csum & 077);
}

/* Get header word (18b) */

int32 dt_gethdr (UNIT *uptr, int32 blk, int32 relpos)
{
int32 wrd = relpos / DT_WSIZE;

if (wrd == DT_BLKWD) return blk;			/* fwd blknum */
if (wrd == (2 * DT_HTWRD + DTU_BSIZE (uptr) - DT_CSMWD - 1))	/* fwd csum */
	return (dt_csum (uptr, blk) << 12);
if (wrd == (2 * DT_HTWRD + DTU_BSIZE (uptr) - DT_BLKWD - 1))	/* rev blkno */
	return dt_comobv (blk);
return 0;						/* all others */
}

/* Reset routine */

t_stat dt_reset (DEVICE *dptr)
{
int32 i, prev_mot;
UNIT *uptr;

for (i = 0; i < DT_NUMDR; i++) {			/* stop all activity */
	uptr = dt_dev.units + i;
	if (sim_is_running) {				/* RESET? */
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
tcst =  tcwc = tcba = tcdt = 0;				/* clear reg */
tccm = CSR_DONE;
CLR_INT (DTA);						/* clear int req */
return SCPE_OK;
}

/* Device bootstrap */

#define BOOT_START 02000				/* start */
#define BOOT_UNIT 02006					/* unit number */
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int32))

static const int32 boot_rom[] = {
	0012706, 0002000,		/* MOV #2000, SP */
	0012700, 0000000,		/* MOV #unit, R0	; unit number */
	0010003,			/* MOV R0, R3 */
	0000303,			/* SWAB R3 */
	0012701, 0177342,		/* MOV #TCCM, R1	; csr */
	0012702, 0004003,		/* RW: MOV #4003, R2	; rev+rnum+go */
	0050302,			/* BIS R3, R2 */
	0010211,			/* MOV R2, (R1)		; load csr */
	0032711, 0100200,		/* BIT #100200, (R1)	; wait */
	0001775,			/* BEQ .-4 */
	0100370,			/* BPL RW		; no err, cont */
	0005737, 0177340,		/* TST TCST		; end zone? */
	0100036,			/* BPL ER		; no, err */
	0012702, 0000003,		/* MOV #3, R2		; rnum+go */
	0050302,			/* BIS R3, R2 */
	0010211,			/* MOV R2, (R1)		; load csr */
	0032711, 0100200,		/* BIT #100200, (R1)	; wait */
	0001775,			/* BEQ .-4 */
	0100426,			/* BMI ER		; err, die */
	0005737, 0177350,		/* TST TCDT		; blk 0? */
	0001023,			/* BNE ER		; no, die */
	0012737, 0177000, 0177344,	/* MOV #-256.*2, TCWC	; load wc */
	0005037, 0177346,		/* CLR TCBA		; clear ba */
	0012702, 0000005,		/* MOV #READ+GO, R2	; read & go */
	0050302,			/* BIS R3, R2 */
	0010211,			/* MOV R2, (R1)		; load csr */
	0005002,			/* CLR R2 */
	0005003,			/* CLR R3 */
	0005004,			/* CLR R4 */
	0012705, 0052104,		/* MOV #"DT, R5 */
	0032711, 0100200,		/* BIT #100200, (R1)	; wait */
	0001775,			/* BEQ .-4 */
	0100401,			/* BMI ER		; err, die */
	0005007,			/* CLR PC */
	0012711, 0000001,		/* ER: MOV #1, (R1)	; stop all */
	0000000				/* HALT */
};

t_stat dt_boot (int32 unitno)
{
int32 i;
extern int32 saved_PC;

dt_unit[unitno].pos = DT_EZLIN;
for (i = 0; i < BOOT_LEN; i++) M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & DT_M_NUMDR;
saved_PC = BOOT_START;
return SCPE_OK;
}

/* Attach routine

   Determine native or PDP8 format
   Allocate buffer
   If PDP8, read 12b format and convert to 18b in buffer
   If native, read data into buffer
*/

t_stat dt_attach (UNIT *uptr, char *cptr)
{
uint16 pdp8b[D8_NBSIZE];
int32 k, p, *bptr;
t_stat r;
t_addr ba;

uptr -> flags = uptr -> flags & ~UNIT_8FMT;
r = attach_unit (uptr, cptr);				/* attach */
if (r != SCPE_OK) return r;				/* fail? */
if (sim_switches & SWMASK ('F'))			/* att foreign? */
	uptr -> flags = uptr -> flags | UNIT_8FMT;	/* PDP8 = T */
else if (!(sim_switches & SWMASK ('N'))) {		/* autosize? */
	if ((fseek (uptr -> fileref, 0, SEEK_END) == 0) &&
	    (p = ftell (uptr -> fileref)) &&
	    (p == D8_FILSIZ)) uptr -> flags = uptr -> flags | UNIT_8FMT;  }
uptr -> capac = DTU_CAPAC (uptr);			/* set capacity */
uptr -> filebuf = calloc (uptr -> capac, sizeof (int32));
if (uptr -> filebuf == NULL) {				/* can't alloc? */
	detach_unit (uptr);
	return SCPE_MEM;  }
printf ("TC: buffering file in memory\n");
rewind (uptr -> fileref);				/* start of file */
if (uptr -> flags & UNIT_8FMT) {			/* PDP-8? */
	bptr = uptr -> filebuf;				/* file buffer */
	for (ba = 0; ba < uptr -> capac; ) {		/* loop thru file */
		k = fxread (pdp8b, sizeof (int16), D8_NBSIZE, uptr -> fileref);
		if (k == 0) break;
		for ( ; k < D8_NBSIZE; k++) pdp8b[k] = 0;
		for (k = 0; k < D8_NBSIZE; k = k + 3) {	/* loop thru blk */
			bptr[ba] = ((uint32) (pdp8b[k] & 07777) << 6) |
				((uint32) (pdp8b[k + 1] >> 6) & 077);
			bptr[ba + 1] = ((pdp8b[k + 1] & 077) << 12) |
				((uint32) (pdp8b[k + 2] & 07777));
			ba = ba + 2;  }			/* end blk loop */
		}					/* end file loop */
	uptr -> hwmark = ba;  }				/* end if */
else uptr -> hwmark = fxread (uptr -> filebuf, sizeof (int32),
			      uptr -> capac, uptr -> fileref);
uptr -> flags = uptr -> flags | UNIT_BUF;		/* set buf flag */
uptr -> pos = DT_EZLIN;					/* beyond leader */
uptr -> LASTT = sim_grtime ();				/* last pos update */
return SCPE_OK;
}

/* Detach routine

   Cancel in progress operation
   If PDP8, convert 18b buffer to 12b and write to file
   If native, write buffer to file
   Deallocate buffer
*/

t_stat dt_detach (UNIT* uptr)
{
uint16 pdp8b[D8_NBSIZE];
int32 k, *bptr;
int32 unum = uptr - dt_dev.units;
t_addr ba;

if (!(uptr -> flags & UNIT_ATT)) return SCPE_OK;
if (sim_is_active (uptr)) {				/* active? cancel op */
	sim_cancel (uptr);
	if ((unum == CSR_GETUNIT (tccm)) && ((tccm & CSR_DONE) == 0)) {
		tcst = tcst | STA_SEL;
		tccm = tccm | CSR_ERR | CSR_DONE;
		if (tccm & CSR_IE) SET_INT (DTA);  }
	uptr -> STATE = uptr -> pos = 0;  }
if (uptr -> hwmark) {					/* any data? */
	printf ("TC: writing buffer to file\n");
	rewind (uptr -> fileref);			/* start of file */
	if (uptr -> flags & UNIT_8FMT) {		/* PDP8? */
		bptr = uptr -> filebuf;			/* file buffer */
		for (ba = 0; ba < uptr -> hwmark; ) {	/* loop thru buf */
			for (k = 0; k < D8_NBSIZE; k = k + 3) {	/* loop blk */
				pdp8b[k] = (bptr[ba] >> 6) & 07777;
				pdp8b[k + 1] = ((bptr[ba] & 077) << 6) |
					((bptr[ba + 1] >> 12) & 077);
				pdp8b[k + 2] = bptr[ba + 1] & 07777;
				ba = ba + 2;  }		/* end loop blk */
		fxwrite (pdp8b, sizeof (int16), D8_NBSIZE, uptr -> fileref);
		if (ferror (uptr -> fileref)) break;  }	/* end loop file */
		}					/* end if PDP8 */
	else fxwrite (uptr -> filebuf, sizeof (int32),	/* write file */
		      uptr -> hwmark, uptr -> fileref);
	if (ferror (uptr -> fileref)) perror ("I/O error");  }	/* end if hwmark */
free (uptr -> filebuf);					/* release buf */
uptr -> flags = uptr -> flags & ~UNIT_BUF;		/* clear buf flag */
uptr -> filebuf = NULL;					/* clear buf ptr */
uptr -> flags = uptr -> flags & ~UNIT_8FMT;		/* default fmt */
uptr -> capac = DT_CAPAC;				/* default size */
return detach_unit (uptr);
}
