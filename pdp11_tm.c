/* pdp11_tm.c: PDP-11 magnetic tape simulator

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

   tm		TM11/TU10 magtape

   18-Oct-01	RMS	Added stub diagnostic register (found by Thord Nilson)
   07-Sep-01	RMS	Revised device disable and interrupt mechanisms
   26-Apr-01	RMS	Added device enable/disable support
   18-Apr-01	RMS	Changed to rewind tape before boot
   14-Apr-99	RMS	Changed t_addr to unsigned
   04-Oct-98	RMS	V2.4 magtape format
   10-May-98	RMS	Fixed bug with non-zero unit operation (from Steven Schultz)
   09-May-98	RMS	Fixed problems in bootstrap (from Steven Schultz)
   10-Apr-98	RMS	Added 2nd block bootstrap (from John Holden,
			University of Sydney)
   31-Jul-97	RMS	Added bootstrap (from Ethan Dicks, Ohio State)
   22-Jan-97	RMS	V2.3 magtape format
   18-Jan-97	RMS	Fixed double interrupt, error flag bugs
   29-Jun-96	RMS	Added unit disable support

   Magnetic tapes are represented as a series of variable 8b records
   of the form:

	32b record length in bytes - exact number
	byte 0
	byte 1
	:
	byte n-2
	byte n-1
	32b record length in bytes - exact number

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a single record length of 0.
   End of tape is two consecutive end of file marks.
*/

#include "pdp11_defs.h"

#define TM_NUMDR	8				/* #drives */
#define UNIT_V_WLK	(UNIT_V_UF + 0)			/* write locked */
#define UNIT_WLK	1 << UNIT_V_WLK
#define UNIT_W_UF	2				/* saved user flags */
#define USTAT		u3				/* unit status */
#define UNUM		u4				/* unit number */
#define DBSIZE		(1 << 16)			/* max data buf */
#define DBMASK		(SBSIZE - 1)

/* Command - tm_cmd */

#define MTC_ERR		(1 << CSR_V_ERR)		/* error */
#define MTC_V_DEN	13				/* density */
#define MTC_M_DEN	03
#define MTC_DEN		(MTC_M_DEN << MTC_V_DEN)
#define MTC_INIT	0010000				/* init */
#define MTC_LPAR	0004000				/* parity select */
#define MTC_V_UNIT	8				/* unit */
#define MTC_M_UNIT	07
#define MTC_UNIT	(MTC_M_UNIT << MTC_V_UNIT)
#define MTC_DONE	(1 << CSR_V_DONE)		/* done */
#define MTC_IE		(1 << CSR_V_IE)			/* interrupt enable */
#define MTC_V_EMA	4				/* ext mem address */
#define MTC_M_EMA	03
#define MTC_EMA		(MTC_M_EMA << MTC_V_EMA)
#define MTC_V_FNC	1				/* function */
#define MTC_M_FNC	07
#define  MTC_UNLOAD	 00
#define  MTC_READ	 01
#define  MTC_WRITE	 02
#define  MTC_WREOF	 03
#define  MTC_SPACEF	 04
#define  MTC_SPACER	 05
#define  MTC_WREXT	 06
#define  MTC_REWIND	 07
#define MTC_FNC		(MTC_M_FNC << MTC_V_FNC)
#define MTC_GO		(1 << CSR_V_GO)			/* go */
#define MTC_RW		(MTC_DEN | MTC_LPAR | MTC_UNIT | MTC_IE | \
			 MTC_EMA | MTC_FNC)
#define GET_EMA(x)	(((x) & MTC_EMA) << (16 - MTC_V_EMA))
#define GET_UNIT(x)	(((x) >> MTC_V_UNIT) & MTC_M_UNIT)
#define GET_FNC(x)	(((x) >> MTC_V_FNC) & MTC_M_FNC)

/* Status - stored in tm_sta or (*) uptr -> USTAT or (+) calculated */

#define STA_ILL		0100000				/* illegal */
#define STA_EOF		0040000				/* *end of file */
#define STA_CRC		0020000				/* CRC error */
#define STA_PAR		0010000				/* parity error */
#define STA_DLT		0004000				/* data late */
#define STA_EOT		0002000				/* *end of tape */
#define STA_RLE		0001000				/* rec lnt error */
#define STA_BAD		0000400				/* bad tape error */
#define STA_NXM		0000200				/* non-existent mem */
#define STA_ONL		0000100				/* *online */
#define STA_BOT		0000040				/* *start of tape */
#define STA_7TK		0000020				/* 7 track */
#define STA_SDN		0000010				/* settle down */
#define STA_WLK		0000004				/* *write locked */
#define STA_REW		0000002				/* *rewinding */
#define STA_TUR		0000001				/* +unit ready */

#define STA_CLR		(STA_7TK | STA_SDN)		/* always clear */
#define STA_DYN		(STA_EOF | STA_EOT | STA_ONL | STA_BOT | \
			 STA_WLK | STA_REW | STA_TUR)	/* kept in USTAT */
#define STA_EFLGS	(STA_ILL | STA_EOF | STA_CRC | STA_PAR | \
			 STA_DLT | STA_EOT | STA_RLE | STA_BAD | STA_NXM)
			 				/* set error */

/* Read lines - tm_rdl */

#define RDL_CLK		0100000				/* 10 Khz clock */

extern uint16 *M;					/* memory */
extern int32 int_req[IPL_HLVL];
extern UNIT cpu_unit;
int32 tm_sta = 0;					/* status register */
int32 tm_cmd = 0;					/* command register */
int32 tm_ca = 0;					/* current address */
int32 tm_bc = 0;					/* byte count */
int32 tm_db = 0;					/* data buffer */
int32 tm_rdl = 0;					/* read lines */
int32 tm_time = 10;					/* record latency */
int32 tm_stopioe = 1;					/* stop on error */
int32 tm_enb = 1;					/* device enable */

t_stat tm_svc (UNIT *uptr);
t_stat tm_reset (DEVICE *dptr);
t_stat tm_attach (UNIT *uptr, char *cptr);
t_stat tm_detach (UNIT *uptr);
t_stat tm_boot (int32 unitno);
void tm_go (UNIT *uptr);
int32 tm_updcsta (UNIT *uptr);
void tm_set_done (void);
t_stat tm_vlock (UNIT *uptr, int32 val);

/* MT data structures

   tm_dev	MT device descriptor
   tm_unit	MT unit list
   tm_reg	MT register list
   tm_mod	MT modifier list
*/

UNIT tm_unit[] = {
	{ UDATA (&tm_svc, UNIT_ATTABLE + UNIT_DISABLE, 0) },
	{ UDATA (&tm_svc, UNIT_ATTABLE + UNIT_DISABLE, 0) },
	{ UDATA (&tm_svc, UNIT_ATTABLE + UNIT_DISABLE, 0) },
	{ UDATA (&tm_svc, UNIT_ATTABLE + UNIT_DISABLE, 0) },
	{ UDATA (&tm_svc, UNIT_ATTABLE + UNIT_DISABLE, 0) },
	{ UDATA (&tm_svc, UNIT_ATTABLE + UNIT_DISABLE, 0) },
	{ UDATA (&tm_svc, UNIT_ATTABLE + UNIT_DISABLE, 0) },
	{ UDATA (&tm_svc, UNIT_ATTABLE + UNIT_DISABLE, 0) }  };

REG tm_reg[] = {
	{ ORDATA (MTS, tm_sta, 16) },
	{ ORDATA (MTC, tm_cmd, 16) },
	{ ORDATA (MTBRC, tm_bc, 16) },
	{ ORDATA (MTCMA, tm_ca, 16) },
	{ ORDATA (MTD, tm_db, 8) },
	{ ORDATA (MTRD, tm_rdl, 16) },
	{ FLDATA (INT, IREQ (TM), INT_V_TM) },
	{ FLDATA (ERR, tm_cmd, CSR_V_ERR) },
	{ FLDATA (DONE, tm_cmd, CSR_V_DONE) },
	{ FLDATA (IE, tm_cmd, CSR_V_IE) },
	{ FLDATA (STOP_IOE, tm_stopioe, 0) },
	{ DRDATA (TIME, tm_time, 24), PV_LEFT },
	{ ORDATA (UST0, tm_unit[0].USTAT, 16) },
	{ ORDATA (UST1, tm_unit[1].USTAT, 16) },
	{ ORDATA (UST2, tm_unit[2].USTAT, 16) },
	{ ORDATA (UST3, tm_unit[3].USTAT, 16) },
	{ ORDATA (UST4, tm_unit[4].USTAT, 16) },
	{ ORDATA (UST5, tm_unit[5].USTAT, 16) },
	{ ORDATA (UST6, tm_unit[6].USTAT, 16) },
	{ ORDATA (UST7, tm_unit[7].USTAT, 16) },
	{ DRDATA (POS0, tm_unit[0].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS1, tm_unit[1].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS2, tm_unit[2].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS3, tm_unit[3].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS4, tm_unit[4].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS5, tm_unit[5].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS6, tm_unit[6].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS7, tm_unit[7].pos, 31), PV_LEFT + REG_RO },
	{ GRDATA (FLG0, tm_unit[0].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG1, tm_unit[1].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG2, tm_unit[2].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG3, tm_unit[3].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG4, tm_unit[4].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG5, tm_unit[5].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG6, tm_unit[6].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG7, tm_unit[7].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ FLDATA (*DEVENB, tm_enb, 0), REG_HRO },
	{ NULL }  };

MTAB tm_mod[] = {
	{ UNIT_WLK, 0, "write enabled", "ENABLED", &tm_vlock },
	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", &tm_vlock }, 
	{ 0 }  };

DEVICE tm_dev = {
	"TM", tm_unit, tm_reg, tm_mod,
	TM_NUMDR, 10, 31, 1, 8, 8,
	NULL, NULL, &tm_reset,
	&tm_boot, &tm_attach, &tm_detach };

/* I/O dispatch routine, I/O addresses 17772520 - 17772532

   17772520	MTS	read only, constructed from tm_sta
			plus current drive status flags
   17772522	MTC	read/write
   17772524	MTBRC	read/write
   17772526	MTCMA	read/write
   17772530	MTD	read/write
   17772532	MTRD	read only
*/

t_stat tm_rd (int32 *data, int32 PA, int32 access)
{
UNIT *uptr;

uptr = tm_dev.units + GET_UNIT (tm_cmd);		/* get unit */
switch ((PA >> 1) & 07) {				/* decode PA<3:1> */
case 0:							/* MTS */
	*data = tm_updcsta (uptr);			/* update status */
	break;
case 1:							/* MTC */
	tm_updcsta (uptr);				/* update status */
	*data = tm_cmd;					/* return command */
	break;
case 2:							/* MTBRC */
	*data = tm_bc;					/* return byte count */
	break;
case 3:							/* MTCMA */
	*data = tm_ca;					/* return mem addr */
	break;
case 4:							/* MTD */
	*data = tm_db;					/* return data buffer */
	break;
case 5:							/* MTRD */
	tm_rdl = tm_rdl ^ RDL_CLK;			/* "clock" ticks */
	*data = tm_rdl;
	break;
default:						/* unimplemented */
	*data = 0;
	break;  }
return SCPE_OK;
}

t_stat tm_wr (int32 data, int32 PA, int32 access)
{
UNIT *uptr;

switch ((PA >> 1) & 07) {				/* decode PA<3:1> */
case 0:							/* MTS: read only */
	break;
case 1:							/* MTC */
	uptr = tm_dev.units + GET_UNIT (tm_cmd);	/* select unit */
	if ((tm_cmd & MTC_DONE) == 0) tm_sta = tm_sta | STA_ILL;
	else {	if (access == WRITEB) data = (PA & 1)?
			(tm_cmd & 0377) | (data << 8):
			(tm_cmd & ~0377) | data;
		if (data & MTC_INIT) {			/* init? */
			tm_reset (&tm_dev);		/* reset device */
			return SCPE_OK;  }
		if ((data & MTC_IE) == 0)		/* int disable? */
			CLR_INT (TM);			/* clr int request */
		else if ((tm_cmd & (MTC_ERR + MTC_DONE)) && !(tm_cmd & MTC_IE))
			SET_INT (TM);			/* set int request */
		tm_cmd = (tm_cmd & ~MTC_RW) | (data & MTC_RW);
		uptr = tm_dev.units + GET_UNIT (tm_cmd);	/* new unit */
		if (data & MTC_GO) tm_go (uptr);  }	/* new function? */
	tm_updcsta (uptr);				/* update status */
	break;
case 2:							/* MTBRC */
	if (access == WRITEB) data = (PA & 1)?
		(tm_bc & 0377) | (data << 8): (tm_bc & ~0377) | data;
	tm_bc = data;
	break;
case 3:							/* MTCMA */
	if (access == WRITEB) data = (PA & 1)?
		(tm_ca & 0377) | (data << 8): (tm_ca & ~0377) | data;
	tm_ca = data;
	break;
case 4:							/* MTD */
	if ((access == WRITEB) && (PA & 1)) return SCPE_OK;
	tm_db = data & 0377;
	break;  }					/* end switch */
return SCPE_OK;
}

/* New magtape command */

void tm_go (UNIT *uptr)
{
int32 f;

f = GET_FNC (tm_cmd);					/* get function */
if (((uptr -> flags & UNIT_ATT) == 0) ||		/* not attached? */
     sim_is_active (uptr) ||				/* busy? */
    (((f == MTC_WRITE) || (f == MTC_WREOF) || (f == MTC_WREXT)) &&
      (uptr -> flags & UNIT_WLK))) {			/* write locked? */
	tm_sta = tm_sta | STA_ILL;			/* illegal */
	tm_set_done ();					/* set done */
	return;  }
uptr -> USTAT = uptr -> USTAT & (STA_WLK | STA_ONL);	/* clear status */
tm_sta = 0;						/* clear errors */
if (f == MTC_UNLOAD) {					/* unload? */
	uptr -> USTAT = (uptr -> USTAT | STA_REW) & ~STA_ONL;
	detach_unit (uptr);  }				/* set offline */
else if (f == MTC_REWIND)				/* rewind */
	uptr -> USTAT = uptr -> USTAT | STA_REW; 	/* rewinding */
/* else /* uncomment this else if rewind/unload don't set done */
tm_cmd = tm_cmd & ~MTC_DONE;				/* clear done */
CLR_INT (TM);						/* clear int */
sim_activate (uptr, tm_time);				/* start io */
return;
}

/* Unit service

   If rewind done, reposition to start of tape, set status
   else, do operation, set done, interrupt
*/

t_stat tm_svc (UNIT *uptr)
{
int32 f, i, p, err;
t_addr xma;
t_stat rval;
t_mtrlnt tbc, cbc;
uint16 c;
static uint8 dbuf[DBSIZE];
static t_mtrlnt bceof = { 0 };

if (uptr -> USTAT & STA_REW) {				/* rewind? */
	uptr -> pos = 0;				/* update position */
	if (uptr -> flags & UNIT_ATT)			/* still on line? */
		uptr -> USTAT = STA_ONL | STA_BOT |
			((uptr -> flags & UNIT_WLK)? STA_WLK: 0);
	else uptr -> USTAT = 0;
	if (uptr -> UNUM == GET_UNIT (tm_cmd)) {	/* selected? */
		tm_set_done ();				/* set done */
		tm_updcsta (uptr);  }			/* update status */
	return SCPE_OK;  }

if ((uptr -> flags & UNIT_ATT) == 0) {			/* if not attached */
	uptr -> USTAT = 0;				/* unit off line */
	tm_sta = tm_sta | STA_ILL;			/* illegal operation */
	tm_set_done ();					/* set done */
	tm_updcsta (uptr);				/* update status */
	return IORETURN (tm_stopioe, SCPE_UNATT);  }

f = GET_FNC (tm_cmd);					/* get command */
if (((f == MTC_WRITE) || (f == MTC_WREOF) || (f == MTC_WREXT)) &&
     (uptr -> flags & UNIT_WLK)) {			/* write and locked? */
	tm_sta = tm_sta | STA_ILL;			/* illegal operation */
	tm_set_done ();					/* set done */
	tm_updcsta (uptr);				/* update status */
	return SCPE_OK;  }

err = 0;
rval = SCPE_OK;
xma = GET_EMA (tm_cmd) | tm_ca;				/* get mem addr */
cbc = 0200000 - tm_bc;					/* get bc */
switch (f) {						/* case on function */

/* Unit service, continued */

case MTC_READ:						/* read */
	fseek (uptr -> fileref, uptr -> pos, SEEK_SET);
	fxread (&tbc, sizeof (t_mtrlnt), 1, uptr -> fileref);
	if ((err = ferror (uptr -> fileref)) ||		/* error or eof? */
	    (feof (uptr -> fileref))) {
		uptr -> USTAT = uptr -> USTAT | STA_EOT;
		break;  }
	if (tbc == 0) {					/* tape mark? */
		uptr -> USTAT = uptr -> USTAT | STA_EOF;
		uptr -> pos = uptr -> pos + sizeof (t_mtrlnt);
		break;  }
	tbc = MTRL (tbc);				/* ignore error flag */
	if (tbc > cbc) tm_sta = tm_sta | STA_RLE;	/* wrong size? */
	if (tbc < cbc) cbc = tbc;			/* use smaller */
	i = fxread (dbuf, sizeof (int8), cbc, uptr -> fileref);
	for ( ; i < tbc; i++) dbuf[i] = 0;		/* fill with 0's */
	err = ferror (uptr -> fileref);
	for (i = 0; i < cbc; i++) {			/* copy buffer */
		if (xma >= MEMSIZE) {			/* nx memory? */
			tm_sta = tm_sta | STA_NXM;
			break;  }
		p = xma >> 1;				/* word address */
		c = dbuf[i];				/* character */
		if (i & 1) M[p] = (M[p] & 0377) | (c << 8);
		else M[p] = (M[p] & 0177400) | c;
		xma = (xma + 1) & 0777777;
		tm_bc = (tm_bc + 1) & 0177777;  }
	uptr -> pos = uptr -> pos + ((tbc + 1) & ~1) +
		(2 * sizeof (t_mtrlnt));
	break;
case MTC_WRITE:						/* write */
case MTC_WREXT:						/* write ext gap */
	fseek (uptr -> fileref, uptr -> pos, SEEK_SET);
	fxwrite (&cbc, sizeof (t_mtrlnt), 1, uptr -> fileref);
	for (i = 0; i < cbc; i++) {			/* copy buf to tape */
		if (xma >= MEMSIZE) {			/* nx memory? */
			tm_sta = tm_sta | STA_NXM;
			break;  }
		if (i & 1) dbuf[i] = (M[xma >> 1] >> 8) & 0377;
		else dbuf[i] = M[xma >> 1] & 0377;
		xma = (xma + 1) & 0777777;
		tm_bc = (tm_bc + 1) & 0177777;  }
	fxwrite (dbuf, sizeof (int8), cbc, uptr -> fileref);
	fxwrite (&cbc, sizeof (t_mtrlnt), 1, uptr -> fileref);
	err = ferror (uptr -> fileref);
	uptr -> pos = uptr -> pos + ((cbc + 1) & ~1) +
		(2 * sizeof (t_mtrlnt));
	break;
case MTC_WREOF:
	fseek (uptr -> fileref, uptr -> pos, SEEK_SET);
	fxwrite (&bceof, sizeof (t_mtrlnt), 1, uptr -> fileref);
	err = ferror (uptr -> fileref);
	uptr -> pos = uptr -> pos + sizeof (t_mtrlnt);	/* update position */
	break;

/* Unit service, continued */

case MTC_SPACEF:					/* space forward */
	do {	tm_bc = (tm_bc + 1) & 0177777;		/* incr wc */
		fseek (uptr -> fileref, uptr -> pos, SEEK_SET);
		fxread (&tbc, sizeof (t_mtrlnt), 1, uptr -> fileref);
		if ((err = ferror (uptr -> fileref)) ||	/* error or eof? */
		     feof (uptr -> fileref)) {
			uptr -> USTAT = uptr -> USTAT | STA_EOT;
			break;  }
		if (tbc == 0) {				/* zero bc? */
			uptr -> USTAT = uptr -> USTAT | STA_EOF;
			uptr -> pos = uptr -> pos + sizeof (t_mtrlnt);
			break;  }
		uptr -> pos = uptr -> pos + ((MTRL (tbc) + 1) & ~1) +
			(2 * sizeof (t_mtrlnt));  }
	while (tm_bc != 0);
	break;
case MTC_SPACER:					/* space reverse */
	if (uptr -> pos == 0) {				/* at BOT? */
		uptr -> USTAT = uptr -> USTAT | STA_BOT;
		break;  }
	do {	tm_bc = (tm_bc + 1) & 0177777;		/* incr wc */
		fseek (uptr -> fileref, uptr -> pos - sizeof (t_mtrlnt),
			SEEK_SET);
		fxread (&tbc, sizeof (t_mtrlnt), 1, uptr -> fileref);
		if ((err = ferror (uptr -> fileref)) ||	/* error or eof? */
		     feof (uptr -> fileref)) {
			uptr -> USTAT = uptr -> USTAT | STA_BOT;
			uptr -> pos = 0;
			break;  }
		if (tbc == 0) {			/* start of prv file? */
			uptr -> USTAT = uptr -> USTAT | STA_EOF;
			uptr -> pos = uptr -> pos - sizeof (t_mtrlnt);
			break;  }
		uptr -> pos = uptr -> pos - ((MTRL (tbc) + 1) & ~1) -
			(2 * sizeof (t_mtrlnt));
		if (uptr -> pos == 0) {			/* start of tape? */
			uptr -> USTAT = uptr -> USTAT | STA_BOT;
			break;  }  }
	while (tm_bc != 0);
	break;  }					/* end case */

/* Unit service, continued */

if (err != 0) {						/* I/O error */
	tm_sta = tm_sta | STA_PAR | STA_CRC;		/* flag error */
	perror ("MT I/O error");
	rval = SCPE_IOERR;
	clearerr (uptr -> fileref);  }
tm_cmd = (tm_cmd & ~MTC_EMA) | ((xma >> (16 - MTC_V_EMA)) & MTC_EMA);
tm_ca = xma & 0177777;					/* update mem addr */
tm_set_done ();						/* set done */
tm_updcsta (uptr);					/* update status */
return IORETURN (tm_stopioe, rval);
}

/* Update controller status */

int32 tm_updcsta (UNIT *uptr)
{
tm_sta = (tm_sta & ~(STA_DYN | STA_CLR)) | (uptr -> USTAT & STA_DYN);
if (sim_is_active (uptr)) tm_sta = tm_sta & ~STA_TUR;
else tm_sta = tm_sta | STA_TUR;
if (tm_sta & STA_EFLGS) tm_cmd = tm_cmd | MTC_ERR;
else tm_cmd = tm_cmd & ~MTC_ERR;
if ((tm_cmd & MTC_IE) == 0) CLR_INT (TM);
return tm_sta;
}

/* Set done */

void tm_set_done (void)
{
tm_cmd = tm_cmd | MTC_DONE;
if (tm_cmd & MTC_IE) SET_INT (TM);
return;
}

/* Reset routine */

t_stat tm_reset (DEVICE *dptr)
{
int32 u;
UNIT *uptr;
extern int32 ts_enb;

if (tm_enb) ts_enb = 0;					/* TM or TS */
tm_cmd = MTC_DONE;					/* set done */
tm_bc = tm_ca = tm_db = tm_sta = tm_rdl = 0;
CLR_INT (TM);						/* clear interrupt */
for (u = 0; u < TM_NUMDR; u++) {			/* loop thru units */
	uptr = tm_dev.units + u;
	uptr -> UNUM = u;				/* init drive number */
	sim_cancel (uptr);				/* cancel activity */
	if (uptr -> flags & UNIT_ATT) uptr -> USTAT = STA_ONL |
		((uptr -> pos)? 0: STA_BOT) |
		((uptr -> flags & UNIT_WLK)? STA_WLK: 0);
	else uptr -> USTAT = 0;  }
return SCPE_OK;
}

/* Attach routine */

t_stat tm_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = attach_unit (uptr, cptr);
if (r != SCPE_OK) return r;
uptr -> USTAT = STA_ONL | STA_BOT | ((uptr -> flags & UNIT_WLK)? STA_WLK: 0);
if (uptr -> UNUM == GET_UNIT (tm_cmd)) tm_updcsta (uptr);
return r;
}

/* Detach routine */

t_stat tm_detach (UNIT* uptr)
{
if (!sim_is_active (uptr)) uptr -> USTAT = 0;
if (uptr -> UNUM == GET_UNIT (tm_cmd)) tm_updcsta (uptr);
return detach_unit (uptr);
}

/* Write lock/enable routine */

t_stat tm_vlock (UNIT *uptr, int32 val)
{
if ((uptr -> flags & UNIT_ATT) && val) uptr -> USTAT = uptr -> USTAT | STA_WLK;
else uptr -> USTAT = uptr -> USTAT & ~STA_WLK;
if (uptr -> UNUM == GET_UNIT (tm_cmd)) tm_updcsta (uptr);
return SCPE_OK;
}

/* Device bootstrap

   Magtape boot format changed over time.  Originally, a boot tape
   contained a boot loader in the first block.  Eventually, the first
   block was reserved for a tape label, and the second block was
   expected to contain a boot loader.  BSD and DEC operating systems
   use the second block scheme, so it is the default.

   To boot from the first block, use boot -o (old).
 */

#define BOOT_START 040000
#define BOOT_UNIT (BOOT_START + 6)
#define BOOT1_LEN (sizeof (boot1_rom) / sizeof (int32))
#define BOOT2_LEN (sizeof (boot2_rom) / sizeof (int32))

static const int32 boot1_rom[] = {
	0012706, 0040000,		/* mov #boot_start, sp */
	0012700, 0000000,		/* mov #unit_num, r0 */
	0012701, 0172526,		/* mov #172526, r1	; mtcma */
	0005011,			/* clr (r1) */
	0011041,			/* mov r1, -(r1)	; mtbrc */
	0010002,			/* mov r0,r2 */
	0000302,			/* swab r2 */
	0062702, 0060003,		/* add #60003, r2 */
	0010241,	 		/* mov r2, -(r1)	; read + go */
	0105711,			/* tstb (r1)		; mtc */
	0100376,			/* bpl .-2 */
	0005002,			/* clr r2 */
	0005003,			/* clr r3 */
	0005004,			/* clr r4 */
	0012705, 0052115,	/* mov #MT, r5 */
	0005007				/* clr r7 */
};

static const int32 boot2_rom[] = {
	0012706, 0040000,		/* mov #boot_start, sp */
	0012700, 0000000,		/* mov #unit_num, r0 */
	0012701, 0172526,		/* mov #172526, r1	; mtcma */
	0005011,			/* clr (r1) */
	0012741, 0177777,		/* mov #-1, -(r1)	; mtbrc */
	0010002,			/* mov r0,r2 */
	0000302,			/* swab r2 */
	0062702, 0060011,		/* add #60011, r2 */
	0010241,			/* mov r2, -(r1)	; space + go */
	0105711,			/* tstb (r1)		; mtc */
	0100376,			/* bpl .-2 */
	0010002,			/* mov r0,r2 */
	0000302,			/* swab r2 */
	0062702, 0060003,		/* add #60003, r2 */
	0010211,	 		/* mov r2, (r1)		; read + go */
	0105711,			/* tstb (r1)		; mtc */
	0100376,			/* bpl .-2 */
	0005002,			/* clr r2 */
	0005003,			/* clr r3 */
	0005004,			/* clr r4 */
	0012705, 0052115,	/* mov #MT, r5 */
	0005007				/* clr r7 */
};

t_stat tm_boot (int32 unitno)
{
int32 i;
extern int32 saved_PC;
extern int32 sim_switches;

tm_unit[unitno].pos = 0;
if (sim_switches & SWMASK ('O')) {
	for (i = 0; i < BOOT1_LEN; i++)
		M[(BOOT_START >> 1) + i] = boot1_rom[i];  }
else {	for (i = 0; i < BOOT2_LEN; i++)
		M[(BOOT_START >> 1) + i] = boot2_rom[i];  }
M[BOOT_UNIT >> 1] = unitno;
saved_PC = BOOT_START;
return SCPE_OK;
} 
