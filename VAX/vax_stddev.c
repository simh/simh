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

   tti		terminal input
   tto		terminal output
   clk		100Hz and TODR clock

   01-Nov-02	RMS	Added 7B/8B capability to terminal
   12-Sep-02	RMS	Removed paper tape, added variable vector support
   30-May-02	RMS	Widened POS to 32b
   30-Apr-02	RMS	Automatically set TODR to VMS-correct value during boot
*/

#include "vax_defs.h"
#include <time.h>

#define TTICSR_IMP	(CSR_DONE + CSR_IE)		/* terminal input */
#define TTICSR_RW	(CSR_IE)
#define TTOCSR_IMP	(CSR_DONE + CSR_IE)		/* terminal output */
#define TTOCSR_RW	(CSR_IE)
#define CLKCSR_IMP	(CSR_IE)			/* real-time clock */
#define CLKCSR_RW	(CSR_IE)
#define CLK_DELAY	5000				/* 100 Hz */
#define TMXR_MULT	2				/* 50 Hz */

#define UNIT_V_8B	(UNIT_V_UF + 0)			/* 8B mode */
#define UNIT_8B		(1 << UNIT_V_8B)

extern int32 int_req[IPL_HLVL];

int32 tti_csr = 0;					/* control/status */
int32 tto_csr = 0;					/* control/status */
int32 clk_csr = 0;					/* control/status */
int32 clk_tps = 100;					/* ticks/second */
int32 todr_reg = 0;					/* TODR register */
int32 todr_blow = 1;					/* TODR battery low */
int32 tmxr_poll = CLK_DELAY * TMXR_MULT;		/* term mux poll */
int32 tmr_poll = CLK_DELAY;				/* pgm timer poll */

t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat clk_svc (UNIT *uptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_reset (DEVICE *dptr);
t_stat clk_reset (DEVICE *dptr);

/* TTI data structures

   tti_dev	TTI device descriptor
   tti_unit	TTI unit descriptor
   tti_reg	TTI register list
*/

DIB tti_dib = { 0, 0, NULL, NULL, 1, IVCL (TTI), SCB_TTI, { NULL } };

UNIT tti_unit = { UDATA (&tti_svc, UNIT_8B, 0), KBD_POLL_WAIT };

REG tti_reg[] = {
	{ HRDATA (BUF, tti_unit.buf, 8) },
	{ HRDATA (CSR, tti_csr, 16) },
	{ FLDATA (INT, int_req[IPL_TTI], INT_V_TTI) },
	{ FLDATA (DONE, tti_csr, CSR_V_DONE) },
	{ FLDATA (IE, tti_csr, CSR_V_IE) },
	{ DRDATA (POS, tti_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, tti_unit.wait, 24), REG_NZ + PV_LEFT },
	{ NULL }  };

MTAB tti_mod[] = {
	{ UNIT_8B, UNIT_8B, "8b", "8B", NULL },
	{ UNIT_8B, 0      , "7b", "7B", NULL },
	{ MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,	NULL, &show_vec },
	{ 0 }  };

DEVICE tti_dev = {
	"TTI", &tti_unit, tti_reg, tti_mod,
	1, 10, 31, 1, 16, 8,
	NULL, NULL, &tti_reset,
	NULL, NULL, NULL,
	&tti_dib, 0 };

/* TTO data structures

   tto_dev	TTO device descriptor
   tto_unit	TTO unit descriptor
   tto_reg	TTO register list
*/

DIB tto_dib = { 0, 0, NULL, NULL, 1, IVCL (TTO), SCB_TTO, { NULL } };

UNIT tto_unit = { UDATA (&tto_svc, UNIT_8B, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
	{ HRDATA (BUF, tto_unit.buf, 8) },
	{ HRDATA (CSR, tto_csr, 16) },
	{ FLDATA (INT, int_req[IPL_TTO], INT_V_TTO) },
	{ FLDATA (DONE, tto_csr, CSR_V_DONE) },
	{ FLDATA (IE, tto_csr, CSR_V_IE) },
	{ DRDATA (POS, tto_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, tto_unit.wait, 24), PV_LEFT },
	{ NULL }  };

MTAB tto_mod[] = {
	{ UNIT_8B, UNIT_8B, "8b", "8B", NULL },
	{ UNIT_8B, 0      , "7b", "7B", NULL },
	{ MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,	NULL, &show_vec },
	{ 0 }  };

DEVICE tto_dev = {
	"TTO", &tto_unit, tto_reg, tto_mod,
	1, 10, 31, 1, 16, 8,
	NULL, NULL, &tto_reset,
	NULL, NULL, NULL,
	&tto_dib, 0 };

/* CLK data structures

   clk_dev	CLK device descriptor
   clk_unit	CLK unit descriptor
   clk_reg	CLK register list
*/

DIB clk_dib = { 0, 0, NULL, NULL, 1, IVCL (CLK), SCB_INTTIM, { NULL } };

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

MTAB clk_mod[] = {
	{ MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,	NULL, &show_vec },
	{ 0 }  };

DEVICE clk_dev = {
	"CLK", &clk_unit, clk_reg, clk_mod,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &clk_reset,
	NULL, NULL, NULL,
	&clk_dib, 0 };

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

/* Terminal input routines

   tti_svc	process event (character ready)
   tti_reset	process reset
*/

t_stat tti_svc (UNIT *uptr)
{
int32 c;

sim_activate (&tti_unit, tti_unit.wait);		/* continue poll */
if ((c = sim_poll_kbd ()) < SCPE_KFLAG) return c;	/* no char or error? */
tti_unit.buf = c & ((tti_unit.flags & UNIT_8B)? 0377: 0177);
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
int32 c;
t_stat r;

tto_csr = tto_csr | CSR_DONE;
if (tto_csr & CSR_IE) SET_INT (TTO);
c = tto_unit.buf & ((tto_unit.flags & UNIT_8B)? 0377: 0177);
if ((r = sim_putchar (c)) != SCPE_OK) return r;
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
base = (((((ctm->tm_yday * 24) +			/* sec since 1-Jan */
	    ctm->tm_hour) * 60) +
	    ctm->tm_min) * 60) +
	    ctm->tm_sec;
todr_reg = (base * 100) + 0x10000000;			/* cvt to VAX form */
todr_blow = 0;
return SCPE_OK;
}
