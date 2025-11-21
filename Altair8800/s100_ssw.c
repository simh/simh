/* s100_ssw.c: MITS Altair 8800 Sense Switches

   Copyright (c) 2025 Patrick A. Linstruth

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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Patrick Linstruth shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Patrick Linstruth.

   History:
   07-Nov-2025 Initial version

*/

#include "s100_bus.h"
#include "s100_ssw.h"

#define DEVICE_NAME "SSW"

static int32 poc = TRUE;       /* Power On Clear */

static int32 SSW = 0;              /* sense switch register */

static t_stat ssw_reset             (DEVICE *dptr);
static int32 ssw_io                 (const int32 addr, const int32 rw, const int32 data);

static t_stat ssw_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);

static const char* ssw_description(DEVICE *dptr) {
    return "Front Panel Sense Switches";
}

static UNIT ssw_unit = {
    UDATA (NULL, 0, 0)
};

static REG ssw_reg[] = {
    { HRDATAD (SSWVAL, SSW, 8, "Front panel sense switches pseudo register") },
    { NULL }
};

static MTAB ssw_mod[] = {
    { 0 }
};

/* Debug Flags */
static DEBTAB ssw_dt[] = {
    { NULL, 0 }
};

DEVICE ssw_dev = {
    DEVICE_NAME, &ssw_unit, ssw_reg, ssw_mod,
    1, ADDRRADIX, ADDRWIDTH, 1, DATARADIX, DATAWIDTH,
    NULL, NULL, &ssw_reset,
    NULL, NULL, NULL,
    NULL, (DEV_DISABLE), 0,
    ssw_dt, NULL, NULL,
    &ssw_show_help, NULL, NULL,
    &ssw_description
};

static t_stat ssw_reset(DEVICE *dptr) {
    if (dptr->flags & DEV_DIS) {    /* Disable Device */
        s100_bus_remio_in(0xff, 1, &ssw_io);

        poc = TRUE;
    }
    else {
        if (poc) {
            s100_bus_addio_in(0xff, 1, &ssw_io, DEVICE_NAME);

            poc = FALSE;
        }
    }

    return SCPE_OK;
}

static int32 ssw_io(const int32 addr, const int32 rw, const int32 data)
{
    if (rw == S100_IO_READ) {
        return SSW;
    }

    return 0x0ff;
}

static t_stat ssw_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "\nAltair 8800 Front Panel Sense Switches (%s)\n", dptr->name);

    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    fprint_reg_help (st, dptr);

    fprintf (st, "\nUse DEP SSWVAL <val> to set the value returned by an IN 0FFH instruction.\n\n");

    return SCPE_OK;
}

