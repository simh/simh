/* pdp1_stddev.c: PDP-1 standard devices

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

   ptr		paper tape reader
   ptp		paper tape punch
   tti		keyboard
   tto		teleprinter

   25-Apr-03	RMS	Revised for extended file support
   22-Dec-02	RMS	Added break support
   29-Nov-02	RMS	Fixed output flag initialization (found by Derek Peschel)
   21-Nov-02	RMS	Changed typewriter to half duplex (found by Derek Peschel)
   06-Oct-02	RMS	Revised for V2.10
   30-May-02	RMS	Widened POS to 32b
   29-Nov-01	RMS	Added read only unit support
   07-Sep-01	RMS	Moved function prototypes
   10-Jun-01	RMS	Fixed comment
   30-Oct-00	RMS	Standardized device naming
*/

#include "pdp1_defs.h"

#define FIODEC_UC	074
#define FIODEC_LC	072
#define UC_V		6				/* upper case */
#define UC		(1 << UC_V)
#define BOTH		(1 << (UC_V + 1))		/* both cases */
#define CW		(1 << (UC_V + 2))		/* char waiting */
#define TT_WIDTH	077
#define TTI		0
#define TTO		1

extern int32 sbs, ioc, iosta, PF, IO, PC;
extern int32 M[];

int32 ptr_rpls = 0, ptr_stopioe = 0, ptr_state = 0;
int32 ptp_rpls = 0, ptp_stopioe = 0;
int32 tti_hold = 0;					/* tti hold buf */
int32 tto_rpls = 0;					/* tto restart */
int32 tty_buf = 0;					/* tty buffer */
int32 tty_uc = 0;					/* tty uc/lc */

t_stat ptr_svc (UNIT *uptr);
t_stat ptp_svc (UNIT *uptr);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat tty_reset (DEVICE *dptr);
t_stat ptr_boot (int32 unitno, DEVICE *dptr);

/* Character translation tables */

int32 fiodec_to_ascii[128] = {
	' ', '1', '2', '3', '4', '5', '6', '7',		/* lower case */
	'8', '9', 0, 0, 0, 0, 0, 0,
	'0', '/', 's', 't', 'u', 'v', 'w', 'x',
	'y', 'z', 0, ',', 0, 0, '\t', 0,
	'@', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
	'q', 'r', 0, 0, '-', ')', '\\', '(',
	0, 'a', 'b', 'c', 'd', 'e', 'f', 'g',
	'h', 'i', '{', '.', '}', '\b', 0, '\r',
	' ', '"', '\'', '~', '#', '!', '&', '<',	/* upper case */
	'>', '^', 0, 0, 0, 0, 0, 0,
	'`', '?', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 0, '=', 0, 0, '\t', 0,
	'_', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 0, 0, '+', ']', '|', '[',
	0, 'A', 'B', 'C', 'D', 'E', 'F', 'G',
	'H', 'I', '{', '*', '}', '\b', 0, '\r' };

int32 ascii_to_fiodec[128] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	BOTH+075, BOTH+036, 0, 0, 0, BOTH+077, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	BOTH+0, UC+005, UC+001, UC+004, 0, 0, UC+006, UC+002,
	057, 055, UC+073, UC+054, 033, 054, 073, 021,
	020, 001, 002, 003, 004, 005, 006, 007,
	010, 011, 0, 0, UC+007, UC+033, UC+010, UC+021,
	040, UC+061, UC+062, UC+063, UC+064, UC+065, UC+066, UC+067,
	UC+070, UC+071, UC+041, UC+042, UC+043, UC+044, UC+045, UC+046,
	UC+047, UC+050, UC+051, UC+022, UC+023, UC+024, UC+025, UC+026,
	UC+027, UC+030, UC+031, UC+057, 056, UC+055, UC+011, UC+040,
	UC+020, 061, 062, 063, 064, 065, 066, 067,
	070, 071, 041, 042, 043, 044, 045, 046,
	047, 050, 051, 022, 023, 024, 025, 026,
	027, 030, 031, 0, UC+056, 0, UC+003, BOTH+075 };

/* PTR data structures

   ptr_dev	PTR device descriptor
   ptr_unit	PTR unit
   ptr_reg	PTR register list
*/

UNIT ptr_unit = {
	UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0),
		SERIAL_IN_WAIT };

REG ptr_reg[] = {
	{ ORDATA (BUF, ptr_unit.buf, 18) },
	{ FLDATA (DONE, iosta, IOS_V_PTR) },
	{ FLDATA (RPLS, ptr_rpls, 0) },
	{ ORDATA (STATE, ptr_state, 5), REG_HRO },
	{ DRDATA (POS, ptr_unit.pos, T_ADDR_W), PV_LEFT },
	{ DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptr_stopioe, 0) },
	{ NULL }  };

DEVICE ptr_dev = {
	"PTR", &ptr_unit, ptr_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptr_reset,
	&ptr_boot, NULL, NULL,
	NULL, 0 };

/* PTP data structures

   ptp_dev	PTP device descriptor
   ptp_unit	PTP unit
   ptp_reg	PTP register list
*/

UNIT ptp_unit = {
	UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG ptp_reg[] = {
	{ ORDATA (BUF, ptp_unit.buf, 8) },
	{ FLDATA (DONE, iosta, IOS_V_PTP) },
	{ FLDATA (RPLS, ptp_rpls, 0) },
	{ DRDATA (POS, ptp_unit.pos, T_ADDR_W), PV_LEFT },
	{ DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptp_stopioe, 0) },
	{ NULL }  };

DEVICE ptp_dev = {
	"PTP", &ptp_unit, ptp_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptp_reset,
	NULL, NULL, NULL,
	NULL, 0 };

/* TTY data structures

   tty_dev	TTY device descriptor
   tty_unit	TTY unit
   tty_reg	TTY register list
*/

UNIT tty_unit[] = {
	{ UDATA (&tti_svc, 0, 0), KBD_POLL_WAIT },
	{ UDATA (&tto_svc, 0, 0), SERIAL_OUT_WAIT }  };

REG tty_reg[] = {
	{ ORDATA (BUF, tty_buf, 6) },
	{ FLDATA (UC, tty_uc, UC_V) },
	{ FLDATA (RPLS, tto_rpls, 0) },
	{ ORDATA (HOLD, tti_hold, 9), REG_HRO },
	{ FLDATA (KDONE, iosta, IOS_V_TTI) },
	{ DRDATA (KPOS, tty_unit[TTI].pos, T_ADDR_W), PV_LEFT },
	{ DRDATA (KTIME, tty_unit[TTI].wait, 24), REG_NZ + PV_LEFT },
	{ FLDATA (TDONE, iosta, IOS_V_TTO) },
	{ DRDATA (TPOS, tty_unit[TTO].pos, T_ADDR_W), PV_LEFT },
	{ DRDATA (TTIME, tty_unit[TTO].wait, 24), PV_LEFT },
	{ NULL }  };

DEVICE tty_dev = {
	"TTY", tty_unit, tty_reg, NULL,
	2, 10, 31, 1, 8, 8,
	NULL, NULL, &tty_reset,
	NULL, NULL, NULL,
	NULL, 0 };

/* Paper tape reader: IOT routine */

int32 ptr (int32 inst, int32 dev, int32 data)
{
iosta = iosta & ~IOS_PTR;				/* clear flag */
if (dev == 0030) return ptr_unit.buf;			/* RRB */
ptr_state = (dev == 0002)? 18: 0;			/* mode = bin/alp */
ptr_rpls = 0;
ptr_unit.buf = 0;					/* clear buffer */
sim_activate (&ptr_unit, ptr_unit.wait);
if (GEN_CPLS (inst)) {					/* comp pulse? */
	ioc = 0;
	ptr_rpls = 1;  }
return data;
}

/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32 temp;

if ((ptr_unit.flags & UNIT_ATT) == 0)			/* attached? */
	return IORETURN (ptr_stopioe, SCPE_UNATT);
if ((temp = getc (ptr_unit.fileref)) == EOF) {		/* end of file? */
	if (feof (ptr_unit.fileref)) {
	    if (ptr_stopioe) printf ("PTR end of file\n");
	    else return SCPE_OK;  }
	else perror ("PTR I/O error");
	clearerr (ptr_unit.fileref);
	return SCPE_IOERR;  }
ptr_unit.pos = ptr_unit.pos + 1;
if (ptr_state == 0) ptr_unit.buf = temp & 0377;		/* alpha */
else if (temp & 0200) {					/* binary */
	ptr_state = ptr_state - 6;
	ptr_unit.buf = ptr_unit.buf | ((temp & 077) << ptr_state);  }
if (ptr_state == 0) {					/* done? */
	if (ptr_rpls) IO = ptr_unit.buf;		/* restart? fill IO */
	iosta = iosta | IOS_PTR;			/* set flag */
	sbs = sbs | SB_RQ;				/* req seq break */
	ioc = ioc | ptr_rpls;  }			/* restart */
else sim_activate (&ptr_unit, ptr_unit.wait);		/* get next char */
return SCPE_OK;
}

/* Reset routine */

t_stat ptr_reset (DEVICE *dptr)
{
ptr_state = 0;						/* clear state */
ptr_unit.buf = 0;
ptr_rpls = 0;
iosta = iosta & ~IOS_PTR;				/* clear flag */
sim_cancel (&ptr_unit);					/* deactivate unit */
return SCPE_OK;
}

/* Bootstrap routine */

#define BOOT_START	07772
#define BOOT_LEN	(sizeof (boot_rom) / sizeof (int))

static const int32 boot_rom[] = {
	0730002,					/* r, rpb + wait */
	0327776,					/* dio x */
	0107776,					/* xct x */
	0730002,					/* rpb + wait */
	0760400,					/* x, halt */
	0607772						/* jmp r */
};

t_stat ptr_boot (int32 unitno, DEVICE *dptr)
{
int32 i;

for (i = 0; i < BOOT_LEN; i++) M[BOOT_START + i] = boot_rom[i];
PC = BOOT_START;
return SCPE_OK;
}

/* Paper tape punch: IOT routine */

int32 ptp (int32 inst, int32 dev, int32 data)
{
iosta = iosta & ~IOS_PTP;				/* clear flag */
ptp_rpls = 0;
ptp_unit.buf = (dev == 0006)? ((data >> 12) | 0200): (data & 0377);
sim_activate (&ptp_unit, ptp_unit.wait);		/* start unit */
if (GEN_CPLS (inst)) {					/* comp pulse? */
	ioc = 0;
	ptp_rpls = 1;  }
return data;
}

/* Unit service */

t_stat ptp_svc (UNIT *uptr)
{
iosta = iosta | IOS_PTP;				/* set flag */
sbs = sbs | SB_RQ;					/* req seq break */
ioc = ioc | ptp_rpls;					/* process restart */
if ((ptp_unit.flags & UNIT_ATT) == 0)			/* not attached? */
	return IORETURN (ptp_stopioe, SCPE_UNATT);
if (putc (ptp_unit.buf, ptp_unit.fileref) == EOF) {	/* I/O error? */
	perror ("PTP I/O error");
	clearerr (ptp_unit.fileref);
	return SCPE_IOERR;  }
ptp_unit.pos = ptp_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
ptp_unit.buf = 0;					/* clear state */
ptp_rpls = 0;
iosta = iosta & ~IOS_PTP;				/* clear flag */
sim_cancel (&ptp_unit);					/* deactivate unit */
return SCPE_OK;
}

/* Typewriter IOT routines */

int32 tti (int32 inst, int32 dev, int32 data)
{
iosta = iosta & ~IOS_TTI;				/* clear flag */
if (inst & (IO_WAIT | IO_CPLS))				/* wait or sync? */
	return (STOP_RSRV << IOT_V_REASON) | (tty_buf & 077);
return tty_buf & 077;
}

int32 tto (int32 inst, int32 dev, int32 data)
{
iosta = iosta & ~IOS_TTO;				/* clear flag */
tto_rpls = 0;
tty_buf = data & TT_WIDTH;				/* load buffer */
sim_activate (&tty_unit[TTO], tty_unit[TTO].wait);	/* activate unit */
if (GEN_CPLS (inst)) {					/* comp pulse? */
	ioc = 0;
	tto_rpls = 1;  }
return data;
}

/* Unit service routines */

t_stat tti_svc (UNIT *uptr)
{
int32 in, temp;

sim_activate (uptr, uptr->wait);			/* continue poll */
if (tti_hold & CW) {					/* char waiting? */
	tty_buf = tti_hold & TT_WIDTH;			/* return char */
	tti_hold = 0;  }				/* not waiting */
else {	if ((temp = sim_poll_kbd ()) < SCPE_KFLAG) return temp;
	if (temp & SCPE_BREAK) return SCPE_OK;		/* ignore break */
	temp = temp & 0177;
	if (temp == 0177) temp = '\b';			/* rubout? bs */
	sim_putchar (temp);				/* echo */
	if (temp == '\r') sim_putchar ('\n');		/* cr? add nl */
	in = ascii_to_fiodec[temp];			/* translate char */
	if (in == 0) return SCPE_OK;			/* no xlation? */
	if ((in & BOTH) || ((in & UC) == (tty_uc & UC)))
	    tty_buf = in & TT_WIDTH;
	else {						/* must shift */
	    tty_uc = in & UC;				/* new case */
	    tty_buf = tty_uc? FIODEC_UC: FIODEC_LC;
	    tti_hold = in | CW;  }  }			/* set 2nd waiting */
iosta = iosta | IOS_TTI;				/* set flag */
sbs = sbs | SB_RQ;					/* req seq break */
PF = PF | 040;						/* set prog flag 1 */
uptr->pos = uptr->pos + 1;
return SCPE_OK;
}

t_stat tto_svc (UNIT *uptr)
{
int32 out;

iosta = iosta | IOS_TTO;				/* set flag */
sbs = sbs | SB_RQ;					/* req seq break */
ioc = ioc | tto_rpls;					/* process restart */
if (tty_buf == FIODEC_UC) {				/* upper case? */
	tty_uc = UC;
	return SCPE_OK;  }
if (tty_buf == FIODEC_LC) {				/* lower case? */
	tty_uc = 0;
	return SCPE_OK;  }
out = fiodec_to_ascii[tty_buf | tty_uc];		/* translate */
if (out == 0) return SCPE_OK;				/* no translation? */
sim_putchar (out);
uptr->pos = uptr->pos + 1;
if (out == '\r') {					/* cr? add lf */
	sim_putchar ('\n');
	uptr->pos = uptr->pos + 1;  }
return SCPE_OK;
}

/* Reset routine */

t_stat tty_reset (DEVICE *dptr)
{
tty_buf = 0;						/* clear buffer */
tty_uc = 0;						/* clear case */
tti_hold = 0;						/* clear hold buf */
tto_rpls = 0;						/* clear reset pulse */
iosta = (iosta & ~IOS_TTI) | IOS_TTO;			/* clear flag */
sim_activate (&tty_unit[TTI], tty_unit[TTI].wait);	/* activate keyboard */
sim_cancel (&tty_unit[TTO]);				/* stop printer */
return SCPE_OK;
}
