/* nova_clk.c: NOVA real-time clock simulator

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

   clk		real-time clock

   05-Mar-01	RMS	Added clock calibration
   24-Sep-97	RMS	Fixed bug in unit service (found by Charles Owen)
*/

#include "nova_defs.h"

extern int32 int_req, dev_busy, dev_done, dev_disable;
int32 clk_sel = 0;					/* selected freq */
int32 clk_time[4] = { 16000, 100000, 10000, 1000 };	/* freq table */
int32 clk_tps[4] = { 60, 10, 100, 1000 };		/* ticks per sec */
t_stat clk_svc (UNIT *uptr);
t_stat clk_reset (DEVICE *dptr);
extern int32 sim_rtc_init (int32 time);
extern int32 sim_rtc_calb (int32 tps);

/* CLK data structures

   clk_dev	CLK device descriptor
   clk_unit	CLK unit descriptor
   clk_reg	CLK register list
*/

UNIT clk_unit = { UDATA (&clk_svc, 0, 0) };

REG clk_reg[] = {
	{ ORDATA (SELECT, clk_sel, 2) },
	{ FLDATA (BUSY, dev_busy, INT_V_CLK) },
	{ FLDATA (DONE, dev_done, INT_V_CLK) },
	{ FLDATA (DISABLE, dev_disable, INT_V_CLK) },
	{ FLDATA (INT, int_req, INT_V_CLK) },
	{ DRDATA (TIME0, clk_time[0], 24), REG_NZ + PV_LEFT },
	{ DRDATA (TIME1, clk_time[1], 24), REG_NZ + PV_LEFT },
	{ DRDATA (TIME2, clk_time[2], 24), REG_NZ + PV_LEFT },
	{ DRDATA (TIME3, clk_time[3], 24), REG_NZ + PV_LEFT },
	{ NULL }  };

DEVICE clk_dev = {
	"CLK", &clk_unit, clk_reg, NULL,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &clk_reset,
	NULL, NULL, NULL };

/* IOT routine */

int32 clk (int32 pulse, int32 code, int32 AC)
{
if (code == ioDOA) {					/* DOA */
	clk_sel = AC & 3;				/* save select */
	sim_rtc_init (clk_time[clk_sel]);  }		/* init calibr */
switch (pulse) {					/* decode IR<8:9> */
case iopS: 						/* start */
	dev_busy = dev_busy | INT_CLK;			/* set busy */
	dev_done = dev_done & ~INT_CLK;			/* clear done, int */
	int_req = int_req & ~INT_CLK;
	if (!sim_is_active (&clk_unit))			/* not running? */
		sim_activate (&clk_unit,		/* activate */
		    sim_rtc_init (clk_time[clk_sel]));	/* init calibr */
	break;
case iopC:						/* clear */
	dev_busy = dev_busy & ~INT_CLK;			/* clear busy */
	dev_done = dev_done & ~INT_CLK;			/* clear done, int */
	int_req = int_req & ~INT_CLK;
	sim_cancel (&clk_unit);				/* deactivate unit */
	break;  }					/* end switch */
return 0;
}

/* Unit service */

t_stat clk_svc (UNIT *uptr)
{
dev_done = dev_done | INT_CLK;				/* set done */
dev_busy = dev_busy & ~INT_CLK;				/* clear busy */
int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
sim_activate (&clk_unit,				/* reactivate unit */
	sim_rtc_calb (clk_tps[clk_sel]));		/* calibrate delay */
return SCPE_OK;
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
clk_sel = 0;
dev_busy = dev_busy & ~INT_CLK;				/* clear busy */
dev_done = dev_done & ~INT_CLK;				/* clear done, int */
int_req = int_req & ~INT_CLK;
sim_cancel (&clk_unit);					/* deactivate unit */
return SCPE_OK;
}
