/* 3b2_defs.h: AT&T 3B2 Model 400 Simulator Definitions

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

#define FALSE 0
#define TRUE 1

#if defined (__GNUC__)
#define noret void __attribute__((noreturn))
#else
#define noret void
#endif

#if defined(__GLIBC__) && !defined(__cplusplus)
/* use glibc internal longjmp to bypass fortify checks */
noret __libc_longjmp (jmp_buf buf, int val);
#define longjmp __libc_longjmp
#endif

#ifndef MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#endif
#ifndef MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#endif
#ifndef UNUSED
#define UNUSED(x)  ((void)((x)))
#endif

#define UNIT_V_EXHALT   (UNIT_V_UF + 0)
#define UNIT_EXHALT     (1u << UNIT_V_EXHALT)

/* -t flag: Translate a virtual address */
#define EX_T_FLAG 1 << 19
/* -v flag for examine routine */
#define EX_V_FLAG 1 << 21

#define MAX_HIST_SIZE  10000000
#define MIN_HIST_SIZE  64
#define MAXMEMSIZE     (1 << 22)             /* 4 MB */
#define MEM_SIZE       (cpu_unit.capac)      /* actual memory size */
#define UNIT_V_MSIZE   (UNIT_V_UF)

#define UNIT_MSIZE     (1 << UNIT_V_MSIZE)

#define WD_MSB     0x80000000
#define HW_MSB     0x8000
#define BT_MSB     0x80
#define WORD_MASK  0xffffffff
#define HALF_MASK  0xffffu
#define BYTE_MASK  0xff

/*
 * Custom t_stat
 */

#define SCPE_PEND     (SCPE_OK + 1)      /* CIO job already pending */
#define SCPE_NOJOB    (SCPE_OK + 2)      /* No CIO job on the request queue */
/*
 *
 * Physical memory in the 3B2 is arranged as follows:
 *
 *    0x00000000 - 0x0000FFFF: 64KB ROM (32K used)
 *    0x00040000 - 0x0004FFFF: IO
 *    0x02000000 - 0x023FFFFF: 4MB RAM ("Mainstore"),
 *
 */

#define PHYS_MEM_BASE 0x2000000

#define ROM_BASE      0

/* IO area */
#define IO_BOTTOM     0x40000
#define IO_TOP        0x50000

/* Register numbers */
#define NUM_FP         9
#define NUM_AP         10
#define NUM_PSW        11
#define NUM_SP         12
#define NUM_PCBP       13
#define NUM_ISP        14
#define NUM_PC         15

/* System board interrupt priority levels */
#define CPU_PIR8_IPL   8
#define CPU_PIR9_IPL   9
#define CPU_ID_IF_IPL  11
#define CPU_IU_DMA_IPL 13
#define CPU_TMR_IPL    15

#define CPU_CM         (cpu_km ? L_KERNEL : ((R[NUM_PSW] >> PSW_CM) & 3))

/* Simulator stop codes */
#define STOP_RSRV           1
#define STOP_IBKPT          2     /* Breakpoint encountered */
#define STOP_OPCODE         3     /* Invalid opcode */
#define STOP_IRQ            4     /* Interrupt */
#define STOP_EX             5     /* Exception */
#define STOP_ESTK           6     /* Exception stack too deep */
#define STOP_MMU            7     /* Unimplemented MMU Feature */
#define STOP_POWER          8     /* System power-off */
#define STOP_LOOP           9     /* Infinite loop stop */
#define STOP_ERR           10     /* Other error */

/* Exceptional conditions handled within the instruction loop */
#define ABORT_EXC           1      /* CPU exception  */
#define ABORT_TRAP          2      /* CPU trap */

/* Contexts for aborts */
#define C_NONE               0     /* No context. Normal handling. */
#define C_NORMAL_GATE_VECTOR 1
#define C_PROCESS_GATE_PCB   2
#define C_PROCESS_OLD_PCB    3
#define C_PROCESS_NEW_PCB    4
#define C_RESET_GATE_VECTOR  5
#define C_RESET_INT_STACK    6
#define C_RESET_NEW_PCB      7
#define C_RESET_SYSTEM_DATA  8
#define C_STACK_FAULT        9

/* Debug flags */
#define READ_MSG     0x0001
#define WRITE_MSG    0x0002
#define DECODE_MSG   0x0004
#define EXECUTE_MSG  0x0008
#define INIT_MSG     0x0010
#define IRQ_MSG      0x0020
#define IO_DBG       0x0040
#define CIO_DBG      0x0080
#define TRACE_DBG    0x0100
#define CALL_DBG     0x0200
#define PKT_DBG      0x0400
#define ERR_MSG      0x0800
#define CACHE_DBG    0x1000
#define DECODE_DBG   0x2000

/* Data types operated on by instructions. NB: These integer values
   have meaning when decoding instructions, so this is not just an
   enum. Please don't change them. */
#define UW 0   /* Unsigned Word */
#define UH 2   /* Unsigned Halfword */
#define BT 3   /* Unsigned Byte */
#define WD 4   /* Signed Word */
#define HW 6   /* Signed Halfword */
#define SB 7   /* Signed Byte */

#define NA -1

/*
 * Exceptions are described on page 2-66 of the WE32100 manual
 */

/* Exception Types */

#define RESET_EXCEPTION       0
#define PROCESS_EXCEPTION     1
#define STACK_EXCEPTION       2
#define NORMAL_EXCEPTION      3

/* Reset Exceptions */
#define OLD_PCB_FAULT         0
#define SYSTEM_DATA_FAULT     1
#define INTERRUPT_STACK_FAULT 2
#define EXTERNAL_RESET        3
#define NEW_PCB_FAULT         4
#define GATE_VECTOR_FAULT     6

/* Processor Exceptions */
#define GATE_PCB_FAULT        1

/* Stack Exceptions */
#define STACK_BOUND           0
#define STACK_FAULT           1
#define INTERRUPT_ID_FETCH    3

/* Normal Exceptions */
#define INTEGER_ZERO_DIVIDE   0
#define TRACE_TRAP            1
#define ILLEGAL_OPCODE        2
#define RESERVED_OPCODE       3
#define INVALID_DESCRIPTOR    4
#define EXTERNAL_MEMORY_FAULT 5
#define N_GATE_VECTOR         6
#define ILLEGAL_LEVEL_CHANGE  7
#define RESERVED_DATATYPE     8
#define INTEGER_OVERFLOW      9
#define PRIVILEGED_OPCODE    10
#define BREAKPOINT_TRAP      14
#define PRIVILEGED_REGISTER  15

#define PSW_ET                0
#define PSW_TM                2u
#define PSW_ISC               3u
#define PSW_I                 7
#define PSW_R                 8
#define PSW_PM                9
#define PSW_CM                11
#define PSW_IPL               13
#define PSW_TE                17
#define PSW_C                 18
#define PSW_V                 19
#define PSW_Z                 20
#define PSW_N                 21
#define PSW_OE                22
#define PSW_CD                23
#define PSW_QIE               24
#define PSW_CFD               25

/* Access Request types */
#define ACC_MT     0       /* Move Translated */
#define ACC_SPW    1       /* Support processor write */
#define ACC_SPF    3       /* Support processor fetch */
#define ACC_IR     7       /* Interlocked read */
#define ACC_AF     8       /* Address fetch */
#define ACC_OF     9       /* Operand fetch */
#define ACC_W      10      /* Write */
#define ACC_IFAD   12      /* Instruction fetch after discontinuity */
#define ACC_IF     13      /* Instruction fetch */


#define L_KERNEL              0
#define L_EXEC                1
#define L_SUPER               2
#define L_USER                3

#define PSW_ET_MASK            3u
#define PSW_TM_MASK           (1u << PSW_TM)
#define PSW_ISC_MASK          (15u << PSW_ISC)
#define PSW_I_MASK            (1u << PSW_I)
#define PSW_R_MASK            (1u << PSW_R)
#define PSW_PM_MASK           (3u << PSW_PM)
#define PSW_CM_MASK           (3u << PSW_CM)
#define PSW_IPL_MASK          (15u << PSW_IPL)
#define PSW_TE_MASK           (1u << PSW_TE)
#define PSW_C_MASK            (1u << PSW_C)
#define PSW_V_MASK            (1u << PSW_V)
#define PSW_N_MASK            (1u << PSW_N)
#define PSW_Z_MASK            (1u << PSW_Z)
#define PSW_OE_MASK           (1u << PSW_OE)
#define PSW_CD_MASK           (1u << PSW_CD)
#define PSW_QIE_MASK          (1u << PSW_QIE)
#define PSW_CFD_MASK          (1u << PSW_CFD)

#define PSW_CUR_IPL  (((R[NUM_PSW] & PSW_IPL_MASK) >> PSW_IPL) & 0xf)

#define TODBASE   0x41000
#define TODSIZE   0x40
#define TIMERBASE 0x42000
#define TIMERSIZE 0x20
#define NVRAMBASE 0x43000
#define NVRAMSIZE 0x1000
#define CSRBASE   0x44000
#define CSRSIZE   0x100

#define CSRTIMO   0x8000   /* Bus Timeout Error      */
#define CSRPARE   0x4000   /* Memory Parity Error    */
#define CSRRRST   0x2000   /* System Reset Request   */
#define CSRALGN   0x1000   /* Memory Alignment Fault */
#define CSRLED    0x0800   /* Failure LED            */
#define CSRFLOP   0x0400   /* Floppy Motor On        */
#define CSRRES    0x0200   /* Reserved               */
#define CSRITIM   0x0100   /* Inhibit Timers         */
#define CSRIFLT   0x0080   /* Inhibit Faults         */
#define CSRCLK    0x0040   /* Clock Interrupt        */
#define CSRPIR8   0x0020   /* Programmed Interrupt 8 */
#define CSRPIR9   0x0010   /* Programmed Interrupt 9 */
#define CSRUART   0x0008   /* UART Interrupt         */
#define CSRDISK   0x0004   /* Floppy Interrupt       */
#define CSRDMA    0x0002   /* DMA Interrupt          */
#define CSRIOF    0x0001   /* I/O Board Fail         */

#define TIMER_REG_DIVA    0x03
#define TIMER_REG_DIVB    0x07
#define TIMER_REG_DIVC    0x0b
#define TIMER_REG_CTRL    0x0f
#define TIMER_CLR_LATCH   0x13

/* Clock state bitmasks */
#define CLK_MD    0x0E        /* Mode mask */
#define CLK_RW    0x30        /* RW mask   */
#define CLK_SC    0xC0        /* SC mask   */

#define CLK_LAT   0x00
#define CLK_LSB   0x10
#define CLK_MSB   0x20
#define CLK_LMB   0x30

#define CLK_MD0   0x00
#define CLK_MD1   0x02
#define CLK_MD2   0x04
#define CLK_MD3   0x06
#define CLK_MD4   0x08
#define CLK_MD5   0x0a

/* IO area */

#define MEMSIZE_REG    0x4C003
#define CIO_BOTTOM     0x200000
#define CIO_TOP        0x2000000
#define CIO_SLOTS      12

#define CIO_CMDSTAT    0x80
#define CIO_SEQBIT     0x40

#define CIO_INT_DELAY  8000

/* Timer definitions */

#define TMR_CLK   0         /* The clock responsible for IPL 15 interrupts */
#define TPS_CLK   100       /* 100 ticks per second */

/* global symbols from the CPU */

extern jmp_buf save_env;
extern uint32 *ROM;
extern uint32 *RAM;
extern uint32 R[16];
extern REG cpu_reg[];
extern DEVICE cpu_dev;
extern UNIT cpu_unit;
extern uint8 fault;
extern DEBTAB sys_deb_tab[];
extern t_bool cpu_km;

/* global symbols from the DMAC */
typedef struct {
    uint8  page;
    uint16 addr;     /* Original addr       */
    uint16 wcount;   /* Original wcount     */
    uint16 addr_c;   /* Current addr        */
    int32  wcount_c; /* Current word-count  */
    uint16 ptr;      /* Pointer into memory */
} dma_channel;

typedef struct {
    /* Byte (high/low) flip-flop */
    uint8  bff;

    /* Address and count registers for channels 0-3 */
    dma_channel channels[4];

    /* DMAC programmable registers */
    uint8 command;
    uint8 mode;
    uint8 request;
    uint8 mask;
    uint8 status;
} DMA_STATE;


/* global symbols from DMA */
extern DMA_STATE dma_state;
extern DEVICE dmac_dev;
uint32 dma_address(uint8 channel, uint32 offset, t_bool r);

/* global symbols from the CSR */
extern uint16 csr_data;

/* global symbols from the timer */
extern int32 tmxr_poll;

/* global symbols from the IU */
extern t_bool iu_increment_a;
extern t_bool iu_increment_b;
extern void increment_modep_a();
extern void increment_modep_b();

/* global symbols from the MMU */
extern t_stat mmu_decode_va(uint32 va, uint8 r_acc, t_bool fc, uint32 *pa);
extern void mmu_enable();
extern void mmu_disable();
extern uint8 read_b(uint32 va, uint8 acc);
extern uint16 read_h(uint32 va, uint8 acc);
extern uint32 read_w(uint32 va, uint8 acc);
extern void write_b(uint32 va, uint8 val);
extern void write_h(uint32 va, uint16 val);
extern void write_w(uint32 va, uint32 val);
extern void pwrite_w(uint32 pa, uint32 val);
extern uint32 pread_w(uint32 pa);

/* global symbols from the MAU */
extern t_stat mau_broadcast(uint32 cmd, uint32 src, uint32 dst);

/* Globally scoped CPU functions */
extern void cpu_abort(uint8 et, uint8 isc);
extern void cpu_set_irq(uint8 ipl, uint8 id, uint16 csr_flags);
extern void cpu_clear_irq(uint8 ipl, uint16 csr_flags);

/* global symbols from the IO system */
extern uint32 io_read(uint32 pa, size_t size);
extern void io_write(uint32 pa, uint32 val, size_t size);
extern void cio_clear(uint8 cid);
extern void cio_xfer();
extern uint8 cio_int;
extern uint16 cio_ipl;

/* Future Use: Global symbols from the PORTS card */
/* extern void ports_express(uint8 cid); */
/* extern void ports_full(uint8 cid); */
/* extern void ports_xfer(uint8 cid); */

#endif
