/* pdp18b_rf.c: fixed head disk simulator

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

   rf		(PDP-9) RF09/RF09
		(PDP-15) RF15/RS09

   06-Jan-02	RMS	Revised enable/disable support
   25-Nov-01	RMS	Revised interrupt structure
   24-Nov-01	RMS	Changed WLK to array
   26-Apr-01	RMS	Added device enable/disable support
   15-Feb-01	RMS	Fixed 3 cycle data break sequencing
   30-Nov-99	RMS	Added non-zero requirement to rf_time
   14-Apr-99	RMS	Changed t_addr to unsigned

   The RFxx is a head-per-track disk.  It uses the multicycle data break
   facility.  To minimize overhead, the entire RFxx is buffered in memory.

   Two timing parameters are provided:

   rf_time	Interword timing.  Must be non-zero.
   rf_burst	Burst mode.  If 0, DMA occurs cycle by cycle; otherwise,
		DMA occurs in a burst.
*/

#include "pdp18b_defs.h"
#include <math.h>

/* Constants */

#define RF_NUMWD	2048				/* words/track */
#define RF_NUMTR	128				/* tracks/disk */
#define RF_NUMDK	8				/* disks/controller */
#define RF_SIZE		(RF_NUMDK * RF_NUMTR * RF_NUMWD) /* words/drive */
#define RF_WMASK	(RF_NUMWD - 1)			/* word mask */
#define RF_WC		036				/* word count */
#define RF_CA		037				/* current addr */

/* Function/status register */

#define RFS_ERR		0400000				/* error */
#define RFS_HDW		0200000				/* hardware error */
#define RFS_APE		0100000				/* addr parity error */
#define RFS_MXF		0040000				/* missed transfer */
#define RFS_WCE		0020000				/* write check error */
#define RFS_DPE		0010000				/* data parity error */
#define RFS_WLO		0004000				/* write lock error */
#define RFS_NED		0002000				/* non-existent disk */
#define RFS_DCH		0001000				/* data chan timing */
#define RFS_PGE		0000400				/* programming error */
#define RFS_DON		0000200				/* transfer complete */
#define RFS_V_FNC	1				/* function */
#define RFS_M_FNC	03
#define RFS_FNC		(RFS_M_FNC << RFS_V_FNC)
#define  FN_NOP		0
#define	 FN_READ	1
#define  FN_WRITE	2
#define  FN_WCHK	3
#define RFS_IE		0000001				/* interrupt enable */

#define RFS_CLR		0000170				/* always clear */
#define RFS_EFLGS	(RFS_HDW | RFS_APE | RFS_MXF | RFS_WCE | \
			 RFS_DPE | RFS_WLO | RFS_NED )	/* error flags */
#define GET_FNC(x)	(((x) >> RFS_V_FNC) & RFS_M_FNC)
#define GET_POS(x)	((int) fmod (sim_gtime() / ((double) (x)), \
			((double) RF_NUMWD)))
#define RF_BUSY		(sim_is_active (&rf_unit))

extern int32 M[];
extern int32 int_hwre[API_HLVL+1], dev_enb;
extern UNIT cpu_unit;
int32 rf_sta = 0;					/* status register */
int32 rf_da = 0;					/* disk address */
int32 rf_dbuf = 0;					/* data buffer */
int32 rf_wlk[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };		/* write lock */
int32 rf_time = 10;					/* inter-word time */
int32 rf_burst = 1;					/* burst mode flag */
int32 rf_stopioe = 1;					/* stop on error */
t_stat rf_svc (UNIT *uptr);
t_stat rf_reset (DEVICE *dptr);
int32 rf_updsta (int32 new);

/* RF data structures

   rf_dev	RF device descriptor
   rf_unit	RF unit descriptor
   rf_reg	RF register list
*/

UNIT rf_unit =
	{ UDATA (&rf_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF,
	RF_SIZE) };

REG rf_reg[] = {
	{ ORDATA (STA, rf_sta, 18) },
	{ ORDATA (DA, rf_da, 21) },
	{ ORDATA (WC, M[RF_WC], 18) },
	{ ORDATA (CA, M[RF_CA], 18) },
	{ ORDATA (BUF, rf_dbuf, 18) },
	{ FLDATA (INT, int_hwre[API_RF], INT_V_RF) },
	{ BRDATA (WLK, rf_wlk, 8, 16, RF_NUMDK) },
	{ DRDATA (TIME, rf_time, 24), PV_LEFT + REG_NZ },
	{ FLDATA (BURST, rf_burst, 0) },
	{ FLDATA (STOP_IOE, rf_stopioe, 0) },
	{ FLDATA (*DEVENB, dev_enb, ENB_V_RF), REG_HRO },
	{ NULL }  };

MTAB rf_mod[] = {
	{ MTAB_XTD|MTAB_VDV, ENB_RF, NULL, "ENABLED", &set_enb },
	{ MTAB_XTD|MTAB_VDV, ENB_RF, NULL, "DISABLED", &set_dsb },
	{ 0 }  };

DEVICE rf_dev = {
	"RF", &rf_unit, rf_reg, rf_mod,
	1, 8, 21, 1, 8, 18,
	NULL, NULL, &rf_reset,
	NULL, NULL, NULL };

/* IOT routines */

int32 rf70 (int32 pulse, int32 AC)
{
int32 t;

if (pulse == 001)					/* DSSF */
	return (rf_sta & (RFS_ERR | RFS_DON))? IOT_SKP + AC: AC;
if (pulse == 021) rf_reset (&rf_dev);			/* DSCC */
if ((pulse & 061) == 041) {				/* DSCF */
	if (RF_BUSY) rf_sta = rf_sta | RFS_PGE;		/* busy inhibits */
	else rf_sta = rf_sta & ~(RFS_FNC | RFS_IE);  }	/* clear func */
if (pulse == 002)  {					/* DRBR */
	if (RF_BUSY) rf_sta = rf_sta | RFS_PGE;		/* busy sets PGE */
	return AC | rf_dbuf;  }
if (pulse == 022) {					/* DRAL */
	if (RF_BUSY) rf_sta = rf_sta | RFS_PGE;		/* busy sets PGE */
	return rf_da & 0777777;  }
if (pulse == 062) {					/* DRAH */
	if (RF_BUSY) rf_sta = rf_sta | RFS_PGE;		/* busy sets PGE */
	return (rf_da >> 18) | ((rf_sta & RFS_NED)? 010: 0);  }
if ((pulse & 062) == 042) {				/* DSFX */
	if (RF_BUSY) rf_sta = rf_sta | RFS_PGE;		/* busy inhibits */
	else rf_sta = rf_sta ^ (AC & (RFS_FNC | RFS_IE));  } /* xor func */
if (pulse == 004) {					/* DLBR */
	if (RF_BUSY) rf_sta = rf_sta | RFS_PGE;		/* busy inhibits */
	else rf_dbuf = AC;  }
if (pulse == 024) {					/* DLAL */
	if (RF_BUSY) rf_sta = rf_sta | RFS_PGE;		/* busy inhibits */
	else rf_da = (rf_da & ~0777777) | AC;  }
if (pulse == 064) {					/* DLAH */
	if (RF_BUSY) rf_sta = rf_sta | RFS_PGE;		/* busy inhibits */
	else rf_da = (rf_da & 0777777) | ((AC & 07) << 18);  }
if ((pulse & 064) == 044) {				/* DSCN */
	if (RF_BUSY) rf_sta = rf_sta | RFS_PGE;		/* busy inhibits */
	else if (GET_FNC (rf_sta) != FN_NOP) {
		t = (rf_da & RF_WMASK) - GET_POS (rf_time); /* delta to new */
		if (t < 0) t = t + RF_NUMWD;		     /* wrap around? */
		sim_activate (&rf_unit, t * rf_time);  }  }  /* schedule op */
rf_updsta (0);						/* update status */
return AC;
}

int32 rf72 (int32 pulse, int32 AC)
{
if (pulse == 002) return AC | GET_POS (rf_time) |	/* DLOK */
	(sim_is_active (&rf_unit)? 0400000: 0);
if (pulse == 042) {					/* DSCD */
	if (RF_BUSY) rf_sta = rf_sta | RFS_PGE;		/* busy inhibits */
	else rf_sta = 0;
	rf_updsta (0);  }
if (pulse == 062) {					/* DSRS */
	if (RF_BUSY) rf_sta = rf_sta | RFS_PGE;		/* busy sets PGE */
	return rf_updsta (0);  }
return AC;
}

/* Unit service

   This code assumes the entire disk is buffered.
*/

t_stat rf_svc (UNIT *uptr)
{
int32 f, pa, d, t;

if ((uptr -> flags & UNIT_BUF) == 0) {			/* not buf? abort */
	rf_updsta (RFS_NED | RFS_DON);			/* set nxd, done */
	return IORETURN (rf_stopioe, SCPE_UNATT);  }

f = GET_FNC (rf_sta);					/* get function */
do {	M[RF_WC] = (M[RF_WC] + 1) & 0777777;		/* incr word count */
 	pa = M[RF_CA] = (M[RF_CA] + 1) & ADDRMASK;	/* incr mem addr */
	if ((f == FN_READ) && MEM_ADDR_OK (pa))		/* read? */
		M[pa] = *(((int32 *) uptr -> filebuf) + rf_da);
	if ((f == FN_WCHK) &&				/* write check? */
	    (M[pa] != *(((int32 *) uptr -> filebuf) + rf_da))) {
		rf_updsta (RFS_WCE);			/* flag error */
		break;  }
	if (f == FN_WRITE) {				/* write? */
		d = (rf_da >> 18) & 07;			/* disk */
		t = (rf_da >> 14) & 017;		/* track groups */
		if ((rf_wlk[d] >> t) & 1) {		/* write locked? */
			rf_updsta (RFS_WLO);
			break;  }
		else {	*(((int32 *) uptr -> filebuf) + rf_da) = M[pa];
			if (((t_addr) rf_da) >= uptr -> hwmark)
				uptr -> hwmark = rf_da + 1;  }  }
	rf_da = rf_da + 1;				/* incr disk addr */
	if (rf_da > RF_SIZE) {				/* disk overflow? */
		rf_da = 0;
		rf_updsta (RFS_NED);			/* nx disk error */
		break;  }  }
while ((M[RF_WC] != 0) && (rf_burst != 0));		/* brk if wc, no brst */

if ((M[RF_WC] != 0) && ((rf_sta & RFS_ERR) == 0))	/* more to do? */
	sim_activate (&rf_unit, rf_time);		/* sched next */
else rf_updsta (RFS_DON);
return SCPE_OK;
}

/* Update status */

int32 rf_updsta (int32 new)
{
rf_sta = (rf_sta | new) & ~(RFS_ERR | RFS_CLR);
if (rf_sta & RFS_EFLGS) rf_sta = rf_sta | RFS_ERR;
if ((rf_sta & (RFS_ERR | RFS_DON)) && (rf_sta & RFS_IE))
	 SET_INT (RF);
else CLR_INT (RF);
return rf_sta;
}

/* Reset routine */

t_stat rf_reset (DEVICE *dptr)
{
rf_sta = rf_da = rf_dbuf = 0;
rf_updsta (0);
sim_cancel (&rf_unit);
return SCPE_OK;
}

/* IORS routine */

int32 rf_iors (void)
{
return ((rf_sta & (RFS_ERR | RFS_DON))? IOS_RF: 0);
}
