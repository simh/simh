/*************************************************************************
 *                                                                       *
 * $Id: s100_mdriveh.c 1940 2008-06-13 05:28:57Z hharte $                *
 *                                                                       *
 * Copyright (c) 2007-2008 Howard M. Harte.                              *
 * http://www.hartetec.com                                               *
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                 *
 * NONINFRINGEMENT. IN NO EVENT SHALL HOWARD M. HARTE BE LIABLE FOR ANY  *
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  *
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     *
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                *
 *                                                                       *
 * Except as contained in this notice, the name of Howard M. Harte shall *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * Howard M. Harte.                                                      *
 *                                                                       *
 * SIMH Interface based on altairz80_hdsk.c, by Peter Schorn.            *
 *                                                                       *
 * Module Description:                                                   *
 *     CompuPro M-DRIVE/H Controller module for SIMH.                    *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

/*#define DBG_MSG */

#include "altairz80_defs.h"

#if defined (_WIN32)
#include <windows.h>
#endif

#ifdef DBG_MSG
#define DBG_PRINT(args) sim_printf args
#else
#define DBG_PRINT(args)
#endif

/* Debug flags */
#define SEEK_MSG    (1 << 0)
#define RD_DATA_MSG (1 << 1)
#define WR_DATA_MSG (1 << 2)
#define VERBOSE_MSG (1 << 3)

#define MDRIVEH_MAX_DRIVES    8

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
    uint32 dma_addr;    /* DMA Transfer Address */
    UNIT uptr[MDRIVEH_MAX_DRIVES];
    uint8 *storage[MDRIVEH_MAX_DRIVES];
} MDRIVEH_INFO;

static MDRIVEH_INFO mdriveh_info_data = { { 0x0, 0, 0xC6, 2 } };
static MDRIVEH_INFO *mdriveh_info = &mdriveh_info_data;

extern uint32 PCX;
extern t_stat set_iobase(UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);

#define UNIT_V_MDRIVEH_WLK      (UNIT_V_UF + 0) /* write locked                             */
#define UNIT_MDRIVEH_WLK        (1 << UNIT_V_MDRIVEH_WLK)
#define UNIT_V_MDRIVEH_VERBOSE  (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_MDRIVEH_VERBOSE    (1 << UNIT_V_MDRIVEH_VERBOSE)
#define MDRIVEH_CAPACITY        (512 * 1000)    /* Default M-DRIVE/H Capacity               */
#define MDRIVEH_NONE            0

static t_stat mdriveh_reset(DEVICE *mdriveh_dev);
static int32 mdrivehdev(const int32 port, const int32 io, const int32 data);
static uint8 MDRIVEH_Read(const uint32 Addr);
static uint8 MDRIVEH_Write(const uint32 Addr, uint8 cData);

static UNIT mdriveh_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_ROABLE, MDRIVEH_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_DIS + UNIT_ROABLE, MDRIVEH_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_DIS + UNIT_ROABLE, MDRIVEH_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_DIS + UNIT_ROABLE, MDRIVEH_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_DIS + UNIT_ROABLE, MDRIVEH_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_DIS + UNIT_ROABLE, MDRIVEH_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_DIS + UNIT_ROABLE, MDRIVEH_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_DIS + UNIT_ROABLE, MDRIVEH_CAPACITY) }
};

static REG mdriveh_reg[] = {
    { NULL }
};

#define MDRIVEH_NAME    "Compupro Memory Drive MDRIVEH"

static MTAB mdriveh_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"    },
    { UNIT_MDRIVEH_WLK,     0,                      "WRTENB",   "WRTENB",
        NULL, NULL, NULL,
        "Enables " MDRIVEH_NAME "n for writing"                                     },
    { UNIT_MDRIVEH_WLK,     UNIT_MDRIVEH_WLK,       "WRTLCK",   "WRTLCK",
        NULL, NULL, NULL,
        "Locks " MDRIVEH_NAME "n for writing"                                       },
    /* quiet, no warning messages       */
    { UNIT_MDRIVEH_VERBOSE, 0,                      "QUIET",    "QUIET",
        NULL, NULL, NULL, "No verbose messages for unit " MDRIVEH_NAME "n"          },
    /* verbose, show warning messages   */
    { UNIT_MDRIVEH_VERBOSE, UNIT_MDRIVEH_VERBOSE,   "VERBOSE",  "VERBOSE",
        NULL, NULL, NULL, "Verbose messages for unit " MDRIVEH_NAME "n"             },
    { 0 }
};

/* Debug Flags */
static DEBTAB mdriveh_dt[] = {
    { "SEEK",       SEEK_MSG,       "Seek messages"     },
    { "READ",       RD_DATA_MSG,    "Read messages"     },
    { "WRITE",      WR_DATA_MSG,    "Write messages"    },
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { NULL,         0                                   }
};

DEVICE mdriveh_dev = {
    "MDRIVEH", mdriveh_unit, mdriveh_reg, mdriveh_mod,
    MDRIVEH_MAX_DRIVES, 10, 31, 1, MDRIVEH_MAX_DRIVES, MDRIVEH_MAX_DRIVES,
    NULL, NULL, &mdriveh_reset,
    NULL, NULL, NULL,
    &mdriveh_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), 0,
    mdriveh_dt, NULL, MDRIVEH_NAME
};


/* Reset routine */
static t_stat mdriveh_reset(DEVICE *dptr)
{
    uint8 i;
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect ROM and I/O Ports */
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &mdrivehdev, TRUE);
    } else {
        /* Connect MDRIVEH at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &mdrivehdev, FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }

    for(i=0; i<MDRIVEH_MAX_DRIVES; i++) {
        mdriveh_info->uptr[i] = dptr->units[i];
        if((dptr->flags & DEV_DIS) || (dptr->units[i].flags & UNIT_DIS)) {
            if (dptr->units[i].flags & UNIT_MDRIVEH_VERBOSE)
                sim_printf("MDRIVEH: Unit %d disabled", i);
            if(mdriveh_info->storage[i] != NULL) {
                if (dptr->units[i].flags & UNIT_MDRIVEH_VERBOSE)
                    sim_printf(", freed 0x%p\n", mdriveh_info->storage[i]);
                free(mdriveh_info->storage[i]);
                mdriveh_info->storage[i] = NULL;
            } else if (dptr->units[i].flags & UNIT_MDRIVEH_VERBOSE) {
                sim_printf(".\n");
            }
        } else {
            if(mdriveh_info->storage[i] == NULL) {
                mdriveh_info->storage[i] = calloc(1, 524288);
            }
            if (dptr->units[i].flags & UNIT_MDRIVEH_VERBOSE)
                sim_printf("MDRIVEH: Unit %d enabled, 512K at 0x%p\n", i, mdriveh_info->storage[i]);
        }
    }

    return SCPE_OK;
}

static int32 mdrivehdev(const int32 port, const int32 io, const int32 data)
{
    DBG_PRINT(("MDRIVEH: " ADDRESS_FORMAT " IO %s, Port %02x" NLP, PCX, io ? "WR" : "RD", port));
    if(io) {
        MDRIVEH_Write(port, data);
        return 0;
    } else {
        return(MDRIVEH_Read(port));
    }
}

#define MDRIVEH_DATA    0   /* R=Drive Status Register / W=DMA Address Register */
#define MDRIVEH_ADDR    1   /* R=Unused / W=Motor Control Register */

static uint8 MDRIVEH_Read(const uint32 Addr)
{
    uint8 cData;
    uint8 unit;
    uint32 offset;

    cData = 0xFF;   /* default is High-Z Data */

    switch(Addr & 0x1) {
        case MDRIVEH_ADDR:
            sim_debug(VERBOSE_MSG, &mdriveh_dev, "MDRIVEH: " ADDRESS_FORMAT " RD Addr = 0x%02x\n", PCX, cData);
            break;
        case MDRIVEH_DATA:
            unit = (mdriveh_info->dma_addr & 0x380000) >> 19;
            offset = mdriveh_info->dma_addr & 0x7FFFF;

            if(mdriveh_info->storage[unit] != NULL) {
                cData = mdriveh_info->storage[unit][offset];
            }

            sim_debug(RD_DATA_MSG, &mdriveh_dev, "MDRIVEH: " ADDRESS_FORMAT " RD Data [%x:%05x] = 0x%02x\n", PCX, unit, offset, cData);

            /* Increment M-DRIVE/H Data Address */
            mdriveh_info->dma_addr++;
            mdriveh_info->dma_addr &= 0x3FFFFF;
            break;
    }

    return (cData);
}

static uint8 MDRIVEH_Write(const uint32 Addr, uint8 cData)
{
    uint8 result = 0;
    uint8 unit;
    uint32 offset;

    switch(Addr & 0x1) {
        case MDRIVEH_ADDR:
            mdriveh_info->dma_addr <<= 8;
            mdriveh_info->dma_addr &= 0x003FFF00;
            mdriveh_info->dma_addr |= cData;
            sim_debug(SEEK_MSG, &mdriveh_dev, "MDRIVEH: " ADDRESS_FORMAT " DMA Address=%06x\n", PCX, mdriveh_info->dma_addr);
            break;
        case MDRIVEH_DATA:
            unit = (mdriveh_info->dma_addr & 0x380000) >> 19;
            offset = mdriveh_info->dma_addr & 0x7FFFF;

            if(mdriveh_info->storage[unit] != NULL) {
                if(mdriveh_info->uptr[unit].flags & UNIT_MDRIVEH_WLK) {
                    sim_debug(WR_DATA_MSG, &mdriveh_dev, "MDRIVEH: " ADDRESS_FORMAT " WR Data [%x:%05x] = Unit Write Locked\n", PCX, unit, offset);
                } else {
                    sim_debug(WR_DATA_MSG, &mdriveh_dev, "MDRIVEH: " ADDRESS_FORMAT " WR Data [%x:%05x] = 0x%02x\n", PCX, unit, offset, cData);
                    mdriveh_info->storage[unit][offset] = cData;
                }
            } else {
                sim_debug(WR_DATA_MSG, &mdriveh_dev, "MDRIVEH: " ADDRESS_FORMAT " WR Data [%x:%05x] = Unit OFFLINE\n", PCX, unit, offset);
            }

            /* Increment M-DRIVE/H Data Address */
            mdriveh_info->dma_addr++;
            mdriveh_info->dma_addr &= 0x3FFFFF;
            break;
    }

    return (result);
}

