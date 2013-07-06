/* pdp11_dmc.h: DMC11 Emulation
  ------------------------------------------------------------------------------

   Copyright (c) 2011, Robert M. A. Jarratt

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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

  ------------------------------------------------------------------------------

  Modification history:

  15-Jan-13  RJ   Contribution from Paul Koning of support for RSTS using the ROM
                  INPUT (ROM I) command to get the DMC11 to report DSR status.
  ------------------------------------------------------------------------------*/

// Notes
// Base address needs to be 760060 according to DMC11 manual, but SYSGEN seems to think CSR is 0760100. However if I use
// 0760100 I get a conflict with the DZ because the first 13 bits are still 00100. If I use 760060 VMS sees the XM device, but
// if I remove the DZ to prevent the conflict VMS does not see an XM device, but I do get lots of reads and writes, possibly
// because it thinks it is a different device. What worries me more though is that there seems to be overlap in the 13-bit base
// addresses of the DZ and DMC.


#ifndef PDP11_DMC_H
#define PDP11_DMC_H

#if defined (VM_VAX)                                    /* VAX version */
#include "vax_defs.h"
#elif defined(VM_PDP10)
#include "pdp10_defs.h"
#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif

extern int32 IREQ (HLVL);
extern int32 tmxr_poll;                                 /* calibrated delay */
extern int32 clk_tps;                                   /* clock ticks per second */
extern int32 tmr_poll;                                  /* instructions per tick */

#include "sim_sock.h"

#if !defined(DMC_NUMDEVICE)
#define DMC_NUMDEVICE 8         /* MAX # DMC-11 devices */
#endif

#if !defined(DMP_NUMDEVICE)
#define DMP_NUMDEVICE 8         /* MAX # DMP-11/DMV-11 devices */
#endif

#define DMC_RDX                     8

/* debugging bitmaps */
#define DBG_TRC  0x0001                                 /* trace routine calls */
#define DBG_REG  0x0002                                 /* trace read/write registers */
#define DBG_WRN  0x0004                                 /* display warnings */
#define DBG_INF  0x0008                                 /* display informational messages (high level trace) */
#define DBG_DAT  0x0010                                 /* display data buffer contents */
#define DBG_DTS  0x0020                                 /* display data summary */
#define DBG_SOK  0x0040                                 /* display socket open/close */
#define DBG_CON  0x0080                                 /* display socket connection establishment */

#define TYPE_BACCI 0
#define TYPE_CNTLI 1
#define TYPE_BASEI 03
#define TYPE_BACCO 0
#define TYPE_CNTLO 1

#define TYPE_DMP_MODE 2
#define TYPE_DMP_CONTROL 1
#define TYPE_DMP_RECEIVE 0
#define TYPE_DMP_TRANSMIT 4


/* SEL0 */
#define DMC_TYPE_INPUT_MASK 0x0003
#define DMC_IN_IO_MASK 0x0004
#define DMP_IEO_MASK 0x0010
#define DMC_RQI_MASK 0x0020
#define DMP_RQI_MASK 0x0080
#define DMC_RDYI_MASK 0x0080
#define DMC_IEI_MASK 0x0040
#define DMP_IEI_MASK 0x0001
#define ROMI_MASK 0x0200
#define LU_LOOP_MASK 0x0800
#define MASTER_CLEAR_MASK 0x4000
#define RUN_MASK 0x8000

/* SEL2 */
#define DMP_IN_IO_MASK 0x0004
#define DMP_TYPE_INPUT_MASK 0x0007
#define TYPE_OUTPUT_MASK 0x0003
#define OUT_IO_MASK 0x0004
#define DMC_RDYO_MASK 0x0080
#define DMC_IEO_MASK 0x0040
#define DMP_RDYI_MASK 0x0010

/* BSEL6 */
#define LOST_DATA_MASK 0x0010
#define DISCONNECT_MASK 0x0040

#define DSPDSR 0x22b3       /* KMC opcode to move line unit status to SEL2 */

#define SEL0_RUN_BIT 15
#define SEL0_MCLR_BIT 14
#define SEL0_LU_LOOP_BIT 11
#define SEL0_ROMI_BIT 9
#define SEL0_RDI_BIT 7
#define SEL0_DMC_IEI_BIT 6
#define SEL0_DMP_IEI_BIT 0
#define SEL0_DMP_IEO_BIT 4
#define SEL0_DMC_RQI_BIT 5
#define SEL0_DMP_RQI_BIT 7
#define SEL0_IN_IO_BIT 2
#define SEL0_TYPEI_BIT 0

#define SEL2_TYPEO_BIT 0
#define SEL2_RDO_BIT 7
#define SEL2_IEO_BIT 6
#define SEL2_OUT_IO_BIT 2
#define SEL2_LINE_BIT 8
#define SEL2_LINE_BIT_LENGTH 6
#define SEL2_PRIO_BIT 14
#define SEL2_PRIO_BIT_LENGTH 2

#define SEL6_LOST_DATA_BIT 4
#define SEL6_DISCONNECT_BIT 6

#define BUFFER_QUEUE_SIZE 7

#endif                                                  /* _VAX_DMC_H */
