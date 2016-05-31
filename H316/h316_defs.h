/* h316_defs.h: Honeywell 316/516 simulator definitions

   Copyright (c) 1999-2015, Robert M. Supnik

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   31-May-13    RLA     DIB - add second channel, interrupt and user parameter
   19-Nov-11    RMS     Removed XR macro, added XR_LOC macro (from Adrian Wise)
   22-May-10    RMS     Added check for 64b definitions
   15-Feb-05    RMS     Added start button interrupt
   01-Dec-04    RMS     Added double precision constants
   24-Oct-03    RMS     Added DMA/DMC support
   25-Apr-03    RMS     Revised for extended file support
*/

#ifndef H316_DEFS_H_
#define H316_DEFS_H_    0

#include "sim_defs.h"                                   /* simulator defns */

#if defined(USE_INT64) || defined(USE_ADDR64)
#error "H316 does not support 64b values!"
#endif

/* Simulator stop codes */

#define STOP_RSRV       1                               /* must be 1 */
#define STOP_IODV       2                               /* must be 2 */
#define STOP_HALT       3                               /* HALT */
#define STOP_IBKPT      4                               /* breakpoint */
#define STOP_IND        5                               /* indirect loop */
#define STOP_DMAER      6                               /* DMA error */
#define STOP_MTWRP      7                               /* MT write protected */
#define STOP_DPOVR      8                               /* DP write overrun */
#define STOP_DPFMT      9                               /* DP invalid format */

/* Memory */

#define MAXMEMSIZE      32768                           /* max memory size */
#define MEMSIZE         (cpu_unit.capac)                /* actual memory size */
#define X_AMASK         (MAXMEMSIZE - 1)                /* ext address mask */
#define NX_AMASK        ((MAXMEMSIZE / 2) - 1)          /* nx address mask */
#define MEM_ADDR_OK(x)  (((uint32) (x)) < MEMSIZE)

/* Architectural constants */

#define SIGN            0100000                         /* sign */
#define DP_SIGN         010000000000
#define DMASK           0177777                         /* data mask */
#define MMASK           (DMASK & ~SIGN)                 /* magnitude mask */
#define M_CLK           061                             /* clock location */
#define M_RSTINT        062                             /* restrict int */
#define M_INT           063                             /* int location */
#define M_XR            (ext? 0: (PC & 040000))         /* XR location */

/* CPU options */

#define UNIT_V_MSIZE    (UNIT_V_UF + 0)                 /* dummy mask */
#define UNIT_V_EXT      (UNIT_V_UF + 1)                 /* extended mem */
#define UNIT_V_HSA      (UNIT_V_UF + 2)                 /* high speed arith */
#define UNIT_V_DMC      (UNIT_V_UF + 3)                 /* DMC */
#define UNIT_MSIZE      (1u << UNIT_V_MSIZE)
#define UNIT_EXT        (1u << UNIT_V_EXT)
#define UNIT_HSA        (1u << UNIT_V_HSA)
#define UNIT_DMC        (1u << UNIT_V_DMC)

/* Instruction format */

#define I_M_OP          077                             /* opcode */
#define I_V_OP          10
#define I_GETOP(x)      (((x) >> I_V_OP) & I_M_OP)
#define I_M_FNC         017                             /* function */
#define I_V_FNC         6
#define I_GETFNC(x)     (((x) >> I_V_FNC) & I_M_FNC)
#define IA              0100000                         /* indirect address */
#define IDX             0040000                         /* indexed */
#define SC              0001000                         /* sector */
#define DISP            0000777                         /* page displacement */
#define PAGENO          0077000                         /* page number */
#define INCLRA          (010 << I_V_FNC)                /* INA clear A */
#define DEVMASK         0000077                         /* device mask */
#define SHFMASK         0000077                         /* shift mask */

/* I/O opcodes */

#define ioOCP           0                               /* output control */
#define ioSKS           1                               /* skip if set */
#define ioINA           2                               /* input to A */
#define ioOTA           3                               /* output from A */
#define ioEND           4                               /* channel end */

/* Device information block */

struct h316_dib {
    uint32              dev;                            /* device number */
    uint32              num;                            /* number of slots */
    uint32              chan;                           /* dma/dmc channel */
    uint32              chan2;                          /* alternate DMA/DMD channel */
    uint32              inum;                           /* interrupt number */
    uint32              inum2;                          /* alternate interrupt */
    int32               (*io) (int32 inst, int32 fnc, int32 dat, int32 dev);
    uint32              u3;                             /* "user" parameter #1 */
};
typedef struct h316_dib DIB;

/* DMA/DMC channel numbers */

#define IOBUS           0                               /* IO bus */
#define DMA_MIN         1                               /* 4 DMA channels */
#define DMA_MAX         4
#define DMC_MIN         1                               /* 16 DMC channels */
#define DMC_MAX         16

#define DMA1            (DMA_MIN)
#define DMC1            (DMA_MAX+DMC_MIN)

/* DMA/DMC bit assignments in channel request word */

#define DMA_V_DMA1      0                               /* DMA channels */
#define DMC_V_DMC1      4                               /* DMC channels */
#define SET_CH_REQ(x)   chan_req = chan_req | (1 << (x))
#define Q_DMA(x)        (((x) >= 0) && ((x) < DMC_V_DMC1))

/* DMA/DMC definitions */

#define DMA_IN          0100000                         /* input flag */
#define DMC_BASE        020                             /* DMC memory base */

/* I/O device codes */

#define PTR             001                             /* paper tape reader */
#define PTP             002                             /* paper tape punch */
#define LPT             003                             /* line printer */
#define TTY             004                             /* console */
#define CDR             005                             /* card reader */
#define MT              010                             /* mag tape data */
#define CLK_KEYS        020                             /* clock/keys (CPU) */
#define FHD             022                             /* fixed head disk */
#define DMA             024                             /* DMA control */
#define DP              025                             /* moving head disk */
#define DEV_MAX         64

/* Interrupt flags, definitions correspond to SMK bits */

#define INT_V_CLK       0                               /* clock */
#define INT_V_MPE       1                               /* parity error */
#define INT_V_LPT       2                               /* line printer */
#define INT_V_CDR       4                               /* card reader */
#define INT_V_TTY       5                               /* teletype */
#define INT_V_PTP       6                               /* paper tape punch */
#define INT_V_PTR       7                               /* paper tape reader */
#define INT_V_FHD       8                               /* fixed head disk */
#define INT_V_DP        12                              /* moving head disk */
#define INT_V_MT        15                              /* mag tape */
#define INT_V_START     16                              /* start button */
#define INT_V_NODEF     17                              /* int not deferred */
#define INT_V_ON        18                              /* int on */
#define INT_V_EXTD      16                              /* first extended interrupt */
#define INT_V_NONE      0xffffffff                      /* no interrupt used */

/* I/O macros */

#define IOT_V_REASON    17
#define IOT_V_SKIP      16
#define IOT_SKIP        (1u << IOT_V_SKIP)
#define IORETURN(f,v)   (((f)? (v): SCPE_OK) << IOT_V_REASON)
#define IOBADFNC(x)     (((stop_inst) << IOT_V_REASON) | (x))
#define IOSKIP(x)       (IOT_SKIP | (x))

#define INT_CLK         (1u << INT_V_CLK)
#define INT_MPE         (1u << INT_V_MPE)
#define INT_LPT         (1u << INT_V_LPT)
#define INT_CDR         (1u << INT_V_CDR)
#define INT_TTY         (1u << INT_V_TTY)
#define INT_PTP         (1u << INT_V_PTP)
#define INT_PTR         (1u << INT_V_PTR)
#define INT_FHD         (1u << INT_V_FHD)
#define INT_DP          (1u << INT_V_DP)
#define INT_MT          (1u << INT_V_MT)
#define INT_START       (1u << INT_V_START)
#define INT_NODEF       (1u << INT_V_NODEF)
#define INT_ON          (1u << INT_V_ON)
#define INT_NMI         (INT_START)
#define INT_PEND        (INT_ON | INT_NODEF)

// [RLA]   These macros now all affect the standard interrupts.  We'll leave
// [RLA] them alone for backward compatibility with the existing code.
#define SET_INT(x)      dev_int = dev_int | (x)
#define CLR_INT(x)      dev_int = dev_int & ~(x)
#define TST_INT(x)      ((dev_int & (x)) != 0)
#define CLR_ENB(x)      dev_enb = dev_enb & ~(x)
#define TST_INTREQ(x)   ((dev_int & dev_enb & (x)) != 0)

// [RLA] These macros are functionally identical, but affect extended interrupts.
#define SET_EXT_INT(x)  dev_ext_int = dev_ext_int |  (x)
#define CLR_EXT_INT(x)  dev_ext_int = dev_ext_int & ~(x)
#define TST_EXT_INT(x)  ((dev_ext_int & (x)) != 0)
#define CLR_EXT_ENB(x)  dev_ext_enb = dev_ext_enb & ~(x)
#define TST_EXT_INTREQ(x) ((dev_ext_int & dev_ext_enb & (x)) != 0)

/* Prototypes */

t_stat io_set_iobus (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat io_set_dma (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat io_set_dmc (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat io_show_chan (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

#endif
