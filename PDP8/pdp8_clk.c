/* pdp8_clk.c: PDP-8 real-time clock simulator

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

   clk		real time clock

   05-Sep-01	RMS	Added terminal multiplexor support
   17-Jul-01	RMS	Moved function prototype
   05-Mar-01	RMS	Added clock calibration support

   Note: includes the IOT's for both the PDP-8/E and PDP-8/A clocks
*/

#include "pdp8_defs.h"

extern int32 int_req, int_enable, dev_done, stop_inst;
t_stat clk_svc (UNIT *uptr);
t_stat clk_reset (DEVICE *dptr);
int32 clk_tps = 60;					/* ticks/second */
int32 tmxr_poll = 16000;				/* term mux poll */

/* CLK data structures

   clk_dev	CLK device descriptor
   clk_unit	CLK unit descriptor
   clk_reg	CLK register list
*/

UNIT clk_unit = { UDATA (&clk_svc, 0, 0), 16000 };

REG clk_reg[] = {
	{ FLDATA (DONE, dev_done, INT_V_CLK) },
	{ FLDATA (ENABLE, int_enable, INT_V_CLK) },
	{ FLDATA (INT, int_req, INT_V_CLK) },
	{ DRDATA (TIME, clk_unit.wait, 24), REG_NZ + PV_LEFT },
	{ DRDATA (TPS, clk_tps, 8), REG_NZ + PV_LEFT },
	{ NULL }  };

DEVICE clk_dev = {
	"CLK", &clk_unit, clk_reg, NULL,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &clk_reset,
	NULL, NULL, NULL };

/* IOT routine

   IOT's 6131-6133 are the PDP-8/E clock
   IOT's 6135-6137 the PDP-8/A clock
*/

int32 clk (int32 pulse, int32 AC)
{
switch (pulse) {					/* decode IR<9:11> */
case 1:							/* CLEI */
	int_enable = int_enable | INT_CLK;		/* enable clk ints */
	int_req = INT_UPDATE;				/* update interrupts */
	return AC;
case 2:							/* CLDI */
	int_enable = int_enable & ~INT_CLK;		/* disable clk ints */
	int_req = int_req & ~INT_CLK;			/* update interrupts */
	return AC;
case 3:							/* CLSC */
	if (dev_done & INT_CLK) {			/* flag set? */
		dev_done = dev_done & ~INT_CLK;		/* clear flag */
		int_req = int_req & ~INT_CLK;		/* clear int req */
		return IOT_SKP + AC;  }
	return AC;
case 5:							/* CLLE */
	if (AC & 1) int_enable = int_enable | INT_CLK;	/* test AC<11> */
	else int_enable = int_enable & ~INT_CLK;
	int_req = INT_UPDATE;				/* update interrupts */
	return AC;
case 6:							/* CLCL */
	dev_done = dev_done & ~INT_CLK;			/* clear flag */
	int_req = int_req & ~INT_CLK;			/* clear int req */
	return AC;
case 7:							/* CLSK */
	return (dev_done & INT_CLK)? IOT_SKP + AC: AC;
default:
	return (stop_inst << IOT_V_REASON) + AC;  }	/* end switch */
}

/* Unit service */

t_stat clk_svc (UNIT *uptr)
{
int32 t;

dev_done = dev_done | INT_CLK;				/* set done */
int_req = INT_UPDATE;					/* update interrupts */
t = sim_rtc_calb (clk_tps);				/* calibrate clock */
sim_activate (&clk_unit, t);				/* reactivate unit */
tmxr_poll = t;						/* set mux poll */
return SCPE_OK;
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
dev_done = dev_done & ~INT_CLK;				/* clear done, int */
int_req = int_req & ~INT_CLK;
int_enable = int_enable & ~INT_CLK;			/* clear enable */
sim_activate (&clk_unit, clk_unit.wait);		/* activate unit */
tmxr_poll = clk_unit.wait;				/* set mux poll */
return SCPE_OK;
}
