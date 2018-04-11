/* 3b2_dmac.h: AT&T 3B2 Model 400 AM9517A DMA Controller Header

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

#ifndef _3B2_DMAC_H_
#define _3B2_DMAC_H_

#include "3b2_sysdev.h"
#include "3b2_id.h"
#include "3b2_if.h"

/* DMA Controller */
#define DMACBASE         0x48000
#define DMACSIZE         0x11

/* DMA integrated disk page buffer */
#define DMAIDBASE        0x45000
#define DMAIDSIZE        0x5

/* DMA integrated uart A page buffer */
#define DMAIUABASE       0x46000
#define DMAIUASIZE       0x5

/* DMA integrated uart B page buffer */
#define DMAIUBBASE       0x47000
#define DMAIUBSIZE       0x5

/* DMA integrated floppy page buffer */
#define DMAIFBASE        0x4E000
#define DMAIFSIZE        0x5

#define DMA_ID_CHAN      0
#define DMA_IF_CHAN      1
#define DMA_IUA_CHAN     2
#define DMA_IUB_CHAN     3

#define DMA_ID           0x45
#define DMA_IUA          0x46
#define DMA_IUB          0x47
#define DMA_C            0x48
#define DMA_IF           0x4E

#define DMA_MODE_VERIFY  0
#define DMA_MODE_WRITE   1     /* Write to memory from device */
#define DMA_MODE_READ    2     /* Read from memory to device  */

#define DMA_IF_READ      (IFBASE + IF_DATA_REG)

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

#endif
