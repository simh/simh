/* i1620_defs.h: IBM 1620 simulator definitions

   Copyright (c) 2002-2017, Robert M. Supnik

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

   This simulator is based on the 1620 simulator written by Geoff Kuenning.
   I am grateful to Al Kossow, the Computer History Museum, and the IBM Corporate
   Archives for their help in gathering documentation about the IBM 1620.

   23-May-17    RMS     MARCHK is indicator 8, not 18 (Dave Wise)
   19-May-17    RMS     Added option for Model I diagnostic mode (Dave Wise)
   05-Feb-15    TFM     Added definitions for flagged RM, GM, NB
   22-May-10    RMS     Added check for 64b definitions
   18-Oct-02    RMS     Fixed bug in ADDR_S macro (found by Hans Pufal)
*/

#ifndef I1620_DEFS_H_
#define I1620_DEFS_H_  0

#include "sim_defs.h"                                   /* simulator defns */

#if defined(USE_INT64) || defined(USE_ADDR64)
#error "1620 does not support 64b values!"
#endif

/* Simulator stop codes */

#define STOP_HALT       1                               /* HALT */
#define STOP_IBKPT      2                               /* breakpoint */
#define STOP_INVINS     3                               /* invalid instruction */
#define STOP_INVDIG     4                               /* invalid digit */
#define STOP_INVCHR     5                               /* invalid char */
#define STOP_INVIND     6                               /* invalid indicator */
#define STOP_INVPDG     7                               /* invalid P addr digit */
#define STOP_INVPAD     8                               /* invalid P addr */
#define STOP_INVPIA     9                               /* invalid P indir addr */
#define STOP_INVQDG     10                              /* invalid Q addr digits */
#define STOP_INVQAD     11                              /* invalid Q addr */
#define STOP_INVQIA     12                              /* invalid Q indir addr */
#define STOP_INVIO      13                              /* invalid IO address */
#define STOP_INVRTN     14                              /* invalid return */
#define STOP_INVFNC     15                              /* invalid function */
#define STOP_INVIAD     16                              /* invalid instr addr */
#define STOP_INVSEL     17                              /* invalid select */
#define STOP_INVIDX     18                              /* invalid index instr */
#define STOP_INVEAD     19                              /* invalid even addr */
#define STOP_INVDCF     20                              /* invalid DCF addr */
#define STOP_INVDRV     21                              /* invalid disk drive */
#define STOP_INVDSC     22                              /* invalid disk sector */
#define STOP_INVDCN     23                              /* invalid disk count */
#define STOP_INVDBA     24                              /* invalid disk buf addr */
#define STOP_DACERR     25                              /* disk addr comp err */
#define STOP_DWCERR     26                              /* disk wr check err */
#define STOP_CYOERR     27                              /* cylinder ovflo err */
#define STOP_WRLERR     28                              /* wrong rec lnt err */
#define STOP_CCT        29                              /* runaway CCT */
#define STOP_FWRAP      30                              /* field wrap */
#define STOP_RWRAP      31                              /* record wrap */
#define STOP_NOCD       32                              /* no card in reader */
#define STOP_OVERFL     33                              /* overflow */
#define STOP_EXPCHK     34                              /* exponent error */
#define STOP_WRADIS     35                              /* write addr disabled */
#define STOP_FPLNT      36                              /* invalid fp length */
#define STOP_FPUNL      37                              /* fp lengths unequal */
#define STOP_FPMF       38                              /* no flag on exp */
#define STOP_FPDVZ      39                              /* divide by zero */

/* Memory */

#define MAXMEMSIZE      60000                           /* max mem size */
#define MEMSIZE         (cpu_unit.capac)                /* act memory size */

/* Processor parameters */

#define INST_LEN        12                              /* inst length */
#define ADDR_LEN        5                               /* addr length */
#define MUL_TABLE       100                             /* multiply table */
#define MUL_TABLE_LEN   200
#define ADD_TABLE       300                             /* add table */
#define ADD_TABLE_LEN   100
#define IDX_A           300                             /* index A base */
#define IDX_B           340                             /* index B base */
#define PROD_AREA       80                              /* product area */
#define PROD_AREA_LEN   20                              /* product area */
#define PROD_AREA_END   (PROD_AREA + PROD_AREA_LEN)

/* Branch indicator codes */

#define NUM_IND         100                             /* number of indicators */

#define IN_SW1          1                               /* sense switch 1 */
#define IN_SW2          2                               /* sense switch 2 */
#define IN_SW3          3                               /* sense switch 3 */
#define IN_SW4          4                               /* sense switch 4 */
#define IN_RDCHK        6                               /* read check (I/O error) */
#define IN_WRCHK        7                               /* write check (I/O error) */
#define IN_MARCHK       8                               /* MAR check - diag only */
#define IN_LAST         9                               /* last card was just read */
#define IN_HP           11                              /* high or positive result */
#define IN_EZ           12                              /* equal or zero result */
#define IN_HPEZ         13                              /* high/positive or equal/zero */
#define IN_OVF          14                              /* overflow */
#define IN_EXPCHK       15                              /* floating exponent check */
#define IN_MBREVEN      16                              /* even parity check */
#define IN_MBRODD       17                              /* odd parity check */
#define IN_ANYCHK       19                              /* any of read, write, even/odd */
#define IN_PRCHK        25                              /* printer check */
#define IN_IXN          30                              /* IX neither */
#define IN_IXA          31                              /* IX A band */
#define IN_IXB          32                              /* IX B band */
#define IN_PRCH9        33                              /* printer chan 9 */
#define IN_PRCH12       34                              /* printer chan 12 */
#define IN_PRBSY        35                              /* printer busy */
#define IN_DACH         36                              /* disk addr/data check */
#define IN_DWLR         37                              /* disk rec length */
#define IN_DCYO         38                              /* disk cyl overflow */
#define IN_DERR         39                              /* disk any error */

/* I/O channel codes */

#define NUM_IO          100                             /* number of IO chan */

#define IO_TTY          1                               /* console typewriter */
#define IO_PTP          2                               /* paper-tape punch */
#define IO_PTR          3                               /* paper-tape reader */
#define IO_CDP          4                               /* card punch */
#define IO_CDR          5                               /* card reader */
#define IO_DSK          7                               /* disk */
#define IO_LPT          9                               /* line printer */
#define IO_BTP          32                              /* binary ptp */
#define IO_BTR          33                              /* binary ptr */

#define LPT_WIDTH       120                             /* line print width */
#define CCT_LNT         132                             /* car ctrl length */

#define CRETIOE(f,c)    return ((f)? (c): SCPE_OK)

/* Memory representation: flag + BCD digit per byte */

#define FLAG            0x10
#define DIGIT           0x0F
#define REC_MARK        0xA
#define NUM_BLANK       0xC
#define GRP_MARK        0xF
#define FLG_REC_MARK    0x1A
#define FLG_NUM_BLANK   0x1C
#define FLG_GRP_MARK    0x1F
#define BAD_DIGIT(x)    ((x) > 9)

/* Instruction format */

#define I_OP            0                               /* opcode */
#define I_P             2                               /* P start */
#define I_PL            6                               /* P end */
#define I_Q             7                               /* Q start */
#define I_QL            11                              /* Q end */
#define I_IO            8                               /* IO select */
#define I_BR            8                               /* indicator select */
#define I_CTL           10                              /* control select */
#define I_SEL           11                              /* BS select */

#define ADDR_A(x,a)     ((((x) + (a)) >= MEMSIZE)? ((x) + (a) - MEMSIZE): ((x) + (a)))
#define ADDR_S(x,a)     (((x) < (a))? ((x) - (a) + MEMSIZE): ((x) - (a)))
#define PP(x)           x = ADDR_A(x,1)
#define MM(x)           x = ADDR_S(x,1)

/* CPU options, stored in cpu_unit.flags */
/* Decoding flags must be part of the same definition set */

#define UNIT_SCP        ((1 << UNIT_V_UF) - 1)          /* mask of SCP flags */
#define IF_MII          (1 << (UNIT_V_UF + 0))          /* model 2 */
#define IF_DIV          (1 << (UNIT_V_UF + 1))          /* automatic divide */
#define IF_IA           (1 << (UNIT_V_UF + 2))          /* indirect addressing */
#define IF_EDT          (1 << (UNIT_V_UF + 3))          /* edit */
#define IF_FP           (1 << (UNIT_V_UF + 4))          /* floating point */
#define IF_BIN          (1 << (UNIT_V_UF + 5))          /* binary */
#define IF_IDX          (1 << (UNIT_V_UF + 6))          /* indexing */
#define IF_VPA          (1 << (UNIT_V_UF + 7))          /* valid P addr */
#define IF_VQA          (1 << (UNIT_V_UF + 8))          /* valid Q addr */
#define IF_4QA          (1 << (UNIT_V_UF + 9))          /* 4 char Q addr */
#define IF_NQX          (1 << (UNIT_V_UF + 10))         /* no Q indexing */
#define IF_IMM          (1 << (UNIT_V_UF + 11))         /* immediate */
#define IF_RMOK         (1 << (UNIT_V_UF + 12))         /* diag mode - force rm to 0 */
#define UNIT_BCD        (1 << (UNIT_V_UF + 13))         /* BCD coded */
#define UNIT_MSIZE      (1 << (UNIT_V_UF + 14))         /* fake flag */
#define ALLOPT          (IF_DIV + IF_IA + IF_EDT + IF_FP + IF_BIN + IF_IDX + IF_RMOK)
#define MI_OPT          (IF_DIV + IF_IA + IF_EDT + IF_FP + IF_RMOK)
#define MI_STD          (IF_DIV + IF_IA + IF_EDT)
#define MII_OPT         (IF_DIV + IF_IA + IF_EDT + IF_FP + IF_BIN + IF_IDX)
#define MII_STD         (IF_DIV + IF_IA + IF_EDT + IF_BIN + IF_IDX)

/* Add status codes */

#define ADD_NOCRY       0                               /* no carry out */
#define ADD_CARRY       1                               /* carry out */
#define ADD_SIGNC       2                               /* sign change */

/* Opcodes */

enum opcodes {
    OP_FADD = 1, OP_FSUB, OP_FMUL,                      /* 00 - 09 */
    OP_FSL = 5, OP_TFL, OP_BTFL, OP_FSR, OP_FDIV,
    OP_BTAM = 10, OP_AM, OP_SM, OP_MM, OP_CM,           /* 10 - 19 */
    OP_TDM, OP_TFM, OP_BTM, OP_LDM, OP_DM,
    OP_BTA = 20, OP_A, OP_S, OP_M, OP_C,                /* 20 - 29 */
    OP_TD, OP_TF, OP_BT, OP_LD, OP_D,
    OP_TRNM = 30, OP_TR, OP_SF, OP_CF, OP_K,            /* 30 - 39 */
    OP_DN, OP_RN, OP_RA, OP_WN, OP_WA,
    OP_NOP = 41, OP_BB, OP_BD, OP_BNF,                  /* 40 - 49 */
    OP_BNR, OP_BI, OP_BNI, OP_H, OP_B,
    OP_BNG = 55,
    OP_BS = 60, OP_BX, OP_BXM, OP_BCX, OP_BCXM,         /* 60 - 69 */
    OP_BLX, OP_BLXM, OP_BSX,
    OP_MA = 70, OP_MF, OP_TNS, OP_TNF,                  /* 70 - 79 */
                                                        /* 80 - 89 */
    OP_BBT = 90, OP_BMK, OP_ORF, OP_ANDF, OP_CPLF,      /* 90 - 99 */
    OP_EORF, OP_OTD, OP_DTO };

/* Device flags */

#define DEV_DEFIO       (1 << (DEV_V_UF + 0))

#define DEFIO_CPS       u4                  /* Characters per Second field */
#if (SIM_MAJOR >= 4)
#define DEFIO_ACTIVATE(uptr) ((uptr)->DEFIO_CPS) ? sim_activate_after (uptr, 1000000/(uptr)->DEFIO_CPS) : sim_activate (uptr, (uptr)->wait)
#define DEFIO_ACTIVATE_ABS(uptr) ((uptr)->DEFIO_CPS) ? sim_activate_after_abs (uptr, 1000000/(uptr)->DEFIO_CPS) : sim_activate_abs (uptr, (uptr)->wait)
#else
#define DEFIO_ACTIVATE(uptr) sim_activate (uptr, (uptr)->wait)
#define DEFIO_ACTIVATE_ABS(uptr) sim_activate_abs (uptr, (uptr)->wait)
#endif

/* Function declarations */

t_stat cpuio_set_inp (uint32 op, uint32 dev, UNIT *uptr);
t_stat cpuio_clr_inp (UNIT *uptr);
const char *opc_lookup (uint32 op, uint32 qv, uint32 *fl);

extern const int8 cdr_to_alp[128];
extern const int8 alp_to_cdp[256];

#endif
