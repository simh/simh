/* 3b2_dmac.h: AM9517 DMA Controller

   Copyright (c) 2021-2022, Seth J. Morabito

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

#ifndef _3B2_DMAC_H_
#define _3B2_DMAC_H_

#include "3b2_defs.h"

#define DMA_XFER_VERIFY  0
#define DMA_XFER_WRITE   1     /* Write to memory from device */
#define DMA_XFER_READ    2     /* Read from memory to device  */

#define DMA_IF_READ      (IFBASE + IF_DATA_REG)

#define DMA_MODE(C)      ((dma_state.channels[(C)].mode >> 6) & 3)
#define DMA_DECR(C)      ((dma_state.channels[(C)].mode >> 5) & 1)
#define DMA_AUTOINIT(C)  ((dma_state.channels[(C)].mode >> 4) & 1)
#define DMA_XFER(C)      ((dma_state.channels[(C)].mode >> 2) & 3)

typedef struct {
    uint8 mode;      /* Channel mode        */
    uint16 page;     /* Memory page         */
    uint16 addr;     /* Original addr       */
    uint16 wcount;   /* Original wcount     */
    uint16 addr_c;   /* Current addr        */
    int32  wcount_c; /* Current word-count  */
    uint16 ptr;      /* Pointer into memory */
} dma_channel;

typedef struct {
    /* Byte (high/low) flip-flop */
    uint8 bff;

    /* Address and count registers for channels 0-3 */
    dma_channel channels[4];

    /* DMAC programmable registers */
    uint8 command;
    uint8 request;
    uint8 mask;
    uint8 status;
} DMA_STATE;

typedef struct {
    uint8  channel;
    uint32 service_address;
    t_bool *drq;
    void  (*dma_handler)(uint8 channel, uint32 service_address);
    void  (*after_dma_callback)();
} dmac_dma_handler;

/* DMAC */
t_stat dmac_reset(DEVICE *dptr);
uint32 dmac_read(uint32 pa, size_t size);
void dmac_write(uint32 pa, uint32 val, size_t size);
void dmac_service_drqs();
void dmac_generic_dma(uint8 channel, uint32 service_address);
uint32 dma_address(uint8 channel, uint32 offset);

extern DMA_STATE dma_state;

#endif
