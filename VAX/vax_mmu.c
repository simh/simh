/* vax_mmu.c - VAX memory management

   Copyright (c) 1998-2013, Robert M Supnik

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

   29-Nov-13    RMS     Reworked unaligned flows
   24-Oct-12    MB      Added support for KA620 virtual addressing
   21-Jul-08    RMS     Removed inlining support
   28-May-08    RMS     Inlined physical memory routines
   29-Apr-07    RMS     Added address masking for system page table reads
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   30-Sep-04    RMS     Comment and formating changes
   19-Sep-03    RMS     Fixed upper/lower case linkage problems on VMS
   01-Jun-03    RMS     Fixed compilation problem with USE_ADDR64

   This module contains the instruction simulators for

        Read            -       read virtual
        Write           -       write virtual
        ReadL(P)        -       read aligned physical longword (physical context)
        WriteL(P)       -       write aligned physical longword (physical context)
        ReadB(W)        -       read aligned physical byte (word)
        WriteB(W)       -       write aligned physical byte (word)
        Test            -       test acccess

        zap_tb          -       clear TB
        zap_tb_ent      -       clear TB entry
        chk_tb_ent      -       check TB entry
        set_map_reg     -       set up working map registers
*/

#include "vax_defs.h"
#include "vax_mmu.h"
#include <setjmp.h>

int32 d_p0br, d_p0lr;                                   /* dynamic copies */
int32 d_p1br, d_p1lr;                                   /* altered per ucode */
int32 d_sbr, d_slr;
TLBENT stlb[VA_TBSIZE], ptlb[VA_TBSIZE];
static const int32 cvtacc[16] = { 0, 0,
    TLB_ACCW (KERN)+TLB_ACCR (KERN),
    TLB_ACCR (KERN),
    TLB_ACCW (KERN)+TLB_ACCW (EXEC)+TLB_ACCW (SUPV)+TLB_ACCW (USER)+
            TLB_ACCR (KERN)+TLB_ACCR (EXEC)+TLB_ACCR (SUPV)+TLB_ACCR (USER),
    TLB_ACCW (KERN)+TLB_ACCW (EXEC)+TLB_ACCR (KERN)+TLB_ACCR (EXEC),
    TLB_ACCW (KERN)+TLB_ACCR (KERN)+TLB_ACCR (EXEC),
    TLB_ACCR (KERN)+TLB_ACCR (EXEC),
    TLB_ACCW (KERN)+TLB_ACCW (EXEC)+TLB_ACCW (SUPV)+
            TLB_ACCR (KERN)+TLB_ACCR (EXEC)+TLB_ACCR (SUPV),
    TLB_ACCW (KERN)+TLB_ACCW (EXEC)+
            TLB_ACCR (KERN)+TLB_ACCR (EXEC)+TLB_ACCR (SUPV),
    TLB_ACCW (KERN)+TLB_ACCR (KERN)+TLB_ACCR (EXEC)+TLB_ACCR (SUPV),
    TLB_ACCR (KERN)+TLB_ACCR (EXEC)+TLB_ACCR (SUPV),
    TLB_ACCW (KERN)+TLB_ACCW (EXEC)+TLB_ACCW (SUPV)+
            TLB_ACCR (KERN)+TLB_ACCR (EXEC)+TLB_ACCR (SUPV)+TLB_ACCR (USER),
    TLB_ACCW (KERN)+TLB_ACCW (EXEC)+
            TLB_ACCR (KERN)+TLB_ACCR (EXEC)+TLB_ACCR (SUPV)+TLB_ACCR (USER),
    TLB_ACCW (KERN)+
            TLB_ACCR (KERN)+TLB_ACCR (EXEC)+TLB_ACCR (SUPV)+TLB_ACCR (USER),
    TLB_ACCR (KERN)+TLB_ACCR (EXEC)+TLB_ACCR (SUPV)+TLB_ACCR (USER)
    };

t_stat tlb_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat tlb_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat tlb_reset (DEVICE *dptr);
const char *tlb_description (DEVICE *dptr);

TLBENT fill (uint32 va, int32 lnt, int32 acc, int32 *stat);
extern int32 ReadIO (uint32 pa, int32 lnt);
extern void WriteIO (uint32 pa, int32 val, int32 lnt);
extern int32 ReadReg (uint32 pa, int32 lnt);
extern void WriteReg (uint32 pa, int32 val, int32 lnt);
int32 ReadU (uint32 pa, int32 lnt);
void WriteU (uint32 pa, int32 val, int32 lnt);

/* TLB data structures

   tlb_dev      pager device descriptor
   tlb_unit     pager units
   pager_reg    pager register list
*/

UNIT tlb_unit[] = {
    { UDATA (NULL, UNIT_FIX, VA_TBSIZE * 2) },
    { UDATA (NULL, UNIT_FIX, VA_TBSIZE * 2) }
    };

REG tlb_reg[] = {
    { NULL }
    };

DEVICE tlb_dev = {
    "TLB", tlb_unit, tlb_reg, NULL,
    2, 16, VA_N_TBI * 2, 1, 16, 32,
    &tlb_ex, &tlb_dep, &tlb_reset,
    NULL, NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, 
    &tlb_description
    };


/* TLB fill

   This routine fills the TLB after a tag or access mismatch, or
   on a write if pte<m> = 0.  It fills the TLB and returns the
   pte to the caller.  On an error, it aborts directly to the
   fault handler in the CPU.

   If called from map (VAX PROBEx), the error status is returned
   to the caller, and no fault occurs.
*/

#define MM_ERR(param) { \
    if (stat) { \
        *stat = param; \
        return zero_pte; \
        } \
    p1 = MM_PARAM (acc & TLB_WACC, param); \
    p2 = va; \
    ABORT ((param & PR_TNV)? ABORT_TNV: ABORT_ACV); }

TLBENT fill (uint32 va, int32 lnt, int32 acc, int32 *stat)
{
int32 ptidx = (((uint32) va) >> 7) & ~03;
int32 tlbpte, ptead, pte, tbi, vpn;
static TLBENT zero_pte = { 0, 0 };

if (va & VA_S0) {                                       /* system space? */
    if (ptidx >= d_slr)                                 /* system */
        MM_ERR (PR_LNV);
    ptead = (d_sbr + ptidx) & PAMASK;
    }
else {
    if (va & VA_P1) {                                   /* P1? */
        if (ptidx < d_p1lr)
            MM_ERR (PR_LNV);
        ptead = d_p1br + ptidx;
        }
    else {                                              /* P0 */
        if (ptidx >= d_p0lr)
            MM_ERR (PR_LNV);
        ptead = d_p0br + ptidx;
        }
#if !defined (VAX_620)
    if ((ptead & VA_S0) == 0)
        ABORT (STOP_PPTE);                              /* ppte must be sys */
    vpn = VA_GETVPN (ptead);                            /* get vpn, tbi */
    tbi = VA_GETTBI (vpn);
    if (stlb[tbi].tag != vpn) {                         /* in sys tlb? */
        ptidx = ((uint32) ptead) >> 7;                  /* xlate like sys */
        if (ptidx >= d_slr)
            MM_ERR (PR_PLNV);
        pte = ReadLP ((d_sbr + ptidx) & PAMASK);        /* get system pte */
#if defined (VAX_780)
        if ((pte & PTE_ACC) == 0)                       /* spte ACV? */
            MM_ERR (PR_PACV);
#endif
        if ((pte & PTE_V) == 0)                         /* spte TNV? */
            MM_ERR (PR_PTNV);
        stlb[tbi].tag = vpn;                            /* set stlb tag */
        stlb[tbi].pte = cvtacc[PTE_GETACC (pte)] |
            ((pte << VA_N_OFF) & TLB_PFN);              /* set stlb data */
        }
    ptead = (stlb[tbi].pte & TLB_PFN) | VA_GETOFF (ptead);
#endif
    }
pte = ReadL (ptead);                                    /* read pte */
tlbpte = cvtacc[PTE_GETACC (pte)] |                     /* cvt access */
    ((pte << VA_N_OFF) & TLB_PFN);                      /* set addr */
if ((tlbpte & acc) == 0)                                /* chk access */
    MM_ERR (PR_ACV);
if ((pte & PTE_V) == 0)                                 /* check valid */
    MM_ERR (PR_TNV);
if (acc & TLB_WACC) {                                   /* write? */
    if ((pte & PTE_M) == 0)
        WriteL (ptead, pte | PTE_M);
    tlbpte = tlbpte | TLB_M;                            /* set M */
    }
vpn = VA_GETVPN (va);
tbi = VA_GETTBI (vpn);
if ((va & VA_S0) == 0) {                                /* process space? */
    ptlb[tbi].tag = vpn;                                /* store tlb ent */
    ptlb[tbi].pte = tlbpte;
    return ptlb[tbi];
    }
stlb[tbi].tag = vpn;                                    /* system space */
stlb[tbi].pte = tlbpte;                                 /* store tlb ent */
return stlb[tbi];
}

/* Utility routines */

void set_map_reg (void)
{
d_p0br = P0BR & ~03;
d_p1br = (P1BR - 0x800000) & ~03;                       /* VA<30> >> 7 */
d_sbr = (SBR - 0x1000000) & ~03;                        /* VA<31> >> 7 */
d_p0lr = (P0LR << 2);
d_p1lr = (P1LR << 2) + 0x800000;                        /* VA<30> >> 7 */
d_slr = (SLR << 2) + 0x1000000;                         /* VA<31> >> 7 */
}

/* Zap process (0) or whole (1) tb */

void zap_tb (int stb)
{
size_t i;

for (i = 0; i < VA_TBSIZE; i++) {
    ptlb[i].tag = ptlb[i].pte = -1;
    if (stb)
        stlb[i].tag = stlb[i].pte = -1;
    }
}

/* Zap single tb entry corresponding to va */

void zap_tb_ent (uint32 va)
{
int32 tbi = VA_GETTBI (VA_GETVPN (va));

if (va & VA_S0)
    stlb[tbi].tag = stlb[tbi].pte = -1;
else ptlb[tbi].tag = ptlb[tbi].pte = -1;
}

/* Check for tlb entry corresponding to va */

t_bool chk_tb_ent (uint32 va)
{
int32 vpn = VA_GETVPN (va);
int32 tbi = VA_GETTBI (vpn);
TLBENT xpte;

xpte = (va & VA_S0)? stlb[tbi]: ptlb[tbi];
if (xpte.tag == vpn)
    return TRUE;
return FALSE;
}

/* TLB examine */

t_stat tlb_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
int32 tlbn = uptr - tlb_unit;
uint32 idx = (uint32) addr >> 1;

if (idx >= VA_TBSIZE)
    return SCPE_NXM;
if (addr & 1)
    *vptr = ((uint32) (tlbn? stlb[idx].pte: ptlb[idx].pte));
else *vptr = ((uint32) (tlbn? stlb[idx].tag: ptlb[idx].tag));
return SCPE_OK;
}

/* TLB deposit */

t_stat tlb_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
int32 tlbn = uptr - tlb_unit;
uint32 idx = (uint32) addr >> 1;

if (idx >= VA_TBSIZE)
    return SCPE_NXM;
if (addr & 1) {
    if (tlbn) stlb[idx].pte = (int32) val;
    else ptlb[idx].pte = (int32) val;
    }
else {
    if (tlbn) stlb[idx].tag = (int32) val;
    else ptlb[idx].tag = (int32) val;
    }
return SCPE_OK;
}

/* TLB reset */

t_stat tlb_reset (DEVICE *dptr)
{
size_t i;

for (i = 0; i < VA_TBSIZE; i++)
    stlb[i].tag = ptlb[i].tag = stlb[i].pte = ptlb[i].pte = -1;
return SCPE_OK;
}

const char *tlb_description (DEVICE *dptr)
    {
    return "translation buffer";
    }
