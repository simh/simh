/* alpha_defs.h: Alpha architecture definitions file

   Copyright (c) 2003-2006, Robert M Supnik

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

   Respectfully dedicated to the great people of the Alpha chip, systems, and
   software development projects; and to the memory of Peter Conklin, of the
   Alpha Program Office.
*/

#ifndef _ALPHA_DEFS_H_
#define _ALPHA_DEFS_H_  0

#include "sim_defs.h"
#include <setjmp.h>

#if defined (__GNUC__)
#define INLINE inline
#else
#define INLINE
#endif

/* Configuration */

#define INITMEMSIZE     (1 << 24)                       /* !!debug!! */
#define MEMSIZE         (cpu_unit.capac)
#define ADDR_IS_MEM(x)  ((x) < MEMSIZE)
#define DEV_DIB         (1u << (DEV_V_UF + 0))          /* takes a DIB */

/* Simulator stops */

#define STOP_HALT       1                               /* halt */
#define STOP_IBKPT      2                               /* breakpoint */
#define STOP_NSPAL      3                               /* non-supported PAL */
#define STOP_KSNV       4                               /* kernel stk inval */
#define STOP_INVABO     5                               /* invalid abort code */
#define STOP_MME        6                               /* console mem mgt error */

/* Bit patterns */

#define M8              0xFF
#define M16             0xFFFF
#define M32             0xFFFFFFFF
#define M64             0xFFFFFFFFFFFFFFFF
#define B_SIGN          0x80
#define W_SIGN          0x8000
#define L_SIGN          0x80000000
#define Q_SIGN          0x8000000000000000
#define Q_GETSIGN(x)    (((uint32) ((x) >> 63)) & 1)

/* Architectural variants */

#define AMASK_BWX       0x0001                          /* byte/word */
#define AMASK_FIX       0x0002                          /* sqrt/flt-int moves */
#define AMASK_CIX       0x0004                          /* counts */
#define AMASK_MVI       0x0100                          /* multimedia */
#define AMASK_PRC       0x0200                          /* precise exceptions */
#define AMASK_PFM       0x1000                          /* prefetch w modify */

#define IMPLV_EV4       0x0                             /* EV4 (21064) */
#define IMPLV_EV5       0x1                             /* EV5 (21164) */
#define IMPLV_EV6       0x2                             /* EV6 (21264) */
#define IMPLV_EV7       0x3                             /* EV7 (21364) */

/* Instruction formats */

#define I_V_OP          26                              /* opcode */
#define I_M_OP          0x3F
#define I_OP            (I_M_OP << I_V_OP)
#define I_V_RA          21                              /* Ra */
#define I_M_RA          0x1F
#define I_V_RB          16                              /* Rb */
#define I_M_RB          0x1F
#define I_V_FTRP        13                              /* floating trap mode */
#define I_M_FTRP        0x7
#define I_FTRP          (I_M_FTRP << I_V_FTRP)
#define  I_F_VAXRSV     0x4800                          /* VAX reserved */
#define  I_FTRP_V       0x2000                          /* /V trap */
#define  I_FTRP_U       0x2000                          /* /U trap */
#define  I_FTRP_S       0x8000                          /* /S trap */
#define  I_FTRP_SUI     0xE000                          /* /SUI trap */
#define  I_FTRP_SVI     0xE000                          /* /SVI trap */
#define I_V_FRND        11                              /* floating round mode */
#define I_M_FRND        0x3
#define I_FRND          (I_M_FRND << I_V_FRND)
#define  I_FRND_C       0                               /* chopped */
#define  I_FRND_M       1                               /* to minus inf */
#define  I_FRND_N       2                               /* normal */
#define  I_FRND_D       3                               /* dynamic */
#define  I_FRND_P       3                               /* in FPCR: plus inf */
#define I_V_FSRC        9                               /* floating source */
#define I_M_FSRC        0x3
#define I_FSRC          (I_M_FSRC << I_V_FSRC)
#define  I_FSRC_X       0x0200                          /* data type X */
#define I_V_FFNC        5                               /* floating function */
#define I_M_FFNC        0x3F
#define I_V_LIT8        13                              /* integer 8b literal */
#define I_M_LIT8        0xFF
#define I_V_ILIT        12                              /* literal flag */
#define I_ILIT          (1u << I_V_ILIT)
#define I_V_IFNC        5                               /* integer function */
#define I_M_IFNC        0x3F
#define I_V_RC          0                               /* Rc */
#define I_M_RC          0x1F
#define I_V_MDSP        0                               /* memory displacement */
#define I_M_MDSP        0xFFFF
#define I_V_BDSP        0
#define I_M_BDSP        0x1FFFFF                        /* branch displacement */
#define I_V_PALOP       0
#define I_M_PALOP       0x3FFFFFF                       /* PAL subopcode */
#define I_GETOP(x)      (((x) >> I_V_OP) & I_M_OP)
#define I_GETRA(x)      (((x) >> I_V_RA) & I_M_RA)
#define I_GETRB(x)      (((x) >> I_V_RB) & I_M_RB)
#define I_GETLIT8(x)    (((x) >> I_V_LIT8) & I_M_LIT8)
#define I_GETIFNC(x)    (((x) >> I_V_IFNC) & I_M_IFNC)
#define I_GETFRND(x)    (((x) >> I_V_FRND) & I_M_FRND)
#define I_GETFFNC(x)    (((x) >> I_V_FFNC) & I_M_FFNC)
#define I_GETRC(x)      (((x) >> I_V_RC) & I_M_RC)
#define I_GETMDSP(x)    (((x) >> I_V_MDSP) & I_M_MDSP)
#define I_GETBDSP(x)    (((x) >> I_V_BDSP) & I_M_BDSP)
#define I_GETPAL(x)     (((x) >> I_V_PALOP) & I_M_PALOP)

/* Floating point types */

#define DT_F            0                               /* type F */
#define DT_G            1                               /* type G */
#define DT_S            0                               /* type S */
#define DT_T            1                               /* type T */

/* Floating point memory format (VAX F) */

#define F_V_SIGN        15
#define F_SIGN          (1u << F_V_SIGN)
#define F_V_EXP         7
#define F_M_EXP         0xFF
#define F_BIAS          0x80
#define F_EXP           (F_M_EXP << F_V_EXP)
#define F_V_FRAC        29
#define F_GETEXP(x)     ((uint32) (((x) >> F_V_EXP) & F_M_EXP))
#define SWAP_VAXF(x)    ((((x) >> 16) & 0xFFFF) | (((x) & 0xFFFF) << 16))

/* Floating point memory format (VAX G) */

#define G_V_SIGN        15
#define G_SIGN          (1u << F_V_SIGN)
#define G_V_EXP         4
#define G_M_EXP         0x7FF
#define G_BIAS          0x400
#define G_EXP           (G_M_EXP << G_V_EXP)
#define G_GETEXP(x)     ((uint32) (((x) >> G_V_EXP) & G_M_EXP))
#define SWAP_VAXG(x)    ((((x) & 0x000000000000FFFF) << 48) | \
                        (((x) & 0x00000000FFFF0000) << 16) | \
                        (((x) >> 16) & 0x00000000FFFF0000) | \
                        (((x) >> 48) & 0x000000000000FFFF))

/* Floating memory format (IEEE S) */

#define S_V_SIGN        31
#define S_SIGN          (1u << S_V_SIGN)
#define S_V_EXP         23
#define S_M_EXP         0xFF
#define S_BIAS          0x7F
#define S_NAN           0xFF
#define S_EXP           (S_M_EXP << S_V_EXP)
#define S_V_FRAC        29
#define S_GETEXP(x)     ((uint32) (((x) >> S_V_EXP) & S_M_EXP))

/* Floating point memory format (IEEE T) */

#define T_V_SIGN        63
#define T_SIGN          0x8000000000000000
#define T_V_EXP         52
#define T_M_EXP         0x7FF
#define T_BIAS          0x3FF
#define T_NAN           0x7FF
#define T_EXP           0x7FF0000000000000
#define T_FRAC          0x000FFFFFFFFFFFFF
#define T_GETEXP(x)     ((uint32) (((uint32) ((x) >> T_V_EXP)) & T_M_EXP))

/* Floating point register format (all except VAX D) */

#define FPR_V_SIGN      63
#define FPR_SIGN        0x8000000000000000
#define FPR_V_EXP       52
#define FPR_M_EXP       0x7FF
#define FPR_NAN         0x7FF
#define FPR_EXP         0x7FF0000000000000
#define FPR_HB          0x0010000000000000
#define FPR_FRAC        0x000FFFFFFFFFFFFF
#define FPR_GUARD       (UF_V_NM - FPR_V_EXP)
#define FPR_GETSIGN(x)  (((uint32) ((x) >> FPR_V_SIGN)) & 1)
#define FPR_GETEXP(x)   (((uint32) ((x) >> FPR_V_EXP)) & FPR_M_EXP)
#define FPR_GETFRAC(x)  ((x) & FPR_FRAC)

#define FP_TRUE         0x4000000000000000              /* 0.5/2.0 in reg */

/* Floating point register format (VAX D) */

#define FDR_V_SIGN      63
#define FDR_SIGN        0x8000000000000000
#define FDR_V_EXP       55
#define FDR_M_EXP       0xFF
#define FDR_EXP         0x7F80000000000000
#define FDR_HB          0x0080000000000000
#define FDR_FRAC        0x007FFFFFFFFFFFFF
#define FDR_GUARD       (UF_V_NM - FDR_V_EXP)
#define FDR_GETSIGN(x)  (((uint32) ((x) >> FDR_V_SIGN)) & 1)
#define FDR_GETEXP(x)   (((uint32) ((x) >> FDR_V_EXP)) & FDR_M_EXP)
#define FDR_GETFRAC(x)  ((x) & FDR_FRAC)

#define D_BIAS          0x80

/* Unpacked floating point number */

typedef struct {
    uint32              sign;
    int32               exp;
    t_uint64            frac;
    } UFP;

#define UF_V_NM         63
#define UF_NM           0x8000000000000000              /* normalized */

/* IEEE control register (left 32b only) */

#define FPCR_SUM        0x80000000                      /* summary */
#define FPCR_INED       0x40000000                      /* inexact disable */
#define FPCR_UNFD       0x20000000                      /* underflow disable */
#define FPCR_UNDZ       0x10000000                      /* underflow to 0 */
#define FPCR_V_RMOD     26                              /* rounding mode */
#define FPCR_M_RMOD     0x3
#define FPCR_IOV        0x02000000                      /* integer overflow */
#define FPCR_INE        0x01000000                      /* inexact */
#define FPCR_UNF        0x00800000                      /* underflow */
#define FPCR_OVF        0x00400000                      /* overflow */
#define FPCR_DZE        0x00200000                      /* div by zero */
#define FPCR_INV        0x00100000                      /* invalid operation */
#define FPCR_OVFD       0x00080000                      /* overflow disable */
#define FPCR_DZED       0x00040000                      /* div by zero disable */
#define FPCR_INVD       0x00020000                      /* invalid op disable */
#define FPCR_DNZ        0x00010000                      /* denormal to zero */
#define FPCR_DNOD       0x00008000                      /* denormal disable */
#define FPCR_RAZ        0x00007FFF                      /* zero */
#define FPCR_ERR        (FPCR_IOV|FPCR_INE|FPCR_UNF|FPCR_OVF|FPCR_DZE|FPCR_INV)
#define FPCR_GETFRND(x) (((x) >> FPCR_V_RMOD) & FPCR_M_RMOD)

/* PTE - hardware format */

#define PTE_V_PFN       32                              /* PFN */
#define PFN_MASK        0xFFFFFFFF
#define PTE_V_UWE       15                              /* write enables */
#define PTE_V_SWE       14
#define PTE_V_EWE       13
#define PTE_V_KWE       12
#define PTE_V_URE       11                              /* read enables */
#define PTE_V_SRE       10
#define PTE_V_ERE       9
#define PTE_V_KRE       8
#define PTE_V_GH        5                               /* granularity hint */
#define PTE_M_GH        0x3
#define PTE_GH          (PTE_M_GH << PTE_V_GH)
#define PTE_V_ASM       4                               /* address space match */
#define PTE_V_FOE       3                               /* fault on execute */
#define PTE_V_FOW       2                               /* fault on write */
#define PTE_V_FOR       1                               /* fault on read */
#define PTE_V_V         0                               /* valid */
#define PTE_UWE         (1u << PTE_V_UWE)
#define PTE_SWE         (1u << PTE_V_SWE)
#define PTE_EWE         (1u << PTE_V_EWE)
#define PTE_KWE         (1u << PTE_V_KWE)
#define PTE_URE         (1u << PTE_V_URE)
#define PTE_SRE         (1u << PTE_V_SRE)
#define PTE_ERE         (1u << PTE_V_ERE)
#define PTE_KRE         (1u << PTE_V_KRE)
#define PTE_ASM         (1u << PTE_V_ASM)
#define PTE_FOE         (1u << PTE_V_FOE)
#define PTE_FOW         (1u << PTE_V_FOW)
#define PTE_FOR         (1u << PTE_V_FOR)
#define PTE_V           (1u << PTE_V_V)
#define PTE_MASK        0xFF7F
#define PTE_GETGH(x)    ((((uint32) (x)) >> PTE_V_GH) & PTE_M_GH)
#define VPN_GETLVL1(x)  (((x) >> ((2 * VA_N_LVL) - 3)) & (VA_M_LVL << 3))
#define VPN_GETLVL2(x)  (((x) >> (VA_N_LVL - 3)) & (VA_M_LVL << 3))
#define VPN_GETLVL3(x)  (((x) << 3) & (VA_M_LVL << 3))

#define ACC_E(m)        ((PTE_KRE << (m)) | PTE_FOE | PTE_V)
#define ACC_R(m)        ((PTE_KRE << (m)) | PTE_FOR | PTE_V)
#define ACC_W(m)        ((PTE_KWE << (m)) | PTE_FOW | PTE_V)
#define ACC_M(m)        (((PTE_KRE|PTE_KWE) << (m)) | PTE_FOR | PTE_FOW | PTE_V)

/* Exceptions */

#define ABORT(x)        longjmp (save_env, (x))
#define ABORT1(x,y)     { p1 = (x); longjmp (save_env, (y)); }

#define EXC_RSVI        0x01                            /* reserved instruction */
#define EXC_RSVO        0x02                            /* reserved operand */
#define EXC_ALIGN       0x03                            /* operand alignment */
#define EXC_FPDIS       0x04                            /* flt point disabled */
#define EXC_TBM         0x08                            /* TLB miss */
#define EXC_FOX         0x10                            /* fault on r/w/e */
#define EXC_ACV         0x14                            /* access control viol */
#define EXC_TNV         0x18                            /* translation not valid */
#define EXC_BVA         0x1C                            /* bad address format */
#define EXC_E           0x00                            /* offset for execute */
#define EXC_R           0x01                            /* offset for read */
#define EXC_W           0x02                            /* offset for write */

/* Traps - corresponds to arithmetic trap summary register */

#define TRAP_SWC        0x001                           /* software completion */
#define TRAP_INV        0x002                           /* invalid operand */
#define TRAP_DZE        0x004                           /* divide by zero */
#define TRAP_OVF        0x008                           /* overflow */
#define TRAP_UNF        0x010                           /* underflow */
#define TRAP_INE        0x020                           /* inexact */
#define TRAP_IOV        0x040                           /* integer overflow */
#define TRAP_SUMM_RW    0x07F

/* PALcode */

#define SP              R[30]                           /* stack pointer */
#define MODE_K          0                               /* kernel */
#define MODE_E          1                               /* executive (UNIX user) */
#define MODE_S          2                               /* supervisor */
#define MODE_U          3                               /* user */

#define PAL_UNDF        0                               /* undefined */
#define PAL_VMS         1                               /* VMS */
#define PAL_UNIX        2                               /* UNIX */
#define PAL_NT          3                               /* Windows NT */

/* Machine check error summary register */

#define MCES_INP        0x01                            /* in progress */
#define MCES_SCRD       0x02                            /* sys corr in prog */
#define MCES_PCRD       0x04                            /* proc corr in prog */
#define MCES_DSCRD      0x08                            /* disable system corr */
#define MCES_DPCRD      0x10                            /* disable proc corr */
#define MCES_W1C        (MCES_INP|MCES_SCRD|MCES_PCRD)
#define MCES_DIS        (MCES_DSCRD|MCES_DPCRD)

/* I/O devices */

#define L_BYTE          0                               /* IO request lengths */
#define L_WORD          1
#define L_LONG          2
#define L_QUAD          3

/* Device information block */

typedef struct {                                        /* device info block */
    t_uint64            low;                            /* low addr */
    t_uint64            high;                           /* high addr */
    t_bool              (*read)(t_uint64 pa, t_uint64 *val, uint32 lnt);
    t_bool              (*write)(t_uint64 pa, t_uint64 val, uint32 lnt);
    uint32              ipl;
    } DIB;

/* Interrupt system - 6 levels in EV4 and EV6, 4 in EV5 - software expects 4 */

#define IPL_HMAX        0x17                            /* highest hwre level */
#define IPL_HMIN        0x14                            /* lowest hwre level */
#define IPL_HLVL        (IPL_HMAX - IPL_HMIN + 1)       /* # hardware levels */
#define IPL_SMAX        0x0F                            /* highest swre level */

/* Macros */

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = (PC - 4) & M64

#define SEXT_B_Q(x)     (((x) & B_SIGN)? ((x) | ~((t_uint64) M8)): ((x) & M8))
#define SEXT_W_Q(x)     (((x) & W_SIGN)? ((x) | ~((t_uint64) M16)): ((x) & M16))
#define SEXT_L_Q(x)     (((x) & L_SIGN)? ((x) | ~((t_uint64) M32)): ((x) & M32))
#define NEG_Q(x)        ((~(x) + 1) & M64)
#define ABS_Q(x)        (((x) & Q_SIGN)? NEG_Q (x): (x))

#define SIGN_BDSP       0x100000
#define SIGN_MDSP       0x008000
#define SEXT_MDSP(x)    (((x) & SIGN_MDSP)? \
                        ((x) | ~((t_uint64) I_M_MDSP)): ((x) & I_M_MDSP))
#define SEXT_BDSP(x)    (((x) & SIGN_BDSP)? \
                        ((x) | ~((t_uint64) I_M_BDSP)): ((x) & I_M_BDSP))

/* Opcodes */

enum opcodes {
    OP_PAL,   OP_OPC01, OP_OPC02, OP_OPC03,
    OP_OPC04, OP_OPC05, OP_OPC06, OP_OPC07,
    OP_LDA,   OP_LDAH,  OP_LDBU,  OP_LDQ_U,
    OP_LDWU,  OP_STW,   OP_STB,   OP_STQ_U,
    OP_IALU,  OP_ILOG,  OP_ISHFT, OP_IMUL,
    OP_IFLT,  OP_VAX,   OP_IEEE,  OP_FP,
    OP_MISC,  OP_PAL19, OP_JMP,   OP_PAL1B,
    OP_FLTI,  OP_PAL1D, OP_PAL1E, OP_PAL1F,
    OP_LDF,   OP_LDG,   OP_LDS,   OP_LDT,
    OP_STF,   OP_STG,   OP_STS,   OP_STT,
    OP_LDL,   OP_LDQ,   OP_LDL_L, OP_LDQ_L,
    OP_STL,   OP_STQ,   OP_STL_C, OP_STQ_C,
    OP_BR,    OP_FBEQ,  OP_FBLT,  OP_FBLE,
    OP_BSR,   OP_FBNE,  OP_FBGE,  OP_FBGT,
    OP_BLBC,  OP_BEQ,   OP_BLT,   OP_BLE,
    OP_BLBS,  OP_BNE,   OP_BGE,   OP_BGT
    };

/* Function prototypes */

uint32 ReadI (t_uint64 va);
t_uint64 ReadB (t_uint64 va);
t_uint64 ReadW (t_uint64 va);
t_uint64 ReadL (t_uint64 va);
t_uint64 ReadQ (t_uint64 va);
t_uint64 ReadAccL (t_uint64 va, uint32 acc);
t_uint64 ReadAccQ (t_uint64 va, uint32 acc);
INLINE t_uint64 ReadPB (t_uint64 pa);
INLINE t_uint64 ReadPW (t_uint64 pa);
INLINE t_uint64 ReadPL (t_uint64 pa);
INLINE t_uint64 ReadPQ (t_uint64 pa);
t_bool ReadIO (t_uint64 pa, t_uint64 *val, uint32 lnt);
void WriteB (t_uint64 va, t_uint64 dat);
void WriteW (t_uint64 va, t_uint64 dat);
void WriteL (t_uint64 va, t_uint64 dat);
void WriteQ (t_uint64 va, t_uint64 dat);
void WriteAccL (t_uint64 va, t_uint64 dat, uint32 acc);
void WriteAccQ (t_uint64 va, t_uint64 dat, uint32 acc);
INLINE void WritePB (t_uint64 pa, t_uint64 dat);
INLINE void WritePW (t_uint64 pa, t_uint64 dat);
INLINE void WritePL (t_uint64 pa, t_uint64 dat);
INLINE void WritePQ (t_uint64 pa, t_uint64 dat);
t_bool WriteIO (t_uint64 pa, t_uint64 val, uint32 lnt);
uint32 mmu_set_cm (uint32 mode);
void mmu_set_icm (uint32 mode);
void mmu_set_dcm (uint32 mode);
void arith_trap (uint32 trap, uint32 ir);

#endif
