/* alpha_ev5_defs.h: Alpha EV5 chip definitions file

   Copyright (c) 2003-2005, Robert M Supnik

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

#ifndef ALPHA_EV5_DEFS_H_
#define ALPHA_EV5_DEFS_H_      0

/* Address limits */

#define VA_SIZE                 43                       /* VA size */
#define NTVA_WIDTH              32                       /* VA width for NT */
#define VA_MASK                 0x000007FFFFFFFFFF
#define EV5_PA_SIZE             40                       /* PA size */
#define EV5_PA_MASK             0x000000FFFFFFFFFF

/* Virtual address */

#define VA_N_OFF        13                              /* offset size */
#define VA_PAGSIZE      (1u << VA_N_OFF)                /* page size */
#define VA_M_OFF        ((1u << VA_N_OFF) - 1)          /* offset mask */
#define VA_N_LVL        10                              /* width per level */
#define VA_M_LVL        ((1u << VA_N_LVL) - 1)          /* level mask */
#define VA_V_VPN        VA_N_OFF                        /* vpn start */
#define VA_N_VPN        (VA_N_LVL * 3)                  /* vpn size */
#define VA_M_VPN        ((1u << VA_N_VPN) - 1)          /* vpn mask */
#define VA_WIDTH        (VA_N_VPN + VA_N_OFF)           /* total VA size */
#define VA_V_SEXT       (VA_WIDTH - 1)                  /* sext start */
#define VA_M_SEXT       ((1u << (64 - VA_V_SEXT)) - 1)  /* sext mask */
#define VA_GETOFF(x)    (((uint32) (x)) & VA_M_OFF)
#define VA_GETVPN(x)    (((uint32) ((x) >> VA_V_VPN)) & VA_M_VPN)
#define VA_GETSEXT(x)   (((uint32) ((x) >> VA_V_SEXT)) & VA_M_SEXT)
#define PHYS_ADDR(p,v)  (((((t_uint64) (p)) < VA_N_OFF) | VA_GETOFF (v)) & EV5_PA_MASK)

/* 43b and 32b superpages - present in all implementations */

#define SPEN_43                 0x2
#define SPEN_32                 0x1
#define SP43_MASK               (EV5_PA_MASK)
#define SP32_MASK               0x000000003FFFFFFF
#define VPN_GETSP43(x)          ((uint32) (((x) >> (VA_WIDTH - VA_N_OFF - 2)) & 3))
#define VPN_GETSP32(x)          ((uint32) (((x) >> (NTVA_WIDTH - VA_N_OFF - 2)) & 0x1FFF))

/* TLBs */

#define INV_TAG                 M32
#define ITLB_SIZE               48
#define DTLB_SIZE               64
#define ITLB_WIDTH              6
#define DTLB_WIDTH              6

#define TLB_CI                  0x1                     /* clear I */
#define TLB_CD                  0x2                     /* clear D */
#define TLB_CA                  0x4                     /* clear all */

typedef struct {
    uint32                      tag;                    /* tag */
    uint8                       asn;                    /* addr space # */
    uint8                       idx;                    /* entry # */
    uint16                      gh_mask;                /* gh mask */
    uint32                      pfn;                    /* pfn */
    uint32                      pte;                    /* swre/pte */
    } TLBENT;

/* Register shadow */

#define PALSHAD_SIZE            8
#define PAL_USE_SHADOW \
    ev5_palsave[0] = R[8]; ev5_palsave[1] = R[9]; \
    ev5_palsave[2] = R[10]; ev5_palsave[3] = R[11]; \
    ev5_palsave[4] = R[12]; ev5_palsave[5] = R[13]; \
    ev5_palsave[6] = R[14]; ev5_palsave[7] = R[25]; \
    R[8] = ev5_palshad[0]; R[9] = ev5_palshad[1]; \
    R[10] = ev5_palshad[2]; R[11] = ev5_palshad[3]; \
    R[12] = ev5_palshad[4]; R[13] = ev5_palshad[5]; \
    R[14] = ev5_palshad[6]; R[25] = ev5_palshad[7]
#define PAL_USE_MAIN \
    ev5_palshad[0] = R[8]; ev5_palshad[1] = R[9]; \
    ev5_palshad[2] = R[10]; ev5_palshad[3] = R[11]; \
    ev5_palshad[4] = R[12]; ev5_palshad[5] = R[13]; \
    ev5_palshad[6] = R[14]; ev5_palshad[7] = R[25]; \
    R[8] = ev5_palsave[0]; R[9] = ev5_palsave[1]; \
    R[10] = ev5_palsave[2]; R[11] = ev5_palsave[3]; \
    R[12] = ev5_palsave[4]; R[13] = ev5_palsave[5]; \
    R[14] = ev5_palsave[6]; R[25] = ev5_palsave[7]

/* PAL instructions */

#define HW_MFPR                 0x19
#define HW_LD                   0x1B
#define HW_MTPR                 0x1D
#define HW_REI                  0x1E
#define HW_ST                   0x1F

#define HW_LD_V                 0x8000
#define HW_LD_ALT               0x4000
#define HW_LD_WCH               0x2000
#define HW_LD_Q                 0x1000
#define HW_LD_PTE               0x0800
#define HW_LD_LCK               0x0400
#define HW_LD_DSP               0x03FF
#define SIGN_HW_LD_DSP          0x0200
#define HW_LD_GETDSP(x)         ((x) & HW_LD_DSP)
#define SEXT_HW_LD_DSP(x)       (((x) & SIGN_HW_LD_DSP)? \
                                ((x) | ~((t_uint64) HW_LD_DSP)): ((x) & HW_LD_DSP))

#define HW_REI_S                0x4000

/* PAL entry offsets */

#define PALO_RESET              0x0000
#define PALO_IACV               0x0080
#define PALO_INTR               0x0100
#define PALO_ITBM               0x0180
#define PALO_DTBM               0x0200
#define PALO_DTBM_D             0x0280
#define PALO_ALGN               0x0300
#define PALO_DFLT               0x0380
#define PALO_MCHK               0x0400
#define PALO_RSVI               0x0480
#define PALO_TRAP               0x0500
#define PALO_FDIS               0x0580
#define PALO_CALLPR             0x2000
#define PALO_CALLUNPR           0x3000

/* Special (above 1F) and normal interrupt levels */

#define IPL_HALT                0x40
#define IPL_SLI                 0x20
#define IPL_1F                  0x1F                    /* highest level */
#define IPL_CRD                 0x1F                    /* corrected read data */
#define IPL_PWRFL               0x1E                    /* power fail */
#define IPL_AST                 0x02                    /* AST interrupt level */

/* Internal registers */

#define PALTEMP_SIZE            24

enum ev5_internal_reg {
    ISR = 0x100, ITB_TAG, ITB_PTE, ITB_ASN,
    ITB_PTE_TEMP, ITB_IA, ITB_IAP, ITB_IS,
    SIRR, ASTRR, ASTEN, EXC_ADDR,
    EXC_SUMM, EXC_MASK, PAL_BASE, ICM,
    IPLR, INTID, IFAULT_VA_FORM, IVPTBR,
    HWINT_CLR = 0x115, SL_XMIT, SL_RCV,
    ICSR, IC_FLUSH_CTL, ICPERR_STAT, PMCTR = 0x11C,
    PALTEMP = 0x140,
    DTB_ASN = 0x200, DTB_CM, DTB_TAG, DTB_PTE,
    DTB_PTE_TEMP, MM_STAT, VA, VA_FORM,
    MVPTBR, DTB_IAP, DTB_IA, DTB_IS,
    ALTMODE, CC, CC_CTL, MCSR,
    DC_FLUSH, DC_PERR_STAT = 0x212, DC_TEST_CTL,
    DC_TEST_TAG, DC_TEST_TAG_TEMP, DC_MODE, MAF_MODE
    };

/* Ibox registers */
/* ISR - instruction summary register - read only */

#define ISR_V_AST               0
#define ISR_V_SIRR              4
#define ISR_V_ATR               19
#define ISR_V_IRQ0              20
#define ISR_V_IRQ1              21
#define ISR_V_IRQ2              22
#define ISR_V_IRQ3              23
#define ISR_V_PFL               30
#define ISR_V_MCHK              31
#define ISR_V_CRD               32
#define ISR_V_SLI               33
#define ISR_V_HALT              34

#define ISR_ATR                 (((t_uint64) 1u) << ISR_V_ATR)
#define ISR_IRQ0                (((t_uint64) 1u) << ISR_V_IRQ0)
#define ISR_IRQ1                (((t_uint64) 1u) << ISR_V_IRQ1)
#define ISR_IRQ2                (((t_uint64) 1u) << ISR_V_IRQ2)
#define ISR_IRQ3                (((t_uint64) 1u) << ISR_V_IRQ3)
#define ISR_HALT                (((t_uint64) 1u) << ISR_V_HALT)

/* ITB_TAG - ITLB tag - write only - stores VPN (tag) of faulting address */

/* ITB_PTE - ITLB pte - read and write in different formats */

#define ITBR_PTE_V_ASM          13
#define ITBR_PTE_ASM            (1u << ITBR_PTE_V_ASM)
#define ITBR_PTE_V_KRE          18
#define ITBR_PTE_GH0            0x00000000
#define ITBR_PTE_GH1            0x20000000
#define ITBR_PTE_GH2            0x60000000
#define ITBR_PTE_GH3            0xE0000000

/* ITB_ASN - ITLB ASN - read write */

#define ITB_ASN_V_ASN           4
#define ITB_ASN_M_ASN           0x7F
#define ITB_ASN_WIDTH           7

/* ITB_PTE_TEMP - ITLB PTE readout - read only */

/* ITB_IA, ITB_IAP, ITB_IS - ITLB invalidates - write only */

/* SIRR - software interrupt request register - read/write */

#define SIRR_V_SIRR             4
#define SIRR_M_SIRR             0x7FFF

/* ASTRR, ASTEN - AST request, enable registers - read/write */

#define AST_MASK                0xF                     /* AST bits */

/* EXC_ADDR - read/write */

/* EXC_SUMM - read/cleared on write */

/* EXC_MASK - read only */

/* PAL_BASE - read/write */

#define PAL_BASE_RW             0x000000FFFFFFFFC000

/* ICM - ITLB current mode - read/write */

#define ICM_V_CM                3
#define ICM_M_CM                0x3

/* IPLR - interrupt priority level - read/write */

#define IPLR_V_IPL              0
#define IPLR_M_IPL              0x1F

/* INTID - interrupt ID - read only */

#define INTID_MASK              0x1F

/* IFAULT_VA_FORM - formated fault VA - read only */

/* IVPTBR - virtual page table base - read/write */

#define IVPTBR_VMS              0xFFFFFFF800000000
#define IVPTBR_NT               0xFFFFFFFFC0000000
#define FMT_IVA_VMS(x)          (ev5_ivptbr | (((x) >> (VA_N_OFF - 3)) & 0x1FFFFFFF8))
#define FMT_IVA_NT(x)           (ev5_ivptbr | (((x) >> (VA_N_OFF - 3)) & 0x0003FFFF8))

/* HWINT_CLR - hardware interrupt clear - write only */

#define HWINT_CLR_W1C           0x00000003C8000000

/* SL_XMIT - serial line transmit - write only */

/* SL_RCV - real line receive - read only */

/* ICSR - Ibox control/status - read/write */

#define ICSR_V_PME              8
#define ICSR_M_PME              0x3
#define ICSR_V_BSE              17
#define ICSR_V_MSK0             20
#define ICSR_V_MSK1             21
#define ICSR_V_MSK2             22
#define ICSR_V_MSK3             23
#define ICSR_V_TMM              24
#define ICSR_V_TMD              25
#define ICSR_V_FPE              26
#define ICSR_V_HWE              27
#define ICSR_V_SPE              28
#define ICSR_M_SPE              0x3
#define ICSR_V_SDE              30
#define ICSR_V_CRDE             32
#define ICSR_V_SLE              33
#define ICSR_V_FMS              34
#define ICSR_V_FBT              35
#define ICSR_V_FBD              36
#define ICSR_V_BIST             38
#define ICSR_V_TEST             39

#define ICSR_NT                 (((t_uint64) 1u) << ICSR_V_SPE)
#define ICSR_BSE                (((t_uint64) 1u) << ICSR_V_BSE)
#define ICSR_MSK0               (((t_uint64) 1u) << ICSR_V_MSK0)
#define ICSR_MSK1               (((t_uint64) 1u) << ICSR_V_MSK1)
#define ICSR_MSK2               (((t_uint64) 1u) << ICSR_V_MSK2)
#define ICSR_MSK3               (((t_uint64) 1u) << ICSR_V_MSK3)
#define ICSR_HWE                (((t_uint64) 1u) << ICSR_V_HWE)
#define ICSR_SDE                (((t_uint64) 1u) << ICSR_V_SDE)
#define ICSR_CRDE               (((t_uint64) 1u) << ICSR_V_CRDE)
#define ICSR_SLE                (((t_uint64) 1u) << ICSR_V_SLE)

#define ICSR_RW                 0x0000009F4BF00300
#define ICSR_MBO                0x0000006000000000

/* IC_FLUSH_CTL - Icache flush control - write only */

/* ICPERR_STAT - Icache parity status - read/write 1 to clear */

#define ICPERR_V_DPE            11
#define ICPERR_V_TPE            12
#define ICPERR_V_TMO            13

#define ICPERR_DPE              (1u << ICPERR_V_DPE)
#define ICPERR_TPE              (1u << ICPERR_V_TPE)
#define ICPERR_TMO              (1u << ICPERR_V_TMO)

#define ICPERR_W1C              (ICPERR_DPE|ICPERR_TPE|ICPERR_TMO)

/* Mbox registers */
/* DTB_ASN - DTLB ASN - write only */

#define DTB_ASN_V_ASN           57
#define DTB_ASN_M_ASN           0x7F
#define DTB_ASN_WIDTH           7

/* DTB_CM - DTLB current mode - write only */

#define DCM_V_CM                3
#define DCM_M_CM                0x3

/* DTB_TAG - DTLB tag and update - write only */

/* DTB_PTE - DTLB PTE - read/write */

/* DTB_PTE_TEMP - DTLB PTE read out register - read only */

/* MM_STAT - data fault status register - read only */

#define MM_STAT_WR              0x00001
#define MM_STAT_ACV             0x00002
#define MM_STAT_FOR             0x00004
#define MM_STAT_FOW             0x00008
#define MM_STAT_TBM             0x00010
#define MM_STAT_BVA             0x00020
#define MM_STAT_V_RA            6
#define MM_STAT_IMASK           0x1FFC0

/* VA - data fault virtual address - read only */

/* VA_FORM - data fault formated virtual address - read only */

#define FMT_MVA_VMS(x)          (ev5_mvptbr | (((x) >> (VA_N_OFF - 3)) & 0x1FFFFFFF8))
#define FMT_MVA_NT(x)           (ev5_mvptbr | (((x) >> (VA_N_OFF - 3)) & 0x0003FFFF8))

/* MVPTBR - DTB virtual page table base - write only */

#define MVPTBR_MBZ              ((t_uint64) 0x3FFFFFFF)

/* DTB_IAP, DTB_IA, DTB_IS - DTB invalidates - write only */

/* ALT_MODE - DTLB current mode - write only */

#define ALT_V_CM                3
#define ALT_M_CM                0x3

/* CC - cycle counter - upper half is RW, lower half is RO */

/* CC_CTL - cycle counter control - write only */

#define CC_CTL_ENB              0x100000000
#define CC_CTL_MBZ              0xF

/* MCSR - Mbox control/status register - read/write */

#define MCSR_RW                 0x11
#define MCSR_V_SPE              1
#define MCSR_M_SPE              0x3
#define MCSR_NT                 0x02

/* DC_PERR_STAT - data cache parity error status - read/write */

#define DC_PERR_W1C             0x3
#define DC_PERR_ERR             0x1C

/* DC_MODE - data cache mode - read/write */

#define DC_MODE_RW              0xF

/* MAF_MODE - miss address file mode - read/write */

#define MAF_MODE_RW             0xFF

/* DC_TEST_CTL - data cache test control - read/write */

#define DC_TEST_CTL_RW          0x1FFFB

/* DC_TEST_TAG - data cache test tag - read/write */

#define DC_TEST_TAG_RW          0x0000007FFFFFFF04

/* Function prototypes (TLB interface) */

void tlb_ia (uint32 flags);
void tlb_is (t_uint64 va, uint32 flags);
void itlb_set_asn (uint32 asn);
void itlb_set_cm (uint32 mode);
void itlb_set_spage (uint32 spage);
TLBENT *itlb_lookup (uint32 vpn);
TLBENT *itlb_load (uint32 vpn, t_uint64 pte);
t_uint64 itlb_read (void);
void dtlb_set_asn (uint32 asn);
void dtlb_set_cm (uint32 mode);
void dtlb_set_spage (uint32 spage);
TLBENT *dtlb_lookup (uint32 vpn);
TLBENT *dtlb_load (uint32 vpn, t_uint64 pte);
t_uint64 dtlb_read (void);

#endif

