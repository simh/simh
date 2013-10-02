/* pdp11_defs.h: PDP-11 simulator definitions

   Copyright (c) 1993-2011, Robert M Supnik

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

   The author gratefully acknowledges the help of Max Burnet, Megan Gentry,
   and John Wilson in resolving questions about the PDP-11

   02-Sep-13    RMS     Added third Massbus adapter and RS drive
   11-Dec-11    RMS     Fixed priority of PIRQ vs IO; added INT_INTERNALn
   22-May-10    RMS     Added check for 64b definitions
   19-Nov-08    RMS     Moved I/O support routines to I/O library
   16-May-08    RMS     Added KE11A, DC11 support
   02-Feb-08    RMS     Fixed DMA memory address limit test (found by John Dundas)
   25-Jan-08    RMS     Added RC11, KG11A support (from John Dundas)
   16-Dec-06    RMS     Added TA11 support
   29-Oct-06    RMS     Added clock coscheduling
   06-Jul-06    RMS     Added multiple KL11/DL11 support
   26-Jun-06    RMS     Added RF11 support
   24-May-06    RMS     Added 11/44 DR support (from CIS diagnostic)
   17-May-06    RMS     Added CR11/CD11 support (from John Dundas)
   30-Sep-04    RMS     Added Massbus support
                        Removed Map_Addr prototype
                        Removed map argument from Unibus routines
                        Added framework for model selection
   28-May-04    RMS     Added DHQ support
   25-Jan-04    RMS     Removed local debug logging support
   22-Dec-03    RMS     Added second DEUNA/DELUA support
   18-Oct-03    RMS     Added DECtape off reel message
   19-May-03    RMS     Revised for new conditional compilation
   05-Apr-03    RMS     Fixed bug in MMR1 update (found by Tim Stark)
   28-Feb-03    RMS     Added TM logging support
   19-Jan-03    RMS     Changed mode definitions for Apple Dev Kit conflict
   11-Nov-02    RMS     Changed log definitions to be VAX compatible
   10-Oct-02    RMS     Added vector information to DIB
                        Changed DZ11 vector to Unibus standard
                        Added DEQNA/DELQA, DEUNA/DELUA support
                        Added multiple RQDX3, autoconfigure support
   12-Sep-02    RMS     Added TMSCP, KW11P,and RX211 support
   28-Apr-02    RMS     Clarified PDF ACF mnemonics
   22-Apr-02    RMS     Added HTRAP, BPOK maint register flags, MT_MAXFR
   06-Mar-02    RMS     Changed system type to KDJ11A
   20-Jan-02    RMS     Added multiboard DZ11 support
   09-Nov-01    RMS     Added bus map support
   07-Nov-01    RMS     Added RQDX3 support
   26-Oct-01    RMS     Added symbolic definitions for IO page
   19-Oct-01    RMS     Added DZ definitions
   15-Oct-01    RMS     Added logging capabilities
   07-Sep-01    RMS     Revised for multilevel interrupts
   01-Jun-01    RMS     Added DZ11 support
   23-Apr-01    RMS     Added RK611 support
   05-Apr-01    RMS     Added TS11/TSV05 support
   10-Feb-01    RMS     Added DECtape support
*/

#ifndef PDP11_DEFS_H
#define PDP11_DEFS_H   0

#ifndef VM_PDP11
#define VM_PDP11        0
#endif

#include "sim_defs.h"                                   /* simulator defns */
#include <setjmp.h>

#if defined(USE_INT64) || defined(USE_ADDR64)
#error "PDP-11 does not support 64b values!"
#endif

/* Architectural constants */

#define STKL_R          0340                            /* stack limit */
#define STKL_Y          0400
#define VASIZE          0200000                         /* 2**16 */
#define VAMASK          (VASIZE - 1)                    /* 2**16 - 1 */
#define MEMSIZE64K      0200000                         /* 2**16 */
#define INIMEMSIZE      001000000                       /* 2**18 */
#define UNIMEMSIZE      001000000                       /* 2**18 */
#define UNIMASK         (UNIMEMSIZE - 1)                /* 2**18 - 1 */
#define IOPAGEBASE      017760000                       /* 2**22 - 2**13 */
#define IOPAGESIZE      000020000                       /* 2**13 */
#define IOPAGEMASK      (IOPAGESIZE - 1)                /* 2**13 - 1 */
#define MAXMEMSIZE      020000000                       /* 2**22 */
#define PAMASK          (MAXMEMSIZE - 1)                /* 2**22 - 1 */
#define MEMSIZE         (cpu_unit.capac)
#define ADDR_IS_MEM(x)  (((t_addr) (x)) < cpu_memsize)  /* use only in sim! */
#define DMASK           0177777

/* CPU models */

#define MOD_1103        0
#define MOD_1104        1
#define MOD_1105        2
#define MOD_1120        3
#define MOD_1123        4
#define MOD_1123P       5
#define MOD_1124        6
#define MOD_1134        7
#define MOD_1140        8
#define MOD_1144        9
#define MOD_1145        10
#define MOD_1160        11
#define MOD_1170        12
#define MOD_1173        13
#define MOD_1153        14
#define MOD_1173B       15
#define MOD_1183        16
#define MOD_1184        17
#define MOD_1193        18
#define MOD_1194        19
#define MOD_T           20

#define CPUT_03         (1u << MOD_1103)                /* LSI-11 */
#define CPUT_04         (1u << MOD_1104)                /* 11/04 */
#define CPUT_05         (1u << MOD_1105)                /* 11/05 */
#define CPUT_20         (1u << MOD_1120)                /* 11/20 */
#define CPUT_23         (1u << MOD_1123)                /* 11/23 */
#define CPUT_23P        (1u << MOD_1123P)               /* 11/23+ */
#define CPUT_24         (1u << MOD_1124)                /* 11/24 */
#define CPUT_34         (1u << MOD_1134)                /* 11/34 */
#define CPUT_40         (1u << MOD_1140)                /* 11/40 */
#define CPUT_44         (1u << MOD_1144)                /* 11/44 */
#define CPUT_45         (1u << MOD_1145)                /* 11/45 */
#define CPUT_60         (1u << MOD_1160)                /* 11/60 */
#define CPUT_70         (1u << MOD_1170)                /* 11/70 */
#define CPUT_73         (1u << MOD_1173)                /* 11/73 */
#define CPUT_53         (1u << MOD_1153)                /* 11/53 */
#define CPUT_73B        (1u << MOD_1173B)               /* 11/73B */
#define CPUT_83         (1u << MOD_1183)                /* 11/83 */
#define CPUT_84         (1u << MOD_1184)                /* 11/84 */
#define CPUT_93         (1u << MOD_1193)                /* 11/93 */
#define CPUT_94         (1u << MOD_1194)                /* 11/94 */
#define CPUT_T          (1u << MOD_T)                   /* T-11 */

#define CPUT_F          (CPUT_23|CPUT_23P|CPUT_24)      /* all F11's */
#define CPUT_J          (CPUT_53|CPUT_73|CPUT_73B| \
                         CPUT_83|CPUT_84|CPUT_93|CPUT_94)
#define CPUT_JB         (CPUT_73B|CPUT_83|CPUT_84)      /* KDJ11B */
#define CPUT_JE         (CPUT_93|CPUT_94)               /* KDJ11E */
#define CPUT_JU         (CPUT_84|CPUT_94)               /* KTJ11B UBA */
#define CPUT_ALL        0xFFFFFFFF

/* CPU options */

#define BUS_U           (1u << 0)                       /* Unibus */
#define BUS_Q           (0)                             /* Qbus */
#define OPT_EIS         (1u << 1)                       /* EIS */
#define OPT_FIS         (1u << 2)                       /* FIS */
#define OPT_FPP         (1u << 3)                       /* FPP */
#define OPT_CIS         (1u << 4)                       /* CIS */
#define OPT_MMU         (1u << 5)                       /* MMU */
#define OPT_RH11        (1u << 6)                       /* RH11 */
#define OPT_PAR         (1u << 7)                       /* parity */
#define OPT_UBM         (1u << 8)                       /* UBM */

#define CPUT(x)         ((cpu_type & (x)) != 0)
#define CPUO(x)         ((cpu_opt & (x)) != 0)
#define UNIBUS          (cpu_opt & BUS_U)
extern uint32 cpu_model, cpu_type, cpu_opt;

/* Feature sets

   SDSD                 source addr, dest addr, source fetch, dest fetch
   SR                   switch register
   DR                   display register
   RTT                  RTT instruction
   SXS                  SXT, XOR, SOB instructions
   MARK                 MARK instruction
   SPL                  SPL instruction
   MXPY                 MTPI, MTPD, MFPI, MFPD instructions
   MXPS                 MTPS, MFPS instructions
   MFPT                 MFPT instruction
   CSM                  CSM instruction
   TSWLK                TSTSET, WRLCK instructions
   PSW                  PSW register
   EXPT                 explicit PSW writes can alter T-bit
   IOSR                 general registers readable from programs in IO space
   2REG                 dual register set
   MMR3                 MMR3 register
   MMTR                 mem mgt traps
   STKLR                STKLIM register
   STKLF                fixed stack limit
   SID                  supervisor mode, I/D spaces
   ODD                  odd address trap
   HALT4                halt in kernel mode traps to 4
   JREG4                JMP/JSR R traps to 4
   STKA                 stop on stack abort
   LTCR                 LTC CSR
   LTCM                 LTC CSR<7>
*/

#define IS_SDSD         (CPUT_20|CPUT_F|CPUT_40|CPUT_60|CPUT_J|CPUT_T)
#define HAS_SR          (CPUT_04|CPUT_05|CPUT_20|CPUT_34|CPUT_40| \
                         CPUT_44|CPUT_45|CPUT_60|CPUT_70)
#define HAS_DR          (CPUT_04|CPUT_05|CPUT_20|CPUT_24|CPUT_34| \
                         CPUT_40|CPUT_44|CPUT_45|CPUT_60|CPUT_70)
#define HAS_RTT         (CPUT_03|CPUT_04|CPUT_F|CPUT_34|CPUT_40| \
                         CPUT_44|CPUT_45|CPUT_60|CPUT_70|CPUT_J|CPUT_T)
#define HAS_SXS         (CPUT_03|CPUT_F|CPUT_34|CPUT_40|CPUT_44| \
                         CPUT_45|CPUT_60|CPUT_70|CPUT_J|CPUT_T)
#define HAS_MARK        (CPUT_03|CPUT_F|CPUT_34|CPUT_40|CPUT_44| \
                         CPUT_45|CPUT_60|CPUT_70|CPUT_J)
#define HAS_SPL         (CPUT_44|CPUT_45|CPUT_70|CPUT_J)
#define HAS_MXPY        (CPUT_F|CPUT_34|CPUT_40|CPUT_44|CPUT_45| \
                         CPUT_60|CPUT_70|CPUT_J)
#define HAS_MXPS        (CPUT_03|CPUT_F|CPUT_34|CPUT_J|CPUT_T)
#define HAS_MFPT        (CPUT_F|CPUT_44|CPUT_J|CPUT_T)
#define HAS_CSM         (CPUT_44|CPUT_J)
#define HAS_TSWLK       (CPUT_J)
#define HAS_PSW         (CPUT_04|CPUT_05|CPUT_20|CPUT_F|CPUT_34|CPUT_40| \
                         CPUT_44|CPUT_45|CPUT_60|CPUT_70|CPUT_J)
#define HAS_EXPT        (CPUT_04|CPUT_05|CPUT_20)
#define HAS_IOSR        (CPUT_04|CPUT_05)
#define HAS_2REG        (CPUT_45|CPUT_70|CPUT_J)
#define HAS_MMR3        (CPUT_F|CPUT_44|CPUT_45|CPUT_70|CPUT_J)
#define HAS_MMTR        (CPUT_45|CPUT_70)
#define HAS_STKLR       (CPUT_45|CPUT_60|CPUT_70)
#define HAS_STKLF       (CPUT_04|CPUT_05|CPUT_20|CPUT_F|CPUT_34| \
                         CPUT_40|CPUT_44|CPUT_J)
#define HAS_SID         (CPUT_44|CPUT_45|CPUT_70|CPUT_J)
#define HAS_ODD         (CPUT_04|CPUT_05|CPUT_20|CPUT_34|CPUT_40| \
                         CPUT_44|CPUT_45|CPUT_60|CPUT_70|CPUT_J)
#define HAS_HALT4       (CPUT_44|CPUT_45|CPUT_70|CPUT_J)
#define HAS_JREG4       (CPUT_03|CPUT_04|CPUT_05|CPUT_20|CPUT_F| \
                         CPUT_34|CPUT_40|CPUT_60|CPUT_T)
#define STOP_STKA       (CPUT_03|CPUT_04|CPUT_05|CPUT_20|CPUT_34|CPUT_44)
#define HAS_LTCR        (CPUT_04|CPUT_05|CPUT_20|CPUT_23P|CPUT_24| \
                         CPUT_34|CPUT_40|CPUT_44|CPUT_45|CPUT_60| \
                         CPUT_70|CPUT_J)
#define HAS_LTCM        (CPUT_04|CPUT_05|CPUT_20|CPUT_24|CPUT_34| \
                         CPUT_40|CPUT_44|CPUT_45|CPUT_60|CPUT_70|CPUT_J)

/* Protection modes */

#define MD_KER          0
#define MD_SUP          1
#define MD_UND          2
#define MD_USR          3

/* I/O access modes */

#define READ            0
#define READC           1                               /* read console */
#define WRITE           2
#define WRITEC          3                               /* write console */
#define WRITEB          4

/* PSW */

#define PSW_V_C         0                               /* condition codes */
#define PSW_V_V         1
#define PSW_V_Z         2
#define PSW_V_N         3
#define PSW_V_TBIT      4                               /* trace trap */
#define PSW_V_IPL       5                               /* int priority */
#define PSW_V_FPD       8                               /* first part done */
#define PSW_V_RS        11                              /* register set */
#define PSW_V_PM        12                              /* previous mode */
#define PSW_V_CM        14                              /* current mode */
#define PSW_CC          017
#define PSW_TBIT        (1 << PSW_V_TBIT)
#define PSW_PM          (3 << PSW_V_PM)

/* FPS */

#define FPS_V_C         0                               /* condition codes */
#define FPS_V_V         1
#define FPS_V_Z         2
#define FPS_V_N         3
#define FPS_V_T         5                               /* truncate */
#define FPS_V_L         6                               /* long */
#define FPS_V_D         7                               /* double */
#define FPS_V_IC        8                               /* ic err int */
#define FPS_V_IV        9                               /* overflo err int */
#define FPS_V_IU        10                              /* underflo err int */
#define FPS_V_IUV       11                              /* undef var err int */
#define FPS_V_ID        14                              /* int disable */
#define FPS_V_ER        15                              /* error */

/* PIRQ */

#define PIRQ_PIR1       0001000
#define PIRQ_PIR2       0002000
#define PIRQ_PIR3       0004000
#define PIRQ_PIR4       0010000
#define PIRQ_PIR5       0020000
#define PIRQ_PIR6       0040000
#define PIRQ_PIR7       0100000
#define PIRQ_IMP        0177356                         /* implemented bits */
#define PIRQ_RW         0177000                         /* read/write bits */

/* STKLIM */

#define STKLIM_RW       0177400

/* MMR0 */

#define MMR0_MME        0000001                         /* mem mgt enable */
#define MMR0_V_PAGE     1                               /* offset to pageno */
#define MMR0_M_PAGE     077                             /* mask for pageno */
#define MMR0_PAGE       (MMR0_M_PAGE << MMR0_V_PAGE)
#define MMR0_IC         0000200                         /* instr complete */
#define MMR0_MAINT      0000400                         /* maintenance */
#define MMR0_TENB       0001000                         /* trap enable */
#define MMR0_TRAP       0010000                         /* mem mgt trap */
#define MMR0_RO         0020000                         /* read only error */
#define MMR0_PL         0040000                         /* page lnt error */
#define MMR0_NR         0100000                         /* no access error */
#define MMR0_FREEZE     0160000                         /* if set, no update */
#define MMR0_WR         0171401                         /* writeable bits */

/* MMR3 */

#define MMR3_UDS        001                             /* user dspace enbl */
#define MMR3_SDS        002                             /* super dspace enbl */
#define MMR3_KDS        004                             /* krnl dspace enbl */
#define MMR3_CSM        010                             /* CSM enable */
#define MMR3_M22E       020                             /* 22b mem mgt enbl */
#define MMR3_BME        040                             /* DMA bus map enbl */

/* PAR */

#define PAR_18B         0007777                         /* 18b addressing */
#define PAR_22B         0177777                         /* 22b addressing */

/* PDR */

#define PDR_ACF         0000007                         /* access control */
#define PDR_ACS         0000006                         /* 2b access control */
#define PDR_ED          0000010                         /* expansion dir */
#define PDR_W           0000100                         /* written flag */
#define PDR_A           0000200                         /* access flag */
#define PDR_PLF         0077400                         /* page lnt field */
#define PDR_NOC         0100000                         /* don't cache */

#define PDR_PRD         0000003                         /* page readable if 2 */

/* Virtual address */

#define VA_DF           0017777                         /* displacement */
#define VA_BN           0017700                         /* block number */
#define VA_V_APF        13                              /* offset to APF */
#define VA_V_DS         16                              /* offset to space */
#define VA_V_MODE       17                              /* offset to mode */
#define VA_DS           (1u << VA_V_DS)                 /* data space flag */

/* Unibus map (if present) */

#define UBM_LNT_LW      32                              /* size in LW */
#define UBM_V_PN        13                              /* page number */
#define UBM_M_PN        037
#define UBM_V_OFF       0                               /* offset */
#define UBM_M_OFF       017777
#define UBM_PAGSIZE     (UBM_M_OFF + 1)                 /* page size */
#define UBM_GETPN(x)    (((x) >> UBM_V_PN) & UBM_M_PN)
#define UBM_GETOFF(x)   ((x) & UBM_M_OFF)

/* CPUERR */

#define CPUE_RED        0004                            /* red stack */
#define CPUE_YEL        0010                            /* yellow stack */
#define CPUE_TMO        0020                            /* IO page nxm */
#define CPUE_NXM        0040                            /* memory nxm */
#define CPUE_ODD        0100                            /* odd address */
#define CPUE_HALT       0200                            /* HALT not kernel */
#define CPUE_IMP        0374                            /* implemented bits */

/* Floating point accumulators */

typedef struct {
    uint32              l;                              /* low 32b */
    uint32              h;                              /* high 32b */
    } fpac_t;

/* Device CSRs */

#define CSR_V_GO        0                               /* go */
#define CSR_V_IE        6                               /* interrupt enable */
#define CSR_V_DONE      7                               /* done */
#define CSR_V_BUSY      11                              /* busy */
#define CSR_V_ERR       15                              /* error */
#define CSR_GO          (1u << CSR_V_GO)
#define CSR_IE          (1u << CSR_V_IE)
#define CSR_DONE        (1u << CSR_V_DONE)
#define CSR_BUSY        (1u << CSR_V_BUSY)
#define CSR_ERR         (1u << CSR_V_ERR)

/* Trap masks, descending priority order, following J-11
   An interrupt summary bit is kept with traps, to minimize overhead
*/

#define TRAP_V_RED      0                               /* red stk abort  4 */
#define TRAP_V_ODD      1                               /* odd address    4 */
#define TRAP_V_MME      2                               /* mem mgt      250 */
#define TRAP_V_NXM      3                               /* nx memory      4 */
#define TRAP_V_PAR      4                               /* parity err   114 */
#define TRAP_V_PRV      5                               /* priv inst      4 */
#define TRAP_V_ILL      6                               /* illegal inst  10 */
#define TRAP_V_BPT      7                               /* BPT           14 */
#define TRAP_V_IOT      8                               /* IOT           20 */
#define TRAP_V_EMT      9                               /* EMT           30 */
#define TRAP_V_TRAP     10                              /* TRAP          34 */
#define TRAP_V_TRC      11                              /* T bit         14 */
#define TRAP_V_YEL      12                              /* stack          4 */
#define TRAP_V_PWRFL    13                              /* power fail    24 */
#define TRAP_V_FPE      14                              /* fpe          244 */
#define TRAP_V_MAX      15                              /* intr = max trp # */
#define TRAP_RED        (1u << TRAP_V_RED)
#define TRAP_ODD        (1u << TRAP_V_ODD)
#define TRAP_MME        (1u << TRAP_V_MME)
#define TRAP_NXM        (1u << TRAP_V_NXM)
#define TRAP_PAR        (1u << TRAP_V_PAR)
#define TRAP_PRV        (1u << TRAP_V_PRV)
#define TRAP_ILL        (1u << TRAP_V_ILL)
#define TRAP_BPT        (1u << TRAP_V_BPT)
#define TRAP_IOT        (1u << TRAP_V_IOT)
#define TRAP_EMT        (1u << TRAP_V_EMT)
#define TRAP_TRAP       (1u << TRAP_V_TRAP)
#define TRAP_TRC        (1u << TRAP_V_TRC)
#define TRAP_YEL        (1u << TRAP_V_YEL)
#define TRAP_PWRFL      (1u << TRAP_V_PWRFL)
#define TRAP_FPE        (1u << TRAP_V_FPE)
#define TRAP_INT        (1u << TRAP_V_MAX)
#define TRAP_ALL        ((1u << TRAP_V_MAX) - 1)        /* all traps */

#define VEC_RED         0004                            /* trap vectors */
#define VEC_ODD         0004
#define VEC_MME         0250
#define VEC_NXM         0004
#define VEC_PAR         0114
#define VEC_PRV         0004
#define VEC_ILL         0010
#define VEC_BPT         0014
#define VEC_IOT         0020
#define VEC_EMT         0030
#define VEC_TRAP        0034
#define VEC_TRC         0014
#define VEC_YEL         0004
#define VEC_PWRFL       0024
#define VEC_FPE         0244

/* Simulator stop codes; codes 1:TRAP_V_MAX correspond to traps 0:TRAPMAX-1 */

#define STOP_HALT       (TRAP_V_MAX + 1)                /* HALT instruction */
#define STOP_IBKPT      (TRAP_V_MAX + 2)                /* instruction bkpt */
#define STOP_WAIT       (TRAP_V_MAX + 3)                /* wait, no events */
#define STOP_VECABORT   (TRAP_V_MAX + 4)                /* abort vector read */
#define STOP_SPABORT    (TRAP_V_MAX + 5)                /* abort trap push */
#define STOP_RQ         (TRAP_V_MAX + 6)                /* RQDX3 panic */
#define STOP_SANITY     (TRAP_V_MAX + 7)                /* sanity timer exp */
#define STOP_DTOFF      (TRAP_V_MAX + 8)                /* DECtape off reel */
#define IORETURN(f,v)   ((f)? (v): SCPE_OK)             /* cond error return */

/* Timers */

#define TMR_CLK         0                               /* line clock */
#define TMR_PCLK        1                               /* KW11P */

/* IO parameters */

#define DZ_MUXES        4                               /* max # of DZ muxes */
#define DZ_LINES        8                               /* lines per DZ mux */
#define VH_MUXES        4                               /* max # of VH muxes */
#define DLX_LINES       16                              /* max # of KL11/DL11's */
#define DCX_LINES       16                              /* max # of DC11's */
#define DUP_LINES       8                               /* max # of DUP11/DPV11's */
#define MT_MAXFR        (1 << 16)                       /* magtape max rec */
#define DIB_MAX         100                             /* max DIBs */

#define DEV_V_UBUS      (DEV_V_UF + 0)                  /* Unibus */
#define DEV_V_QBUS      (DEV_V_UF + 1)                  /* Qbus */
#define DEV_V_Q18       (DEV_V_UF + 2)                  /* Qbus with <= 256KB */
#define DEV_V_MBUS      (DEV_V_UF + 3)                  /* Massbus */
#define DEV_V_FFUF      (DEV_V_UF + 4)                  /* first free flag */
#define DEV_UBUS        (1u << DEV_V_UBUS)
#define DEV_QBUS        (1u << DEV_V_QBUS)
#define DEV_Q18         (1u << DEV_V_Q18)
#define DEV_MBUS        (1u << DEV_V_MBUS)

#define DEV_RDX         8                               /* default device radix */

/* Device information block */

#define VEC_DEVMAX      4                               /* max device vec */

struct pdp_dib {
    uint32              ba;                             /* base addr */
    uint32              lnt;                            /* length */
    t_stat              (*rd)(int32 *dat, int32 ad, int32 md);
    t_stat              (*wr)(int32 dat, int32 ad, int32 md);
    int32               vnum;                           /* vectors: number */
    int32               vloc;                           /* locator */
    int32               vec;                            /* value */
    int32               (*ack[VEC_DEVMAX])(void);       /* ack routines */
    uint32              ulnt;                           /* IO length per-device */
                                                        /* Only need to be populated */
                                                        /* when numunits != num devices */
    };

typedef struct pdp_dib DIB;

/* Unibus I/O page layout - see pdp11_io_lib.c for address layout details
   Massbus devices (RP, TU) do not appear in the Unibus IO page */

#define IOBA_AUTO       (0)                             /* Assigned by Auto Configure */

/* Processor registers which have I/O page addresses
 */

#define IOBA_CTL        (IOPAGEBASE + 017520)           /* board ctrl */
#define IOLN_CTL        010
#define IOBA_UBM        (IOPAGEBASE + 010200)           /* Unibus map */
#define IOLN_UBM        (UBM_LNT_LW * sizeof (int32))
#define IOBA_MMR3       (IOPAGEBASE + 012516)           /* MMR3 */
#define IOLN_MMR3       002
#define IOBA_TTI        (IOPAGEBASE + 017560)           /* DL11 rcv */
#define IOLN_TTI        004
#define IOBA_TTO        (IOPAGEBASE + 017564)           /* DL11 xmt */
#define IOLN_TTO        004
#define IOBA_SR         (IOPAGEBASE + 017570)           /* SR */
#define IOLN_SR         002
#define IOBA_MMR012     (IOPAGEBASE + 017572)           /* MMR0-2 */
#define IOLN_MMR012     006
#define IOBA_GPR        (IOPAGEBASE + 017700)           /* GPR's */
#define IOLN_GPR        010
#define IOBA_UCTL       (IOPAGEBASE + 017730)           /* UBA ctrl */
#define IOLN_UCTL       010
#define IOBA_CPU        (IOPAGEBASE + 017740)           /* CPU reg */
#define IOLN_CPU        036
#define IOBA_PSW        (IOPAGEBASE + 017776)           /* PSW */
#define IOLN_PSW        002
#define IOBA_UIPDR      (IOPAGEBASE + 017600)           /* user APR's */
#define IOLN_UIPDR      020
#define IOBA_UDPDR      (IOPAGEBASE + 017620)
#define IOLN_UDPDR      020
#define IOBA_UIPAR      (IOPAGEBASE + 017640)
#define IOLN_UIPAR      020
#define IOBA_UDPAR      (IOPAGEBASE + 017660)
#define IOLN_UDPAR      020
#define IOBA_SUP        (IOPAGEBASE + 012200)           /* supervisor APR's */
#define IOLN_SUP        0100
#define IOBA_KIPDR      (IOPAGEBASE + 012300)           /* kernel APR's */
#define IOLN_KIPDR      020
#define IOBA_KDPDR      (IOPAGEBASE + 012320)
#define IOLN_KDPDR      020
#define IOBA_KIPAR      (IOPAGEBASE + 012340)
#define IOLN_KIPAR      020
#define IOBA_KDPAR      (IOPAGEBASE + 012360)
#define IOLN_KDPAR      020

/* Interrupt assignments; within each level, priority is right to left
   PIRQn has the highest priority with a level and is always bit <0>
   On level 6, the clock is second highest priority */

#define IPL_HLVL        8                               /* # int levels */
#define IPL_HMIN        4                               /* lowest IO int level */

#define INT_V_PIR7      0                               /* BR7 */

#define INT_V_PIR6      0                               /* BR6 */
#define INT_V_CLK       1
#define INT_V_PCLK      2
#define INT_V_DTA       3
#define INT_V_TA        4

#define INT_V_PIR5      0                               /* BR5 */
#define INT_V_RK        1
#define INT_V_RL        2
#define INT_V_RX        3
#define INT_V_TM        4
#define INT_V_RP        5
#define INT_V_TS        6
#define INT_V_HK        7
#define INT_V_RQ        8
#define INT_V_DZRX      9
#define INT_V_DZTX      10
#define INT_V_TQ        11
#define INT_V_RY        12
#define INT_V_XQ        13
#define INT_V_XU        14
#define INT_V_TU        15
#define INT_V_RF        16
#define INT_V_RC        17
#define INT_V_RS        18
#define INT_V_DMCRX     19
#define INT_V_DMCTX     20
#define INT_V_DUPRX     21
#define INT_V_DUPTX     22

#define INT_V_PIR4      0                               /* BR4 */
#define INT_V_TTI       1
#define INT_V_TTO       2
#define INT_V_PTR       3
#define INT_V_PTP       4
#define INT_V_LPT       5
#define INT_V_VHRX      6
#define INT_V_VHTX      7  
#define INT_V_CR        8
#define INT_V_DLI       9
#define INT_V_DLO       10
#define INT_V_DCI       11
#define INT_V_DCO       12

#define INT_V_PIR3      0                               /* BR3 */
#define INT_V_PIR2      0                               /* BR2 */
#define INT_V_PIR1      0                               /* BR1 */

#define INT_PIR7        (1u << INT_V_PIR7)
#define INT_PIR6        (1u << INT_V_PIR6)
#define INT_CLK         (1u << INT_V_CLK)
#define INT_PCLK        (1u << INT_V_PCLK)
#define INT_DTA         (1u << INT_V_DTA)
#define INT_TA          (1u << INT_V_TA)
#define INT_PIR5        (1u << INT_V_PIR5)
#define INT_RK          (1u << INT_V_RK)
#define INT_RL          (1u << INT_V_RL)
#define INT_RX          (1u << INT_V_RX)
#define INT_TM          (1u << INT_V_TM)
#define INT_RP          (1u << INT_V_RP)
#define INT_TS          (1u << INT_V_TS)
#define INT_HK          (1u << INT_V_HK)
#define INT_RQ          (1u << INT_V_RQ)
#define INT_DZRX        (1u << INT_V_DZRX)
#define INT_DZTX        (1u << INT_V_DZTX)
#define INT_TQ          (1u << INT_V_TQ)
#define INT_RY          (1u << INT_V_RY)
#define INT_XQ          (1u << INT_V_XQ)
#define INT_XU          (1u << INT_V_XU)
#define INT_TU          (1u << INT_V_TU)
#define INT_RF          (1u << INT_V_RF)
#define INT_RC          (1u << INT_V_RC)
#define INT_RS          (1u << INT_V_RS)
#define INT_DMCRX       (1u << INT_V_DMCRX)
#define INT_DMCTX       (1u << INT_V_DMCTX)
#define INT_DUPRX       (1u << INT_V_DUPRX)
#define INT_DUPTX       (1u << INT_V_DUPTX)
#define INT_PIR4        (1u << INT_V_PIR4)
#define INT_TTI         (1u << INT_V_TTI)
#define INT_TTO         (1u << INT_V_TTO)
#define INT_PTR         (1u << INT_V_PTR)
#define INT_PTP         (1u << INT_V_PTP)
#define INT_LPT         (1u << INT_V_LPT)
#define INT_VHRX        (1u << INT_V_VHRX)
#define INT_VHTX        (1u << INT_V_VHTX)
#define INT_CR          (1u << INT_V_CR)
#define INT_DLI         (1u << INT_V_DLI)
#define INT_DLO         (1u << INT_V_DLO)
#define INT_DCI         (1u << INT_V_DCI)
#define INT_DCO         (1u << INT_V_DCO)
#define INT_PIR3        (1u << INT_V_PIR3)
#define INT_PIR2        (1u << INT_V_PIR2)
#define INT_PIR1        (1u << INT_V_PIR1)

#define INT_INTERNAL7   (INT_PIR7)
#define INT_INTERNAL6   (INT_PIR6 | INT_CLK)
#define INT_INTERNAL5   (INT_PIR5)
#define INT_INTERNAL4   (INT_PIR4)
#define INT_INTERNAL3   (INT_PIR3)
#define INT_INTERNAL2   (INT_PIR2)
#define INT_INTERNAL1   (INT_PIR1)

#define IPL_CLK         6                               /* int pri levels */
#define IPL_PCLK        6
#define IPL_DTA         6
#define IPL_TA          6
#define IPL_RK          5
#define IPL_RL          5
#define IPL_RX          5
#define IPL_TM          5
#define IPL_RP          5
#define IPL_TS          5
#define IPL_HK          5
#define IPL_RQ          5
#define IPL_DZRX        5
#define IPL_DZTX        5
#define IPL_TQ          5
#define IPL_RY          5
#define IPL_XQ          5
#define IPL_XU          5
#define IPL_TU          5
#define IPL_RF          5
#define IPL_RC          5
#define IPL_RS          5
#define IPL_DMCRX       5
#define IPL_DMCTX       5
#define IPL_DUPRX       5
#define IPL_DUPTX       5
#define IPL_PTR         4
#define IPL_PTP         4
#define IPL_TTI         4
#define IPL_TTO         4
#define IPL_LPT         4
#define IPL_VHRX        4
#define IPL_VHTX        4
#define IPL_CR          4
#define IPL_DLI         4
#define IPL_DLO         4
#define IPL_DCI         4
#define IPL_DCO         4

#define IPL_PIR7        7
#define IPL_PIR6        6
#define IPL_PIR5        5
#define IPL_PIR4        4
#define IPL_PIR3        3
#define IPL_PIR2        2
#define IPL_PIR1        1

/* Device vectors */

#define VEC_AUTO        (0)                             /* Assigned by Auto Configure */
#define VEC_FLOAT       (0)                             /* Assigned by Auto Configure */

#define VEC_Q           0000                            /* vector base */

/* Processor specific internal fixed vectors */
#define VEC_PIRQ        0240
#define VEC_TTI         0060
#define VEC_TTO         0064

/* Interrupt macros */

#define IVCL(dv)        ((IPL_##dv * 32) + INT_V_##dv)
#define IREQ(dv)        int_req[IPL_##dv]
#define SET_INT(dv)     int_req[IPL_##dv] = int_req[IPL_##dv] | (INT_##dv)
#define CLR_INT(dv)     int_req[IPL_##dv] = int_req[IPL_##dv] & ~(INT_##dv)

/* Massbus definitions */

#define MBA_NUM         3                               /* number of MBA's */
#define MBA_RP          0                               /* MBA for RP */
#define MBA_TU          1                               /* MBA for TU */
#define MBA_RS          2                               /* MBA for RS */
#define MBA_RMASK       037                             /* max 32 reg */
#define MBE_NXD         1                               /* nx drive */
#define MBE_NXR         2                               /* nx reg */
#define MBE_GOE         3                               /* err on GO */

/* CPU and FPU macros */

#define update_MM       ((MMR0 & MMR0_FREEZE) == 0)
#define setTRAP(name)   trap_req = trap_req | (name)
#define setCPUERR(name) CPUERR = CPUERR | (name)
#define ABORT(val)      longjmp (save_env, (val))
#define SP R[6]
#define PC R[7]

/* Function prototypes */

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf);
int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf);
int32 Map_WriteB (uint32 ba, int32 bc, uint8 *buf);
int32 Map_WriteW (uint32 ba, int32 bc, uint16 *buf);

int32 mba_rdbufW (uint32 mbus, int32 bc, uint16 *buf);
int32 mba_wrbufW (uint32 mbus, int32 bc, uint16 *buf);
int32 mba_chbufW (uint32 mbus, int32 bc, uint16 *buf);
int32 mba_get_bc (uint32 mbus);
int32 mba_get_csr (uint32 mbus);
void mba_upd_ata (uint32 mbus, uint32 val);
void mba_set_exc (uint32 mbus);
void mba_set_don (uint32 mbus);
void mba_set_enbdis (uint32 mb, t_bool dis);
t_stat mba_show_num (FILE *st, UNIT *uptr, int32 val, void *desc);

#include "pdp11_io_lib.h"

#endif
