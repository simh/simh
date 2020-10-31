/* ka10_pd.c: DeCoriolis clock.

   Copyright (c) 2018, Lars Brinkhoff

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This is a device which keeps track of the time and date.  An access
   will return the number of ticks since the beginning of the year.
   There are 60 ticks per second.  The device was made by Paul
   DeCoriolis at MIT.

   When used with a KL10, the clock was part of the KL-UDGE board
   which could also provide a 60 Hz interrupt and set console lights.
   This is not needed on a KA10, so it's not implemented here.

*/

#include "kx10_defs.h"

#ifndef NUM_DEVS_PD
#define NUM_DEVS_PD 0
#endif

#if (NUM_DEVS_PD > 0)

#define PD_DEVNUM       0500
#define PD_OFF          (1 << DEV_V_UF)

#define PIA_CH          u3

#define PIA_FLG         07
#define CLK_IRQ         010

int pd_tps =            60;

t_stat         pd_devio(uint32 dev, uint64 *data);
const char     *pd_description (DEVICE *dptr);
t_stat         pd_srv(UNIT *uptr);
t_stat         pd_set_on(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat         pd_set_off(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat         pd_show_on(FILE *st, UNIT *uptr, int32 val, CONST void *desc);

UNIT                pd_unit[] = {
    {UDATA(pd_srv, UNIT_IDLE|UNIT_DISABLE, 0)},  /* 0 */
};
DIB pd_dib = {PD_DEVNUM, 1, &pd_devio, NULL};

MTAB pd_mod[] = {
    { MTAB_VDV, 0, "ON", "ON", pd_set_on, pd_show_on },
    { MTAB_VDV, PD_OFF, NULL, "OFF", pd_set_off },
    { 0 }
    };

DEVICE              pd_dev = {
    "PD", pd_unit, NULL, pd_mod,
    1, 8, 0, 1, 8, 36,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &pd_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, NULL,
    NULL, NULL, NULL, NULL, NULL, &pd_description
};

static uint64 pd_ticks (void)
{
    time_t t = sim_get_time(NULL);
    struct tm *x = localtime(&t);
    uint64 seconds;
    seconds = 86400ULL * x->tm_yday;
    seconds += 3600ULL * x->tm_hour;
    seconds +=   60ULL * x->tm_min;
    seconds +=           x->tm_sec;
    // We could add individual ticks here, but there's no pressing need.
    return 60ULL * seconds;
}

t_stat pd_devio(uint32 dev, uint64 *data)
{
    DEVICE *dptr = &pd_dev;

    switch(dev & 07) {
    case DATAI:
        if (dptr->flags & PD_OFF)
            *data = 0;
        else
            *data = pd_ticks();
        break;
    case CONI:
        *data = (uint64)(pd_unit[0].PIA_CH & (CLK_IRQ|PIA_FLG));
        break;
    case CONO:
        pd_unit[0].PIA_CH &= ~(PIA_FLG);
        pd_unit[0].PIA_CH |= (int32)(*data & PIA_FLG);
        if (pd_unit[0].PIA_CH & PIA_FLG) {
            if (!sim_is_active(pd_unit))
                sim_activate(pd_unit, 10000); 
        }
        if (*data & CLK_IRQ) {
            pd_unit[0].PIA_CH &= ~(CLK_IRQ);
            clr_interrupt(PD_DEVNUM);
        }
        break;
    default:
        break;
    }

    return SCPE_OK;
}

t_stat
pd_srv(UNIT * uptr)
{
    sim_activate_after(uptr, 1000000/pd_tps);
    if (uptr->PIA_CH & PIA_FLG) {
        uptr->PIA_CH |= CLK_IRQ;
        set_interrupt(PD_DEVNUM, uptr->PIA_CH);
    } else
        sim_cancel(uptr);

    return SCPE_OK;
}

const char *pd_description (DEVICE *dptr)
{
    return "Paul DeCoriolis clock";
}

t_stat pd_set_on(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE *dptr = &pd_dev;
    dptr->flags &= ~PD_OFF;
    return SCPE_OK;
}

t_stat pd_set_off(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE *dptr = &pd_dev;
    dptr->flags |= PD_OFF;
    return SCPE_OK;
}

t_stat pd_show_on(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    DEVICE *dptr = &pd_dev;
    fprintf (st, "%s", (dptr->flags & PD_OFF) ? "off" : "on");
    return SCPE_OK;
}

#endif
