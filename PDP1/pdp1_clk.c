/* pdp1_clk.c: PDP-1D clock simulator

   Copyright (c) 2006-2008, Robert M. Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   bused in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   clk          PDP-1D clock

   Note that the clock is run at 1/8 of real speed (125Hz instead of 1Khz), to
   provide for eventual implementation of idling.
*/

#include "pdp1_defs.h"

#define CLK_HWRE_TPS    1000                            /* hardware freq */
#define CLK_TPS         125                             /* sim freq */
#define CLK_CNTS        (CLK_HWRE_TPS / CLK_TPS)        /* counts per tick */
#define CLK_C1MIN       (1000 * 60)                     /* counts per min */
#define CLK_C32MS       32                              /* counts per 32ms */

int32 clk32ms_sbs = 0;                                  /* 32ms SBS level */
int32 clk1min_sbs = 0;                                  /* 1min SBS level */
int32 clk_cntr = 0;
int32 tmxr_poll = 5000;

extern int32 stop_inst;

t_stat clk_svc (UNIT *uptr);
t_stat clk_reset (DEVICE *dptr);

/* CLK data structures

   clk_dev      CLK device descriptor
   clk_unit     CLK unit
   clk_reg      CLK register list
*/

UNIT clk_unit = {
    UDATA (&clk_svc, 0, 0), 5000
    };

REG clk_reg[] = {
    { ORDATAD (CNTR, clk_cntr, 16, "clock counter, 0-59999(base 10)") },
    { DRDATA (SBS32LVL, clk32ms_sbs, 4), REG_HRO },
    { DRDATA (SBS1MLVL, clk1min_sbs, 4), REG_HRO },
    { NULL }
    };

MTAB clk_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "SBS32MSLVL", "SBS32MSLVL",
      &dev_set_sbs, &dev_show_sbs, (void *) &clk32ms_sbs },
    { MTAB_XTD|MTAB_VDV, 0, "SBS1MINLVL", "SBS1MINLVL",
      &dev_set_sbs, &dev_show_sbs, (void *) &clk1min_sbs },
    { 0 }
    };

DEVICE clk_dev = {
    "CLK", &clk_unit, clk_reg, clk_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &clk_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DIS
    };

/* Clock IOT routine */

int32 clk (int32 inst, int32 dev, int32 dat)
{
int32 used, incr;

if (clk_dev.flags & DEV_DIS)                            /* disabled? */
    return (stop_inst << IOT_V_REASON) | dat;           /* illegal inst */
used = tmxr_poll - (sim_activate_time (&clk_unit) - 1);
incr = (used * CLK_CNTS) / tmxr_poll;
return clk_cntr + incr;
}

/* Unit service, generate appropriate interrupts */

t_stat clk_svc (UNIT *uptr)
{
if (clk_dev.flags & DEV_DIS)                            /* disabled? */
    return SCPE_OK;
tmxr_poll = sim_rtcn_calb (CLK_TPS, TMR_CLK);           /* calibrate clock */
sim_activate_after (uptr, 1000000/CLK_TPS);             /* reactivate unit */
clk_cntr = clk_cntr + CLK_CNTS;                         /* incr counter */
if ((clk_cntr % CLK_C32MS) == 0)                        /* 32ms interval? */
    dev_req_int (clk32ms_sbs);                          /* req intr */
if (clk_cntr >= CLK_C1MIN) {                            /* 1min interval? */
    dev_req_int (clk1min_sbs);                          /* req intr */
    clk_cntr = 0;                                       /* reset counter */
    }
return SCPE_OK;
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
if (clk_dev.flags & DEV_DIS) sim_cancel (&clk_unit);    /* disabled? */
else {
    tmxr_poll = sim_rtcn_init_unit (&clk_unit, clk_unit.wait, TMR_CLK);/* init timer */
    sim_activate_after (&clk_unit, 1000000/CLK_TPS);    /* activate unit */
    }
clk_cntr = 0;                                           /* clear counter */
return SCPE_OK;
}
