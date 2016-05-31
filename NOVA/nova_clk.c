/* nova_clk.c: NOVA real-time clock simulator

   Copyright (c) 1993-2008, Robert M. Supnik

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

   clk          real-time clock

   04-Jul-07    BKR     DEV_SET/CLR macros now used,
                        changed CLK name to RTC for DG compatiblity,
                        device may now bw DISABLED
   01-Mar-03    RMS     Added SET/SHOW CLK FREQ support
   03-Oct-02    RMS     Added DIB
   17-Sep-01    RMS     Added terminal multiplexor support
   17-Mar-01    RMS     Moved function prototype
   05-Mar-01    RMS     Added clock calibration
   24-Sep-97    RMS     Fixed bug in unit service (Charles Owen)
*/

#include "nova_defs.h"

extern int32 int_req, dev_busy, dev_done, dev_disable ;

int32 clk_sel = 0;                                      /* selected freq */
int32 clk_time[4] = { 16000, 100000, 10000, 1000 };     /* freq table */
int32 clk_tps[4] = { 60, 10, 100, 1000 };               /* ticks per sec */
int32 clk_adj[4] = { 1, -5, 2, 20 };                    /* tmxr adjust */
int32 tmxr_poll = 16000;                                /* tmxr poll */

int32  clk (int32 pulse, int32 code, int32 AC);
t_stat clk_svc (UNIT *uptr);
t_stat clk_reset (DEVICE *dptr);
t_stat clk_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat clk_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* CLK data structures

   clk_dev      CLK device descriptor
   clk_unit     CLK unit descriptor
   clk_reg      CLK register list
*/

DIB clk_dib = { DEV_CLK, INT_CLK, PI_CLK, &clk };

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
    { DRDATA (TPS0, clk_tps[0], 6), PV_LEFT + REG_HRO },
    { NULL }
    };

MTAB clk_mod[] = {
    { MTAB_XTD|MTAB_VDV, 50, NULL, "50HZ",
      &clk_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 60, NULL, "60HZ",
      &clk_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "LINE", NULL,
      NULL, &clk_show_freq, NULL },
    { 0 }
    };

DEVICE clk_dev = {
    "RTC", &clk_unit, clk_reg, clk_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &clk_reset,
    NULL, NULL, NULL,
    &clk_dib, DEV_DISABLE
    };


/* IOT routine */

int32 clk (int32 pulse, int32 code, int32 AC)
{
if (code == ioDOA) {                                    /* DOA */
    clk_sel = AC & 3;                                   /* save select */
    sim_rtc_init (clk_time[clk_sel]);                   /* init calibr */
    }

switch (pulse) {                                        /* decode IR<8:9> */

  case iopS:                                            /* start */
    DEV_SET_BUSY( INT_CLK ) ;
    DEV_CLR_DONE( INT_CLK ) ;
    DEV_UPDATE_INTR ;
    if (!sim_is_active (&clk_unit))                     /* not running? */
        sim_activate (&clk_unit,                        /* activate */
            sim_rtc_init (clk_time[clk_sel]));          /* init calibr */
    break;

  case iopC:                                            /* clear */
    DEV_CLR_BUSY( INT_CLK ) ;
    DEV_CLR_DONE( INT_CLK ) ;
    DEV_UPDATE_INTR ;
    sim_cancel (&clk_unit);                             /* deactivate unit */
    break;
    }                                                   /* end switch */

return 0;
}

/* Unit service */

t_stat clk_svc (UNIT *uptr)
{
int32 t;

if ( DEV_IS_BUSY(INT_CLK) )
    {
    DEV_CLR_BUSY( INT_CLK ) ;
    DEV_SET_DONE( INT_CLK ) ;
    DEV_UPDATE_INTR ;
    }
t = sim_rtc_calb (clk_tps[clk_sel]);                    /* calibrate delay */
sim_activate_after (uptr, 1000000/clk_tps[clk_sel]);    /* reactivate unit */
if (clk_adj[clk_sel] > 0)                               /* clk >= 60Hz? */
    tmxr_poll = t * clk_adj[clk_sel];                   /* poll is longer */
else
    tmxr_poll = t / (-clk_adj[clk_sel]);                /* poll is shorter */

return SCPE_OK;
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
sim_register_clock_unit (&clk_unit);                    /* declare clock unit */
clk_sel = 0;
DEV_CLR_BUSY( INT_CLK ) ;
DEV_CLR_DONE( INT_CLK ) ;
DEV_UPDATE_INTR ;

sim_cancel (&clk_unit);                                 /* deactivate unit */
tmxr_poll = clk_time[0];                                /* poll is default */
return SCPE_OK;
}

/* Set line frequency */

t_stat clk_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (cptr)
    return SCPE_ARG;
if ((val != 50) && (val != 60))
    return SCPE_IERR;
clk_tps[0] = val;
return SCPE_OK;
}

/* Show line frequency */

t_stat clk_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, (clk_tps[0] == 50)? "50Hz": "60Hz");
return SCPE_OK;
}
