/* 3b2_rev3_defs.h: Veresion 3 (3B2/700) Common Definitions

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

#ifndef _3B2_REV3_DEFS_H_
#define _3B2_REV3_DEFS_H_

#define NUM_REGISTERS   32

#define DEFMEMSIZE      MSIZ_16M
#define MAXMEMSIZE      MSIZ_64M

#define HWORD_OP_COUNT  12
#define CPU_VERSION     0x1F  /* Version encoded in WE32200 */

/* CSR Flags */
#define CSRCLK          1u         /* UNIX Interval Timer Timeout  */
#define CSRPWRDN        (1u << 1)  /* Power Down Request           */
#define CSROPINT15      (1u << 2)  /* Oper. Interrupt Level 15     */
#define CSRUART         (1u << 3)  /* DUART Interrupt              */
#define CSRDMA          (1u << 4)  /* DUART DMA Complete Interrupt */
#define CSRPIR9         (1u << 5)  /* Programmed Interrupt 9       */
#define CSRPIR8         (1u << 6)  /* Programmed Interrupt 8       */
#define CSRITIM         (1u << 7)  /* Inhibit UNIX Interval Timer  */
#define CSRISTIM        (1u << 8)  /* Inhibit System Sanity Timer  */
#define CSRITIMO        (1u << 9)  /* Inhibit Bus Timer            */
#define CSRICPUFLT      (1u << 10) /* Inhibit Faults to CPU        */
#define CSRISBERR       (1u << 11) /* Inhibit Single Bit Error Rpt */
#define CSRIIOBUS       (1u << 12) /* Inhibit Integral 3B2 I/O Bus */
#define CSRIBUB         (1u << 13) /* Inhibit 4 BUB Slots          */
#define CSRFECC         (1u << 14) /* Force ECC Syndrome           */
#define CSRTHERM        (1u << 15) /* Thermal Shutdown Request     */
#define CSRLED          (1u << 16) /* Failure LED                  */
#define CSRPWRSPDN      (1u << 17) /* Power Down -- Power Supply   */
#define CSRFLPFST       (1u << 18) /* Floppy Speed Fast            */
#define CSRFLPS1        (1u << 19) /* Floppy Side 1                */
#define CSRFLPMO        (1u << 20) /* Floppy Motor On              */
#define CSRFLPDEN       (1u << 21) /* Floppy Density               */
#define CSRFLPSZ        (1u << 22) /* Floppy Size                  */
#define CSRSBERR        (1u << 23) /* Single Bit Error             */
#define CSRMBERR        (1u << 24) /* Multiple Bit Error           */
#define CSRUBUBF        (1u << 25) /* Ubus/BUB Received Fail       */
#define CSRTIMO         (1u << 26) /* Bus Timer Timeout            */
#define CSRFRF          (1u << 27) /* Fault Registers Frozen       */
#define CSRALGN         (1u << 28) /* Data Alignment Error         */
#define CSRSTIMO        (1u << 29) /* Sanity Timer Timeout         */
#define CSRABRT         (1u << 30) /* Abort Switch Activated       */
#define CSRRRST         (1u << 31) /* System Reset Request         */

/* Interrupt Sources */
#define INT_CLOCK        0x0001    /* UNIX Interval Timer Timeout - IPL 15       */
#define INT_PWRDWN       0x0002    /* Power Down Request - IPL 15                */
#define INT_BUS_OP       0x0004    /* UBUS or BUB Operational Interrupt - IPL 15 */
#define INT_SBERR        0x0008    /* Single Bit Memory Error - IPL 15           */
#define INT_MBERR        0x0010    /* Multiple Bit Memory Error - IPL 15         */
#define INT_BUS_RXF      0x0020    /* UBUS, BUB, EIO Bus Received Fail - IPL 15  */
#define INT_BUS_TMO      0x0040    /* UBUS Timer Timeout - IPL 15                */
#define INT_UART_DMA     0x0080    /* UART DMA Complete - IPL 13                 */
#define INT_UART         0x0100    /* UART Interrupt - IPL 13                    */
#define INT_FLOPPY_DMA   0x0200    /* Floppy DMA Complete - IPL 11               */
#define INT_FLOPPY       0x0400    /* Floppy Interrupt - IPL 11                  */
#define INT_PIR9         0x0800    /* PIR-9 (from CSER) - IPL 9                  */
#define INT_PIR8         0x1000    /* PIR-8 (from CSER) - IPL 8                  */

#define INT_MAP_LEN      0x2000

#define IFCSRBASE       0x40000
#define IFCSRSIZE       0x100
#define TIMERBASE       0x41000
#define TIMERSIZE       0x20
#define NVRBASE         0x42000
#define NVRSIZE         0x2000
#define CSRBASE         0x44000
#define CSRSIZE         0x100
#define DMAIFBASE       0x45000
#define DMAIFSIZE       0x5
#define DMAIUABASE      0x46000
#define DMAIUASIZE      0x5
#define DMAIUBBASE      0x47000
#define DMAIUBSIZE      0x5
#define DMACBASE        0x48000
#define DMACSIZE        0x11
#define IFBASE          0x4a000
#define IFSIZE          0x10
#define TODBASE         0x4e000
#define TODSIZE         0x40
#define MMUBASE         0x4f000
#define MMUSIZE         0x1000
#define FLTLBASE        0x4c000
#define FLTLSIZE        0x1000
#define FLTHBASE        0x4d000
#define FLTHSIZE        0x1000

#define VCACHE_BOTTOM   0x1c00000
#define VCACHE_TOP      0x2000000

#define BUB_BOTTOM      0x6000000
#define BUB_TOP         0x1a000000

#define IF_STATUS_REG   0
#define IF_CMD_REG      0
#define IF_TRACK_REG    1
#define IF_SECTOR_REG   2
#define IF_DATA_REG     3

#define DMA_IF_CHAN     1
#define DMA_IUA_CHAN    2
#define DMA_IUB_CHAN    3

#define DMA_IF          0x45
#define DMA_IUA         0x46
#define DMA_IUB         0x47
#define DMA_C           0x48

#endif
