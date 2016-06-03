/* sigma_rtc.c: Sigma clocks

   Copyright (c) 2007, Robert M. Supnik

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

   rtc           clocks

   The real-time clock includes an internal scheduler for events which need to
   be driven at multiples of the clock frequency, such as console and multiplexor
   polling.  Other devices can "register" with the clock module to receive service
   callbacks at a timed interval.  This replaces the standard SimH event queue
   mechanism for real-time synchronous events.
*/

#include "sigma_io_defs.h"

#define RTC_HZ_BASE     500
#define RTC_TICKS_DFLT  500

/* Timed events data structures */

uint8 rtc_indx[RTC_NUM_EVNTS];                          /* index into rtc_tab */
uint8 rtc_cntr[RTC_NUM_EVNTS];                          /* timer ticks left */
uint8 rtc_xtra[RTC_NUM_EVNTS];                          /* extra counter */
UNIT *rtc_usrv[RTC_NUM_EVNTS];                          /* unit servers */

/* Real-time clock counter frequencies */

uint16 rtc_tps[RTC_NUM_CNTRS] = {
    RTC_HZ_OFF, RTC_HZ_OFF, RTC_HZ_500, RTC_HZ_500
    };

/* Frequency descriptors.  The base clock runs at 500Hz.  To get submultiples,
   an event uses a tick counter.  If the frequency is not an even submultiple, the
   event can specify an "extra" counter.  Every "extra" ticks of the event counter, 
   the event counter is increased by one.  Thus, 60Hz counts as 8-8-9, providing
   3 clock ticks for every 25 base timer ticks. */

typedef struct {
    uint32      hz;
    uint32      cntr_reset;
    uint32      xtra_reset;
    } rtcdef_t;

static rtcdef_t rtc_tab[RTC_NUM_HZ] = {
    { 0, 0, 0 },
    { 500, 1, 0 },
    { 50, 10, 0 },
    { 60, 8, 3 },
    { 100, 5, 0 },
    { 2, 250, 0 },
    };

t_stat rtc_svc (UNIT *uptr);
t_stat rtc_cntr_svc (UNIT *uptr);
t_stat rtc_reset (DEVICE *dptr);
t_stat rtc_show_events (FILE *of, UNIT *uptr, int32 val, CONST void *desc);

/* Clock data structures

   rtc_dev      RTC device descriptor
   rtc_unit     RTC unit
   rtc_reg      RTC register list
*/

UNIT rtc_unit = { UDATA (&rtc_svc, 0, 0), RTC_TICKS_DFLT };

UNIT rtc_cntr_unit[RTC_NUM_CNTRS] = {
    { UDATA (&rtc_cntr_svc, 0, 0) },
    { UDATA (&rtc_cntr_svc, 0, 0) },
    { UDATA (&rtc_cntr_svc, 0, 0) },
    { UDATA (&rtc_cntr_svc, 0, 0) }
    };

REG rtc_reg[] = {
    { BRDATA (TPS, rtc_tps, 10, 10, RTC_NUM_CNTRS), REG_HRO },
    { BRDATA (INDX, rtc_indx, 10, 4, RTC_NUM_EVNTS), REG_HRO },
    { BRDATA (CNTR, rtc_cntr, 10, 6, RTC_NUM_EVNTS), REG_HRO },
    { BRDATA (XTRA, rtc_xtra, 10, 6, RTC_NUM_EVNTS), REG_HRO },
    { NULL }
    };

MTAB rtc_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, RTC_C1, "C1", "C1",
      &rtc_set_tps, &rtc_show_tps, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, RTC_C2, "C2", "C2",
      &rtc_set_tps, &rtc_show_tps, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, RTC_C3, "C3", "C3",
      &rtc_set_tps, &rtc_show_tps, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, RTC_C4, "C4", NULL,
      NULL, &rtc_show_tps, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "EVENTS", NULL,
      NULL, &rtc_show_events, NULL },
    { 0 }
    };

DEVICE rtc_dev = {
    "RTC", &rtc_unit, rtc_reg, rtc_mod,
    1, 16, 8, 1, 16, 8,
    NULL, NULL, &rtc_reset,
    NULL, NULL, NULL
    };

/* Master timer service routine */

t_stat rtc_svc (UNIT *uptr)
{
uint32 i, idx;
int32 t;
t_stat st;

t = sim_rtcn_calb (RTC_HZ_BASE, TMR_RTC);               /* calibrate clock */
sim_activate (uptr, t);                                 /* reactivate unit */
for (i = 0; i < RTC_NUM_EVNTS; i++) {                   /* loop thru events */
    if (rtc_cntr[i] != 0) {                             /* event active? */
        rtc_cntr[i] = rtc_cntr[i] - 1;                  /* decrement */
        if (rtc_cntr[i] == 0) {                         /* expired? */
            idx = rtc_indx[i];
            rtc_cntr[i] = rtc_tab[idx].cntr_reset;      /* reset counter */
            if (rtc_xtra[i] != 0) {                     /* fudge factor? */
                rtc_xtra[i] = rtc_xtra[i] - 1;          /* decr fudge cntr */
                if (rtc_xtra[i] == 0) {                 /* expired? */
                    rtc_cntr[i]++;                      /* extra tick */
                    rtc_xtra[i] = rtc_tab[idx].xtra_reset; /* reset fudge cntr */
                    }                                   /* end fudge = 0 */
                }                                       /* end fudge active */
            if ((rtc_usrv[i] == NULL) ||                /* registered? */
                (rtc_usrv[i]->action == NULL))
                return SCPE_IERR;                       /* should be */
            st = rtc_usrv[i]->action (rtc_usrv[i]);     /* callback */
            if (st != SCPE_OK)                          /* error */
                return st;
            }                                           /* end cntr = 0 */
        }                                               /* end event active */
    }                                                   /* end event loop */
return SCPE_OK;
}

/* Callback for a system timer */

t_stat rtc_cntr_svc (UNIT *uptr)
{
uint32 cn = uptr - rtc_cntr_unit;

io_sclr_req (INTV (INTG_OVR, cn), 1);                   /* set cntr intr */
return SCPE_OK;
}

/* Register a timer */

t_stat rtc_register (uint32 tm, uint32 idx, UNIT *uptr)
{
if ((tm >= RTC_NUM_EVNTS) ||                            /* validate params */
    (idx >= RTC_NUM_HZ) ||
    (uptr == NULL) ||
    (uptr->action == NULL))
    return SCPE_IERR;
rtc_usrv[tm] = uptr;
rtc_indx[tm] = idx;
rtc_cntr[tm] = rtc_tab[idx].cntr_reset;                 /* init event */
rtc_xtra[tm] = rtc_tab[idx].xtra_reset;
return SCPE_OK;
}

/* Set timer ticks */

t_stat rtc_set_tps (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 newval, i;
t_stat r;

if (val >= RTC_NUM_EVNTS)                               /* validate params */
    return SCPE_IERR;
if (cptr == NULL)                                       /* must have arg */
    return SCPE_ARG;
newval = get_uint (cptr, 10, 10000, &r);
if ((r != SCPE_OK) ||                                   /* error? */
    ((newval == 0) && (val >= 2)))                      /* can't turn off 3,4 */
    return SCPE_ARG;
for (i = 0; i < RTC_NUM_HZ; i++) {                      /* loop thru freqs */
    if (newval == rtc_tab[i].hz) {                      /* found freq? */
        rtc_tps[val] = i;
        rtc_indx[val] = i;                              /* save event vals */
        rtc_cntr[val] = rtc_tab[i].cntr_reset;
        rtc_xtra[val] = rtc_tab[i].xtra_reset;
        return SCPE_OK;
        }
    }
return SCPE_ARG;
}

/* Show timer ticks */

t_stat rtc_show_tps (FILE *of, UNIT *uptr, int32 val, CONST void *desc)
{
uint32 idx;

if (val >= RTC_NUM_EVNTS)
    return SCPE_IERR;
idx = rtc_tps[val];                                     /* ptr to clk defs */
if (rtc_tab[idx].hz == 0)
    fprintf (of, "off\n");
else fprintf (of, "%dHz\n", rtc_tab[idx].hz);
return SCPE_OK;
}


/* Reset routine */

t_stat rtc_reset (DEVICE *dptr)
{
uint32 i;

sim_rtcn_init (rtc_unit.wait, TMR_RTC);                 /* init base clock */
sim_activate_abs (&rtc_unit, rtc_unit.wait);            /* activate unit */

for (i = 0; i < RTC_NUM_EVNTS; i++) {                   /* clear counters */
    if (i < RTC_NUM_CNTRS) {
        rtc_cntr[i] = 0;
        rtc_xtra[i] = 0;
        rtc_indx[i] = 0;
        rtc_usrv[i] = NULL;
        if (rtc_register (i, rtc_tps[i], &rtc_cntr_unit[i]) != SCPE_OK)
            return SCPE_IERR;
        }
    else if ((rtc_usrv[i] != NULL) &&
            (rtc_register (i, rtc_indx[i], rtc_usrv[i]) != SCPE_OK))
        return SCPE_IERR;
    }
return SCPE_OK;
}

/* Show events */

t_stat rtc_show_events (FILE *of, UNIT *uptr, int32 val, CONST void *desc)
{
uint32 i;

fprintf (of, "Event  Status  Frequency  Ticks  Extra\n");
for (i = 0; i < RTC_NUM_EVNTS; i++) {
    if (rtc_cntr[i])
        fprintf (of, "  %d      on      %3dHz     %3d      %d\n",
            i, rtc_tab[rtc_indx[i]].hz, rtc_cntr[i], rtc_xtra[i]);
    else fprintf (of, "  %d      off\n", i);
    }
return SCPE_OK;
}