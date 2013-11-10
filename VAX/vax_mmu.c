/* vax_mmu.c - VAX memory management

   Copyright (c) 1998-2008, Robert M Supnik

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

   09-Nov-13    MB      Fixed reading/writing of unaligned data
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
#include <setjmp.h>

typedef struct {
    int32       tag;                                    /* tag */
    int32       pte;                                    /* pte */
    } TLBENT;

extern uint32 *M;
extern const uint32 align[4];
extern int32 PSL;
extern int32 mapen;
extern int32 p1, p2;
extern int32 P0BR, P0LR;
extern int32 P1BR, P1LR;
extern int32 SBR, SLR;
extern int32 SISR;
extern jmp_buf save_env;
extern UNIT cpu_unit;

int32 d_p0br, d_p0lr;                                   /* dynamic copies */
int32 d_p1br, d_p1lr;                                   /* altered per ucode */
int32 d_sbr, d_slr;
extern int32 mchk_va, mchk_ref;                         /* for mcheck */
TLBENT stlb[VA_TBSIZE], ptlb[VA_TBSIZE];
static const int32 insert[4] = {
    0x00000000, 0x000000FF, 0x0000FFFF, 0x00FFFFFF
    };
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
char *tlb_description (DEVICE *dptr);

TLBENT fill (uint32 va, int32 lnt, int32 acc, int32 *stat);
extern int32 ReadIO (uint32 pa, int32 lnt);
extern void WriteIO (uint32 pa, int32 val, int32 lnt);
extern int32 ReadReg (uint32 pa, int32 lnt);
extern void WriteReg (uint32 pa, int32 val, int32 lnt);

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

/* Read and write virtual

   These routines logically fall into three phases:

   1.   Look up the virtual address in the translation buffer, calling
        the fill routine on a tag mismatch or access mismatch (invalid
        tlb entries have access = 0 and thus always mismatch).  The
        fill routine handles all errors.  If the resulting physical
        address is aligned, do an aligned physical read or write.
   2.   Test for unaligned across page boundaries.  If cross page, look
        up the physical address of the second page.  If not cross page,
        the second physical address is the same as the first.
   3.   Using the two physical addresses, do an unaligned read or
        write, with three cases: unaligned long, unaligned word within
        a longword, unaligned word crossing a longword boundary.

   Note that these routines do not handle quad or octa references.
*/

/* Read virtual

   Inputs:
        va      =       virtual address
        lnt     =       length code (BWL)
        acc     =       access code (KESU)
   Output:
        returned data, right justified in 32b longword
*/

int32 Read (uint32 va, int32 lnt, int32 acc)
{
int32 vpn, off, tbi, pa;
int32 pa1, bo, sc, wl, wh;
TLBENT xpte;

mchk_va = va;
if (mapen) {                                            /* mapping on? */
    vpn = VA_GETVPN (va);                               /* get vpn, offset */
    off = VA_GETOFF (va);
    tbi = VA_GETTBI (vpn);
    xpte = (va & VA_S0)? stlb[tbi]: ptlb[tbi];          /* access tlb */
    if (((xpte.pte & acc) == 0) || (xpte.tag != vpn) ||
        ((acc & TLB_WACC) && ((xpte.pte & TLB_M) == 0)))
        xpte = fill (va, lnt, acc, NULL);               /* fill if needed */
    pa = (xpte.pte & TLB_PFN) | off;                    /* get phys addr */
    }
else {
    pa = va & PAMASK;
    off = 0;
    }
if ((pa & (lnt - 1)) == 0) {                            /* aligned? */
    if (lnt >= L_LONG)                                  /* long, quad? */
        return ReadL (pa);
    if (lnt == L_WORD)                                  /* word? */
        return ReadW (pa);
    return ReadB (pa);                                  /* byte */
    }
if (mapen && ((uint32)(off + lnt) > VA_PAGSIZE)) {      /* cross page? */
    vpn = VA_GETVPN (va + lnt);                         /* vpn 2nd page */
    tbi = VA_GETTBI (vpn);
    xpte = (va & VA_S0)? stlb[tbi]: ptlb[tbi];          /* access tlb */
    if (((xpte.pte & acc) == 0) || (xpte.tag != vpn) ||
        ((acc & TLB_WACC) && ((xpte.pte & TLB_M) == 0)))
        xpte = fill (va + lnt, lnt, acc, NULL);         /* fill if needed */
    pa1 = (xpte.pte & TLB_PFN) | VA_GETOFF (va + 4);
    }
else pa1 = (pa + 4) & PAMASK;                           /* not cross page */
bo = pa & 3;
pa = pa & ~3;                                           /* convert to aligned */
pa1 = pa1 & ~3;
if (lnt >= L_LONG) {                                    /* lw unaligned? */
    sc = bo << 3;
    wl = ReadL (pa);                                    /* read both lw */
    wh = ReadL (pa1);                                   /* extract */
    return ((((wl >> sc) & align[bo]) | (wh << (32 - sc))) & LMASK);
    }
else if (bo == 1)
    return ((ReadL (pa) >> 8) & WMASK);
else {
    wl = ReadL (pa);                                    /* word cross lw */
    wh = ReadL (pa1);                                   /* read, extract */
    return (((wl >> 24) & 0xFF) | ((wh & 0xFF) << 8));
    }
}

/* Write virtual

   Inputs:
        va      =       virtual address
        val     =       data to be written, right justified in 32b lw
        lnt     =       length code (BWL)
        acc     =       access code (KESU)
   Output:
        none
*/

void Write (uint32 va, int32 val, int32 lnt, int32 acc)
{
int32 vpn, off, tbi, pa;
int32 pa1, bo, sc, wl, wh;
TLBENT xpte;

mchk_va = va;
if (mapen) {
    vpn = VA_GETVPN (va);
    off = VA_GETOFF (va);
    tbi = VA_GETTBI (vpn);
    xpte = (va & VA_S0)? stlb[tbi]: ptlb[tbi];          /* access tlb */
    if (((xpte.pte & acc) == 0) || (xpte.tag != vpn) ||
        ((xpte.pte & TLB_M) == 0))
        xpte = fill (va, lnt, acc, NULL);
    pa = (xpte.pte & TLB_PFN) | off;
    }
else {
    pa = va & PAMASK;
    off = 0;
    }
if ((pa & (lnt - 1)) == 0) {                            /* aligned? */
    if (lnt >= L_LONG)                                  /* long, quad? */
        WriteL (pa, val);
    else if (lnt == L_WORD)                             /* word? */
        WriteW (pa, val);
    else WriteB (pa, val);                              /* byte */
    return;
    }
if (mapen && ((uint32)(off + lnt) > VA_PAGSIZE)) {
    vpn = VA_GETVPN (va + 4);
    tbi = VA_GETTBI (vpn);
    xpte = (va & VA_S0)? stlb[tbi]: ptlb[tbi];          /* access tlb */
    if (((xpte.pte & acc) == 0) || (xpte.tag != vpn) ||
        ((xpte.pte & TLB_M) == 0))
        xpte = fill (va + lnt, lnt, acc, NULL);
    pa1 = (xpte.pte & TLB_PFN) | VA_GETOFF (va + 4);
    }
else pa1 = (pa + 4) & PAMASK;
bo = pa & 3;
pa = pa & ~3;                                           /* convert to aligned */
pa1 = pa1 & ~3;
wl = ReadL (pa);
if (lnt >= L_LONG) {
    sc = bo << 3;
    wh = ReadL (pa1);
    wl = (wl & insert[bo]) | ((val << sc) & LMASK);
    wh = (wh & ~insert[bo]) | ((val >> (32 - sc)) & insert[bo]);
    WriteL (pa, wl);
    WriteL (pa1, wh);
    }
else if (bo == 1) {
    wl = (wl & 0xFF0000FF) | (val << 8);
    WriteL (pa, wl);
    }
else {
    wh = ReadL (pa1);
    wl = (wl & 0x00FFFFFF) | ((val & 0xFF) << 24);
    wh = (wh & 0xFFFFFF00) | ((val >> 8) & 0xFF);
    WriteL (pa, wl);
    WriteL (pa1, wh);
    }
return;
}

/* Test access to a byte (VAX PROBEx) */

int32 Test (uint32 va, int32 acc, int32 *status)
{
int32 vpn, off, tbi;
TLBENT xpte;

*status = PR_OK;                                        /* assume ok */
if (mapen) {                                            /* mapping on? */
    vpn = VA_GETVPN (va);                               /* get vpn, off */
    off = VA_GETOFF (va);
    tbi = VA_GETTBI (vpn);
    xpte = (va & VA_S0)? stlb[tbi]: ptlb[tbi];          /* access tlb */
    if ((xpte.pte & acc) && (xpte.tag == vpn))          /* TB hit, acc ok? */ 
        return (xpte.pte & TLB_PFN) | off;
    xpte = fill (va, L_BYTE, acc, status);              /* fill TB */
    if (*status == PR_OK)
        return (xpte.pte & TLB_PFN) | off;
    else return -1;
    }
return va & PAMASK;                                     /* ret phys addr */
}

/* Read aligned physical (in virtual context, unless indicated)

   Inputs:
        pa      =       physical address, naturally aligned
   Output:
        returned data, right justified in 32b longword
*/

SIM_INLINE int32 ReadB (uint32 pa)
{
int32 dat;

if (ADDR_IS_MEM (pa))
    dat = M[pa >> 2];
else {
    mchk_ref = REF_V;
    if (ADDR_IS_IO (pa))
        dat = ReadIO (pa, L_BYTE);
    else dat = ReadReg (pa, L_BYTE);
    }
return ((dat >> ((pa & 3) << 3)) & BMASK);
}

SIM_INLINE int32 ReadW (uint32 pa)
{
int32 dat;

if (ADDR_IS_MEM (pa))
    dat = M[pa >> 2];
else {
    mchk_ref = REF_V;
    if (ADDR_IS_IO (pa))
        dat = ReadIO (pa, L_WORD);
    else dat = ReadReg (pa, L_WORD);
    }
return ((dat >> ((pa & 2)? 16: 0)) & WMASK);
}

SIM_INLINE int32 ReadL (uint32 pa)
{
if (ADDR_IS_MEM (pa))
    return M[pa >> 2];
mchk_ref = REF_V;
if (ADDR_IS_IO (pa)) return ReadIO (pa, L_LONG);
return ReadReg (pa, L_LONG);
}

SIM_INLINE int32 ReadLP (uint32 pa)
{
if (ADDR_IS_MEM (pa))
    return M[pa >> 2];
mchk_va = pa;
mchk_ref = REF_P;
if (ADDR_IS_IO (pa))
    return ReadIO (pa, L_LONG);
return ReadReg (pa, L_LONG);
}

/* Write aligned physical (in virtual context, unless indicated)

   Inputs:
        pa      =       physical address, naturally aligned
        val     =       data to be written, right justified in 32b longword
   Output:
        none
*/

SIM_INLINE void WriteB (uint32 pa, int32 val)
{
if (ADDR_IS_MEM (pa)) {
    int32 id = pa >> 2;
    int32 sc = (pa & 3) << 3;
    int32 mask = 0xFF << sc;
    M[id] = (M[id] & ~mask) | (val << sc);
    }
else {
    mchk_ref = REF_V;
    if (ADDR_IS_IO (pa))
        WriteIO (pa, val, L_BYTE);
    else WriteReg (pa, val, L_BYTE);
    }
return;
}

SIM_INLINE void WriteW (uint32 pa, int32 val)
{
if (ADDR_IS_MEM (pa)) {
    int32 id = pa >> 2;
    M[id] = (pa & 2)? (M[id] & 0xFFFF) | (val << 16):
        (M[id] & ~0xFFFF) | val;
    }
else {
    mchk_ref = REF_V;
    if (ADDR_IS_IO (pa))
        WriteIO (pa, val, L_WORD);
    else WriteReg (pa, val, L_WORD);
    }
return;
}

SIM_INLINE void WriteL (uint32 pa, int32 val)
{
if (ADDR_IS_MEM (pa))
    M[pa >> 2] = val;
else {
    mchk_ref = REF_V;
    if (ADDR_IS_IO (pa))
        WriteIO (pa, val, L_LONG);
    else WriteReg (pa, val, L_LONG);
    }
return;
}

void WriteLP (uint32 pa, int32 val)
{
if (ADDR_IS_MEM (pa))
    M[pa >> 2] = val;
else {
    mchk_va = pa;
    mchk_ref = REF_P;
    if (ADDR_IS_IO (pa))
        WriteIO (pa, val, L_LONG);
    else WriteReg (pa, val, L_LONG);
    }
return;
}

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

extern void set_map_reg (void)
{
d_p0br = P0BR & ~03;
d_p1br = (P1BR - 0x800000) & ~03;                       /* VA<30> >> 7 */
d_sbr = (SBR - 0x1000000) & ~03;                        /* VA<31> >> 7 */
d_p0lr = (P0LR << 2);
d_p1lr = (P1LR << 2) + 0x800000;                        /* VA<30> >> 7 */
d_slr = (SLR << 2) + 0x1000000;                         /* VA<31> >> 7 */
return;
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
return;
}

/* Zap single tb entry corresponding to va */

void zap_tb_ent (uint32 va)
{
int32 tbi = VA_GETTBI (VA_GETVPN (va));

if (va & VA_S0)
    stlb[tbi].tag = stlb[tbi].pte = -1;
else ptlb[tbi].tag = ptlb[tbi].pte = -1;
return;
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

char *tlb_description (DEVICE *dptr)
    {
    return "translation buffer";
    }
