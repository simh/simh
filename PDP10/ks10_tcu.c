/* ks10_tcu.c: PDP-10

   Copyright (c) 2021, Richard Cornwell

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
#include "sim_sock.h"
#include "sim_tmxr.h"

#ifndef NUM_DEVS_TCU
#define NUM_DEVS_TCU 0
#endif

#if (NUM_DEVS_TCU > 0)

#define UNIT_V_Y2K      (UNIT_V_UF + 0)                 /* Y2K compliant OS */
#define UNIT_Y2K        (1u << UNIT_V_Y2K)


int    tcu_write(DEVICE *dptr, t_addr addr, uint16 data, int32 access);
int    tcu_read(DEVICE *dptr, t_addr addr, uint16 *data, int32 access);
t_stat tcu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
        const char *cptr);
const char *tcu_description (DEVICE *dptr);
DIB tcu_dib = { 0760770, 07, 0, 0, 3, &tcu_read, &tcu_write, NULL, 0, 0 };

UNIT tcu_unit = {UDATA (NULL, UNIT_IDLE+UNIT_DISABLE, 0)};

MTAB tcu_mod[] = {
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "addr", "addr",  &uba_set_addr, uba_show_addr,
              NULL, "Sets address of TCU" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "ctl", "ctl",  &uba_set_ctl, uba_show_ctl,
              NULL, "Sets uba of TCU" },
    { UNIT_Y2K, 0, "non Y2K OS", "NOY2K", NULL },
    { UNIT_Y2K, UNIT_Y2K, "Y2K OS", "Y2K", NULL },
    { 0 }
    };

DEVICE tcu_dev = {
    "TIM", &tcu_unit, NULL, tcu_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &tcu_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &tcu_help, NULL, NULL, &tcu_description
    };


/* Time can't be set in device */
int
tcu_write(DEVICE *dptr, t_addr addr, uint16 data, int32 access)
{
    if ((dptr->units[0].flags & UNIT_DIS) != 0)
       return 1;

    return 0;
}

/* Read current time of day */
int
tcu_read(DEVICE *dptr, t_addr addr, uint16 *data, int32 access)
{
    struct pdp_dib   *dibp = (DIB *)dptr->ctxt;
    time_t            curtim;
    struct tm        *tptr;

    if ((dptr->units[0].flags & UNIT_DIS) != 0)
       return 1;

    /* Get time */
    curtim = sim_get_time (NULL);
    tptr = localtime (&curtim);
    if (tptr == NULL)
        return 0;
    if ((tptr->tm_year > 99) && !(dptr->units[0].flags & UNIT_Y2K))
        tptr->tm_year = 99;                               /* Y2K prob? */

    switch (addr & 06) {

    case 0:                                             /* year/month/day */
        *data = (((tptr->tm_year) & 0177) << 9) |
                (((tptr->tm_mon + 1) & 017) << 5) |
                ((tptr->tm_mday) & 037);
        break;

    case 2:                                             /* hour/minute */
        *data = (((tptr->tm_hour) & 037) << 8) |
                ((tptr->tm_min) & 077);
        break;

    case 4:                                             /* second */
        *data = (tptr->tm_sec) & 077;
        break;

    case 6:                                             /* status */
        *data = 0200;
        break;
        }

    sim_debug(DEBUG_DETAIL, dptr, "TCU read %06o %06o %o\n",
             addr, *data, access);
    return 0;
}


t_stat tcu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
return SCPE_OK;
}

const char *tcu_description (DEVICE *dptr)
{
return "TCU150 Time of day clock";
}

#endif
