/* i1401_mt.c: IBM 1401 magnetic tape simulator

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

   mt		7-track magtape

   28-Feb-03	RMS	Modified for magtape library
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
#include "sim_tape.h"

#define MT_NUMDR	7				/* #drives */
#define MT_MAXFR	(MAXMEMSIZE * 2)		/* max transfer */

extern uint8 M[];					/* memory */
extern int32 ind[64];
extern int32 BS, iochk;
extern UNIT cpu_unit;
uint8 dbuf[MT_MAXFR];					/* tape buffer */
t_stat mt_reset (DEVICE *dptr);
t_stat mt_boot (int32 unitno, DEVICE *dptr);
t_stat mt_map_status (t_stat st);
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
	{ MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL },
	{ MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL }, 
	{ 0 }  };

DEVICE mt_dev = {
	"MT", mt_unit, mt_reg, mt_mod,
	MT_NUMDR, 10, 31, 1, 8, 8,
	NULL, NULL, &mt_reset,
	&mt_boot, &sim_tape_attach, &sim_tape_detach };

/* Function routine

   Inputs:
	unit	=	unit character
	mod	=	modifier character
   Outputs:
	status	=	status
*/

t_stat mt_func (int32 unit, int32 mod)
{
t_mtrlnt tbc;
UNIT *uptr;
t_stat st;

if ((uptr = get_unit (unit)) == NULL) return STOP_INVMTU; /* valid unit? */
if ((uptr->flags & UNIT_ATT) == 0) return SCPE_UNATT;	/* attached? */
switch (mod) {						/* case on modifier */
case BCD_B:						/* backspace */
	ind[IN_END] = 0;				/* clear end of file */
	st = sim_tape_sprecr (uptr, &tbc);		/* space rev */
	break;  					/* end case */

case BCD_E:						/* erase = nop */
	if (sim_tape_wrp (uptr)) return STOP_MTL;
	return SCPE_OK;

case BCD_M:						/* write tapemark */
	st = sim_tape_wrtmk (uptr);			/* write tmk */
	break;

case BCD_R:						/* rewind */
	sim_tape_rewind (uptr);				/* update position */
	return SCPE_OK;

case BCD_U:						/* unload */
	sim_tape_rewind (uptr);				/* update position */
	return detach_unit (uptr);			/* detach */

default:
	return STOP_INVM;  }

return mt_map_status (st);
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
int32 t, wm_seen;
t_mtrlnt i, tbc;
t_stat st;
UNIT *uptr;

if ((uptr = get_unit (unit)) == NULL) return STOP_INVMTU; /* valid unit? */
if ((uptr->flags & UNIT_ATT) == 0) return SCPE_UNATT;	/* attached? */

switch (mod) {
case BCD_R:						/* read */
	ind[IN_TAP] = ind[IN_END] = 0;			/* clear error */
	wm_seen = 0;					/* no word mk seen */
	st = sim_tape_rdrecf (uptr, dbuf, &tbc, MT_MAXFR);	/* read rec */
	if (st == MTSE_RECE) ind[IN_TAP] = 1;		/* rec in error? */
	else if (st != MTSE_OK) break;			/* stop on error */
	for (i = 0; i < tbc; i++) {			/* loop thru buf */
	    if (M[BS] == (BCD_GRPMRK + WM)) {		/* GWM in memory? */
		BS++;					/* incr BS */
		if (ADDR_ERR (BS)) {			/* test for wrap */
		    BS = BA | (BS % MAXMEMSIZE);
		    return STOP_WRAP;  }
		return SCPE_OK;  }			/* done */
	    t = dbuf[i];				/* get char */
	    if ((flag != MD_BIN) && (t == BCD_ALT)) t = BCD_BLANK;
	    if (flag == MD_WM) {			/* word mk mode? */
		if ((t == BCD_WM) && (wm_seen == 0)) wm_seen = WM;
		else {
		    M[BS] = wm_seen | (t & CHAR);
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
	if (uptr->flags & MTUF_WRP) return STOP_MTL;	/* locked? */
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
	st = sim_tape_wrrecf (uptr, dbuf, tbc);		/* write record */
	if (ADDR_ERR (BS)) {				/* check final BS */
	    BS = BA | (BS % MAXMEMSIZE);
	    return STOP_WRAP;  }
	break;
default:
	return STOP_INVM;  }

return mt_map_status (st);
}

/* Get unit pointer from unit number */

UNIT *get_unit (int32 unit)
{
if ((unit <= 0) || (unit >= MT_NUMDR)) return NULL;
return mt_dev.units + unit;
}

/* Map tape status */

t_stat mt_map_status (t_stat st)
{
switch (st) {
case MTSE_OK:						/* no error */
case MTSE_BOT:						/* reverse into BOT */
	break;
case MTSE_FMT:						/* illegal fmt */
	return SCPE_IERR;
case MTSE_UNATT:					/* not attached */
	return SCPE_UNATT;
case MTSE_INVRL:					/* invalid rec lnt */
	return SCPE_MTRLNT;
case MTSE_TMK:						/* end of file */
	ind[IN_END] = 1;				/* set end mark */
	break;
case MTSE_IOERR:					/* IO error */
	ind[IN_TAP] = 1;				/* set error */
	if (iochk) return SCPE_IOERR;
	break;
case MTSE_RECE:						/* record in error */
case MTSE_EOM:						/* end of medium */
	ind[IN_TAP] = 1;				/* set error */
	break;
case MTSE_WRP:						/* write protect */
	return STOP_MTL;  }
return SCPE_OK;
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

/* Bootstrap routine */

t_stat mt_boot (int32 unitno, DEVICE *dptr)
{
extern int32 saved_IS;

sim_tape_rewind (&mt_unit[unitno]);			/* force rewind */
BS = 1;							/* set BS = 001 */
mt_io (unitno, MD_WM, BCD_R);				/* LDA %U1 001 R */
saved_IS = 1;
return SCPE_OK;
}
