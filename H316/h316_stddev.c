/* h316_stddev.c: Honeywell 316/516 standard devices

   Copyright (c) 1999-2002, Robert M. Supnik

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

   ptr		316/516-50 paper tape reader
   ptp		316/516-52 paper tape punch
   tty		316/516-33 teleprinter
   clk/options	316/516-12 real time clocks/internal options

   22-Dec-02	RMS	Added break support
   01-Nov-02	RMS	Added 7b/8b support to terminal
   30-May-02	RMS	Widened POS to 32b
   03-Nov-01	RMS	Implemented upper case for console output
   29-Nov-01	RMS	Added read only unit support
   07-Sep-01	RMS	Moved function prototypes
*/

#include "h316_defs.h"
#include <ctype.h>

#define UNIT_V_8B	(UNIT_V_UF + 0)			/* 8B */
#define UNIT_V_KSR	(UNIT_V_UF + 1)			/* KSR33 */
#define UNIT_8B		(1 << UNIT_V_8B)
#define UNIT_KSR	(1 << UNIT_V_KSR)

extern uint16 M[];
extern int32 PC;
extern int32 stop_inst;
extern int32 C, dp, ext, extoff_pending, sc;
extern int32 dev_ready, dev_enable;
extern UNIT cpu_unit;

int32 ptr_stopioe = 0, ptp_stopioe = 0;			/* stop on error */
int32 ptp_power = 0, ptp_ptime;				/* punch power, time */
int32 tty_mode = 0, tty_buf = 0;			/* tty mode, buf */
int32 clk_tps = 60;				/* ticks per second */

t_stat ptr_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptr_boot (int32 unitno, DEVICE *dptr);
t_stat ptp_svc (UNIT *uptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tty_reset (DEVICE *dptr);
t_stat tty_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat clk_svc (UNIT *uptr);
t_stat clk_reset (DEVICE *dptr);

/* PTR data structures

   ptr_dev	PTR device descriptor
   ptr_unit	PTR unit descriptor
   ptr_mod	PTR modifiers
   ptr_reg	PTR register list
*/

UNIT ptr_unit = {
	UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0),
		SERIAL_IN_WAIT };

REG ptr_reg[] = {
	{ ORDATA (BUF, ptr_unit.buf, 8) },
	{ FLDATA (READY, dev_ready, INT_V_PTR) },
	{ FLDATA (ENABLE, dev_enable, INT_V_PTR) },
	{ DRDATA (POS, ptr_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptr_stopioe, 0) },
	{ NULL }  };

DEVICE ptr_dev = {
	"PTR", &ptr_unit, ptr_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptr_reset,
	&ptr_boot, NULL, NULL };

/* PTP data structures

   ptp_dev	PTP device descriptor
   ptp_unit	PTP unit descriptor
   ptp_mod	PTP modifiers
   ptp_reg	PTP register list
*/

UNIT ptp_unit = {
	UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG ptp_reg[] = {
	{ ORDATA (BUF, ptp_unit.buf, 8) },
	{ FLDATA (READY, dev_ready, INT_V_PTP) },
	{ FLDATA (ENABLE, dev_enable, INT_V_PTP) },
	{ FLDATA (POWER, ptp_power, 0) },
	{ DRDATA (POS, ptp_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
	{ DRDATA (PWRTIME, ptp_ptime, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptp_stopioe, 0) },
	{ NULL }  };


DEVICE ptp_dev = {
	"PTP", &ptp_unit, ptp_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptp_reset,
	NULL, NULL, NULL };

/* TTY data structures

   tty_dev	TTY device descriptor
   tty_unit	TTY unit descriptor
   tty_reg	TTY register list
   tty_mod	TTy modifiers list
*/

#define TTI	0
#define TTO	1

UNIT tty_unit[] = {
	{ UDATA (&tti_svc, UNIT_KSR, 0), KBD_POLL_WAIT },
	{ UDATA (&tto_svc, UNIT_KSR, 0), SERIAL_OUT_WAIT }  };

REG tty_reg[] = {
	{ ORDATA (BUF, tty_buf, 8) },
	{ FLDATA (MODE, tty_mode, 0) },
	{ FLDATA (READY, dev_ready, INT_V_TTY) },
	{ FLDATA (ENABLE, dev_enable, INT_V_TTY) },
	{ DRDATA (KPOS, tty_unit[TTI].pos, 32), PV_LEFT },
	{ DRDATA (KTIME, tty_unit[TTI].wait, 24), REG_NZ + PV_LEFT },
	{ DRDATA (TPOS, tty_unit[TTO].pos, 32), PV_LEFT },
	{ DRDATA (TTIME, tty_unit[TTO].wait, 24), REG_NZ + PV_LEFT },
	{ NULL }  };

MTAB tty_mod[] = {
	{ UNIT_KSR+UNIT_8B, UNIT_KSR, "KSR", "KSR", &tty_set_mode },
	{ UNIT_KSR+UNIT_8B, 0       , "7b" , "7B" , &tty_set_mode },
	{ UNIT_KSR+UNIT_8B, UNIT_8B , "8b" , "8B" , &tty_set_mode },
	{ 0 }  };

DEVICE tty_dev = {
	"TTY", tty_unit, tty_reg, tty_mod,
	2, 10, 31, 1, 8, 8,
	NULL, NULL, &tty_reset,
	NULL, NULL, NULL };

/* CLK data structures

   clk_dev	CLK device descriptor
   clk_unit	CLK unit descriptor
   clk_mod	CLK modifiers
   clk_reg	CLK register list
*/

UNIT clk_unit = {
	UDATA (&clk_svc, 0, 0), 16000 };

REG clk_reg[] = {
	{ FLDATA (READY, dev_ready, INT_V_CLK) },
	{ FLDATA (ENABLE, dev_enable, INT_V_CLK) },
	{ DRDATA (TIME, clk_unit.wait, 24), REG_NZ + PV_LEFT },
	{ DRDATA (TPS, clk_tps, 8), REG_NZ + PV_LEFT },
	{ NULL }  };

DEVICE clk_dev = {
	"CLK", &clk_unit, clk_reg, NULL,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &clk_reset,
	NULL, NULL, NULL };

/* Paper tape reader: IO routine */

int32 ptrio (int32 inst, int32 fnc, int32 dat)
{
switch (inst) {						/* case on opcode */
case ioOCP:						/* OCP */
	if (fnc & 016) return IOBADFNC (dat);		/* only fnc 0,1 */
	if (fnc) sim_cancel (&ptr_unit);		/* fnc 1? stop */
	else sim_activate (&ptr_unit, ptr_unit.wait);	/* fnc 0? start */
	break;
case ioSKS:						/* SKS */
	if (fnc & 013) return IOBADFNC (dat);		/* only fnc 0,4 */
	if (((fnc == 0) && TST_READY (INT_PTR)) ||	/* fnc 0? skip rdy */
	    ((fnc == 4) && !TST_INTREQ (INT_PTR)))	/* fnc 4? skip !int */
	    return IOSKIP (dat);
	break;
case ioINA:						/* INA */
	if (fnc & 007) return IOBADFNC (dat);		/* only fnc 0,10 */
	if (TST_READY (INT_PTR)) {			/* ready? */
	    CLR_READY (INT_PTR);			/* clear ready */
	    return IOSKIP (ptr_unit.buf | dat);  }	/* ret buf, skip */
	break;  }					/* end case op */
return dat;
}

/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32 temp;

if ((ptr_unit.flags & UNIT_ATT) == 0)			/* attached? */
	return IORETURN (ptr_stopioe, SCPE_UNATT);
if ((temp = getc (ptr_unit.fileref)) == EOF) {		/* read byte */
	if (feof (ptr_unit.fileref)) {
	    if (ptr_stopioe) printf ("PTR end of file\n");
	    else return SCPE_OK;  }
	else perror ("PTR I/O error");
	clearerr (ptr_unit.fileref);
	return SCPE_IOERR;  }
SET_READY (INT_PTR);					/* set ready flag */
ptr_unit.buf = temp & 0377;				/* get byte */
ptr_unit.pos = ftell (ptr_unit.fileref);		/* update pos */
sim_activate (&ptr_unit, ptr_unit.wait);		/* reactivate */
return SCPE_OK;
}

/* Reset routine */

t_stat ptr_reset (DEVICE *dptr)
{
CLR_READY (INT_PTR);					/* clear ready, enb */
CLR_ENABLE (INT_PTR);
ptr_unit.buf = 0;					/* clear buffer */
sim_cancel (&ptr_unit);					/* deactivate unit */
return SCPE_OK;
}

/* Paper tape reader bootstrap routine */

#define PBOOT_START	1
#define PBOOT_SIZE	(sizeof (pboot) / sizeof (int32))

static const int32 pboot[] = {
 0010057,		/*	STA 57 */
 0030001,		/*	OCP 1 */
 0131001,		/* READ,	INA 1001 */
 0002003,		/*	JMP READ */
 0101040,		/*	SNZ */
 0002003,		/* 	JMP READ */
 0010000,		/*	STA 0 */
 0131001,		/* READ1,	INA 1001 */
 0002010,		/*	JMP READ1 */
 0041470,		/*	LGL 8 */
 0130001,		/* READ2,	INA 1 */
 0002013,		/*	JMP READ2 */
 0110000,		/*	STA* 0 */
 0024000,		/*	IRS 0 */
 0100040		/*	SZE */
};

t_stat ptr_boot (int32 unitno, DEVICE *dptr)
{
int32 i;

for (i = 0; i < PBOOT_SIZE; i++)			/* copy bootstrap */
	M[PBOOT_START + i] = pboot[i];
PC = PBOOT_START;	
return SCPE_OK;
}

/* Paper tape punch: IO routine */

int32 ptpio (int32 inst, int32 fnc, int32 dat)
{
switch (inst) {						/* case on opcode */
case ioOCP:						/* OCP */
	if (fnc & 016) return IOBADFNC (dat);		/* only fnc 0,1 */
	if (fnc) {					/* fnc 1? pwr off */
	    CLR_READY (INT_PTP);			/* not ready */
	    ptp_power = 0;				/* turn off power */
	    sim_cancel (&ptp_unit);  }			/* stop punch */
	else if (ptp_power == 0)			/* fnc 0? start */
	    sim_activate (&ptp_unit, ptp_ptime);
	break;
case ioSKS:						/* SKS */
	if ((fnc & 012) || (fnc == 005))		/* only 0, 1, 4 */
	    return IOBADFNC (dat);
	if (((fnc == 00) && TST_READY (INT_PTP)) ||	/* fnc 0? skip rdy */
	    ((fnc == 01)				/* fnc 1? skip ptp on */
		&& (ptp_power || sim_is_active (&ptp_unit))) ||
	    ((fnc == 04) && !TST_INTREQ (INT_PTP)))	/* fnc 4? skip !int */
	    return IOSKIP (dat);
	break;
case ioOTA:						/* OTA */
	if (fnc) return IOBADFNC (dat);			/* only fnc 0 */
	if (TST_READY (INT_PTP)) {			/* if ptp ready */
	    CLR_READY (INT_PTP);			/* clear ready */
	    ptp_unit.buf = dat & 0377;			/* store byte */
	    sim_activate (&ptp_unit, ptp_unit.wait);
	    return IOSKIP (dat);  }			/* skip return */
	break;  }
return dat;
}

/* Unit service */

t_stat ptp_svc (UNIT *uptr)
{

SET_READY (INT_PTP);					/* set flag */
if (ptp_power == 0) {					/* power on? */
	ptp_power = 1;					/* ptp is ready */
	return SCPE_OK;  }
if ((ptp_unit.flags & UNIT_ATT) == 0)			/* attached? */
	return IORETURN (ptp_stopioe, SCPE_UNATT);
if (putc (ptp_unit.buf, ptp_unit.fileref) == EOF) {	/* output byte */
	perror ("PTP I/O error");
	clearerr (ptp_unit.fileref);
	return SCPE_IOERR;  }
ptp_unit.pos = ftell (ptp_unit.fileref);		/* update pos */
return SCPE_OK;
}

/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
CLR_READY (INT_PTP);					/* clear ready, enb */
CLR_ENABLE (INT_PTP);
ptp_power = 0;						/* power off */
ptp_unit.buf = 0;					/* clear buffer */
sim_cancel (&ptp_unit);					/* deactivate unit */
return SCPE_OK;
}

/* Terminal: IO routine */

int32 ttyio (int32 inst, int32 fnc, int32 dat)
{
switch (inst) {						/* case on opcode */
case ioOCP:						/* OCP */
	if (fnc & 016) return IOBADFNC (dat);		/* only fnc 0,1 */
	if (fnc && (tty_mode == 0)) {			/* input to output? */
	    if (!sim_is_active (&tty_unit[TTO]))	/* set ready */
		SET_READY (INT_TTY);
	    tty_mode = 1;  }				/* mode is output */
	else if ((fnc == 0) && tty_mode) {		/* output to input? */
	    CLR_READY (INT_TTY);			/* clear ready */
	    tty_mode = 0;  }				/* mode is input */
	break;
case ioSKS:						/* SKS */
	if (fnc & 012) return IOBADFNC (dat);		/* fnc 0,1,4,5 */
	if (((fnc == 0) && TST_READY (INT_TTY)) ||	/* fnc 0? skip rdy */
	    ((fnc == 1) &&				/* fnc 1? skip !busy */
		tty_mode && !sim_is_active (&tty_unit[TTO])) ||
	    ((fnc == 4) && !TST_INTREQ (INT_TTY)) ||	/* fnc 4? skip !int */
	    ((fnc == 5) &&				/* fnc 5? skip !xoff */
		!tty_mode && ((tty_buf & 0177) == 023)))
	    return IOSKIP (dat);
	break;
case ioINA:						/* INA */
	if (fnc & 005) return IOBADFNC (dat);		/* only 0,2,10,12 */
	if (TST_READY (INT_TTY)) {			/* ready? */
	    if (tty_mode == 0) CLR_READY (INT_TTY);	/* inp? clear rdy */
	    return IOSKIP (dat |
		(tty_buf & ((fnc & 002)? 077: 0377)));  }
	break;
case ioOTA:
	if (fnc & 015) return IOBADFNC (dat);		/* only 0,2 */
	if (TST_READY (INT_TTY)) {			/* ready? */
	    tty_buf = dat & 0377;			/* store char */
	    if (fnc & 002) {				/* binary mode? */
		tty_buf = tty_buf | 0100;		/* set ch 7 */
		if (tty_buf & 040) tty_buf = tty_buf & 0277;  }
	    if (tty_mode) {
		sim_activate (&tty_unit[TTO], tty_unit[TTO].wait);
		CLR_READY (INT_TTY);  }
	    return IOSKIP (dat);  }
	break;  }					/* end case op */
return dat;
}

/* Unit service routines */

t_stat tti_svc (UNIT *uptr)
{
int32 out, c;

sim_activate (&tty_unit[TTI], tty_unit[TTI].wait);	/* continue poll */
if ((c = sim_poll_kbd ()) < SCPE_KFLAG) return c;	/* no char or error? */
out = c & 0177;						/* mask echo to 7b */
if (c & SCPE_BREAK) c = 0;				/* break? */
else if (tty_unit[TTI].flags & UNIT_KSR) {		/* KSR? */
	if (islower (out)) out = toupper (out);		/* cvt to UC */
	c = out | 0200;  }				/* add TTY bit */
else c = c & ((tty_unit[TTI].flags & UNIT_8B)? 0377: 0177);
if (tty_mode == 0) {					/* input mode? */
	tty_buf = c;					/* put char in buf */
	tty_unit[TTI].pos = tty_unit[TTI].pos + 1;
	SET_READY (INT_TTY);				/* set flag */
	if (out) sim_putchar (out);  }			/* echo */
return SCPE_OK;
}

t_stat tto_svc (UNIT *uptr)
{
int32 c;
t_stat r;

SET_READY (INT_TTY);					/* set done flag */
if (tty_unit[TTO].flags & UNIT_KSR) {			/* UC only? */
	c = tty_buf & 0177;				/* mask to 7b */
	if (islower (c)) c = toupper (c);  }		/* cvt to UC */
else c = tty_buf & ((tty_unit[TTO].flags & UNIT_8B)? 0377: 0177);
if ((r = sim_putchar (c)) != SCPE_OK) return r;		/* output char */
tty_unit[TTO].pos = tty_unit[TTO].pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat tty_reset (DEVICE *dptr)
{
CLR_READY (INT_TTY);					/* clear ready, enb */
CLR_ENABLE (INT_TTY);
tty_mode = 0;						/* mode = input */
tty_buf = 0;
sim_activate (&tty_unit[TTI], tty_unit[TTI].wait);	/* activate poll */
sim_cancel (&tty_unit[TTO]);				/* cancel output */
return SCPE_OK;
}

t_stat tty_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc)
{
tty_unit[TTI].flags = (tty_unit[TTI].flags & ~(UNIT_KSR | UNIT_8B)) | val;
tty_unit[TTO].flags = (tty_unit[TTO].flags & ~(UNIT_KSR | UNIT_8B)) | val;
return SCPE_OK;
}

/* Clock/options: IO routine */

int32 clkio (int32 inst, int32 fnc, int32 dat)
{
switch (inst) {						/* case on opcode */
case ioOCP:						/* OCP */
	if (fnc & 015) return IOBADFNC (dat);		/* only fnc 0,2 */
	CLR_READY (INT_CLK);				/* reset ready */
	if (fnc) sim_cancel (&clk_unit);		/* fnc = 2? stop */
	else {						/* fnc = 0? */
	    if (!sim_is_active (&clk_unit))
		sim_activate (&clk_unit,		/* activate */
		sim_rtc_init (clk_unit.wait));  }	/* init calibr */
	break;
case ioSKS:						/* SKS */
	if (fnc == 0) {					/* clock skip !int */
	    if (!TST_INTREQ (INT_CLK)) return IOSKIP (dat);  }
	else if ((fnc & 007) == 002) {			/* mem parity? */
	    if (((fnc == 002) && !TST_READY (INT_MPE)) ||
		((fnc == 012) && TST_READY (INT_MPE)))
		return IOSKIP (dat);  }
	else return IOBADFNC (dat);			/* invalid fnc */
	break;
case ioOTA:						/* OTA */
	if (fnc == 000) dev_enable = dat;		/* SMK */
	else if (fnc == 010) {				/* OTK */
	    C = (dat >> 15) & 1;			/* set C */
	    if (cpu_unit.flags & UNIT_HSA)		/* HSA included? */
		dp = (dat >> 14) & 1;			/* set dp */
	    if (cpu_unit.flags & UNIT_EXT) {		/* ext opt? */
		if (dat & 020000) {			/* ext set? */
		    ext = 1;				/* yes, set */
		    extoff_pending = 0;  }
		else extoff_pending = 1;  }		/* no, clr later */
	    sc = dat & 037;  }				/* set sc */
	else return IOBADFNC (dat);
	break;  }
return dat;
}

/* Unit service */

t_stat clk_svc (UNIT *uptr)
{

M[M_CLK] = M[M_CLK + 1] & DMASK;			/* increment mem ctr */
if (M[M_CLK] == 0) SET_READY (INT_CLK);			/* = 0? set flag */
sim_activate (&clk_unit, sim_rtc_calb (clk_tps));	/* reactivate */
return SCPE_OK;
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
CLR_READY (INT_CLK);					/* clear ready, enb */
CLR_ENABLE (INT_CLK);
sim_cancel (&clk_unit);					/* deactivate unit */
return SCPE_OK;
}
