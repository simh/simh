/* vax_stddev.c: VAX standard I/O devices simulator

   Copyright (c) 1998-2002, Robert M Supnik

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
   tti		terminal input
   tto		terminal output
   clk		line frequency clock

   30-May-02	RMS	Widened POS to 32b
   30-Apr-02	RMS	Automatically set TODR to VMS-correct value during boot
*/

#include "vax_defs.h"
#include <time.h>

#define PTRCSR_IMP	(CSR_ERR+CSR_BUSY+CSR_DONE+CSR_IE) /* paper tape reader */
#define PTRCSR_RW	(CSR_IE)
#define PTPCSR_IMP	(CSR_ERR + CSR_DONE + CSR_IE)	/* paper tape punch */
#define PTPCSR_RW	(CSR_IE)
#define TTICSR_IMP	(CSR_DONE + CSR_IE)		/* terminal input */
#define TTICSR_RW	(CSR_IE)
#define TTOCSR_IMP	(CSR_DONE + CSR_IE)		/* terminal output */
#define TTOCSR_RW	(CSR_IE)
#define CLKCSR_IMP	(CSR_IE)			/* real-time clock */
#define CLKCSR_RW	(CSR_IE)
#define CLK_DELAY	5000				/* 100 Hz */
#define TMXR_MULT	2				/* 50 Hz */

extern int32 int_req[IPL_HLVL];
int32 ptr_csr = 0;					/* control/status */
int32 ptr_stopioe = 0;					/* stop on error */
int32 ptp_csr = 0;					/* control/status */
int32 ptp_stopioe = 0;					/* stop on error */
int32 tti_csr = 0;					/* control/status */
int32 tto_csr = 0;					/* control/status */
int32 clk_csr = 0;					/* control/status */
int32 clk_tps = 100;					/* ticks/second */
int32 todr_reg = 0;					/* TODR register */
int32 todr_blow = 1;					/* TODR battery low */
int32 tmxr_poll = CLK_DELAY * TMXR_MULT;		/* term mux poll */
int32 tmr_poll = CLK_DELAY;				/* pgm timer poll */

t_stat pt_rd (int32 *data, int32 PA, int32 access);
t_stat pt_wr (int32 data, int32 PA, int32 access);
t_stat ptr_svc (UNIT *uptr);
t_stat ptp_svc (UNIT *uptr);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat clk_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_reset (DEVICE *dptr);
t_stat clk_reset (DEVICE *dptr);
t_stat ptr_attach (UNIT *uptr, char *ptr);
t_stat ptr_detach (UNIT *uptr);
t_stat ptp_attach (UNIT *uptr, char *ptr);
t_stat ptp_detach (UNIT *uptr);

/* PTR data structures

   ptr_dev	PTR device descriptor
   ptr_unit	PTR unit descriptor
   ptr_reg	PTR register list
*/

DIB pt_dib = { 1, IOBA_PT, IOLN_PT, &pt_rd, &pt_wr };

UNIT ptr_unit = {
	UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0),
		SERIAL_IN_WAIT };

REG ptr_reg[] = {
	{ HRDATA (BUF, ptr_unit.buf, 8) },
	{ HRDATA (CSR, ptr_csr, 16) },
	{ FLDATA (INT, int_req[IPL_PTR], INT_V_PTR) },
	{ FLDATA (ERR, ptr_csr, CSR_V_ERR) },
	{ FLDATA (BUSY, ptr_csr, CSR_V_BUSY) },
	{ FLDATA (DONE, ptr_csr, CSR_V_DONE) },
	{ FLDATA (IE, ptr_csr, CSR_V_IE) },
	{ DRDATA (POS, ptr_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptr_stopioe, 0) },
	{ NULL }  };

MTAB ptr_mod[] = {
	{ MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,
		NULL, &show_addr, &pt_dib },
	{ MTAB_XTD|MTAB_VDV, 1, NULL, "ENABLED",
		&set_enbdis, NULL, &pt_dib },
	{ MTAB_XTD|MTAB_VDV, 0, NULL, "DISABLED",
		&set_enbdis, NULL, &pt_dib },
	{ 0 }  };

DEVICE ptr_dev = {
	"PTR", &ptr_unit, ptr_reg, ptr_mod,
	1, 10, 31, 1, 16, 8,
	NULL, NULL, &ptr_reset,
	NULL, &ptr_attach, &ptr_detach };

/* PTP data structures

   ptp_dev	PTP device descriptor
   ptp_unit	PTP unit descriptor
   ptp_reg	PTP register list
*/

UNIT ptp_unit = {
	UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG ptp_reg[] = {
	{ HRDATA (BUF, ptp_unit.buf, 8) },
	{ HRDATA (CSR, ptp_csr, 16) },
	{ FLDATA (INT, int_req[IPL_PTP], INT_V_PTP) },
	{ FLDATA (ERR, ptp_csr, CSR_V_ERR) },
	{ FLDATA (DONE, ptp_csr, CSR_V_DONE) },
	{ FLDATA (IE, ptp_csr, CSR_V_IE) },
	{ DRDATA (POS, ptp_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptp_stopioe, 0) },
	{ NULL }  };

MTAB ptp_mod[] = {
	{ MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,
		NULL, &show_addr, &pt_dib },
	{ MTAB_XTD|MTAB_VDV, 1, NULL, "ENABLED",
		&set_enbdis, NULL, &pt_dib },
	{ MTAB_XTD|MTAB_VDV, 0, NULL, "DISABLED",
		&set_enbdis, NULL, &pt_dib },
	{ 0 }  };

DEVICE ptp_dev = {
	"PTP", &ptp_unit, ptp_reg, ptp_mod,
	1, 10, 31, 1, 16, 8,
	NULL, NULL, &ptp_reset,
	NULL, &ptp_attach, &ptp_detach };

/* TTI data structures

   tti_dev	TTI device descriptor
   tti_unit	TTI unit descriptor
   tti_reg	TTI register list
*/

UNIT tti_unit = { UDATA (&tti_svc, 0, 0), KBD_POLL_WAIT };

REG tti_reg[] = {
	{ HRDATA (BUF, tti_unit.buf, 8) },
	{ HRDATA (CSR, tti_csr, 16) },
	{ FLDATA (INT, int_req[IPL_TTI], INT_V_TTI) },
	{ FLDATA (DONE, tti_csr, CSR_V_DONE) },
	{ FLDATA (IE, tti_csr, CSR_V_IE) },
	{ DRDATA (POS, tti_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, tti_unit.wait, 24), REG_NZ + PV_LEFT },
	{ NULL }  };

DEVICE tti_dev = {
	"TTI", &tti_unit, tti_reg, NULL,
	1, 10, 31, 1, 16, 8,
	NULL, NULL, &tti_reset,
	NULL, NULL, NULL };

/* TTO data structures

   tto_dev	TTO device descriptor
   tto_unit	TTO unit descriptor
   tto_reg	TTO register list
*/

UNIT tto_unit = { UDATA (&tto_svc, 0, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
	{ HRDATA (BUF, tto_unit.buf, 8) },
	{ HRDATA (CSR, tto_csr, 16) },
	{ FLDATA (INT, int_req[IPL_TTO], INT_V_TTO) },
	{ FLDATA (DONE, tto_csr, CSR_V_DONE) },
	{ FLDATA (IE, tto_csr, CSR_V_IE) },
	{ DRDATA (POS, tto_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, tto_unit.wait, 24), PV_LEFT },
	{ NULL }  };

DEVICE tto_dev = {
	"TTO", &tto_unit, tto_reg, NULL,
	1, 10, 31, 1, 16, 8,
	NULL, NULL, &tto_reset,
	NULL, NULL, NULL };

/* CLK data structures

   clk_dev	CLK device descriptor
   clk_unit	CLK unit descriptor
   clk_reg	CLK register list
*/

UNIT clk_unit = { UDATA (&clk_svc, 0, 0), CLK_DELAY };

REG clk_reg[] = {
	{ HRDATA (CSR, clk_csr, 16) },
	{ FLDATA (INT, int_req[IPL_CLK], INT_V_CLK) },
	{ FLDATA (IE, clk_csr, CSR_V_IE) },
	{ DRDATA (TODR, todr_reg, 32), PV_LEFT },
	{ FLDATA (BLOW, todr_blow, 0) },
	{ DRDATA (TIME, clk_unit.wait, 24), REG_NZ + PV_LEFT },
	{ DRDATA (TPS, clk_tps, 8), REG_NZ + PV_LEFT },
	{ NULL }  };

DEVICE clk_dev = {
	"CLK", &clk_unit, clk_reg, NULL,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &clk_reset,
	NULL, NULL, NULL };

/* Paper tape I/O dispatch routine, I/O addresses 17777550-17777577

   17777550		ptr CSR
   17777552		ptr buffer
   17777554		ptp CSR
   17777556		ptp buffer

   Note: Word access routines filter out odd addresses.  Thus,
   an odd address implies an (odd) byte access.
*/

t_stat pt_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 03) {				/* decode PA<2:1> */
case 00:						/* ptr csr */
	*data = ptr_csr & PTRCSR_IMP;
	break;
case 01:						/* ptr buf */
	ptr_csr = ptr_csr & ~CSR_DONE;
	CLR_INT (PTR);
	*data = ptr_unit.buf & 0377;
	break;
case 02:						/* ptp csr */
	*data = ptp_csr & PTPCSR_IMP;
	break;
case 03:						/* ptp buf */
	*data = ptp_unit.buf;
	break;  }
return SCPE_OK;
}

t_stat pt_wr (int32 data, int32 PA, int32 access)
{
switch ((PA >> 1) & 03) {				/* decode PA<2:1> */
case 00:						/* ptr csr */
	if (PA & 1) return SCPE_OK;
	if ((data & CSR_IE) == 0) CLR_INT (PTR);
	else if (((ptr_csr & CSR_IE) == 0) && (ptr_csr & (CSR_ERR | CSR_DONE)))
		SET_INT (PTR);
	if (data & CSR_GO) {
		ptr_csr = (ptr_csr & ~CSR_DONE) | CSR_BUSY;
		CLR_INT (PTR);
		if (ptr_unit.flags & UNIT_ATT)		/* data to read? */
			sim_activate (&ptr_unit, ptr_unit.wait);  
		else sim_activate (&ptr_unit, 0);  }	/* error if not */
	ptr_csr = (ptr_csr & ~PTRCSR_RW) | (data & PTRCSR_RW);
	break;
case 01:						/* ptr buf */
	break;
case 02:						/* ptp csr */
	if (PA & 1) return SCPE_OK;
	if ((data & CSR_IE) == 0) CLR_INT (PTP);
	else if (((ptp_csr & CSR_IE) == 0) && (ptp_csr & (CSR_ERR | CSR_DONE)))
		SET_INT (PTP);
	ptp_csr = (ptp_csr & ~PTPCSR_RW) | (data & PTPCSR_RW);
	break;
case 03:						/* ptp buf */
	if ((PA & 1) == 0) ptp_unit.buf = data & 0377;
	ptp_csr = ptp_csr & ~CSR_DONE;
	CLR_INT (PTP);
	if (ptp_unit.flags & UNIT_ATT)			/* file to write? */
		sim_activate (&ptp_unit, ptp_unit.wait);
	else sim_activate (&ptp_unit, 0);		/* error if not */
	break;  }					/* end switch PA */
return SCPE_OK;
}

/* Clock and terminal MxPR routines

   iccs_rd/wr	interval timer
   todr_rd/wr	time of year clock
   rxcs_rd/wr	input control/status
   rxdb_rd	input buffer
   txcs_rd/wr	output control/status
   txdb_wr	output buffer
*/

int32 iccs_rd (void)
{
return (clk_csr & CLKCSR_IMP);
}

int32 todr_rd (void)
{
return todr_reg;
}

int32 rxcs_rd (void)
{
return (tti_csr & TTICSR_IMP);
}

int32 rxdb_rd (void)
{
tti_csr = tti_csr & ~CSR_DONE;
CLR_INT (TTI);
return (tti_unit.buf & 0377);
}

int32 txcs_rd (void)
{
return (tto_csr & TTOCSR_IMP);
}

void iccs_wr (int32 data)
{
if ((data & CSR_IE) == 0) CLR_INT (CLK);
clk_csr = (clk_csr & ~CLKCSR_RW) | (data & CLKCSR_RW);
return;
}

void todr_wr (int32 data)
{
todr_reg = data;
if (data) todr_blow = 0;
return;
}

void rxcs_wr (int32 data)
{
if ((data & CSR_IE) == 0) CLR_INT (TTI);
else if ((tti_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
		SET_INT (TTI);
tti_csr = (tti_csr & ~TTICSR_RW) | (data & TTICSR_RW);
return;
}

void txcs_wr (int32 data)
{
if ((data & CSR_IE) == 0) CLR_INT (TTO);
else if ((tto_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
	SET_INT (TTO);
tto_csr = (tto_csr & ~TTOCSR_RW) | (data & TTOCSR_RW);
return;
}

void txdb_wr (int32 data)
{
tto_unit.buf = data & 0377;
tto_csr = tto_csr & ~CSR_DONE;
CLR_INT (TTO);
sim_activate (&tto_unit, tto_unit.wait);
return;
}

/* Paper tape reader routines

   ptr_svc	process event (character ready)
   ptr_reset	process reset
   ptr_attach	process attach
   ptr_detach	process detach
*/

t_stat ptr_svc (UNIT *uptr)
{
int32 temp;

ptr_csr = (ptr_csr | CSR_ERR) & ~CSR_BUSY;
if (ptr_csr & CSR_IE) SET_INT (PTR);
if ((ptr_unit.flags & UNIT_ATT) == 0)
	return IORETURN (ptr_stopioe, SCPE_UNATT);
if ((temp = getc (ptr_unit.fileref)) == EOF) {
	if (feof (ptr_unit.fileref)) {
		if (ptr_stopioe) printf ("PTR end of file\n");
		else return SCPE_OK;  }
	else perror ("PTR I/O error");
	clearerr (ptr_unit.fileref);
	return SCPE_IOERR;  }
ptr_csr = (ptr_csr | CSR_DONE) & ~CSR_ERR;
ptr_unit.buf = temp & 0377;
ptr_unit.pos = ptr_unit.pos + 1;
return SCPE_OK;
}

t_stat ptr_reset (DEVICE *dptr)
{
ptr_unit.buf = 0;
ptr_csr = 0;
if ((ptr_unit.flags & UNIT_ATT) == 0) ptr_csr = ptr_csr | CSR_ERR;
CLR_INT (PTR);
sim_cancel (&ptr_unit);
return SCPE_OK;
}

t_stat ptr_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
if ((ptr_unit.flags & UNIT_ATT) == 0) ptr_csr = ptr_csr | CSR_ERR;
else ptr_csr = ptr_csr & ~CSR_ERR;
return reason;
}

t_stat ptr_detach (UNIT *uptr)
{
ptr_csr = ptr_csr | CSR_ERR;
return detach_unit (uptr);
}

/* Paper tape punch routines

   ptp_svc	process event (character punched)
   ptp_reset	process reset
   ptp_attach	process attach
   ptp_detach	process detach
*/

t_stat ptp_svc (UNIT *uptr)
{
ptp_csr = ptp_csr | CSR_ERR | CSR_DONE;
if (ptp_csr & CSR_IE) SET_INT (PTP);
if ((ptp_unit.flags & UNIT_ATT) == 0)
	return IORETURN (ptp_stopioe, SCPE_UNATT);
if (putc (ptp_unit.buf, ptp_unit.fileref) == EOF) {
	perror ("PTP I/O error");
	clearerr (ptp_unit.fileref);
	return SCPE_IOERR;  }
ptp_csr = ptp_csr & ~CSR_ERR;
ptp_unit.pos = ptp_unit.pos + 1;
return SCPE_OK;
}

t_stat ptp_reset (DEVICE *dptr)
{
ptp_unit.buf = 0;
ptp_csr = CSR_DONE;
if ((ptp_unit.flags & UNIT_ATT) == 0) ptp_csr = ptp_csr | CSR_ERR;
CLR_INT (PTP);
sim_cancel (&ptp_unit);					/* deactivate unit */
return SCPE_OK;
}

t_stat ptp_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
if ((ptp_unit.flags & UNIT_ATT) == 0) ptp_csr = ptp_csr | CSR_ERR;
else ptp_csr = ptp_csr & ~CSR_ERR;
return reason;
}

t_stat ptp_detach (UNIT *uptr)
{
ptp_csr = ptp_csr | CSR_ERR;
return detach_unit (uptr);
}

/* Terminal input routines

   tti_svc	process event (character ready)
   tti_reset	process reset
*/

t_stat tti_svc (UNIT *uptr)
{
int32 temp;

sim_activate (&tti_unit, tti_unit.wait);		/* continue poll */
if ((temp = sim_poll_kbd ()) < SCPE_KFLAG) return temp;	/* no char or error? */
tti_unit.buf = temp & 0377;
tti_unit.pos = tti_unit.pos + 1;
tti_csr = tti_csr | CSR_DONE;
if (tti_csr & CSR_IE) SET_INT (TTI);
return SCPE_OK;
}

t_stat tti_reset (DEVICE *dptr)
{
tti_unit.buf = 0;
tti_csr = 0;
CLR_INT (TTI);
sim_activate (&tti_unit, tti_unit.wait);		/* activate unit */
return SCPE_OK;
}

/* Terminal output routines

   tto_svc	process event (character typed)
   tto_reset	process reset
*/

t_stat tto_svc (UNIT *uptr)
{
int32 temp;

tto_csr = tto_csr | CSR_DONE;
if (tto_csr & CSR_IE) SET_INT (TTO);
if ((temp = sim_putchar (tto_unit.buf & 0177)) != SCPE_OK) return temp;
tto_unit.pos = tto_unit.pos + 1;
return SCPE_OK;
}

t_stat tto_reset (DEVICE *dptr)
{
tto_unit.buf = 0;
tto_csr = CSR_DONE;
CLR_INT (TTO);
sim_cancel (&tto_unit);					/* deactivate unit */
return SCPE_OK;
}

/* Clock routines

   clk_svc	process event (clock tick)
   clk_reset	process reset
   todr_powerup	powerup for TODR (get date from system)
*/

t_stat clk_svc (UNIT *uptr)
{
int32 t;

if (clk_csr & CSR_IE) SET_INT (CLK);
t = sim_rtcn_calb (clk_tps, TMR_CLK);			/* calibrate clock */
sim_activate (&clk_unit, t);				/* reactivate unit */
tmr_poll = t;						/* set tmr poll */
tmxr_poll = t * TMXR_MULT;				/* set mux poll */
if (!todr_blow) todr_reg = todr_reg + 1;		/* incr TODR */
return SCPE_OK;
}

t_stat clk_reset (DEVICE *dptr)
{
int32 t;

clk_csr = 0;
CLR_INT (CLK);
t = sim_rtcn_init (clk_unit.wait, TMR_CLK);		/* init timer */
sim_activate (&clk_unit, t);				/* activate unit */
tmr_poll = t;						/* set tmr poll */
tmxr_poll = t * TMXR_MULT;				/* set mux poll */
return SCPE_OK;
}

t_stat todr_powerup (void)
{
uint32 base;
time_t curr;
struct tm *ctm;

curr = time (NULL);					/* get curr time */
if (curr == (time_t) -1) return SCPE_NOFNC;		/* error? */
ctm = localtime (&curr);				/* decompose */
if (ctm == NULL) return SCPE_NOFNC;			/* error? */
base = (((((ctm -> tm_yday * 24) +			/* sec since 1-Jan */
	    ctm -> tm_hour) * 60) +
	    ctm -> tm_min) * 60) +
	    ctm -> tm_sec;
todr_reg = (base * 100) + 0x10000000;			/* cvt to VAX form */
todr_blow = 0;
return SCPE_OK;
}
