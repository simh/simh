/* i1401_mt.c: IBM 1401 magnetic tape simulator

   Copyright (c) 1993-2001, Robert M. Supnik

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
#define UNIT_WLK	(1 << UNIT_V_WLK)
#define UNIT_W_UF	2				/* #save flags */
#define UNIT_WPRT	(UNIT_WLK | UNIT_RO)		/* write protect */

extern uint8 M[];					/* memory */
extern int32 ind[64];
extern int32 BS, iochk;
extern UNIT cpu_unit;
unsigned int8 dbuf[MAXMEMSIZE * 2];			/* tape buffer */
t_stat mt_reset (DEVICE *dptr);
t_stat mt_boot (int32 unitno);
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
	{ FLDATA (PAR, ind[IN_PAR], 0) },
	{ DRDATA (POS1, mt_unit[1].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS2, mt_unit[2].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS3, mt_unit[3].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS4, mt_unit[4].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS5, mt_unit[5].pos, 31), PV_LEFT + REG_RO },
	{ DRDATA (POS6, mt_unit[6].pos, 31), PV_LEFT + REG_RO },
	{ GRDATA (FLG1, mt_unit[1].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG2, mt_unit[2].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG3, mt_unit[3].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG4, mt_unit[4].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG5, mt_unit[5].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG6, mt_unit[6].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ NULL }  };

MTAB mt_mod[] = {
	{ UNIT_WLK, 0, "write enabled", "ENABLED", NULL },
	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL }, 
	{ 0 }  };

DEVICE mt_dev = {
	"MT", mt_unit, mt_reg, mt_mod,
	MT_NUMDR, 10, 31, 1, 8, 8,
	NULL, NULL, &mt_reset,
	&mt_boot, NULL, NULL };

/* Function routine

   Inputs:
	unit	=	unit character
	mod	=	modifier character
   Outputs:
	status	=	status
*/

t_stat mt_func (int32 unit, int32 mod)
{
int32 err;
t_mtrlnt tbc;
UNIT *uptr;
static t_mtrlnt bceof = { 0 };

if ((uptr = get_unit (unit)) == NULL) return STOP_INVMTU; /* valid unit? */
if ((uptr -> flags & UNIT_ATT) == 0) return SCPE_UNATT;	/* attached? */
switch (mod) {						/* case on modifier */
case BCD_B:						/* backspace */
	ind[IN_END] = 0;				/* clear end of reel */
	if (uptr -> pos == 0) return SCPE_OK;		/* at bot? */
	fseek (uptr -> fileref, uptr -> pos - sizeof (t_mtrlnt),
		SEEK_SET);
	fxread (&tbc, sizeof (t_mtrlnt), 1, uptr -> fileref);
	if ((err = ferror (uptr -> fileref)) ||
	    (feof (uptr -> fileref))) break;
	if (tbc == 0)				/* file mark? */
		uptr -> pos = uptr -> pos - sizeof (t_mtrlnt);
	else uptr -> pos = uptr -> pos - ((MTRL (tbc) + 1) & ~1) -
		(2 * sizeof (t_mtrlnt));
	break;  					/* end case */
case BCD_E:						/* erase = nop */
	if (uptr -> flags & UNIT_WPRT) return STOP_MTL;
	return SCPE_OK;
case BCD_M:						/* write tapemark */
	if (uptr -> flags & UNIT_WPRT) return STOP_MTL;
	fseek (uptr -> fileref, uptr -> pos, SEEK_SET);
	fxwrite (&bceof, sizeof (t_mtrlnt), 1, uptr -> fileref);
	err = ferror (uptr -> fileref);
	uptr -> pos = uptr -> pos + sizeof (t_mtrlnt);
	break;
case BCD_R:						/* rewind */
	uptr -> pos = 0;				/* update position */
	return SCPE_OK;
case BCD_U:						/* unload */
	uptr -> pos = 0;				/* update position */
	return detach_unit (uptr);			/* detach */
default:
	return STOP_INVM;  }
if (err != 0) {						/* I/O error */
	perror ("MT I/O error");
	clearerr (uptr -> fileref);
	if (iochk) return SCPE_IOERR;
	ind[IN_TAP] = 1;  }
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
int32 err, i, t, wm_seen;
t_mtrlnt tbc;
UNIT *uptr;

if ((uptr = get_unit (unit)) == NULL) return STOP_INVMTU; /* valid unit? */
if ((uptr -> flags & UNIT_ATT) == 0) return SCPE_UNATT;	/* attached? */
switch (mod) {
case BCD_R:						/* read */
	ind[IN_TAP] = ind[IN_END] = 0;			/* clear error */
	wm_seen = 0;					/* no word mk seen */
	fseek (uptr -> fileref, uptr -> pos, SEEK_SET);
	fxread (&tbc, sizeof (t_mtrlnt), 1, uptr -> fileref);
	if ((err = ferror (uptr -> fileref)) || (feof (uptr -> fileref))) {
		ind[IN_END] = 1;			/* err or eof? */
		break;  }
	if (tbc == 0) {					/* tape mark? */
		ind[IN_END] = 1;			/* set end mark */
		uptr -> pos = uptr -> pos + sizeof (t_mtrlnt);
		break;  }
	tbc = MTRL (tbc);				/* ignore error flag */		
	i = fxread (dbuf, sizeof (int8), tbc, uptr -> fileref);
	for ( ; i < tbc; i++) dbuf[i] = 0;		/* fill with 0's */
	err = ferror (uptr -> fileref);
	uptr -> pos = uptr -> pos + ((tbc + 1) & ~1) +
		(2 * sizeof (t_mtrlnt));
	for (i = 0; (i < tbc) && (M[BS] != (BCD_GRPMRK + WM)); i++) {
		t = dbuf[i];				/* get char */
		if ((flag != MD_BIN) && (t == BCD_ALT)) t = BCD_BLANK;
		if (flag == MD_WM) {			/* word mk mode? */
			if ((t == BCD_WM) && (wm_seen == 0)) wm_seen = WM;
			else {	M[BS] = wm_seen | (t & CHAR);
				wm_seen = 0;  }  }
		else M[BS] = (M[BS] & WM) | (t & CHAR);
		if (!wm_seen) BS++;
		if (ADDR_ERR (BS)) {
			BS = BA | (BS % MAXMEMSIZE);
			return STOP_NXM;  }  }
	M[BS++] = BCD_GRPMRK + WM;			/* end of record */
	break;

case BCD_W:
	if (uptr -> flags & UNIT_WPRT) return STOP_MTL;	/* locked? */
	if (M[BS] == (BCD_GRPMRK + WM)) return STOP_MTZ;	/* eor? */
	ind[IN_TAP] = ind[IN_END] = 0;			/* clear error */
	for (tbc = 0; (t = M[BS++]) != (BCD_GRPMRK + WM); ) {
		if ((t & WM) && (flag == MD_WM)) dbuf[tbc++] = BCD_WM;
		if (((t & CHAR) == BCD_BLANK) && (flag != MD_BIN))
			dbuf[tbc++] = BCD_ALT;
		else dbuf[tbc++] = t & CHAR;
		if (ADDR_ERR (BS)) {
			BS = BA | (BS % MAXMEMSIZE);
			return STOP_NXM;  }  }
	fseek (uptr -> fileref, uptr -> pos, SEEK_SET);
	fxwrite (&tbc, sizeof (t_mtrlnt), 1, uptr -> fileref);
	fxwrite (dbuf, sizeof (int8), (tbc + 1) & ~1, uptr -> fileref);
	fxwrite (&tbc, sizeof (t_mtrlnt), 1, uptr -> fileref);
	err = ferror (uptr -> fileref);
	uptr -> pos = uptr -> pos + ((tbc + 1) & ~1) +
		(2 * sizeof (t_mtrlnt));
	break;
default:
	return STOP_INVM;  }

if (err != 0) {						/* I/O error */
	perror ("MT I/O error");
	clearerr (uptr -> fileref);
	if (iochk) return SCPE_IOERR;
	ind[IN_TAP] = 1;  }				/* flag error */
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
ind[IN_END] = ind[IN_PAR] = ind[IN_TAP] = 0;		/* clear indicators */
return SCPE_OK;
}

/* Bootstrap routine */

#define BOOT_START 3980
#define BOOT_LEN (sizeof (boot_rom) / sizeof (unsigned char))

static const unsigned char boot_rom[] = {
	OP_LCA + WM, BCD_PERCNT, BCD_U, BCD_ONE,
		BCD_ZERO, BCD_ZERO, BCD_ONE, BCD_R,	/* LDA %U1 001 R */
	OP_B + WM, BCD_ZERO, BCD_ZERO, BCD_ONE,		/* B 001 */
	OP_H + WM };					/* HALT */

t_stat mt_boot (int32 unitno)
{
int32 i;
extern int32 saved_IS;

mt_unit[unitno].pos = 0;
for (i = 0; i < BOOT_LEN; i++) M[BOOT_START + i] = boot_rom[i];
M[BOOT_START + 3] = unitno & 07;
saved_IS = BOOT_START;
return SCPE_OK;
}
