/* ka10_dk.c: PDP-10 DK subsystem simulator

   Copyright (c) 2013-2017, Richard Cornwell

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

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell.

*/

#include "kx10_defs.h"
#include <time.h>

#ifndef NUM_DEVS_DK
#define NUM_DEVS_DK 0
#endif

#if (NUM_DEVS_DK > 0)

#define DK_DEVNUM       070
#define STAT_REG        u3
#define CLK_REG         u4
#define INT_REG         u5
#define CLK_TIM         u6

/* CONO */
#define PIA             000007
#define CLK_CLR_FLG     000010                     /* Clear Clock flag */
#define CLK_CLR_OVF     000020                     /* Clear OVFL flag */
#define CLK_SET_EN      000040                     /* Enable Clock */
#define CLK_CLR_EN      000100                     /* Disable Clock */
#define CLK_SET_PI      000200                     /* Set PI Control Flip-Flop */
#define CLK_CLR_PI      000400                     /* Clear PI Control Flip-Flop */
#define CLK_GEN_CLR     001000                     /* Clear control */
#define CLK_ADD_ONE     002000                     /* Bump clock */
#define CLK_SET_FLG     004000                     /* Set Clock Flag */
#define CLK_SET_OVF     010000                     /* Set OVFL Flag */

/* CONI */
#define CLK_FLG         000010
#define CLK_OVF         000020
#define CLK_EN          000040
#define CLK_PI          000200
#define CLK_EXT         001000

t_stat dk_devio(uint32 dev, uint64 *data);
void   dk_test (UNIT *uptr);
t_stat dk_svc (UNIT *uptr);
const char *dk_description (DEVICE *dptr);

DIB dk_dib[] = {
        { DK_DEVNUM, 1, &dk_devio, NULL },
        { DK_DEVNUM + 4, 1, &dk_devio, NULL}};

UNIT dk_unit[] = {
        {UDATA (&dk_svc, UNIT_IDLE, 0) },
#if (NUM_DEVS_DK > 1)
        {UDATA (&dk_svc, UNIT_IDLE, 0) },
#endif
        };

DEVICE dk_dev = {
    "DK", dk_unit, NULL, NULL,
    NUM_DEVS_DK, 0, 0, 0, 0, 0,
    NULL, NULL, NULL,
    NULL, NULL, NULL,
    &dk_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, NULL, NULL, NULL, &dk_description
    };

t_stat dk_devio(uint32 dev, uint64 *data) {
    int         unit = (dev - DK_DEVNUM) >> 2;
    UNIT        *uptr;
    double      us;
    int32       t;

    if (unit < 0 || unit >= NUM_DEVS_DK)
        return SCPE_OK;
    uptr = &dk_unit[unit];
    switch (dev & 3) {
    case CONI:
        *data = (uint64)(uptr->STAT_REG);
        *data |= ((uint64)uptr->INT_REG) << 18;
        sim_debug(DEBUG_CONI, &dk_dev, "DK  %03o CONI %06o PC=%o %06o\n",
               dev, (uint32)*data, PC, uptr->CLK_REG);
        break;

    case CONO:
        /* Adjust U3 */
        clr_interrupt(dev);
        uptr->STAT_REG &= ~07;
        if (*data & CLK_GEN_CLR) {
           uptr->CLK_REG = 0;
           uptr->STAT_REG = 0;
           sim_cancel(uptr);
        }
        uptr->STAT_REG |= (uint32)(*data & 07);

        if (*data & CLK_ADD_ONE)  {
           if ((uptr->STAT_REG & CLK_EN) == 0) {
              uptr->CLK_REG++;
              dk_test(uptr);
           }
        }

        if (*data & CLK_SET_EN)
           uptr->STAT_REG |= CLK_EN;
        if (*data & CLK_CLR_EN)
           uptr->STAT_REG &= ~CLK_EN;
        if (*data & CLK_SET_OVF)
           uptr->STAT_REG |= CLK_OVF;
        if (*data & CLK_CLR_OVF)
           uptr->STAT_REG &= ~CLK_OVF;
        if (*data & CLK_SET_FLG)
           uptr->STAT_REG |= CLK_FLG;
        if (*data & CLK_CLR_FLG)
           uptr->STAT_REG &= ~CLK_FLG;
        if (*data & CLK_SET_PI)
           uptr->STAT_REG |= CLK_PI;
        if (*data & CLK_CLR_PI)
           uptr->STAT_REG &= ~CLK_PI;

        if ((uptr->STAT_REG & CLK_EN) != 0 &&
                (uptr->STAT_REG & (CLK_FLG|CLK_OVF))) {
           set_interrupt(dev, uptr->STAT_REG);
        }

set_clock:
        if (sim_is_active(uptr)) {  /* Save current clock time */
           us = sim_activate_time_usecs(uptr);
           uptr->CLK_REG += uptr->CLK_TIM - (uint32)(us / 10.0);
           sim_cancel(uptr);
        }
        if (uptr->INT_REG == uptr->CLK_REG) {
           uptr->STAT_REG |= CLK_FLG; 
           set_interrupt(dev, uptr->STAT_REG);
        }
        if (uptr->STAT_REG & CLK_EN) {
           if (uptr->INT_REG < uptr->CLK_REG)  /* Count until overflow */
               uptr->CLK_TIM = 01000000;
           else
               uptr->CLK_TIM = uptr->INT_REG;
           t = uptr->CLK_TIM - uptr->CLK_REG;
           us = (double)(t) * 10.0;
           sim_activate_after_d(uptr, us);
        } else {
           sim_cancel(uptr);
        }
        sim_debug(DEBUG_CONO, &dk_dev, "DK %03o CONO %06o PC=%06o %06o\n",
               dev, (uint32)*data, PC, uptr->STAT_REG);
        break;

    case DATAO:
        uptr->INT_REG = (uint32)(*data & RMASK);
        sim_debug(DEBUG_DATAIO, &dk_dev, "DK %03o DATO %012llo PC=%06o\n",
                    dev, *data, PC);
        goto set_clock;

    case DATAI:
        if (sim_is_active(uptr)) {  /* Save current clock time */
           double us = sim_activate_time_usecs(uptr);
           uptr->CLK_REG += uptr->CLK_TIM - (uint32)(us / 10.0);
           sim_cancel(uptr);
        }
        *data = (uint64)(uptr->CLK_REG);
        sim_debug(DEBUG_DATAIO, &dk_dev, "DK %03o DATI %012llo PC=%06o\n",
                    dev, *data, PC);
        goto set_clock;
    }

    return SCPE_OK;
}

/* Bump counter by 1 */
void dk_test (UNIT *uptr)
{
    int   dev;
    if (uptr->CLK_REG & (~RMASK))
       uptr->STAT_REG |= CLK_OVF;
    uptr->CLK_REG &= RMASK;
    if (uptr->INT_REG == uptr->CLK_REG)
       uptr->STAT_REG |= CLK_FLG;
    if (uptr->STAT_REG & (CLK_FLG|CLK_OVF)) {
       dev = ((uptr - dk_unit) << 2) + DK_DEVNUM;
       set_interrupt(dev, uptr->STAT_REG);
    }
}

/* Timer service - */
t_stat dk_svc (UNIT *uptr)
{
    uptr->CLK_REG = uptr->CLK_TIM;
    dk_test (uptr);
    return SCPE_OK;
}

const char *dk_description (DEVICE *dptr)
{
return "DK10 Timer module";
}


#endif
