/* ka10_pclk.c: Petit Calendar Clock.

   Copyright (c) 2018, Lars Brinkhoff
   Copyright (c) 2020, Bruce Baumgart ( by editing Brinkhoff ka10_pd.c )

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

*/

#include "kx10_defs.h"

#ifndef NUM_DEVS_PCLK
#define NUM_DEVS_PCLK 0
#endif

#if (NUM_DEVS_PCLK > 0)

#define PCLK_DEVNUM 0730
#define PCLK_OFF (1 << DEV_V_UF)
#define PIA_CH          u3
#define PIA_FLG         07
#define CLK_IRQ         010

t_stat         pclk_devio(uint32 dev, uint64 *data);
const char *pclk_description (DEVICE *dptr);
t_stat         pclk_srv(UNIT *uptr);
t_stat         pclk_set_on(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat         pclk_set_off(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat         pclk_show_on(FILE *st, UNIT *uptr, int32 val, CONST void *desc);

UNIT pclk_unit[] = {
    {UDATA(pclk_srv, UNIT_IDLE|UNIT_DISABLE, 0)},  /* 0 */
};
DIB pclk_dib = {PCLK_DEVNUM, 1, &pclk_devio, NULL};
MTAB pclk_mod[] = {
    { MTAB_VDV, 0, "ON", "ON", pclk_set_on, pclk_show_on },
    { MTAB_VDV, PCLK_OFF, NULL, "OFF", pclk_set_off },
    { 0 }
    };
DEVICE pclk_dev = {
    "PCLK", pclk_unit, NULL, pclk_mod, 1, 8, 0, 1, 8, 36, NULL, NULL, NULL, NULL, NULL, NULL,
    &pclk_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, NULL, NULL, NULL, NULL, NULL, NULL, &pclk_description
};
/*
        This is the Petit real-time calendar-clock re-enactment, where
        the DATE is always Friday 1974-07-26, and
        the TIME is the local wall clock time.

        Months are encoded 4,5,6, 7,8,9, A,B,C, D,E,F for January to December.
        Day-of-month runs from 0 to 30, for the 1st to 31st.
        July 1974 is hex '74A' and the 26th day is coded '25' decimal.

        The original PCLK was installed on the PDP-6 I/O bus at the SAIL D.C.Power Lab in 1967.
*/
t_stat pclk_devio(uint32 dev, uint64 *data)
{
    time_t t=sim_get_time(NULL);
    struct tm *dt;
    uint64 hour=12, minute=1, seconds=2, milliseconds=3;
    uint64 coni_word = ((minute&0xF)<<26) | (seconds<<20) | milliseconds;
    uint64 datai_word;
    dt = localtime( &t );
    hour = dt->tm_hour;
    minute = dt->tm_min;
    coni_word = (dt->tm_min << 26) | (dt->tm_sec << 20);
    coni_word += 02020136700; // plus the Petit/Panofsky offset.
    datai_word = ( 0x74A<<16 | 25<<11 | hour<<6 | minute ) + 05004;
    switch(dev & 3) {
    case DATAI:
      *data = datai_word;
      break;
    case CONI:
      *data = coni_word;
      break;
    case CONO:
        pclk_unit[0].PIA_CH &= ~(PIA_FLG);
        pclk_unit[0].PIA_CH |= (int32)(*data & PIA_FLG);
        break;
    default:
        break;
    }
    return SCPE_OK;
}

t_stat
pclk_srv(UNIT * uptr)
{
    if (uptr->PIA_CH & PIA_FLG) {
        uptr->PIA_CH |= CLK_IRQ;
        //        set_interrupt(PCLK_DEVNUM, uptr->PIA_CH);
    } else
        sim_cancel(uptr);
    return SCPE_OK;
}

const char *pclk_description (DEVICE *dptr)
{
    return "Stanford A.I.Lab Phil Petit calendar clock crock";
}

t_stat pclk_set_on(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE *dptr = &pclk_dev;
    dptr->flags &= ~PCLK_OFF;
    return SCPE_OK;
}

t_stat pclk_set_off(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE *dptr = &pclk_dev;
    dptr->flags |= PCLK_OFF;
    return SCPE_OK;
}

t_stat pclk_show_on(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    DEVICE *dptr = &pclk_dev;
    fprintf (st, "%s", (dptr->flags & PCLK_OFF) ? "off" : "on");
    return SCPE_OK;
}
#endif
