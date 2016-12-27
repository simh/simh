/* alpha_pal_defs.h: Alpha architecture PAL definitions file

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

#ifndef ALPHA_PAL_DEFS_H_
#define ALPHA_PAL_DEFS_H_  0

/* VA - NT software format */

#define NTVA_N_PDE      (VA_N_OFF - 2)                  /* PDE width */
#define NTVA_M_PDE      ((1u << NTVA_N_PDE) - 1)        /* PDE mask */
#define NTVA_N_PTD      (32 - VA_N_OFF - NTVA_N_PDE)    /* PTD width */
#define NTVA_M_PTD      ((1u << NTVA_N_PTD) - 1)        /* PTD mask */
#define NTVA_M_VPN      (M32 >> VA_N_OFF)               /* 32b VPN mask */
#define NTVPN_N_SEXT    (VA_WIDTH - 32 + 1)             /* VPN sext size */
#define NTVPN_V_SEXT    (VA_N_VPN - NTVPN_N_SEXT)       /* VPN sext start */
#define NTVPN_M_SEXT    ((1u << NTVPN_N_SEXT) - 1)      /* VPN sext mask */
#define NTVPN_GETSEXT(x) (((x) >> NTVPN_V_SEXT) & NTVPN_M_SEXT)

/* PTE - NT software format */

#define NT_VPTB         0xFFFFFFFFC0000000              /* virt page tbl base */
#define NTP_V_PFN       9                               /* PFN */
#define NTP_M_PFN       0x7FFFFF
#define NTP_PFN         (NTP_M_PFN << NTP_V_PFN)
#define NTP_V_GH        5
#define NTP_M_GH        0x3
#define NTP_V_GBL       4                               /* global = ASM */
#define NTP_V_DIRTY     2                               /* dirty = !FOW */
#define NTP_V_OWNER     1                               /* owner */
#define NTP_V_V         0                               /* valid */
#define NTP_GBL         (1u << NTP_V_GBL)
#define NTP_DIRTY       (1u << NTP_V_DIRTY)
#define NTP_OWNER       (1u << NTP_V_OWNER)
#define NTP_V           (1u << NTP_V_V)
#define NT_VPNPTD(x)    (((x) >> (NTVA_N_PDE - 2)) & (NTVA_M_PTD << 2))
#define NT_VPNPDE(x)    (((x) << 2) & (NTVA_M_PDE << 2))

/* VMS PALcode */

#define PSV_V_SPA       56                              /* VMS PS: stack align */
#define PSV_M_SPA       0x3F
#define PSV_V_IPL       8                               /* interrupt priority */
#define PSV_M_IPL       0x1F
#define PSV_V_VMM       7                               /* virt machine monitor */
#define PSV_V_CM        3                               /* current mode */
#define PSV_M_CM        0x3
#define PSV_V_IP        2                               /* intr in progress */
#define PSV_V_SW        0                               /* software */
#define PSV_M_SW        0x3
#define PSV_VMM         (1u << PSV_V_VMM)
#define PSV_IP          (1u << PSV_V_IP)
#define PSV_MASK        (PSV_VMM | PSV_IP | PSV_M_SW)
#define PSV_MBZ         0xC0FFFFFFFFFFE0E4              /* must be zero */

#define PCBV_FLAGS      56                              /* PCB flags word */

#define SISR_MASK       0xFFFE                          /* SISR bits */

#define IPL_SMAX        0x0F                            /* highest swre level */

#define SCB_FDIS        0x010                           /* SCB offsets */
#define SCB_ACV         0x080
#define SCB_TNV         0x090
#define SCB_FOR         0x0A0
#define SCB_FOW         0x0B0
#define SCB_FOE         0x0C0
#define SCB_ARITH       0x200
#define SCB_KAST        0x240
#define SCB_EAST        0x250
#define SCB_SAST        0x260
#define SCB_UAST        0x270
#define SCB_ALIGN       0x280
#define SCB_BPT         0x400
#define SCB_BUG         0x410
#define SCB_RSVI        0x420
#define SCB_RSVO        0x430
#define SCB_GENTRAP     0x440
#define SCB_CHMK        0x480
#define SCB_CHME        0x490
#define SCB_CHMS        0x4A0
#define SCB_CHMU        0x4B0
#define SCB_SISR0       0x500
#define SCB_CLOCK       0x600
#define SCB_IPIR        0x610
#define SCB_SCRD        0x620
#define SCB_PCRD        0x630
#define SCB_POWER       0x640
#define SCB_PERFM       0x650
#define SCB_SMCHK       0x660
#define SCB_PMCHK       0x670
#define SCB_PASVR       0x6F0
#define SCB_IO          0x800

#define VMS_L_STKF      (8 * 8)                         /* stack frame length */
#define VMS_MME_E       0x0000000000000001              /* mem mgt error flags */
#define VMS_MME_R       0x0000000000000000
#define VMS_MME_W       0x8000000000000000

/* VAX compatible data length definitions (for ReadUna, WriteUna) */

#define L_BYTE          1
#define L_WORD          2
#define L_LONG          4
#define L_QUAD          8

/* Unix PALcode */

#define PSU_V_CM        3                               /* Unix PS: curr mode */
#define PSU_M_CM        0x1
#define PSU_CM          (PSU_M_CM << PSU_V_CM)
#define PSU_V_IPL       0                               /* IPL */
#define PSU_M_IPL       0x7
#define PSU_IPL         (PSU_M_IPL << PSU_V_IPL)

#define PCBU_FLAGS      40                              /* PCB flags word */

#define UNIX_L_STKF     (6 * 8)                         /* kernel stack frame */
#define UNIX_IF_BPT     0                               /* entIF a0 values */
#define UNIX_IF_BUG     1
#define UNIX_IF_GEN     2
#define UNIX_IF_FDIS    3
#define UNIX_IF_RSVI    4
#define UNIX_INT_IPIR   0                               /* entInt a0 values */
#define UNIX_INT_CLK    1
#define UNIX_INT_MCRD   2
#define UNIX_INT_IO     3
#define UNIX_INT_PERF   4
#define UNIX_MMCSR_TNV  0                               /* entMM a1 values */
#define UNIX_MMCSR_ACV  1
#define UNIX_MMCSR_FOR  2
#define UNIX_MMCSR_FOW  3
#define UNIX_MMCSR_FOE  4
#define UNIX_MME_E      M64                             /* entMM a2 values */
#define UNIX_MME_R      0
#define UNIX_MME_W      1

enum vms_pal_opcodes {
    OP_HALT,    OP_DRAINA,  OP_CFLUSH,  OP_LDQP,
    OP_STQP,    OP_SWPCTX,  MF_ASN,     MT_ASTEN,
    MT_ASTSR,   OP_CSERVE,  OP_SWPPAL,  MF_FEN,
    MT_FEN,     MT_IPIR,    MF_IPL,     MT_IPL,
    MF_MCES,    MT_MCES,    MF_PCBB,    MF_PRBR,
    MT_PRBR,    MF_PTBR,    MF_SCBB,    MT_SCBB,
    MT_SIRR,    MF_SISR,    MF_TBCHK,   MT_TBIA,
    MT_TBIAP,   MT_TBIS,    MF_ESP,     MT_ESP,
    MF_SSP,     MT_SSP,     MF_USP,     MT_USP,
    MT_TBISD,   MT_TBISI,   MF_ASTEN,   MF_ASTSR,
    MF_VTBR = 0x29, MT_VTBR,MT_PERFMON, MT_DATFX = 0x2E,
    MF_VIRBND = 0x30, MT_VIRBND, MF_SYSPTBR, MT_SYSPTBR,
    OP_WTINT = 0x3E, MF_WHAMI = 0x3F,
    OP_BPT = 0x80, OP_BUGCHK, OP_CHME,  OP_CHMK,
    OP_CHMS,    OP_CHMU,    OP_IMB,     OP_INSQHIL,
    OP_INSQTIL, OP_INSQHIQ, OP_INSQTIQ, OP_INSQUEL,
    OP_INSQUEQ, OP_INSQUELD,OP_INSQUEQD,OP_PROBER,
    OP_PROBEW,  OP_RD_PS,   OP_REI,     OP_REMQHIL,
    OP_REMQTIL, OP_REMQHIQ, OP_REMQTIQ, OP_REMQUEL,
    OP_REMQUEQ, OP_REMQUELD,OP_REMQUEQD,OP_SWASTEN,
    OP_WR_PS_SW,OP_RSCC,    OP_RD_UNQ,  OP_WR_UNQ,
    OP_AMOVRR,  OP_AMOVRM,  OP_INSQHILR,OP_INSQTILR,
    OP_INSQHIQR,OP_INSQTIQR,OP_REMQHILR,OP_REMQTILR,
    OP_REMQHIQR,OP_REMQTIQR,OP_GENTRAP,
    OP_CLRFEN = 0xAE
    };

enum unix_pal_opcodes {
    OP_halt,     OP_draina, OP_cflush,
    OP_cserve = 0x9, OP_swppal,
    OP_rdmces = 0x10, OP_wrmces,
    OP_wrvirbnd = 0x13, OP_wrsysptbr = 0x14,
    OP_wrfen = 0x2B, OP_wrvptptr = 0x2D, OP_wrasn,
    OP_swpctx = 0x30, OP_wrval, OP_rdval, OP_tbi,
    OP_wrent,   OP_swpipl,  OP_rdps,    OP_wrkgp,
    OP_wrusp,   OP_wrperfmon, OP_rdusp,
    OP_whami = 0x3C, OP_retsys, OP_wtint, OP_rti,
    OP_bpt = 0x80, OP_bugchk, OP_syscall = 0x83,
    OP_imb = 0x86,
    OP_urti = 0x92, OP_rdunique = 0x9E, OP_wrunique,
    OP_gentrap = 0xAA, OP_clrfen = 0xAE
    };

#endif
