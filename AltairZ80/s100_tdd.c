/*************************************************************************
 *                                                                       *
 * Copyright (c) 2022 Howard M. Harte.                                   *
 * https://github.com/hharte                                             *
 *                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining *
 * a copy of this software and associated documentation files (the       *
 * "Software"), to deal in the Software without restriction, including   *
 * without limitation the rights to use, copy, modify, merge, publish,   *
 * distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to *
 * the following conditions:                                             *
 *                                                                       *
 * The above copyright notice and this permission notice shall be        *
 * included in all copies or substantial portions of the Software.       *
 *                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-            *
 * INFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE   *
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN       *
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN     *
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE      *
 * SOFTWARE.                                                             *
 *                                                                       *
 * Except as contained in this notice, the names of The Authors shall    *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * from the Authors.                                                     *
 *                                                                       *
 * Based on s100_64fdc.c                                                 *
 *                                                                       *
 * Module Description:                                                   *
 *     Tarbell Double-Density Floppy Controller module for SIMH.         *
 * This module is a wrapper around the wd179x FDC module.                *
 *                                                                       *
 * Reference:                                                            *
 * http://www.bitsavers.org/pdf/tarbell/Tarbell_Double_Density_Floppy_Disk_Interface_Jul81.pdf
 *                                                                       *
 *************************************************************************/

#include "altairz80_defs.h"
#include "sim_defs.h"
#include "wd179x.h"

#define DEV_NAME    "TDD"

/* Debug flags */
#define STATUS_MSG  (1 << 0)
#define DRIVE_MSG   (1 << 1)
#define VERBOSE_MSG (1 << 2)
#define IRQ_MSG     (1 << 3)

#define TDD_MAX_DRIVES  4

#define TDD_IO_BASE     0x7C
#define TDD_IO_SIZE     0x2
#define TDD_IO_MASK     (TDD_IO_SIZE - 1)

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
} TDD_INFO;

extern WD179X_INFO_PUB *wd179x_infop;

static TDD_INFO tdd_info_data = { { 0x0000, 0, TDD_IO_BASE, TDD_IO_SIZE } };

extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);

extern uint32 PCX;      /* external view of PC  */

#define TDD_CAPACITY        (77*1*26*128)   /* Default SSSD 8" (IBM 3740) Disk Capacity */

static t_stat tdd_reset(DEVICE *tdd_dev);

static int32 tdd_control(const int32 port, const int32 io, const int32 data);
static const char* tdd_description(DEVICE *dptr);

#define TDD_FLAG_EOJ        (1 << 7)    /* End of Job (INTRQ) */

static UNIT tdd_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TDD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TDD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TDD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TDD_CAPACITY) }
};

static REG tdd_reg[] = {
    { NULL }
};

#define TDD_NAME    "Tarbell Double-Density FDC"

static const char* tdd_description(DEVICE *dptr) {
    if (dptr == NULL) {
        return NULL;
    }
    return TDD_NAME;
}

static MTAB tdd_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"        },
    { 0 }
};

/* Debug Flags */
static DEBTAB tdd_dt[] = {
    { "STATUS",     STATUS_MSG,     "Status messages"   },
    { "DRIVE",      DRIVE_MSG,      "Drive messages"    },
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { "IRQ",        IRQ_MSG,        "IRQ messages"      },
    { NULL,         0                                   }
};

DEVICE tdd_dev = {
    DEV_NAME, tdd_unit, tdd_reg, tdd_mod,
    TDD_MAX_DRIVES, 10, 31, 1, TDD_MAX_DRIVES, TDD_MAX_DRIVES,
    NULL, NULL, &tdd_reset,
    NULL, &wd179x_attach, &wd179x_detach,
    &tdd_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), 0,
    tdd_dt, NULL, NULL, NULL, NULL, NULL, &tdd_description
};

/* Reset routine */
static t_stat tdd_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect ROM and I/O Ports */
        /* Unmap I/O Ports */
        sim_map_resource(pnp->io_base, 1, RESOURCE_TYPE_IO, &tdd_control, "tdd_control", TRUE);
    } else {
        /* Connect TDD Disk Flags and Control Register */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &tdd_control, "tdd_control", FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }

    return SCPE_OK;
}

/* Tarbell pp. 12-5 Disk Control/Status */
static int32 tdd_control(const int32 port, const int32 io, const int32 data)
{
    int32 result = 0;
    if(io) { /* I/O Write */
        if ((port & TDD_IO_MASK) == 0) {
            wd179x_infop->fdc_head = (data & 0x40) >> 6;
            wd179x_infop->sel_drive = (data & 0x30) >> 4;
            wd179x_infop->ddens = (data & 0x08) >> 3;

            sim_debug(DRIVE_MSG, &tdd_dev, DEV_NAME ": " ADDRESS_FORMAT " WR CTRL(0x%02x)  = 0x%02x: Drive: %d, Head: %d, %s-Density.\n",
                PCX, port,
                data & 0xFF,
                wd179x_infop->sel_drive,
                wd179x_infop->fdc_head,
                wd179x_infop->ddens == 1 ? "Double" : "Single");
        } else {
            sim_debug(STATUS_MSG, &tdd_dev, DEV_NAME ": " ADDRESS_FORMAT
                " Write Extended Address, Port 0x%02x=0x%02x\n", PCX, port, data);
        }
    } else { /* I/O Read */
        if ((port & TDD_IO_MASK) == 0) {
            result = (wd179x_infop->intrq) ? 0 : TDD_FLAG_EOJ;
            sim_debug(STATUS_MSG, &tdd_dev, DEV_NAME ": " ADDRESS_FORMAT
                " Read EOJ, Port 0x%02x Result 0x%02x\n", PCX, port, result);
        } else {
            result = (wd179x_infop->drq) ? TDD_FLAG_EOJ : 0;
            sim_debug(STATUS_MSG, &tdd_dev, DEV_NAME ": " ADDRESS_FORMAT
                " Read DRQ, Port 0x%02x Result 0x%02x\n", PCX, port, result);
        }
    }

    return result;
}
