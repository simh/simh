/* hp2100_stddev.c: HP2100 standard devices simulator

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
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTI_CTLILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LII_CTLLE FOR ANY CLAIM, DAMAGES OR OTHER LII_CTLILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   ptr		12597A-002 paper tape reader
   ptp		12597A-005 paper tape punch
   tty		12531C buffered teleprinter interface
   clk		12539C time base generator

   29-Mar-03	RMS	Added support for console backpressure
   25-Apr-03	RMS	Added extended file support
   22-Dec-02	RMS	Added break support
   01-Nov-02	RMS	Revised BOOT command for IBL ROMs
			Fixed bug in TTY reset, TTY starts in input mode
			Fixed bug in TTY mode OTA, stores data as well
			Fixed clock to add calibration, proper start/stop
			Added UC option to TTY output
   30-May-02	RMS	Widened POS to 32b
   22-Mar-02	RMS	Revised for dynamically allocated memory
   03-Nov-01	RMS	Changed DEVNO to use extended SET/SHOW
   29-Nov-01	RMS	Added read only unit support
   24-Nov-01	RMS	Changed TIME to an array
   07-Sep-01	RMS	Moved function prototypes
   21-Nov-00	RMS	Fixed flag, buffer power up state
			Added status input for ptp, tty
   15-Oct-00	RMS	Added dynamic device number support

   The reader and punch, like most HP devices, have a command flop.  The
   teleprinter and clock do not.

   The clock autocalibrates.  If the specified clock frequency is below
   10Hz, the clock service routine runs at 10Hz and counts down a repeat
   counter before generating an interrupt.  Autocalibration will not work
   if the clock is running at 1Hz or less.

   Clock diagnostic mode corresponds to inserting jumper W2 on the 12539C.
   This turns off autocalibration and divides the longest time intervals down
   by 10**3.  The clk_time values were chosen to allow the diagnostic to
   pass its clock calibration test.
*/

#include "hp2100_defs.h"
#include <ctype.h>
#define UNIT_V_8B	(UNIT_V_UF + 0)			/* 8B */
#define UNIT_V_UC	(UNIT_V_UF + 1)			/* UC only */
#define UNIT_V_DIAG	(UNIT_V_UF + 2)			/* diag mode */
#define UNIT_8B		(1 << UNIT_V_8B)
#define UNIT_UC		(1 << UNIT_V_UC)
#define UNIT_DIAG	(1 << UNIT_V_DIAG)

#define PTP_LOW		0000040				/* low tape */
#define TM_MODE		0100000				/* mode change */
#define TM_KBD		0040000				/* enable keyboard */
#define TM_PRI		0020000				/* enable printer */
#define TM_PUN		0010000				/* enable punch */
#define TP_BUSY		0100000				/* busy */

#define CLK_V_ERROR	4				/* clock overrun */
#define CLK_ERROR	(1 << CLK_V_ERROR)

extern uint16 *M;
extern uint32 PC, SR;
extern uint32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];
extern UNIT cpu_unit;

int32 ptr_stopioe = 0, ptp_stopioe = 0;			/* stop on error */
int32 ttp_stopioe = 0;
int32 tty_buf = 0, tty_mode = 0;			/* tty buffer, mode */
int32 clk_select = 0;					/* clock time select */
int32 clk_error = 0;					/* clock error */
int32 clk_ctr = 0;					/* clock counter */
int32 clk_time[8] =					/* clock intervals */
	{ 155, 1550, 15500, 155000, 155000, 155000, 155000, 155000 };
int32 clk_tps[8] =					/* clock tps */
	{ 10000, 1000, 100, 10, 10, 10, 10, 10 };
int32 clk_rpt[8] =					/* number of repeats */
	{ 1, 1, 1, 1, 10, 100, 1000, 10000 };

DEVICE ptr_dev, ptp_dev, tty_dev, clk_dev;
int32 ptrio (int32 inst, int32 IR, int32 dat);
t_stat ptr_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptr_boot (int32 unitno, DEVICE *dptr);
int32 ptpio (int32 inst, int32 IR, int32 dat);
t_stat ptp_svc (UNIT *uptr);
t_stat ptp_reset (DEVICE *dptr);
int32 ttyio (int32 inst, int32 IR, int32 dat);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tty_reset (DEVICE *dptr);
t_stat tty_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc);
int32 clkio (int32 inst, int32 IR, int32 dat);
t_stat clk_svc (UNIT *uptr);
t_stat clk_reset (DEVICE *dptr);
int32 clk_delay (int32 flg);

/* PTR data structures

   ptr_dev	PTR device descriptor
   ptr_unit	PTR unit descriptor
   ptr_mod	PTR modifiers
   ptr_reg	PTR register list
*/

DIB ptr_dib = { PTR, 0, 0, 0, 0, &ptrio };

UNIT ptr_unit = {
	UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0),
		SERIAL_IN_WAIT };

REG ptr_reg[] = {
	{ ORDATA (BUF, ptr_unit.buf, 8) },
	{ FLDATA (CMD, ptr_dib.cmd, 0) },
	{ FLDATA (CTL, ptr_dib.ctl, 0) },
	{ FLDATA (FLG, ptr_dib.flg, 0) },
	{ FLDATA (FBF, ptr_dib.fbf, 0) },
	{ DRDATA (POS, ptr_unit.pos, T_ADDR_W), PV_LEFT },
	{ DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptr_stopioe, 0) },
	{ ORDATA (DEVNO, ptr_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB ptr_mod[] = {
	{ MTAB_XTD | MTAB_VDV, 0, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &ptr_dev },
	{ 0 }  };

DEVICE ptr_dev = {
	"PTR", &ptr_unit, ptr_reg, ptr_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptr_reset,
	&ptr_boot, NULL, NULL,
	&ptr_dib, DEV_DISABLE };

/* PTP data structures

   ptp_dev	PTP device descriptor
   ptp_unit	PTP unit descriptor
   ptp_mod	PTP modifiers
   ptp_reg	PTP register list
*/

DIB ptp_dib = { PTP, 0, 0, 0, 0, &ptpio };

UNIT ptp_unit = {
	UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG ptp_reg[] = {
	{ ORDATA (BUF, ptp_unit.buf, 8) },
	{ FLDATA (CMD, ptp_dib.cmd, 0) },
	{ FLDATA (CTL, ptp_dib.ctl, 0) },
	{ FLDATA (FLG, ptp_dib.flg, 0) },
	{ FLDATA (FBF, ptp_dib.fbf, 0) },
	{ DRDATA (POS, ptp_unit.pos, T_ADDR_W), PV_LEFT },
	{ DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptp_stopioe, 0) },
	{ ORDATA (DEVNO, ptp_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB ptp_mod[] = {
	{ MTAB_XTD | MTAB_VDV, 0, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &ptp_dev },
	{ 0 }  };

DEVICE ptp_dev = {
	"PTP", &ptp_unit, ptp_reg, ptp_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptp_reset,
	NULL, NULL, NULL,
	&ptp_dib, DEV_DISABLE };

/* TTY data structures

   tty_dev	TTY device descriptor
   tty_unit	TTY unit descriptor
   tty_reg	TTY register list
   tty_mod	TTy modifiers list
*/

#define TTI	0
#define TTO	1
#define TTP	2

DIB tty_dib = { TTY, 0, 0, 0, 0, &ttyio };

UNIT tty_unit[] = {
	{ UDATA (&tti_svc, UNIT_UC, 0), KBD_POLL_WAIT },
	{ UDATA (&tto_svc, UNIT_UC, 0), SERIAL_OUT_WAIT },
	{ UDATA (&tto_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_8B, 0), SERIAL_OUT_WAIT }  };

REG tty_reg[] = {
	{ ORDATA (BUF, tty_buf, 8) },
	{ ORDATA (MODE, tty_mode, 16) },
	{ FLDATA (CMD, tty_dib.cmd, 0), REG_HRO },
	{ FLDATA (CTL, tty_dib.ctl, 0) },
	{ FLDATA (FLG, tty_dib.flg, 0) },
	{ FLDATA (FBF, tty_dib.fbf, 0) },
	{ DRDATA (KPOS, tty_unit[TTI].pos, T_ADDR_W), PV_LEFT },
	{ DRDATA (KTIME, tty_unit[TTI].wait, 24), REG_NZ + PV_LEFT },
	{ DRDATA (TPOS, tty_unit[TTO].pos, T_ADDR_W), PV_LEFT },
	{ DRDATA (TTIME, tty_unit[TTO].wait, 24), REG_NZ + PV_LEFT },
	{ DRDATA (PPOS, tty_unit[TTP].pos, T_ADDR_W), PV_LEFT },
	{ FLDATA (STOP_IOE, ttp_stopioe, 0) },
	{ ORDATA (DEVNO, tty_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB tty_mod[] = {
	{ UNIT_UC+UNIT_8B, UNIT_UC, "UC", "UC", &tty_set_mode },
	{ UNIT_UC+UNIT_8B, 0      , "7b", "7B", &tty_set_mode },
	{ UNIT_UC+UNIT_8B, UNIT_8B, "8b", "8B", &tty_set_mode },
	{ MTAB_XTD | MTAB_VDV, 0, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &tty_dev },
	{ 0 }  };

DEVICE tty_dev = {
	"TTY", tty_unit, tty_reg, tty_mod,
	3, 10, 31, 1, 8, 8,
	NULL, NULL, &tty_reset,
	NULL, NULL, NULL,
	&tty_dib, 0 };

/* CLK data structures

   clk_dev	CLK device descriptor
   clk_unit	CLK unit descriptor
   clk_mod	CLK modifiers
   clk_reg	CLK register list
*/

DIB clk_dib = { CLK, 0, 0, 0, 0, &clkio };

UNIT clk_unit = {
	UDATA (&clk_svc, 0, 0) };

REG clk_reg[] = {
	{ ORDATA (SEL, clk_select, 3) },
	{ DRDATA (CTR, clk_ctr, 14) },
	{ FLDATA (CMD, clk_dib.cmd, 0), REG_HRO },
	{ FLDATA (CTL, clk_dib.ctl, 0) },
	{ FLDATA (FLG, clk_dib.flg, 0) },
	{ FLDATA (FBF, clk_dib.fbf, 0) },
	{ FLDATA (ERR, clk_error, CLK_V_ERROR) },
	{ BRDATA (TIME, clk_time, 10, 24, 8) },
	{ ORDATA (DEVNO, clk_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB clk_mod[] = {
	{ UNIT_DIAG, UNIT_DIAG, "diagnostic mode", "DIAG", NULL },
	{ UNIT_DIAG, 0, "calibrated", "CALIBRATED", NULL },
	{ MTAB_XTD | MTAB_VDV, 0, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &clk_dev },
	{ 0 }  };

DEVICE clk_dev = {
	"CLK", &clk_unit, clk_reg, clk_mod,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &clk_reset,
	NULL, NULL, NULL,
	&clk_dib, 0 };

/* Paper tape reader: IOT routine */

int32 ptrio (int32 inst, int32 IR, int32 dat)
{
int32 dev;

dev = IR & I_DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & I_HC) == 0) { setFLG (dev); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (dev) == 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (dev) != 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioMIX:						/* merge */
	dat = dat | ptr_unit.buf;
	break;
case ioLIX:						/* load */
	dat = ptr_unit.buf;
	break;
case ioCTL:						/* control clear/set */
	if (IR & I_CTL) {				/* CLC */
	    clrCMD (dev);				/* clear cmd, ctl */
	    clrCTL (dev); }
	else {						/* STC */
	    setCMD (dev);				/* set cmd, ctl */
	    setCTL (dev);
	    sim_activate (&ptr_unit, ptr_unit.wait);  }
	break;
default:
	break;  }
if (IR & I_HC) { clrFLG (dev); }			/* H/C option */
return dat;
}

/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32 dev, temp;

dev = ptr_dib.devno;					/* get device no */
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

/* Reset routine - called from SCP, flags in DIB's */

t_stat ptr_reset (DEVICE *dptr)
{
ptr_dib.cmd = ptr_dib.ctl = 0;				/* clear cmd, ctl */
ptr_dib.flg = ptr_dib.fbf = 1;				/* set flg, fbf */
ptr_unit.buf = 0;
sim_cancel (&ptr_unit);					/* deactivate unit */
return SCPE_OK;
}

/* Paper tape reader bootstrap routine (HP 12992K ROM) */

#define LDR_BASE	077
#define CHANGE_DEV	(1 << 24)

static const int32 pboot[IBL_LNT] = {
	0107700,		/*ST CLC 0,C		; intr off */
	0002401,		/*   CLA,RSS		; skip in */		
	0063756,		/*CN LDA M11		; feed frame */
	0006700,		/*   CLB,CCE		; set E to rd byte */
	0017742,		/*   JSB READ		; get #char */
	0007306,		/*   CMB,CCE,INB,SZB	; 2's comp */
	0027713,		/*   JMP *+5		; non-zero byte */
	0002006,		/*   INA,SZA		; feed frame ctr */
	0027703,		/*   JMP *-3 */
	0102077,		/*   HLT 77B		; stop */
	0027700,		/*   JMP ST		; next */
	0077754,		/*   STA WC		; word in rec */
	0017742,		/*   JSB READ		; get feed frame */
	0017742,		/*   JSB READ		; get address */
	0074000,		/*   STB 0		; init csum */
	0077755,		/*   STB AD		; save addr */
	0067755,		/*CK LDB AD		; check addr */
	0047777,		/*   ADB MAXAD		; below loader */
	0002040,		/*   SEZ		; E =0 => OK */
	0027740,		/*   JMP H55 */
	0017742,		/*   JSB READ		; get word */
	0040001,		/*   ADA 1		; cont checksum */
	0177755,		/*   STA AD,I		; store word */
	0037755,		/*   ISZ AD */
	0000040,		/*   CLE		; force wd read */
	0037754,		/*   ISZ WC		; block done? */
	0027720,		/*   JMP CK		; no */
	0017742,		/*   JSB READ		; get checksum */
	0054000,		/*   CPB 0		; ok? */
	0027702,		/*   JMP CN		; next block */
	0102011,		/*   HLT 11		; bad csum */
	0027700,		/*   JMP ST		; next */
	0102055,		/*H55 HALT 55		; bad address */
	0027700,		/*   JMP ST		; next */
	0000000,		/*RD 0 */
	0006600,		/*   CLB,CME		; E reg byte ptr */
	0103700+CHANGE_DEV,	/*   STC RDR,C		; start reader */
	0102300+CHANGE_DEV,	/*   SFS RDR		; wait */
	0027745,		/*   JMP *-1 */
	0106400+CHANGE_DEV,	/*   MIB RDR		; get byte */
	0002041,		/*   SEZ,RSS		; E set? */
	0127742,		/*   JMP RD,I		; no, done */
	0005767,		/*   BLF,CLE,BLF	; shift byte */
	0027744,		/*   JMP RD+2		; again */
	0000000,		/*WC 000000		; word count */
	0000000,		/*AD 000000		; address */
	0177765,		/*M11 -11		; feed count */
	0, 0, 0, 0, 0, 0, 0, 0,	/* unused */
	0, 0, 0, 0, 0, 0, 0,	/* unused */
	0000000  };		/*MAXAD -ST		; max addr */

t_stat ptr_boot (int32 unitno, DEVICE *dptr)
{
int32 i, dev;

dev = ptr_dib.devno;					/* get device no */
PC = ((MEMSIZE - 1) & ~IBL_MASK) & VAMASK;		/* start at mem top */
SR = IBL_PTR + (dev << IBL_V_DEV);			/* set SR */
for (i = 0; i < IBL_LNT; i++) {				/* copy bootstrap */
	if (pboot[i] & CHANGE_DEV)			/* IO instr? */
	    M[PC + i] = (pboot[i] + dev) & DMASK;
	else M[PC + i] = pboot[i];  }
M[PC + LDR_BASE] = (~PC + 1) & DMASK;
return SCPE_OK;
}

/* Paper tape punch: IOT routine */

int32 ptpio (int32 inst, int32 IR, int32 dat)
{
int32 dev;

dev = IR & I_DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & I_HC) == 0) { setFLG (dev); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (dev) == 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (dev) != 0) PC = (PC + 1) & VAMASK;
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
	if (IR & I_CTL) {				/* CLC */
	    clrCMD (dev);				/* clear cmd, ctl */
	    clrCTL (dev); }
	else {						/* STC */
	    setCMD (dev);				/* set cmd, ctl */
	    setCTL (dev);
	    sim_activate (&ptp_unit, ptp_unit.wait);  }
	break;
default:
	break;  }
if (IR & I_HC) { clrFLG (dev); }			/* H/C option */
return dat;
}

/* Unit service */

t_stat ptp_svc (UNIT *uptr)
{
int32 dev;

dev = ptp_dib.devno;					/* get device no */
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
ptp_dib.cmd = ptp_dib.ctl = 0;				/* clear cmd, ctl */
ptp_dib.flg = ptp_dib.fbf = 1;				/* set flg, fbf */
ptp_unit.buf = 0;
sim_cancel (&ptp_unit);					/* deactivate unit */
return SCPE_OK;
}

/* Terminal: IOT routine */

int32 ttyio (int32 inst, int32 IR, int32 dat)
{
int32 dev;

dev = IR & I_DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & I_HC) == 0) { setFLG (dev); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (dev) == 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (dev) != 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioLIX:						/* load */
	dat = 0;
case ioMIX:						/* merge */
	dat = dat | tty_buf;
	if (!(tty_mode & TM_KBD) && sim_is_active (&tty_unit[TTO]))
	    dat = dat | TP_BUSY;
	break;
case ioOTX:						/* output */
	if (dat & TM_MODE) tty_mode = dat & (TM_KBD|TM_PRI|TM_PUN);
	tty_buf = dat & 0377;
	break;
case ioCTL:						/* control clear/set */
	if (IR & I_CTL) { clrCTL (dev); }		/* CLC */
	else {						/* STC */
	    setCTL (dev);
	    if (!(tty_mode & TM_KBD))			/* output? */
		sim_activate (&tty_unit[TTO], tty_unit[TTO].wait);  }
	break;
default:
	break;  }
if (IR & I_HC) { clrFLG (dev); }			/* H/C option */
return dat;
}

/* Unit service routines */

t_stat tto_out (int32 c)
{
t_stat r;

if (tty_mode & TM_PRI) {				/* printing? */
	if (tty_unit[TTO].flags & UNIT_UC) {		/* UC only? */
	    c = c & 0177;
	    if (islower (c)) c = toupper (c);  }
	else c = c & ((tty_unit[TTO].flags & UNIT_8B)? 0377: 0177);
	if (r = sim_putchar_s (c)) return r;		/* output char */
	tty_unit[TTO].pos = tty_unit[TTO].pos + 1;  }
return SCPE_OK;
}

t_stat ttp_out (int32 c)
{
if (tty_mode & TM_PUN) {				/* punching? */
	if ((tty_unit[TTP].flags & UNIT_ATT) == 0)	/* attached? */
	    return IORETURN (ttp_stopioe, SCPE_UNATT);
	if (putc (c, tty_unit[TTP].fileref) == EOF) {	/* output char */
	    perror ("TTP I/O error");
	    clearerr (tty_unit[TTP].fileref);
	    return SCPE_IOERR;  }
	tty_unit[TTP].pos = ftell (tty_unit[TTP].fileref);  }
return SCPE_OK;
}

t_stat tti_svc (UNIT *uptr)
{
int32 c, dev;

dev = tty_dib.devno;					/* get device no */
sim_activate (&tty_unit[TTI], tty_unit[TTI].wait);	/* continue poll */
if ((c = sim_poll_kbd ()) < SCPE_KFLAG) return c;	/* no char or error? */
if (c & SCPE_BREAK) c = 0;				/* break? */
else if (tty_unit[TTI].flags & UNIT_UC) {		/* UC only? */
	c = c & 0177;
	if (islower (c)) c = toupper (c);  }
else c = c & ((tty_unit[TTI].flags & UNIT_8B)? 0377: 0177);
if (tty_mode & TM_KBD) {				/* keyboard enabled? */
	tty_buf = c;					/* put char in buf */
	tty_unit[TTI].pos = tty_unit[TTI].pos + 1;
	setFLG (dev);					/* set flag */
	if (c) {
	    tto_out (c);				/* echo? */
	    return ttp_out (c);  }  }			/* punch? */
return SCPE_OK;
}

t_stat tto_svc (UNIT *uptr)
{
int32 c, dev;
t_stat r;

c = tty_buf;						/* get char */
tty_buf = 0377;						/* defang buf */
if ((r = tto_out (c)) != SCPE_OK) {			/* output; error? */
	sim_activate (uptr, uptr->wait);		/* retry */
	return ((r == SCPE_STALL)? SCPE_OK: r);  }	/* !stall? report */
dev = tty_dib.devno;					/* get device no */
setFLG (dev);						/* set done flag */
return ttp_out (c);					/* punch if enabled */
}

/* Reset routine */

t_stat tty_reset (DEVICE *dptr)
{
tty_dib.cmd = tty_dib.ctl = 0;				/* clear cmd, ctl */
tty_dib.flg = tty_dib.fbf = 1;				/* set flg, fbf */
tty_mode = TM_KBD;					/* enable input */
tty_buf = 0;
sim_activate (&tty_unit[TTI], tty_unit[TTI].wait);	/* activate poll */
sim_cancel (&tty_unit[TTO]);				/* cancel output */
return SCPE_OK;
}

t_stat tty_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 u = uptr - tty_dev.units;

if (u > 1) return SCPE_NOFNC;
tty_unit[TTI].flags = (tty_unit[TTI].flags & ~(UNIT_UC | UNIT_8B)) | val;
tty_unit[TTO].flags = (tty_unit[TTO].flags & ~(UNIT_UC | UNIT_8B)) | val;
return SCPE_OK;
}

/* Clock: IOT routine */

int32 clkio (int32 inst, int32 IR, int32 dat)
{
int32 dev;

dev = IR & I_DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & I_HC) == 0) { setFLG (dev); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (dev) == 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (dev) != 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioMIX:						/* merge */
	dat = dat | clk_error;
	break;
case ioLIX:						/* load */
	dat = clk_error;
	break;
case ioOTX:						/* output */
	clk_select = dat & 07;				/* save select */
	sim_cancel (&clk_unit);				/* stop the clock */
	clrCTL (dev);					/* clear control */
	break;
case ioCTL:						/* control clear/set */
	if (IR & I_CTL) {				/* CLC */
	    clrCTL (dev);				/* turn off clock */
	    sim_cancel (&clk_unit);  }			/* deactivate unit */
	else {						/* STC */
	    setCTL (dev);				/* set CTL */
	    if (!sim_is_active (&clk_unit)) {		/* clock running? */
		sim_activate (&clk_unit,
		     sim_rtc_init (clk_delay (0)));	/* no, start clock */
		clk_ctr = clk_delay (1);  }		/* set repeat ctr */
	    clk_error = 0;  }				/* clear error */
	break;
default:
	break;  }
if (IR & I_HC) { clrFLG (dev); }			/* H/C option */
return dat;
}

/* Unit service */

t_stat clk_svc (UNIT *uptr)
{
int32 tim, dev;

dev = clk_dib.devno;					/* get device no */
if (!CTL (dev)) return SCPE_OK;				/* CTL off? done */
if (clk_unit.flags & UNIT_DIAG)				/* diag mode? */
	tim = clk_delay (0);				/* get fixed delay */
else tim = sim_rtc_calb (clk_tps[clk_select]);		/* calibrate delay */
sim_activate (uptr, tim);				/* reactivate */
clk_ctr = clk_ctr - 1;					/* decrement counter */
if (clk_ctr <= 0) {					/* end of interval? */
	tim = FLG (dev);
	if (FLG (dev)) clk_error = CLK_ERROR;		/* overrun? error */
	else { setFLG (dev); }				/* else set flag */
	clk_ctr = clk_delay (1);  }			/* reset counter */
return SCPE_OK;
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
clk_dib.cmd = clk_dib.ctl = 0;				/* clear cmd, ctl */
clk_dib.flg = clk_dib.fbf = 1;				/* set flg, fbf */
clk_error = 0;						/* clear error */
clk_select = 0;						/* clear select */
clk_ctr = 0;						/* clear counter */
sim_cancel (&clk_unit);					/* deactivate unit */
return SCPE_OK;
}

/* Clock delay routine */

int32 clk_delay (int32 flg)
{
int32 sel = clk_select;

if ((clk_unit.flags & UNIT_DIAG) && (sel >= 4)) sel = sel - 3;
if (flg) return clk_rpt[sel];
else return clk_time[sel];
}
