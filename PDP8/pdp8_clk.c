/* pdp8_clk.c: PDP-8 real-time clock simulator

   Copyright (c) 1993-2012, Robert M Supnik

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
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   clk          real time clock

   18-Apr-12    RMS     Added clock coscheduling
   18-Jun-07    RMS     Added UNIT_IDLE flag
   01-Mar-03    RMS     Aded SET/SHOW CLK FREQ support
   04-Oct-02    RMS     Added DIB, device number support
   30-Dec-01    RMS     Removed for generalized timers
   05-Sep-01    RMS     Added terminal multiplexor support
   17-Jul-01    RMS     Moved function prototype
   05-Mar-01    RMS     Added clock calibration support

   Note: includes the IOT's for both the PDP-8/E and PDP-8/A clocks
*/

#include "pdp8_defs.h"

extern int32 int_req, int_enable, dev_done, stop_inst;

int32 clk_tps = 60;                                     /* ticks/second */
int32 tmxr_poll = 16000;                                /* term mux poll */

int32 clk (int32 IR, int32 AC);
t_stat clk_svc (UNIT *uptr);
t_stat clk_reset (DEVICE *dptr);
t_stat clk_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat clk_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* CLK data structures

   clk_dev      CLK device descriptor
   clk_unit     CLK unit descriptor
   clk_reg      CLK register list
*/

DIB clk_dib = { DEV_CLK, 1, { &clk } };

UNIT clk_unit = { UDATA (&clk_svc, UNIT_IDLE, 0), 16000 };

REG clk_reg[] = {
    { FLDATAD (DONE, dev_done, INT_V_CLK, "device done flag") },
    { FLDATAD (ENABLE, int_enable, INT_V_CLK, "interrupt enable flag") },
    { FLDATAD (INT, int_req, INT_V_CLK, "interrupt pending flag") },
    { DRDATAD (TIME, clk_unit.wait, 24, "clock interval"), REG_NZ + PV_LEFT },
    { DRDATA (TPS, clk_tps, 8), PV_LEFT + REG_HRO },
    { NULL }
    };

MTAB clk_mod[] = {
    { MTAB_XTD|MTAB_VDV, 50, NULL, "50HZ",
      &clk_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 60, NULL, "60HZ",
      &clk_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "FREQUENCY", NULL,
      NULL, &clk_show_freq, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", NULL, NULL, &show_dev },
    { 0 }
    };

DEVICE clk_dev = {
    "CLK", &clk_unit, clk_reg, clk_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &clk_reset,
    NULL, NULL, NULL,
    &clk_dib, 0
    };

/* IOT routine

   IOT's 6131-6133 are the PDP-8/E clock
   IOT's 6135-6137 are the PDP-8/A clock
*/

int32 clk (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 1:                                             /* CLEI */
        int_enable = int_enable | INT_CLK;              /* enable clk ints */
        int_req = INT_UPDATE;                           /* update interrupts */
        return AC;

    case 2:                                             /* CLDI */
        int_enable = int_enable & ~INT_CLK;             /* disable clk ints */
        int_req = int_req & ~INT_CLK;                   /* update interrupts */
        return AC;

    case 3:                                             /* CLSC */
        if (dev_done & INT_CLK) {                       /* flag set? */
            dev_done = dev_done & ~INT_CLK;             /* clear flag */
            int_req = int_req & ~INT_CLK;               /* clear int req */
            return IOT_SKP + AC;
            }
        return AC;

    case 5:                                             /* CLLE */
        if (AC & 1)                                     /* test AC<11> */
            int_enable = int_enable | INT_CLK;
        else int_enable = int_enable & ~INT_CLK;
        int_req = INT_UPDATE;                           /* update interrupts */
        return AC;

    case 6:                                             /* CLCL */
        dev_done = dev_done & ~INT_CLK;                 /* clear flag */
        int_req = int_req & ~INT_CLK;                   /* clear int req */
        return AC;

    case 7:                                             /* CLSK */
        return (dev_done & INT_CLK)? IOT_SKP + AC: AC;

    default:
        return (stop_inst << IOT_V_REASON) + AC;
        }                                               /* end switch */
}

/* Unit service */

t_stat clk_svc (UNIT *uptr)
{
dev_done = dev_done | INT_CLK;                          /* set done */
int_req = INT_UPDATE;                                   /* update interrupts */
tmxr_poll = sim_rtcn_calb (clk_tps, TMR_CLK);           /* calibrate clock */
sim_activate_after (uptr, 1000000/clk_tps);             /* reactivate unit */
return SCPE_OK;
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
dev_done = dev_done & ~INT_CLK;                         /* clear done, int */
int_req = int_req & ~INT_CLK;
int_enable = int_enable & ~INT_CLK;                     /* clear enable */
if (!sim_is_running) {                                  /* RESET (not CAF)? */
    tmxr_poll = sim_rtcn_init_unit (&clk_unit, clk_unit.wait, TMR_CLK);/* init 100Hz timer */
    sim_activate_after (&clk_unit, 1000000/clk_tps);        /* activate 100Hz unit */
    }
return SCPE_OK;
}

/* Set frequency */

t_stat clk_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (cptr)
    return SCPE_ARG;
if ((val != 50) && (val != 60))
    return SCPE_IERR;
clk_tps = val;
return SCPE_OK;
}

/* Show frequency */

t_stat clk_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, (clk_tps == 50)? "50Hz": "60Hz");
return SCPE_OK;
}
