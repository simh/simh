/* 3b2_defs.h: AT&T 3B2 Shared Simulator Definitions

   Copyright (c) 2017-2022, Seth J. Morabito

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

#ifndef _3B2_DEFS_H_
#define _3B2_DEFS_H_

#include <setjmp.h>

#include "sim_defs.h"

#if defined(REV3)
#include "3b2_rev3_defs.h"
#else
#include "3b2_rev2_defs.h"
#endif

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#if defined(__GNUC__)
#define noret void __attribute__((noreturn))
#else
#define noret void
#endif

#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef UNUSED
#define UNUSED(x) ((void)((x)))
#endif

#define ATOW(arr, i)                                                           \
  ((uint32)(arr)[i + 3] + ((uint32)(arr)[i + 2] << 8) +                        \
   ((uint32)(arr)[i + 1] << 16) + ((uint32)(arr)[i] << 24))

#define ATOH(arr, i) ((uint32)(arr)[i + 1] + ((uint32)(arr)[i] << 8))

#define CSRBIT(bit, sc)                                                        \
  {                                                                            \
    if (sc) {                                                                  \
      csr_data |= (bit);                                                       \
    } else {                                                                   \
      csr_data &= ~(bit);                                                      \
    }                                                                          \
  }

#define PCHAR(c) (((char) (c) >= 0x20 && (char) (c) < 0x7f) ? (char) (c) : '.')

#define ROM_SIZE       (128 * 1024)
#define POLL_WAIT      70000

#define UNIT_V_EXBRK   (UNIT_V_UF + 0)
#define UNIT_V_OPBRK   (UNIT_V_UF + 1)
#define UNIT_EXBRK     (1u << UNIT_V_EXBRK)
#define UNIT_OPBRK     (1u << UNIT_V_OPBRK)

#define EX_V_FLAG      1 << 21

#define ROM_BASE       0
#define PHYS_MEM_BASE  0x2000000

#define MSIZ_512K      0x80000
#define MSIZ_1M        0x100000
#define MSIZ_2M        0x200000
#define MSIZ_4M        0x400000
#define MSIZ_8M        0x800000
#define MSIZ_16M       0x1000000
#define MSIZ_32M       0x2000000
#define MSIZ_64M       0x4000000

/* Simulator stop codes */
#define STOP_RSRV      1
#define STOP_IBKPT     2  /* Breakpoint encountered */
#define STOP_OPCODE    3  /* Invalid opcode */
#define STOP_IRQ       4  /* Interrupt */
#define STOP_EX        5  /* Exception */
#define STOP_ESTK      6  /* Exception stack too deep */
#define STOP_MMU       7  /* Unimplemented MMU Feature */
#define STOP_POWER     8  /* System power-off */
#define STOP_LOOP      9  /* Infinite loop stop */
#define STOP_ERR       10 /* Other error */

/* Debug flags */
#define READ_MSG       0x0001
#define WRITE_MSG      0x0002
#define DECODE_MSG     0x0004
#define EXECUTE_MSG    0x0008
#define INIT_MSG       0x0010
#define IRQ_MSG        0x0020
#define IO_DBG         0x0040
#define CIO_DBG        0x0080
#define TRACE_DBG      0x0100
#define CALL_DBG       0x0200
#define PKT_DBG        0x0400
#define ERR_MSG        0x0800
#define CACHE_DBG      0x1000
#define DECODE_DBG     0x2000

#define TIMER_SANITY   0
#define TIMER_INTERVAL 1
#define TIMER_BUS      2

/* Timers */
#define TMR_CLK        0     /* Calibrated 100Hz timer */

/* Global symbols */

extern DEBTAB sys_deb_tab[];
extern DEVICE contty_dev;
extern DEVICE cpu_dev;
extern DEVICE csr_dev;
extern DEVICE ctc_dev;
extern DEVICE dmac_dev;
extern DEVICE id_dev;
extern DEVICE if_dev;
extern DEVICE iu_timer_dev;
extern DEVICE mau_dev;
extern DEVICE mmu_dev;
extern DEVICE ni_dev;
extern DEVICE nvram_dev;
extern DEVICE ports_dev;
extern DEVICE timer_dev;
extern DEVICE tod_dev;
extern DEVICE tti_dev;
extern DEVICE tto_dev;
#if defined(REV3)
extern DEVICE flt_dev;
extern DEVICE ha_dev;
#endif /* defined(REV3) */

#endif /* _3B2_DEFS_H_ */
