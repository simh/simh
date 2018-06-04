/* sigma_defs.h: XDS Sigma simulator definitions

   Copyright (c) 2007-2010, Robert M Supnik

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

   The author gratefullly acknowledges the help of George Plue, who provided
   answers to many puzzling questions about how the Sigma series worked.

   22-May-10    RMS     Added check for 64b definitions
*/

#ifndef SIGMA_DEFS_H_
#define SIGMA_DEFS_H_  0

#include "sim_defs.h"                                   /* simulator defns */

#if defined(USE_INT64) || defined(USE_ADDR64)
#error "Sigma 32b does not support 64b values!"
#endif

/* Simulator stops */

#define STOP_INVIOC     1                               /* invalid IO config */
#define STOP_IBKPT      2                               /* breakpoint */
#define STOP_ASTOP      3                               /* address stop */
#define STOP_WAITNOINT  4                               /* WAIT, no intr */
#define STOP_INVPSD     5                               /* invalid PSD */
#define STOP_ROLLBACK   6                               /* >= here, rollback PC */
#define STOP_EXULIM     6                               /* EXU loop */
#define STOP_ILLEG      7                               /* illegal instr */
#define STOP_ILLTRP     8                               /* illegal trap inst */
#define STOP_ILLVEC     9                               /* illegal vector */
#define STOP_TRPT       10                              /* trap inside int/trap */
#define STOP_MAX        15                              /* <= here for all stops */

/* Timers */

#define TMR_RTC         0                               /* clocks */

/* Architectural constants */

#define PASIZE17        17                              /* phys addr width, S5-8 */
#define PASIZE20        20                              /* phys addr width, 5X0 */
#define PASIZE22        22                              /* phys addr width, S9 */
#define PAMASK17        ((1u << PASIZE17) - 1)
#define BPAMASK17       ((1u << (PASIZE17 + 2)) - 1)
#define PAMASK20        ((1u << PASIZE20) - 1)
#define BPAMASK20       ((1u << (PASIZE20 + 2)) - 1)
#define PAMASK22        ((1u << PASIZE22) - 1)
#define BPAMASK22       ((1u << (PASIZE22 + 2)) - 1)
#define MAXMEMSIZE      (1u << PASIZE20)                /* maximum memory */
#define MEMSIZE         (cpu_unit.capac)
#define MEM_IS_NXM(x)   ((x) >= MEMSIZE)
#define BPA_IS_NXM(x)   (((x) >> 2) >= MEMSIZE)
#define VASIZE          17                              /* virtual addr width */
#define VAMASK          ((1u << VASIZE) - 1)            /* virtual addr mask */
#define BVAMASK         ((1u << (VASIZE + 2)) - 1)      /* byte virtual addr mask */
#define RF_NUM          16                              /* number of registers */
#define RF_NBLK         32                              /* max number reg blocks */
#define RF_DFLT         4                               /* default reg blocks */

/* CPU models, options, and variable data */

#define CPUF_STR        (1u << (UNIT_V_UF + 0))         /* byte string */
#define CPUF_DEC        (1u << (UNIT_V_UF + 1))         /* decimal */
#define CPUF_FP         (1u << (UNIT_V_UF + 2))         /* floating point */
#define CPUF_MAP        (1u << (UNIT_V_UF + 3))         /* memory map */
#define CPUF_WLK        (1u << (UNIT_V_UF + 4))         /* write lock protect */
#define CPUF_LAMS       (1u << (UNIT_V_UF + 5))         /* LAS/LMS */
#define CPUF_ALLOPT     (CPUF_STR|CPUF_DEC|CPUF_FP|CPUF_MAP|CPUF_WLK|CPUF_LAMS)
#define CPUF_MSIZE      (1u << (UNIT_V_UF + 6))         /* dummy for memory */

#define CPU_V_S5        0
#define CPU_V_S6        1
#define CPU_V_S7        2
#define CPU_V_S7B       3
#define CPU_V_S8        4                               /* not supported */
#define CPU_V_S9        5                               /* not supported */
#define CPU_V_550       6                               /* not supported */
#define CPU_V_560       7                               /* not supported */
#define CPU_S5          (1u << CPU_V_S5)
#define CPU_S6          (1u << CPU_V_S6)
#define CPU_S7          (1u << CPU_V_S7)
#define CPU_S7B         (1u << CPU_V_S7B)
#define CPU_S8          (1u << CPU_V_S8)
#define CPU_S9          (1u << CPU_V_S9)
#define CPU_550         (1u << CPU_V_550)
#define CPU_560         (1u << CPU_V_560)

#define QCPU_S5         (cpu_model == CPU_V_S5)
#define QCPU_S9         (cpu_model == CPU_V_S9)
#define QCPU_5X0        ((1u << cpu_model) & (CPU_550|CPU_560))
#define QCPU_S567       ((1u << cpu_model) & (CPU_S5|CPU_S6|CPU_S7|CPU_S7B))
#define QCPU_S89        ((1u << cpu_model) & (CPU_S8|CPU_S9))
#define QCPU_S89_5X0    ((1u << cpu_model) & (CPU_S8|CPU_S9|CPU_550|CPU_560))
#define QCPU_BIGM       ((1u << cpu_model) & (CPU_S7B|CPU_S9|CPU_550|CPU_560))

#define CPU_MUNIT_SIZE  (1u << 15)                      /* mem unit size */

typedef struct {
    uint32              psw1_mbz;                       /* PSW1 mbz */
    uint32              psw2_mbz;                       /* PSW2 mbz */
    uint32              mmc_cm_map1;                    /* MMC mode 1 cmask */
    uint32              pamask;                         /* physical addr mask */
    uint32              eigrp_max;                      /* max num ext int groups */
    uint32              chan_max;                       /* max num channels */
    uint32              iocc;                           /* IO instr CC bits */
    uint32              std;                            /* required options */
    uint32              opt;                            /* variable options */
    } cpu_var_t;

/* Instruction format */

#define INST_V_IND      31                              /* indirect */
#define INST_IND        (1u << INST_V_IND)
#define INST_V_OP       24                              /* opcode */
#define INST_M_OP       0x7F
#define INST_V_RN       20                              /* register */
#define INST_M_RN       0xF
#define INST_V_XR       17                              /* index */
#define INST_M_XR       0x7
#define INST_V_ADDR     0                               /* 17b addr */
#define INST_M_ADDR     0x1FFFF
#define INST_V_LIT      0                               /* 20b literal or addr */
#define INST_M_LIT      0xFFFFF
#define TST_IND(x)      ((x) & INST_IND)
#define I_GETOP(x)      (((x) >> INST_V_OP) & INST_M_OP)
#define I_GETRN(x)      (((x) >> INST_V_RN) & INST_M_RN)
#define I_GETXR(x)      (((x) >> INST_V_XR) & INST_M_XR)
#define I_GETADDR(x)    (((x) >> INST_V_ADDR) & INST_M_ADDR)
#define I_GETADDR20(x)  (((x) >> INST_V_ADDR) & PAMASK20)
#define I_GETLIT(x)     (((x) >> INST_V_LIT) & INST_M_LIT)
#define IRB(x)          (1u << (31 - (x)))

/* Shift instructions */

#define SHF_V_SOP       8                               /* shift operation */
#define SHF_M_SOP       0x7
#define SHF_V_SC        0                               /* shift count */
#define SHF_M_SC        0x7F
#define SCSIGN          0x40
#define SHF_GETSOP(x)   (((x) >> SHF_V_SOP) & SHF_M_SOP)
#define SHF_GETSC(x)    (((x) >> SHF_V_SC) & SHF_M_SC)

/* String instructions */

#define S_V_MCNT        24                              /* string mask/count */
#define S_M_MCNT        0xFF
#define S_MCNT          (S_M_MCNT << S_V_MCNT)
#define S_GETMCNT(x)    (((x) >> S_V_MCNT) & S_M_MCNT)
#define S_ADDRINC       (S_MCNT + 1)

/* Data types */

#define WMASK           0xFFFFFFFF                      /* word */
#define WSIGN           0x80000000                      /* word sign */
#define LITMASK         (INST_M_LIT)                    /* literal */
#define LITSIGN         0x80000                         /* literal sign */
#define HMASK           0xFFFF                          /* halfword mask */
#define HSIGN           0x8000                          /* halfword sign */
#define BMASK           0xFF                            /* byte */
#define BSIGN           0x80                            /* byte sign */
#define RNMASK          (INST_M_RN)                     /* reg lit */
#define RNSIGN          0x08                            /* reg lit sign */

#define FP_V_SIGN       31                              /* sign */
#define FP_SIGN         (1u << FP_V_SIGN)
#define FP_V_EXP        24                              /* exponent */
#define FP_M_EXP        0x7F
#define FP_BIAS         0x40                            /* exponent bias */
#define FP_V_FRHI       0                               /* high fraction */
#define FP_M_FRHI       0x00FFFFFF
#define FP_NORM         0x00F00000
#define FP_M_FRLO       0xFFFFFFFF                      /* low fraction */
#define FP_GETSIGN(x)   (((x) >> FP_V_SIGN) & 1)
#define FP_GETEXP(x)    (((x) >> FP_V_EXP) & FP_M_EXP)
#define FP_GETFRHI(x)   (((x) >> FP_V_FRHI) & FP_M_FRHI)
#define FP_GETFRLO(x)   ((x) & FP_M_FRLO)

/* PSW1 fields */

#define PSW1_V_CC       28                              /* cond codes */
#define PSW1_M_CC       0xF
#define  CC1            0x8
#define  CC2            0x4
#define  CC3            0x2
#define  CC4            0x1
#define PSW1_V_FR       27                              /* fp mode controls */
#define PSW1_V_FS       26
#define PSW1_V_FZ       25
#define PSW1_V_FN       24
#define PSW1_V_FPC      24                              /* as a group */
#define PSW1_M_FPC      0xF
#define PSW1_FPC        (PSW1_M_FPC << PSW1_V_FPC)
#define PSW1_V_MS       23                              /* master/slave */
#define PSW1_V_MM       22                              /* memory map */
#define PSW1_V_DM       21                              /* decimal trap */
#define PSW1_V_AM       20                              /* arithmetic trap */
#define PSW1_V_AS       19                              /* EBCDIC/ASCII, S9 */
#define PSW1_V_XA       15                              /* ext addr flag, S9 */
#define PSW1_V_PC       0                               /* PC */
#define PSW1_M_PC       (VAMASK)
#define PSW1_FR         (1u << PSW1_V_FR)
#define PSW1_FS         (1u << PSW1_V_FS)
#define PSW1_FZ         (1u << PSW1_V_FZ)
#define PSW1_FN         (1u << PSW1_V_FN)
#define PSW1_MS         (1u << PSW1_V_MS)
#define PSW1_MM         (1u << PSW1_V_MM)
#define PSW1_DM         (1u << PSW1_V_DM)
#define PSW1_AM         (1u << PSW1_V_AM)
#define PSW1_AS         (1u << PSW1_V_AS)
#define PSW1_XA         (1u << PSW1_V_XA)
#define PSW1_CCMASK     (PSW1_M_CC << PSW1_V_CC)
#define PSW1_PCMASK     (PSW1_M_PC << PSW1_V_PC)
#define PSW1_GETCC(x)   (((x) >> PSW1_V_CC) & PSW1_M_CC)
#define PSW1_GETPC(x)   (((x) >> PSW1_V_PC) & PSW1_M_PC)
#define PSW1_DFLT       0

/* PSW2 fields */

#define PSW2_V_WLK      28                              /* write key */
#define PSW2_M_WLK      0xF
#define PSW2_V_CI       26                              /* counter int inhibit */
#define PSW2_V_II       25                              /* IO int inhibit */
#define PSW2_V_EI       24                              /* external int inhibit */
#define PSW2_V_INH      (PSW2_V_EI)                     /* inhibits as a group */
#define PSW2_M_INH      0x7
#define PSW2_V_MA9      23                              /* mode altered, S9 */
#define PSW2_V_EA       16                              /* ext addr, S9 */
#define PSW2_M_EA       0x3F
#define PSW2_EA         (PSW2_M_EA << PSW2_V_EA)
#define PSW2_V_TSF      8                               /* trapped status, S9 */
#define PSW2_M_TSF      0xFF
#define PSW2_TSF        (PSW2_M_TSF << PSW2_V_TSF)
#define PSW2_V_RP       4                               /* register block ptr */
#define PSW2_M_RP5B     0x1F
#define PSW2_M_RP4B     0xF
#define PSW2_RP         ((QCPU_S567? PSW2_M_RP5B: PSW2_M_RP4B) << PSW2_V_RP)
#define PSW2_V_RA       3                               /* reg altered, 9,5X0 */
#define PSW2_V_MA5X0    2                               /* mode altered, 5X0 */
#define PSW2_CI         (1u << PSW2_V_CI)
#define PSW2_II         (1u << PSW2_V_II)
#define PSW2_EI         (1u << PSW2_V_EI)
#define PSW2_ALLINH     (PSW2_CI|PSW2_II|PSW2_EI)       /* all inhibits */
#define PSW2_MA9        (1u << PSW2_V_MA9)
#define PSW2_RA         (1u << PSW2_V_RA)
#define PSW2_MA5X0      (1u << PSW2_V_MA5X0)
#define PSW2_WLKMASK    (PSW2_M_WLK << PSW2_V_WLK)
#define PSW2_RPMASK     (PSW2_M_RP << PSW2_V_RP)
#define PSW2_GETINH(x)  (((x) >> PSW2_V_INH) & PSW2_M_INH);
#define PSW2_GETWLK(x)  (((x) >> PSW2_V_WLK) & PSW2_M_WLK)
#define PSW2_GETRP(x)   (((x) & PSW2_RP) >> PSW2_V_RP)
#define PSW2_DFLT       0

/* Stack pointers */

#define SP_V_TS         31                              /* space trap enable */
#define SP_TS           (1u << SP_V_TS)
#define SP_V_SPC        16                              /* space */
#define SP_M_SPC        0x7FFF
#define SP_V_TW         15                              /* words trap enable */
#define SP_TW           (1u << SP_V_TW)
#define SP_V_WDS        0                               /* words */
#define SP_M_WDS        0x7FFF
#define SP_GETSPC(x)    (((x) >> SP_V_SPC) & SP_M_SPC)
#define SP_GETWDS(x)    (((x) >> SP_V_WDS) & SP_M_WDS)

/* System stack pointer (5X0 only) */

#define SSP_TOS         0                               /* system stack */
#define SSP_SWC         1                               /* space/word count */
#define SSP_DFLT_PSW1   2                               /* default PSD */
#define SSP_DFLT_PSW2   3
#define SSP_FR_LNT      28                              /* frame length */
#define SSP_FR_RN       0                               /* registers */
#define SSP_FR_PSW1     24                              /* PSD */
#define SSP_FR_PSW2     25
#define SSP_FR_PSW4     27

/* The Sigma series had word addressable memory, but byte addressable
   data.  Virtual addresses in the simulator are BYTE addresses, and
   these definitions are in terms of a byte address (word << 2). */

#define VA_NUM_PAG     (1 << (VASIZE - (BVA_V_PAG - 2)))
#define PA_NUM_PAG     (1 << (PASIZE22 - (BVA_V_PAG - 2)))
#define BVA_V_OFF       0                               /* offset */
#define BVA_M_OFF       0x7FF
#define BVA_V_PAG       11                              /* page */
#define BVA_M_PAG       0xFF
#define BVA_GETOFF(x)   (((x) >> BVA_V_OFF) & BVA_M_OFF)
#define BVA_GETPAG(x)   (((x) >> BVA_V_PAG) & BVA_M_PAG)
#define BPA_V_PAG       (BVA_V_PAG)                     /* phys page */
#define BPA_M_PAG       0x1FFF
#define BPA_GETPAG(x)   (((x) >> BPA_V_PAG) & BPA_M_PAG)

/* Memory maps */

#define MMC_V_CNT       24                              /* count */
#define MMC_M_CNT       0xFF
#define MMC_CNT         (MMC_M_CNT << MMC_V_CNT)
#define MMC_V_CS        9                               /* start of page */
                                                        /* map 1: 2b locks, per model */
#define MMC_M_CS2       0xFC                            /* map 2: access controls */
#define MMC_M_CS3       0x7FE                           /* map 3: 4b locks */
#define MMC_M_CS4       0xFF                            /* map 4: 8b relocation */
#define MMC_M_CS5       0xFF                            /* map 5: 13b relocation */
#define MMC_GETCNT(x)   (((x) >> MMC_V_CNT) & MMC_M_CNT)
#define MMC_L_CS1       (VA_NUM_PAG)                    /* map lengths */
#define MMC_L_CS2       (VA_NUM_PAG)
#define MMC_L_CS3       (PA_NUM_PAG)
#define MMC_L_CS4       (VA_NUM_PAG)
#define MMC_L_CS5       (VA_NUM_PAG)

/* Trap codes */

#define TR_V_FL         17                              /* trap flag */
#define TR_FL           (1u << TR_V_FL)
#define TR_V_PDF        16                              /* proc detected fault */
#define TR_PDF          (1u << TR_V_FL)
#define TR_V_CC         12                              /* or'd to CC/addr offset */
#define TR_M_CC         0xF
#define TR_V_VEC        0                               /* trap address */
#define TR_M_VEC        0xFFF
#define TR_GETVEC(x)    (((x) >> TR_V_VEC) & TR_M_VEC)
#define TR_GETCC(x)     (((x) >> TR_V_CC) & TR_M_CC)

#define TR_NXI          (TR_FL|0x8040)                  /* non-existent inst */
#define TR_NXM          (TR_FL|0x4040)                  /* non-existent memory */
#define TR_PRV          (TR_FL|0x2040)                  /* privileged inst */
#define TR_MPR          (TR_FL|0x1040)                  /* mem protect violation */
#define TR_WLK          (TR_FL|0x3040)                  /* write lock (5x0 only) */
#define TR_UNI          (TR_FL|0x0041)                  /* unimplemented inst */
#define TR_PSH          (TR_FL|0x0042)                  /* pushdown overflow */
#define TR_FIX          (TR_FL|0x0043)                  /* fixed point arith */
#define TR_FLT          (TR_FL|0x0044)                  /* floating point arith */
#define TR_DEC          (TR_FL|0x0045)                  /* decimal arithmetic */
#define TR_WAT          (TR_FL|0x0046)                  /* watchdog timer */
#define TR_47           (TR_FL|0x0047)                  /* 5X0 - WD trap */
#define TR_C1(x)        (TR_FL|0x0048|((x) << TR_V_CC)) /* call instruction */
#define TR_C2(x)        (TR_FL|0x0049|((x) << TR_V_CC)) /* call instruction */
#define TR_C3(x)        (TR_FL|0x004A|((x) << TR_V_CC)) /* call instruction */
#define TR_C4(x)        (TR_FL|0x004B|((x) << TR_V_CC)) /* call instruction */
#define TR_NESTED       (TR_FL|TR_PDF|0xF04D)           /* 9,5X0 - fault in inv/trap */
#define TR_INVTRP       (TR_FL|TR_PDF|0xC04D)           /* 9,5X0 - inv int/trap inst */
#define TR_INVRPT       (TR_FL|TR_PDF|0x804D)           /* 9 - inv new RP in trap */
#define TR_INVSSP       (TR_FL|TR_PDF|0x404D)           /* 5X0 - inv SSP for PLS */
#define TR_INVMMC       (TR_FL|TR_PDF|0x204D)           /* 9,5X0 - inv MMC config */
#define TR_INVREG       (TR_FL|0x104D)                  /* 9,5x0 - inv reg num */
#define TR_INVRPN       (TR_FL|TR_PDF|0x004D)           /* 9 - inv new RP, non-trap */

/* Effective address and memory access routines interface

   The access types are defined to make the following equation work:

   trap if ((access_type != 0) && (access_control >= access_type))

   The length codes are defined so that length in bytes = 1 << length_code */

#define PH              0x0                             /* physical */
#define VW              0x1                             /* write */
#define VI              0x2                             /* instruction */
#define VR              0x3                             /* read */
#define VNT             0x4                             /* no traps */

#define BY              0x0                             /* byte */
#define HW              0x1                             /* halfword */
#define WD              0x2                             /* word */
#define DW              0x3                             /* doubleword */

/* Interrupt groups - the Sigma's have flexibly configured interrupt groups
   of various widths that map non-uniformly to control register bits */

typedef struct {
    uint32              psw2_inh;                       /* PSW2 inhibit */
    uint32              nbits;                          /* number of bits */
    uint32              vecbase;                        /* vector base */
    uint32              rwgroup;                        /* RWdirect group */
    uint32              regbit;                         /* RWdirect reg bit */
    } int_grp_t;

#define INTG_MAX        17                              /* max # int groups */
#define EIGRP_DFLT      1                               /* dflt # ei groups */
#define INTG_OVR        0                               /* override group */
#define INTG_CTR        1                               /* counter group */
#define INTG_IO         2                               /* I/O group */
#define INTGIO_IO        0x2                            /* I/O interrupt */
#define INTGIO_PANEL     0x1                            /* panel interrupt */
#define INTG_E2         3                               /* ext group 2 */
#define INTG_E3         4                               /* ext group 3 */

#define INT_V_GRP       4                               /* interrupt group */
#define INT_M_GRP       0x1F
#define INT_V_BIT       0                               /* interrupt bit */
#define INT_M_BIT       0xF
#define INT_GETGRP(x)   (((x) >> INT_V_GRP) & INT_M_GRP)
#define INT_GETBIT(x)   (((x) >> INT_V_BIT) & INT_M_BIT)
#define INTV(x,y)       (((x) << INT_V_GRP) | ((y) << INT_V_BIT))
#define NO_INT          (INTV (INTG_MAX, 0))

#define VEC_C1P         0x52                            /* clock pulse vectors */
#define VEC_C4P         0x55
#define VEC_C1Z         0x58                            /* clock zero vector */

/* Integer data operations and condition codes */

#define SEXT_RN_W(x)    (((x) & RNSIGN)? ((x) | ~RNMASK): ((x) & RNMASK))
#define SEXT_H_W(x)     (((x) & HSIGN)? ((x) | ~HMASK): ((x) & HMASK))
#define SEXT_LIT_W(x)   (((x) & LITSIGN)? ((x) | ~LITMASK): ((x) & LITMASK))
#define NEG_W(x)        ((~(x) + 1) & WMASK)
#define NEG_D(x,y)      do { y = NEG_W(y); x = (~(x) + ((y) == 0)) & WMASK; } while (0)
#define CC34_W(x)       CC = (((x) & WSIGN)? \
                                ((CC & ~CC3) | CC4): \
                                (((x) != 0)? \
                                    ((CC & ~CC4) | CC3): \
                                    (CC & ~(CC3|CC4))))
#define CC234_CMP(x,y)  CC = (CC & CC1) | Cmp32 ((x), (y)) | \
                             (((x) & (y))? CC2: 0)

/* Instructions */

enum opcodes {
    OP_00,   OP_01,   OP_LCFI, OP_03,   OP_CAL1, OP_CAL2, OP_CAL3, OP_CAL4,
    OP_PLW,  OP_PSW,  OP_PLM,  OP_PSM,  OP_PLS,  OP_PSS,  OP_LPSD, OP_XPSD,
    OP_AD,   OP_CD,   OP_LD,   OP_MSP,  OP_14,   OP_STD,  OP_16,   OP_17,
    OP_SD,   OP_CLM,  OP_LCD,  OP_LAD,  OP_FSL,  OP_FAL,  OP_FDL,  OP_FML,
    OP_AI,   OP_CI,   OP_LI,   OP_MI,   OP_SF,   OP_S,    OP_LAS,  OP_27,
    OP_CVS,  OP_CVA,  OP_LM,   OP_STM,  OP_LRA,  OP_LMS,  OP_WAIT, OP_LRP,
    OP_AW,   OP_CW,   OP_LW,   OP_MTW,  OP_LVAW, OP_STW,  OP_DW,   OP_MW,
    OP_SW,   OP_CLR,  OP_LCW,  OP_LAW,  OP_FSS,  OP_FAS,  OP_FDS,  OP_FMS,
    OP_TTBS, OP_TBS,  OP_42,   OP_43,   OP_ANLZ, OP_CS,   OP_XW,   OP_STS,
    OP_EOR,  OP_OR,   OP_LS,   OP_AND,  OP_SIO,  OP_TIO,  OP_TDV,  OP_HIO,
    OP_AH,   OP_CH,   OP_LH,   OP_MTH,  OP_54,   OP_STH,  OP_DH,   OP_MH,
    OP_SH,   OP_59,   OP_LCH,  OP_LAH,  OP_5C,   OP_5D,   OP_5E,   OP_5F,
    OP_CBS,  OP_MBS,  OP_62,   OP_EBS,  OP_BDR,  OP_BIR,  OP_AWM,  OP_EXU,
    OP_BCR,  OP_BCS,  OP_BAL,  OP_INT,  OP_RD,   OP_WD,   OP_AIO,  OP_MMC,
    OP_LCF,  OP_CB,   OP_LB,   OP_MTB,  OP_STCF, OP_STB,  OP_PACK, OP_UNPK,
    OP_DS,   OP_DA,   OP_DD,   OP_DM,   OP_DSA,  OP_DC,   OP_DL,   OP_DST
    };

/* Function prototypes */

uint32 Ea (uint32 ir, uint32 *bva, uint32 acc, uint32 lnt);
uint32 ReadB (uint32 bva, uint32 *dat, uint32 acc);
uint32 ReadH (uint32 bva, uint32 *dat, uint32 acc);
uint32 ReadW (uint32 bva, uint32 *dat, uint32 acc);
uint32 ReadD (uint32 bva, uint32 *dat, uint32 *dat1, uint32 acc);
uint32 WriteB (uint32 bva, uint32 dat, uint32 acc);
uint32 WriteH (uint32 bva, uint32 dat, uint32 acc);
uint32 WriteW (uint32 bva, uint32 dat, uint32 acc);
uint32 WriteD (uint32 bva, uint32 dat, uint32 dat1, uint32 acc);
uint32 ReadMemVW (uint32 bva, uint32 *dat, uint32 acc);
uint32 WriteMemVW (uint32 bva, uint32 dat, uint32 acc);
uint32 ReadPB (uint32 ba, uint32 *dat);
uint32 WritePB (uint32 ba, uint32 dat);
uint32 ReadPW (uint32 pa, uint32 *dat);
uint32 WritePW (uint32 pa, uint32 dat);
uint32 ReadHist (uint32 bva, uint32 *dat, uint32 *dat1, uint32 acc, uint32 lnt);

#endif