/*************************************************************************
 *                                                                       *
 * $Id: s100_selchan.c 1995 2008-07-15 03:59:13Z hharte $                *
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
 *     CompuPro Selector Channel module for SIMH.                        *
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
#define VERBOSE_MSG (1 << 0)
#define DMA_MSG     (1 << 1)

#define SELCHAN_MAX_DRIVES  1

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
    uint32  selchan;    /* Selector Channel Register */
    uint32  dma_addr;   /* DMA Transfer Address */
    uint32  dma_mode;   /* DMA Mode register */
    uint8   reg_cnt;    /* Counter for selchan register */
} SELCHAN_INFO;

static SELCHAN_INFO selchan_info_data = { { 0x0, 0, 0xF0, 1 } };
static SELCHAN_INFO *selchan_info = &selchan_info_data;
int32 selchan_dma(uint8 *buf, uint32 len);

extern t_stat set_iobase(UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);
extern uint32 PCX;

/* These are needed for DMA. */
extern void PutByteDMA(const uint32 Addr, const uint32 Value);
extern uint8 GetByteDMA(const uint32 Addr);

static t_stat selchan_reset(DEVICE *selchan_dev);

static int32 selchandev(const int32 port, const int32 io, const int32 data);


static UNIT selchan_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_ROABLE, 0) }
};

static REG selchan_reg[] = {
    { HRDATAD (DMA_MODE, selchan_info_data.dma_mode, 8,     "DMA mode register"),               },
    { HRDATAD (DMA_ADDR, selchan_info_data.dma_addr, 24,    "DMA transfer address register"),   },
    { NULL }
};

static MTAB selchan_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "IOBASE", "IOBASE", &set_iobase, &show_iobase, NULL,
        "Sets disk controller I/O base address"                                     },
    { 0 }
};

/* Debug Flags */
static DEBTAB selchan_dt[] = {
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { "DMA",        DMA_MSG,        "DMA messages"      },
    { NULL,         0                                   }
};

DEVICE selchan_dev = {
    "SELCHAN", selchan_unit, selchan_reg, selchan_mod,
    SELCHAN_MAX_DRIVES, 10, 31, 1, SELCHAN_MAX_DRIVES, SELCHAN_MAX_DRIVES,
    NULL, NULL, &selchan_reset,
    NULL, NULL, NULL,
    &selchan_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), 0,
    selchan_dt, NULL, "Compupro Selector Channel SELCHAN"
};

/* Reset routine */
static t_stat selchan_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &selchandev, TRUE);
    } else {
        /* Connect SELCHAN at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &selchandev, FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }
    return SCPE_OK;
}

#define SELCHAN_MODE_WRITE  0x80    /* Selector Channel Memory or I/O Write */
#define SELCHAN_MODE_IO     0x40    /* Set if I/O Access, otherwise memory */
#define SELCHAN_MODE_CNT_UP 0x20    /* Set = DMA Address Count Up, otherwise down. (Mem only */
#define SELCHAN_MODE_WAIT   0x10    /* Insert one wait state. */
#define SELCHAN_MODE_DMA_MASK   0x0F    /* Mask for DMA Priority field */

static int32 selchandev(const int32 port, const int32 io, const int32 data)
{
    DBG_PRINT(("SELCHAN: IO %s, Port %02x" NLP, io ? "WR" : "RD", port));
    if(io) {
        selchan_info->selchan <<= 8;
        selchan_info->selchan &= 0xFFFFFF00;
        selchan_info->selchan |= data;

        selchan_info->dma_addr = (selchan_info->selchan & 0xFFFFF00) >> 8;
        selchan_info->dma_mode = (selchan_info->selchan & 0xFF);

        selchan_info->reg_cnt ++;

        if(selchan_info->reg_cnt == 4) {
            sim_debug(VERBOSE_MSG, &selchan_dev, "SELCHAN: " ADDRESS_FORMAT " DMA=0x%06x, Mode=0x%02x (%s, %s, %s)\n", PCX, selchan_info->dma_addr, selchan_info->dma_mode, selchan_info->dma_mode & SELCHAN_MODE_WRITE ? "WR" : "RD", selchan_info->dma_mode & SELCHAN_MODE_IO ? "I/O" : "MEM", selchan_info->dma_mode & SELCHAN_MODE_IO ? "FIX" : selchan_info->dma_mode & SELCHAN_MODE_CNT_UP ? "INC" : "DEC");
        }

        return 0;
    } else {
        sim_debug(VERBOSE_MSG, &selchan_dev, "SELCHAN: " ADDRESS_FORMAT " Reset\n", PCX);
        selchan_info->reg_cnt = 0;
        return(0xFF);
    }
}

int32 selchan_dma(uint8 *buf, uint32 len)
{
    uint32 i;

    if(selchan_info->reg_cnt != 4) {
        sim_printf("SELCHAN: " ADDRESS_FORMAT " Programming error: selector channel disabled." NLP,
            PCX);
        return (-1);
    }

    if(selchan_info->dma_mode & SELCHAN_MODE_IO)
    {
        sim_printf("SELCHAN: " ADDRESS_FORMAT " I/O Not supported" NLP, PCX);
        return (-1);
    } else {
        sim_debug(DMA_MSG, &selchan_dev, "SELCHAN: " ADDRESS_FORMAT " DMA %s Transfer, len=%d\n", PCX, (selchan_info->dma_mode & SELCHAN_MODE_WRITE) ? "WR" : "RD", len);
        for(i=0;i<len;i++) {
            if(selchan_info->dma_mode & SELCHAN_MODE_WRITE) {
                PutByteDMA(selchan_info->dma_addr + i, buf[i]);
            } else {
                buf[i] = GetByteDMA(selchan_info->dma_addr + i);
            }
        }

        if(selchan_info->dma_mode & SELCHAN_MODE_CNT_UP) {
            selchan_info->dma_addr += i;
        } else {
            selchan_info->dma_addr -= i;
        }
    }

    return(0);
}
