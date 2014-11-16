/* alpha_ev5_tlb.c - Alpha EV5 TLB simulator

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

   EV5 was the second generation Alpha CPU.  It was a four-way, in order issue
   CPU with onchip primary instruction and data caches, an onchip second level
   cache, and support for an offchip third level cache.  EV56 was a shrink, with
   added support for byte and word operations.  EV56PC was a version of EV56
   without the onchip second level cache.

   This module contains the routines for

        itlb_lookup             lookup vpn in instruction TLB
        itlb_load               load pte into instruction TLB
        itlb_read               read pte from instruction TLB using NLU pointer
        itlb_set_asn            set iasn
        itlb_set_cm             set icm
        itlb_set_spage          set ispage
        dtlb_lookup             lookup vpn in data TLB
        dtlb_load               load pte into data TLB
        dtlb_read               read pte from data TLB using NLU pointer
        dtlb_set_asn            set dasn
        dtlb_set_cm             set dcm
        dtlb_set_spage          set dspage
        tlb_ia                  TLB invalidate all
        tlb_is                  TLB invalidate single
        tlb_set_cm              TLB set current mode
*/

#include "alpha_defs.h"
#include "alpha_ev5_defs.h"

#define ITLB_SORT       qsort (itlb, ITLB_SIZE, sizeof (TLBENT), &tlb_comp);
#define DTLB_SORT       qsort (dtlb, DTLB_SIZE, sizeof (TLBENT), &tlb_comp);
#define TLB_ESIZE       (sizeof (TLBENT)/sizeof (uint32))
#define MM_RW(x)        (((x) & PTE_FOW)? EXC_W: EXC_R)

uint32 itlb_cm = 0;                                     /* current modes */
uint32 itlb_spage = 0;                                  /* superpage enables */
uint32 itlb_asn = 0;
uint32 itlb_nlu = 0;
TLBENT i_mini_tlb;
TLBENT itlb[ITLB_SIZE];
uint32 dtlb_cm = 0;
uint32 dtlb_spage = 0;
uint32 dtlb_asn = 0;
uint32 dtlb_nlu = 0;
TLBENT d_mini_tlb;
TLBENT dtlb[DTLB_SIZE];

uint32 cm_eacc = ACC_E (MODE_K);                        /* precomputed */
uint32 cm_racc = ACC_R (MODE_K);                        /* access checks */
uint32 cm_wacc = ACC_W (MODE_K);
uint32 cm_macc = ACC_M (MODE_K);

extern t_uint64 p1;
extern jmp_buf save_env;

uint32 mm_exc (uint32 macc);
void tlb_inval (TLBENT *tlbp);
t_stat itlb_reset (void);
t_stat dtlb_reset (void);
int tlb_comp (const void *e1, const void *e2);
t_stat tlb_reset (DEVICE *dptr);

/* TLB data structures

   tlb_dev      pager device descriptor
   tlb_unit     pager units
   tlb_reg      pager register list
*/

UNIT tlb_unit = { UDATA (NULL, 0, 0) };

REG tlb_reg[] = {
    { HRDATA (ICM, itlb_cm, 2) },
    { HRDATA (ISPAGE, itlb_spage, 2), REG_HRO },
    { HRDATA (IASN, itlb_asn, ITB_ASN_WIDTH) },
    { HRDATA (INLU, itlb_nlu, ITLB_WIDTH) },
    { BRDATA (IMINI, &i_mini_tlb, 16, 32, TLB_ESIZE) },
    { BRDATA (ITLB, itlb, 16, 32, ITLB_SIZE*TLB_ESIZE) },
    { HRDATA (DCM, dtlb_cm, 2) },
    { HRDATA (DSPAGE, dtlb_spage, 2), REG_HRO },
    { HRDATA (DASN, dtlb_asn, DTB_ASN_WIDTH) },
    { HRDATA (DNLU, dtlb_nlu, DTLB_WIDTH) },
    { BRDATA (DMINI, &d_mini_tlb, 16, 32, TLB_ESIZE) },
    { BRDATA (DTLB, dtlb, 16, 32, DTLB_SIZE*TLB_ESIZE) },
    { NULL }
    };

DEVICE tlb_dev = {
    "TLB", &tlb_unit, tlb_reg, NULL,
    1, 0, 0, 1, 0, 0,
    NULL, NULL, &tlb_reset,
    NULL, NULL, NULL
    };

/* Translate address, instruction, data, and console

   Inputs:
        va      =       virtual address
        acc     =       (VAX only) access mode
   Outputs:
        pa      =       translation buffer index
*/

t_uint64 trans_i (t_uint64 va)
{
uint32 va_sext = VA_GETSEXT (va);
uint32 vpn = VA_GETVPN (va);
TLBENT *tlbp;

if ((va_sext != 0) && (va_sext != VA_M_SEXT))           /* invalid virt addr? */
    ABORT1 (va, EXC_BVA + EXC_E);
if ((itlb_spage & SPEN_43) && VPN_GETSP43 (vpn) == 2) { /* 43b superpage? */
    if (itlb_cm != MODE_K) ABORT1 (va, EXC_ACV + EXC_E);
    return (va & SP43_MASK);
    }
if ((itlb_spage & SPEN_32) && (VPN_GETSP32 (vpn) == 0x1FFE)) {
    if (itlb_cm != MODE_K) ABORT1 (va, EXC_ACV + EXC_E);
    return (va & SP32_MASK);                            /* 32b superpage? */
    }
if (!(tlbp = itlb_lookup (vpn)))                        /* lookup vpn; miss? */
    ABORT1 (va, EXC_TBM + EXC_E);                       /* abort reference */
if (cm_eacc & ~tlbp->pte)                               /* check access */
    ABORT1 (va, mm_exc (cm_eacc & ~tlbp->pte) | EXC_E);
return PHYS_ADDR (tlbp->pfn, va);                       /* return phys addr */
}

t_uint64 trans_d (t_uint64 va, uint32 acc)
{
uint32 va_sext = VA_GETSEXT (va);
uint32 vpn = VA_GETVPN (va);
TLBENT *tlbp;

if ((va_sext != 0) && (va_sext != VA_M_SEXT))           /* invalid virt addr? */
    ABORT1 (va, EXC_BVA + MM_RW (acc));
if ((dtlb_spage & SPEN_43) && (VPN_GETSP43 (vpn) == 2)) {
    if (dtlb_cm != MODE_K) ABORT1 (va, EXC_ACV + MM_RW (acc));
    return (va & SP43_MASK);                            /* 43b superpage? */
    }
if ((dtlb_spage & SPEN_32) && (VPN_GETSP32 (vpn) == 0x1FFE)) {
    if (dtlb_cm != MODE_K) ABORT1 (va, EXC_ACV + MM_RW (acc));
    return (va & SP32_MASK);                            /* 32b superpage? */
    }
if (!(tlbp = dtlb_lookup (vpn)))                        /* lookup vpn; miss? */
    ABORT1 (va, EXC_TBM + MM_RW (acc));                 /* abort reference */
if (acc & ~tlbp->pte)                                   /* check access */
    ABORT1 (va, mm_exc (acc & ~tlbp->pte) | MM_RW (acc));
return PHYS_ADDR (tlbp->pfn, va);                       /* return phys addr */
}

/* Generate a memory management error code, based on the access check bits not
   set in PTE

   - If the access check bits, without FOx and V, fail, then ACV
   - If FOx set, then FOx
   - Otherwise, TNV */

uint32 mm_exc (uint32 not_set)
{
uint32 tacc;

tacc = not_set & ~(PTE_FOR | PTE_FOW | PTE_FOE | PTE_V);
if (tacc) return EXC_ACV;
tacc = not_set & (PTE_FOR | PTE_FOW | PTE_FOE);
if (tacc) return EXC_FOX;
return EXC_TNV;
}

/* TLB invalidate single */

void tlb_is (t_uint64 va, uint32 flags)
{
uint32 va_sext = VA_GETSEXT (va);
uint32 vpn = VA_GETVPN (va);
TLBENT *itlbp, *dtlbp;

if ((va_sext != 0) && (va_sext != VA_M_SEXT)) return;
if ((flags & TLB_CI) && (itlbp = itlb_lookup (vpn))) {
    tlb_inval (itlbp);
    tlb_inval (&i_mini_tlb);
    ITLB_SORT;
    }
if ((flags & TLB_CD) && (dtlbp = dtlb_lookup (vpn))) {
    tlb_inval (dtlbp);
    tlb_inval (&d_mini_tlb);
    DTLB_SORT;
    }
return;
}

/* TLB invalidate all */

void tlb_ia (uint32 flags)
{
uint32 i;

if (flags & TLB_CA) {
    if (flags & TLB_CI) itlb_reset ();
    if (flags & TLB_CD) dtlb_reset ();
    return;
    }
if (flags & TLB_CI) {
    for (i = 0; i < ITLB_SIZE; i++) {
        if (!(itlb[i].pte & PTE_ASM)) tlb_inval (&itlb[i]);
        }
    tlb_inval (&i_mini_tlb);
    ITLB_SORT;
    }
if (flags & TLB_CD) {
    for (i = 0; i < DTLB_SIZE; i++) {
        if (!(dtlb[i].pte & PTE_ASM)) tlb_inval (&dtlb[i]);
        }
    tlb_inval (&d_mini_tlb);
    DTLB_SORT;
    }
return;
}

/* TLB lookup */

TLBENT *itlb_lookup (uint32 vpn)
{
int32 p, hi, lo;

if (vpn == i_mini_tlb.tag) return &i_mini_tlb;
lo = 0;                                                 /* initial bounds */
hi = ITLB_SIZE - 1;
do {
    p = (lo + hi) >> 1;                                 /* probe */
    if ((itlb_asn == itlb[p].asn) && 
        (((vpn ^ itlb[p].tag) &
         ~((uint32) itlb[p].gh_mask)) == 0)) {          /* match to TLB? */
        i_mini_tlb.tag = vpn;
        i_mini_tlb.pte = itlb[p].pte;
        i_mini_tlb.pfn = itlb[p].pfn;
        itlb_nlu = itlb[p].idx + 1;
        if (itlb_nlu >= ITLB_SIZE) itlb_nlu = 0;
        return &i_mini_tlb;
        }
    if ((itlb_asn < itlb[p].asn) ||
        ((itlb_asn == itlb[p].asn) && (vpn < itlb[p].tag)))
        hi = p - 1;                                     /* go down? p is upper */
    else lo = p + 1;                                    /* go up? p is lower */
    }
while (lo <= hi);
return NULL;
}

TLBENT *dtlb_lookup (uint32 vpn)
{
int32 p, hi, lo;

if (vpn == d_mini_tlb.tag) return &d_mini_tlb;
lo = 0;                                                 /* initial bounds */
hi = DTLB_SIZE - 1;
do {
    p = (lo + hi) >> 1;                                 /* probe */
    if ((dtlb_asn == dtlb[p].asn) && 
        (((vpn ^ dtlb[p].tag) &
         ~((uint32) dtlb[p].gh_mask)) == 0)) {          /* match to TLB? */
        d_mini_tlb.tag = vpn;
        d_mini_tlb.pte = dtlb[p].pte;
        d_mini_tlb.pfn = dtlb[p].pfn;
        dtlb_nlu = dtlb[p].idx + 1;
        if (dtlb_nlu >= DTLB_SIZE) dtlb_nlu = 0;
        return &d_mini_tlb;
        }
    if ((dtlb_asn < dtlb[p].asn) ||
        ((dtlb_asn == dtlb[p].asn) && (vpn < dtlb[p].tag)))
        hi = p - 1;                                     /* go down? p is upper */
    else lo = p + 1;                                    /* go up? p is lower */
    }
while (lo <= hi);
return NULL;
}

/* Load TLB entry at NLU pointer, advance NLU pointer */

TLBENT *itlb_load (uint32 vpn, t_uint64 l3pte)
{
uint32 i, gh;

for (i = 0; i < ITLB_SIZE; i++) {
    if (itlb[i].idx == itlb_nlu) {
        TLBENT *tlbp = itlb + i;
        itlb_nlu = itlb_nlu + 1;
        if (itlb_nlu >= ITLB_SIZE) itlb_nlu = 0;
        tlbp->tag = vpn;
        tlbp->pte = (uint32) (l3pte & PTE_MASK) ^ (PTE_FOR|PTE_FOR|PTE_FOE);
        tlbp->pfn = ((uint32) (l3pte >> PTE_V_PFN)) & PFN_MASK;
        tlbp->asn = itlb_asn;
        gh = PTE_GETGH (tlbp->pte);
        tlbp->gh_mask = (1u << (3 * gh)) - 1;
        tlb_inval (&i_mini_tlb);
        ITLB_SORT;
        return tlbp;
        }
    }
fprintf (stderr, "%%ITLB entry not found, itlb_nlu = %d\n", itlb_nlu);
ABORT (-SCPE_IERR);
return NULL;
}

TLBENT *dtlb_load (uint32 vpn, t_uint64 l3pte)
{
uint32 i, gh;

for (i = 0; i < DTLB_SIZE; i++) {
    if (dtlb[i].idx == dtlb_nlu) {
        TLBENT *tlbp = dtlb + i;
        dtlb_nlu = dtlb_nlu + 1;
        if (dtlb_nlu >= ITLB_SIZE) dtlb_nlu = 0;
        tlbp->tag = vpn;
        tlbp->pte = (uint32) (l3pte & PTE_MASK) ^ (PTE_FOR|PTE_FOR|PTE_FOE);
        tlbp->pfn = ((uint32) (l3pte >> PTE_V_PFN)) & PFN_MASK;
        tlbp->asn = dtlb_asn;
        gh = PTE_GETGH (tlbp->pte);
        tlbp->gh_mask = (1u << (3 * gh)) - 1;
        tlb_inval (&d_mini_tlb);
        DTLB_SORT;
        return tlbp;
        }
    }
fprintf (stderr, "%%DTLB entry not found, dtlb_nlu = %d\n", dtlb_nlu);
ABORT (-SCPE_IERR);
return NULL;
}

/* Read TLB entry at NLU pointer, advance NLU pointer */

t_uint64 itlb_read (void)
{
uint8 i;

for (i = 0; i < ITLB_SIZE; i++) {
    if (itlb[i].idx == itlb_nlu) {
        TLBENT *tlbp = itlb + i;
        itlb_nlu = itlb_nlu + 1;
        if (itlb_nlu >= ITLB_SIZE) itlb_nlu = 0;
        return (((t_uint64) tlbp->pfn) << PTE_V_PFN) |
            ((tlbp->pte ^ (PTE_FOR|PTE_FOR|PTE_FOE)) & PTE_MASK);
        }
    }
fprintf (stderr, "%%ITLB entry not found, itlb_nlu = %d\n", itlb_nlu);
ABORT (-SCPE_IERR);
return 0;
}

t_uint64 dtlb_read (void)
{
uint8 i;

for (i = 0; i < DTLB_SIZE; i++) {
    if (dtlb[i].idx == dtlb_nlu) {
        TLBENT *tlbp = dtlb + i;
        dtlb_nlu = dtlb_nlu + 1;
        if (dtlb_nlu >= DTLB_SIZE) dtlb_nlu = 0;
        return (((t_uint64) tlbp->pfn) << PTE_V_PFN) |
            ((tlbp->pte ^ (PTE_FOR|PTE_FOR|PTE_FOE)) & PTE_MASK);
        }
    }
fprintf (stderr, "%%DTLB entry not found, dtlb_nlu = %d\n", dtlb_nlu);
ABORT (-SCPE_IERR);
return 0;
}

/* Set ASN - rewrite TLB globals with correct ASN */

void itlb_set_asn (uint32 asn)
{
int32 i;

itlb_asn = asn;
for (i = 0; i < ITLB_SIZE; i++) {
    if (itlb[i].pte & PTE_ASM) itlb[i].asn = asn;
    }
tlb_inval (&i_mini_tlb);
ITLB_SORT;
return;
} 

void dtlb_set_asn (uint32 asn)
{
int32 i;

dtlb_asn = asn;
for (i = 0; i < DTLB_SIZE; i++) {
    if (dtlb[i].pte & PTE_ASM) dtlb[i].asn = asn;
    }
tlb_inval (&d_mini_tlb);
DTLB_SORT;
return;
}

/* Set superpage */

void itlb_set_spage (uint32 spage)
{
itlb_spage = spage;
return;
}

void dtlb_set_spage (uint32 spage)
{
dtlb_spage = spage;
return;
}

/* Set current mode */

void itlb_set_cm (uint32 mode)
{
itlb_cm = mode;
cm_eacc = ACC_E (mode);
return;
}

void dtlb_set_cm (uint32 mode)
{
dtlb_cm = mode;
cm_racc = ACC_R (mode);
cm_wacc = ACC_W (mode);
return;
}

uint32 tlb_set_cm (int32 cm)
{
if (cm >= 0) {
    itlb_set_cm (cm);
    dtlb_set_cm (cm);
    return cm;
    }
itlb_set_cm (itlb_cm);
dtlb_set_cm (dtlb_cm);
return dtlb_cm;
}

/* Invalidate TLB entry */

void tlb_inval (TLBENT *tlbp)
{
tlbp->tag = INV_TAG;
tlbp->pte = 0;
tlbp->pfn = 0;
tlbp->asn = tlbp->idx;
tlbp->gh_mask = 0;
return;
}

/* Compare routine for qsort */

int tlb_comp (const void *e1, const void *e2)
{
TLBENT *t1 = (TLBENT *) e1;
TLBENT *t2 = (TLBENT *) e2;

if (t1->asn > t2->asn) return +1;
if (t1->asn < t2->asn) return -1;
if (t1->tag > t2->tag) return +1;
if (t1->tag < t2->tag) return -1;
return 0;
}

/* ITLB reset */

t_stat itlb_reset (void)
{
int32 i;

itlb_nlu = 0;
for (i = 0; i < ITLB_SIZE; i++) {
    itlb[i].tag = INV_TAG;
    itlb[i].pte = 0;
    itlb[i].pfn = 0;
    itlb[i].asn = i;
    itlb[i].gh_mask = 0;
    itlb[i].idx = i;
    }
tlb_inval (&i_mini_tlb);
return SCPE_OK;
}
/* DTLB reset */

t_stat dtlb_reset (void)
{
int32 i;

dtlb_nlu = 0;
for (i = 0; i < DTLB_SIZE; i++) {
    dtlb[i].tag = INV_TAG;
    dtlb[i].pte = 0;
    dtlb[i].pfn = 0;
    dtlb[i].asn = i;
    dtlb[i].gh_mask = 0;
    dtlb[i].idx = i;
    }
tlb_inval (&d_mini_tlb);
return SCPE_OK;
}

/* SimH reset */

t_stat tlb_reset (DEVICE *dptr)
{
itlb_reset ();
dtlb_reset ();
return SCPE_OK;
}

/* Show TLB entry or entries */

t_stat cpu_show_tlb (FILE *of, UNIT *uptr, int32 val, void *desc)
{
t_addr lo, hi;
uint32 lnt;
TLBENT *tlbp;
DEVICE *dptr;
const char *cptr = (char *) desc;

lnt = (val)? DTLB_SIZE: ITLB_SIZE;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL) return SCPE_IERR;
if (cptr) {
    cptr = get_range (dptr, cptr, &lo, &hi, 10, lnt, 0);
    if ((cptr == NULL) || (*cptr != 0)) return SCPE_ARG;
    }
else {
    lo = 0;
    hi = lnt - 1;
    }
tlbp = (val)? dtlb + lo: itlb + lo;

do {
    fprintf (of, "TLB %02d\tTAG=%02X/%08X, ", (uint32) lo, tlbp->asn, tlbp->tag);
    fprintf (of, "MASK=%X, INDX=%d, ", tlbp->gh_mask, tlbp->idx);
    fprintf (of, "PTE=%04X, PFN=%08X\n", tlbp->pte, tlbp->pfn);
    tlbp++;
    lo++;
    } while (lo <= hi);

return SCPE_OK;
}

