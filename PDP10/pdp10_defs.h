/* pdp10_defs.h: PDP-10 simulator definitions

   Copyright (c) 1993-2010, Robert M Supnik

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

   22-May-10    RMS     Added check for 64b addresses
   01-Feb-07    RMS     Added CD support
   29-Oct-06    RMS     Added clock coscheduling function
   29-Dec-03    RMS     Added Q18 definition for PDP11 compatibility
   19-May-03    RMS     Revised for new conditional compilation scheme
   09-Jan-03    RMS     Added DEUNA/DELUA support
   29-Sep-02    RMS     Added variable vector, RX211 support
   22-Apr-02    RMS     Removed magtape record length error
   20-Jan-02    RMS     Added multiboard DZ11 support
   23-Oct-01    RMS     New IO page address constants
   19-Oct-01    RMS     Added DZ11 definitions
   07-Sep-01    RMS     Revised for PDP-11 multi-level interrupts
   31-Aug-01    RMS     Changed int64 to t_int64 for Windoze
   29-Aug-01    RMS     Corrected models and dates (found by Lars Brinkhoff)
   01-Jun-01    RMS     Updated DZ11 vector definitions
   19-May-01    RMS     Added workaround for TOPS-20 V4.1 boot bug
*/

#ifndef PDP10_DEFS_H_
#define PDP10_DEFS_H_  0

#ifndef VM_PDP10
#define VM_PDP10        0
#endif

#include "sim_defs.h"                                   /* simulator defns */

#if defined(USE_ADDR64)
#error "PDP-10 does not support 64b addresses!"
#endif

/* Digital Equipment Corporation's 36b family had six implementations:

   name         mips    comments

   PDP-6        0.25    Original 36b implementation, 1964
   KA10         0.38    First PDP-10, flip chips, 1967
   KI10         0.72    First paging system, flip chip + MSI, 1972
   KL10         1.8     First ECL system, ECL 10K, 1975
   KL10B        1.8     Expanded addressing, ECL 10K, 1978
   KS10         0.3     Last 36b system, 2901 based, 1979

   In addition, it ran four major (incompatible) operating systems:

   name         company comments

   TOPS-10      DEC     Original timesharing system
   ITS          MIT     "Incompatible Timesharing System"
   TENEX        BBN     ARPA-sponsored, became
   TOPS-20      DEC     Commercial version of TENEX

   All of the implementations differ from one another, in instruction set,
   I/O structure, and memory management.  Further, each of the operating
   systems customized the microcode of the paging systems (KI10, KL10, KS10)
   for additional instructions and specialized memory management.  As a
   result, there is no "reference implementation" for the 36b family that
   will run all programs and all operating systems.  The conditionalization
   and generality needed to support the full matrix of models and operating
   systems, and to support 36b hardware on 32b data types, is beyond the
   scope of this project.

   Instead, this simulator emulates one model -- the KS10.  It has the best
   documentation and allows reuse of some of the Unibus peripheral emulators
   written for the PDP-11 simulator.  Further, the simulator requires that
   the underlying compiler support 64b integer data types, allowing 36b data
   to be maintained in a single data item.  Lastly, the simulator implements
   the maximum memory size, so that NXM's never happen.
*/

/* Data types */

typedef int32           a10;                            /* PDP-10 addr (30b) */
typedef t_int64         d10;                            /* PDP-10 data (36b) */

/* Abort codes, used to sort out longjmp's back to the main loop
   Codes > 0 are simulator stop codes
   Codes < 0 are internal aborts
   Code  = 0 stops execution for an interrupt check
*/

#define STOP_HALT       1                               /* halted */
#define STOP_IBKPT      2                               /* breakpoint */
#define STOP_ILLEG      3                               /* illegal instr */
#define STOP_ILLINT     4                               /* illegal intr inst */
#define STOP_PAGINT     5                               /* page fail in intr */
#define STOP_ZERINT     6                               /* zero vec in intr */
#define STOP_NXMPHY     7                               /* nxm on phys ref */
#define STOP_IND        8                               /* indirection loop */
#define STOP_XCT        9                               /* XCT loop */
#define STOP_ILLIOC     10                              /* invalid UBA num */
#define STOP_ASTOP      11                              /* address stop */
#define STOP_CONSOLE    12                              /* FE halt */
#define STOP_IOALIGN    13                              /* DMA word access to odd address */
#define STOP_UNKNOWN    14                              /* unknown stop  */
#define PAGE_FAIL       -1                              /* page fail */
#define INTERRUPT       -2                              /* interrupt */
#define ABORT(x)        longjmp (save_env, (x))         /* abort */
#define IORETURN(f,v)   ((f)? (v): SCPE_OK)             /* cond error return */

/* Return codes from eXTEND */

#define XT_MUUO         0                               /* invalid operation */
#define XT_SKIP         1                               /* skip return */
#define XT_NOSK         2                               /* no skip return */

/* Operating system flags, kept in cpu_unit.flags */

#define UNIT_V_ITS      (UNIT_V_UF)                     /* ITS */
#define UNIT_V_T20      (UNIT_V_UF + 1)                 /* TOPS-20 */
#define UNIT_V_KLAD     (UNIT_V_UF + 2)                 /* diagnostics */
#define UNIT_ITS        (1 << UNIT_V_ITS)
#define UNIT_T20        (1 << UNIT_V_T20)
#define UNIT_KLAD       (1 << UNIT_V_KLAD)
#define Q_T10           ((cpu_unit.flags & (UNIT_ITS|UNIT_T20|UNIT_KLAD)) == 0)
#define Q_ITS           (cpu_unit.flags & UNIT_ITS)
#define Q_T20           (cpu_unit.flags & UNIT_T20)
#define Q_KLAD          (cpu_unit.flags & UNIT_KLAD)
#define Q_IDLE          (sim_idle_enab)

/* Architectural constants */

#define PASIZE          20                              /* phys addr width */
#define MAXMEMSIZE      (1 << PASIZE)                   /* maximum memory */
#define PAMASK          ((1 << PASIZE) - 1)
#define MEMSIZE         MAXMEMSIZE                      /* fixed, KISS */
#define MEM_ADDR_NXM(x) ((x) >= MEMSIZE)
#define VASIZE          18                              /* virtual addr width */
#define AMASK           ((1 << VASIZE) - 1)             /* virtual addr mask */
#define LMASK           INT64_C(0777777000000)          /* left mask */
#define LSIGN           INT64_C(0400000000000)          /* left sign */
#define RMASK           INT64_C(0000000777777)          /* right mask */
#define RSIGN           INT64_C(0000000400000)          /* right sign */
#define DMASK           INT64_C(0777777777777)          /* data mask */
#define SIGN            INT64_C(0400000000000)          /* sign */
#define MMASK           INT64_C(0377777777777)          /* magnitude mask */
#define ONES            INT64_C(0777777777777)
#define MAXPOS          INT64_C(0377777777777)
#define MAXNEG          INT64_C(0400000000000)

/* Instruction format */

#define INST_V_OP       27                              /* opcode */
#define INST_M_OP       0777
#define INST_V_DEV      26
#define INST_M_DEV      0177                            /* device */
#define INST_V_AC       23                              /* AC */
#define INST_M_AC       017
#define INST_V_IND      22                              /* indirect */
#define INST_IND        (1 << INST_V_IND)
#define INST_V_XR       18                              /* index */
#define INST_M_XR       017
#define OP_JRST         0254                            /* JRST */
#define AC_XPCW         07                              /* XPCW */
#define OP_JSR          0264                            /* JSR */
#define GET_OP(x)       ((int32) (((x) >> INST_V_OP) & INST_M_OP))
#define GET_DEV(x)      ((int32) (((x) >> INST_V_DEV) & INST_M_DEV))
#define GET_AC(x)       ((int32) (((x) >> INST_V_AC) & INST_M_AC))
#define TST_IND(x)      ((x) & INST_IND)
#define GET_XR(x)       ((int32) (((x) >> INST_V_XR) & INST_M_XR))
#define GET_ADDR(x)     ((a10) ((x) & AMASK))

/* Byte pointer format */

#define BP_V_P          30                              /* position */
#define BP_M_P          INT64_C(077)
#define BP_P            INT64_C(0770000000000)
#define BP_V_S          24                              /* size */
#define BP_M_S          INT64_C(077)
#define BP_S            INT64_C(0007700000000)
#define GET_P(x)        ((int32) (((x) >> BP_V_P) & BP_M_P))
#define GET_S(x)        ((int32) (((x) >> BP_V_S) & BP_M_S))
#define PUT_P(b,x)      (((b) & ~BP_P) | ((((t_int64) (x)) & BP_M_P) << BP_V_P))

/* Flags (stored in their own halfword) */

#define F_V_AOV         17                              /* arithmetic ovflo */
#define F_V_C0          16                              /* carry 0 */
#define F_V_C1          15                              /* carry 1 */
#define F_V_FOV         14                              /* floating ovflo */
#define F_V_FPD         13                              /* first part done */
#define F_V_USR         12                              /* user mode */
#define F_V_UIO         11                              /* user I/O mode */
#define F_V_PUB         10                              /* public mode */
#define F_V_AFI         9                               /* addr fail inhibit */
#define F_V_T2          8                               /* trap 2 */
#define F_V_T1          7                               /* trap 1 */
#define F_V_FXU         6                               /* floating exp unflo */
#define F_V_DCK         5                               /* divide check */
#define F_AOV           (1 << F_V_AOV)
#define F_C0            (1 << F_V_C0)
#define F_C1            (1 << F_V_C1)
#define F_FOV           (1 << F_V_FOV)
#define F_FPD           (1 << F_V_FPD)
#define F_USR           (1 << F_V_USR)
#define F_UIO           (1 << F_V_UIO)
#define F_PUB           (1 << F_V_PUB)
#define F_AFI           (1 << F_V_AFI)
#define F_T2            (1 << F_V_T2)
#define F_T1            (1 << F_V_T1)
#define F_TR            (F_T1 | F_T2)
#define F_FXU           (1 << F_V_FXU)
#define F_DCK           (1 << F_V_DCK)
#define F_1PR           (F_AFI)                         /* ITS: 1-proceed */
#define F_MASK          0777740                         /* all flags */
#define SETF(x)         flags = flags | (x)
#define CLRF(x)         flags = flags & ~(x)
#define TSTF(x)         (flags & (x))
#define GET_TRAPS(x)    (((x) & (F_T2 | F_T1)) >> F_V_T1)

/* Priority interrupt system */

#define PI_CPRQ         020000                          /* drop prog req */
#define PI_INIT         010000                          /* clear pi system */
#define PI_SPRQ         004000                          /* set prog req */
#define PI_SENB         002000                          /* set enables */
#define PI_CENB         001000                          /* clear enables */
#define PI_CON          000400                          /* turn off pi system */
#define PI_SON          000200                          /* turn on pi system */
#define PI_M_LVL        000177                          /* level mask */
#define PI_V_PRQ        18                              /* in CONI */
#define PI_V_ACT        8
#define PI_V_ON         7
#define PI_V_ENB        0

/* Arithmetic processor flags */

#define APR_SENB        0100000                         /* set enable */
#define APR_CENB        0040000                         /* clear enable */
#define APR_CFLG        0020000                         /* clear flag */
#define APR_SFLG        0010000                         /* set flag */
#define APR_IRQ         0000010                         /* int request */
#define APR_M_LVL       0000007                         /* pi level */
#define APR_V_FLG       4                               /* system flags */
#define APR_M_FLG       0377
#define APRF_ITC        (002000 >> APR_V_FLG)           /* int console flag */
#define APRF_NXM        (000400 >> APR_V_FLG)           /* nxm flag */
#define APRF_TIM        (000040 >> APR_V_FLG)           /* timer request */
#define APRF_CON        (000020 >> APR_V_FLG)           /* console int */
#define APR_GETF(x)     (((x) >> APR_V_FLG) & APR_M_FLG)

/* Virtual address, DEC paging */

#define PAG_V_OFF       0                               /* offset - must be 0 */
#define PAG_N_OFF       9                               /* page offset width  */
#define PAG_SIZE        01000                           /* page offset size */
#define PAG_M_OFF       0777                            /* mask for offset */
#define PAG_V_PN        PAG_N_OFF                       /* page number */
#define PAG_N_PPN       (PASIZE - PAG_N_OFF)            /* phys pageno width */
#define PAG_M_PPN       03777                           /* phys pageno mask */
#define PAG_PPN         03777000
#define PAG_N_VPN       (VASIZE - PAG_N_OFF)            /* virt pageno width */
#define PAG_M_VPN       0777                            /* virt pageno mask */
#define PAG_VPN         0777000
#define PAG_GETOFF(x)   ((x) & PAG_M_OFF)
#define PAG_GETVPN(x)   (((x) >> PAG_V_PN) & PAG_M_VPN)
#define PAG_XPTEPA(p,x) (((p) + PAG_GETOFF (x)) & PAMASK)
#define PAG_PTEPA(p,x)  (((((int32) (p)) & PTE_PPMASK) << PAG_V_PN) + PAG_GETOFF (x))

/* Page table entry, TOPS-10 paging */

#define PTE_T10_A       0400000                         /* T10: access */
#define PTE_T10_P       0200000                         /* T10: public */
#define PTE_T10_W       0100000                         /* T10: writeable */
#define PTE_T10_S       0040000                         /* T10: software */
#define PTE_T10_C       0020000                         /* T10: cacheable */
#define PTE_PPMASK      PAG_M_PPN

/* Page table entry, TOPS-20 paging */

#define PTE_T20_V_TYP   INT64_C(33)                     /* T20: pointer type */
#define PTE_T20_M_TYP   INT64_C(07)
#define  T20_NOA         0                              /* no access */
#define  T20_IMM         1                              /* immediate */
#define  T20_SHR         2                              /* shared */
#define  T20_IND         3                              /* indirect */
#define PTE_T20_W       INT64_C(0020000000000)          /* T20: writeable */
#define PTE_T20_C       INT64_C(0004000000000)          /* T20: cacheable */
#define PTE_T20_STM     INT64_C(0000077000000)          /* T20: storage medium */
#define PTE_T20_V_PMI   18                              /* page map index */
#define PTE_T20_M_PMI   0777
#define T20_GETTYP(x)   ((int32) (((x) >> PTE_T20_V_TYP) & PTE_T20_M_TYP))
#define T20_GETPMI(x)   ((int32) (((x) >> PTE_T20_V_PMI) & PTE_T20_M_PMI))

/* CST entry, TOPS-20 paging */

#define CST_AGE         INT64_C(0770000000000)          /* age field */
#define CST_M           INT64_C(0000000000001)          /* modified */

/* Page fail word, DEC paging */

#define PF_USER         INT64_C(0400000000000)          /* user mode */
#define PF_HARD         INT64_C(0200000000000)          /* nx I/O reg */
#define PF_NXM          INT64_C(0370000000000)          /* nx memory */
#define PF_T10_A        INT64_C(0100000000000)          /* T10: pte A bit */
#define PF_T10_W        INT64_C(0040000000000)          /* T10: pte W bit */
#define PF_T10_S        INT64_C(0020000000000)          /* T10: pte S bit */
#define PF_T20_DN       INT64_C(0100000000000)          /* T20: eval done */
#define PF_T20_M        INT64_C(0040000000000)          /* T20: modified */
#define PF_T20_W        INT64_C(0020000000000)          /* T20: writeable */
#define PF_WRITE        INT64_C(0010000000000)          /* write reference */
#define PF_PUB          INT64_C(0004000000000)          /* pte public bit */
#define PF_C            INT64_C(0002000000000)          /* pte C bit */
#define PF_VIRT         INT64_C(0001000000000)          /* pfl: virt ref */
#define PF_NXMP         INT64_C(0001000000000)          /* nxm: phys ref */
#define PF_IO           INT64_C(0000200000000)          /* I/O reference */
#define PF_BYTE         INT64_C(0000020000000)          /* I/O byte ref */

/* Virtual address, ITS paging */

#define ITS_V_OFF       0                               /* offset - must be 0 */
#define ITS_N_OFF       10                              /* page offset width */
#define ITS_SIZE        02000                           /* page offset size */
#define ITS_M_OFF       01777                           /* mask for offset */
#define ITS_V_PN        ITS_N_OFF                       /* page number */
#define ITS_N_PPN       (PASIZE- ITS_N_OFF)             /* phys pageno width */
#define ITS_M_PPN       01777                           /* phys pageno mask */
#define ITS_PPN         03776000
#define ITS_N_VPN       (VASIZE - ITS_N_OFF)            /* virt pageno width */
#define ITS_M_VPN       0377                            /* virt pageno mask */
#define ITS_VPN         0776000
#define ITS_GETVPN(x)   (((x) >> ITS_V_PN) & ITS_M_VPN)

/* Page table entry, ITS paging */

#define PTE_ITS_V_ACC   16                              /* access field */
#define PTE_ITS_M_ACC   03
#define  ITS_ACC_NO      0                              /* no access */
#define  ITS_ACC_RO      1                              /* read only */
#define  ITS_ACC_RWF     2                              /* read-write first */
#define  ITS_ACC_RW      3                              /* read write */
#define PTE_ITS_AGE     0020000                         /* age */
#define PTE_ITS_C       0010000                         /* cacheable */
#define PTE_ITS_PPMASK  ITS_M_PPN
#define ITS_GETACC(x)   (((x) >> PTE_ITS_V_ACC) & PTE_ITS_M_ACC)

/* Page fail word, ITS paging */

#define PF_ITS_WRITE    INT64_C(0010000000000)          /* write reference */
#define PF_ITS_V_ACC    28                              /* access from PTE */

/* Page table fill operations */

#define PTF_RD          0                               /* read check */
#define PTF_WR          1                               /* write check */
#define PTF_MAP         2                               /* map instruction */
#define PTF_CON         4                               /* console access */

/* User base register */

#define UBR_SETACB      INT64_C(0400000000000)          /* set AC blocks */
#define UBR_SETUBR      INT64_C(0100000000000)          /* set UBR */
#define UBR_V_CURAC     27                              /* current AC block */
#define UBR_V_PRVAC     24                              /* previous AC block */
#define UBR_M_AC        07
#define UBR_ACBMASK     INT64_C(0007700000000)
#define UBR_V_UBR       0                               /* user base register */
#define UBR_N_UBR       11
#define UBR_M_UBR       03777
#define UBR_UBRMASK     INT64_C(0000000003777)
#define UBR_GETCURAC(x) ((int32) (((x) >> UBR_V_CURAC) & UBR_M_AC))
#define UBR_GETPRVAC(x) ((int32) (((x) >> UBR_V_PRVAC) & UBR_M_AC))
#define UBR_GETUBR(x)   ((int32) (((x) >> UBR_V_UBR) & PAG_M_PPN))
#define UBRWORD         (ubr | UBR_SETACB | UBR_SETUBR)

/* Executive base register */

#define EBR_V_T20P      14                              /* TOPS20 paging */
#define EBR_T20P        (1u << EBR_V_T20P)
#define EBR_V_PGON      13                              /* enable paging */
#define EBR_PGON        (1u << EBR_V_PGON)
#define EBR_V_EBR       0                               /* exec base register */
#define EBR_N_EBR       11
#define EBR_M_EBR       03777
#define EBR_MASK        (EBR_T20P | EBR_PGON | (EBR_M_EBR << EBR_V_EBR))
#define EBR_GETEBR(x)   ((int32) (((x) >> EBR_V_EBR) & PAG_M_PPN))
#define PAGING          (ebr & EBR_PGON)
#define T20PAG          (ebr & EBR_T20P)

/* AC and mapping contexts

   There are only two real contexts for selecting the AC block and
   the memory map: current and previous.  However, PXCT allows the
   choice of current versus previous to be made selectively for
   various parts of an instruction.  The PXCT flags are kept in a
   dynamic CPU variable.
*/

#define EA_PXCT         010                             /* eff addr calc */
#define OPND_PXCT       004                             /* operand, bdst */
#define EABP_PXCT       002                             /* bp eff addr calc */
#define BSTK_PXCT       001                             /* stk, bp op, bsrc */
#define XSRC_PXCT       002                             /* extend source */
#define XDST_PXCT       001                             /* extend destination */
#define MM_CUR          000                             /* current context */
#define MM_EA           (pflgs & EA_PXCT)
#define MM_OPND         (pflgs & OPND_PXCT)
#define MM_EABP         (pflgs & EABP_PXCT)
#define MM_BSTK         (pflgs & BSTK_PXCT)

/* Accumulator access.  The AC blocks are kept in array acs[AC_NBLK * AC_NUM].
   Two pointers are provided to the bases of the current and previous blocks.
   Macro AC selects the current AC block; macro XR selects current or previous,
   depending on whether the selected bit in the "pxct in progress" flag is set.
*/

#define AC_NUM          16                              /* # AC's/block */
#define AC_NBLK         8                               /* # AC blocks */
#define AC(r)           (ac_cur[r])                     /* AC select current */
#define XR(r,prv)       ((prv)? ac_prv[r]: ac_cur[r])   /* AC select context */
#define ADDAC(x,i)      (((x) + (i)) & INST_M_AC)
#define P1              ADDAC (ac, 1)

/* User process table entries */

#define UPT_T10_UMAP    0000                            /* T10: user map */
#define UPT_T10_X340    0400                            /* T10: exec 340-377 */
#define UPT_TRBASE      0420                            /* trap base */
#define UPT_MUUO        0424                            /* MUUO block */
#define UPT_MUPC        0425                            /* caller's PC */
#define UPT_T10_CTX     0426                            /* T10: context */
#define UPT_T20_UEA     0426                            /* T20: address */
#define UPT_T20_CTX     0427                            /* T20: context */
#define UPT_ENPC        0430                            /* MUUO new PC, exec */
#define UPT_1PO         0432                            /* ITS 1-proc: old PC */
#define UPT_1PN         0433                            /* ITS 1-proc: new PC */
#define UPT_UNPC        0434                            /* MUUO new PC, user */
#define UPT_NPCT        1                               /* PC offset if trap */
#define UPT_T10_PAG     0500                            /* T10: page fail blk */
#define UPT_T20_PFL     0500                            /* T20: page fail wd */
#define UPT_T20_OFL     0501                            /* T20: flags */
#define UPT_T20_OPC     0502                            /* T20: old PC */
#define UPT_T20_NPC     0503                            /* T20: new PC */
#define UPT_T20_SCTN    0540                            /* T20: section 0 ptr */

/* Exec process table entries */

#define EPT_PIIT        0040                            /* PI interrupt table */
#define EPT_UBIT        0100                            /* Unibus intr table */
#define EPT_T10_X400    0200                            /* T10: exec 400-777 */
#define EPT_TRBASE      0420                            /* trap base */
#define EPT_ITS_PAG     0440                            /* ITS: page fail blk */
#define EPT_T20_SCTN    0540                            /* T20: section 0 ptr */
#define EPT_T10_X000    0600                            /* T10: exec 0 - 337 */

/* Microcode constants */

#define UC_INHCST       INT64_C(0400000000000)          /* inhibit CST update */
#define UC_UBABLT       INT64_C(0040000000000)          /* BLTBU and BLTUB */
#define UC_KIPAGE       INT64_C(0020000000000)          /* "KI" paging */
#define UC_KLPAGE       INT64_C(0010000000000)          /* "KL" paging */
#define UC_VERDEC       (0130 << 18)                    /* ucode version */
#define UC_VERITS       (262u << 18)
#define UC_SERDEC       4097                            /* serial number */
#define UC_SERITS       1729
#define UC_AIDDEC       (UC_INHCST | UC_UBABLT | UC_KIPAGE | UC_KLPAGE | \
                         UC_VERDEC)
#define UC_AIDITS       (UC_KIPAGE | UC_VERITS)

#define UC_HSBDEC       0376000                         /* DEC initial HSB */
#define UC_HSBITS       0000500                         /* ITS initial HSB */

/* Front end communications region */

#define FE_SWITCH       030                             /* halt switch */
#define FE_KEEPA        031                             /* keep alive */
#define FE_CTYIN        032                             /* console in */
#define FE_CTYOUT       033                             /* console out */
#define FE_KLININ       034                             /* KLINIK in */
#define FE_KLINOUT      035                             /* KLINIK out */
#define FE_RHBASE       036                             /* boot: RH11 addr */
#define FE_UNIT         037                             /* boot: unit num */
#define FE_MTFMT        040                             /* boot: magtape params */
#define FE_CVALID       0400                            /* char valid flag */

/* Halfword operations */

#define ADDL(x,y)       (((x) + ((y) << 18)) & LMASK)
#define ADDR(x,y)       (((x) + (y)) & RMASK)
#define INCL(x)         ADDL (x, 1)
#define INCR(x)         ADDR (x, 1)
#define AOB(x)          (INCL (x) | INCR(x))
#define SUBL(x,y)       (((x) - ((y) << 18)) & LMASK)
#define SUBR(x,y)       (((x) - (y)) & RMASK)
#define DECL(x)         SUBL (x, 1)
#define DECR(x)         SUBR (x, 1)
#define SOB(x)          (DECL (x) | DECR(x))
#define LLZ(x)          ((x) & LMASK)
#define RLZ(x)          (((x) << 18) & LMASK)
#define RRZ(x)          ((x) & RMASK)
#define LRZ(x)          (((x) >> 18) & RMASK)
#define LIT8(x)         (((x) & RSIGN)? \
                        (((x) & 0377)? (-(x) & 0377): 0400): ((x) & 0377))

/* Fullword operations */

#define INC(x)          (((x) + 1) & DMASK)
#define DEC(x)          (((x) - 1) & DMASK)
#define SWP(x)          ((((x) << 18) & LMASK) | (((x) >> 18) & RMASK))
#define XWD(x,y)        (((((d10) (x)) << 18) & LMASK) | (((d10) (y)) & RMASK))
#define SETS(x)         ((x) | SIGN)
#define CLRS(x)         ((x) & ~SIGN)
#define TSTS(x)         ((x) & SIGN)
#define NEG(x)          (-(x) & DMASK)
#define ABS(x)          (TSTS (x)? NEG(x): (x))
#define SXT(x)          (TSTS (x)? (x) | ~DMASK: (x))

/* Doubleword operations (on 2-word arrays) */

#define DMOVN(rs)       rs[1] = (-rs[1]) & MMASK; \
                        rs[0] = (~rs[0] + (rs[1] == 0)) & DMASK
#define MKDNEG(rs)      rs[1] = SETS (-rs[1]) & DMASK; \
                        rs[0] = (~rs[0] + (rs[1] == MAXNEG)) & DMASK
#define DCMPGE(a,b)     ((a[0] > b[0]) || ((a[0] == b[0]) && (a[1] >= b[1])))

/* Address operations */

#define ADDA(x,i)       (((x) + (i)) & AMASK)
#define INCA(x)         ADDA (x, 1)

/* Unibus adapter control/status register */

#define UBCS_TMO        0400000                         /* timeout */
#define UBCS_BMD        0200000                         /* bad mem data NI */
#define UBCS_PAR        0100000                         /* parity error NI */
#define UBCS_NXD        0040000                         /* nx device */
#define UBCS_HI         0004000                         /* irq on BR7 or BR6 */
#define UBCS_LO         0002000                         /* irq on BR5 or BR4 */
#define UBCS_PWR        0001000                         /* power low NI */
#define UBCS_DXF        0000200                         /* disable xfer NI*/
#define UBCS_INI        0000100                         /* Unibus init */
#define UBCS_RDZ        0030500                         /* read as zero */
#define UBCS_RDW        0000277                         /* read/write bits */
#define UBCS_V_LHI      3                               /* hi pri irq level */
#define UBCS_V_LLO      0                               /* lo pri irq level */
#define UBCS_M_PRI      07
#define UBCS_GET_HI(x)  (((x) >> UBCS_V_LHI) & UBCS_M_PRI)
#define UBCS_GET_LO(x)  (((x) >> UBCS_V_LLO) & UBCS_M_PRI)

/* Unibus adapter page map */

#define UBANUM          2                               /* # of Unibus adapters */
#define UMAP_ASIZE      6                               /* address size */
#define UMAP_MEMSIZE    (1 << UMAP_ASIZE)               /* length */
#define UMAP_AMASK      (UMAP_MEMSIZE - 1)
#define UMAP_V_RRV      30                              /* read reverse  */
#define UMAP_V_DSB      29                              /* 16b on NPR read */
#define UMAP_V_FST      28                              /* fast transfer */
#define UMAP_V_VLD      27                              /* valid flag  */
#define UMAP_RRV        (1 << UMAP_V_RRV)
#define UMAP_DSB        (1 << UMAP_V_DSB)
#define UMAP_FST        (1 << UMAP_V_FST)
#define UMAP_VLD        (1 << UMAP_V_VLD)
#define UMAP_V_FLWR     14                              /* flags as written */
#define UMAP_V_FLRD     27                              /* flags as stored */
#define UMAP_M_FL       017
#define UMAP_V_PNWR     0                               /* page num, write */
#define UMAP_V_PNRD     9                               /* page num, read */
#define UMAP_M_PN       03777
#define UMAP_MASK       ((UMAP_M_FL << UMAP_V_FLRD) | (UMAP_M_PN << UMAP_V_PNRD))
#define UMAP_POSFL(x)   (((x) & (UMAP_M_FL << UMAP_V_FLWR)) \
                     << (UMAP_V_FLRD - UMAP_V_FLWR))
#define UMAP_POSPN(x)   (((x) & (UMAP_M_PN << UMAP_V_PNWR)) \
                     << (UMAP_V_PNRD - UMAP_V_PNWR))

/* Unibus I/O constants */

#define READ            0                               /* PDP11 compatible */
/* #define READC        1                             *//* console read */
#define WRITE           2
/* #define WRITEC       3                             *//* console write */
#define WRITEB          4
#define IO_V_UBA        18                              /* UBA in I/O addr */
#define IO_N_UBA        16                              /* max num of UBA's */
#define IO_M_UBA        (IO_N_UBA - 1)
#define IO_UBA1         (1 << IO_V_UBA)
#define IO_UBA3         (3 << IO_V_UBA)
#define GET_IOUBA(x)    (((x) >> IO_V_UBA) & IO_M_UBA)

/* Device information block */

#define VEC_DEVMAX      8                               /* max device vec */

struct pdp_dib {
    uint32              ba;                             /* base addr */
    uint32              lnt;                            /* length */
    t_stat              (*rd)(int32 *dat, int32 ad, int32 md);
    t_stat              (*wr)(int32 dat, int32 ad, int32 md);
    int32               vnum;                           /* vectors: number */
    int32               vloc;                           /* locator */
    int32               vec;                            /* value */
    int32               (*ack[VEC_DEVMAX])(void);       /* ack routines */
    uint32              ulnt;                           /* IO length per unit */
    uint32              flags;                          /* Special flags */
#define DIB_M_REGSIZE   03                              /* Device register size */
#define DIB_REG16BIT     00
#define DIB_REG18BIT     01
};

typedef struct pdp_dib DIB;

/* I/O system parameters */

#define DZ_MUXES        4                               /* max # of muxes */
#define DZ_LINES        8                               /* lines per mux */
#define KMC_UNITS       1                               /* max # of KMCs */
#define INITIAL_KMCS    0                               /* Number initially enabled */
#define DUP_LINES       4                               /* max # of DUP11's */
#define DIB_MAX         100                             /* max DIBs */

#define DEV_V_UBUS      (DEV_V_UF + 0)                  /* Unibus */
#define DEV_V_QBUS      (DEV_V_UF + 1)                  /* Qbus */
#define DEV_V_Q18       (DEV_V_UF + 2)                  /* Qbus, mem <= 256KB */
#define DEV_UBUS        (1u << DEV_V_UBUS)
#define DEV_QBUS        (1u << DEV_V_QBUS)
#define DEV_Q18         (1u << DEV_V_Q18)

#define UNIBUS          TRUE                            /* 18b only */

#define DEV_RDX         8                               /* default device radix */

/* I/O page layout */

#define IOPAGEBASE      (IO_UBA3 + 0760000)             /* I/O page base */
#define IOBA_UBMAP      0763000

#define IOBA_UBMAP1     (IO_UBA1 + IOBA_UBMAP)          /* Unibus 1 map */
#define IOLN_UBMAP1     0100
#define IOBA_UBCS1      (IO_UBA1 + 0763100)             /* Unibus 1 c/s reg */
#define IOLN_UBCS1      001
#define IOBA_UBMNT1     (IO_UBA1 + 0763101)             /* Unibus 1 maint reg */
#define IOLN_UBMNT1     001
#define IOBA_RP         (IO_UBA1 + 0776700)             /* RH11/disk */
#define IOLN_RP         050

#define IOBA_TCU        (IO_UBA3 + 0760770)             /* TCU150 */
#define IOLN_TCU        006
#define IOBA_UBMAP3     (IO_UBA3 + IOBA_UBMAP)          /* Unibus 3 map */
#define IOLN_UBMAP3     0100
#define IOBA_UBCS3      (IO_UBA3 + 0763100)             /* Unibus 3 c/s reg */
#define IOLN_UBCS3      001
#define IOBA_UBMNT3     (IO_UBA3 + 0763101)             /* Unibus 3 maint reg */
#define IOLN_UBMNT3     001
#define IOBA_TU         (IO_UBA3 + 0772440)             /* RH11/tape */
#define IOLN_TU         034
#define IOBA_LP20       (IO_UBA3 + 0775400)             /* LP20 */
#define IOLN_LP20       020
#define IOBA_AUTO       0                               /* Set by Auto Configure */

/* Common Unibus CSR flags */

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

/* I/O system definitions, lifted from the PDP-11 simulator
   Interrupt assignments, priority is right to left

   <3:0> =      BR7
   <7:4> =      BR6
   <19:8> =     BR5
   <30:20> =    BR4
*/

#define INT_V_RP        6                               /* RH11/RP,RM drives */
#define INT_V_TU        7                               /* RH11/TM03/TU45 */
#define INT_V_KMCA      8                               /* KMC11 */
#define INT_V_KMCB      9
#define INT_V_DMCRX     10                              /* DMC11/DMR11 */
#define INT_V_DMCTX     11
#define INT_V_DZRX      16                              /* DZ11 */
#define INT_V_DZTX      17
#define INT_V_RY        18                              /* RX211 */
#define INT_V_PTR       24                              /* PC11 */
#define INT_V_PTP       25
#define INT_V_LP20      26                              /* LPT20 */
#define INT_V_CR        27                              /* CD20 (CD11) */
#define INT_V_DUPRX     28                              /* DUP11 */
#define INT_V_DUPTX     29

#define INT_RP          (1u << INT_V_RP)
#define INT_TU          (1u << INT_V_TU)
#define INT_KMCA        (1u << INT_V_KMCA)
#define INT_KMCB        (1u << INT_V_KMCB)
#define INT_DMCRX       (1u << INT_V_DMCRX)
#define INT_DMCTX       (1u << INT_V_DMCTX)
#define INT_XU          (1u << INT_V_XU)
#define INT_DZRX        (1u << INT_V_DZRX)
#define INT_DZTX        (1u << INT_V_DZTX)
#define INT_RY          (1u << INT_V_RY)
#define INT_PTR         (1u << INT_V_PTR)
#define INT_PTP         (1u << INT_V_PTP)
#define INT_LP20        (1u << INT_V_LP20)
#define INT_CR          (1u << INT_V_CR)
#define INT_DUPRX       (1u << INT_V_DUPRX)
#define INT_DUPTX       (1u << INT_V_DUPTX)

#define IPL_RP          6                               /* int levels */
#define IPL_TU          6
#define IPL_KMCA        5
#define IPL_KMCB        5
#define IPL_DMCRX       5
#define IPL_DMCTX       5
#define IPL_DZRX        5
#define IPL_DZTX        5
#define IPL_RY          5
#define IPL_DUPRX       5
#define IPL_DUPTX       5
#define IPL_PTR         4
#define IPL_PTP         4
#define IPL_LP20        4
#define IPL_CR          4

#define INT_UB1         INT_RP                          /* on Unibus 1 */
#define INT_UB3         (0xFFFFFFFFu & ~INT_UB1)        /* on Unibus 3 */

#define INT_IPL7        0x0000000F                      /* int level masks */
#define INT_IPL6        0x000000F0
#define INT_IPL5        0x000FFF00
#define INT_IPL4        0x7FF00000

#define VEC_Q           0000                            /* vector base */
#define VEC_TU          0224                            /* interrupt vectors */
#define VEC_RP          0254
#define VEC_LP20        0754
#define VEC_AUTO        0                               /* Set by Auto Configure */

#define IVCL(dv)        (INT_V_##dv)
#define IREQ(dv)        int_req
#define SET_INT(dv)     IREQ(dv) = IREQ(dv) | (INT_##dv)
#define CLR_INT(dv)     IREQ(dv) = IREQ(dv) & ~(INT_##dv)

/* Function prototypes */

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf);
int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf);
int32 Map_ReadW18 (uint32 ba, int32 bc, uint32 *buf);
int32 Map_WriteB (uint32 ba, int32 bc, uint8 *buf);
int32 Map_WriteW (uint32 ba, int32 bc, uint16 *buf);
int32 Map_WriteW18 (uint32 ba, int32 bc, uint32 *buf);
void uba_debug_dma_in (uint32 ba, a10 pa_start, a10 pa_end);
void uba_debug_dma_out (uint32 ba, a10 pa_start, a10 pa_end);
void uba_debug_dma_nxm (const char *msg, a10 pa10, uint32 ba, int32 bc);

t_stat set_addr (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat set_addr_flt (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat show_addr (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat set_vec (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat show_vec (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat show_vec_mux (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat auto_config (char *name, int32 num);

/* Global data */

extern t_bool sim_idle_enab;

#endif
