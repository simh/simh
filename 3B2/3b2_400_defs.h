/* 3b2_400_defs.h: AT&T 3B2 Model 400 Simulator Definitions

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

#ifndef _3B2_400_DEFS_H_
#define _3B2_400_DEFS_H_

#include "sim_defs.h"

#define MAXMEMSIZE      (1 << 22) /* 4 MB */

#define TODBASE         0x41000
#define TODSIZE         0x40
#define TIMERBASE       0x42000
#define TIMERSIZE       0x20
#define NVRAMBASE       0x43000
#define NVRAMSIZE       0x1000
#define CSRBASE         0x44000
#define CSRSIZE         0x100
#define IFBASE          0x4d000
#define IFSIZE          0x10
#define IDBASE          0x4a000
#define IDSIZE          0x2

#define IF_STATUS_REG   0
#define IF_CMD_REG      0
#define IF_TRACK_REG    1
#define IF_SECTOR_REG   2
#define IF_DATA_REG     3

#define ID_DATA_REG     0
#define ID_CMD_STAT_REG 1

/* CSR Flags */
#define CSRTIMO         0x8000 /* Bus Timeout Error      */
#define CSRPARE         0x4000 /* Memory Parity Error    */
#define CSRRRST         0x2000 /* System Reset Request   */
#define CSRALGN         0x1000 /* Memory Alignment Fault */
#define CSRLED          0x0800 /* Failure LED            */
#define CSRFLOP         0x0400 /* Floppy Motor On        */
#define CSRRES          0x0200 /* Reserved               */
#define CSRITIM         0x0100 /* Inhibit Timers         */
#define CSRIFLT         0x0080 /* Inhibit Faults         */
#define CSRCLK          0x0040 /* Clock Interrupt        */
#define CSRPIR8         0x0020 /* Programmed Interrupt 8 */
#define CSRPIR9         0x0010 /* Programmed Interrupt 9 */
#define CSRUART         0x0008 /* UART Interrupt         */
#define CSRDISK         0x0004 /* Floppy Interrupt       */
#define CSRDMA          0x0002 /* DMA Interrupt          */
#define CSRIOF          0x0001 /* I/O Board Fail         */

#define MEMSIZE_REG     0x4C003

/* DMA Controller */
#define DMACBASE        0x48000
#define DMACSIZE        0x11

/* DMA integrated disk page buffer */
#define DMAIDBASE       0x45000
#define DMAIDSIZE       0x5

/* DMA integrated uart A page buffer */
#define DMAIUABASE      0x46000
#define DMAIUASIZE      0x5

/* DMA integrated uart B page buffer */
#define DMAIUBBASE      0x47000
#define DMAIUBSIZE      0x5

/* DMA integrated floppy page buffer */
#define DMAIFBASE       0x4E000
#define DMAIFSIZE       0x5

#define DMA_ID_CHAN     0
#define DMA_IF_CHAN     1
#define DMA_IUA_CHAN    2
#define DMA_IUB_CHAN    3

#define DMA_ID          0x45
#define DMA_IUA         0x46
#define DMA_IUB         0x47
#define DMA_C           0x48
#define DMA_IF          0x4E

#endif /* _3B2_400_DEFS_H_  */
