/* id4_stddev.c: Interdata 4 standard devices

   Copyright (c) 1993-2000, Robert M. Supnik

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

   pt			paper tape reader and punch
   tt			console
   cdr			card reader
*/

#include "id4_defs.h"
#include <ctype.h>
#define UNIT_V_UC	(UNIT_V_UF + 0)			/* UC only */
#define UNIT_UC		(1 << UNIT_V_UC)

extern unsigned int16 M[];
extern int32 int_req[INTSZ], int_enb[INTSZ];
int32 pt_run = 0, pt_slew = 0;				/* ptr modes */
int pt_rw = 0, pt_busy = 0;				/* pt state */
int32 ptr_stopioe = 0, ptp_stopioe = 0;			/* stop on error */
int32 tt_hdpx = 0;					/* tt mode */
int32 tt_rw = 0, tt_busy = 0;				/* tt state */
t_stat ptr_svc (UNIT *uptr);
t_stat ptp_svc (UNIT *uptr);
t_stat pt_boot (int32 unitno);
t_stat pt_reset (DEVICE *dptr);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tt_reset (DEVICE *dptr);
extern t_stat sim_poll_kbd (void);
extern t_stat sim_putchar (int32 out);

/* Device definitions */

#define PTR		0
#define PTP		1
#define PT_STA_OVFL	0x80				/* overflow */
#define PT_STA_NMTN	0x10				/* no motion */

#define PT_V_RUN	4				/* run/stop */
#define PT_M_RUN	0x3
#define PT_RUN		 1
#define PT_STOP		 2
#define PT_CRS		 3
#define PT_GETRUN(x)	(((x) >> PT_V_RUN) & PT_M_RUN)

#define PT_V_SLEW	2				/* slew/step */
#define PT_M_SLEW	0x3
#define PT_SLEW		 1
#define PT_STEP		 2
#define PT_CSLEW	 3
#define PT_GETSLEW(x)	(((x) >> PT_V_SLEW) & PT_M_SLEW)

#define PT_V_RW		0				/* read/write */
#define PT_M_RW		0x3
#define PT_RD		 1
#define PT_WD		 2
#define PT_CRW		 3
#define PT_GETRW(x)	(((x) >> PT_V_RW) & PT_M_RW)

#define TTI		0
#define TTO		1
#define TT_V_DPX	4				/* half/full duplex */
#define TT_M_DPX	0x3
#define TT_FDPX		 1
#define TT_HDPX		 2
#define TT_CDPX		 3
#define TT_GETDPX(x)	(((x) >> TT_V_DPX) & TT_M_DPX)

#define TT_V_RW		2				/* read/write */
#define TT_M_RW		0x3
#define TT_RD		 1
#define TT_WD		 2
#define TT_CRW		 3
#define TT_GETRW(x)	(((x) >> TT_V_RW) & TT_M_RW)

/* PT data structures

   pt_dev	PT device descriptor
   pt_unit	PT unit descriptors
   pt_reg	PT register list
*/

UNIT pt_unit[] = {
	{ UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_IN_WAIT },
	{ UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT }
};

REG pt_reg[] = {
	{ HRDATA (RBUF, pt_unit[PTR].buf, 8) },
	{ DRDATA (RPOS, pt_unit[PTR].pos, 31), PV_LEFT },
	{ DRDATA (RTIME, pt_unit[PTR].wait, 24), PV_LEFT },
	{ FLDATA (RSTOP_IOE, ptr_stopioe, 0) },
	{ HRDATA (PBUF, pt_unit[PTP].buf, 8) },
	{ DRDATA (PPOS, pt_unit[PTP].pos, 31), PV_LEFT },
	{ DRDATA (PTIME, pt_unit[PTP].wait, 24), PV_LEFT },
	{ FLDATA (PSTOP_IOE, ptp_stopioe, 0) },
	{ FLDATA (IREQ, int_req[PT/32], PT & 0x1F) },
	{ FLDATA (IENB, int_enb[PT/32], PT & 0x1F) },
	{ FLDATA (RUN, pt_run, 0) },
	{ FLDATA (SLEW, pt_slew, 0) },
	{ FLDATA (BUSY, pt_busy, 0) },
	{ FLDATA (RW, pt_rw, 0) },
	{ NULL }  };

DEVICE pt_dev = {
	"PT", pt_unit, pt_reg, NULL,
	2, 10, 31, 1, 8, 8,
	NULL, NULL, &pt_reset,
	&pt_boot, NULL, NULL };

/* TT data structures

   tt_dev	TT device descriptor
   tt_unit	TT unit descriptors
   tt_reg	TT register list
   tt_mod	TT modifiers list
*/

UNIT tt_unit[] = {
	{ UDATA (&tti_svc, UNIT_UC, 0), KBD_POLL_WAIT },
	{ UDATA (&tto_svc, UNIT_UC, 0), SERIAL_OUT_WAIT }
};

REG tt_reg[] = {
	{ HRDATA (KBUF, tt_unit[TTI].buf, 8) },
	{ DRDATA (KPOS, tt_unit[TTI].pos, 31), PV_LEFT },
	{ DRDATA (KTIME, tt_unit[TTI].wait, 24), REG_NZ + PV_LEFT },
	{ HRDATA (TBUF, tt_unit[TTO].buf, 8) },
	{ DRDATA (TPOS, tt_unit[TTO].pos, 31), PV_LEFT },
	{ DRDATA (TTIME, tt_unit[TTO].wait, 24), REG_NZ + PV_LEFT },
	{ FLDATA (IREQ, int_req[TT/32], TT & 0x1F) },
	{ FLDATA (IENB, int_enb[TT/32], TT & 0x1F) },
	{ FLDATA (HDPX, tt_hdpx, 0) },
	{ FLDATA (BUSY, tt_busy, 0) },
	{ FLDATA (RW, tt_rw, 0) },
	{ FLDATA (UC, tt_unit[TTI].flags, UNIT_V_UC), REG_HRO },
	{ NULL }  };

MTAB tt_mod[] = {
	{ UNIT_UC, 0, "lower case", "LC", NULL },
	{ UNIT_UC, UNIT_UC, "upper case", "UC", NULL },
	{ 0 }  };

DEVICE tt_dev = {
	"TT", tt_unit, tt_reg, tt_mod,
	2, 10, 31, 1, 8, 8,
	NULL, NULL, tt_reset,
	NULL, NULL, NULL };

/* Paper tape: IO routine */

int32 pt (int32 op, int32 dat)
{
int32 t;

switch (op) {						/* case IO op */
case IO_ADR:						/* select */
	break;
case IO_OC:						/* command */
	t = CMD_GETINT (dat);				/* get enable */
	if (t == CMD_IENB) SET_ENB (PT);		/* process */
	else if (t == CMD_IDIS) CLR_ENB (PT);
	else if (t == CMD_ICOM) COM_ENB (PT);
	t = PT_GETRUN (dat);				/* run/stop */
	if (t == PT_RUN) pt_run = 1;			/* process */
	else if (t == PT_STOP) pt_run = 0;
	else if (t == PT_CRS) pt_run = pt_run ^ 1;
	t = PT_GETSLEW (dat);				/* slew/step */
	if (t == PT_SLEW) pt_slew = 1;			/* process */
	else if (t == PT_STEP) pt_slew = 0;
	else if (t == PT_CSLEW) pt_slew = pt_slew ^ 1;
	t = PT_GETRW (dat);				/* read/write */
	if (t == PT_RD) pt_rw = 0;			/* process */
	else if (t == PT_WD) pt_rw = 1;
	else if (t == PT_CRW) pt_rw = pt_rw ^ 1;
	if ((pt_rw == 0) && pt_run)			/* read + run? */
		sim_activate (&pt_unit[PTR], pt_unit[PTR].wait);
	if (sim_is_active (&pt_unit[pt_rw? PTP: PTR])) pt_busy = 1;
	else {	if (pt_busy) SET_INT (PT);
		pt_busy = 0;  }
	break;
case IO_RD:						/* read */
	if (pt_run && !pt_slew)
		sim_activate (&pt_unit[PTR], pt_unit[PTR].wait);
	if (pt_rw == 0) pt_busy = 1;
	return (pt_unit[PTR].buf & 0xFF);
case IO_WD:						/* write */
	pt_unit[PTP].buf = dat & 0xFF;
	sim_activate (&pt_unit[PTP], pt_unit[PTP].wait);
	if (pt_rw) pt_busy = 1;
	break;
case IO_SS:						/* status */
	t = pt_busy? STA_BSY: 0;
	if ((pt_unit[PTR].flags & UNIT_ATT) == 0) t = t | STA_DU;
	if (!sim_is_active (&pt_unit[PTR])) t = t | PT_STA_NMTN | STA_EX;
	return t;  }
return 0;
}

/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32 temp;

if ((pt_unit[PTR].flags & UNIT_ATT) == 0)		/* attached? */
	return IORETURN (ptr_stopioe, SCPE_UNATT);
if (pt_rw == 0) {					/* read mode? */
	if (pt_busy) SET_INT (PT);			/* if busy, intr */
	pt_busy = 0;  }					/* not busy */
if (pt_slew) sim_activate (&pt_unit[PTR], pt_unit[PTR].wait);	/* slew? */
if ((temp = getc (pt_unit[PTR].fileref)) == EOF) {
	if (feof (pt_unit[PTR].fileref)) {
		if (ptr_stopioe) printf ("PTR end of file\n");
		else return SCPE_OK;  }
	else perror ("PTR I/O error");
	clearerr (pt_unit[PTR].fileref);
	return SCPE_IOERR;  }
pt_unit[PTR].buf = temp & 0xFF;
pt_unit[PTR].pos = pt_unit[PTR].pos + 1;
return SCPE_OK;
}

t_stat ptp_svc (UNIT *uptr)
{
if ((pt_unit[PTP].flags & UNIT_ATT) == 0)		/* attached? */
	return IORETURN (ptp_stopioe, SCPE_UNATT);
if (pt_rw) {						/* write mode? */
	if (pt_busy) SET_INT (PT);			/* if busy, intr */
	pt_busy = 0;  }					/* not busy */
if (putc (pt_unit[PTP].buf, pt_unit[PTP].fileref) == EOF) {
	perror ("PTP I/O error");
	clearerr (pt_unit[PTP].fileref);
	return SCPE_IOERR;  }
pt_unit[PTP].pos = pt_unit[PTP].pos + 1;
return SCPE_OK;
}

/* Bootstrap routine */

#define BOOT_START 0x3E
#define BOOT_LEN (sizeof (boot_rom) / sizeof (unsigned int16))

static const unsigned int16 boot_rom[] = {
	0xC820,		/* START:	LHI 2,80 */
	0x0080,
	0xC830,		/*	LHI 3,1 */
	0x0001,
	0xC840,		/*	LHI 4,CF */
	0x00CF,
	0xD3A0,		/*	LB A,78 */
	0x0078,
	0xDEA0,		/*	OC A,79 */
	0x0079,
	0x9DAE,		/* LOOP: SSR A,E */
	0x42F0,		/* 	BTC F,LOOP */
	0x0052,
	0x9BAE,		/*	RDR A,E */
	0x08EE,		/*	LHR E,E */
	0x4330,		/*	BZ LOOP */
	0x0052,
	0x4300,		/*	BR STORE */
	0x006C,
	0x9DAE,		/* LOOP1: SSR A,E */
	0x42F0,		/*	BTC F,LOOP1 */
	0x0064,
	0x9BAE,		/*	RDR A,E */
	0xD2E2,		/* STORE: STB E,0(2) */
	0x0000,
	0xC120,		/*	BXLE 2,LOOP1 */
	0x0064,
	0x4300,		/*	BR 80 */
	0x0080,
	0x0395,		/* HS PAPER TAPE INPUT */
	0x039A,		/* HS PAPER TAPE OUTPUT */
	0x0420,		/* CARD INPUT TO ASSEMBLER */
	0x0298		/* TELEPRINTER OUTPUT FOR ASSEMBLER */
};

t_stat pt_boot (int32 unitno)
{
int32 i;
extern int32 saved_PC;

for (i = 0; i < BOOT_LEN; i++) M[(BOOT_START >> 1) + i] = boot_rom[i];
saved_PC = BOOT_START;
return SCPE_OK;
}

/* Reset routine */

t_stat pt_reset (DEVICE *dptr)
{
sim_cancel (&pt_unit[PTR]);				/* deactivate units */
sim_cancel (&pt_unit[PTP]);
pt_busy = pt_run = pt_slew = pt_rw = 0;
return SCPE_OK;
}

/* Terminal: IO routine */

int32 tt (int32 op, int32 dat)
{
int32 t, old_tt_rw;

switch (op) {						/* case IO op */
case IO_ADR:						/* select */
	break;
case IO_OC:						/* command */
	t = CMD_GETINT (dat);				/* get enable */
	if (t == CMD_IENB) SET_ENB (TT);		/* process */
	else if (t == CMD_IDIS) CLR_ENB (TT);
	else if (t == CMD_ICOM) COM_ENB (TT);
	t = TT_GETDPX (dat);				/* get duplex */
	if (t == TT_FDPX) tt_hdpx = 0;			/* process */
	else if (t == TT_HDPX) tt_hdpx = 1;
	else if (t == TT_CDPX) tt_hdpx = tt_hdpx ^ 1;
	t = TT_GETRW (dat);				/* read/write */
	old_tt_rw = tt_rw;
	if (t == TT_RD) tt_rw = 0;			/* process */
	else if (t == TT_WD) tt_rw = 1;
	else if (t == TT_CRW) tt_rw = tt_rw ^ 1;
	if (tt_rw == 0) {				/* read? */
		if (old_tt_rw != 0) tt_busy = 1;  }	/* chg? set busy */
	else {	if (sim_is_active (&tt_unit[TTO])) tt_busy = 1;
		else {	if (tt_busy) SET_INT (TT);
			tt_busy = 0;  }  }
	break;
case IO_RD:						/* read */
	if (tt_rw == 0) tt_busy = 1;
	return (tt_unit[TTI].buf & 0xFF);
case IO_WD:						/* write */
	tt_unit[TTO].buf = dat & 0xFF;
	sim_activate (&tt_unit[TTO], tt_unit[TTO].wait);
	if (pt_rw) tt_busy = 1;
	break;
case IO_SS:						/* status */
	t = tt_busy? STA_BSY: 0;
	return t;  }
return 0;
}

/* Unit service routines */

t_stat tti_svc (UNIT *uptr)
{
int32 temp;

sim_activate (&tt_unit[TTI], tt_unit[TTI].wait);	/* continue poll */
if ((temp = sim_poll_kbd ()) < SCPE_KFLAG) return temp;	/* no char or error? */
if (tt_rw == 0) {					/* read mode? */
	if (tt_busy) SET_INT (TT);			/* if  busy, intr */
	tt_busy = 0;  }					/* not busy */
temp = temp & 0x7F;
if ((tt_unit[TTI].flags & UNIT_UC) && islower (temp))
	temp = toupper (temp);
tt_unit[TTI].buf = temp | 0x80;				/* got char */
tt_unit[TTI].pos = tt_unit[TTI].pos + 1;
if (tt_hdpx) {						/* half duplex? */
	sim_putchar (temp);				/* write char */
	tt_unit[TTO].pos = tt_unit[TTO].pos + 1;  }
return SCPE_OK;
}

t_stat tto_svc (UNIT *uptr)
{
int32 temp;

if (tt_rw) {						/* write mode? */
	if (tt_busy) SET_INT (TT);			/* if was busy, intr */
	tt_busy = 0;  }					/* not busy */
if ((temp = sim_putchar (tt_unit[TTO].buf & 0177)) != SCPE_OK) return temp;
tt_unit[TTO].pos = tt_unit[TTO].pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat tt_reset (DEVICE *dptr)
{
sim_activate (&tt_unit[TTI], tt_unit[TTI].wait);	/* activate input */
sim_cancel (&tt_unit[TTO]);				/* cancel output */
tt_hdpx = tt_rw = 0;
tt_busy = 1;						/* no kbd input yet */
return SCPE_OK;
}
