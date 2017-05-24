/* hp2100_cpu1.h: HP 2100/1000 firmware dispatcher definitions

   Copyright (c) 2006-2013, J. David Bryan

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

   Except as contained in this notice, the name of the author shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   18-Mar-13    JDB     Added externs for microcode helper functions
   14-Mar-13    MP      Changed guard macro name to avoid reserved namespace
   11-Sep-08    JDB     Moved microcode function prototypes here
   30-Apr-08    JDB     Corrected OP_AFF to OP_AAFF for SIGNAL/1000
                        Removed unused operand patterns
   23-Feb-08    HV      Added more OP_* for SIGNAL/1000 and VIS
   28-Nov-07    JDB     Added fprint_ops, fprint_regs for debug printouts
   19-Oct-07    JDB     Revised OP_KKKAKK operand profile to OP_CCCACC for $LOC
   16-Oct-06    JDB     Generalized operands for F-Series FP types
   26-Sep-06    JDB     Split from hp2100_cpu1.c
*/

#ifndef HP2100_CPU1_H_
#define HP2100_CPU1_H_


/* Register print encoding */

#define REG_COUNT       9                               /* count of print flags */

#define REG_CIR         (1 << 0)                        /* print central interrupt register */
#define REG_A           (1 << 1)                        /* print A register */
#define REG_B           (1 << 2)                        /* print B register */
#define REG_E           (1 << 3)                        /* print E register */
#define REG_X           (1 << 4)                        /* print X register */
#define REG_Y           (1 << 5)                        /* print Y register */
#define REG_O           (1 << 6)                        /* print O register */
#define REG_P           (1 << 7)                        /* print P register */
#define REG_P_REL       (1 << 8)                        /* print P register as relative */


/* Operand processing encoding */

/* Base operand types.  Note that all address encodings must be grouped together
   after OP_ADR.
*/

#define OP_NUL          0                               /* no operand */
#define OP_IAR          1                               /* 1-word int in A reg */
#define OP_JAB          2                               /* 2-word int in A/B regs */
#define OP_FAB          3                               /* 2-word FP const in A/B regs */
#define OP_CON          4                               /* inline 1-word constant */
#define OP_VAR          5                               /* inline 1-word variable */

#define OP_ADR          6                               /* inline address */
#define OP_ADK          7                               /* addr of 1-word int const */
#define OP_ADD          8                               /* addr of 2-word int const */
#define OP_ADF          9                               /* addr of 2-word FP const */
#define OP_ADX         10                               /* addr of 3-word FP const */
#define OP_ADT         11                               /* addr of 4-word FP const */
#define OP_ADE         12                               /* addr of 5-word FP const */

#define OP_N_FLAGS      4                               /* number of bits needed for flags */
#define OP_M_FLAGS      ((1 << OP_N_FLAGS) - 1)         /* mask for flag bits */

#define OP_N_F          (8 * sizeof (uint32) / OP_N_FLAGS)  /* max number of op fields */

#define OP_V_F1         (0 * OP_N_FLAGS)                /* 1st operand field */
#define OP_V_F2         (1 * OP_N_FLAGS)                /* 2nd operand field */
#define OP_V_F3         (2 * OP_N_FLAGS)                /* 3rd operand field */
#define OP_V_F4         (3 * OP_N_FLAGS)                /* 4th operand field */
#define OP_V_F5         (4 * OP_N_FLAGS)                /* 5th operand field */
#define OP_V_F6         (5 * OP_N_FLAGS)                /* 6th operand field */
#define OP_V_F7         (6 * OP_N_FLAGS)                /* 7th operand field */
#define OP_V_F8         (7 * OP_N_FLAGS)                /* 8th operand field */

/* Operand processing patterns */

#define OP_N            (OP_NUL << OP_V_F1)
#define OP_I            (OP_IAR << OP_V_F1)
#define OP_J            (OP_JAB << OP_V_F1)
#define OP_R            (OP_FAB << OP_V_F1)
#define OP_C            (OP_CON << OP_V_F1)
#define OP_V            (OP_VAR << OP_V_F1)
#define OP_A            (OP_ADR << OP_V_F1)
#define OP_K            (OP_ADK << OP_V_F1)
#define OP_D            (OP_ADD << OP_V_F1)
#define OP_X            (OP_ADX << OP_V_F1)
#define OP_T            (OP_ADT << OP_V_F1)
#define OP_E            (OP_ADE << OP_V_F1)

#define OP_IA           ((OP_IAR << OP_V_F1) | (OP_ADR << OP_V_F2))
#define OP_JA           ((OP_JAB << OP_V_F1) | (OP_ADR << OP_V_F2))
#define OP_JD           ((OP_JAB << OP_V_F1) | (OP_ADD << OP_V_F2))
#define OP_RC           ((OP_FAB << OP_V_F1) | (OP_CON << OP_V_F2))
#define OP_RK           ((OP_FAB << OP_V_F1) | (OP_ADK << OP_V_F2))
#define OP_RF           ((OP_FAB << OP_V_F1) | (OP_ADF << OP_V_F2))
#define OP_CV           ((OP_CON << OP_V_F1) | (OP_VAR << OP_V_F2))
#define OP_AC           ((OP_ADR << OP_V_F1) | (OP_CON << OP_V_F2))
#define OP_AA           ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2))
#define OP_AK           ((OP_ADR << OP_V_F1) | (OP_ADK << OP_V_F2))
#define OP_AX           ((OP_ADR << OP_V_F1) | (OP_ADX << OP_V_F2))
#define OP_AT           ((OP_ADR << OP_V_F1) | (OP_ADT << OP_V_F2))
#define OP_KV           ((OP_ADK << OP_V_F1) | (OP_VAR << OP_V_F2))
#define OP_KA           ((OP_ADK << OP_V_F1) | (OP_ADR << OP_V_F2))
#define OP_KK           ((OP_ADK << OP_V_F1) | (OP_ADK << OP_V_F2))

#define OP_IIF          ((OP_IAR << OP_V_F1) | (OP_IAR << OP_V_F2) | \
                         (OP_ADF << OP_V_F3))

#define OP_IAT          ((OP_IAR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADT << OP_V_F3))

#define OP_CVA          ((OP_CON << OP_V_F1) | (OP_VAR << OP_V_F2) | \
                         (OP_ADR << OP_V_F3))

#define OP_AAA          ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADR << OP_V_F3))

#define OP_AAF          ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADF << OP_V_F3))

#define OP_AAX          ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADX << OP_V_F3))

#define OP_AAT          ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADT << OP_V_F3))

#define OP_AKA          ((OP_ADR << OP_V_F1) | (OP_ADK << OP_V_F2) | \
                         (OP_ADR << OP_V_F3))

#define OP_AKK          ((OP_ADR << OP_V_F1) | (OP_ADK << OP_V_F2) | \
                         (OP_ADK << OP_V_F3))

#define OP_AXX          ((OP_ADR << OP_V_F1) | (OP_ADX << OP_V_F2) | \
                         (OP_ADX << OP_V_F3))

#define OP_ATT          ((OP_ADR << OP_V_F1) | (OP_ADT << OP_V_F2) | \
                         (OP_ADT << OP_V_F3))

#define OP_AEE          ((OP_ADR << OP_V_F1) | (OP_ADE << OP_V_F2) | \
                         (OP_ADE << OP_V_F3))

#define OP_AAXX         ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADX << OP_V_F3) | (OP_ADX << OP_V_F4))

#define OP_AAFF         ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADF << OP_V_F3) | (OP_ADF << OP_V_F4))

#define OP_AAKK         ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADK << OP_V_F3) | (OP_ADK << OP_V_F4))

#define OP_KKKK         ((OP_ADK << OP_V_F1) | (OP_ADK << OP_V_F2) | \
                         (OP_ADK << OP_V_F3) | (OP_ADK << OP_V_F4))

#define OP_AAAKK        ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADR << OP_V_F3) | (OP_ADK << OP_V_F4) | \
                         (OP_ADK << OP_V_F5))

#define OP_AKAKK        ((OP_ADR << OP_V_F1) | (OP_ADK << OP_V_F2) | \
                         (OP_ADR << OP_V_F3) | (OP_ADK << OP_V_F4) | \
                         (OP_ADK << OP_V_F5))

#define OP_AAACCC       ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADR << OP_V_F3) | (OP_CON << OP_V_F4) | \
                         (OP_CON << OP_V_F5) | (OP_CON << OP_V_F6))

#define OP_AAFFKK       ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADF << OP_V_F3) | (OP_ADF << OP_V_F4) | \
                         (OP_ADK << OP_V_F5) | (OP_ADK << OP_V_F6))

#define OP_AAKAKK       ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADK << OP_V_F3) | (OP_ADR << OP_V_F4) | \
                         (OP_ADK << OP_V_F5) | (OP_ADK << OP_V_F6))

#define OP_CATAKK       ((OP_CON << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADT << OP_V_F3) | (OP_ADR << OP_V_F4) | \
                         (OP_ADK << OP_V_F5) | (OP_ADK << OP_V_F6))

#define OP_CCCACC       ((OP_CON << OP_V_F1) | (OP_CON << OP_V_F2) | \
                         (OP_CON << OP_V_F3) | (OP_ADR << OP_V_F4) | \
                         (OP_CON << OP_V_F5) | (OP_CON << OP_V_F6))

#define OP_AAAFFKK      ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADR << OP_V_F3) | (OP_ADF << OP_V_F4) | \
                         (OP_ADF << OP_V_F5) | (OP_ADK << OP_V_F6) | \
                         (OP_ADK << OP_V_F7))

#define OP_AKAKAKK      ((OP_ADR << OP_V_F1) | (OP_ADK << OP_V_F2) | \
                         (OP_ADR << OP_V_F3) | (OP_ADK << OP_V_F4) | \
                         (OP_ADR << OP_V_F5) | (OP_ADK << OP_V_F6) | \
                         (OP_ADK << OP_V_F7))

#define OP_AAKAKAKK     ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADK << OP_V_F3) | (OP_ADR << OP_V_F4) | \
                         (OP_ADK << OP_V_F5) | (OP_ADR << OP_V_F6) | \
                         (OP_ADK << OP_V_F7) | (OP_ADK << OP_V_F8))

#define OP_CCACACCA     ((OP_CON << OP_V_F1) | (OP_CON << OP_V_F2) | \
                         (OP_ADR << OP_V_F3) | (OP_CON << OP_V_F4) | \
                         (OP_ADR << OP_V_F5) | (OP_CON << OP_V_F6) | \
                         (OP_CON << OP_V_F7) | (OP_ADR << OP_V_F8))


/* Operand precisions (compatible with F-Series FPP):

    - S = 1-word integer
    - D = 2-word integer
    - F = 2-word single-precision floating-point
    - X = 3-word extended-precision floating-point
    - T = 4-word double-precision floating-point
    - E = 5-word expanded-exponent floating-point
    - A = null operand (operand is in FPP accumulator)

   5-word floating-point numbers are supported by the F-Series Floating-Point
   Processor hardware, but the instruction codes are not documented.

   Note that ordering is important, as we depend on the "fp" type codes to
   reflect the number of words needed.
*/

typedef enum { in_s, in_d, fp_f, fp_x, fp_t, fp_e, fp_a } OPSIZE;


/* Conversion from operand size to word count */

#define TO_COUNT(s)     ((s == fp_a) ? 0 : (uint32) (s + (s < fp_f)))


/* HP in-memory representation of a packed floating-point number.
   Actual value will use two, three, four, or five words, as needed.
*/

typedef uint16 FPK[5];


/* Operand processing types.

   NOTE: Microsoft VC++ 6.0 does not support the C99 standard, so we cannot
   initialize unions by arbitrary variant ("designated initializers").
   Therefore, we follow the C90 form of initializing via the first named
   variant.  The FPK variant must appear first in the OP structure, as we define
   a number of FPK constants in other modules.
*/

typedef union {                                         /* general operand */
    FPK fpk;                                            /* floating-point value */
    uint16 word;                                        /* 16-bit integer */
    uint32 dword;                                       /* 32-bit integer */
    } OP;

typedef OP OPS[OP_N_F];                                 /* operand array */

typedef uint32 OP_PAT;                                  /* operand pattern */


/* Microcode dispatcher functions (grouped by cpu module number) */

extern t_stat cpu_ds      (uint32 IR, uint32 intrq);                /* [0] Distributed System stub */
extern t_stat cpu_user    (uint32 IR, uint32 intrq);                /* [0] User firmware dispatcher */
extern t_stat cpu_user_20 (uint32 IR, uint32 intrq);                /* [0] Module 20 user microprograms stub */

extern t_stat cpu_eau   (uint32 IR, uint32 intrq);                  /* [1] EAU group simulator */
extern t_stat cpu_uig_0 (uint32 IR, uint32 intrq, uint32 iotrap);   /* [1] UIG group 0 dispatcher */
extern t_stat cpu_uig_1 (uint32 IR, uint32 intrq, uint32 iotrap);   /* [1] UIG group 1 dispatcher */

#if !defined (HAVE_INT64)                                           /* int64 support unavailable */
extern t_stat cpu_fp  (uint32 IR, uint32 intrq);                    /* [2] Firmware Floating Point */
#endif
extern t_stat cpu_dms (uint32 IR, uint32 intrq);                    /* [2] Dynamic mapping system */
extern t_stat cpu_eig (uint32 IR, uint32 intrq);                    /* [2] Extended instruction group */
extern t_stat cpu_iop (uint32 IR, uint32 intrq);                    /* [2] 2000 I/O Processor */

extern t_stat cpu_ffp (uint32 IR, uint32 intrq);                    /* [3] Fast FORTRAN Processor */
extern t_stat cpu_dbi (uint32 IR, uint32 intrq);                    /* [3] Double-Integer instructions */

#if defined (HAVE_INT64)                                            /* int64 support available */
extern t_stat cpu_fpp (uint32 IR, uint32 intrq);                    /* [4] Floating Point Processor */
extern t_stat cpu_sis (uint32 IR, uint32 intrq);                    /* [4] Scientific Instruction Set */
#endif

extern t_stat cpu_rte_vma (uint32 IR, uint32 intrq);                /* [5] RTE-6 VMA */
extern t_stat cpu_rte_ema (uint32 IR, uint32 intrq);                /* [5] RTE-IV EMA */

extern t_stat cpu_rte_os (uint32 IR, uint32 intrq, uint32 iotrap);  /* [6] RTE-6 OS */

#if defined (HAVE_INT64)                                            /* int64 support available */
extern t_stat cpu_vis    (uint32 IR, uint32 intrq);                 /* [7] Vector Instruction Set */
extern t_stat cpu_signal (uint32 IR, uint32 intrq);                 /* [7] SIGNAL/1000 Instructions */
#endif


/* Microcode helper functions */

extern OP     ReadOp  (uint32 va, OPSIZE precision);               /* generalized operand read */
extern void   WriteOp (uint32 va, OP operand, OPSIZE precision);   /* generalized operand write */
extern t_stat cpu_ops (OP_PAT pattern, OPS op, uint32 irq);        /* operand processor */

extern void fprint_ops  (OP_PAT pattern, OPS op);                  /* debug print operands */
extern void fprint_regs (char *caption, uint32 regs, uint32 base); /* debug print CPU registers */

/* implemented in hp2100_cpu5.c (RTE-IV EMA functions) */

extern t_stat cpu_ema_eres (uint32 *rtn, uint32 dtbl, uint32 atbl, t_bool debug);
extern t_stat cpu_ema_eseg (uint32 *rtn, uint32 ir, uint32 tbl, t_bool debug);
extern t_stat cpu_ema_vset (uint32 *rtn, OPS op, t_bool debug);

#endif
