/* ibm1130_disk.c: IBM 1130 disk IO simulator

NOTE - there is a problem with this code. The Device Status Word (DSW) is
computed from current conditions when requested by an XIO load status
command; the value of DSW available to the simulator's examine & save
commands may NOT be accurate. This should probably be fixed.

   Copyright (c) 2002, Brian Knittel
   Based on PDP-11 simulator written by Robert M Supnik

   Revision History

   31July2001 - Derived from pdp11_stddev.c, which carries this disclaimer:
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
*/

#include "ibm1130_defs.h"

/* Constants */

#define DSK_NUMWD	321				/* words/sector */
#define DSK_NUMSC	4				/* sectors/surface */
#define DSK_NUMSF	2				/* surfaces/cylinder */
#define DSK_NUMCY	203				/* cylinders/drive */
#define DSK_NUMTR	(DSK_NUMCY * DSK_NUMSF)		/* tracks/drive */
#define DSK_NUMDR	5				/* drives/controller */
#define DSK_SIZE (DSK_NUMCY * DSK_NUMSF * DSK_NUMSC * DSK_NUMWD)  /* words/drive */

#define UNIT_V_RONLY    (UNIT_V_UF + 0)	/* hwre write lock */
#define UNIT_V_OPERR    (UNIT_V_UF + 1)	/* operation error flag */
#define UNIT_V_HARDERR  (UNIT_V_UF + 2)	/* hard error flag (reset on power down) */
#define UNIT_RONLY	 (1u << UNIT_V_RONLY)
#define UNIT_OPERR	 (1u << UNIT_V_OPERR)
#define UNIT_HARDERR (1u << UNIT_V_HARDERR)

static int16 dsk_dsw[DSK_NUMDR] = {0};	/* device status words */
static int16 dsk_sec[DSK_NUMDR] = {0};	/* next-sector-up */
int32 dsk_swait = 10;					/* seek time */
int32 dsk_rwait = 10;					/* rotate time */

#define DSK_DSW_DATA_ERROR				0x8000		/* device status word bits */
#define DSK_DSW_OP_COMPLETE				0x4000
#define DSK_DSW_NOT_READY				0x2000
#define DSK_DSW_DISK_BUSY				0x1000
#define DSK_DSW_CARRIAGE_HOME			0x0800
#define DSK_DSW_SECTOR_MASK				0x0003
					
static t_stat dsk_svc    (UNIT *uptr);
static t_stat dsk_reset  (DEVICE *dptr);
static t_stat dsk_attach (UNIT *uptr, char *cptr);
static t_stat dsk_detach (UNIT *uptr);
static t_stat dsk_boot   (int unitno);

static void diskfail (UNIT *uptr, int errflag);

/* DSK data structures

   dsk_dev	disk device descriptor
   dsk_unit	unit descriptor
   dsk_reg	register list
*/

UNIT dsk_unit[] = {
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE) },
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE) },
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE) },
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE) },
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE) }
};

#define IS_ONLINE(u) (((u)->flags & (UNIT_ATT|UNIT_DIS)) == UNIT_ATT)

/* Parameters in the unit descriptor */

#define CYL		u3				/* current cylinder */
#define FUNC	u4				/* current function */

REG dsk_reg[] = {
	{ HRDATA (DSKDSW0, dsk_dsw[0], 16) },
	{ HRDATA (DSKDSW1, dsk_dsw[1], 16) },
	{ HRDATA (DSKDSW2, dsk_dsw[2], 16) },
	{ HRDATA (DSKDSW3, dsk_dsw[3], 16) },
	{ HRDATA (DSKDSW4, dsk_dsw[4], 16) },
	{ DRDATA (STIME,   dsk_swait, 24), PV_LEFT },
	{ DRDATA (RTIME,   dsk_rwait, 24), PV_LEFT },
	{ NULL }  };

MTAB dsk_mod[] = {
	{ UNIT_RONLY, 0,          "write enabled", "ENABLED", NULL },
	{ UNIT_RONLY, UNIT_RONLY, "write locked",  "LOCKED",  NULL },
	{ 0 }  };

DEVICE dsk_dev = {
	"DSK", dsk_unit, dsk_reg, dsk_mod,
	DSK_NUMDR, 16, 16, 1, 16, 16,
	NULL, NULL, &dsk_reset,
	dsk_boot, dsk_attach, dsk_detach};

static int32 dsk_ilswbit[DSK_NUMDR] = {		/* interrupt level status word bits for the drives */
	ILSW_2_1131_DISK,
	ILSW_2_2310_DRV_1,
	ILSW_2_2310_DRV_2,
	ILSW_2_2310_DRV_3,
	ILSW_2_2310_DRV_4,
};

static int32 dsk_ilswlevel[DSK_NUMDR] =
{
	2,										/* interrupt levels for the drives */
	2, 2, 2, 2
};

/* xio_disk - XIO command interpreter for the disk drives */
/*
 * device status word:
 *
 * 0 data error, occurs when:
 *		1. A modulo 4 error is detected during a read, read-check, or write operation. 
 *		2. The disk storage is in a read or write mode at the leading edge of a sector pulse. 
 *		3. A seek-incomplete signal is received from the 2311.
 *		4. A write select error has occurred in the disk storage drive. 
 *		5. The power unsafe latch is set in the attachment.
 *		Conditions 1, 2, and 3 are turned off by a sense device command with modifier bit 15
 *		set to 1. Conditions 4 and 5 are turned off by powering the drive off and back on.
 * 1 operation complete
 * 2 not ready, occurs when disk not ready or busy or disabled or off-line or
 *		power unsafe latch set. Also included in the disk not ready is the write select error,
 *		which can be a result of power unsafe or write select. 
 * 3 disk busy
 * 4 carriage home (on cyl 0)
 * 15-16: number of next sector spinning into position.
 */

void xio_disk (int32 iocc_addr, int32 func, int32 modify, int drv)
{
	int i, rev, nsteps, newcyl, sec, nwords;
	t_addr newpos;
	char msg[80];
	UNIT *uptr = dsk_unit+drv;
	int16 buf[DSK_NUMWD];

	if (! BETWEEN(drv, 0, DSK_NUMDR-1)) {	// hmmm, invalid drive */
		if (func != XIO_SENSE_DEV) {		// tried to use it, too
			sprintf(msg, "Op %x on invalid drive number %d", func, drv);
			xio_error(msg);
		}
		return;
	}

	CLRBIT(uptr->flags, UNIT_OPERR);				/* clear pending error flag from previous op, if any */

	switch (func) {
		case XIO_INITR:
			if (! IS_ONLINE(uptr)) {				/* disk is offline */
				diskfail(uptr, UNIT_HARDERR);		/* make error stick till reset or attach */
				break;
			}

			sim_cancel(uptr);						/* cancel any pending ops */
			dsk_dsw[drv] |= DSK_DSW_DISK_BUSY;		/* and mark the disk as busy */

			nwords = M[iocc_addr++ & mem_mask];		/* get word count w/o upsetting SAR/SBR */

			if (nwords == 0)						/* this is bad -- locks up disk controller ! */
				break;

			nwords &= 1023;							/* sanity check */
			if (! BETWEEN(nwords, 1, DSK_NUMWD)) {	/* count bad */
				SETBIT(uptr->flags, UNIT_OPERR);	/* set data error DSW bit when op complete */
				nwords = DSK_NUMWD;					/* limit xfer to proper sector size */
			}

			sec = modify & 0x07;					/* get sector on cylinder */

			if ((modify & 0x0080) == 0) {			/* it's real if not a read check */
				newpos = (uptr->CYL*DSK_NUMSC*DSK_NUMSF + sec)*2*DSK_NUMWD;
				if (uptr->pos != newpos)
					fseek(uptr->fileref, newpos, SEEK_SET);
				
				fread(buf, 2, DSK_NUMWD, uptr->fileref);	// read whole sector so we're in position for next read
				uptr->pos = newpos + 2*DSK_NUMWD;

				trace_io("* DSK%d read %d words from %d.%d (%x) to M[%04x-%04x]", uptr-dsk_unit, nwords, uptr->CYL, sec, newpos, iocc_addr & mem_mask,
					(iocc_addr + nwords - 1) & mem_mask);
																																	  
				for (i = 0; i < nwords; i++)
					M[iocc_addr++ & mem_mask] = buf[i];
			}
			else
				trace_io("* DSK%d verify %d.%d", uptr-dsk_unit, uptr->CYL, sec);

			uptr->FUNC = func;
			sim_activate(uptr, dsk_rwait);

			break;

		case XIO_INITW:
			if (! IS_ONLINE(uptr)) {				/* disk is offline */
				diskfail(uptr, UNIT_HARDERR);		/* make error stick till reset or attach */
				break;
			}

			if (uptr->flags & UNIT_RONLY) {			/* oops, write to RO disk? permanent error */
				diskfail(uptr, UNIT_HARDERR);
				break;
			}

			sim_cancel(uptr);						/* cancel any pending ops */
			dsk_dsw[drv] |= DSK_DSW_DISK_BUSY;		/* and mark drive as busy */

			nwords = M[iocc_addr++ & mem_mask];		/* get word count w/o upsetting SAR/SBR */

			if (nwords == 0)						/* this is bad -- locks up disk controller ! */
				break;

			nwords &= 1023;							/* sanity check */
			if (! BETWEEN(nwords, 1, DSK_NUMWD)) {	/* count bad */
				SETBIT(uptr->flags, UNIT_OPERR);	/* set data error DSW bit when op complete */
				nwords = DSK_NUMWD;					/* limit xfer to proper sector size */
			}

			sec    = modify & 0x07;					/* get sector on cylinder */
			newpos = (uptr->CYL*DSK_NUMSC*DSK_NUMSF + sec)*2*DSK_NUMWD;
			if (uptr->pos != newpos)
				fseek(uptr->fileref, newpos, SEEK_SET);
			
			trace_io("* DSK%d wrote %d words from M[%04x-%04x] to %d.%d (%x)", uptr-dsk_unit, nwords, iocc_addr & mem_mask, (iocc_addr + nwords - 1) & mem_mask, uptr->CYL, sec, newpos);

			for (i = 0; i < nwords; i++)
				buf[i] = M[iocc_addr++ & mem_mask];

			for (; i < DSK_NUMWD; i++)				/* rest of sector gets zeroed */
				buf[i] = 0;

			fwrite(buf, 2, DSK_NUMWD, uptr->fileref);
			uptr->pos = newpos + 2*DSK_NUMWD;

			uptr->FUNC = func;
			sim_activate(uptr, dsk_rwait);
			break;

		case XIO_CONTROL:								/* step fwd/rev */
			if (! IS_ONLINE(uptr)) {
				diskfail(uptr, UNIT_HARDERR);
				break;
			}

			sim_cancel(uptr);

			rev    = modify & 4;
			nsteps = iocc_addr & 0x00FF;
			if (nsteps == 0)							/* 0 steps does not cause op complete interrupt */
				break;

			newcyl = uptr->CYL + (rev ? (-nsteps) : nsteps);
			if (newcyl < 0)
				newcyl = 0;
			else if (newcyl >= DSK_NUMCY)
				newcyl = DSK_NUMCY-1;

			uptr->FUNC = func;
			uptr->CYL  = newcyl;
			sim_activate(uptr, dsk_swait);			/* schedule interrupt */

			dsk_dsw[drv] |= DSK_DSW_DISK_BUSY;
			trace_io("* DSK%d at cyl %d", uptr-dsk_unit, newcyl);
			break;

		case XIO_SENSE_DEV:
			CLRBIT(dsk_dsw[drv], DSK_DSW_CARRIAGE_HOME|DSK_DSW_NOT_READY);

			if ((uptr->flags & UNIT_HARDERR) || (dsk_dsw[drv] & DSK_DSW_DISK_BUSY) || ! IS_ONLINE(uptr))
				SETBIT(dsk_dsw[drv], DSK_DSW_NOT_READY);
			else if (uptr->CYL <= 0) {
				SETBIT(dsk_dsw[drv], DSK_DSW_CARRIAGE_HOME);
				uptr->CYL = 0;
			}

			dsk_sec[drv] = (dsk_sec[drv] + 1) % 4;				/* advance the "next sector" count every time */
			ACC = dsk_dsw[drv] | dsk_sec[drv];

			if (modify & 0x01) {						/* reset interrupts */
				CLRBIT(dsk_dsw[drv], DSK_DSW_OP_COMPLETE|DSK_DSW_DATA_ERROR);
				CLRBIT(ILSW[dsk_ilswlevel[drv]], dsk_ilswbit[drv]);
			}
			break;

		default:
			sprintf(msg, "Invalid disk XIO function %x", func);
			xio_error(msg);
	}
}

/* diskfail - schedule an operation complete that sets the error bit */

static void diskfail (UNIT *uptr, int errflag)
{
	sim_cancel(uptr);					/* cancel any pending ops */
	SETBIT(uptr->flags, errflag);		/* set the error flag */
	uptr->FUNC = XIO_FAILED;			/* tell svc routine why it failed */
	sim_activate(uptr, 1);				/* schedule an immediate op complete interrupt */
}

t_stat dsk_svc (UNIT *uptr)
{
	int drv = uptr - dsk_unit;

	CLRBIT(dsk_dsw[drv], DSK_DSW_DISK_BUSY);				/* activate operation complete interrupt */
	SETBIT(dsk_dsw[drv], DSK_DSW_OP_COMPLETE);

	if (uptr->flags & (UNIT_OPERR|UNIT_HARDERR)) {		/* word count error or data error */
		SETBIT(dsk_dsw[drv], DSK_DSW_DATA_ERROR);
		CLRBIT(uptr->flags, UNIT_OPERR);					/* but don't clear hard error */
	}

	SETBIT(ILSW[dsk_ilswlevel[drv]], dsk_ilswbit[drv]);

#ifdef XXXX
	switch (uptr->FUNC) {
		case XIO_CONTROL:
		case XIO_INITR:
		case XIO_INITW:
		case XIO_FAILED:
			break;
		
		default:
			fprintf(stderr, "Unexpected FUNC %x in dsk_svc(%d)\n", uptr->FUNC, drv);
			break;
			
	}
	uptr->FUNC = -1;			// we're done with this operation
#endif

	return SCPE_OK;
}

t_stat dsk_reset (DEVICE *dptr)
{
	int drv;
	UNIT *uptr;

	for (drv = 0, uptr = dsk_dev.units; drv < DSK_NUMDR; drv++, uptr++) {
		sim_cancel(uptr);

		CLRBIT(ILSW[2], dsk_ilswbit[drv]);
		CLRBIT(uptr->flags, UNIT_OPERR|UNIT_HARDERR);

		uptr->CYL    = 0;
		uptr->FUNC   = -1;
		dsk_dsw[drv] = (uptr->flags & UNIT_ATT) ? DSK_DSW_CARRIAGE_HOME : 0;
	}

	calc_ints();

	return SCPE_OK;
}

static t_stat dsk_attach (UNIT *uptr, char *cptr)
{
	int drv = uptr - dsk_unit;
	t_stat rval;

	sim_cancel(uptr);

	if ((rval = attach_unit(uptr, cptr)) != SCPE_OK)
		return rval;

	CLRBIT(ILSW[2], dsk_ilswbit[drv]);
	CLRBIT(uptr->flags, UNIT_OPERR|UNIT_HARDERR);
	calc_ints();

	uptr->CYL    = 0;
	uptr->FUNC   = -1;
	dsk_dsw[drv] = DSK_DSW_CARRIAGE_HOME;

	if (drv == 0) {
		disk_ready(TRUE);
		disk_unlocked(FALSE);
	}

	return SCPE_OK;
}

static t_stat dsk_detach (UNIT *uptr)
{
	t_stat rval;
	int drv = uptr - dsk_unit;

	sim_cancel(uptr);

	if ((rval = detach_unit (uptr)) != SCPE_OK)
		return rval;

	CLRBIT(ILSW[2], dsk_ilswbit[drv]);
	CLRBIT(uptr->flags, UNIT_OPERR|UNIT_HARDERR);
	calc_ints();

	uptr->CYL    = 0;
	uptr->FUNC   =  -1;
	dsk_dsw[drv] = 0;

	if (drv == 0) {
		disk_unlocked(TRUE);
		disk_ready(FALSE);
	}

	return SCPE_OK;
}

// boot routine - if they type BOOT DSK, load the standard boot card.

static t_stat dsk_boot (int unitno)
{
	t_stat rval;

	if ((rval = reset_all(0)) != SCPE_OK)
		return rval;

	return load_cr_boot(unitno);
}
