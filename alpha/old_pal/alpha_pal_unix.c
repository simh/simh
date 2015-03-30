/* alpha_pal_unix.c - Alpha Unix PAL code simulator

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

   This module contains the PALcode implementation for Alpha Unix, except for
   the console, which is always done in hardware mode.

   Alpha Unix/Linux requires the following privileged state:

        ps<3:0>                 processor status
          cm<0>                   current mode - in base
          ipl<2:0>                interrupt level - in base
        ksp<63:0>               kernel stack pointer
        kgp<63:0>               kernel global pointer
        usp<63:0>               user stack pointer
        pcbb<63:0>              process control block base
        ptptr<63:0>             page table base
        vptptr<63:0>            virtual page table base
        virbnd<63:0>            virtual address boundary
        sysptbr<63:0>           system page table base register
        sysval<63:0>            processor base (sysvalue)
        unique<63:0>            thread-unique value
        entArith<63:0>          entry vector, arithmetic trap
        entIF<63:0>             entry vector, instruction
        entInt<63:0>            entry vector, interrupt
        entSys<63:0>            entry vector, system call
        entMM<63:0>             entry vector, memory management fault
        entUna<63:0>            entry vector, unaligned

   Unix maps kernel/user to the hardware's kernel/executive.  It maps the
   8 IPL's to the hardware IPL's as follows:

        0                       0
        1                       1
        2                       2
        3                       IPL_HMIN
        4                       IPL_HMIN+1
        5                       IPL_HMIN+2
        6                       IPL_HMIN+3
        7                       IPL_1F
*/

#include "alpha_defs.h"

#define GET_PSU                 (((unix_cm & PSU_M_CM) << PSU_V_CM) | \
                            ((unix_ipl & PSU_M_IPL) << PSU_V_IPL))

// kludge for debugging...
#define io_get_vec(x)           0

#define ksp                     unix_stkp[MODE_K]
#define usp                     unix_stkp[MODE_E]
#define entInt                  unix_entVec[0]
#define entArith                unix_entVec[1]
#define entMM                   unix_entVec[2]
#define entIF                   unix_entVec[3]
#define entUna                  unix_entVec[4]
#define entSys                  unix_entVec[5]
#define v0                      R[0]
#define a0                      R[16]
#define a1                      R[17]
#define a2                      R[18]
#define a3                      R[19]
#define at                      R[28]
#define gp                      R[29]

t_uint64 unix_ptptr = 0;                                /* page table base */
t_uint64 unix_vptptr = 0;                               /* virt page table base */
t_uint64 unix_virbnd = M64;                             /* virtual boundary */
t_uint64 unix_sysptbr = 0;                              /* system page table base */
t_uint64 unix_hwpcb = 0;                                /* hardware PCB */
t_uint64 unix_unique = 0;                               /* thread unique */
t_uint64 unix_sysval = 0;                               /* processor unique */
t_uint64 unix_mces = 0;                                 /* machine check err summ */
t_uint64 unix_stkp[2] = { 0 };
t_uint64 unix_entVec[6] = { 0 };
t_uint64 unix_kgp = 0;
uint32 unix_ipl = 0;
uint32 unix_cm = 0;

static const uint32 map_ipl[8] = {
 0, 1, 2, IPL_HMIN, IPL_HMIN + 1, IPL_HMIN + 2, IPL_HMIN + 3, IPL_1F
 };

extern t_uint64 R[32];
extern t_uint64 PC, trap_mask;
extern t_uint64 p1;
extern uint32 vax_flag, lock_flag;
extern uint32 fpen;
extern uint32 ir, pcc_h, pcc_l, pcc_enb;
extern uint32 cm_racc, cm_wacc;
extern uint32 mmu_ispage, mmu_dspage;
extern jmp_buf save_env;
extern uint32 int_req[IPL_HLVL];

t_stat unix_syscall (void);
t_stat unix_retsys (void);
t_stat unix_rti (void);
void unix_urti (void);
void unix_swpctx (void);
t_stat unix_intexc (t_uint64 vec, t_uint64 arg);
t_stat unix_mm_intexc (t_uint64 par1, t_uint64 par2);
t_stat pal_proc_reset_unix (DEVICE *dptr);
uint32 pal_find_pte_unix (uint32 vpn, t_uint64 *l3pte);

extern t_stat (*pal_eval_intr) (uint32 ipl);
extern t_stat (*pal_proc_excp) (uint32 type);
extern t_stat (*pal_proc_trap) (uint32 type);
extern t_stat (*pal_proc_intr) (uint32 type);
extern t_stat (*pal_proc_inst) (uint32 fnc);
extern uint32 (*pal_find_pte) (uint32 vpn, t_uint64 *pte);
extern uint32 Test (t_uint64 va, uint32 acc, t_uint64 *pa);

/* UNIXPAL data structures

   unixpal_dev  device descriptor
   unixpal_unit unit
   unixpal_reg  register list
*/

UNIT unixpal_unit = { UDATA (NULL, 0, 0) };

REG unixpal_reg[] = {
    { HRDATA (KSP, ksp, 64) },
    { HRDATA (USP, usp, 64) },
    { HRDATA (ENTARITH, entArith, 64) },
    { HRDATA (ENTIF, entIF, 64) },
    { HRDATA (ENTINT, entInt, 64) },
    { HRDATA (ENTMM, entMM, 64) },
    { HRDATA (ENTSYS, entSys, 64) },
    { HRDATA (ENTUNA, entUna, 64) },
    { HRDATA (KGP, unix_kgp, 64) },
    { HRDATA (PTPTR, unix_ptptr, 64) },
    { HRDATA (VPTPTR, unix_vptptr, 64) },
    { HRDATA (VIRBND, unix_virbnd, 64) },
    { HRDATA (SYSPTBR, unix_sysptbr, 64) },
    { HRDATA (UNIQUE, unix_unique, 64) },
    { HRDATA (SYSVAL, unix_sysval, 64) },
    { HRDATA (HWPCB, unix_hwpcb, 64) },
    { HRDATA (MCES, unix_mces, 64) },
    { HRDATA (IPL, unix_ipl, 3) },
    { HRDATA (CM, unix_cm, 0) },
    { NULL }
    };

DEVICE unixpal_dev = {
    "UNIXPAL", &unixpal_unit, unixpal_reg, NULL,
    1, 16, 1, 1, 16, 8,
    NULL, NULL, &pal_proc_reset_unix,
    NULL, NULL, NULL,
    NULL, DEV_DIS
    };

/* Unix interrupt evaluator - returns IPL of highest priority interrupt */

uint32 pal_eval_intr_unix (uint32 lvl)
{
uint32 i;
uint32 mipl = map_ipl[lvl & PSU_M_IPL];

for (i = IPL_HMAX; i >= IPL_HMIN; i--) {                /* chk hwre int */
    if (i <= mipl) return 0;                            /* at ipl? no int */
    if (int_req[i - IPL_HMIN]) return i;                /* req != 0? int */
    }
return 0;
}

/* Unix interrupt dispatch - reached from top of execute loop */

t_stat pal_proc_intr_unix (uint32 lvl)
{
t_stat r;

if (lvl > IPL_HMAX) return SCPE_IERR;                   /* above max? */
else if (lvl >= IPL_HMIN) a1 = io_get_vec (lvl);        /* hwre? get vector */
else return SCPE_IERR;                                  /* bug */
r = unix_intexc (entInt, UNIX_INT_IO);                  /* do interrupt */
if (a1 == SCB_CLOCK) a0 = UNIX_INT_CLK;
if (a1 == SCB_IPIR) a0 = UNIX_INT_IPIR;
unix_ipl = lvl;
return r;
}

/* Unix trap dispatch - reached synchronously from bottom of execute loop */

t_stat pal_proc_trap_unix (uint32 tsum)
{
t_stat r;

r = unix_intexc (entArith, tsum);                       /* arithmetic trap */
a1 = trap_mask;                                         /* set parameter */
return r;
}

/* Unix exception dispatch - reached from the ABORT handler */

t_stat pal_proc_excp_unix (uint32 abval)
{
t_stat r;

switch (abval) {

    case EXC_RSVI:                                      /* reserved instruction */
        return unix_intexc (entIF, UNIX_IF_RSVI);       /* trap */

    case EXC_RSVO:                                      /* reserved operand */
        return unix_intexc (entIF, UNIX_IF_RSVI);       /* trap */

    case EXC_ALIGN:                                     /* unaligned */
        PC = (PC - 4) & M64;                            /* back up PC */
        r = unix_intexc (entUna, PC);                   /* fault */
        a1 = I_GETOP (ir);                              /* get opcode */
        a2 = I_GETRA (ir);                              /* get ra */
        return r;

    case EXC_FPDIS:                                     /* fp disabled */
        PC = (PC - 4) & M64;                            /* backup PC */
        return unix_intexc (entIF, UNIX_IF_FDIS);       /* fault */

    case EXC_FOX+EXC_E:                                 /* FOE */
        tlb_is (p1, TLB_CI);
        return unix_mm_intexc (UNIX_MMCSR_FOE, UNIX_MME_E);

    case EXC_FOX+EXC_R:                                 /* FOR */
        PC = (PC - 4) & M64;                            /* back up PC */
        return unix_mm_intexc (UNIX_MMCSR_FOR, UNIX_MME_R);

    case EXC_FOX+EXC_W:                                 /* FOW */
        PC = (PC - 4) & M64;                            /* back up PC */
        return unix_mm_intexc (UNIX_MMCSR_FOW, UNIX_MME_W);

    case EXC_BVA+EXC_E:
    case EXC_ACV+EXC_E:                                 /* instr ACV */
        return unix_mm_intexc (UNIX_MMCSR_ACV, UNIX_MME_E);

    case EXC_BVA+EXC_R:
    case EXC_ACV+EXC_R:                                 /* data read ACV */
        PC = (PC - 4) & M64;                            /* back up PC */
        return unix_mm_intexc (UNIX_MMCSR_ACV, UNIX_MME_R);

    case EXC_BVA+EXC_W:
    case EXC_ACV+EXC_W:                                 /* data write ACV */
        PC = (PC - 4) & M64;                            /* back up PC */
        return unix_mm_intexc (UNIX_MMCSR_ACV, UNIX_MME_W);

    case EXC_TNV+EXC_E:                                 /* instr TNV */
        tlb_is (p1, TLB_CI);
        return unix_mm_intexc (UNIX_MMCSR_TNV, UNIX_MME_E);

    case EXC_TNV+EXC_R:                                 /* data read TNV */
        tlb_is (p1, TLB_CD);
        PC = (PC - 4) & M64;                            /* back up PC */
        return unix_mm_intexc (UNIX_MMCSR_TNV, UNIX_MME_R);

    case EXC_TNV+EXC_W:                                 /* data write TNV */
        tlb_is (p1, TLB_CD);
        PC = (PC - 4) & M64;                            /* back up PC */
        return unix_mm_intexc (UNIX_MMCSR_TNV, UNIX_MME_W);

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

t_stat pal_proc_inst_unix (uint32 fnc)
{
uint32 arg32 = (uint32) a0;

if ((fnc < 0x40) && (unix_cm != MODE_K)) ABORT (EXC_RSVI);
switch (fnc) {

    case OP_halt:
        return STOP_HALT;

    case OP_cflush:
    case OP_draina:
        break;

    case OP_cserve:
        //tbd
        break;

    case OP_swppal:
        v0 = 0;
        break;

    case OP_rdmces:
        v0 = unix_mces;
        break;

    case OP_wrmces:
        unix_mces = (unix_mces | (arg32 & MCES_DIS)) & ~(arg32 & MCES_W1C);
        break;

    case OP_wrvirbnd:
        unix_virbnd = a0;
        break;

    case OP_wrsysptbr:
        unix_sysptbr = a0;
        break;

    case OP_wrfen:
        fpen = arg32 & 1;
        arg32 = ReadPL (unix_hwpcb + PCBU_FLAGS);
        arg32 = (arg32 & ~1) | fpen;
        WritePL (unix_hwpcb + PCBU_FLAGS, arg32);
        break;

    case OP_wrvptptr:
        unix_vptptr = a0;
        break;

    case OP_wrasn:
        itlb_set_asn (arg32 & M16);
        dtlb_set_asn (arg32 & M16);
        WritePL (unix_hwpcb + 28, arg32 & M16);
        break;

    case OP_swpctx:
        unix_swpctx ();
        break;

    case OP_wrval:
        unix_sysval = a0;
        break;

    case OP_rdval:
        v0 = unix_sysval;
        break;

    case OP_tbi:
        switch (a0 + 2) {
        case 0:                                                 /* -2 = tbia */
            tlb_ia (TLB_CI | TLB_CD | TLB_CA);
            break;
        case 1:                                                 /* -1 = tbiap */
            tlb_ia (TLB_CI | TLB_CD);
            break;
        case 3:                                                 /* +1 = tbis */
            tlb_is (a1, TLB_CI | TLB_CD);
            break;
        case 4:                                                 /* +2 = tbisd */
            tlb_is (a1, TLB_CD);
            break;
            case 5:                                             /* +3 = tbisi */
        tlb_is (a1, TLB_CI);
            break;
        default:
            break;
            }
        break;

    case OP_wrent:
        if (a0 <= 5) unix_entVec[arg32] = a0;
        break;

    case OP_swpipl:
        v0 = unix_ipl;
        unix_ipl = arg32 & PSU_M_IPL;
        break;

    case OP_rdps:
        v0 = GET_PSU;
        break;

    case OP_wrkgp:
        unix_kgp = a0;
        break;

    case OP_wrusp:
        usp = a0;
        break;

    case OP_wrperfmon:
        // tbd
        break;

    case OP_rdusp:
        v0 = usp;
        break;

    case OP_whami:
        v0 = 0;
        break;

    case OP_retsys:
        unix_retsys ();
        break;

    case OP_wtint:
        v0 = 0;
        break;

    case OP_rti:
        unix_rti ();
        break;

/* Non-privileged */

    case OP_bpt:
        return unix_intexc (entIF, UNIX_IF_BPT);

    case OP_bugchk:
        return unix_intexc (entIF, UNIX_IF_BUG);

    case OP_syscall:
        if (unix_cm == MODE_K) {
            //tbd
            }
        return unix_syscall ();

    case OP_imb:
        break;

    case OP_urti:
        if (unix_cm == MODE_K) {
            //tbd
            }
        unix_urti ();
        break;

    case OP_rdunique:
        v0 = unix_unique;
        break;

    case OP_wrunique:
        unix_unique = a0;
        break;

    case OP_gentrap:
        return unix_intexc (entIF, UNIX_IF_GEN);

    case OP_clrfen:
        fpen = 0;
        arg32 = ReadPL (unix_hwpcb + PCBU_FLAGS);
        arg32 = arg32 & ~1;
        WritePL (unix_hwpcb + PCBU_FLAGS, arg32);
        break;

    default:
        ABORT (EXC_RSVI);
        }

return SCPE_OK;
}

/* Swap privileged context */

void unix_swpctx (void)
{
t_uint64 val;
uint32 tmp1;

WritePQ (unix_hwpcb + 0, SP);                           /* save stack ptrs */
WritePQ (unix_hwpcb + 8, usp);
tmp1 = (pcc_h + pcc_l) & M32;                           /* elapsed time */
WritePL (unix_hwpcb + 24, tmp1);                        /* save PCC */
WritePQ (unix_hwpcb + 32, unix_unique);                 /* save unique */
v0 = unix_hwpcb;                                        /* return curr PCBB */
unix_hwpcb = a0;                                        /* new PCBB */
SP = ksp = ReadPQ (unix_hwpcb + 0);                     /* read stack ptrs */
usp = ReadPQ (unix_hwpcb + 8);
val = ReadPQ (unix_hwpcb + 16) << VA_N_OFF;             /* read new PTBR */
if (val != unix_ptptr) tlb_ia (TLB_CI | TLB_CD);        /* ptbr change? zap TLB */
unix_ptptr = val;
tmp1 = ReadPL (unix_hwpcb + 24);                        /* restore PCC */
pcc_h = (tmp1 - pcc_l) & M32;
tmp1 = ReadPL (unix_hwpcb + 28) & M16;                  /* read ASN */
itlb_set_asn (tmp1);
dtlb_set_asn (tmp1);
unix_unique = ReadPQ (unix_hwpcb + 32);                 /* read unique */
fpen = ReadPL (unix_hwpcb + PCBU_FLAGS) & 1;            /* read FEN */
return;
}

/* Unix interrupt or exception - always to kernel mode

   Inputs:
        vec     =       entry vector
        arg     =       argument for a0
   Outputs:
        reason  =       possible processor halt
*/

t_stat unix_intexc (t_uint64 vec, t_uint64 arg)
{
t_uint64 sav_ps = GET_PSU;                              /* old PS */

if ((unix_cm & PSU_M_CM) != MODE_K) {                   /* not kernel? */
    usp = SP;                                           /* save SP */
    SP = ksp;                                           /* load new SP */
    unix_cm = mmu_set_cm (MODE_K);                      /* PS = 0 */
    unix_ipl = 0;
    }
SP = (SP - UNIX_L_STKF) & M64;                          /* decr stack */
if (Test (SP, cm_wacc, NULL)) return STOP_KSNV;         /* validate writes */
if (Test (SP + UNIX_L_STKF - 8, cm_wacc, NULL) < 0) return STOP_KSNV;
WriteQ (SP, sav_ps);                                    /* save PS, PC, gp */
WriteQ (SP + 8, PC);
WriteQ (SP + 16, gp);
WriteQ (SP + 24, a0);                                   /* save a0-a2 */
WriteQ (SP + 32, a1);
WriteQ (SP + 40, a2);
PC = vec;                                               /* new PC */
gp = unix_kgp;                                          /* kernel GP */
a0 = arg;                                               /* argument */
return SCPE_OK;
}

/* Memory management fault */

t_stat unix_mm_intexc (t_uint64 par1, t_uint64 par2)
{
t_stat r;

r = unix_intexc (entMM, p1);                            /* do exception */
a1 = par1;                                              /* set arguments */
a2 = par2;
tlb_is (p1, TLB_CI | TLB_CD);                           /* zap TLB entry */
return r;
}

/* System call - always user to kernel, abbreviated stack frame, no arguments */

t_stat unix_syscall (void)
{
t_uint64 sav_ps = GET_PSU;                              /* save PS */

usp = SP;                                               /* save user SP */
SP = ksp;                                               /* load kernel SP */
unix_cm = mmu_set_cm (MODE_K);                          /* PS = 0 */
unix_ipl = 0;
SP = (SP - UNIX_L_STKF) & M64;                          /* decr stack */
if (Test (SP, cm_wacc, NULL)) return STOP_KSNV;         /* validate writes */
if (Test (SP + UNIX_L_STKF - 8, cm_wacc, NULL)) return STOP_KSNV;
WriteQ (SP, sav_ps);                                    /* save PS, PC, gp */
WriteQ (SP + 8, PC);
WriteQ (SP + 16, gp);
PC = entSys;                                            /* new PC */
gp = unix_kgp;                                          /* kernel GP */
return SCPE_OK;
}

/* Return from trap or interrupt - always from kernel */

t_stat unix_rti (void)
{
t_uint64 tpc;
uint32 tps, newm;

if (Test (SP, cm_racc, NULL)) return STOP_KSNV;         /* validate reads */
if (Test (SP + UNIX_L_STKF - 8, cm_racc, NULL)) return STOP_KSNV;
tps = (uint32) ReadQ (SP);                              /* read PS, PC */
tpc = ReadQ (SP + 8);
gp = ReadQ (SP + 16);                                   /* restore gp, a0-a2 */
a0 = ReadQ (SP + 24);
a1 = ReadQ (SP + 32);
a2 = ReadQ (SP + 40);
SP = (SP + UNIX_L_STKF);                                /* incr stack */
newm = (tps >> PSU_V_CM) & PSU_M_CM;
unix_cm = mmu_set_cm (newm);                            /* new current mode */
if (newm) {                                             /* to user? */
    ksp = SP;                                           /* save kernel stack */
    SP = usp;                                           /* load user stack */
    unix_ipl = 0;                                       /* ipl = 0 */
    }
else unix_ipl = (tps >> PSU_V_IPL) & PSU_V_IPL;         /* restore ipl */
PC = tpc;                                               /* restore PC */
vax_flag = 0;                                           /* clear VAX, lock flags */
lock_flag = 0;
return SCPE_OK;
}

/* Return from system call - always from kernel to user */

t_stat unix_retsys (void)
{
t_uint64 tpc;

if (Test (SP + 8, cm_racc, NULL)) return STOP_KSNV;     /* validate reads */
if (Test (SP + 16, cm_racc, NULL)) return STOP_KSNV;
tpc = ReadQ (SP + 8);                                   /* read PC */
gp = ReadQ (SP + 16);                                   /* restore GP */
ksp = (SP + UNIX_L_STKF);                               /* update kernel stack */
SP = usp;                                               /* restore user stack */
unix_cm = mmu_set_cm (MODE_E);                          /* PS = 8 */
unix_ipl = 0;
PC = tpc;                                               /* restore PC */
vax_flag = 0;                                           /* clear VAX, lock flags */
lock_flag = 0;
return SCPE_OK;
}

/* Return from user mode trap - always from user to user */

void unix_urti (void)
{
t_uint64 tsp, tpc;
uint32 tps;

if (SP & 0x3F) ABORT (EXC_RSVO);                        /* not aligned? */
tps = ReadL (SP + 16);                                  /* read PS */
if (!(tps & PSU_CM) || (tps & PSU_IPL)) ABORT (EXC_RSVO);
at = ReadQ (SP + 0);                                    /* restore at */
tsp = ReadQ (SP + 8);                                   /* read SP, PC */
tpc = ReadQ (SP + 24);
gp = ReadQ (SP + 32);                                   /* restore gp, a0-a2 */
a0 = ReadQ (SP + 40);
a1 = ReadQ (SP + 48);
a2 = ReadQ (SP + 56);
SP = tsp;                                               /* restore SP */
PC = tpc;                                               /* restore PC */
vax_flag = 0;                                           /* clear VAX, lock flags */
lock_flag = 0;
return;
}

/* Unix 3-level PTE lookup

   Inputs:
        vpn     =       virtual page number (30b, sext)
        *pte    =       pointer to pte to be returned
   Output:
        status  =       0 for successful fill
                        EXC_ACV for ACV on intermediate level
                        EXC_TNV for TNV on intermediate level
*/

uint32 pal_find_pte_unix (uint32 vpn, t_uint64 *l3pte)
{
t_uint64 vptea, l1ptea, l2ptea, l3ptea, l1pte, l2pte;
uint32 vpte_vpn;
TLBENT *vpte_p;

vptea = unix_vptptr | (((t_uint64) (vpn & VA_M_VPN)) << 3); /* try virtual lookup */ 
vpte_vpn = VA_GETVPN (vptea);                           /* get vpte vpn */
vpte_p = dtlb_lookup (vpte_vpn);                        /* get vpte tlb ptr */
if (vpte_p && ((vpte_p->pte & (PTE_KRE|PTE_V)) == (PTE_KRE|PTE_V)))
    l3ptea = vpte_p->pfn | VA_GETOFF (vptea);
else {
    l1ptea = unix_ptptr + VPN_GETLVL1 (vpn);
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

/* Unix PALcode reset */

t_stat pal_proc_reset_unix (DEVICE *dptr)
{
mmu_ispage = mmu_dspage = SPEN_43;
unix_ipl = PSU_M_IPL;
unix_cm = mmu_set_cm (MODE_K);
pcc_enb = 1;
pal_eval_intr = &pal_eval_intr_unix;
pal_proc_intr = &pal_proc_intr_unix;
pal_proc_trap = &pal_proc_trap_unix;
pal_proc_excp = &pal_proc_excp_unix;
pal_proc_inst = &pal_proc_inst_unix;
pal_find_pte = &pal_find_pte_unix;
return SCPE_OK;
}
