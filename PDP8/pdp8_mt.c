/* pdp8_mt.c: PDP-8 magnetic tape simulator

   Copyright (c) 1993-2002, Robert M Supnik

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

   mt		TM8E/TU10 magtape

   30-Oct-02	RMS	Revised BOT handling, added error record handling
   04-Oct-02	RMS	Added DIBs, device number support
   30-Aug-02	RMS	Revamped error handling
   28-Aug-02	RMS	Added end of medium support
   30-May-02	RMS	Widened POS to 32b
   22-Apr-02	RMS	Added maximum record length test
   06-Jan-02	RMS	Changed enable/disable support
   30-Nov-01	RMS	Added read only unit, extended SET/SHOW support
   24-Nov-01	RMS	Changed UST, POS, FLG to arrays
   25-Apr-01	RMS	Added device enable/disable support
   04-Oct-98	RMS	V2.4 magtape format
   22-Jan-97	RMS	V2.3 magtape format
   01-Jan-96	RMS	Rewritten from TM8-E Maintenance Manual

   Magnetic tapes are represented as a series of variable records
   of the form:

	32b byte count
	byte 0
	byte 1
	:
	byte n-2
	byte n-1
	32b byte count

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a byte count of 0.
*/

#include "pdp8_defs.h"

#define MT_NUMDR	8				/* #drives */
#define UNIT_V_WLK	(UNIT_V_UF + 0)			/* write locked */
#define UNIT_V_PNU	(UNIT_V_UF + 1)			/* pos not updated */
#define UNIT_WLK	(1 << UNIT_V_WLK)
#define UNIT_PNU	(1 << UNIT_V_PNU)
#define USTAT		u3				/* unit status */
#define MT_MAXFR	(1 << 16)			/* max record lnt */
#define DBSIZE		(1 << 12)			/* max data cmd */
#define DBMASK		(SBSIZE - 1)
#define UNIT_WPRT	(UNIT_WLK | UNIT_RO)		/* write protect */

/* Command/unit - mt_cu */

#define CU_V_UNIT	9				/* unit */
#define CU_M_UNIT	07
#define CU_PARITY	00400				/* parity select */
#define CU_IEE		00200				/* error int enable */
#define CU_IED		00100				/* done int enable */
#define CU_V_EMA	3				/* ext mem address */
#define CU_M_EMA	07
#define CU_EMA		(CU_M_EMA << CU_V_EMA)
#define CU_DTY		00002				/* drive type */
#define CU_UNPAK	00001				/* 6b vs 8b mode */
#define GET_UNIT(x)	(((x) >> CU_V_UNIT) & CU_M_UNIT)
#define GET_EMA(x)	(((x) & CU_EMA) << (12 - CU_V_EMA))

/* Function - mt_fn */

#define FN_V_FNC	9				/* function */
#define FN_M_FNC	07
#define  FN_UNLOAD	 00
#define  FN_REWIND	 01
#define  FN_READ	 02
#define  FN_CMPARE	 03
#define  FN_WRITE	 04
#define  FN_WREOF	 05
#define  FN_SPACEF	 06
#define  FN_SPACER	 07
#define FN_ERASE	00400				/* erase */
#define FN_CRC		00200				/* read CRC */
#define FN_GO		00100				/* go */
#define FN_INC		00040				/* incr mode */
#define FN_RMASK	07700				/* readable bits */
#define GET_FNC(x)	(((x) >> FN_V_FNC) & FN_M_FNC)

/* Status - stored in mt_sta or (*) uptr->USTAT */

#define STA_ERR		(04000 << 12)			/* error */
#define STA_REW		(02000 << 12)			/* *rewinding */
#define STA_BOT		(01000 << 12)			/* *start of tape */
#define STA_REM		(00400 << 12)			/* *offline */
#define STA_PAR		(00200 << 12)			/* parity error */
#define STA_EOF		(00100 << 12)			/* *end of file */
#define STA_RLE		(00040 << 12)			/* rec lnt error */
#define STA_DLT		(00020 << 12)			/* data late */
#define STA_EOT		(00010 << 12)			/* *end of tape */
#define STA_WLK		(00004 << 12)			/* *write locked */
#define STA_CPE		(00002 << 12)			/* compare error */
#define STA_ILL		(00001 << 12)			/* illegal */
#define STA_9TK		00040				/* 9 track */
/* #define STA_BAD	00020				/* bad tape?? */
#define STA_INC		00010				/* increment error */
#define STA_LAT		00004				/* lateral par error */
#define STA_CRC		00002				/* CRC error */
#define STA_LON		00001				/* long par error */

#define STA_CLR		(FN_RMASK | 00020)		/* always clear */
#define STA_DYN		(STA_REW | STA_BOT | STA_REM | STA_EOF | \
			 STA_EOT | STA_WLK)		/* kept in USTAT */
#define STA_EFLGS	(STA_BOT | STA_PAR | STA_RLE | STA_DLT | \
			 STA_EOT | STA_CPE | STA_ILL | STA_EOF | \
			 STA_INC )
			 				/* set error */
#define TUR(u)		(!sim_is_active (u))		/* tape unit ready */

extern uint16 M[];
extern int32 int_req, stop_inst;
extern UNIT cpu_unit;

int32 mt_cu = 0;					/* command/unit */
int32 mt_fn = 0;					/* function */
int32 mt_ca = 0;					/* current address */
int32 mt_wc = 0;					/* word count */
int32 mt_sta = 0;					/* status register */
int32 mt_db = 0;					/* data buffer */
int32 mt_done = 0;					/* mag tape flag */
int32 mt_time = 10;					/* record latency */
int32 mt_stopioe = 1;					/* stop on error */

DEVICE mt_dev;
int32 mt70 (int32 IR, int32 AC);
int32 mt71 (int32 IR, int32 AC);
int32 mt72 (int32 IR, int32 AC);
t_stat mt_svc (UNIT *uptr);
t_stat mt_reset (DEVICE *dptr);
t_stat mt_attach (UNIT *uptr, char *cptr);
t_stat mt_detach (UNIT *uptr);
int32 mt_updcsta (UNIT *uptr);
int32 mt_ixma (int32 xma);
t_stat mt_vlock (UNIT *uptr, int32 val, char *cptr, void *desc);
UNIT *mt_busy (void);
void mt_set_done (void);
t_bool mt_rdlntf (UNIT *uptr, t_mtrlnt *tbc, int32 *err);
t_bool mt_rdlntr (UNIT *uptr, t_mtrlnt *tbc, int32 *err);

/* MT data structures

   mt_dev	MT device descriptor
   mt_unit	MT unit list
   mt_reg	MT register list
   mt_mod	MT modifier list
*/

DIB mt_dib = { DEV_MT, 3, { &mt70, &mt71, &mt72 } };

UNIT mt_unit[] = {
	{ UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
	{ UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
	{ UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
	{ UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
	{ UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
	{ UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
	{ UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
	{ UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) }  };

REG mt_reg[] = {
	{ ORDATA (CMD, mt_cu, 12) },
	{ ORDATA (FNC, mt_fn, 12) },
	{ ORDATA (CA, mt_ca, 12) },
	{ ORDATA (WC, mt_wc, 12) },
	{ ORDATA (DB, mt_db, 12) },
	{ GRDATA (STA, mt_sta, 8, 12, 12) },
	{ ORDATA (STA2, mt_sta, 6) },
	{ FLDATA (DONE, mt_done, 0) },
	{ FLDATA (INT, int_req, INT_V_MT) },
	{ FLDATA (STOP_IOE, mt_stopioe, 0) },
	{ DRDATA (TIME, mt_time, 24), PV_LEFT },
	{ URDATA (UST, mt_unit[0].USTAT, 8, 16, 0, MT_NUMDR, 0) },
	{ URDATA (POS, mt_unit[0].pos, 10, 32, 0,
		  MT_NUMDR, PV_LEFT | REG_RO) },
	{ FLDATA (DEVNUM, mt_dib.dev, 6), REG_HRO },
	{ NULL }  };

MTAB mt_mod[] = {
	{ UNIT_WLK, 0, "write enabled", "WRITEENABLED", &mt_vlock },
	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", &mt_vlock }, 
	{ MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
		&set_dev, &show_dev, NULL },
	{ 0 }  };

DEVICE mt_dev = {
	"MT", mt_unit, mt_reg, mt_mod,
	MT_NUMDR, 10, 31, 1, 8, 8,
	NULL, NULL, &mt_reset,
	NULL, &mt_attach, &mt_detach,
	&mt_dib, DEV_DISABLE };

/* IOT routines */

int32 mt70 (int32 IR, int32 AC)
{
int32 f;
UNIT *uptr;

uptr = mt_dev.units + GET_UNIT (mt_cu);			/* get unit */
switch (IR & 07) {					/* decode IR<9:11> */
case 1:							/* LWCR */
	mt_wc = AC;					/* load word count */
	return 0;
case 2:							/* CWCR */
	mt_wc = 0;					/* clear word count */
	return AC;
case 3:							/* LCAR */
	mt_ca = AC;					/* load mem address */
	return 0;
case 4:							/* CCAR */
	mt_ca = 0;					/* clear mem address */
	return AC;
case 5:							/* LCMR */
	if (mt_busy ()) mt_sta = mt_sta | STA_ILL;	/* busy? illegal op */
	mt_cu = AC;					/* load command reg */
	mt_updcsta (mt_dev.units + GET_UNIT (mt_cu));
	return 0;
case 6:							/* LFGR */
	if (mt_busy ()) mt_sta = mt_sta | STA_ILL;	/* busy? illegal op */
	mt_fn = AC;					/* load function */
	if ((mt_fn & FN_GO) == 0) {			/* go set? */
		mt_updcsta (uptr);			/* update status */
		return 0;  }
	f = GET_FNC (mt_fn);				/* get function */
	if (((uptr->flags & UNIT_ATT) == 0) || !TUR (uptr) ||
	   (((f == FN_WRITE) || (f == FN_WREOF)) && (uptr->flags & UNIT_WPRT))
	   || (((f == FN_SPACER) || (f == FN_REWIND)) && (uptr->USTAT & STA_BOT))) {
		mt_sta = mt_sta | STA_ILL;		/* illegal op error */
		mt_set_done ();				/* set done */
		mt_updcsta (uptr);			/* update status */
		return 0;  }
	uptr->USTAT = uptr->USTAT & STA_WLK;		/* clear status */
	if (f == FN_UNLOAD) {				/* unload? */
		detach_unit (uptr);			/* set offline */
		uptr->USTAT = STA_REW | STA_REM;	/* rewinding, off */
		mt_set_done ();  }			/* set done */
	else if (f == FN_REWIND) {			/* rewind */
		uptr->USTAT = uptr->USTAT | STA_REW;	/* rewinding */
		mt_set_done ();  }			/* set done */
	else mt_done = 0;				/* clear done */
	mt_updcsta (uptr);				/* update status */
	sim_activate (uptr, mt_time);			/* start io */
	return 0;
case 7:							/* LDBR */
	if (mt_busy ()) mt_sta = mt_sta | STA_ILL;	/* busy? illegal op */
	mt_db = AC;					/* load buffer */
	mt_set_done ();					/* set done */
	mt_updcsta (uptr);				/* update status */
	return 0;  }					/* end switch */
return (stop_inst << IOT_V_REASON) + AC;		/* ill inst */
}

/* IOTs, continued */

int32 mt71 (int32 IR, int32 AC)
{
UNIT *uptr;

uptr = mt_dev.units + GET_UNIT (mt_cu);
switch (IR & 07) {					/* decode IR<9:11> */
case 1:							/* RWCR */
	return mt_wc;					/* read word count */
case 2:							/* CLT */
	mt_reset (&mt_dev);				/* reset everything */
	return AC;
case 3:							/* RCAR */
	return mt_ca;					/* read mem address */
case 4:							/* RMSR */
	return ((mt_updcsta (uptr) >> 12) & 07777);	/* read status */
case 5:							/* RCMR */
	return mt_cu;					/* read command */
case 6:							/* RFSR */
	return (((mt_fn & FN_RMASK) | (mt_updcsta (uptr) & ~FN_RMASK))
		 & 07777);				/* read function */
case 7:							/* RDBR */
	return mt_db;  }				/* read data buffer */
return (stop_inst << IOT_V_REASON) + AC;		/* ill inst */
}

int32 mt72 (int32 IR, int32 AC)
{
UNIT *uptr;

uptr = mt_dev.units + GET_UNIT (mt_cu);			/* get unit */
switch (IR & 07) {					/* decode IR<9:11> */
case 1:							/* SKEF */
	return (mt_sta & STA_ERR)? IOT_SKP + AC: AC;
case 2:							/* SKCB */
	return (!mt_busy ())? IOT_SKP + AC: AC;
case 3:							/* SKJD */
	return mt_done? IOT_SKP + AC: AC;
case 4:							/* SKTR */
	return (TUR (uptr))? IOT_SKP + AC: AC;
case 5:							/* CLF */
	if (TUR (uptr)) mt_reset (&mt_dev);		/* if TUR, zap */
	else {	mt_sta = 0;				/* clear status */
		mt_done = 0;				/* clear done */
		mt_updcsta (uptr);  }			/* update status */
	return AC;  }					/* end switch */
return (stop_inst << IOT_V_REASON) + AC;		/* ill inst */
}

/* Unit service

   If rewind done, reposition to start of tape, set status
   else, do operation, set done, interrupt
*/

t_stat mt_svc (UNIT *uptr)
{
int32 f, i, p, u, err, wc, xma, pnu;
t_mtrlnt abc, tbc, cbc;
uint16 c, c1, c2;
uint8 dbuf[(2 * DBSIZE)];
static t_mtrlnt bceof = MTR_TMK;

err = 0;
u = uptr - mt_dev.units;				/* get unit number */
f = GET_FNC (mt_fn);					/* get command */
pnu = MT_TST_PNU (uptr);				/* get pos not upd */
MT_CLR_PNU (uptr);					/* and clear */

if (uptr->USTAT & STA_REW) {				/* rewind? */
	uptr->pos = 0;					/* update position */
	if (uptr->flags & UNIT_ATT)			/* still on line? */
		uptr->USTAT = (uptr->USTAT & STA_WLK) | STA_BOT;
	else uptr->USTAT = STA_REM;
	if (u == GET_UNIT (mt_cu)) {			/* selected? */
		mt_set_done ();				/* set done */
		mt_updcsta (uptr);  }			/* update status */
	return SCPE_OK;  }

if ((uptr->flags & UNIT_ATT) == 0) {			/* if not attached */
	uptr->USTAT = STA_REM;				/* unit off line */
	mt_sta = mt_sta | STA_ILL;			/* illegal operation */
	mt_set_done ();					/* set done */
	mt_updcsta (uptr);				/* update status */
	return IORETURN (mt_stopioe, SCPE_UNATT);  }

if (((f == FN_WRITE) || (f == FN_WREOF)) && (uptr->flags & UNIT_WPRT)) {
	mt_sta = mt_sta | STA_ILL;			/* illegal operation */
	mt_set_done ();					/* set done */
	mt_updcsta (uptr);				/* update status */
	return SCPE_OK;  }

xma = GET_EMA (mt_cu) + mt_ca;				/* get mem addr */
wc = 010000 - mt_wc;					/* get wc */
switch (f) {						/* case on function */

/* Unit service, continued */

case FN_READ:						/* read */
case FN_CMPARE:						/* read/compare */
	if (mt_rdlntf (uptr, &tbc, &err)) {		/* read rec lnt */
		mt_sta = mt_sta | STA_RLE;		/* err, eof/eom, tmk */
		break;  }
	if (tbc > MT_MAXFR) return SCPE_MTRLNT;		/* record too long? */
	cbc = (mt_cu & CU_UNPAK)? wc: wc * 2;		/* expected bc */
	if (tbc != cbc) mt_sta = mt_sta | STA_RLE;	/* wrong size? */
	if (tbc < cbc) {				/* record small? */
		cbc = tbc;				/* use smaller */
		wc = (mt_cu & CU_UNPAK)? cbc: (cbc + 1) / 2;  }
	abc = fxread (dbuf, sizeof (uint8), cbc, uptr->fileref);
	if (err = ferror (uptr->fileref)) {		/* error? */
		mt_sta = mt_sta | STA_RLE;		/* set flag */
		MT_SET_PNU (uptr);			/* pos not upd */
		break;  }
	for ( ; abc < cbc; abc++) dbuf[abc] = 0;	/* fill with 0's */
	for (i = p = 0; i < wc; i++) {			/* copy buffer */
		xma = mt_ixma (xma);			/* increment xma */
		if (mt_cu & CU_UNPAK) c = dbuf[p++];
		else {	c1 = dbuf[p++] & 077;
			c2 = dbuf[p++] & 077;
			c = (c1 << 6) | c2;  }
		if ((f == FN_READ) && MEM_ADDR_OK (xma)) M[xma] = c;
		else if ((f == FN_CMPARE) && (M[xma] != c)) {
			mt_sta = mt_sta | STA_CPE;
			break;  }  }
	mt_wc = (mt_wc + wc) & 07777;			/* update wc */
	uptr->pos = uptr->pos + ((tbc + 1) & ~1) +	/* update tape pos */
		(2 * sizeof (t_mtrlnt));
	break;

case FN_WRITE:						/* write */
	fseek (uptr->fileref, uptr->pos, SEEK_SET);
	tbc = (mt_cu & CU_UNPAK)? wc: wc * 2;
	fxwrite (&tbc, sizeof (t_mtrlnt), 1, uptr->fileref);
	for (i = p = 0; i < wc; i++) {			/* copy buf to tape */
		xma = mt_ixma (xma);			/* incr mem addr */
		if (mt_cu & CU_UNPAK) dbuf[p++] = M[xma] & 0377;
		else {	dbuf[p++] = (M[xma] >> 6) & 077;
			dbuf[p++] = M[xma] & 077;  }  }
	fxwrite (dbuf, sizeof (int8), (tbc + 1) & ~1, uptr->fileref);
	fxwrite (&tbc, sizeof (t_mtrlnt), 1, uptr->fileref);
	if (err = ferror (uptr->fileref)) MT_SET_PNU (uptr); /* error? */
	else {	mt_wc = 0;
		uptr->pos = uptr->pos + ((tbc + 1) & ~1) + /* upd tape pos */
		(2 * sizeof (t_mtrlnt));  }
	break;

/* Unit service, continued */

case FN_WREOF:
	fseek (uptr->fileref, uptr->pos, SEEK_SET);
	fxwrite (&bceof, sizeof (t_mtrlnt), 1, uptr->fileref); /* write eof */
	if (err = ferror (uptr->fileref)) MT_SET_PNU (uptr); /* error? */
	else uptr->pos = uptr->pos + sizeof (t_mtrlnt);	/* update tape pos */
	break;

case FN_SPACEF:						/* space forward */
	do {	mt_wc = (mt_wc + 1) & 07777;		/* incr wc */
		if (mt_rdlntf (uptr, &tbc, &err)) break;/* read rec lnt, err? */
		uptr->pos = uptr->pos + ((tbc + 1) & ~1) +
			(2 * sizeof (t_mtrlnt));  }
	while (mt_wc != 0);
	break;

case FN_SPACER:						/* space reverse */
	do {	mt_wc = (mt_wc + 1) & 07777;		/* incr wc */
		if (pnu) pnu = 0;			/* pos not upd? */
		else {	if (mt_rdlntr (uptr, &tbc, &err)) break;
			uptr->pos = uptr->pos - ((tbc + 1) & ~1) -
			    (2 * sizeof (t_mtrlnt));  }  }
	while (mt_wc != 0);
	break;  }					/* end case */

if (err != 0) mt_sta = mt_sta | STA_PAR;		/* error? set flag */
mt_cu = (mt_cu & ~CU_EMA) | ((xma >> (12 - CU_V_EMA)) & CU_EMA);
mt_ca = xma & 07777;					/* update mem addr */
mt_set_done ();						/* set done */
mt_updcsta (uptr);					/* update status */
if (err != 0) {						/* error? */
	perror ("MT I/O error");
	clearerr (uptr->fileref);
	if (mt_stopioe) return SCPE_IOERR;  }
return SCPE_OK;
}

/* Update controller status */

int32 mt_updcsta (UNIT *uptr)
{
mt_sta = (mt_sta & ~(STA_DYN | STA_ERR | STA_CLR)) | (uptr->USTAT & STA_DYN);
if (mt_sta & STA_EFLGS) mt_sta = mt_sta | STA_ERR;
if (((mt_sta & STA_ERR) && (mt_cu & CU_IEE)) ||
     (mt_done && (mt_cu & CU_IED))) int_req = int_req | INT_MT;
else int_req = int_req & ~INT_MT;
return mt_sta;
}

/* Test if controller busy */

UNIT *mt_busy (void)
{
int32 u;
UNIT *uptr;

for (u = 0; u < MT_NUMDR; u++) {			/* loop thru units */
	uptr = mt_dev.units + u;
	if (sim_is_active (uptr) && ((uptr->USTAT & STA_REW) == 0))
		return uptr;  }
return NULL;
}

/* Increment extended memory address */

int32 mt_ixma (int32 xma)				/* incr extended ma */
{
int32 v;

v = ((xma + 1) & 07777) | (xma & 070000);		/* wrapped incr */
if (mt_fn & FN_INC) {					/* increment mode? */
	if (xma == 077777) mt_sta = mt_sta | STA_INC;	/* at limit? error */
	else v = xma + 1;  }				/* else 15b incr */
return v;
}

/* Set done */

void mt_set_done (void)
{
mt_done = 1;						/* set done */
mt_fn = mt_fn & ~(FN_CRC | FN_GO | FN_INC);		/* clear func<4:6> */
return;
}

/* Read record length forward - return T if error, EOM, or EOF */

t_bool mt_rdlntf (UNIT *uptr, t_mtrlnt *tbc, int32 *err)
{
fseek (uptr->fileref, uptr->pos, SEEK_SET);		/* set tape pos */
fxread (tbc, sizeof (t_mtrlnt), 1, uptr->fileref);	/* read rec lnt */
if ((*err = ferror (uptr->fileref)) ||			/* error, */
     feof (uptr->fileref) || (*tbc == MTR_EOM)) {	/* eof or eom? */
	mt_sta = mt_sta | STA_PAR;			/* parity error */
	MT_SET_PNU (uptr);				/* pos not upd */
	return TRUE;  }
if (*tbc == MTR_TMK) {					/* tape mark? */
	uptr->USTAT = uptr->USTAT | STA_EOF;		/* end of file */
	uptr->pos = uptr->pos + sizeof (t_mtrlnt);	/* spc over tmk */
	return TRUE;  }
if (MTRF (*tbc)) mt_sta = mt_sta | STA_PAR;		/* record in error? */
*tbc = MTRL (*tbc);					/* clear error flag */
return FALSE;
}

/* Read record length reverse - return T if error, EOM, or EOF */

t_bool mt_rdlntr (UNIT *uptr, t_mtrlnt *tbc, int32 *err)
{
if (uptr->pos < sizeof (t_mtrlnt)) {			/* at BOT? */
	uptr->USTAT = uptr->USTAT | STA_BOT;		/* set status */
	return TRUE;  }					/* error */
fseek (uptr->fileref, uptr->pos - sizeof (t_mtrlnt), SEEK_SET);
fxread (tbc, sizeof (t_mtrlnt), 1, uptr->fileref);
if ((*err = ferror (uptr->fileref)) ||			/* error? */
     feof (uptr->fileref)) {				/* end of file? */
	mt_sta = mt_sta | STA_PAR;			/* parity error */
	return TRUE;  }
if (*tbc == MTR_EOM) {					/* eom? */
	mt_sta = mt_sta | STA_PAR;			/* bad tape */
	uptr->pos = uptr->pos - sizeof (t_mtrlnt);	/* spc over eom */
	return TRUE;  }
if (*tbc == MTR_TMK) {					/* tape mark? */
	uptr->USTAT = uptr->USTAT | STA_EOF;		/* end of file */
	uptr->pos = uptr->pos - sizeof (t_mtrlnt);	/* spc over tmk */
	return TRUE;  }
if (MTRF (*tbc)) mt_sta = mt_sta | STA_PAR;		/* record in error? */
*tbc = MTRL (*tbc);					/* clear error flag */
return FALSE;
}

/* Reset routine */

t_stat mt_reset (DEVICE *dptr)
{
int32 u;
UNIT *uptr;

mt_cu = mt_fn = mt_wc = mt_ca = mt_db = mt_sta = mt_done = 0;
int_req = int_req & ~INT_MT;				/* clear interrupt */
for (u = 0; u < MT_NUMDR; u++) {			/* loop thru units */
	uptr = mt_dev.units + u;
	sim_cancel (uptr);				/* cancel activity */
	MT_CLR_PNU (uptr);				/* clear pos flag */
	if (uptr->flags & UNIT_ATT) uptr->USTAT =
		((uptr->pos)? 0: STA_BOT) |
		((uptr->flags & UNIT_WPRT)? STA_WLK: 0);
	else uptr->USTAT = STA_REM;  }
return SCPE_OK;
}

/* Attach routine */

t_stat mt_attach (UNIT *uptr, char *cptr)
{
t_stat r;
int32 u = uptr - mt_dev.units;				/* get unit number */

r = attach_unit (uptr, cptr);
if (r != SCPE_OK) return r;
MT_CLR_PNU (uptr);
uptr->USTAT = STA_BOT | ((uptr->flags & UNIT_WPRT)? STA_WLK: 0);
if (u == GET_UNIT (mt_cu)) mt_updcsta (uptr);
return r;
}

/* Detach routine */

t_stat mt_detach (UNIT* uptr)
{
int32 u = uptr - mt_dev.units;				/* get unit number */

MT_CLR_PNU (uptr);
if (!sim_is_active (uptr)) uptr->USTAT = STA_REM;
if (u == GET_UNIT (mt_cu)) mt_updcsta (uptr);
return detach_unit (uptr);
}

/* Write lock/enable routine */

t_stat mt_vlock (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 u = uptr - mt_dev.units;				/* get unit number */

if ((uptr->flags & UNIT_ATT) && val) uptr->USTAT = uptr->USTAT | STA_WLK;
else uptr->USTAT = uptr->USTAT & ~STA_WLK;
if (u == GET_UNIT (mt_cu)) mt_updcsta (uptr);
return SCPE_OK;
}
