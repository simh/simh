/* 3b2_defs.h: AT&T 3B2 Shared Simulator Definitions

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

#ifndef _3B2_DEFS_H_
#define _3B2_DEFS_H_

#include "sim_defs.h"
#include "sim_tmxr.h"
#include <setjmp.h>

#if defined(REV3)
#include "3b2_1000_defs.h"
#else
#include "3b2_400_defs.h"
#include "3b2_400_cpu.h"
#include "3b2_400_mau.h"
#include "3b2_400_mmu.h"
#include "3b2_400_stddev.h"
#include "3b2_400_sys.h"
#include "3b2_id.h"
#endif

#include "3b2_io.h"
#include "3b2_dmac.h"
#include "3b2_if.h"
#include "3b2_iu.h"

#include "3b2_ports.h"
#include "3b2_ctc.h"
#include "3b2_ni.h"

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

#if defined(__GLIBC__) && !defined(__cplusplus)
/* use glibc internal longjmp to bypass fortify checks */
noret __libc_longjmp(jmp_buf buf, int val);
#define longjmp __libc_longjmp
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

#define UNIT_V_EXHALT  (UNIT_V_UF + 0)
#define UNIT_EXHALT    (1u << UNIT_V_EXHALT)
#define EX_V_FLAG      1 << 21

#define PHYS_MEM_BASE  0x2000000

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

/* Global symbols */

extern volatile int32 stop_reason;

extern CIO_STATE      cio[CIO_SLOTS];

extern instr         *cpu_instr;
extern uint32        *ROM;
extern uint32        *RAM;
extern uint32         R[16];
extern REG            cpu_reg[];
extern UNIT           cpu_unit;
extern uint8          fault;
extern t_bool         cpu_km;
extern uint32         R[16];
extern char           sim_name[];
extern REG           *sim_PC;
extern int32          sim_emax;
extern uint16         csr_data;
extern int32          tmxr_poll;

extern DEBTAB         sys_deb_tab[];
extern DEVICE         contty_dev;
extern DEVICE         cpu_dev;
extern DEVICE         csr_dev;
extern DEVICE         ctc_dev;
extern DEVICE         dmac_dev;
extern DEVICE         id_dev;
extern DEVICE         if_dev;
extern DEVICE         iu_timer_dev;
extern DEVICE         mau_dev;
extern DEVICE         mmu_dev;
extern DEVICE         ni_dev;
extern DEVICE         nvram_dev;
extern DEVICE         ports_dev;
extern DEVICE         timer_dev;
extern DEVICE         tod_dev;
extern DEVICE         tti_dev;
extern DEVICE         tto_dev;

extern IU_PORT        iu_console;
extern IU_PORT        iu_contty;
extern IF_STATE       if_state;
extern MMU_STATE      mmu_state;
extern DMA_STATE      dma_state;

extern t_bool         id_drq;
extern t_bool         if_irq;
extern t_bool         cio_skip_seqbit;
extern t_bool         iu_increment_a;
extern t_bool         iu_increment_b;

#endif
