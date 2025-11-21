/* s100_po.c: MITS Altair 8800 Programmed Output

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

#include "sim_defs.h"
#include "s100_bus.h"
#include "s100_po.h"

#define DEVICE_NAME "PO"

static int32 poc = TRUE;       /* Power On Clear */

static int32 PO = 0;               /* programmed output register */

static t_stat po_reset             (DEVICE *dptr);
static int32 po_io                 (const int32 addr, const int32 rw, const int32 data);

static t_stat po_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);

static const char* po_description(DEVICE *dptr) {
    return "Front Panel";
}

static UNIT po_unit = {
    UDATA (NULL, UNIT_PO_VERBOSE, 0)
};

static REG po_reg[] = {
    { HRDATAD (PO,  PO,  8, "Programmed Output") },
    { NULL }
};

static MTAB po_mod[] = {
    { UNIT_PO_VERBOSE,     UNIT_PO_VERBOSE, "VERBOSE", "VERBOSE", NULL, NULL,
        NULL, "Enable verbose messages"  },
    { UNIT_PO_VERBOSE,     0,               "QUIET",   "QUIET",   NULL, NULL,
        NULL, "Disable verbose messages" },
    { 0 }
};

/* Debug Flags */
static DEBTAB po_dt[] = {
    { NULL, 0 }
};

DEVICE po_dev = {
    DEVICE_NAME, &po_unit, po_reg, po_mod,
    1, ADDRRADIX, ADDRWIDTH, 1, DATARADIX, DATAWIDTH,
    NULL, NULL, &po_reset,
    NULL, NULL, NULL,
    NULL, (DEV_DISABLE | DEV_DIS), 0,
    po_dt, NULL, NULL, &po_show_help, NULL, NULL, &po_description
};

static t_stat po_reset(DEVICE *dptr) {
    if (dptr->flags & DEV_DIS) {    /* Disable Device */
        s100_bus_remio_out(0xff, 1, &po_io);

        poc = TRUE;
    }
    else {
        if (poc) {
            s100_bus_addio_out(0xff, 1, &po_io, DEVICE_NAME);

            poc = FALSE;
        }
    }

    return SCPE_OK;
}

static int32 po_io(const int32 addr, const int32 rw, const int32 data)
{
    if (rw == S100_IO_WRITE) {

        PO = data & DATAMASK;

        if (po_unit.flags & UNIT_PO_VERBOSE) {
            sim_printf("\n[PO %02X]\n", ~data & DATAMASK);    /* IMSAI FP is Inverted */
        }
    }

    return 0x0ff;
}

static t_stat po_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "\nProgrammed Output (%s)\n", dptr->name);

    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    fprint_reg_help (st, dptr);

    return SCPE_OK;
}

