/* i1401_mt.c: IBM 1401 magnetic tape simulator

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

   mt		7-track magtape

   31-Oct-02	RMS	Added error record handling
   10-Oct-02	RMS	Fixed end-of-record on load read writes WM plus GM
   30-Sep-02	RMS	Revamped error handling
   28-Aug-02	RMS	Added end of medium support
   12-Jun-02	RMS	End-of-record on move read preserves old WM under GM
			(found by Van Snyder)
   03-Jun-02	RMS	Modified for 1311 support
   30-May-02	RMS	Widened POS to 32b
   22-Apr-02	RMS	Added protection against bad record lengths
   30-Jan-02	RMS	New zero footprint tape bootstrap from Van Snyder
   20-Jan-02	RMS	Changed write enabled modifier
   29-Nov-01	RMS	Added read only unit support
   18-Apr-01	RMS	Changed to rewind tape before boot
   07-Dec-00	RMS	Widened display width from 6 to 8 bits to see record lnt
		CEO	Added tape bootstrap
   14-Apr-99	RMS	Changed t_addr to unsigned
   04-Oct-98	RMS	V2.4 magtape format

   Magnetic tapes are represented as a series of variable 16b records
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

#include "i1401_defs.h"

#define MT_NUMDR	7				/* #drives */
#define UNIT_V_WLK	(UNIT_V_UF + 0)			/* write locked */
#define UNIT_V_PNU	(UNIT_V_UF + 1)			/* pos not upd */
#define UNIT_WLK	(1 << UNIT_V_WLK)
#define UNIT_PNU	(1 << UNIT_V_PNU)
#define UNIT_WPRT	(UNIT_WLK | UNIT_RO)		/* write protect */
#define MT_MAXFR	(MAXMEMSIZE * 2)		/* max transfer */

extern uint8 M[];					/* memory */
extern int32 ind[64];
extern int32 BS, iochk;
extern UNIT cpu_unit;
uint8 dbuf[MT_MAXFR];					/* tape buffer */
t_stat mt_reset (DEVICE *dptr);
t_stat mt_boot (int32 unitno, DEVICE *dptr);
t_stat mt_attach (UNIT *uptr, char *cptr);
UNIT *get_unit (int32 unit);

/* MT data structures

   mt_dev	MT device descriptor
   mt_unit	MT unit list
   mt_reg	MT register list
   mt_mod	MT modifier list
*/

UNIT mt_unit[] = {
	{ UDATA (NULL, UNIT_DIS, 0) },			/* doesn't exist */
	{ UDATA (NULL, UNIT_DISABLE + UNIT_ATTABLE +
		UNIT_ROABLE + UNIT_BCD, 0) },
	{ UDATA (NULL, UNIT_DISABLE + UNIT_ATTABLE +
		UNIT_ROABLE + UNIT_BCD, 0) },
	{ UDATA (NULL, UNIT_DISABLE + UNIT_ATTABLE +
		UNIT_ROABLE + UNIT_BCD, 0) },
	{ UDATA (NULL, UNIT_DISABLE + UNIT_ATTABLE +
		UNIT_ROABLE + UNIT_BCD, 0) },
	{ UDATA (NULL, UNIT_DISABLE + UNIT_ATTABLE +
		UNIT_ROABLE + UNIT_BCD, 0) },
	{ UDATA (NULL, UNIT_DISABLE + UNIT_ATTABLE +
		UNIT_ROABLE + UNIT_BCD, 0) }  };

REG mt_reg[] = {
	{ FLDATA (END, ind[IN_END], 0) },
	{ FLDATA (ERR, ind[IN_TAP], 0) },
	{ DRDATA (POS1, mt_unit[1].pos, 32), PV_LEFT + REG_RO },
	{ DRDATA (POS2, mt_unit[2].pos, 32), PV_LEFT + REG_RO },
	{ DRDATA (POS3, mt_unit[3].pos, 32), PV_LEFT + REG_RO },
	{ DRDATA (POS4, mt_unit[4].pos, 32), PV_LEFT + REG_RO },
	{ DRDATA (POS5, mt_unit[5].pos, 32), PV_LEFT + REG_RO },
	{ DRDATA (POS6, mt_unit[6].pos, 32), PV_LEFT + REG_RO },
	{ NULL }  };

MTAB mt_mod[] = {
	{ UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL }, 
	{ 0 }  };

DEVICE mt_dev = {
	"MT", mt_unit, mt_reg, mt_mod,
	MT_NUMDR, 10, 31, 1, 8, 8,
	NULL, NULL, &mt_reset,
	&mt_boot, &mt_attach, NULL };

/* Function routine

   Inputs:
	unit	=	unit character
	mod	=	modifier character
   Outputs:
	status	=	status
*/

t_stat mt_func (int32 unit, int32 mod)
{
int32 err, pnu;
t_mtrlnt tbc;
UNIT *uptr;
static t_mtrlnt bceof = { MTR_TMK };

if ((uptr = get_unit (unit)) == NULL) return STOP_INVMTU; /* valid unit? */
pnu = MT_TST_PNU (uptr);				/* get pos not upd */
MT_CLR_PNU (uptr);					/* and clear */
if ((uptr->flags & UNIT_ATT) == 0) return SCPE_UNATT;	/* attached? */
switch (mod) {						/* case on modifier */
case BCD_B:						/* backspace */
	ind[IN_END] = 0;				/* clear end of reel */
	if (pnu || (uptr->pos < sizeof (t_mtrlnt)))	/* bot or pnu? */
		return SCPE_OK;
	fseek (uptr->fileref, uptr->pos - sizeof (t_mtrlnt),
		SEEK_SET);
	fxread (&tbc, sizeof (t_mtrlnt), 1, uptr->fileref);
	if ((err = ferror (uptr->fileref)) ||		/* err or eof? */
	    feof (uptr->fileref)) break;
	if ((tbc == MTR_TMK) || (tbc == MTR_EOM))	/* tmk or eom? */
		uptr->pos = uptr->pos - sizeof (t_mtrlnt);
	else uptr->pos = uptr->pos - ((MTRL (tbc) + 1) & ~1) -
		(2 * sizeof (t_mtrlnt));
	break;  					/* end case */

case BCD_E:						/* erase = nop */
	if (uptr->flags & UNIT_WPRT) return STOP_MTL;
	return SCPE_OK;

case BCD_M:						/* write tapemark */
	if (uptr->flags & UNIT_WPRT) return STOP_MTL;
	fseek (uptr->fileref, uptr->pos, SEEK_SET);
	fxwrite (&bceof, sizeof (t_mtrlnt), 1, uptr->fileref);
	if (err = ferror (uptr->fileref)) MT_SET_PNU (uptr); /* error */
	else uptr->pos = uptr->pos + sizeof (t_mtrlnt);
	break;

case BCD_R:						/* rewind */
	uptr->pos = 0;					/* update position */
	return SCPE_OK;

case BCD_U:						/* unload */
	uptr->pos = 0;					/* update position */
	return detach_unit (uptr);			/* detach */

default:
	return STOP_INVM;  }

if (err != 0) {						/* I/O error */
	perror ("MT I/O error");
	clearerr (uptr->fileref);
	ind[IN_TAP] = 1;				/* set indicator */
	if (iochk) return SCPE_IOERR;  }
return SCPE_OK;
}

/* Read and write routines

   Inputs:
	unit	=	unit character
	flag	=	normal, word mark, or binary mode
	mod	=	modifier character
   Outputs:
	status	=	status
*/

t_stat mt_io (int32 unit, int32 flag, int32 mod)
{
int32 err, t, wm_seen;
t_mtrlnt i, tbc, ebc;
UNIT *uptr;

if ((uptr = get_unit (unit)) == NULL) return STOP_INVMTU; /* valid unit? */
uptr->flags = uptr->flags & ~UNIT_PNU;			/* clr pos not upd */
if ((uptr->flags & UNIT_ATT) == 0) return SCPE_UNATT;	/* attached? */

switch (mod) {
case BCD_R:						/* read */
	ind[IN_TAP] = ind[IN_END] = 0;			/* clear error */
	wm_seen = 0;					/* no word mk seen */
	fseek (uptr->fileref, uptr->pos, SEEK_SET);
	fxread (&tbc, sizeof (t_mtrlnt), 1, uptr->fileref);
	if (err = ferror (uptr->fileref)) break;	/* error? */
	if (feof (uptr->fileref) || (tbc == MTR_EOM)) {	/* eom or eof? */
		ind[IN_TAP] = 1;			/* pretend error */
		MT_SET_PNU (uptr);			/* pos not upd */
		break;  }
	if (tbc == MTR_TMK) {				/* tape mark? */
		ind[IN_END] = 1;			/* set end mark */
		uptr->pos = uptr->pos + sizeof (t_mtrlnt);
		break;  }
	if (MTRF (tbc)) ind[IN_TAP] = 1;		/* error? set flag */
	tbc = MTRL (tbc);				/* clear error flag */
	if (tbc > MT_MAXFR) return SCPE_MTRLNT;		/* record too long? */	
	i = fxread (dbuf, sizeof (int8), tbc, uptr->fileref);
	if (err = ferror (uptr->fileref)) break;	/* I/O error? */
	for ( ; i < tbc; i++) dbuf[i] = 0;		/* fill with 0's */
	uptr->pos = uptr->pos + ((tbc + 1) & ~1) +
		(2 * sizeof (t_mtrlnt));
	for (i = 0; i < tbc; i++) {			/* loop thru buf */
		if (M[BS] == (BCD_GRPMRK + WM)) {	/* GWM in memory? */
			BS++;				/* incr BS */
			if (ADDR_ERR (BS)) {		/* test for wrap */
				BS = BA | (BS % MAXMEMSIZE);
				return STOP_WRAP;  }
			return SCPE_OK;  }		/* done */
		t = dbuf[i];				/* get char */
		if ((flag != MD_BIN) && (t == BCD_ALT)) t = BCD_BLANK;
		if (flag == MD_WM) {			/* word mk mode? */
			if ((t == BCD_WM) && (wm_seen == 0)) wm_seen = WM;
			else {	M[BS] = wm_seen | (t & CHAR);
				wm_seen = 0;  }  }
		else M[BS] = (M[BS] & WM) | (t & CHAR);
		if (!wm_seen) BS++;
		if (ADDR_ERR (BS)) {			/* check next BS */
			BS = BA | (BS % MAXMEMSIZE);
			return STOP_WRAP;  }  }
	if (flag == MD_WM) M[BS] = WM | BCD_GRPMRK;	/* load? set WM */
	else M[BS] = (M[BS] & WM) | BCD_GRPMRK;		/* move? save WM */
	BS++;						/* adv BS */
	if (ADDR_ERR (BS)) {				/* check final BS */
		BS = BA | (BS % MAXMEMSIZE);
		return STOP_WRAP;  }
	break;

case BCD_W:
	if (uptr->flags & UNIT_WPRT) return STOP_MTL;	/* locked? */
	if (M[BS] == (BCD_GRPMRK + WM)) return STOP_MTZ;	/* eor? */
	ind[IN_TAP] = ind[IN_END] = 0;			/* clear error */
	for (tbc = 0; (t = M[BS++]) != (BCD_GRPMRK + WM); ) {
		if ((t & WM) && (flag == MD_WM)) dbuf[tbc++] = BCD_WM;
		if (((t & CHAR) == BCD_BLANK) && (flag != MD_BIN))
			dbuf[tbc++] = BCD_ALT;
		else dbuf[tbc++] = t & CHAR;
		if (ADDR_ERR (BS)) {			/* check next BS */
			BS = BA | (BS % MAXMEMSIZE);
			return STOP_WRAP;  }  }
	ebc = (tbc + 1) & ~1;				/* force even */
	fseek (uptr->fileref, uptr->pos, SEEK_SET);
	fxwrite (&tbc, sizeof (t_mtrlnt), 1, uptr->fileref);
	fxwrite (dbuf, sizeof (int8), ebc, uptr->fileref);
	fxwrite (&tbc, sizeof (t_mtrlnt), 1, uptr->fileref);
	if (err = ferror (uptr->fileref)) break;	/* I/O error? */
	uptr->pos = uptr->pos + ebc + (2 * sizeof (t_mtrlnt));
	if (ADDR_ERR (BS)) {				/* check final BS */
		BS = BA | (BS % MAXMEMSIZE);
		return STOP_WRAP;  }
	break;
default:
	return STOP_INVM;  }

if (err != 0) {						/* I/O error? */
	perror ("MT I/O error");
	clearerr (uptr->fileref);
	MT_SET_PNU (uptr);				/* pos not upd */
	ind[IN_TAP] = 1;				/* flag error */
	if (iochk) return SCPE_IOERR;  }
return SCPE_OK;
}

/* Get unit pointer from unit number */

UNIT *get_unit (int32 unit)
{
if ((unit <= 0) || (unit >= MT_NUMDR)) return NULL;
else return mt_dev.units + unit;
}

/* Reset routine */

t_stat mt_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

for (i = 0; i < MT_NUMDR; i++) {			/* clear pos flag */
	if (uptr = get_unit (i)) MT_CLR_PNU (uptr);  }
ind[IN_END] = ind[IN_TAP] = 0;				/* clear indicators */
return SCPE_OK;
}

/* Attach routine */

t_stat mt_attach (UNIT *uptr, char *cptr)
{
MT_CLR_PNU (uptr);
return attach_unit (uptr, cptr);
}

/* Bootstrap routine */

t_stat mt_boot (int32 unitno, DEVICE *dptr)
{
extern int32 saved_IS;

mt_unit[unitno].pos = 0;				/* force rewind */
BS = 1;							/* set BS = 001 */
mt_io (unitno, MD_WM, BCD_R);				/* LDA %U1 001 R */
saved_IS = 1;
return SCPE_OK;
}
