/* nova_dsk.c: 4019 fixed head disk simulator

   Copyright (c) 1993-2004, Robert M. Supnik

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

   dsk		fixed head disk

   04-Jan-04	RMS	Changed sim_fsize calling sequence
   26-Jul-03	RMS	Fixed bug in set size routine
   14-Mar-03	RMS	Fixed variable capacity interaction with save/restore
   03-Mar-03	RMS	Fixed variable capacity and autosizing
   03-Oct-02	RMS	Added DIB
   06-Jan-02	RMS	Revised enable/disable support
   23-Aug-01	RMS	Fixed bug in write watermarking
   26-Apr-01	RMS	Added device enable/disable support
   10-Dec-00	RMS	Added Eclipse support
   15-Oct-00	RMS	Editorial changes
   14-Apr-99	RMS	Changed t_addr to unsigned

   The 4019 is a head-per-track disk.  To minimize overhead, the entire disk
   is buffered in memory.
*/

#include "nova_defs.h"
#include <math.h>

#define UNIT_V_AUTO	(UNIT_V_UF + 0)			/* autosize */
#define UNIT_V_PLAT	(UNIT_V_UF + 1)			/* #platters - 1 */
#define UNIT_M_PLAT	07
#define UNIT_PLAT	(UNIT_M_PLAT << UNIT_V_PLAT)
#define UNIT_GETP(x)	((((x) >> UNIT_V_PLAT) & UNIT_M_PLAT) + 1)
#define UNIT_AUTO	(1 << UNIT_V_AUTO)
#define UNIT_PLAT	(UNIT_M_PLAT << UNIT_V_PLAT)

/* Constants */

#define DSK_NUMWD	256				/* words/sector */
#define DSK_NUMSC	8				/* sectors/track */
#define DSK_NUMTR	128				/* tracks/disk */
#define DSK_DKSIZE	(DSK_NUMTR*DSK_NUMSC*DSK_NUMWD)	/* words/disk */
#define DSK_AMASK	((DSK_NUMDK*DSK_NUMTR*DSK_NUMSC) - 1)	/* address mask */
#define DSK_NUMDK	8				/* disks/controller */
#define GET_DISK(x)	(((x) / (DSK_NUMSC * DSK_NUMTR)) & (DSK_NUMDK - 1))

/* Parameters in the unit descriptor */

#define FUNC		u4				/* function */

/* Status register */

#define DSKS_WLS	020				/* write lock status */
#define DSKS_DLT	010				/* data late error */
#define DSKS_NSD	004				/* non-existent disk */
#define DSKS_CRC	002				/* parity error */
#define DSKS_ERR	001				/* error summary */
#define DSKS_ALLERR	(DSKS_WLS | DSKS_DLT | DSKS_NSD | DSKS_CRC | DSKS_ERR)

/* Map logical sector numbers to physical sector numbers
   (indexed by track<2:0>'sector)
*/

static const int32 sector_map[] = {
	0, 2, 4, 6, 1, 3, 5, 7, 1, 3, 5, 7, 2, 4, 6, 0,
	2, 4, 6, 0, 3, 5, 7, 1, 3, 5, 7, 1, 4, 6, 0, 2,
	4, 6, 0, 2, 5, 7, 1, 3, 5, 7, 1, 3, 6, 0, 2, 4,
	6, 0, 2, 4, 7, 1, 3, 5, 7, 1, 3, 5, 0, 2, 4, 6  };

#define DSK_MMASK	077
#define GET_SECTOR(x)	((int) fmod (sim_gtime() / ((double) (x)), \
			((double) DSK_NUMSC)))

extern uint16 M[];
extern UNIT cpu_unit;
extern int32 int_req, dev_busy, dev_done, dev_disable;

int32 dsk_stat = 0;					/* status register */
int32 dsk_da = 0;					/* disk address */
int32 dsk_ma = 0;					/* memory address */
int32 dsk_wlk = 0;					/* wrt lock switches */
int32 dsk_stopioe = 1;					/* stop on error */
int32 dsk_time = 100;					/* time per sector */

DEVICE dsk_dev;
int32 dsk (int32 pulse, int32 code, int32 AC);
t_stat dsk_svc (UNIT *uptr);
t_stat dsk_reset (DEVICE *dptr);
t_stat dsk_boot (int32 unitno, DEVICE *dptr);
t_stat dsk_attach (UNIT *uptr, char *cptr);
t_stat dsk_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);

/* DSK data structures

   dsk_dev	device descriptor
   dsk_unit	unit descriptor
   dsk_reg	register list
*/

DIB dsk_dib = { DEV_DSK, INT_DSK, PI_DSK, &dsk };

UNIT dsk_unit =
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF,
		DSK_DKSIZE) };

REG dsk_reg[] = {
	{ ORDATA (STAT, dsk_stat, 16) },
	{ ORDATA (DA, dsk_da, 16) },
	{ ORDATA (MA, dsk_ma, 16) },
	{ FLDATA (BUSY, dev_busy, INT_V_DSK) },
	{ FLDATA (DONE, dev_done, INT_V_DSK) },
	{ FLDATA (DISABLE, dev_disable, INT_V_DSK) },
	{ FLDATA (INT, int_req, INT_V_DSK) },
	{ ORDATA (WLK, dsk_wlk, 8) },
	{ DRDATA (TIME, dsk_time, 24), REG_NZ + PV_LEFT },
	{ FLDATA (STOP_IOE, dsk_stopioe, 0) },
	{ NULL }  };

MTAB dsk_mod[] = {
	{ UNIT_PLAT, (0 << UNIT_V_PLAT), NULL, "1P", &dsk_set_size },
	{ UNIT_PLAT, (1 << UNIT_V_PLAT), NULL, "2P", &dsk_set_size },
	{ UNIT_PLAT, (2 << UNIT_V_PLAT), NULL, "3P", &dsk_set_size },
	{ UNIT_PLAT, (3 << UNIT_V_PLAT), NULL, "4P", &dsk_set_size },
	{ UNIT_PLAT, (4 << UNIT_V_PLAT), NULL, "5P", &dsk_set_size },
	{ UNIT_PLAT, (5 << UNIT_V_PLAT), NULL, "6P", &dsk_set_size },
	{ UNIT_PLAT, (6 << UNIT_V_PLAT), NULL, "7P", &dsk_set_size },
	{ UNIT_PLAT, (7 << UNIT_V_PLAT), NULL, "8P", &dsk_set_size },
	{ UNIT_AUTO, UNIT_AUTO, "autosize", "AUTOSIZE", NULL },
	{ 0 }  };

DEVICE dsk_dev = {
	"DK", &dsk_unit, dsk_reg, dsk_mod,
	1, 8, 21, 1, 8, 16,
	NULL, NULL, &dsk_reset,
	&dsk_boot, &dsk_attach, NULL,
	&dsk_dib, DEV_DISABLE };

/* IOT routine */

int32 dsk (int32 pulse, int32 code, int32 AC)
{
int32 t, rval;

rval = 0;
switch (code) {						/* decode IR<5:7> */
case ioDIA:						/* DIA */
	rval = dsk_stat & DSKS_ALLERR;			/* read status */
	break;
case ioDOA:						/* DOA */
	dsk_da = AC & DSK_AMASK;			/* save disk addr */
	break;
case ioDIB:						/* DIB */
	rval = dsk_ma & AMASK;				/* read mem addr */
	break;
case ioDOB:						/* DOB */
	dsk_ma = AC & AMASK;				/* save mem addr */
	break;  }					/* end switch code */

if (pulse) {						/* any pulse? */
	dev_busy = dev_busy & ~INT_DSK;			/* clear busy */
	dev_done = dev_done & ~INT_DSK;			/* clear done */
	int_req = int_req & ~INT_DSK;			/* clear int */
	dsk_stat = 0;					/* clear status */
	sim_cancel (&dsk_unit);  }			/* stop I/O */

if ((pulse == iopP) && ((dsk_wlk >> GET_DISK (dsk_da)) & 1)) {	/* wrt lock? */
	dev_done = dev_done | INT_DSK;			/* set done */
	int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
	dsk_stat = DSKS_ERR + DSKS_WLS;			/* set status */
	return rval;  }

if (pulse & 1) {					/* read or write? */
	if (((uint32) (dsk_da * DSK_NUMWD)) >= dsk_unit.capac) { /* inv sev? */
	    dev_done = dev_done | INT_DSK;		/* set done */
	    int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
	    dsk_stat = DSKS_ERR + DSKS_NSD;		/* set status */
	    return rval;  }				/* done */
	dsk_unit.FUNC = pulse;				/* save command */
	dev_busy = dev_busy | INT_DSK;			/* set busy */
	t = sector_map[dsk_da & DSK_MMASK] - GET_SECTOR (dsk_time);
	if (t < 0) t = t + DSK_NUMSC;
	sim_activate (&dsk_unit, t * dsk_time);  }	/* activate */
return rval;
}

/* Unit service */

t_stat dsk_svc (UNIT *uptr)
{
int32 i, da, pa;
int16 *fbuf = uptr->filebuf;

dev_busy = dev_busy & ~INT_DSK;				/* clear busy */
dev_done = dev_done | INT_DSK;				/* set done */
int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);

if ((uptr->flags & UNIT_BUF) == 0) {			/* not buf? abort */
	dsk_stat = DSKS_ERR + DSKS_NSD;			/* set status */
	return IORETURN (dsk_stopioe, SCPE_UNATT);  }

da = dsk_da * DSK_NUMWD;				/* calc disk addr */
if (uptr->FUNC == iopS) {				/* read? */
	for (i = 0; i < DSK_NUMWD; i++) {		/* copy sector */
	    pa = MapAddr (0, (dsk_ma + i) & AMASK);	/* map address */
	    if (MEM_ADDR_OK (pa)) M[pa] = fbuf[da + i];  }
	dsk_ma = (dsk_ma + DSK_NUMWD) & AMASK;  }
if (uptr->FUNC == iopP) {				/* write? */
	for (i = 0; i < DSK_NUMWD; i++) {		/* copy sector */
	    pa = MapAddr (0, (dsk_ma + i) & AMASK);	/* map address */
	    fbuf[da + i] = M[pa];  }
	if (((uint32) (da + i)) >= uptr->hwmark)	/* past end? */
	    uptr->hwmark = da + i + 1;			/* upd hwmark */
	dsk_ma = (dsk_ma + DSK_NUMWD + 3) & AMASK;  }

dsk_stat = 0;						/* set status */
return SCPE_OK;
}

/* Reset routine */

t_stat dsk_reset (DEVICE *dptr)
{
dsk_stat = dsk_da = dsk_ma = 0;
dev_busy = dev_busy & ~INT_DSK;				/* clear busy */
dev_done = dev_done & ~INT_DSK;				/* clear done */
int_req = int_req & ~INT_DSK;				/* clear int */
sim_cancel (&dsk_unit);
return SCPE_OK;
}

/* Bootstrap routine */

#define BOOT_START 2000
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int))

static const int32 boot_rom[] = {
	060220,			/* NIOC DSK		; clear disk */
	0102400,		/* SUB 0,0		; addr = 0 */
	061020,			/* DOA 0,DSK		; set disk addr */
	062120,			/* DOBS 0,DSK		; set mem addr, rd */
	063620,			/* SKPDN DSK		; done? */
	000776,			/* JMP .-2 */
	000377,			/* JMP 377 */
};

t_stat dsk_boot (int32 unitno, DEVICE *dptr)
{
int32 i;
extern int32 saved_PC;

for (i = 0; i < BOOT_LEN; i++) M[BOOT_START + i] = boot_rom[i];
saved_PC = BOOT_START;
return SCPE_OK;
}

/* Attach routine */

t_stat dsk_attach (UNIT *uptr, char *cptr)
{
uint32 sz, p;
uint32 ds_bytes = DSK_DKSIZE * sizeof (int16);
t_stat r;

r = attach_unit (uptr, cptr);
if (r != SCPE_OK) return r;
if ((uptr->flags & UNIT_AUTO) && (sz = sim_fsize (uptr->fileref))) {
	p = (sz + ds_bytes - 1) / ds_bytes;
	if (p >= DSK_NUMDK) p = DSK_NUMDK - 1;
	uptr->flags = (uptr->flags & ~UNIT_PLAT) |
	    (p << UNIT_V_PLAT);  }
uptr->capac = UNIT_GETP (uptr->flags) * DSK_DKSIZE;	/* set capacity */
return SCPE_OK;
}

/* Change disk size */

t_stat dsk_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (val < 0) return SCPE_IERR;
if (uptr->flags & UNIT_ATT) return SCPE_ALATT;
uptr->capac = UNIT_GETP (val) * DSK_DKSIZE;
uptr->flags = uptr->flags & ~UNIT_AUTO;
return SCPE_OK;
}
