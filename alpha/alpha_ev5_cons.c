/* alpha_ev5_cons.c - Alpha console support routines for EV5

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
*/

#include "alpha_defs.h"
#include "alpha_ev5_defs.h"

t_uint64 srm_ptbr = 1;

extern uint32 dtlb_spage;
extern uint32 pal_type;
extern uint32 ev5_mcsr;
extern t_uint64 *M;
extern t_uint64 ev5_mvptbr;
extern UNIT cpu_unit;

/* Local quadword physical read - <no> exceptions or IO space lookups */

t_stat l_ReadPQ (t_uint64 pa, t_uint64 *dat)
{
if (ADDR_IS_MEM (pa)) {
    *dat = M[pa >> 3];
    return TRUE;
    }
return FALSE;
}

/* "SRM" 3-level pte lookup

   Inputs:
        va      =       virtual address
        *pte    =       pointer to pte to be returned
   Output:
        status  =       0 for successful fill
                        EXC_ACV for ACV on intermediate level
                        EXC_TNV for TNV on intermediate level
*/

uint32 cons_find_pte_srm (t_uint64 va, t_uint64 *l3pte)
{
t_uint64 vptea, l1ptea, l2ptea, l3ptea, l1pte, l2pte;
uint32 vpte_vpn;
TLBENT *vpte_p;

vptea = FMT_MVA_VMS (va);                               /* try virt lookup */
vpte_vpn = VA_GETVPN (vptea);                           /* get vpte vpn */
vpte_p = dtlb_lookup (vpte_vpn);                        /* get vpte tlb ptr */
if (vpte_p && ((vpte_p->pte & (PTE_KRE|PTE_V)) == (PTE_KRE|PTE_V)))
    l3ptea = PHYS_ADDR (vpte_p->pfn, vptea);
else {
    uint32 vpn = VA_GETVPN (va);
    if (srm_ptbr & 1) return 1;                         /* uninitialized? */
    l1ptea = srm_ptbr + VPN_GETLVL1 (vpn);
    if (!l_ReadPQ (l1ptea, &l1pte)) return 1;
    if ((l1pte & PTE_V) == 0)
        return ((l1pte & PTE_KRE)? EXC_TNV: EXC_ACV);
    l2ptea = (l1pte & PFN_MASK) >> (PTE_V_PFN - VA_N_OFF);
    l2ptea = l2ptea + VPN_GETLVL2 (vpn);
    if (!l_ReadPQ (l2ptea, &l2pte)) return 1;
    if ((l2pte & PTE_V) == 0)
        return ((l2pte & PTE_KRE)? EXC_TNV: EXC_ACV);
    l3ptea = (l2pte & PFN_MASK) >> (PTE_V_PFN - VA_N_OFF);
    l3ptea = l3ptea + VPN_GETLVL3 (vpn);
    }
if (!l_ReadPQ (l3ptea, l3pte)) return 1;
return 0;
}

/* NT 2-level pte lookup

   Inputs:
        va      =       virtual address
        *pte    =       pointer to pte to be returned
   Output:
        status  =       0 for successful fill
                        EXC_ACV for ACV on intermediate level
                        EXC_TNV for TNV on intermediate level
*/

uint32 cons_find_pte_nt (t_uint64 va, t_uint64 *l3pte)
{
t_uint64 vptea, l3ptea;
uint32 vpte_vpn;
TLBENT *vpte_p;

vptea = FMT_MVA_NT (va);                                /* try virt lookup */
vpte_vpn = VA_GETVPN (vptea);                           /* get vpte vpn */
vpte_p = dtlb_lookup (vpte_vpn);                        /* get vpte tlb ptr */
if (vpte_p && ((vpte_p->pte & (PTE_KRE|PTE_V)) == (PTE_KRE|PTE_V)))
    l3ptea = PHYS_ADDR (vpte_p->pfn, vptea);
else {
    return 1;                                           /* for now */
    }
if (!l_ReadPQ (l3ptea, l3pte)) return 1;
return 0;
}

/* Translate address for console access */

t_uint64 trans_c (t_uint64 va)
{
uint32 va_sext = VA_GETSEXT (va);
uint32 vpn = VA_GETVPN (va);
TLBENT *tlbp;
t_uint64 pte64;
uint32 exc, pfn;

if ((va_sext != 0) && (va_sext != VA_M_SEXT))           /* invalid virt addr? */
    return M64;
if ((dtlb_spage & SPEN_43) && (VPN_GETSP43 (vpn) == 2))
    return (va & SP43_MASK);                            /* 43b superpage? */
if ((dtlb_spage & SPEN_32) && (VPN_GETSP32 (vpn) == 0x1FFE))
    return (va & SP32_MASK);                            /* 32b superpage? */
if ((tlbp = dtlb_lookup (vpn)))                         /* try TLB */
    return PHYS_ADDR (tlbp->pfn, va);                   /* found it */
if (ev5_mcsr & MCSR_NT) exc = cons_find_pte_nt (va, &pte64);
else exc = cons_find_pte_srm (va, &pte64);
if (exc || ((pte64 & PTE_V) == 0)) return M64;          /* check valid */
pfn = (uint32) (pte64 >> 32) & M32;
return PHYS_ADDR (pfn, va);                             /* return phys addr */
}
