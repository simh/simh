/* hp2100_stddev.c: HP2100 standard devices

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

   ptr		12597A-002 paper tape reader
   ptp		12597A-005 paper tape punch
   tty		12531C buffered teleprinter interface
   clk		12539A/B/C time base generator

   07-Sep-01	RMS	Moved function prototypes
   21-Nov-00	RMS	Fixed flag, buffer power up state
			Added status input for ptp, tty
   15-Oct-00	RMS	Added dynamic device number support

   The reader and punch, like most HP devices, have a command flop.  The
   teleprinter and clock do not.
*/

#include "hp2100_defs.h"
#include <ctype.h>
#define UNIT_V_UC	(UNIT_V_UF + 1)			/* UC only */
#define UNIT_UC		(1 << UNIT_V_UC)
#define PTP_LOW		0000040				/* low tape */
#define TM_MODE		0100000				/* mode change */
#define TM_KBD		0040000				/* enable keyboard */
#define TM_PRI		0020000				/* enable printer */
#define TM_PUN		0010000				/* enable punch */
#define TP_BUSY		0100000				/* busy */
#define CLK_V_ERROR	4				/* clock overrun */
#define CLK_ERROR	(1 << CLK_V_ERROR)

extern uint16 M[];
extern int32 PC;
extern int32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];
extern UNIT cpu_unit;
extern struct hpdev infotab[];
int32 ptr_stopioe = 0, ptp_stopioe = 0;			/* stop on error */
int32 ttp_stopioe = 0;
int32 tty_buf = 0, tty_mode = 0;			/* tty buffer, mode */
int32 clk_select = 0;					/* clock time select */
int32 clk_error = 0;					/* clock error */
int32 clk_delay[8] =					/* clock intervals */
	{ 50, 500, 5000, 50000, 500000, 5000000, 50000000, 50000000 };
t_stat ptr_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptr_boot (int32 unitno);
t_stat ptp_svc (UNIT *uptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tty_reset (DEVICE *dptr);
t_stat clk_svc (UNIT *uptr);
t_stat clk_reset (DEVICE *dptr);

/* PTR data structures

   ptr_dev	PTR device descriptor
   ptr_unit	PTR unit descriptor
   ptr_mod	PTR modifiers
   ptr_reg	PTR register list
*/

UNIT ptr_unit = {
	UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_IN_WAIT };

REG ptr_reg[] = {
	{ ORDATA (BUF, ptr_unit.buf, 8) },
	{ FLDATA (CMD, infotab[inPTR].cmd, 0) },
	{ FLDATA (CTL, infotab[inPTR].ctl, 0) },
	{ FLDATA (FLG, infotab[inPTR].flg, 0) },
	{ FLDATA (FBF, infotab[inPTR].fbf, 0) },
	{ DRDATA (POS, ptr_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptr_stopioe, 0) },
	{ ORDATA (DEVNO, infotab[inPTR].devno, 6), REG_RO },
	{ NULL }  };

MTAB ptr_mod[] = {
	{ UNIT_DEVNO, inPTR, NULL, "DEVNO", &hp_setdev },
	{ 0 }  };

DEVICE ptr_dev = {
	"PTR", &ptr_unit, ptr_reg, ptr_mod,
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
	{ FLDATA (CMD, infotab[inPTP].cmd, 0) },
	{ FLDATA (CTL, infotab[inPTP].ctl, 0) },
	{ FLDATA (FLG, infotab[inPTP].flg, 0) },
	{ FLDATA (FBF, infotab[inPTP].fbf, 0) },
	{ DRDATA (POS, ptp_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptp_stopioe, 0) },
	{ ORDATA (DEVNO, infotab[inPTP].devno, 6), REG_RO },
	{ NULL }  };

MTAB ptp_mod[] = {
	{ UNIT_DEVNO, inPTP, NULL, "DEVNO", &hp_setdev },
	{ 0 }  };

DEVICE ptp_dev = {
	"PTP", &ptp_unit, ptp_reg, ptp_mod,
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
#define TTP	2

UNIT tty_unit[] = {
	{ UDATA (&tti_svc, UNIT_UC, 0), KBD_POLL_WAIT },
	{ UDATA (&tto_svc, UNIT_UC, 0), SERIAL_OUT_WAIT },
	{ UDATA (&tto_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT }  };

REG tty_reg[] = {
	{ ORDATA (BUF, tty_buf, 8) },
	{ ORDATA (MODE, tty_mode, 16) },
	{ FLDATA (CMD, infotab[inTTY].cmd, 0), REG_HRO },
	{ FLDATA (CTL, infotab[inTTY].ctl, 0) },
	{ FLDATA (FLG, infotab[inTTY].flg, 0) },
	{ FLDATA (FBF, infotab[inTTY].fbf, 0) },
	{ DRDATA (KPOS, tty_unit[TTI].pos, 31), PV_LEFT },
	{ DRDATA (KTIME, tty_unit[TTI].wait, 24), REG_NZ + PV_LEFT },
	{ DRDATA (TPOS, tty_unit[TTO].pos, 31), PV_LEFT },
	{ DRDATA (TTIME, tty_unit[TTO].wait, 24), REG_NZ + PV_LEFT },
	{ DRDATA (PPOS, tty_unit[TTP].pos, 31), PV_LEFT },
	{ FLDATA (STOP_IOE, ttp_stopioe, 0) },
	{ ORDATA (DEVNO, infotab[inTTY].devno, 6), REG_RO },
	{ FLDATA (UC, tty_unit[TTI].flags, UNIT_V_UC), REG_HRO },
	{ NULL }  };

MTAB tty_mod[] = {
	{ UNIT_UC, 0, "lower case", "LC", NULL },
	{ UNIT_UC, UNIT_UC, "upper case", "UC", NULL },
	{ UNIT_DEVNO, inTTY, NULL, "DEVNO", &hp_setdev },
	{ 0 }  };

DEVICE tty_dev = {
	"TTY", tty_unit, tty_reg, tty_mod,
	3, 10, 31, 1, 8, 8,
	NULL, NULL, &tty_reset,
	NULL, NULL, NULL };

/* CLK data structures

   clk_dev	CLK device descriptor
   clk_unit	CLK unit descriptor
   clk_mod	CLK modifiers
   clk_reg	CLK register list
*/

UNIT clk_unit = {
	UDATA (&clk_svc, 0, 0) };

REG clk_reg[] = {
	{ ORDATA (SEL, clk_select, 3) },
	{ FLDATA (CMD, infotab[inCLK].cmd, 0), REG_HRO },
	{ FLDATA (CTL, infotab[inCLK].ctl, 0) },
	{ FLDATA (FLG, infotab[inCLK].flg, 0) },
	{ FLDATA (FBF, infotab[inCLK].fbf, 0) },
	{ FLDATA (ERR, clk_error, CLK_V_ERROR) },
	{ DRDATA (TIME0, clk_delay[0], 31), PV_LEFT },
	{ DRDATA (TIME1, clk_delay[1], 31), PV_LEFT },
	{ DRDATA (TIME2, clk_delay[2], 31), PV_LEFT },
	{ DRDATA (TIME3, clk_delay[3], 31), PV_LEFT },
	{ DRDATA (TIME4, clk_delay[4], 31), PV_LEFT },
	{ DRDATA (TIME5, clk_delay[5], 31), PV_LEFT },
	{ DRDATA (TIME6, clk_delay[6], 31), PV_LEFT },
	{ DRDATA (TIME7, clk_delay[7], 31), PV_LEFT },
	{ ORDATA (DEVNO, infotab[inCLK].devno, 6), REG_RO },
	{ NULL }  };

MTAB clk_mod[] = {
	{ UNIT_DEVNO, inCLK, NULL, "DEVNO", &hp_setdev },
	{ 0 }  };

DEVICE clk_dev = {
	"CLK", &clk_unit, clk_reg, clk_mod,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &clk_reset,
	NULL, NULL, NULL };

/* Paper tape reader: IOT routine */

int32 ptrio (int32 inst, int32 IR, int32 dat)
{
int32 dev;

dev = IR & DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & HC) == 0) { setFLG (dev); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (dev) == 0) PC = (PC + 1) & AMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (dev) != 0) PC = (PC + 1) & AMASK;
	return dat;
case ioMIX:						/* merge */
	dat = dat | ptr_unit.buf;
	break;
case ioLIX:						/* load */
	dat = ptr_unit.buf;
	break;
case ioCTL:						/* control clear/set */
	if (IR & AB) {					/* CLC */
		clrCMD (dev);				/* clear cmd, ctl */
		clrCTL (dev); }
	else {	setCMD (dev);				/* STC */
		setCTL (dev);				/* set cmd, ctl */
		sim_activate (&ptr_unit, ptr_unit.wait);  }
	break;
default:
	break;  }
if (IR & HC) { clrFLG (dev); }				/* H/C option */
return dat;
}

/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32 dev, temp;

dev = infotab[inPTR].devno;				/* get device no */
clrCMD (dev);						/* clear cmd */
if ((ptr_unit.flags & UNIT_ATT) == 0)			/* attached? */
	return IORETURN (ptr_stopioe, SCPE_UNATT);
if ((temp = getc (ptr_unit.fileref)) == EOF) {		/* read byte */
	if (feof (ptr_unit.fileref)) {
		if (ptr_stopioe) printf ("PTR end of file\n");
		else return SCPE_OK;  }
	else perror ("PTR I/O error");
	clearerr (ptr_unit.fileref);
	return SCPE_IOERR;  }
setFLG (dev);						/* set flag */
ptr_unit.buf = temp & 0377;				/* put byte in buf */
ptr_unit.pos = ftell (ptr_unit.fileref);
return SCPE_OK;
}

/* Reset routine - called from SCP, flags in infotab */

t_stat ptr_reset (DEVICE *dptr)
{
infotab[inPTR].cmd = infotab[inPTR].ctl = 0;		/* clear cmd, ctl */
infotab[inPTR].flg = infotab[inPTR].fbf = 1;		/* set flg, fbf */
ptr_unit.buf = 0;
sim_cancel (&ptr_unit);					/* deactivate unit */
return SCPE_OK;
}

/* Paper tape reader bootstrap routine */

#define CHANGE_DEV	(1 << 24)
#define PBOOT_MASK	077
#define PBOOT_SIZE	(sizeof (pboot) / sizeof (int32))

static const int32 pboot[] = {
0107700, 0063770, 0106501, 0004010,
0002400, 0006020, 0063771, 0073736,
0006401, 0067773, 0006006, 0027717,
0107700, 0102077, 0027700, 0017762,
0002003, 0027712, 0003104, 0073774,
0017762, 0017753, 0070001, 0073775,
0063775, 0043772, 0002040, 0027751,
0017753, 0044000, 0000000, 0002101,
0102000, 0037775, 0037774, 0027730,
0017753, 0054000, 0027711, 0102011,
0027700, 0102055, 0027700, 0000000,
0017762, 0001727, 0073776, 0017762,
0033776, 0127753, 0000000, 0103700+CHANGE_DEV,
0102300+CHANGE_DEV, 0027764, 0102500+CHANGE_DEV, 0127762,
0173775, 0153775, 0170100, 0177765 };

t_stat ptr_boot (int32 unit)
{
int32 i, dev;

dev = infotab[inPTR].devno;				/* get device no */
PC = (MEMSIZE - 1) & ~PBOOT_MASK;			/* start at mem top */
for (i = 0; i < PBOOT_SIZE; i++)			/* copy bootstrap */
	M[PC + i] = (pboot[i] & CHANGE_DEV)?		/* insert ptr dev no */
		((pboot[i] | dev) & DMASK): pboot[i];	
return SCPE_OK;
}

/* Paper tape punch: IOT routine */

int32 ptpio (int32 inst, int32 IR, int32 dat)
{
int32 dev;

dev = IR & DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & HC) == 0) { setFLG (dev); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (dev) == 0) PC = (PC + 1) & AMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (dev) != 0) PC = (PC + 1) & AMASK;
	return dat;
case ioLIX:						/* load */
	dat = 0;
case ioMIX:						/* merge */
	if ((ptp_unit.flags & UNIT_ATT) == 0)
		dat = dat | PTP_LOW;			/* out of tape? */
	break;
case ioOTX:						/* output */
	ptp_unit.buf = dat;
	break;
case ioCTL:						/* control clear/set */
	if (IR & AB) {					/* CLC */
		clrCMD (dev);				/* clear cmd, ctl */
		clrCTL (dev); }
	else {	setCMD (dev);				/* STC */
		setCTL (dev);				/* set cmd, ctl */
		sim_activate (&ptp_unit, ptp_unit.wait);  }
	break;
default:
	break;  }
if (IR & HC) { clrFLG (dev); }				/* H/C option */
return dat;
}

/* Unit service */

t_stat ptp_svc (UNIT *uptr)
{
int32 dev;

dev = infotab[inPTP].devno;				/* get device no */
clrCMD (dev);						/* clear cmd */
setFLG (dev);						/* set flag */
if ((ptp_unit.flags & UNIT_ATT) == 0)			/* attached? */
	return IORETURN (ptp_stopioe, SCPE_UNATT);
if (putc (ptp_unit.buf, ptp_unit.fileref) == EOF) {	/* output byte */
	perror ("PTP I/O error");
	clearerr (ptp_unit.fileref);
	return SCPE_IOERR;  }
ptp_unit.pos = ftell (ptp_unit.fileref);		/* update position */
return SCPE_OK;
}

/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
infotab[inPTP].cmd = infotab[inPTP].ctl = 0;		/* clear cmd, ctl */
infotab[inPTP].flg = infotab[inPTP].fbf = 1;		/* set flg, fbf */
ptp_unit.buf = 0;
sim_cancel (&ptp_unit);					/* deactivate unit */
return SCPE_OK;
}

/* Terminal: IOT routine */

int32 ttyio (int32 inst, int32 IR, int32 dat)
{
int32 dev;

dev = IR & DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & HC) == 0) { setFLG (dev); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (dev) == 0) PC = (PC + 1) & AMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (dev) != 0) PC = (PC + 1) & AMASK;
	return dat;
case ioLIX:						/* load */
	dat = 0;
case ioMIX:						/* merge */
	dat = dat | tty_buf;
	if (!(tty_mode & TM_KBD) && sim_is_active (&tty_unit[TTO]))
		dat = dat | TP_BUSY;
	break;
case ioOTX:						/* output */
	if (dat & TM_MODE) tty_mode = dat;
	else tty_buf = dat & 0377;
	break;
case ioCTL:						/* control clear/set */
	if (IR & AB) { clrCTL (dev); }			/* CLC */
	else {	setCTL (dev);				/* STC */
		if (!(tty_mode & TM_KBD)) 		/* output? */
			sim_activate (&tty_unit[TTO], tty_unit[TTO].wait);  }
	break;
default:
	break;  }
if (IR & HC) { clrFLG (dev); }				/* H/C option */
return dat;
}

/* Unit service routines */

t_stat tto_out (int32 ch)
{
t_stat ret = SCPE_OK;

if (tty_mode & TM_PRI) {				/* printing? */
	ret = sim_putchar (ch & 0177);			/* output char */
	tty_unit[TTO].pos = tty_unit[TTO].pos + 1;  }
if (tty_mode & TM_PUN) {				/* punching? */
	if ((tty_unit[TTP].flags & UNIT_ATT) == 0)	/* attached? */
		return IORETURN (ttp_stopioe, SCPE_UNATT);
	if (putc (ch, tty_unit[TTP].fileref) == EOF) {	/* output char */
		perror ("TTP I/O error");
		clearerr (tty_unit[TTP].fileref);
		return SCPE_IOERR;  }
	tty_unit[TTP].pos = ftell (tty_unit[TTP].fileref);  }
return ret;
}

t_stat tti_svc (UNIT *uptr)
{
int32 temp, dev;

dev = infotab[inTTY].devno;				/* get device no */
sim_activate (&tty_unit[TTI], tty_unit[TTI].wait);	/* continue poll */
if ((temp = sim_poll_kbd ()) < SCPE_KFLAG) return temp;	/* no char or error? */
temp = temp & 0177;
if ((tty_unit[TTI].flags & UNIT_UC) && islower (temp))	/* force upper case? */
	 temp = toupper (temp);
if (tty_mode & TM_KBD) {				/* keyboard enabled? */
	tty_buf = temp;					/* put char in buf */
	tty_unit[TTI].pos = tty_unit[TTI].pos + 1;
	setFLG (dev);					/* set flag */
	return tto_out (temp);  }			/* echo or punch? */
return SCPE_OK;
}

t_stat tto_svc (UNIT *uptr)
{
int32 ch, dev;

dev = infotab[inTTY].devno;				/* get device no */
setFLG (dev);						/* set done flag */
ch = tty_buf;
tty_buf = 0377;						/* defang buf */
return tto_out (ch);					/* print and/or punch */
}

/* Reset routine */

t_stat tty_reset (DEVICE *dptr)
{
infotab[inTTY].cmd = infotab[inTTY].ctl = 0;		/* clear cmd, ctl */
infotab[inTTY].flg = infotab[inTTY].fbf = 1;		/* set flg, fbf */
tty_mode = 0;
tty_buf = 0;
sim_activate (&tty_unit[TTI], tty_unit[TTI].wait);	/* activate poll */
sim_cancel (&tty_unit[TTO]);				/* cancel output */
return SCPE_OK;
}

/* Clock: IOT routine */

int32 clkio (int32 inst, int32 IR, int32 dat)
{
int32 dev;

dev = IR & DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & HC) == 0) { setFLG (dev); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (dev) == 0) PC = (PC + 1) & AMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (dev) != 0) PC = (PC + 1) & AMASK;
	return dat;
case ioMIX:						/* merge */
	dat = dat | clk_error;
	break;
case ioLIX:						/* load */
	dat = clk_error;
	break;
case ioOTX:						/* output */
	clk_select = dat & 07;
	break;
case ioCTL:						/* control clear/set */
	if (IR & AB) {					/* CLC */
		clrCTL (dev);				/* turn off clock */
		sim_cancel (&clk_unit);  }		/* deactivate unit */
	else {	setCTL (dev);				/* turn on clock */
		clk_error = 0;				/* clear error */
		sim_activate (&clk_unit, clk_delay[clk_select]);  }
	break;
default:
	break;  }
if (IR & HC) { clrFLG (dev); }				/* H/C option */
return dat;
}

/* Unit service */

t_stat clk_svc (UNIT *uptr)
{
int32 dev;

dev = infotab[inCLK].devno;				/* get device no */
if (FLG (dev)) clk_error = CLK_ERROR;			/* overrun? */
setFLG (dev);						/* set device flag */
return SCPE_OK;
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
infotab[inCLK].cmd = infotab[inCLK].ctl = 0;		/* clear cmd, ctl */
infotab[inCLK].flg = infotab[inCLK].fbf = 1;		/* set flg, fbf */
clk_error = 0;						/* clear error */
clk_select = 0;						/* clear select */
sim_cancel (&clk_unit);					/* deactivate unit */
return SCPE_OK;
}
