/* pdp18b_stddev.c: 18b PDP's standard devices

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

   ptr		paper tape reader
   ptp		paper tape punch
   tti		keyboard
   tto		teleprinter
   clk		clock

   10-Jun-01	RMS	Cleaned up IOT decoding to reflect hardware
   27-May-01	RMS	Added multiconsole support
   10-Mar-01	RMS	Added funny format loader support
   05-Mar-01	RMS	Added clock calibration support
   22-Dec-00	RMS	Added PDP-9/15 half duplex support
   30-Nov-00	RMS	Fixed PDP-4/7 bootstrap loader for 4K systems
   30-Oct-00	RMS	Standardized register naming
   06-Jan-97	RMS	Fixed PDP-4 console input
   16-Dec-96	RMS	Fixed bug in binary ptr service
*/

#include "pdp18b_defs.h"
#include <ctype.h>

extern int32 M[];
extern int32 int_req, saved_PC;
extern UNIT cpu_unit;
int32 clk_state = 0;
int32 ptr_err = 0, ptr_stopioe = 0, ptr_state = 0;
int32 ptp_err = 0, ptp_stopioe = 0;
int32 tti_state = 0;
int32 tto_state = 0;
int32 clk_tps = 60;
#if defined (TTY1)
static uint8 tto_consout[CONS_SIZE];
#endif

t_stat clk_svc (UNIT *uptr);
t_stat ptr_svc (UNIT *uptr);
t_stat ptp_svc (UNIT *uptr);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat clk_reset (DEVICE *dptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_reset (DEVICE *dptr);
t_stat ptr_attach (UNIT *uptr, char *cptr);
t_stat ptp_attach (UNIT *uptr, char *cptr);
t_stat ptr_detach (UNIT *uptr);
t_stat ptp_detach (UNIT *uptr);
t_stat ptr_boot (int32 unitno);
extern int32 sim_rtc_init (int32 time);
extern int32 sim_rtc_calb (int32 tps);
extern t_stat sim_poll_kbd (void);
extern t_stat sim_putchar (int32 out);

/* CLK data structures

   clk_dev	CLK device descriptor
   clk_unit	CLK unit
   clk_reg	CLK register list
*/

UNIT clk_unit = { UDATA (&clk_svc, 0, 0), 16000 };

REG clk_reg[] = {
	{ FLDATA (INT, int_req, INT_V_CLK) },
	{ FLDATA (DONE, int_req, INT_V_CLK) },
	{ FLDATA (ENABLE, clk_state, 0) },
	{ DRDATA (TIME, clk_unit.wait, 24), REG_NZ + PV_LEFT },
	{ DRDATA (TPS, clk_tps, 8), REG_NZ + PV_LEFT },
	{ NULL }  };

DEVICE clk_dev = {
	"CLK", &clk_unit, clk_reg, NULL,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &clk_reset,
	NULL, NULL, NULL };

/* PTR data structures

   ptr_dev	PTR device descriptor
   ptr_unit	PTR unit
   ptr_reg	PTR register list
*/

UNIT ptr_unit = {
	UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_IN_WAIT };

REG ptr_reg[] = {
	{ ORDATA (BUF, ptr_unit.buf, 18) },
	{ FLDATA (INT, int_req, INT_V_PTR) },
	{ FLDATA (DONE, int_req, INT_V_PTR) },
#if defined (IOS_PTRERR)
	{ FLDATA (ERR, ptr_err, 0) },
#endif
	{ ORDATA (STATE, ptr_state, 5), REG_HRO },
	{ DRDATA (POS, ptr_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptr_stopioe, 0) },
	{ NULL }  };

DEVICE ptr_dev = {
	"PTR", &ptr_unit, ptr_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptr_reset,
	&ptr_boot, &ptr_attach, &ptr_detach };

/* PTP data structures

   ptp_dev	PTP device descriptor
   ptp_unit	PTP unit
   ptp_reg	PTP register list
*/

UNIT ptp_unit = {
	UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG ptp_reg[] = {
	{ ORDATA (BUF, ptp_unit.buf, 8) },
	{ FLDATA (INT, int_req, INT_V_PTP) },
	{ FLDATA (DONE, int_req, INT_V_PTP) },
#if defined (IOS_PTPERR)
	{ FLDATA (ERR, ptp_err, 0) },
#endif
	{ DRDATA (POS, ptp_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptp_stopioe, 0) },
	{ NULL }  };

DEVICE ptp_dev = {
	"PTP", &ptp_unit, ptp_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptp_reset,
	NULL, &ptp_attach, &ptp_detach };

/* TTI data structures

   tti_dev	TTI device descriptor
   tti_unit	TTI unit
   tti_reg	TTI register list
   tti_trans	ASCII to Baudot table
*/

#if defined (KSR28)
#define TTI_WIDTH	5
#define TTI_FIGURES	(1 << TTI_WIDTH)
#define TTI_2ND		(1 << (TTI_WIDTH + 1))
#define TTI_BOTH	(1 << (TTI_WIDTH + 2))
#define BAUDOT_LETTERS	033
#define BAUDOT_FIGURES	037

static const int32 tti_trans[128] = {
	000,000,000,000,000,000,000,064,		/* bell */
	000,000,0210,000,000,0202,000,000,		/* lf, cr */
	000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,
	0204,066,061,045,062,000,053,072,		/* space - ' */
	076,051,000,000,046,070,047,067,		/* ( - / */
	055,075,071,060,052,041,065,074,		/* 0 - 7 */
	054,043,056,057,000,000,000,063,		/* 8 - ? */
	000,030,023,016,022,020,026,013,		/* @ - G */
	005,014,032,036,011,007,006,003,		/* H - O */
	015,035,012,024,001,034,017,031,		/* P - W */
	027,025,021,000,000,000,000,000,		/* X - _ */
	000,030,023,016,022,020,026,013,		/* ` - g */
	005,014,032,036,011,007,006,003,		/* h - o */
	015,035,012,024,001,034,017,031,		/* p - w */
	027,025,021,000,000,000,000,000 };		/* x - DEL */
#else

#define TTI_WIDTH	8
#endif

#define TTI_MASK	((1 << TTI_WIDTH) - 1)
#define UNIT_V_UC	(UNIT_V_UF + 0)			/* UC only */
#define UNIT_UC		(1 << UNIT_V_UC)
#define UNIT_V_HDX	(UNIT_V_UF + 1)			/* half duplex */
#define UNIT_HDX	(1 << UNIT_V_HDX)

#if defined (PDP4) || defined (PDP7)
UNIT tti_unit = { UDATA (&tti_svc, UNIT_UC+UNIT_CONS, 0), KBD_POLL_WAIT };
#else
UNIT tti_unit = { UDATA (&tti_svc, UNIT_UC+UNIT_HDX+UNIT_CONS, 0), KBD_POLL_WAIT };
#endif

REG tti_reg[] = {
	{ ORDATA (BUF, tti_unit.buf, TTI_WIDTH) },
	{ FLDATA (INT, int_req, INT_V_TTI) },
	{ FLDATA (DONE, int_req, INT_V_TTI) },
#if defined (KSR28)
	{ ORDATA (TTI_STATE, tti_state, (TTI_WIDTH + 3)), REG_HRO },
#else
	{ FLDATA (UC, tti_unit.flags, UNIT_V_UC), REG_HRO },
	{ FLDATA (HDX, tti_unit.flags, UNIT_V_HDX), REG_HRO },
#endif
	{ DRDATA (POS, tti_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, tti_unit.wait, 24), REG_NZ + PV_LEFT },
#if defined (TTY1)
	{ FLDATA (CFLAG, tti_unit.flags, UNIT_V_CONS), REG_HRO },
#endif
	{ NULL }  };

MTAB tti_mod[] = {
#if defined (TTY1)
	{ UNIT_CONS, 0, "inactive", NULL, NULL },
	{ UNIT_CONS, UNIT_CONS, "active console", "CONSOLE", &set_console },
#endif
#if !defined (KSR28)
	{ UNIT_UC, 0, "lower case", "LC", NULL },
	{ UNIT_UC, UNIT_UC, "upper case", "UC", NULL },
	{ UNIT_HDX, 0, "full duplex", "FDX", NULL },
	{ UNIT_HDX, UNIT_HDX, "half duplex", "HDX", NULL },
#endif
	{ 0 }  };

DEVICE tti_dev = {
	"TTI", &tti_unit, tti_reg, tti_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tti_reset,
	NULL, NULL, NULL };

/* TTO data structures

   tto_dev	TTO device descriptor
   tto_unit	TTO unit
   tto_reg	TTO register list
   tto_trans	Baudot to ASCII table
*/

#if defined (KSR28)
#define TTO_WIDTH	5
#define TTO_FIGURES	(1 << TTO_WIDTH)

static const char tto_trans[64] = {
	 0 ,'T',015,'O',' ','H','N','M',
	012,'L','R','G','I','P','C','V',
	'E','Z','D','B','S','Y','F','X',
	'A','W','J', 0 ,'U','Q','K', 0,
	 0 ,'5','\r','9',' ','#',',','.',
	012,')','4','&','8','0',':',';',
	'3','"','$','?','\a','6','!','/',
	'-','2','\'',0 ,'7','1','(', 0 };
#else

#define TTO_WIDTH	8
#endif

#define TTO_MASK	((1 << TTO_WIDTH) - 1)

UNIT tto_unit = { UDATA (&tto_svc, UNIT_UC+UNIT_CONS, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
	{ ORDATA (BUF, tto_unit.buf, TTO_WIDTH) },
	{ FLDATA (INT, int_req, INT_V_TTO) },
	{ FLDATA (DONE, int_req, INT_V_TTO) },
#if defined (KSR28)
	{ FLDATA (TTO_STATE, tto_state, 0), REG_HRO },
#endif
	{ DRDATA (POS, tto_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, tto_unit.wait, 24), PV_LEFT },
#if defined (TTY1)
	{ BRDATA (CONSOUT, tto_consout, 8, 8, CONS_SIZE), REG_HIDDEN },
	{ FLDATA (CFLAG, tto_unit.flags, UNIT_V_CONS), REG_HRO },
#endif
	{ NULL }  };

MTAB tto_mod[] = {
#if defined (TTY1)
	{ UNIT_CONS, 0, "inactive", NULL, NULL },
	{ UNIT_CONS, UNIT_CONS, "active console", "CONSOLE", &set_console },
#endif
#if !defined (KSR28)
	{ UNIT_UC, 0, "lower case", "LC", NULL },
	{ UNIT_UC, UNIT_UC, "upper case", "UC", NULL },
#endif
	{ 0 }  };

DEVICE tto_dev = {
	"TTO", &tto_unit, tto_reg, tto_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tto_reset,
	NULL, NULL, NULL };

/* Clock: IOT routine */

int32 clk (int32 pulse, int32 AC)
{
if (pulse & 001) {					/* CLSF */
	if (int_req & INT_CLK) AC = AC | IOT_SKP;  }
if (pulse & 004) {					/* CLON/CLOF */
	if (pulse & 040) {				/* CLON */
		int_req = int_req & ~INT_CLK;		/* clear flag */
		clk_state = 1;				/* clock on */
		if (!sim_is_active (&clk_unit))		/* already on? */
			sim_activate (&clk_unit,	/* start, calibr */
			    sim_rtc_init (clk_unit.wait));  }
	else clk_reset (&clk_dev);  }			/* CLOF */
return AC;
}

/* Unit service */

t_stat clk_svc (UNIT *uptr)
{
if (clk_state) {					/* clock on? */
	M[7] = (M[7] + 1) & 0777777;			/* incr counter */
	if (M[7] == 0) int_req = int_req | INT_CLK;	/* ovrflo? set flag */
	sim_activate (&clk_unit,			/* reactivate unit */
		sim_rtc_calb (clk_tps));  }		/* calibr delay */
return SCPE_OK;
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
int_req = int_req & ~INT_CLK;				/* clear flag */
clk_state = 0;						/* clock off */
sim_cancel (&clk_unit);					/* stop clock */
return SCPE_OK;
}

/* IORS service for all standard devices */

int32 std_iors (void)
{
return	((int_req & INT_CLK)? IOS_CLK: 0) |
	((int_req & INT_PTR)? IOS_PTR: 0) |
	((int_req & INT_PTP)? IOS_PTP: 0) |
	((int_req & INT_TTI)? IOS_TTI: 0) |
	((int_req & INT_TTO)? IOS_TTO: 0) |
#if defined (IOS_PTRERR)
	(ptr_err? IOS_PTRERR: 0) |
#endif
#if defined (IOS_PTPERR)
	(ptp_err? IOS_PTPERR: 0) |
#endif
	(clk_state? IOS_CLKON: 0);
}

/* Paper tape reader: IOT routine */

int32 ptr (int32 pulse, int32 AC)
{
if (pulse & 001) {					/* RSF */
	if (int_req & INT_PTR) AC = AC | IOT_SKP;  }
if (pulse & 002) {					/* RRB, RCF */
	int_req = int_req & ~INT_PTR;			/* clear done */
	AC = AC | ptr_unit.buf;  }			/* return buffer */
if (pulse & 004) {					/* RSA, RSB */
	ptr_state = (pulse & 040)? 18: 0;		/* set mode */
	int_req = int_req & ~INT_PTR;			/* clear done */
	ptr_unit.buf = 0;				/* clear buffer */
	sim_activate (&ptr_unit, ptr_unit.wait);  }
return AC;
}

/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32 temp;

if ((ptr_unit.flags & UNIT_ATT) == 0) {			/* attached? */
#if defined (IOS_PTRERR)
	int_req = int_req | INT_PTR;			/* if err, set int */
	ptr_err = 1;
#endif
	return IORETURN (ptr_stopioe, SCPE_UNATT);  }
if ((temp = getc (ptr_unit.fileref)) == EOF) {		/* end of file? */
#if defined (IOS_PTRERR)
	int_req = int_req | INT_PTR;			/* if err, set done */
	ptr_err = 1;
#endif
	if (feof (ptr_unit.fileref)) {
		if (ptr_stopioe) printf ("PTR end of file\n");
		else return SCPE_OK;  }
	else perror ("PTR I/O error");
	clearerr (ptr_unit.fileref);
	return SCPE_IOERR;  }
if (ptr_state == 0) ptr_unit.buf = temp & 0377;		/* alpha */
else if (temp & 0200) {					/* binary */
	ptr_state = ptr_state - 6;
	ptr_unit.buf = ptr_unit.buf | ((temp & 077) << ptr_state);  }
if (ptr_state == 0) int_req = int_req | INT_PTR;	/* if done, set flag */
else sim_activate (&ptr_unit, ptr_unit.wait);		/* else restart */
ptr_unit.pos = ptr_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat ptr_reset (DEVICE *dptr)
{
ptr_state = 0;						/* clear state */
ptr_unit.buf = 0;
int_req = int_req & ~INT_PTR;				/* clear flag */
ptr_err = (ptr_unit.flags & UNIT_ATT)? 0: 1;
sim_cancel (&ptr_unit);					/* deactivate unit */
return SCPE_OK;
}

/* Attach routine */

t_stat ptr_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
ptr_err = (ptr_unit.flags & UNIT_ATT)? 0: 1;
return reason;
}

/* Detach routine */

t_stat ptr_detach (UNIT *uptr)
{
ptr_err = 1;
return detach_unit (uptr);
}

#if defined (PDP4) || defined (PDP7)

/* Bootstrap routine, PDP-4 and PDP-7

   In a 4K system, the boostrap resides at 7762-7776.
   In an 8K or greater system, the bootstrap resides at 17762-17776.
   Because the program is so small, simple masking can be
   used to remove addr<5> for a 4K system.
 */

#define BOOT_START	017577
#define BOOT_FPC	017577
#define BOOT_RPC	017770
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int))

static const int32 boot_rom[] = {
	0700144,					/* rsb */
	0117762,					/* ff,	jsb r1b */
	0057666,					/*	dac done 1 */
	0117762,					/*	jms r1b */
	0057667,					/*	dac done 2 */
	0117762,					/*	jms r1b */
	0040007,					/*	dac conend */
	0057731,					/*	dac conbeg */
	0440007,					/*	isz conend */
	0117762,					/* blk,	jms r1b */
	0057673,					/*	dac cai */
	0741100,					/*	spa */
	0617665,					/*	jmp done */
	0117762,					/*	jms r1b */
	0057777,					/*	dac tem1 */
	0317673,					/*	add cai */
	0057775,					/*	dac cks */
	0117713,					/*	jms r1a */
	0140010,					/*	dzm word */
	0457777,					/* cont, isz tem1 */
	0617632,					/*	jmp cont1 */
	0217775,					/*	lac cks */
	0740001,					/*	cma */
	0740200,					/*	sza */
	0740040,					/*	hlt */
	0700144,					/*	rsb */
	0617610,					/*	jmp blk */
	0117713,					/* cont1, jms r1a */
	0057762,					/*	dac tem2 */
	0117713,					/*	jms r1a */
	0742010,					/*	rtl */
	0742010,					/*	rtl */
	0742010,					/*	rtl */
	0742010,					/*	rtl */
	0317762,					/*	add tem2 */
	0057762,					/*	dac tem2 */
	0117713,					/*	jms r1a */
        0742020,					/*	rtr */
        0317726,					/*	add cdsp */
        0057713,					/*	dac r1a */
        0517701,					/*	and ccma */
        0740020,					/*	rar */
        0317762,					/*	add tem2 */
        0437713,					/*	xct i r1a */
        0617622,					/*	jmp cont */
        0617672,					/* dsptch, jmp code0 */
        0617670,					/*	jmp code1 */
        0617700,					/*	jmp code2 */
        0617706,					/*	jmp code3 */
        0417711,					/*	xct code4 */
        0617732,					/*	jmp const */
        0740000,					/*	nop */
        0740000,					/*	nop */
        0740000,					/*	nop */
        0200007,					/* done, lac conend */
        0740040,					/*	xx */
        0740040,					/*	xx */
        0517727,					/* code1, and imsk */
        0337762,					/*	add i tem2 */
        0300010,					/* code0, add word */
        0740040,					/* cai,	xx */
        0750001,					/*	clc */
        0357673,					/*	tad cai */
        0057673,					/*	dac cai */
        0617621,					/*	jmp cont-1 */
        0711101,					/* code2, spa cla */
        0740001,					/* ccma, cma */
        0277762,					/*	xor i tem2 */
        0300010,					/*	add word */
        0040010,					/* code2a, dac word */
        0617622,					/* jmp cont */
        0057711,					/* code3, dac code4 */
        0217673,					/*	lac cai */
        0357701,					/*	tad ccma */
        0740040,					/* code4, xx */
        0617622,					/*	jmp cont */
        0000000,					/* r1a,	0 */
        0700101,					/*	rsf */
        0617714,					/*	jmp .-1 */
        0700112,					/*	rrb */
        0700104,					/*	rsa */
        0057730,					/*	dac tem */
        0317775,					/*	add cks */
        0057775,					/*	dac cks */
        0217730,					/*	lac tem */
        0744000,					/*	cll */
        0637713,					/*	jmp i r1a */
        0017654,					/* cdsp, dsptch */
        0760000,					/* imsk, 760000 */
        0000000,					/* tem,	0 */
        0000000,					/* conbeg, 0 */
        0300010,					/* const, add word */
        0060007,					/*	dac i conend */
        0217731,					/*	lac conbeg */
        0040010,					/*	dac index */
        0220007,					/*	lac i conend */
        0560010,					/* con1, sad i index */
        0617752,					/*	jmp find */
        0560010,					/*	sad i index */
        0617752,					/*	jmp find */
        0560010,					/*	sad i index */
        0617752,					/*	jmp find */
        0560010,					/*	sad i index */
        0617752,					/*	jmp find */
        0560010,					/*	sad i index */
        0617752,					/*	jmp find */
	0617737,					/*	jmp con1 */
        0200010,					/* find, lac index */
        0540007,					/*	sad conend */
        0440007,					/*	isz conend */
        0617704,					/*	jmp code2a */
	0000000,
	0000000,
	0000000,
	0000000,
	0000000,					/* r1b,	0 */
	0700101,					/*	rsf */
	0617763,					/*	jmp .-1 */
	0700112,					/*	rrb */
	0700144,					/*	rsb */
	0637762,					/*	jmp i r1b */
	0700144,					/* go,	rsb */
	0117762,					/* g,	jms r1b */
	0057775,					/*	dac cks */
	0417775,					/*	xct cks */
	0117762,					/*	jms r1b */
	0000000,					/* cks,	0 */
	0617771						/*	jmp g */
};

t_stat ptr_boot (int32 unitno)
{
int32 i, mask, wd;
extern int32 sim_switches;

if (MEMSIZE < 8192) mask = 0767777;			/* 4k? */
else mask = 0777777;
for (i = 0; i < BOOT_LEN; i++) {
	wd = boot_rom[i];
	if ((wd >= 0040000) && (wd < 0640000)) wd = wd & mask;
	M[(BOOT_START & mask) + i] = wd;  }
saved_PC = ((sim_switches & SWMASK ('F'))? BOOT_FPC: BOOT_RPC) & mask;
return SCPE_OK;
}

#else

/* PDP-9 and PDP-15 have built-in hardware RIM loaders */

t_stat ptr_boot (int32 unitno)
{
return SCPE_ARG;
}

#endif

/* Paper tape punch: IOT routine */

int32 ptp (int32 pulse, int32 AC)
{
if (pulse & 001) {					/* PSF */
	if (int_req & INT_PTP) AC = AC | IOT_SKP;  }
if (pulse & 002) int_req = int_req & ~INT_PTP;		/* PCF */
if (pulse & 004) {					/* PSA, PSB, PLS */
	int_req = int_req & ~INT_PTP;			/* clear flag */
	ptp_unit.buf = (pulse & 040)?			/* load punch buf */
		(AC & 077) | 0200: AC & 0377;		/* bin or alpha */
	sim_activate (&ptp_unit, ptp_unit.wait);  }	/* activate unit */
return AC;
}

/* Unit service */

t_stat ptp_svc (UNIT *uptr)
{
int_req = int_req | INT_PTP;				/* set done flag */
if ((ptp_unit.flags & UNIT_ATT) == 0) {			/* not attached? */
	ptp_err = 1;					/* set error */
	return IORETURN (ptp_stopioe, SCPE_UNATT);  }
if (putc (ptp_unit.buf, ptp_unit.fileref) == EOF) {	/* I/O error? */
	ptp_err = 1;					/* set error */
	perror ("PTP I/O error");
	clearerr (ptp_unit.fileref);
	return SCPE_IOERR;  }
ptp_unit.pos = ptp_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
ptp_unit.buf = 0;
int_req = int_req & ~INT_PTP;				/* clear flag */
ptp_err = (ptp_unit.flags & UNIT_ATT)? 0: 1;
sim_cancel (&ptp_unit);					/* deactivate unit */
return SCPE_OK;
}

/* Attach routine */

t_stat ptp_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
ptp_err = (ptp_unit.flags & UNIT_ATT)? 0: 1;
return reason;
}

/* Detach routine */

t_stat ptp_detach (UNIT *uptr)
{
ptp_err = 1;
return detach_unit (uptr);
}

/* Terminal input: IOT routine */

int32 tti (int32 pulse, int32 AC)
{
if (pulse & 001) {					/* KSF */
	if (int_req & INT_TTI) AC = AC | IOT_SKP;  }
if (pulse & 002) {					/* KRB */
	int_req = int_req & ~INT_TTI;			/* clear flag */
	AC = AC | tti_unit.buf & TTI_MASK;  }		/* return buffer */
return AC;
}

/* Unit service */

t_stat tti_svc (UNIT *uptr)
{
int32 temp;

sim_activate (&tti_unit, tti_unit.wait);		/* continue poll */

#if defined (KSR28)					/* Baudot... */
if (tti_state & TTI_2ND) {				/* char waiting? */
	tti_unit.buf = tti_state & TTI_MASK;		/* return char */
	tti_state = tti_state & ~TTI_2ND;  }		/* not waiting */
else {	if ((temp = sim_poll_kbd ()) < SCPE_KFLAG) return temp;
	temp = tti_trans[temp & 0177];			/* translate char */
	if (temp == 0) return SCPE_OK;			/* untranslatable? */
	if (((temp & TTI_FIGURES) == (tti_state & TTI_FIGURES)) ||
	    (temp & TTI_BOTH)) tti_unit.buf = temp & TTI_MASK;
	else {	tti_unit.buf = (temp & TTI_FIGURES)?
				BAUDOT_FIGURES: BAUDOT_LETTERS;
		tti_state = temp | TTI_2ND;  }  }	/* set 2nd waiting */
#else							/* ASCII... */
if ((temp = sim_poll_kbd ()) < SCPE_KFLAG) return temp;	/* no char or error? */
temp = temp & 0177;
if ((tti_unit.flags & UNIT_UC) && islower (temp)) temp = toupper (temp);
if ((tti_unit.flags & UNIT_HDX) &&
    (!(tto_unit.flags & UNIT_UC) ||
	  ((temp >= 007) && (temp <= 0137)))) {
	sim_putcons (temp, uptr);
	tto_unit.pos = tto_unit.pos + 1;  }
tti_unit.buf = temp | 0200;				/* got char */
#endif
int_req = int_req | INT_TTI;				/* set flag */
tti_unit.pos = tti_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat tti_reset (DEVICE *dptr)
{
tti_unit.buf = 0;					/* clear buffer */
tti_state = 0;						/* clear state */
int_req = int_req & ~INT_TTI;				/* clear flag */
#if defined (TTY1)
if (tti_unit.flags & UNIT_CONS)				/* if active cons */
#endif
sim_activate (&tti_unit, tti_unit.wait);		/* activate unit */
return SCPE_OK;
}

/* Terminal output: IOT routine */

int32 tto (int32 pulse, int32 AC)
{
if (pulse & 001) {					/* TSF */
	if (int_req & INT_TTO) AC = AC | IOT_SKP;  }
if (pulse & 002) int_req = int_req & ~INT_TTO;		/* clear flag */
if (pulse & 004) {					/* load buffer */
	sim_activate (&tto_unit, tto_unit.wait);	/* activate unit */
	tto_unit.buf = AC & TTO_MASK;  }		/* load buffer */
return AC;
}

/* Unit service */

t_stat tto_svc (UNIT *uptr)
{
int32 out, temp;

int_req = int_req | INT_TTO;				/* set flag */
#if defined (KSR28)					/* Baudot... */
if (tto_unit.buf == BAUDOT_FIGURES) {			/* set figures? */
	tto_state = TTO_FIGURES;
	return SCPE_OK;  }
if (tto_unit.buf == BAUDOT_LETTERS) {			/* set letters? */
	tto_state = 0;
	return SCPE_OK;  }
out = tto_trans[tto_unit.buf + tto_state];		/* translate */
#else
out = tto_unit.buf & 0177;				/* ASCII... */
#endif
if (!(tto_unit.flags & UNIT_UC) ||
	 ((out >= 007) && (out <= 0137))) {
	temp = sim_putcons (out, uptr);
	if (temp != SCPE_OK) return temp;
	tto_unit.pos = tto_unit.pos + 1;  }
return SCPE_OK;
}

/* Reset routine */

t_stat tto_reset (DEVICE *dptr)
{
tto_unit.buf = 0;					/* clear buffer */
tto_state = 0;						/* clear state */
int_req = int_req & ~INT_TTO;				/* clear flag */
sim_cancel (&tto_unit);					/* deactivate unit */
#if defined (TTY1)
tto_unit.filebuf = tto_consout;
#endif
return SCPE_OK;
}
