/* id_tt.c: Interdata teletype

   Copyright (c) 2000-2003, Robert M. Supnik

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

   tt		console

   11-Jan-03	RMS	Added TTP support
   22-Dec-02	RMS	Added break support
*/

#include "id_defs.h"
#include <ctype.h>

#define UNIT_V_8B	(UNIT_V_UF + 0)			/* 8B */
#define UNIT_V_KSR	(UNIT_V_UF + 1)			/* KSR33 */
#define UNIT_8B		(1 << UNIT_V_8B)
#define UNIT_KSR	(1 << UNIT_V_KSR)

/* Device definitions */

#define TTI		0
#define TTO		1

#define STA_OVR		0x80 				/* overrun */
#define STA_BRK		0x20				/* break */
#define STA_MASK	(STA_OVR | STA_BRK | STA_BSY)	/* status mask */
#define SET_EX		(STA_OVR | STA_BRK)		/* set EX */

#define CMD_V_FDPX	4				/* full/half duplex */
#define CMD_V_RD	2				/* read/write */

extern uint32 int_req[INTSZ], int_enb[INTSZ];

uint32 tt_sta = STA_BSY;				/* status */
uint32 tt_fdpx = 1;					/* tt mode */
uint32 tt_rd = 1, tt_chp = 0;				/* tt state */
uint32 tt_arm = 0;					/* int arm */

uint32 tt (uint32 dev, uint32 op, uint32 dat);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tt_reset (DEVICE *dptr);
t_stat tt_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat tt_set_break (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat tt_set_enbdis (UNIT *uptr, int32 val, char *cptr, void *desc);

/* TT data structures

   tt_dev	TT device descriptor
   tt_unit	TT unit descriptors
   tt_reg	TT register list
   tt_mod	TT modifiers list
*/

DIB tt_dib = { d_TT, -1, v_TT, NULL, &tt, NULL };

UNIT tt_unit[] = {
	{ UDATA (&tti_svc, UNIT_KSR, 0), KBD_POLL_WAIT },
	{ UDATA (&tto_svc, UNIT_KSR, 0), SERIAL_OUT_WAIT }
};

REG tt_reg[] = {
	{ HRDATA (STA, tt_sta, 8) },
	{ HRDATA (KBUF, tt_unit[TTI].buf, 8) },
	{ DRDATA (KPOS, tt_unit[TTI].pos, 32), PV_LEFT },
	{ DRDATA (KTIME, tt_unit[TTI].wait, 24), REG_NZ + PV_LEFT },
	{ HRDATA (TBUF, tt_unit[TTO].buf, 8) },
	{ DRDATA (TPOS, tt_unit[TTO].pos, 32), PV_LEFT },
	{ DRDATA (TTIME, tt_unit[TTO].wait, 24), REG_NZ + PV_LEFT },
	{ FLDATA (IREQ, int_req[l_TT], i_TT) },
	{ FLDATA (IENB, int_enb[l_TT], i_TT) },
	{ FLDATA (IARM, tt_arm, 0) },
	{ FLDATA (RD, tt_rd, 0) },
	{ FLDATA (FDPX, tt_fdpx, 0) },
	{ FLDATA (CHP, tt_chp, 0) },
	{ HRDATA (DEVNO, tt_dib.dno, 8), REG_HRO },
	{ NULL }  };

MTAB tt_mod[] = {
	{ UNIT_KSR+UNIT_8B, UNIT_KSR, "KSR", "KSR", &tt_set_mode },
	{ UNIT_KSR+UNIT_8B, 0       , "7b" , "7B" , &tt_set_mode },
	{ UNIT_KSR+UNIT_8B, UNIT_8B , "8b" , "8B" , &tt_set_mode },
	{ MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, NULL, "ENABLED",
		&tt_set_enbdis, NULL, NULL },
	{ MTAB_XTD|MTAB_VDV|MTAB_NMO, DEV_DIS, NULL, "DISABLED",
		&tt_set_enbdis, NULL, NULL },
	{ MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, NULL, "BREAK",
		&tt_set_break, NULL, NULL },
	{ MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
		&set_dev, &show_dev, &tt_dib },
	{ 0 }  };

DEVICE tt_dev = {
	"TT", tt_unit, tt_reg, tt_mod,
	2, 10, 31, 1, 16, 8,
	NULL, NULL, &tt_reset,
	NULL, NULL, NULL,
	&tt_dib, 0 };

/* Terminal: IO routine */

uint32 tt (uint32 dev, uint32 op, uint32 dat)
{
uint32 old_rd, t;

switch (op) {						/* case IO op */
case IO_ADR:						/* select */
	return BY;					/* byte only */
case IO_OC:						/* command */
	old_rd = tt_rd;
	tt_arm = int_chg (v_TT, dat, tt_arm);		/* upd int ctrl */
	tt_fdpx = io_2b (dat, CMD_V_FDPX, tt_fdpx);	/* upd full/half */
	tt_rd = io_2b (dat, CMD_V_RD, tt_rd);		/* upd rd/write */
	if (tt_rd != old_rd) {				/* rw change? */
	    if (tt_rd? tt_chp: !sim_is_active (&tt_unit[TTO])) {
		tt_sta = 0;				/* busy = 0 */
		if (tt_arm) SET_INT (v_TT);  }		/* req intr */
	    else {
		tt_sta = STA_BSY;			/* busy = 1 */
		CLR_INT (v_TT);  }  }			/* clr int */
	else tt_sta = tt_sta & ~STA_OVR;		/* clr ovflo */
	break;
case IO_RD:						/* read */
	tt_chp = 0;					/* clear pend */
	if (tt_rd) tt_sta = (tt_sta | STA_BSY) & ~STA_OVR;
	return (tt_unit[TTI].buf & 0xFF);
case IO_WD:						/* write */
	tt_unit[TTO].buf = dat & 0xFF;			/* save char */
	if (!tt_rd) tt_sta = tt_sta | STA_BSY;		/* set busy */
	sim_activate (&tt_unit[TTO], tt_unit[TTO].wait);
	break;
case IO_SS:						/* status */
	t = tt_sta & STA_MASK;				/* get status */
	if (t & SET_EX) t = t | STA_EX;			/* test for EX */
	return t;  }
return 0;
}

/* Unit service routines */

t_stat tti_svc (UNIT *uptr)
{
int32 out, temp;

sim_activate (uptr, uptr->wait);			/* continue poll */
tt_sta = tt_sta & ~STA_BRK;				/* clear break */
if ((temp = sim_poll_kbd ()) < SCPE_KFLAG) return temp;	/* no char or error? */
if (tt_rd) {						/* read mode? */
	tt_sta = tt_sta & ~STA_BSY;			/* clear busy */
	if (tt_arm) SET_INT (v_TT);			/* if armed, intr */
	if (tt_chp) tt_sta = tt_sta | STA_OVR;  }	/* got char? overrun */
tt_chp = 1;						/* char pending */
out = temp & 0x7F;					/* echo is 7B */
if (temp & SCPE_BREAK) {				/* break? */
	tt_sta = tt_sta | STA_BRK;			/* set status */
	uptr->buf = 0;  }				/* no character */
else if (uptr->flags & UNIT_KSR) {			/* KSR mode? */
	if (islower (out)) out = toupper (out);		/* cvt to UC */ 
	uptr->buf = out | 0x80;  }			/* set high bit */
else uptr->buf = temp & ((tt_unit[TTI].flags & UNIT_8B)? 0xFF: 0x7F);
uptr->pos = uptr->pos + 1;				/* incr count */
if (!tt_fdpx) {						/* half duplex? */
	if (out) sim_putchar (out);			/* write char */
	tt_unit[TTO].pos = tt_unit[TTO].pos + 1;  }
return SCPE_OK;
}

t_stat tto_svc (UNIT *uptr)
{
int32 ch;
t_stat r;

if (!tt_rd) {						/* write mode? */
	tt_sta = tt_sta & ~STA_BSY;			/* clear busy */
	if (tt_arm) SET_INT (v_TT);  }			/* if armed, intr */
if (uptr->flags & UNIT_KSR) {				/* KSR mode? */
	ch = uptr->buf & 0x7F;				/* mask to 7b */
	if (islower (ch)) ch = toupper (ch);  }		/* cvt to UC */
else ch = uptr->buf & ((tt_unit[TTO].flags & UNIT_8B)? 0xFF: 0x7F);
if (!(uptr->flags & UNIT_8B) &&				/* KSR or 7b? */
	((ch == 0) || (ch == 0x7F))) return SCPE_OK;	/* supr NULL, DEL */
if ((r = sim_putchar (ch)) != SCPE_OK) return r;	/* output */
uptr->pos = uptr->pos + 1;				/* incr count */
return SCPE_OK;
}

/* Reset routine */

t_stat tt_reset (DEVICE *dptr)
{
if (dptr->flags & DEV_DIS) sim_cancel (&tt_unit[TTI]);	/* dis? cancel poll */
else sim_activate (&tt_unit[TTI], tt_unit[TTI].wait);	/* activate input */
sim_cancel (&tt_unit[TTO]);				/* cancel output */
tt_rd = tt_fdpx = 1;					/* read, full duplex */
tt_chp = 0;						/* no char */
tt_sta = STA_BSY;					/* buffer empty */
CLR_INT (v_TT);						/* clear int */
CLR_ENB (v_TT);						/* disable int */
tt_arm = 0;						/* disarm int */
return SCPE_OK;
}

/* Make mode flags uniform */

t_stat tt_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc)
{
tt_unit[TTI].flags = (tt_unit[TTI].flags & ~(UNIT_KSR | UNIT_8B)) | val;
tt_unit[TTO].flags = (tt_unit[TTO].flags & ~(UNIT_KSR | UNIT_8B)) | val;
return SCPE_OK;
}

/* Set input break */

t_stat tt_set_break (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (tt_dev.flags & DEV_DIS) return SCPE_NOFNC;
tt_sta = tt_sta | STA_BRK;
if (tt_rd) {						/* read mode? */
	tt_sta = tt_sta & ~STA_BSY;			/* clear busy */
	if (tt_arm) SET_INT (v_TT);  }			/* if armed, intr */
sim_cancel (&tt_unit[TTI]);				/* restart TT poll */
sim_activate (&tt_unit[TTI], tt_unit[TTI].wait);	/* so brk is seen */
return SCPE_OK;
}

/* Set enabled/disabled */

t_stat tt_set_enbdis (UNIT *uptr, int32 val, char *cptr, void *desc)
{
extern DEVICE ttp_dev;
extern t_stat ttp_reset (DEVICE *dptr);

tt_dev.flags = (tt_dev.flags & ~DEV_DIS) | val;
ttp_dev.flags = (ttp_dev.flags & ~DEV_DIS) | (val ^ DEV_DIS);
tt_reset (&tt_dev);
ttp_reset (&ttp_dev);
return SCPE_OK;
}
