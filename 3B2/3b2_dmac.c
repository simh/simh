/* 3b2_dmac.c: AT&T 3B2 Model 400 AM9517A DMA Controller Implementation

   Copyright (c) 2017, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.
*/

#include "3b2_dmac.h"

DMA_STATE dma_state;

UNIT dmac_unit[] = {
    { UDATA (NULL, 0, 0), 0, 0 },
    { UDATA (NULL, 0, 0), 0, 1 },
    { UDATA (NULL, 0, 0), 0, 2 },
    { UDATA (NULL, 0, 0), 0, 3 },
    { NULL }
};

REG dmac_reg[] = {
    { NULL }
};

DEVICE dmac_dev = {
    "DMAC", dmac_unit, dmac_reg, NULL,
    1, 16, 8, 4, 16, 32,
    NULL, NULL, &dmac_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

dmac_dma_handler device_dma_handlers[] = {
    {DMA_ID_CHAN,  IDBASE+ID_DATA_REG,  &id_drq,         dmac_generic_dma, id_after_dma},
    {DMA_IF_CHAN,  IFBASE+IF_DATA_REG,  &if_state.drq,   dmac_generic_dma, if_after_dma},
    {DMA_IUA_CHAN, IUBASE+IUA_DATA_REG, &iu_console.drq, iu_dma,           NULL},
    {DMA_IUB_CHAN, IUBASE+IUB_DATA_REG, &iu_contty.drq,  iu_dma,           NULL},
    {0,            0,                   NULL,            NULL,             NULL }
};

t_stat dmac_reset(DEVICE *dptr)
{
    int i;

    memset(&dma_state, 0, sizeof(dma_state));

    for (i = 0; i < 4; i++) {
        dma_state.channels[i].page = 0;
        dma_state.channels[i].addr = 0;
        dma_state.channels[i].wcount = 0;
        dma_state.channels[i].addr_c = 0;
        dma_state.channels[i].wcount_c = 0;
        dma_state.channels[i].ptr = 0;
    }

    return SCPE_OK;
}

uint32 dmac_read(uint32 pa, size_t size)
{
    uint8 reg, base, data;

    base =(uint8) (pa >> 12);
    reg = pa & 0xff;

    switch (base) {
    case DMA_C:     /* 0x48xxx */
        switch (reg) {
        case 0: /* channel 0 current address reg */
            data = ((dma_state.channels[0].addr_c) >> (dma_state.bff * 8)) & 0xff;
            sim_debug(READ_MSG, &dmac_dev,
                      "[%08x] Reading Channel 0 Addr Reg: %08x\n",
                      R[NUM_PC], data);
            dma_state.bff ^= 1;
            break;
        case 1: /* channel 0 current address reg */
            data = ((dma_state.channels[0].wcount_c) >> (dma_state.bff * 8)) & 0xff;
            sim_debug(READ_MSG, &dmac_dev,
                      "[%08x] Reading Channel 0 Addr Count Reg: %08x\n",
                      R[NUM_PC], data);
            dma_state.bff ^= 1;
            break;
        case 2: /* channel 1 current address reg */
            data = ((dma_state.channels[1].addr_c) >> (dma_state.bff * 8)) & 0xff;
            sim_debug(READ_MSG, &dmac_dev,
                      "[%08x] Reading Channel 1 Addr Reg: %08x\n",
                      R[NUM_PC], data);
            dma_state.bff ^= 1;
            break;
        case 3: /* channel 1 current address reg */
            data = ((dma_state.channels[1].wcount_c) >> (dma_state.bff * 8)) & 0xff;
            sim_debug(READ_MSG, &dmac_dev,
                      "[%08x] Reading Channel 1 Addr Count Reg: %08x\n",
                      R[NUM_PC], data);
            dma_state.bff ^= 1;
            break;
        case 4: /* channel 2 current address reg */
            data = ((dma_state.channels[2].addr_c) >> (dma_state.bff * 8)) & 0xff;
            sim_debug(READ_MSG, &dmac_dev,
                      "[%08x] Reading Channel 2 Addr Reg: %08x\n",
                      R[NUM_PC], data);
            dma_state.bff ^= 1;
            break;
        case 5: /* channel 2 current address reg */
            data = ((dma_state.channels[2].wcount_c) >> (dma_state.bff * 8)) & 0xff;
            sim_debug(READ_MSG, &dmac_dev,
                      "[%08x] Reading Channel 2 Addr Count Reg: %08x\n",
                      R[NUM_PC], data);
            dma_state.bff ^= 1;
            break;
        case 6: /* channel 3 current address reg */
            data = ((dma_state.channels[3].addr_c) >> (dma_state.bff * 8)) & 0xff;
            sim_debug(READ_MSG, &dmac_dev,
                      "[%08x] Reading Channel 3 Addr Reg: %08x\n",
                      R[NUM_PC], data);
            dma_state.bff ^= 1;
            break;
        case 7: /* channel 3 current address reg */
            data = ((dma_state.channels[3].wcount_c) >> (dma_state.bff * 8)) & 0xff;
            sim_debug(READ_MSG, &dmac_dev,
                      "[%08x] Reading Channel 3 Addr Count Reg: %08x\n",
                      R[NUM_PC], data);
            dma_state.bff ^= 1;
            break;
        case 8:
            data = dma_state.status;
            sim_debug(READ_MSG, &dmac_dev,
                      "[%08x] Reading DMAC Status %08x\n",
                      R[NUM_PC], data);
            dma_state.status = 0;
            break;
        default:
            sim_debug(READ_MSG, &dmac_dev,
                      "[%08x] DMAC READ %lu B @ %08x\n",
                      R[NUM_PC], size, pa);
            data = 0;
        }

        return data;
    default:
        sim_debug(READ_MSG, &dmac_dev,
                  "[%08x] [BASE: %08x] DMAC READ %lu B @ %08x\n",
                  R[NUM_PC], base, size, pa);
        return 0;
    }
}

/*
 * Program the DMAC
 */
void dmac_program(uint8 reg, uint8 val)
{
    uint8 channel_id, i, chan_num;
    dma_channel *channel;

    if (reg < 8) {
        switch (reg) {
        case 0:
        case 1:
            chan_num = 0;
            break;
        case 2:
        case 3:
            chan_num = 1;
            break;
        case 4:
        case 5:
            chan_num = 2;
            break;
        case 6:
        case 7:
            chan_num = 3;
            break;
        }

        channel = &dma_state.channels[chan_num];

        switch (reg & 1) {
        case 0: /* Address */
            channel->addr &= ~(0xff << dma_state.bff * 8);
            channel->addr |= (val & 0xff) << (dma_state.bff * 8);
            channel->addr_c = channel->addr;
            sim_debug(WRITE_MSG, &dmac_dev,
                      "Set address channel %d byte %d = %08x\n",
                      chan_num, dma_state.bff, channel->addr);
            break;
        case 1: /* Word Count */
            channel->wcount &= ~(0xff << dma_state.bff * 8);
            channel->wcount |= (val & 0xff) << (dma_state.bff * 8);
            channel->wcount_c = channel->wcount;
            channel->ptr = 0;
            sim_debug(WRITE_MSG, &dmac_dev,
                      "Set word count channel %d byte %d = %08x\n",
                      chan_num, dma_state.bff, channel->wcount);
            break;
        }

        /* Toggle the byte flip-flop */
        dma_state.bff ^= 1;

        /* Handled. */
        return;
    }

    /* If it hasn't been handled, it must be one of the following
       registers. */

    switch (reg) {
    case 8:  /* Command */
        dma_state.command = val;
        sim_debug(WRITE_MSG, &dmac_dev,
                  "[%08x] Command: val=%02x\n",
                  R[NUM_PC], val);
        break;
    case 9:  /* Request */
        sim_debug(WRITE_MSG, &dmac_dev,
                  "[%08x] Request set: val=%02x\n",
                  R[NUM_PC], val);
        dma_state.request = val;
        break;
    case 10: /* Write Single Mask Register Bit */
        channel_id = val & 3;

        /* "Clear or Set" is bit 2 */
        if ((val >> 2) & 1) {
            dma_state.mask |= (1 << channel_id);
        } else {
            dma_state.mask &= ~(1 << channel_id);
            /* Set the appropriate DRQ */
            /* *dmac_drq_handlers[channel_id].drq = TRUE; */
        }

        sim_debug(WRITE_MSG, &dmac_dev,
                  "[%08x] Write Single Mask Register Bit. channel=%d set/clear=%02x\n",
                  R[NUM_PC], channel_id, (val >> 2) & 1);
        break;
    case 11: /* Mode */
        sim_debug(WRITE_MSG, &dmac_dev,
                  "[%08x] Mode Set. val=%02x\n",
                  R[NUM_PC], val);
        dma_state.mode = val;
        break;
    case 12: /* Clear Byte Pointer Flip/Flop */
        dma_state.bff = 0;
        break;
    case 13: /* Master Clear */
        dma_state.bff = 0;
        dma_state.command = 0;
        dma_state.status = 0;
        for (i = 0; i < 4; i++) {
            dma_state.channels[i].page = 0;
            dma_state.channels[i].addr = 0;
            dma_state.channels[i].wcount = 0;
            dma_state.channels[i].addr_c = 0;
            dma_state.channels[i].wcount_c = 0;
            dma_state.channels[i].ptr = 0;
        }
        break;
    case 15: /* Write All Mask Register Bits */
        sim_debug(WRITE_MSG, &dmac_dev,
                  "[%08x] Write DMAC mask (all bits). Val=%02x\n",
                  R[NUM_PC], val);
        dma_state.mask = val & 0xf;
        break;
    case 16: /* Clear DMAC Interrupt */
        sim_debug(WRITE_MSG, &dmac_dev,
                  "[%08x] Clear DMAC Interrupt in DMAC. val=%02x\n",
                  R[NUM_PC], val);
        break;
    default:
        sim_debug(WRITE_MSG, &dmac_dev,
                  "[%08x] Unhandled DMAC write. reg=%x val=%02x\n",
                  R[NUM_PC], reg, val);
        break;
    }
}

void dmac_page_update(uint8 base, uint8 reg, uint8 val)
{
    uint8 shift = 0;

    /* Sanity check */
    if (reg > 3) {
        return;
    }

    /* The actual register is a 32-bit, byte-addressed register, so
       that address 4x000 is the highest byte, 4x003 is the lowest
       byte. */

    shift = -(reg - 3) * 8;

    switch (base) {
    case DMA_ID:
        sim_debug(WRITE_MSG, &dmac_dev, "Set page channel 0 = %x\n", val);
        dma_state.channels[DMA_ID_CHAN].page &= ~(0xff << shift);
        dma_state.channels[DMA_ID_CHAN].page |= (val << shift);
        break;
    case DMA_IF:
        sim_debug(WRITE_MSG, &dmac_dev, "Set page channel 1 = %x\n", val);
        dma_state.channels[DMA_IF_CHAN].page &= ~(0xff << shift);
        dma_state.channels[DMA_IF_CHAN].page |= (val << shift);
        break;
    case DMA_IUA:
        sim_debug(WRITE_MSG, &dmac_dev, "Set page channel 2 = %x\n", val);
        dma_state.channels[DMA_IUA_CHAN].page &= ~(0xff << shift);
        dma_state.channels[DMA_IUA_CHAN].page |= (val << shift);
        break;
    case DMA_IUB:
        sim_debug(WRITE_MSG, &dmac_dev, "Set page channel 3 = %x\n", val);
        dma_state.channels[DMA_IUB_CHAN].page &= ~(0xff << shift);
        dma_state.channels[DMA_IUB_CHAN].page |= (val << shift);
        break;
    }
}

void dmac_write(uint32 pa, uint32 val, size_t size)
{
    uint8 reg, base;

    base = (uint8) (pa >> 12);
    reg = pa & 0xff;

    switch (base) {
    case DMA_C:     /* 0x48xxx */
        dmac_program(reg, (uint8) val);
        break;

    case DMA_ID:    /* 0x45xxx */
    case DMA_IUA:   /* 0x46xxx */
    case DMA_IUB:   /* 0x47xxx */
    case DMA_IF:    /* 0x4Exxx */
        dmac_page_update(base, reg, (uint8) val);
        break;
    }
}

void dmac_generic_dma(uint8 channel, uint32 service_address)
{
    uint8 data;
    int32 i;
    uint32 addr;
    dma_channel *chan = &dma_state.channels[channel];

    i = (int32) chan->wcount_c;

    /* TODO: This does not handle decrement-mode transfers,
       which don't seem to be used in SVR3 */

    switch ((dma_state.mode >> 2) & 0xf) {
    case DMA_MODE_VERIFY:
        sim_debug(EXECUTE_MSG, &dmac_dev,
                  "[%08x] [dmac_generic_dma channel=%d] unhandled VERIFY request.\n",
                  R[NUM_PC], channel);
        break;
    case DMA_MODE_WRITE:
        sim_debug(EXECUTE_MSG, &dmac_dev,
                  "[%08x] [dmac_generic_dma channel=%d] write: %d bytes from %08x\n",
                  R[NUM_PC], channel,
                  chan->wcount + 1,
                  dma_address(channel, 0, TRUE));
        for (; i >= 0; i--) {
            chan->wcount_c--;
            addr = dma_address(channel, chan->ptr, TRUE);
            chan->addr_c = dma_state.channels[channel].addr + chan->ptr;
            chan->ptr++;
            data = pread_b(service_address);
            write_b(addr, data);
        }
        break;
    case DMA_MODE_READ:
        sim_debug(EXECUTE_MSG, &dmac_dev,
                  "[%08x] [dmac_generic_dma channel=%d] read: %d bytes to %08x\n",
                  R[NUM_PC], channel,
                  chan->wcount + 1,
                  dma_address(channel, 0, TRUE));
        for (; i >= 0; i--) {
            chan->wcount_c = i;
            addr = dma_address(channel, chan->ptr++, TRUE);
            chan->addr_c = dma_state.channels[channel].addr + chan->ptr;
            data = pread_b(addr);
            write_b(service_address, data);
        }
        break;
    }

    /* End of Process must set the channel's mask bit */
    dma_state.mask |= (1 << channel);
    dma_state.status |= (1 << channel);
}

/*
 * Service pending DRQs
 */
void dmac_service_drqs()
{
    dmac_dma_handler *h;

    for (h = &device_dma_handlers[0]; h->drq != NULL; h++) {
        /* Only trigger if the channel has a DRQ set and its channel's
           mask bit is 0 */
        if (*h->drq && ((dma_state.mask >> h->channel) & 0x1) == 0) {
            h->dma_handler(h->channel, h->service_address);
            /* Each handler is responsible for clearing its own DRQ line! */
            if (h->after_dma_callback != NULL) {
                h->after_dma_callback();
            }
        }
    }
}
