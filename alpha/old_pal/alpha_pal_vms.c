/* alpha_pal_vms.c - Alpha VMS PAL code simulator

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

   This module contains the PALcode implementation for Alpha VMS, except for
   the console, which is always done in hardware mode.

   Alpha VMS requires a complex privileged state, modelled after the VAX:

        PS<12:0>                processor status
          IPL<4:0>                interrupt level - in base
          VMM<0>                  virtual machine mode
          CM<1:0>                 current mode - in base
          IP<0>                   interrupt in progress
          SW<1:0>                 software controlled
        KSP<63:0>               kernel stack pointer
        ESP<63:0>               executive stack pointer
        SSP<63:0>               supervisor stack pointer
        USP<63:0>               user stack pointer
        SSC<63:0>               system cycle counter
        PCBB<63:0>              process control block base
        SCBB<63:0>              system control block base
        PTBR<63:0>              page table base
        VTBR<63:0>              virtual page table base
        VIRBND<63:0>            virtual address boundary
        SYSPTBR<63:0>           system page table base register
        PRBR<63:0>              processor base register
        THREAD<63:0>            thread-unique value
        SIRR<15:1>              software interrupt requests
        ASTEN<3:0>              AST enables
        ASTRQ<3:0>              AST requests
        FEN<0>                  floating enable
        DATFX<0>                data alignment trap enable

   Note that some of this state exists in the hardware implementations and
   so is declared in the base CPU.
*/

#include "alpha_defs.h"

/* Alignment table */

#define ALG_W                   1                       /* word inst */
#define ALG_L                   2                       /* long inst */
#define ALG_Q                   3                       /* quad inst */
#define ALG_ST                  0x10                    /* store */
#define ALG_INV                 -1                      /* invalid inst */
#define ALG_ERR                 0                       /* internal error */
#define ALG_GETLNT(x)           ((x) & 3)

#define GET_PSV                 ((vms_ipl << PSV_V_IPL) | (vms_cm << PSV_V_CM) | \
                                (vms_ps & PSV_MASK))
#define AST_TST(l)              (((l) < IPL_AST) && (vms_asten & vms_astsr & ast_map[vms_cm]))
#define MOST_PRIV(m1,m2)        (((m1) < (m2))? (m1): (m2))

#define ksp                     vms_stkp[MODE_K]
#define esp                     vms_stkp[MODE_E]
#define ssp                     vms_stkp[MODE_S]
#define usp                     vms_stkp[MODE_U]

// kludge for debugging...
#define io_get_vec(x)           0

t_uint64 vms_ptbr = 0;                                  /* page table base */
t_uint64 vms_vtbr = 0;                                  /* virt page table base */
t_uint64 vms_virbnd = M64;                              /* virtual boundary */
t_uint64 vms_sysptbr = 0;                               /* system page table base */
t_uint64 vms_hwpcb = 0;                                 /* hardware PCB */
t_uint64 vms_thread = 0;                                /* thread unique */
t_uint64 vms_prbr = 0;                                  /* processor unique */
t_uint64 vms_stkp[4];                                   /* stack pointers */
t_uint64 vms_scbb = 0;                                  /* SCB base */
t_uint64 vms_scc = 0;                                   /* system cycle ctr */
t_uint64 vms_mces = 0;                                  /* machine check err summ */
uint32 vms_ipl = 0;                                     /* hardware IPL */
uint32 vms_cm = 0;                                      /* inst current mode */
uint32 vms_sisr = 0;                                    /* software int req */
uint32 vms_asten = 0;                                   /* AST enables */
uint32 vms_astsr = 0;                                   /* AST requests */
uint32 vms_last_pcc = 0;                                /* last pcc_l */
uint32 vms_datfx = 0;                                   /* data alignment */
uint32 vms_ps = 0;                                      /* static PS */

const uint32 ast_map[4] = { 0x1, 0x3, 0x7, 0xF };
const uint32 ast_pri[16] = {
 0,      MODE_K, MODE_E, MODE_K, MODE_S, MODE_K, MODE_E, MODE_K,
 MODE_U, MODE_K, MODE_E, MODE_K, MODE_S, MODE_K, MODE_E, MODE_K
 };
static const uint32 lnt_map[4] = { L_BYTE, L_WORD, L_LONG, L_QUAD };
static const int8 alg_map[64] = {
 ALG_ERR, ALG_ERR, ALG_ERR, ALG_ERR,
 ALG_ERR, ALG_ERR, ALG_ERR, ALG_ERR,
 ALG_ERR, ALG_ERR, ALG_ERR, ALG_ERR,
 ALG_W, ALG_W|ALG_ST, ALG_ERR, ALG_ERR,
 ALG_ERR, ALG_ERR, ALG_ERR, ALG_ERR,
 ALG_ERR, ALG_ERR, ALG_ERR, ALG_ERR,
 ALG_ERR, ALG_ERR, ALG_ERR, ALG_ERR,
 ALG_ERR, ALG_ERR, ALG_ERR, ALG_ERR,
 ALG_L,   ALG_Q,   ALG_L,   ALG_Q,
 ALG_L|ALG_ST, ALG_Q|ALG_ST, ALG_L|ALG_ST, ALG_Q|ALG_ST,
 ALG_L,   ALG_Q,   ALG_INV, ALG_INV,
 ALG_L|ALG_ST, ALG_Q|ALG_ST, ALG_INV, ALG_INV,
 ALG_ERR, ALG_ERR, ALG_ERR, ALG_ERR,
 ALG_ERR, ALG_ERR, ALG_ERR, ALG_ERR,
 ALG_ERR, ALG_ERR, ALG_ERR, ALG_ERR,
 ALG_ERR, ALG_ERR, ALG_ERR, ALG_ERR
 };

extern t_uint64 R[32];
extern t_uint64 PC, trap_mask;
extern t_uint64 p1;
extern uint32 vax_flag, lock_flag;
extern uint32 fpen;
extern uint32 ir, pcc_h, pcc_l, pcc_enb;
extern uint32 cm_racc, cm_wacc, cm_macc;
extern uint32 mmu_ispage, mmu_dspage;
extern jmp_buf save_env;
extern uint32 int_req[IPL_HLVL];

t_int64 vms_insqhil (void);
t_int64 vms_insqtil (void);
t_int64 vms_insqhiq (void);
t_int64 vms_insqtiq (void);
t_int64 vms_insquel (uint32 defer);
t_int64 vms_insqueq (uint32 defer);
t_int64 vms_remqhil (void);
t_int64 vms_remqtil (void);
t_int64 vms_remqhiq (void);
t_int64 vms_remqtiq (void);
t_int64 vms_remquel (uint32 defer);
t_int64 vms_remqueq (uint32 defer);
t_int64 vms_insqhilr (void);
t_int64 vms_insqtilr (void);
t_int64 vms_insqhiqr (void);
t_int64 vms_insqtiqr (void);
t_int64 vms_remqhilr (void);
t_int64 vms_remqtilr (void);
t_int64 vms_remqhiqr (void);
t_int64 vms_remqtiqr (void);
uint32 vms_probe (uint32 acc);
uint32 vms_amovrr (void);
uint32 vms_amovrm (void);
t_stat vms_rei (void);
void vms_swpctx (void);
t_stat vms_intexc (uint32 vec, uint32 newmode, uint32 newipl);
t_stat vms_mm_intexc (uint32 vec, t_uint64 par2);
t_stat pal_proc_reset_vms (DEVICE *dptr);
t_uint64 ReadUna (t_uint64 va, uint32 lnt, uint32 acc);
void WriteUna (t_uint64 va, t_uint64 val, uint32 lnt, uint32 acc);
uint32 tlb_check (t_uint64 va);
uint32 Test (t_uint64 va, uint32 acc, t_uint64 *pa);

extern t_stat (*pal_eval_intr) (uint32 ipl);
extern t_stat (*pal_proc_excp) (uint32 type);
extern t_stat (*pal_proc_trap) (uint32 type);
extern t_stat (*pal_proc_intr) (uint32 type);
extern t_stat (*pal_proc_inst) (uint32 fnc);
extern uint32 (*pal_find_pte) (uint32 vpn, t_uint64 *pte);

/* VMSPAL data structures

   vmspal_dev   device descriptor
   vmspal_unit  unit
   vmspal_reg   register list
*/

UNIT vmspal_unit = { UDATA (NULL, 0, 0) };

REG vmspal_reg[] = {
    { HRDATA (KSP, ksp, 64) },
    { HRDATA (ESP, esp, 64) },
    { HRDATA (SSP, ssp, 64) },
    { HRDATA (USP, usp, 64) },
    { HRDATA (PTBR, vms_ptbr, 64) },
    { HRDATA (VTBR, vms_vtbr, 64) },
    { HRDATA (VIRBND, vms_virbnd, 64) },
    { HRDATA (SYSPTBR, vms_sysptbr, 64) },
    { HRDATA (THREAD, vms_thread, 64) },
    { HRDATA (PRBR, vms_prbr, 64) },
    { HRDATA (HWPCB, vms_hwpcb, 64) },
    { HRDATA (SCBB, vms_scbb, 64) },
    { HRDATA (SCC, vms_scc, 64) },
    { HRDATA (LASTPCC, vms_last_pcc, 32), REG_HRO },
    { HRDATA (MCES, vms_mces, 64) },
    { HRDATA (PS, vms_ps, 13) },
    { HRDATA (IPL, vms_ipl, 5) },
    { HRDATA (CM, vms_cm, 2) },
    { HRDATA (SISR, vms_sisr, 16) },
    { HRDATA (ASTEN, vms_asten, 4) },
    { HRDATA (ASTSR, vms_astsr, 4) },
    { FLDATA (DATFX, vms_datfx, 0) },
    { NULL }
    };

DEVICE vmspal_dev = {
    "VMSPAL", &vmspal_unit, vmspal_reg, NULL,
    1, 16, 1, 1, 16, 8,
    NULL, NULL, &pal_proc_reset_vms,
    NULL, NULL, NULL,
    NULL, 0
    };

/* VMS interrupt evaluator - returns IPL of highest priority interrupt */

uint32 pal_eval_intr_vms (uint32 lvl)
{
uint32 i;
static const int32 sw_int_mask[32] = {
    0xFFFE, 0xFFFC, 0xFFF8, 0xFFF0,                     /* 0 - 3 */
    0xFFE0, 0xFFC0, 0xFF80, 0xFF00,                     /* 4 - 7 */
    0xFE00, 0xFC00, 0xF800, 0xF000,                     /* 8 - B */
    0xE000, 0xC000, 0x8000, 0x0000,                     /* C - F */
    0x0000, 0x0000, 0x0000, 0x0000,                     /* 10+ */
    0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000
    };

vms_scc = vms_scc + ((pcc_l - vms_last_pcc) & M32);     /* update scc */
vms_last_pcc = pcc_l;
for (i = IPL_HMAX; i >= IPL_HMIN; i--) {                /* chk hwre int */
    if (i <= lvl) return 0;                             /* at ipl? no int */
    if (int_req[i - IPL_HMIN]) return i;                /* req != 0? int */
    }
if (vms_sisr & sw_int_mask[lvl]) {                      /* swre interrupt? */
    for (i = IPL_SMAX; i > lvl; i--) {                  /* check swre int */
        if ((vms_sisr >> i) & 1)                        /* req != 0? int */
            return (AST_TST (i)? IPL_AST: i);           /* check for AST */
        }
    }
return (AST_TST (lvl)? IPL_AST: 0);                     /* no swre, check AST */
}

/* VMS interrupt dispatch - reached from top of execute loop */

t_stat pal_proc_intr_vms (uint32 lvl)
{
uint32 vec;
t_stat r;

if (lvl > IPL_HMAX) return SCPE_IERR;                   /* above max? */
else if (lvl >= IPL_HMIN) vec = io_get_vec (lvl);       /* hwre? get vector */
else if (lvl > IPL_SMAX) return SCPE_IERR;              /* above swre max? */
else if (lvl > 0) {                                     /* swre int? */
    if ((lvl == IPL_AST) && (vms_asten & vms_astsr & ast_map[vms_cm])) {
        uint32 astm = ast_pri[vms_astsr & 0xF];         /* get AST priority */
        vms_astsr = vms_astsr & ~(1u << astm);          /* clear hi pri */
        vec = SCB_KAST + (astm << 4);
        }
    else {                                              /* swre int */
        vms_sisr = vms_sisr & ~(1u << lvl);
        vec = SCB_SISR0 + (lvl << 4);
        }
    }
else return SCPE_IERR;                                  /* bug */
if (vec == 0) vec = SCB_PASVR;                          /* passive release? */
r = vms_intexc (vec, MODE_K, lvl);                      /* do interrupt */
vms_ps = vms_ps | PSV_IP;                               /* set int in prog */
return r;
}

/* VMS trap dispatch - reached synchronously from bottom of execute loop */

t_stat pal_proc_trap_vms (uint32 tsum)
{
t_stat r;

r = vms_intexc (SCB_ARITH, MODE_K, vms_ipl);            /* arithmetic trap */
R[4] = trap_mask;                                       /* set parameters */
R[5] = tsum;
return r;
}

/* VMS exception dispatch - reached from the ABORT handler */

t_stat pal_proc_excp_vms (uint32 abval)
{
uint32 op, ra, lntc;
int8 fl;
t_stat r;

switch (abval) {

    case EXC_RSVI:                                      /* reserved instr */
        return vms_intexc (SCB_RSVI, MODE_K, vms_ipl);  /* trap */

    case EXC_RSVO:                                      /* reserved operand */
        return vms_intexc (SCB_RSVO, MODE_K, vms_ipl);  /* trap */

    case EXC_ALIGN:                                     /* unaligned */
        op = I_GETOP (ir);                              /* get opcode */
        ra = I_GETRA (ir);                              /* get RA */
        fl = alg_map[op];                               /* get alignment map */
        if (fl == ALG_ERR) return SCPE_IERR;            /* impossible? */
        if (fl == ALG_INV) return (SCB_RSVI, MODE_K, vms_ipl);  /* conditional? */
        lntc = ALG_GETLNT (fl);                         /* get length code */
        if (fl & ALG_ST)                                /* store? */
            WriteUna (p1, R[ra], lnt_map[lntc], cm_wacc);
        else if (ra != 31)
            R[ra] = ReadUna (p1, lnt_map[lntc], cm_racc);
        if (vms_datfx) break;                           /* trap? */
        r = vms_intexc (SCB_ALIGN, MODE_K, vms_ipl);    /* do trap */
        R[4] = p1;                                      /* R4 = va */
        R[5] = (fl & ALG_ST)? 1: 0;                     /* R5 = load/store */
        return r;

    case EXC_FPDIS:                                     /* fp disabled */
        PC = (PC - 4) & M64;                            /* back up PC */
        return vms_intexc (SCB_FDIS, MODE_K, vms_ipl);  /* fault */

    case EXC_FOX+EXC_E:                                 /* FOE */
        tlb_is (p1, TLB_CI);
        return vms_mm_intexc (SCB_FOE, VMS_MME_E);

    case EXC_FOX+EXC_R:                                 /* FOR */
        PC = (PC - 4) & M64;                            /* back up PC */
        return vms_mm_intexc (SCB_FOR, VMS_MME_R);

    case EXC_FOX+EXC_W:                                 /* FOW */
        PC = (PC - 4) & M64;                            /* back up PC */
        return vms_mm_intexc (SCB_FOW, VMS_MME_W);

    case EXC_BVA+EXC_E:
    case EXC_ACV+EXC_E:                                 /* instr ACV */
        return vms_mm_intexc (SCB_ACV, VMS_MME_E);

    case EXC_BVA+EXC_R:
    case EXC_ACV+EXC_R:                                 /* data read ACV */
        PC = (PC - 4) & M64;                            /* back up PC */
        return vms_mm_intexc (SCB_ACV, VMS_MME_R);

    case EXC_BVA+EXC_W:
    case EXC_ACV+EXC_W:                                 /* data write ACV */
        PC = (PC - 4) & M64;                            /* back up PC */
        return vms_mm_intexc (SCB_ACV, VMS_MME_W);

    case EXC_TNV+EXC_E:                                 /* instr TNV */
        tlb_is (p1, TLB_CI);
        return vms_mm_intexc (SCB_TNV, VMS_MME_E);

    case EXC_TNV+EXC_R:                                 /* data read TNV */
        tlb_is (p1, TLB_CD);
        PC = (PC - 4) & M64;                            /* back up PC */
        return vms_mm_intexc (SCB_TNV, VMS_MME_R);

    case EXC_TNV+EXC_W:                                 /* data write TNV */
        tlb_is (p1, TLB_CD);
        PC = (PC - 4) & M64;                            /* back up PC */
        return vms_mm_intexc (SCB_TNV, VMS_MME_W);

    case EXC_TBM + EXC_E:                               /* TLB miss */
    case EXC_TBM + EXC_R:
    case EXC_TBM + EXC_W:
        return SCPE_IERR;                               /* should not occur */

    default:
        return STOP_INVABO;
        }

return SCPE_OK;
}

/* PALcode instruction dispatcher - function code verified in CPU */

t_stat pal_proc_inst_vms (uint32 fnc)
{
t_uint64 val;
uint32 arg32 = (uint32) R[16];

if ((fnc < 0x40) && (vms_cm != MODE_K)) ABORT (EXC_RSVI);
switch (fnc) {

    case OP_HALT:
        return STOP_HALT;

    case OP_CFLUSH:
    case OP_DRAINA:
        break;

    case OP_LDQP:
        R[0] = ReadPQ (R[16]);
        break;

    case OP_STQP:
        WritePQ (R[16], R[17]);
        break;

    case OP_SWPCTX:
        vms_swpctx ();
        break;

    case MF_ASN:
        R[0] = itlb_read_asn ();
        break;

    case MT_ASTEN:
        R[0] = vms_asten & AST_MASK;
        vms_asten = ((vms_asten & arg32) | (arg32 >> 4)) & AST_MASK;
        break;

    case MT_ASTSR:
        R[0] = vms_astsr & AST_MASK;
        vms_astsr = ((vms_astsr & arg32) | (arg32 >> 4)) & AST_MASK;
        break;

    case OP_CSERVE:
        // tbd
        break;

    case OP_SWPPAL:
        R[0] = 0;
        break;

    case MF_FEN:
        R[0] = fpen & 1;
        break;

    case MT_FEN:
        fpen = arg32 & 1;
        arg32 = ReadPL (vms_hwpcb + PCBV_FLAGS);
        arg32 = (arg32 & ~1) | fpen;
        WritePL (vms_hwpcb + PCBV_FLAGS, arg32);
        break;

    case MT_IPIR:
        //tbd
        break;

    case MF_IPL:
        R[0] = vms_ipl & PSV_M_IPL;
        break;

    case MT_IPL:
        R[0] = vms_ipl & PSV_M_IPL;
        vms_ipl = arg32 & PSV_M_IPL;
        break;

    case MF_MCES:
        R[0] = vms_mces;
        break;

    case MT_MCES:
        vms_mces = (vms_mces | (arg32 & MCES_DIS)) & ~(arg32 & MCES_W1C);
        break;

    case MF_PCBB:
        R[0] = vms_hwpcb;
        break;

    case MF_PRBR:
        R[0] = vms_prbr;
        break;

    case MT_PRBR:
        vms_prbr = R[16];
        break;

    case MF_PTBR:
        R[0] = (vms_ptbr >> VA_N_OFF);                  /* PFN only */
        break;

    case MF_SCBB:
        R[0] = vms_scbb;
        break;

    case MT_SCBB:
        vms_scbb = R[16];
        break;

    case MF_SISR:
        R[0] = vms_sisr & SISR_MASK;
        break;

    case MT_SIRR:
        vms_sisr = (vms_sisr | (1u << (arg32 & 0xF))) & SISR_MASK;
        break;

    case MF_TBCHK:
        if (tlb_check (R[16])) R[0] = Q_SIGN + 1;
        else R[0] = Q_SIGN;
        break;

    case MT_TBIA:
        tlb_ia (TLB_CI | TLB_CD | TLB_CA);
        break;

    case MT_TBIAP:
        tlb_ia (TLB_CI | TLB_CD);
        break;

    case MT_TBIS:
        tlb_is (R[16], TLB_CI | TLB_CD | TLB_CA);
        break;

    case MF_ESP:
        R[0] = esp;
        break;

    case MT_ESP:
        esp = R[16];
        break;

    case MF_SSP:
        R[0] = ssp;
        break;

    case MT_SSP:
        ssp = R[16];
        break;

    case MF_USP:
        R[0] = usp;
        break;

    case MT_USP:
        usp = R[16];
        break;

    case MT_TBISI:
        tlb_is (R[16], TLB_CI | TLB_CA);
        break;

    case MT_TBISD:
        tlb_is (R[16], TLB_CD | TLB_CA);
        break;

    case MF_ASTEN:
        R[0] = vms_asten & AST_MASK;
        break;

    case MF_ASTSR:
        R[0] = vms_astsr & AST_MASK;
        break;

    case MF_VTBR:
        R[0] = vms_vtbr;
        break;

    case MT_VTBR:
        vms_vtbr = R[16];
        break;

    case MT_PERFMON:
        // tbd
        break;

    case MT_DATFX:
        vms_datfx = arg32 & 1;
        val = ReadPQ (vms_hwpcb + PCBV_FLAGS);
        val = (val & ~0x8000000000000000) | (((t_uint64) vms_datfx) << 63);
        WritePQ (vms_hwpcb + PCBV_FLAGS, val);
        break;

    case MF_VIRBND:
        R[0] = vms_virbnd;
        break;

    case MT_VIRBND:
        vms_virbnd = R[16];
        break;

    case MF_SYSPTBR:
        R[0] = vms_sysptbr;
        break;

    case MT_SYSPTBR:
        vms_sysptbr = R[16];
        break;

    case OP_WTINT:
        R[0] = 0;
        break;

    case MF_WHAMI:
        R[0] = 0;
        break;

/* Non-privileged */

    case OP_BPT:
        return vms_intexc (SCB_BPT, MODE_K, vms_ipl);

    case OP_BUGCHK:
        return vms_intexc (SCB_BUG, MODE_K, vms_ipl);

    case OP_CHME:
        return vms_intexc (SCB_CHME, MOST_PRIV (MODE_E, vms_cm), vms_ipl);

    case OP_CHMK:
        return vms_intexc (SCB_CHMK, MODE_K, vms_ipl);

    case OP_CHMS:
        return vms_intexc (SCB_CHMS, MOST_PRIV (MODE_S, vms_cm), vms_ipl);

    case OP_CHMU:
        return vms_intexc (SCB_CHMU, vms_cm, vms_ipl);
        break;

    case OP_IMB:
        break;

    case OP_INSQHIL:
        R[0] = vms_insqhil ();
        break;

    case OP_INSQTIL:
        R[0] = vms_insqtil ();
        break;

    case OP_INSQHIQ:
        R[0] = vms_insqhiq ();
        break;

    case OP_INSQTIQ:
        R[0] = vms_insqtiq ();
        break;

    case OP_INSQUEL:
        R[0] = vms_insquel (0);
        break;

    case OP_INSQUEQ:
        R[0] = vms_insqueq (0);
        break;

    case OP_INSQUELD:
        R[0] = vms_insquel (1);
        break;

    case OP_INSQUEQD:
        R[0] = vms_insqueq (1);
        break;

    case OP_PROBER:
        R[0] = vms_probe (PTE_KRE);
        break;

    case OP_PROBEW:
        R[0] = vms_probe (PTE_KRE|PTE_KWE);
        break;

    case OP_RD_PS:
        R[0] = GET_PSV;
        break;

    case OP_REI:
        return vms_rei ();

    case OP_REMQHIL:
        R[0] = vms_insqhil ();
        break;

    case OP_REMQTIL:
        R[0] = vms_remqtil ();
        break;

    case OP_REMQHIQ:
        R[0] = vms_remqhiq ();
        break;

    case OP_REMQTIQ:
        R[0] = vms_remqtiq ();
        break;

    case OP_REMQUEL:
        R[0] = vms_remquel (0);
        break;

    case OP_REMQUEQ:
        R[0] = vms_remqueq (0);
        break;

    case OP_REMQUELD:
        R[0] = vms_remquel (1);
        break;

    case OP_REMQUEQD:
        R[0] = vms_remqueq (1);
        break;

    case OP_SWASTEN:
        R[0] = (vms_asten >> vms_cm) & 1;
        vms_asten = (vms_asten & ~(1u << vms_cm)) | ((arg32 & 1) << vms_cm);
        break;

    case OP_WR_PS_SW:
        vms_ps = (vms_ps & ~PSV_M_SW) | (arg32 & PSV_M_SW);
        break;

    case OP_RSCC:
        vms_scc = vms_scc + ((pcc_l - vms_last_pcc) & M32);     /* update scc */
        vms_last_pcc = pcc_l;
        R[0] = vms_scc;
        break;

    case OP_RD_UNQ:
        R[0] = vms_thread;
        break;

    case OP_WR_UNQ:
        vms_thread = R[16];
        break;

    case OP_AMOVRR:
        R[18] = vms_amovrr ();
        break;

    case OP_AMOVRM:
        R[18] = vms_amovrm ();
        break;

    case OP_INSQHILR:
        R[0] = vms_insqhilr ();
        break;

    case OP_INSQTILR:
        R[0] = vms_insqtilr ();
        break;

    case OP_INSQHIQR:
        R[0] = vms_insqhiqr ();
        break;

    case OP_INSQTIQR:
        R[0] = vms_insqtiqr ();
        break;

    case OP_REMQHILR:
        R[0] = vms_insqhilr ();
        break;

    case OP_REMQTILR:
        R[0] = vms_remqtilr ();
        break;

    case OP_REMQHIQR:
        R[0] = vms_remqhiqr ();
        break;

    case OP_REMQTIQR:
        R[0] = vms_remqtiqr ();
        break;

    case OP_GENTRAP:
        return vms_intexc (SCB_GENTRAP, MODE_K, vms_ipl);

    case OP_CLRFEN:
        fpen = 0;
        arg32 = ReadPL (vms_hwpcb + PCBV_FLAGS);
        arg32 = arg32 & ~1;
        WritePL (vms_hwpcb + PCBV_FLAGS, arg32);
        break;

    default:
        ABORT (EXC_RSVI);
        }

return SCPE_OK;
}

/* Interlocked insert instructions

        R[16]   =       entry
        R[17]   =       header

        Pictorially:

        BEFORE          AFTER INSQHI            AFTER INSQTI

        H:      A-H     H:      D-H     W       H:      A-H     W for interlock
        H+4/8:  C-H     H+4/8:  C-H             H+4/8:  D-H     W

        A:      B-A     A:      B-A             A:      B-A
        A+4/8:  H-A     A+4/8:  D-A     W       A+4/8:  H-A

        B:      C-B     B:      C-B             B:      C-B
        B+4/8:  A-B     B+4/8:  A-B             B+4/8:  A-B

        C:      H-C     C:      H-C             C:      D-C     W
        C+4/8:  B-C     C+4/8:  B-C             C+4/8:  B-C

        D:      ---     D:      A-D     W       D:      H-D     W
        D+4/8:  ---     D+4/8:  H-D     W       D+4/8:  C-D     W

        Note that the queue header, the entry to be inserted, and all
        the intermediate entries that are "touched" in any way must be
        QUAD(OCTA)WORD aligned.  In addition, the header and the entry
        must not be equal.

        Note that the offset arithmetic (+4, +8) cannot overflow 64b,
        because the entries are quad or octa aligned.
*/

t_int64 vms_insqhil (void)
{
t_uint64 h = R[16];
t_uint64 d = R[17];
t_uint64 ar, a;

if ((h == d) || ((h | d) & 07) ||                       /* h, d quad align? */
    ((SEXT_L_Q (h) & M64) != h) ||
    ((SEXT_L_Q (d) & M64) != d)) ABORT (EXC_RSVO);
ReadAccQ (d, cm_wacc);                                  /* wchk (d) */
ar = ReadQ (h);                                         /* a <- (h) */
if (ar & 06) ABORT (EXC_RSVO);                          /* a quad align? */
if (ar & 01) return -1;                                 /* busy, ret -1 */
WriteQ (h, ar | 1);                                     /* get interlock */
a = (SEXT_L_Q (ar + h)) & M64;                          /* abs addr of a */
if (Test (a, cm_wacc, NULL)) WriteQ (h, ar);            /* wtst a, rls if err */
WriteL (a + 4, (uint32) (d - a));                       /* (a+4) <- d-a, flt ok */
WriteL (d, (uint32) (a - d));                           /* (d) <- a-d */
WriteL (d + 4, (uint32) (h - d));                       /* (d+4) <- h-d */
WriteL (h, (uint32) (d - h));                           /* (h) <- d-h, rls int */
return ((ar & M32) == 0)? 0: +1;                        /* ret 0 if q was empty */
}

t_int64 vms_insqhilr (void)
{
t_uint64 h = R[16];
t_uint64 d = R[17];
t_uint64 ar, a;

ar = ReadQ (h);                                         /* a <- (h) */
if (ar & 01) return -1;                                 /* busy, ret -1 */
WriteQ (h, ar | 1);                                     /* get interlock */
a = (SEXT_L_Q (ar + h)) & M64;                          /* abs addr of a */
WriteL (a + 4, (uint32) (d - a));                       /* (a+4) <- d-a, flt ok */
WriteL (d, (uint32) (a - d));                           /* (d) <- a-d */
WriteL (d + 4, (uint32) (h - d));                       /* (d+4) <- h-d */
WriteL (h, (uint32) (d - h));                           /* (h) <- d-h, rls int */
return ((ar & M32) == 0)? 0: +1;                        /* ret 0 if q was empty */
}

t_int64 vms_insqhiq (void)
{
t_uint64 h = R[16];
t_uint64 d = R[17];
t_uint64 ar, a;

if ((h == d) || ((h | d) & 0xF)) ABORT (EXC_RSVO);      /* h, d octa align? */
ReadAccQ (d, cm_wacc);                                  /* wchk (d) */
ar = ReadQ (h);                                         /* a <- (h) */
if (ar & 0xE) ABORT (EXC_RSVO);                         /* a octa align? */
if (ar & 01) return -1;                                 /* busy, ret -1 */
WriteQ (h, ar | 1);                                     /* get interlock */
a = (ar + h) & M64;                                     /* abs addr of a */
if (Test (a, cm_wacc, NULL)) WriteQ (h, ar);            /* wtst a, rls if err */
WriteQ (a + 8, (d - a) & M64);                          /* (a+8) <- d-a, flt ok */
WriteQ (d, (a - d) & M64);                              /* (d) <- a-d */
WriteQ (d + 8, (h - d) & M64);                          /* (d+8) <- h-d */
WriteQ (h, (d - h) & M64);                              /* (h) <- d-h, rls int */
return (ar == 0)? 0: +1;                                /* ret 0 if q was empty */
}

t_int64 vms_insqhiqr (void)
{
t_uint64 h = R[16];
t_uint64 d = R[17];
t_uint64 ar, a;

ar = ReadQ (h);                                         /* a <- (h) */
if (ar & 01) return -1;                                 /* busy, ret -1 */
WriteQ (h, ar | 1);                                     /* get interlock */
a = (ar + h) & M64;                                     /* abs addr of a */
WriteQ (a + 8, (d - a) & M64);                          /* (a+8) <- d-a, flt ok */
WriteQ (d, (a - d) & M64);                              /* (d) <- a-d */
WriteQ (d + 8, (h - d) & M64);                          /* (d+8) <- h-d */
WriteQ (h, (d - h) & M64);                              /* (h) <- d-h, rls int */
return (ar == 0)? 0: +1;                                /* ret 0 if q was empty */
}

t_int64 vms_insqtil (void)
{
t_uint64 h = R[16];
t_uint64 d = R[17];
t_uint64 ar, c;

if ((h == d) || ((h | d) & 07) ||                       /* h, d quad align? */
    ((SEXT_L_Q (h) & M64) != h) ||
    ((SEXT_L_Q (d) & M64) != d)) ABORT (EXC_RSVO);
ReadAccQ (d, cm_wacc);                                  /* wchk (d) */
ar = ReadQ (h);                                         /* a <- (h) */
if ((ar & M32) == 0) return vms_insqhil ();             /* if empty, ins hd */
if (ar & 06) ABORT (EXC_RSVO);                          /* a quad align? */
if (ar & 01) return -1;                                 /* busy, ret -1 */
WriteQ (h, ar | 1);                                     /* acquire interlock */
c = ar >> 32;                                           /* c <- (h+4) */
c = (SEXT_L_Q (c + h)) & M64;                           /* abs addr of c */
if (c & 07) {                                           /* c quad aligned? */
    WriteQ (h, ar);                                     /* release interlock */
    ABORT (EXC_RSVO);                                   /* fault */
    }
if (Test (c, cm_wacc, NULL)) WriteQ (h, ar);            /* wtst c, rls if err */
WriteL (c, (uint32) (d - c));                           /* (c) <- d-c, flt ok */
WriteL (d, (uint32) (h - d));                           /* (d) <- h-d */
WriteL (d + 4, (uint32) (c - d));                       /* (d+4) <- c-d */
WriteL (h + 4, (uint32) (d - h));                       /* (h+4) <- d-h */
WriteL (h, (uint32) ar);                                /* release interlock */
return 0;                                               /* q was not empty */
}

t_int64 vms_insqtilr (void)
{
t_uint64 h = R[16];
t_uint64 d = R[17];
t_uint64 ar, c;

ar = ReadQ (h);                                         /* a <- (h) */
if ((ar & M32) == 0) return vms_insqhilr ();            /* if empty, ins hd */
if (ar & 01) return -1;                                 /* busy, ret -1 */
WriteQ (h, ar | 1);                                     /* acquire interlock */
c = ar >> 32;                                           /* c <- (h+4) */
c = (SEXT_L_Q (c + h)) & M64;                           /* abs addr of c */
WriteL (c, (uint32) (d - c));                           /* (c) <- d-c */
WriteL (d, (uint32) (h - d));                           /* (d) <- h-d */
WriteL (d + 4, (uint32) (c - d));                       /* (d+4) <- c-d */
WriteL (h + 4, (uint32) (d - h));                       /* (h+4) <- d-h */
WriteL (h, (uint32) ar);                                /* release interlock */
return 0;                                               /* q was not empty */
}

t_int64 vms_insqtiq (void)
{
t_uint64 h = R[16];
t_uint64 d = R[17];
t_uint64 ar, c;

if ((h == d) || ((h | d) & 0xF)) ABORT (EXC_RSVO);      /* h, d octa align? */
ReadAccQ (d, cm_wacc);                                  /* wchk ent */
ar = ReadQ (h);                                         /* a <- (h) */
if (ar == 0) return vms_insqhiq ();                     /* if empty, ins hd */
if (ar & 0xE) ABORT (EXC_RSVO);                         /* a octa align? */
if (ar & 01) return -1;                                 /* busy, ret -1 */
WriteQ (h, ar | 1);                                     /* acquire interlock */
c = ReadQ (h + 8);                                      /* c <- (h+8) */
c = (c + h) & M64;                                      /* abs addr of C */
if (c & 0xF) {                                          /* c octa aligned? */
    WriteQ (h, ar);                                     /* release interlock */
    ABORT (EXC_RSVO);                                   /* fault */
    }
if (Test (c, cm_wacc, NULL)) WriteQ (h, ar);            /* wtst c, rls if err */
WriteQ (c, (d - c) & M64);                              /* (c) <- d-c, flt ok */
WriteQ (d, (h - d) & M64);                              /* (d) <- h-d */
WriteQ (d + 8, (c - d) & M64);                          /* (d+8) <- c-d */
WriteQ (h + 8, (d - h) & M64);                          /* (h+8) <- d-h */
WriteQ (h, ar);                                         /* release interlock */
return 0;                                               /* q was not empty */
}

t_int64 vms_insqtiqr (void)
{
t_uint64 h = R[16];
t_uint64 d = R[17];
t_uint64 ar, c;

ar = ReadQ (h);                                         /* a <- (h) */
if (ar == 0) return vms_insqhiqr ();                    /* if empty, ins hd */
if (ar & 01) return -1;                                 /* busy, ret -1 */
WriteQ (h, ar | 1);                                     /* acquire interlock */
c = ReadQ (h + 8);                                      /* c <- (h+8) */
c = (c + h) & M64;                                      /* abs addr of C */
WriteQ (c, (d - c) & M64);                              /* (c) <- d-c */
WriteQ (d, (h - d) & M64);                              /* (d) <- h-d */
WriteQ (d + 8, (c - d) & M64);                          /* (d+8) <- c-d */
WriteQ (h + 8, (d - h) & M64);                          /* (h+8) <- d-h */
WriteQ (h, ar);                                         /* release interlock */
return 0;                                               /* q was not empty */
}

/* Interlocked remove instructions

        R[16]   =       header (hdr.aq)
        R[1]    ] =     receives destination address

        Pictorially:

        BEFORE          AFTER REMQHI            AFTER REMQTI

        H:      A-H     H:      B-H     W       H:      A-H     W for interlock
        H+4/8:  C-H     H+4/8:  C-H             H+4/8:  B-H     W

        A:      B-A     A:      B-A     R       A:      B-A
        A+4/8:  H-A     A+4/8:  H-A             A+4/8:  H-A

        B:      C-B     B:      C-B             B:      H-B     W
        B+4/8:  A-B     B+4/8:  H-B     W       B+4/8:  A-B

        C:      H-C     C:      H-C             C:      H-C
        C+4/8:  B-C     C+4/8:  B-C             C+4/8:  B-C     R

        Note that the queue header and all the  entries that are
        "touched" in any way must be QUAD(OCTA)WORD aligned.
*/

t_int64 vms_remqhil (void)
{
t_uint64 h = R[16];
t_uint64 ar, a, b;

if ((h & 07) || ((SEXT_L_Q (h) & M64) != h))            /* h quad aligned? */
    ABORT (EXC_RSVO);
ar = ReadQ (h);                                         /* ar <- (h) */
if (ar & 06) ABORT (EXC_RSVO);                          /* a quad aligned? */
if (ar & 01) return -1;                                 /* busy, ret -1 */
if ((ar & M32) == 0) return 0;                          /* queue empty? */
WriteQ (h, ar | 1);                                     /* acquire interlock */
a = (SEXT_L_Q (ar + h)) & M64;                          /* abs addr of a */
if (Test (a, cm_racc, NULL)) WriteQ (h, ar);            /* rtst a, rls if err */
b = ReadL (a);                                          /* b <- (a), flt ok */
b = (SEXT_L_Q (b + a)) & M64;                           /* abs addr of b */
if (b & 07) {                                           /* b quad aligned? */
    WriteQ (h, ar);                                     /* release interlock */
    ABORT (EXC_RSVO);                                   /* fault */
    }
if (Test (b, cm_wacc, NULL)) WriteQ (h, ar);            /* wtst b, rls if err */
WriteL (b + 4, (uint32) (h - b));                       /* (b+4) <- h-b, flt ok */
WriteL (h, (uint32) (b - h));                           /* (h) <- b-h, rls int */
R[1] = a;                                               /* address of entry */
return ((b & M32) == (h & M32))? +2: +1;                /* if b = h, q empty */
}

t_int64 vms_remqhilr (void)
{
t_uint64 h = R[16];
t_uint64 ar, a, b;

ar = ReadQ (h);                                         /* ar <- (h) */
if (ar & 01) return -1;                                 /* busy, ret -1 */
if ((ar & M32) == 0) return 0;                          /* queue empty? */
WriteQ (h, ar | 1);                                     /* acquire interlock */
a = (SEXT_L_Q (ar + h)) & M64;                          /* abs addr of a */
b = ReadL (a);                                          /* b <- (a), flt ok */
b = (SEXT_L_Q (b + a)) & M64;                           /* abs addr of b */
WriteL (b + 4, (uint32) (h - b));                       /* (b+4) <- h-b, flt ok */
WriteL (h, (uint32) (b - h));                           /* (h) <- b-h, rls int */
R[1] = a;                                               /* address of entry */
return ((b & M32) == (h & M32))? +2: +1;                /* if b = h, q empty */
}

t_int64 vms_remqhiq (void)
{
t_uint64 h = R[16];
t_uint64 ar, a, b;

if (h & 0xF) ABORT (EXC_RSVO);                          /* h octa aligned? */
ar = ReadQ (h);                                         /* ar <- (h) */
if (ar & 0xE) ABORT (EXC_RSVO);                         /* a octa aligned? */
if (ar & 01) return -1;                                 /* busy, ret -1 */
if (ar == 0) return 0;                                  /* queue empty? */
WriteQ (h, ar | 1);                                     /* acquire interlock */
a = (ar + h) & M64;                                     /* abs addr of a */
if (Test (a, cm_racc, NULL)) WriteQ (h, ar);            /* rtst a, rls if err */
b = ReadQ (a);                                          /* b <- (a), flt ok */
b = (b + a) & M64;                                      /* abs addr of b */
if (b & 0xF) {                                          /* b octa aligned? */
    WriteQ (h, ar);                                     /* release interlock */
    ABORT (EXC_RSVO);                                   /* fault */
    }
if (Test (b, cm_wacc, NULL)) WriteQ (h, ar);            /* wtst b, rls if err */
WriteQ (b + 8, (h - b) & M64);                          /* (b+8) <- h-b, flt ok */
WriteQ (h, (b - h) & M64);                              /* (h) <- b-h, rls int */
R[1] = a;                                               /* address of entry */
return (b == h)? +2: +1;                                /* if b = h, q empty */
}

t_int64 vms_remqhiqr (void)
{
t_uint64 h = R[16];
t_uint64 ar, a, b;

ar = ReadQ (h);                                         /* ar <- (h) */
if (ar & 01) return -1;                                 /* busy, ret -1 */
if (ar == 0) return 0;                                  /* queue empty? */
WriteQ (h, ar | 1);                                     /* acquire interlock */
a = (ar + h) & M64;                                     /* abs addr of a */
b = ReadQ (a);                                          /* b <- (a) */
b = (b + a) & M64;                                      /* abs addr of b */
WriteQ (b + 8, (h - b) & M64);                          /* (b+8) <- h-b, flt ok */
WriteQ (h, (b - h) & M64);                              /* (h) <- b-h, rls int */
R[1] = a;                                               /* address of entry */
return (b == h)? +2: +1;                                /* if b = h, q empty */
}

t_int64 vms_remqtil (void)
{
t_uint64 h = R[16];
t_uint64 ar, b, c;

if ((h & 07) || ((SEXT_L_Q (h) & M64) != h))            /* h quad aligned? */
    ABORT (EXC_RSVO);
ar = ReadQ (h);                                         /* a <- (h) */
if (ar & 06) ABORT (EXC_RSVO);                          /* a quad aligned? */
if (ar & 01) return -1;                                 /* busy, return - 1*/
if ((ar & M32) == 0) return 0;                          /* empty, return 0 */
WriteQ (h, ar | 1);                                     /* acquire interlock */
c = ar >> 32;                                           /* c <- (h+4) */
if (c & 07) {                                           /* c quad aligned? */
    WriteQ (h, ar);                                     /* release interlock */
    ABORT (EXC_RSVO);                                   /* fault */
    }
if ((ar & M32) == (c & M32)) {                          /* single entry? */
    WriteQ (h, ar);                                     /* release interlock */
    return vms_remqhil ();                              /* treat as remqhil */
    }
c = (SEXT_L_Q (c + h)) & M64;                           /* abs addr of c */
if (Test (c + 4, cm_racc, NULL)) WriteQ (h, ar);        /* rtst c+4, rls if err */
b = ReadL (c + 4);                                      /* b <- (c+4), flt ok */
b = (SEXT_L_Q (b + c)) & M64;                           /* abs addr of b */             
if (b & 07) {                                           /* b quad aligned? */
    WriteQ (h, ar);                                     /* release interlock */
    ABORT (EXC_RSVO);                                   /* fault */
    }
if (Test (b, cm_wacc, NULL)) WriteQ (h, ar);            /* wtst b, rls if err */
WriteL (b, (uint32) (h - b));                           /* (b) <- h-b, flt ok */
WriteL (h + 4, (uint32) (b - h));                       /* (h+4) <- b-h */
WriteL (h, (uint32) ar);                                /* release interlock */
R[1] = c;                                               /* store result */
return +1;                                              /* q can't be empty */
}

t_int64 vms_remqtilr (void)
{
t_uint64 h = R[16];
t_uint64 ar, b, c;

ar = ReadQ (h);                                         /* a <- (h) */
if (ar & 01) return -1;                                 /* busy, return - 1*/
if ((ar & M32) == 0) return 0;                          /* emtpy, return 0 */
WriteQ (h, ar | 1);                                     /* acquire interlock */
c = ar >> 32;                                           /* c <- (h+4) */
if ((ar & M32) == (c & M32)) {                          /* single entry? */
    WriteQ (h, ar);                                     /* release interlock */
    return vms_remqhilr ();                             /* treat as remqhil */
    }
c = (SEXT_L_Q (c + h)) & M64;                           /* abs addr of c */
b = ReadL (c + 4);                                      /* b <- (c+4) */
b = (SEXT_L_Q (b) + c) & M64;                           /* abs addr of b */             
WriteL (b, (uint32) (h - b));                           /* (b) <- h-b */
WriteL (h + 4, (uint32) (b - h));                       /* (h+4) <- b-h */
WriteL (h, (uint32) ar);                                /* release interlock */
R[1] = c;                                               /* store result */
return +1;                                              /* q can't be empty */
}

t_int64 vms_remqtiq (void)
{
t_uint64 h = R[16];
t_uint64 ar, b, c;

if (h & 0xF) ABORT (EXC_RSVO);                          /* h octa aligned? */
ar = ReadQ (h);                                         /* a <- (h) */
if (ar & 0xE) ABORT (EXC_RSVO);                         /* a quad aligned? */
if (ar & 01) return -1;                                 /* busy, return - 1*/
if (ar == 0) return 0;                                  /* emtpy, return 0 */
WriteQ (h, ar | 1);                                     /* acquire interlock */
c = ReadQ (h + 8);                                      /* c <- (h+8) */
if (c & 0xF) {                                          /* c octa aligned? */
    WriteQ (h, ar);                                     /* release interlock */
    ABORT (EXC_RSVO);                                   /* fault */
    }
if (ar == c) {                                          /* single entry? */
    WriteQ (h, ar);                                     /* release interlock */
    return vms_remqhiq ();                              /* treat as remqhil */
    }
c = (c + h) & M64;                                      /* abs addr of c */
if (Test (c + 8, cm_racc, NULL)) WriteQ (h, ar);        /* rtst c+8, rls if err */
b = ReadQ (c + 8);                                      /* b <- (c+8), flt ok */
b = (b + c) & M64;                                      /* abs addr of b */             
if (b & 0xF) {                                          /* b octa aligned? */
    WriteQ (h, ar);                                     /* release interlock */
    ABORT (EXC_RSVO);                                   /* fault */
    }
if (Test (b, cm_wacc, NULL)) WriteQ (h, ar);            /* wtst b, rls if err */
WriteQ (b, (h - b) & M64);                              /* (b) <- h-b, flt ok */
WriteQ (h + 8, (b - h) & M64);                          /* (h+8) <- b-h */
WriteQ (h, ar);                                         /* release interlock */
R[1] = c;                                               /* store result */
return +1;                                              /* q can't be empty */
}

t_int64 vms_remqtiqr (void)
{
t_uint64 h = R[16];
t_uint64 ar, b, c;

ar = ReadQ (h);                                         /* a <- (h) */
if (ar & 01) return -1;                                 /* busy, return - 1*/
if (ar == 0) return 0;                                  /* emtpy, return 0 */
WriteQ (h, ar | 1);                                     /* acquire interlock */
c = ReadQ (h + 8);                                      /* c <- (h+8) */
if (ar == c) {                                          /* single entry? */
    WriteQ (h, ar);                                     /* release interlock */
    return vms_remqhiq ();                              /* treat as remqhil */
    }
c = (c + h) & M64;                                      /* abs addr of c */
b = ReadQ (c + 8);                                      /* b <- (c+8) */
b = (b + c) & M64;                                      /* abs addr of b */             
WriteQ (b, (h - b) & M64);                              /* (b) <- h-b */
WriteQ (h + 8, (b - h) & M64);                          /* (h+8) <- b-h */
WriteQ (h, ar);                                         /* release interlock */
R[1] = c;                                               /* store result */
return +1;                                              /* q can't be empty */
}

/* INSQUE

        R[16]   =       predecessor address
        R[17]   =       entry address

   All writes must be checked before any writes are done.

   Pictorially:

        BEFORE                  AFTER

        P:      S               P:      E       W
        P+4/8:  (n/a)           P+4/8:  (n/a)

        E:      ---             E:      S       W
        E+4/8:  ---             E+4/8:  P       W

        S:      (n/a)           S:      (n/a)
        S+4/8:  P               S+4/8:  E       W

   For longword queues, operands can be misaligned.
   Quadword queues must be octaword aligned, and the
   address addition cannot overflow 64b.
   Note that WriteUna masks data to its proper length.
*/

t_int64 vms_insquel (uint32 defer)
{
t_uint64 p = SEXT_L_Q (R[16]) & M64;
t_uint64 e = SEXT_L_Q (R[17]) & M64;
t_uint64 s;

if (defer) {                                            /* defer? */
    p = ReadUna (p, L_LONG, cm_racc);                   /* get address */
    p = SEXT_L_Q (p) & M64;                             /* make 64b */
    }
s = ReadUna (p, L_LONG, cm_macc);                       /* s <- (p), wchk */
s = SEXT_L_Q (s) & M64;                                 /* make 64b */
ReadUna ((s + 4) & M64, L_LONG, cm_wacc);               /* wchk s+4 */
ReadUna ((e + 4) & M64, L_LONG, cm_wacc);               /* wchk e+4 */
WriteUna (e, s, L_LONG, cm_wacc);                       /* (e) <- s, last unchecked */
WriteUna ((e + 4) & M64, p, L_LONG, cm_wacc);           /* (e+4) <- p */
WriteUna ((s + 4) & M64, e, L_LONG, cm_wacc);           /* (s+4) <- ent */
WriteUna (p, e, L_LONG, cm_wacc);                       /* (p) <- e */
return (((s & M32) == (p & M32))? +1: 0);               /* return status */
}

t_int64 vms_insqueq (uint32 defer)
{
t_uint64 p = R[16];
t_uint64 e = R[17];
t_uint64 s;

if (defer) {                                            /* defer? */
    if (p & 07) ABORT (EXC_RSVO);
    p = ReadQ (p);
    }
if ((e | p) & 0xF) ABORT (EXC_RSVO);                    /* p, e octa aligned? */
s = ReadAccQ (p, cm_macc);                              /* s <- (p), wchk */
if (s & 0xF) ABORT (EXC_RSVO);                          /* s octa aligned? */
ReadAccQ (s + 8, cm_wacc);                              /* wchk s+8 */
ReadAccQ (e + 8, cm_wacc);                              /* wchk e+8 */
WriteQ (e, s);                                          /* (e) <- s */
WriteQ (e + 8, p);                                      /* (e+8) <- p */
WriteQ (s + 8, e);                                      /* (s+8) <- ent */
WriteQ (p, e);                                          /* (p) <- e */
return ((s == p)? +1: 0);                               /* return status */
}

/* REMQUE

        R[16]   =       entry address

   All writes must be checked before any writes are done.

   Pictorially:

        BEFORE                  AFTER

        P:      E               P:      S       W
        P+4/8:  (n/a)           P+4/8:  (n/a)

        E:      S       W       E:      S
        E+4/8:  P       W       E+4/8:  P

        S:      (n/a)           S:      (n/a)
        S+4/8:  E       W       S+4/8:  P

*/

t_int64 vms_remquel (uint32 defer)
{
t_uint64 e = SEXT_L_Q (R[16]) & M64;
t_uint64 s, p;

if (defer) {                                            /* defer? */
    e = ReadUna (e, L_LONG, cm_racc);                   /* get address */
    e = SEXT_L_Q (e) & M64;                             /* make 64b */
    }
s = ReadUna (e, L_LONG, cm_racc);                       /* s <- (e) */
p = ReadUna ((e + 4) & M64, L_LONG, cm_racc);           /* p <- (e+4) */
s = SEXT_L_Q (s) & M64;
p = SEXT_L_Q (p) & M64;
if (e == p) return -1;                                  /* queue empty? */
ReadUna ((s + 4) & M64, L_LONG, cm_wacc);               /* wchk (s+4) */
WriteUna (p, s, L_LONG, cm_wacc);                       /* (p) <- s */
WriteUna ((s + 4) & M64, p, L_LONG, cm_wacc);           /* (s+4) <- p */
return ((s == p)? 0: +1); 
}

t_int64 vms_remqueq (uint32 defer)
{
t_uint64 e = R[16];
t_uint64 s, p;

if (defer) {                                            /* defer? */
    if (e & 07) ABORT (EXC_RSVO);
    e = ReadQ (e);
    }
if (e & 0xF) ABORT (EXC_RSVO);                          /* e octa aligned? */
s = ReadQ (e);                                          /* s <- (e) */
p = ReadQ (e + 8);                                      /* p <- (e+8) */
if ((s | p) & 0xF) ABORT (EXC_RSVO);                    /* s, p octa aligned? */
if (e == p) return -1;                                  /* queue empty? */
ReadAccQ (s + 8, cm_wacc);                              /* wchk (s+8) */
WriteQ (p, s);                                          /* (p) <- s */
WriteQ (s + 8, p);                                      /* (s+8) <- p */
return ((s == p)? 0: +1); 
}

/* Probe */

uint32 vms_probe (uint32 acc)
{
uint32 pm = ((uint32) R[18]) & 3;

if (pm <= vms_cm) pm = vms_cm;                          /* least privileged */
acc = (acc << pm) | PTE_V;                              /* access test - no FOR/W */
if (Test (R[16], acc, NULL)) return 0;                  /* test start */
if (Test ((R[16] + R[17]) & M64, acc, NULL)) return 0;  /* test end */
return 1;
}

/* VMS TIE support instructions */

uint32 vms_amovrr (void)
{
uint32 lnt1 = ((uint32) R[18]) & 3;
uint32 lnt2 = ((uint32) R[21]) & 3;

if (vax_flag == 0) return 0;                            /* stop if !vax_flag */
vax_flag = 0;                                           /* clear vax_flag */
ReadUna (R[17], lnt_map[lnt1], cm_wacc);                /* verify writes */
ReadUna (R[20], lnt_map[lnt2], cm_wacc);
WriteUna (R[17], R[16], lnt_map[lnt1], cm_wacc);        /* do both writes */
WriteUna (R[20], R[21], lnt_map[lnt2], cm_wacc);        /* WriteUna masks data */
return 1;
}

uint32 vms_amovrm (void)
{
t_uint64 va, va1;
uint32 lnt1 = ((uint32) R[18]) & 3;
uint32 lnt2 = ((uint32) R[21]) & 0x3F;
uint32 i, dat;

if (vax_flag == 0) return 0;                            /* stop if !vax_flag */
vax_flag = 0;                                           /* clear vax_flag */
if (lnt2 && ((R[19] | R[20]) & 3)) ABORT (EXC_RSVO);    /* lw aligned? */
ReadUna (R[17], lnt_map[lnt1], cm_wacc);                /* verify first write */
if (lnt2) {                                             /* if second length */
    va = (R[19] + (lnt2 << 2) - 4) & M64;
    va1 = (R[20] + (lnt2 << 2) - 4) & M64;
    ReadL (R[19]);                                      /* verify source */
    ReadL (va);
    ReadAccL (R[20], cm_wacc);                          /* verify destination */
    ReadAccL (va1, cm_wacc);
    }
WriteUna (R[17], R[16], lnt_map[lnt1], cm_wacc);        /* do first write */
for (i = 0, va = R[19], va1 = R[20]; i < lnt2; i++) {   /* move data */
    dat = ReadL (va);
    WriteL (va1, dat);
    va = (va + 4) & M64;
    va1 = (va1 + 4) & M64;
    }
return 1;
}

/* Swap privileged context */

void vms_swpctx (void)
{
t_uint64 val;
uint32 tmp;

if (R[16] & 0x7F) ABORT (EXC_RSVO);                     /* must be 128B aligned */
WritePQ (vms_hwpcb + 0, SP);                            /* save stack ptrs */
WritePQ (vms_hwpcb + 8, esp);
WritePQ (vms_hwpcb + 16, ssp);
WritePQ (vms_hwpcb + 24, usp);
WritePQ (vms_hwpcb + 48, (vms_astsr << 4) | vms_asten); /* save AST */
WritePQ (vms_hwpcb + 64, (pcc_h + pcc_l) & M32);        /* save PCC */
WritePQ (vms_hwpcb + 72, vms_thread);                   /* save UNIQUE */
vms_hwpcb = R[16];                                      /* new PCB */
SP = ksp = ReadPQ (vms_hwpcb + 0);                      /* read stack ptrs */
esp = ReadPQ (vms_hwpcb + 8);
ssp = ReadPQ (vms_hwpcb + 16);
usp = ReadPQ (vms_hwpcb + 24);
val = ReadPQ (vms_hwpcb + 32) << VA_N_OFF;              /* read PTBR */
if (val != vms_ptbr) tlb_ia (TLB_CI | TLB_CD);          /* if changed, zap TLB */
vms_ptbr = val;
tmp = ReadPL (vms_hwpcb + 40) & M16;                    /* read ASN */
itlb_set_asn (tmp);
dtlb_set_asn (tmp);
tmp = ReadPL (vms_hwpcb + 48);                          /* read AST */
vms_astsr = (tmp >> 4) & AST_MASK;                      /* separate ASTSR, ASTEN */
vms_asten = tmp & AST_MASK;
val = ReadPQ (vms_hwpcb + PCBV_FLAGS);                  /* read flags */
fpen = ((uint32) val) & 1;                              /* set FEN */
vms_datfx = ((uint32) (val >> 63)) & 1;                 /* set DATFX */
tmp = ReadL (vms_hwpcb + 64);
pcc_h = (tmp - pcc_l) & M32;
vms_thread = ReadPQ (vms_hwpcb + 72);                   /* read UNIQUE */
return;
}

/* VMS interrupt or exception

   Inputs:
        vec     =       SCB vector
        newmode =       new mode (usually kernel)
        newipl  =       new IPL
   Outputs:
        reason  =       possible processor halt
*/

t_stat vms_intexc (uint32 vec, uint32 newmode, uint32 newipl)
{
t_uint64 pa = (vms_scbb + vec) & ~0xF;                  /* vector */
t_uint64 sav_ps = GET_PSV;                              /* old PS */
uint32 wacc = ACC_W (newmode);
uint32 exc;

vms_stkp[vms_cm] = SP;                                  /* save SP */
SP = vms_stkp[newmode];                                 /* load new SP */
sav_ps = sav_ps | ((SP & PSV_M_SPA) << PSV_V_SPA);      /* save SP align */
SP = SP & ~PSV_M_SPA;                                   /* align SP */
SP = (SP - VMS_L_STKF) & M64;
if (exc = Test (SP, wacc, NULL)) {                      /* check writes */
    if (newmode == MODE_K) return STOP_KSNV;            /* error? stop if kernel */
    ABORT1 (SP, exc + EXC_W);                           /* else, force fault */
    }
if (exc = Test (SP + VMS_L_STKF - 8, wacc, NULL)) {
    if (newmode == MODE_K) return STOP_KSNV;
    ABORT1 (SP + VMS_L_STKF - 8, exc + EXC_W);
    }
vms_cm = mmu_set_cm (newmode);                          /* switch mode */
WriteQ (SP, R[2]);                                      /* save R2-R7 */
WriteQ (SP + 8, R[3]);
WriteQ (SP + 16, R[4]);
WriteQ (SP + 24, R[5]);
WriteQ (SP + 32, R[6]);
WriteQ (SP + 40, R[7]);
WriteQ (SP + 48, PC);                                   /* save PC */
WriteQ (SP + 56, sav_ps);                               /* save PS */
PC = R[2] = ReadPQ (pa);                                /* set new PC */
R[3] = ReadPQ (pa + 8);                                 /* set argument */
vms_ipl = newipl;                                       /* change IPL */
vms_ps = vms_ps & ~PSV_M_SW;
return SCPE_OK;
}

/* Memory management fault */

t_stat vms_mm_intexc (uint32 vec, t_uint64 par2)
{
t_stat r;

r = vms_intexc (vec, MODE_K, vms_ipl);                  /* take exception */
R[4] = p1;                                              /* R[4] = va */
R[5] = par2;                                            /* R[5] = MME */
tlb_is (p1, TLB_CI | TLB_CD);                           /* zap TLB entry */
return r;
}

/* Return from exception of interrupt */

t_stat vms_rei (void)
{
t_uint64 t1, t2, t3, t4, t5, t6, t7, t8;
uint32 newmode;

if (SP & PSV_M_SPA) ABORT (EXC_RSVO);                   /* check alignment */
if (vms_cm == MODE_K) {                                 /* in kernel mode? */
    if (Test (SP, cm_racc, NULL)) return STOP_KSNV;     /* must be accessible */
    if (Test (SP + VMS_L_STKF - 8, cm_racc, NULL)) return STOP_KSNV;
    }
t1 = ReadQ (SP);                                        /* pop stack */
t2 = ReadQ (SP + 8);
t3 = ReadQ (SP + 16);
t4 = ReadQ (SP + 24);
t5 = ReadQ (SP + 32);
t6 = ReadQ (SP + 40);
t7 = ReadQ (SP + 48);
t8 = ReadQ (SP + 56);
newmode = (((uint32) t8) >> PSV_V_CM) && PSV_M_CM;      /* get new mode */
if ((vms_cm != MODE_K) &&                               /* not kernel? check new PS */
    ((newmode < vms_cm) || (t8 & PSV_MBZ))) ABORT (EXC_RSVO);
SP = (SP + VMS_L_STKF) | ((t8 >> PSV_V_SPA) & PSV_M_SPA);
vms_stkp[vms_cm] = SP;                                  /* save SP */
SP = vms_stkp[newmode];                                 /* load new SP */
R[2] = t1;                                              /* restore R2-R7 */
R[3] = t2;
R[4] = t3;
R[5] = t4;
R[6] = t5;
R[7] = t6;
PC = t7 & ~3;                                           /* restore PC */
vms_ps = ((uint32) t8) & PSV_MASK;                      /* restore PS */
vms_cm = mmu_set_cm (newmode);                          /* switch modes */
vms_ipl = (((uint32) t8) >> PSV_V_IPL) & PSV_M_IPL;     /* new IPL */
vax_flag = 0;                                           /* clear vax, lock flags */
lock_flag = 0;
return SCPE_OK;
}

/* Unaligned read virtual - for VMS PALcode only

   Inputs:
        va      =       virtual address
        lnt     =       length code (BWLQ)
        acc     =       access code (includes FOR, FOW)
   Output:
        returned data, right justified
*/

t_uint64 ReadUna (t_uint64 va, uint32 lnt, uint32 acc)
{
t_uint64 pa, pa1, wl, wh;
uint32 exc, bo, sc;

if (exc = Test (va, acc, &pa))                          /* test, translate */
    ABORT1 (va, exc + EXC_R);
if ((pa & (lnt - 1)) == 0) {                            /* aligned? */
    if (lnt == L_QUAD) return ReadPQ (pa);              /* quad? */
    if (lnt == L_LONG) return ReadPL (pa);              /* long? */
    if (lnt == L_WORD) return ReadPW (pa);              /* word? */
    return ReadPB (pa);                                 /* byte */
    }
if ((VA_GETOFF (va) + lnt) > VA_PAGSIZE) {              /* cross page? */
    if (exc = Test (va + 8, acc, &pa1))                 /* test, translate */
        ABORT1 (va + 8, exc + EXC_R);
    }
else pa1 = (pa + 8) & PA_MASK;                          /* not cross page */
bo = ((uint32) pa) & 7;                                 /* byte in qw */
sc = bo << 3;                                           /* shift count */
wl = ReadPQ (pa);                                       /* get low qw */
if (lnt == L_QUAD) {                                    /* qw unaligned? */
    wh = ReadPQ (pa1);                                  /* get high qw */
    return ((((wl >> sc) & (((t_uint64) M64) >> sc)) |
        (wh << (64 - sc))) & M64);                      /* extract data */
    }
if (lnt == L_LONG) {                                    /* lw unaligned? */
    if (bo <= 4) return ((wl >> sc) & M32);             /* all in one qw? */
    wh = ReadPQ (pa1);                                  /* get high qw */
    return ((((wl >> sc) & (M32 >> (sc - 32))) |
       (wh << (64 - sc))) & M32);
    }
if (bo < 7) return ((wl >> sc) & M16);                  /* wd, all in one qw? */
wh = ReadPQ (pa1);                                      /* get hi qw, extract */
return (((wl >> 56) & 0xFF) | ((wh & 0xFF) << 8));
}

/* Unaligned write virtual - for VMS PALcode only

   Inputs:
        va      =       virtual address
        val     =       data to be written, right justified in 64b
        lnt     =       length code (BWLQ)
        acc     =       access code (includes FOW)
   Output:
        none
*/

void WriteUna (t_uint64 va, t_uint64 val, uint32 lnt, uint32 acc)
{
t_uint64 pa, pa1, wl, wh, mask;
uint32 exc, bo, sc;

if (exc = Test (va, acc, &pa))                          /* test, translate */
    ABORT1 (va, exc + EXC_W);
if ((pa & (lnt - 1)) == 0) {                            /* aligned? */
    if (lnt == L_QUAD) WritePQ (pa, val);               /* quad? */
    else if (lnt == L_LONG) WritePL (pa, (uint32) val); /* long? */
    else if (lnt == L_WORD) WritePW (pa, (uint32) val); /* word? */
    else WritePB (pa, (uint32) val);                    /* byte */
    return;
    }
if ((VA_GETOFF (va) + lnt) > VA_PAGSIZE) {              /* cross page? */
    if (exc = Test (va + 8, acc, &pa1))                 /* test, translate */
        ABORT1 (va + 8, exc + EXC_W);
    }
else pa1 = (pa + 8) & PA_MASK;                          /* not cross page */
bo = ((uint32) pa) & 7;                                 /* byte in qw */
sc = bo << 3;                                           /* shift count */
wl = ReadPQ (pa);                                       /* get low qw */
if (lnt == L_QUAD) {                                    /* qw unaligned? */
    val = val & M64;                                    /* mask data */
    mask = ((t_uint64) M64) << sc;                      /* low qw mask */
    wl = (wl & ~mask) | ((val << sc) & mask);           /* insert low */
    wh = ReadPQ (pa1);                                  /* hi qw */
    mask = ((t_uint64) M64) >> (64 - sc);               /* hi qw mask */
    wh = (wh & ~mask) | ((val >> (64 - sc)) & mask);
    WritePQ (pa, wl);                                   /* write low */
    WritePQ (pa, wh);                                   /* write high */
    }
else if (lnt == L_LONG) {                               /* lw unaligned? */
    val = val & M32;
    mask = ((t_uint64) M32) << sc;                      /* low qw mask */
    wl = (wl & ~mask) | (val << sc);                    /* insert low */
    WritePQ (pa, wl);                                   /* write low */
    if (bo >= 4) {                                      /* 2nd qw? */
        wh = ReadPQ (pa1);                              /* read hi qw */
        mask = ((t_uint64) M32) >> (sc - 32);           /* hi qw mask */
        wh = (wh & ~mask) | (val >> (sc - 32));         /* insert high */
        WritePQ (pa1, wh);                              /* write hi */
        }
    }
else {
    val = val & M16;                                    /* mask data */
    mask = ((t_uint64) M16) << sc;                      /* word, low qw mask */
    wl = (wl & ~mask) | ((val & M16) << sc);            /* insert low */
    WritePQ (pa, wl);                                   /* write low */
    if (bo >= 7) {                                      /* 2nd qw? */
        wh = ReadPQ (pa1);                              /* read hi */
        mask = 0xFF;                                    /* hi qw mask */
        wh = (wh & ~mask) | (val >> 8);                 /* insert high */
        WritePQ (pa1, wh);                              /* write hi */
        }
    }
return;
}

/* Test the accessibility of an address (VMS and UNIX PALcode only)

   - In VMS, superpage is always 0
   - In Unix, current mode is always kernel
   - Hence, superpages are always accessible */

uint32 Test (t_uint64 va, uint32 acc, t_uint64 *pa)
{
uint32 va_sext = VA_GETSEXT (va);
uint32 vpn = VA_GETVPN (va);
t_uint64 pte;
uint32 exc;
TLBENT *tlbp;

if (!dmapen) {                                          /* mapping off? */
    if (pa) *pa = va & PA_MASK;                         /* pa = va */
    return 0;
    }
if ((va_sext != 0) && (va_sext != VA_M_SEXT))           /* invalid virt addr? */
    return EXC_BVA;
if ((mmu_dspage & SPEN_43) && (VPN_GETSP43 (vpn) == 2)) {
    if (pa) *pa = va & SP43_MASK;                       /* 43b superpage? */
    return 0;
    }
if ((mmu_dspage & SPEN_32) && (VPN_GETSP32 (vpn) == 0x1FFE)) {
    if (pa) *pa = va & SP32_MASK;                       /* 32b superpage? */
    return 0;
    }
if (!(tlbp = dtlb_lookup (vpn))) {                      /* lookup vpn; miss? */
    if (exc = pal_find_pte (vpn, &pte)) return exc;     /* fetch pte; error? */
    tlbp = dtlb_load (vpn, pte);                        /* load new entry */
    }
if (acc & ~tlbp->pte) return mm_exc (acc & ~tlbp->pte); /* check access */
if (pa) *pa = PHYS_ADDR (tlbp->pfn, va);                /* return phys addr */
return 0;                                               /* ok */
}

/* TLB check - VMS PALcode only */

uint32 tlb_check (t_uint64 va)
{
uint32 va_sext = VA_GETSEXT (va);
uint32 vpn = VA_GETVPN (va);

if ((va_sext != 0) && (va_sext != VA_M_SEXT)) return 0;
if (itlb_lookup (vpn)) return 1;
if (dtlb_lookup (vpn)) return 1;
return 0;
}

/* VMS 3-level PTE lookup

   Inputs:
        vpn     =       virtual page number (30b, sext)
        *pte    =       pointer to pte to be returned
   Output:
        status  =       0 for successful fill
                        EXC_ACV for ACV on intermediate level
                        EXC_TNV for TNV on intermediate level
*/

uint32 pal_find_pte_vms (uint32 vpn, t_uint64 *l3pte)
{
t_uint64 vptea, l1ptea, l2ptea, l3ptea, l1pte, l2pte;
uint32 vpte_vpn;
TLBENT *vpte_p;

vptea = vms_vtbr | (((t_uint64) (vpn & VA_M_VPN)) << 3);/* try virtual lookup */
vpte_vpn = VA_GETVPN (vptea);                           /* get vpte vpn */
vpte_p = dtlb_lookup (vpte_vpn);                        /* get vpte tlb ptr */
if ((vpte_p->tag == vpte_vpn) &&                        /* TLB hit? */
    ((vpte_p->pte & (PTE_KRE|PTE_V)) == (PTE_KRE|PTE_V)))
        l3ptea = vpte_p->pfn | VA_GETOFF (vptea);
else {
    l1ptea = vms_ptbr + VPN_GETLVL1 (vpn);
    l1pte = ReadPQ (l1ptea);
    if ((l1pte & PTE_V) == 0)
        return ((l1pte & PTE_KRE)? EXC_TNV: EXC_ACV);
    l2ptea = (l1pte & PFN_MASK) >> (PTE_V_PFN - VA_N_OFF);
    l2ptea = l2ptea + VPN_GETLVL2 (vpn);
    l2pte = ReadPQ (l2ptea);
    if ((l2pte & PTE_V) == 0)
        return ((l2pte & PTE_KRE)? EXC_TNV: EXC_ACV);
    l3ptea = (l2pte & PFN_MASK) >> (PTE_V_PFN - VA_N_OFF);
    l3ptea = l3ptea + VPN_GETLVL3 (vpn);
    }
*l3pte = ReadPQ (l3ptea);
return 0;
}

/* VMS PALcode reset */

t_stat pal_proc_reset_vms (DEVICE *dptr)
{
mmu_ispage = mmu_dspage = 0;
vms_cm = mmu_set_cm (MODE_K);
vms_ipl = IPL_1F;
vms_ps = 0;
vms_datfx = 0;
vms_scbb = 0;
vms_prbr = 0;
vms_scc = 0;
vms_last_pcc = pcc_l;
pcc_enb = 1;
pal_eval_intr = &pal_eval_intr_vms;
pal_proc_intr = &pal_proc_intr_vms;
pal_proc_trap = &pal_proc_trap_vms;
pal_proc_excp = &pal_proc_excp_vms;
pal_proc_inst = &pal_proc_inst_vms;
pal_find_pte = &pal_find_pte_vms;
return SCPE_OK;
}
