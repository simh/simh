/* pdp10_pag.c: PDP-10 paging subsystem simulator

   Copyright (c) 1993-2008, Robert M Supnik

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

   pag          KS10 pager

   22-Sep-05    RMS     Fixed declarations (from Sterling Garwood)
   02-Dec-01    RMS     Fixed bug in ITS LPMR (found by Dave Conroy)
   21-Aug-01    RMS     Fixed bug in ITS paging (found by Miriam Lennox)
                        Removed register from declarations
   19-May-01    RMS     Added workaround for TOPS-20 V4.1 boot bug
   03-May-01    RMS     Fixed bug in indirect page table pointer processing
   29-Apr-01    RMS     Added CLRCSH for ITS, fixed LPMR

   The pager consists of a standard hardware part (the translation
   tables) and an operating-system specific page table fill routine.

   There are two translation tables, one for executive mode and one
   for user mode.  Each table consists of 512 page table entries,
   one for each page in the 18b virtual address space.  Each pte
   contains (in the hardware) a valid bit, a writeable bit, an
   address space bit (executive or user), and a cacheable bit, plus
   the physical page number corresponding to the virtual page.  In
   the simulator, the pte is expanded for rapid processing of normal
   reads and writes.  An expanded pte contains a valid bit, a writeable
   bit, and the physical page number shifted left by the page size.

        Expanded pte            meaning
        0                       invalid
        >0                      read only
        <0                      read write

   There is a third, physical table, which is used in place of the
   executive and user tables if paging is off.  Its entries are always
   valid and always writeable.

   To translate a virtual to physical address, the simulator uses
   the virtual page number to index into the appropriate page table.
   If the page table entry (pte) is not valid, the page fill routine
   is called to see if the entry is merely not filled or is truly
   inaccessible.  If the pte is valid but not writeable, and the
   reference is a write reference, the page fill routine is also
   called to see if the reference can be resolved.

   The page fill routine is operating system dependent.  Three styles
   of paging are supported:

        TOPS10          known in the KS10 microcode as KI10 paging,
                        used by earlier versions of TOPS10
        TOPS20          known in the KS10 microcode as KL10 paging,
                        used by later versions of TOPS10, and TOPS20
        ITS             used only by ITS

   TOPS10 vs TOPS20 is selected by a bit in the EBR; ITS paging is
   "hardwired" (it required different microcode).
*/

#include "pdp10_defs.h"
#include <setjmp.h>

/* Page table (contains expanded pte's) */

#define PTBL_ASIZE      PAG_N_VPN
#define PTBL_MEMSIZE    (1 << PTBL_ASIZE)               /* page table size */
#define PTBL_AMASK      (PTBL_MEMSIZE - 1)
#define PTBL_M          (1u << 31)                      /* must be sign bit */
#define PTBL_V          (1u << 30)
#define PTBL_MASK       (PAG_PPN | PTBL_M | PTBL_V)

/* NXM processing */

#define REF_V           0                               /* ref is virt */
#define REF_P           1                               /* ref is phys */
#define PF_OK           0                               /* pfail ok */
#define PF_TR           1                               /* pfail trap */

extern d10 *M;
extern d10 acs[AC_NBLK * AC_NUM];
extern d10 *ac_prv, *last_pa;
extern a10 epta, upta;
extern d10 pager_word;
extern int32 apr_flg;
extern d10 ebr, ubr, hsb;
extern d10 spt, cst, cstm, pur;
extern a10 dbr1, dbr2, dbr3, dbr4;
extern d10 pcst, quant;
extern t_bool paging;
extern UNIT cpu_unit;
extern jmp_buf save_env;
extern int32 test_int (void);
extern int32 pi_eval (void);

int32 eptbl[PTBL_MEMSIZE];                              /* exec page table */
int32 uptbl[PTBL_MEMSIZE];                              /* user page table */
int32 physptbl[PTBL_MEMSIZE];                           /* phys page table */
int32 *ptbl_cur, *ptbl_prv;
int32 save_ea;

int32 ptbl_fill (a10 ea, int32 *ptbl, int32 mode);
t_stat pag_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat pag_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat pag_reset (DEVICE *dptr);
void pag_nxm (a10 pa, int32 phys, int32 trap);

/* Pager data structures

   pag_dev      pager device descriptor
   pag_unit     pager units
   pager_reg    pager register list
*/

UNIT pag_unit[] = {
    { UDATA (NULL, UNIT_FIX, PTBL_MEMSIZE) },
    { UDATA (NULL, UNIT_FIX, PTBL_MEMSIZE) }
    };

REG pag_reg[] = {
    { ORDATA (PANIC_EA, save_ea, PASIZE), REG_HRO },
    { NULL }
    };

DEVICE pag_dev = {
    "PAG", pag_unit, pag_reg, NULL,
    2, 8, PTBL_ASIZE, 1, 8, 32,
    &pag_ex, &pag_dep, &pag_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

/* Memory read and write routines

   Read - read current or previous, read checking
   ReadM - read current or previous, write checking
   ReadE - read exec
   ReadP - read physical
   Write - write current or previous
   WriteE - write exec
   WriteP - write physical
   AccChk - test accessibility of virtual address
*/

d10 Read (a10 ea, int32 prv)
{
int32 pa, vpn, xpte;

if (ea < AC_NUM)                                        /* AC request */
    return (prv? ac_prv[ea]: ac_cur[ea]);
vpn = PAG_GETVPN (ea);                                  /* get page num */
xpte = prv? ptbl_prv[vpn]: ptbl_cur[vpn];               /* get exp pte */
if (xpte == 0)
    xpte = ptbl_fill (ea, prv? ptbl_prv: ptbl_cur, PTF_RD);
pa = PAG_XPTEPA (xpte, ea);                             /* calc phys addr */
if (MEM_ADDR_NXM (pa))                                  /* process nxm */
    pag_nxm (pa, REF_V, PF_TR);
return M[pa];                                           /* return data */
}

d10 ReadM (a10 ea, int32 prv)
{
int32 pa, vpn, xpte;

if (ea < AC_NUM)                                        /* AC request */
    return (prv? ac_prv[ea]: ac_cur[ea]);
vpn = PAG_GETVPN (ea);                                  /* get page num */
xpte = prv? ptbl_prv[vpn]: ptbl_cur[vpn];               /* get exp pte */
if (xpte >= 0)
    xpte = ptbl_fill (ea, prv? ptbl_prv: ptbl_cur, PTF_WR);
pa = PAG_XPTEPA (xpte, ea);                             /* calc phys addr */
if (MEM_ADDR_NXM (pa))                                  /* process nxm */
    pag_nxm (pa, REF_V, PF_TR);
return M[pa];                                           /* return data */
}

d10 ReadE (a10 ea)
{
int32 pa, vpn, xpte;

if (ea < AC_NUM)                                        /* AC? use current */
    return AC(ea);
if (!PAGING)                                            /* phys? no mapping */
    return M[ea];
vpn = PAG_GETVPN (ea);                                  /* get page num */
xpte = eptbl[vpn];                                      /* get exp pte, exec tbl */
if (xpte == 0)
    xpte = ptbl_fill (ea, eptbl, PTF_RD);
pa = PAG_XPTEPA (xpte, ea);                             /* calc phys addr */
if (MEM_ADDR_NXM (pa))                                  /* process nxm */
    pag_nxm (pa, REF_V, PF_TR);
return M[pa];                                           /* return data */
}

d10 ReadP (a10 ea)
{
if (ea < AC_NUM)                                        /* AC request */
    return AC(ea);
if (MEM_ADDR_NXM (ea))                                  /* process nxm */
    pag_nxm (ea, REF_P, PF_TR);
return M[ea];                                           /* return data */
}

void Write (a10 ea, d10 val, int32 prv)
{
int32 pa, vpn, xpte;

if (ea < AC_NUM) {                                      /* AC request */
    if (prv)                                            /* write AC */
        ac_prv[ea] = val;
    else ac_cur[ea] = val;
    }
else {
    vpn = PAG_GETVPN (ea);                              /* get page num */
    xpte = prv? ptbl_prv[vpn]: ptbl_cur[vpn];           /* get exp pte */
    if (xpte >= 0)
        xpte = ptbl_fill (ea, prv? ptbl_prv: ptbl_cur, PTF_WR);
    pa = PAG_XPTEPA (xpte, ea);                         /* calc phys addr */
    if (MEM_ADDR_NXM (pa))                              /* process nxm */
        pag_nxm (pa, REF_V, PF_TR);
    else M[pa] = val;                                   /* write data */
    }
return;
}

void WriteE (a10 ea, d10 val)
{
int32 pa, vpn, xpte;

if (ea < AC_NUM)                                        /* AC? use current */
    AC(ea) = val;
else if (!PAGING)                                       /* phys? no mapping */
    M[ea] = val;
else {
    vpn = PAG_GETVPN (ea);                              /* get page num */
    xpte = eptbl[vpn];                                  /* get exp pte, exec tbl */
    if (xpte >= 0)
        xpte = ptbl_fill (ea, eptbl, PTF_WR);
    pa = PAG_XPTEPA (xpte, ea);                         /* calc phys addr */
    if (MEM_ADDR_NXM (pa))                              /* process nxm */
        pag_nxm (pa, REF_V, PF_TR);
    else M[pa] = val;                                   /* write data */
    }
return;
}

void WriteP (a10 ea, d10 val)
{
if (ea < AC_NUM)                                        /* AC request */
    AC(ea) = val;
else {
    if (MEM_ADDR_NXM (ea))                              /* process nxm */
        pag_nxm (ea, REF_P, PF_TR);
    M[ea] = val;                                        /* memory */
    }
return;
}

t_bool AccViol (a10 ea, int32 prv, int32 mode)
{
int32 vpn, xpte;

if (ea < AC_NUM)                                        /* AC request */
    return FALSE;
vpn = PAG_GETVPN (ea);                                  /* get page num */
xpte = prv? ptbl_prv[vpn]: ptbl_cur[vpn];               /* get exp pte */
if ((xpte == 0) || ((mode & PTF_WR) && (xpte > 0)))     /* not accessible? */
    xpte = ptbl_fill (ea, prv? ptbl_prv: ptbl_cur, mode | PTF_MAP);
if (xpte)                                               /* accessible */
    return FALSE;
return TRUE;                                            /* not accessible */
}

void pag_nxm (a10 pa, int32 phys, int32 trap)
{
apr_flg = apr_flg | APRF_NXM;                           /* set APR flag */
pi_eval ();                                             /* eval intr */
pager_word = PF_NXM | (phys? PF_NXMP: 0) |
    (TSTF (F_USR)? PF_USER: 0) | ((d10) pa);
if (PAGING && trap)                                     /* trap? */
    ABORT (PAGE_FAIL);
return;
}

/* Page table fill

   This routine is called if the page table is invalid, or on a write
   reference if the page table is read only.  If the access is allowed
   it stores the pte in the page table entry and returns an expanded
   pte for use by the caller.  Otherwise, it generates a page fail.

   Notes:
        - If called from the console, invalid references return a pte
          of 0, and the page table entry is not filled.
        - If called from MAP, invalid references return a pte of 0. The
          page fail word is properly set up.
*/

#define PAGE_FAIL_TRAP  if (mode & (PTF_CON | PTF_MAP)) \
                            return 0; \
                        ABORT (PAGE_FAIL)
#define READPT(x,y)     if (MEM_ADDR_NXM (y)) { \
                            pag_nxm (y, REF_P, PF_OK); \
                            PAGE_FAIL_TRAP; \
                            } \
                        x = ReadP (y)

int32 ptbl_fill (a10 ea, int32 *tbl, int32 mode)
{

/* ITS paging is based on conventional page tables.  ITS divides each address
   space into a 128K high and low section, and uses different descriptor base
   pointers (dbr) for each.  ITS pages are twice the size of DEC standard;
   therefore, the fill routine fills two page table entries and returns the pte
   that maps the correct ITS half page.  This allows the DEC paging macros to
   be used in the normal path read-write routines.

   ITS has no MAP instruction, therefore, physical NXM traps are ok.
*/

if (Q_ITS) {                                            /* ITS paging */
    int32 acc, decvpn, pte, vpn, ptead, xpte;
    d10 ptewd;

    vpn = ITS_GETVPN (ea);                              /* get ITS pagno */
    if (tbl == uptbl)
        ptead = ((ea & RSIGN)? dbr2: dbr1) + ((vpn >> 1) & 077);
    else ptead = ((ea & RSIGN)? dbr3: dbr4) + ((vpn >> 1) & 077);
    ptewd = ReadP (ptead);                              /* get PTE pair */
    pte = (int32) ((ptewd >> ((vpn & 1)? 0: 18)) & RMASK);
    acc = ITS_GETACC (pte);                             /* get access */
    pager_word = PF_VIRT | ea | ((tbl == uptbl)? PF_USER: 0) |
        ((mode & PTF_WR)? PF_ITS_WRITE: 0) | (acc << PF_ITS_V_ACC);
    if ((acc != ITS_ACC_NO) && (!(mode & PTF_WR) || (acc == ITS_ACC_RW))) { 
        pte = pte & ~PTE_ITS_AGE;                       /* clear age */
        if (vpn & 1)
            WriteP (ptead, (ptewd & LMASK) | pte);
        else WriteP (ptead, (ptewd & RMASK) | (((d10) pte) << 18));
        xpte = ((pte & PTE_ITS_PPMASK) << ITS_V_PN) | PTBL_V |
            ((acc == ITS_ACC_RW)? PTBL_M: 0);
        decvpn = PAG_GETVPN (ea);                       /* get tlb idx */
        if (!(mode & PTF_CON)) {
            tbl[decvpn & ~1] = xpte;                    /* map lo ITS page */
            tbl[decvpn | 1] = xpte + PAG_SIZE;          /* map hi */
            }
        return (xpte + ((decvpn & 1)? PAG_SIZE: 0));
        }
    PAGE_FAIL_TRAP;
    }                                                   /* end ITS paging */

/* TOPS-10 paging - checked against KS10 microcode

   TOPS-10 paging is also based on conventional page tables.  The user page
   tables are arranged contiguously at the beginning of the user process table;
   however, the executive page tables are scattered through the executive and
   user process tables.
*/

else if (!T20PAG) {                                     /* TOPS-10 paging */
    int32 pte, vpn, ptead, xpte;
    d10 ptewd;

    vpn = PAG_GETVPN (ea);                              /* get virt page num */
    if (tbl == uptbl)
        ptead = upta + UPT_T10_UMAP + (vpn >> 1);
    else if (vpn < 0340)
        ptead = epta + EPT_T10_X000 + (vpn >> 1);
    else if (vpn < 0400)
        ptead = upta + UPT_T10_X340 + ((vpn - 0340) >> 1);
    else ptead = epta + EPT_T10_X400 + ((vpn - 0400) >> 1);
    READPT (ptewd, ptead);                              /* get PTE pair */
    pte = (int32) ((ptewd >> ((vpn & 1)? 0: 18)) & RMASK);
    pager_word = PF_VIRT | ea | ((tbl == uptbl)? PF_USER: 0) |
        ((mode & PTF_WR)? PF_WRITE: 0) |
        ((pte & PTE_T10_A)? PF_T10_A |
        ((pte & PTE_T10_S)? PF_T10_S: 0): 0);
    if (mode & PTF_MAP) pager_word = pager_word |       /* map? add to pf wd */
        ((pte & PTE_T10_W)? PF_T10_W: 0) |              /* W, S, C bits */
        ((pte & PTE_T10_S)? PF_T10_S: 0) |
        ((pte & PTE_T10_C)? PF_C: 0);
    if ((pte & PTE_T10_A) && (!(mode & PTF_WR) || (pte & PTE_T10_W))) {
        xpte = ((pte & PTE_PPMASK) << PAG_V_PN) |       /* calc exp pte */
            PTBL_V | ((pte & PTE_T10_W)? PTBL_M: 0);
        if (!(mode & PTF_CON))                          /* set tbl if ~cons */
            tbl[vpn] = xpte;
        return xpte;
        }
    PAGE_FAIL_TRAP;
    }                                                   /* end TOPS10 paging */

/* TOPS-20 paging - checked against KS10 microcode

   TOPS-20 paging has three phases:

   1.   Starting at EPT/UPT + 540 + section number, chase section pointers to
        get the pointer to the section page table.  In the KS10, because there
        is only one section, the microcode caches the result of this evaluation.
        Also, the evaluation of indirect pointers is simplified, as the section
        table index is ignored.

   2.   Starting with the page map pointer, chase page pointers to get the
        pointer to the page.  The KS10 allows the operating system to inhibit
        updating of the CST (base address = 0).

   3.   Use the page pointer to get the CST entry.  If a write reference to
        a writeable page, set CST_M.  If CST_M is set, set M in page table.
*/

else {                                                  /* TOPS-20 paging */
    int32 pmi, vpn, xpte;
    int32 flg, t;
    t_bool stop;
    a10 pa, csta = 0;
    d10 ptr, cste;
    d10 acc = PTE_T20_W | PTE_T20_C;                    /* init access bits */

    pager_word = PF_VIRT | ea | ((tbl == uptbl)? PF_USER: 0) |
        ((mode & PTF_WR)? PF_WRITE: 0);                 /* set page fail word */

/* First phase - evaluate section pointers - returns a ptr to a page map
   As a single section machine, the KS10 short circuits this part of the
   process.  In particular, the indirect pointer calculation assumes that
   the section table index will be 0.  It adds the full pointer (not just
   the right half) to the SPT base.  If the section index is > 0, the
   result is a physical memory address > 256KW.  Depending on the size of
   memory, the SPT fetch may or may not generate a NXM page fail.  The
   KS10 then ignores the section table index in fetching the next pointer.

   The KS10 KL10 memory management diagnostic (dskec.sav) tests for this
   behavior with a section index of 3.  However, this would be a legal
   physical address in a system with 1MW.  Accordingly, the simulator
   special cases non-zero section indices (which can't work in any case)
   to generate the right behavior for the diagnostic.
*/

    vpn = PAG_GETVPN (ea);                              /* get virt page num */
    pa = (tbl == uptbl)? upta + UPT_T20_SCTN: epta + EPT_T20_SCTN;
    READPT (ptr, pa & PAMASK);                          /* get section 0 ptr */
    for (stop = FALSE, flg = 0; !stop; flg++) {         /* eval section ptrs */
        acc = acc & ptr;                                /* cascade acc bits */
        switch (T20_GETTYP (ptr)) {                     /* case on ptr type */

        case T20_NOA:                                   /* no access */
        default:                                        /* undefined type */
            PAGE_FAIL_TRAP;                             /* page fail */

        case T20_IMM:                                   /* immediate */
            stop = TRUE;                                /* exit */
            break;

        case T20_SHR:                                   /* shared */
            pa = (int32) (spt + (ptr & RMASK));         /* get SPT idx */
            READPT (ptr, pa & PAMASK);                  /* get SPT entry */
            stop = TRUE;                                /* exit */
            break;

        case T20_IND:                                   /* indirect */
            if (flg && (t = test_int ()))
                ABORT (t);
            pmi = T20_GETPMI (ptr);                     /* get sect tbl idx */
            pa = (int32) (spt + (ptr & RMASK));         /* get SPT idx */
            if (pmi) {                                  /* for dskec */
                pag_nxm ((pmi << 18) | pa, REF_P, PF_OK);
                PAGE_FAIL_TRAP;
                }
            READPT (ptr, pa & PAMASK);                  /* get SPT entry */
            if (ptr & PTE_T20_STM) {
                PAGE_FAIL_TRAP;
                }
            pa = PAG_PTEPA (ptr, pmi);                  /* index off page */
            READPT (ptr, pa & PAMASK);                  /* get pointer */
            break;                                      /* continue in loop */
            }                                           /* end case */
        }                                               /* end for */

/* Second phase - found page map ptr, evaluate page pointers */

    pa = PAG_PTEPA (ptr, vpn);                          /* get ptbl address */
    for (stop = FALSE, flg = 0; !stop; flg++) {         /* eval page ptrs */
        if (ptr & PTE_T20_STM) {                        /* non-res? */
            PAGE_FAIL_TRAP;
            }
        if (cst) {                                      /* cst really there? */
            csta = (int32) ((cst + (ptr & PTE_PPMASK)) & PAMASK);
            READPT (cste, csta);                        /* get CST entry */
            if ((cste & CST_AGE) == 0) {
                PAGE_FAIL_TRAP;
                }
            cste = (cste & cstm) | pur;                 /* update entry */
            WriteP (csta, cste);                        /* rewrite */
            }
        READPT (ptr, pa & PAMASK);                      /* get pointer */
        acc = acc & ptr;                                /* cascade acc bits */
        switch (T20_GETTYP (ptr)) {                     /* case on ptr type */

        case T20_NOA:                                   /* no access */
        default:                                        /* undefined type */
            PAGE_FAIL_TRAP;                             /* page fail */

        case T20_IMM:                                   /* immediate */
            stop = TRUE;                                /* exit */
            break;

        case T20_SHR:                                   /* shared */
            pa = (int32) (spt + (ptr & RMASK));         /* get SPT idx */
            READPT (ptr, pa & PAMASK);                  /* get SPT entry */
            stop = TRUE;                                /* exit */
            break;

        case T20_IND:                                   /* indirect */
            if (flg && (t = test_int ()))
                ABORT (t);
            pmi = T20_GETPMI (ptr);                     /* get section index */
            pa = (int32) (spt + (ptr & RMASK));         /* get SPT idx */
            READPT (ptr, pa & PAMASK);                  /* get SPT entry */
            pa = PAG_PTEPA (ptr, pmi);                  /* index off page */
            break;                                      /* continue in loop */
            }                                           /* end case */
        }                                               /* end for */

/* Last phase - have final page pointer, check modifiability */

    if (ptr & PTE_T20_STM) {                            /* non-resident? */
        PAGE_FAIL_TRAP;
        }
    if (cst) {                                          /* CST really there? */
        csta = (int32) ((cst + (ptr & PTE_PPMASK)) & PAMASK);
        READPT (cste, csta);                            /* get CST entry */
        if ((cste & CST_AGE) == 0) {
            PAGE_FAIL_TRAP;
            }
        cste = (cste & cstm) | pur;                     /* update entry */
        }
    else cste = 0;                                      /* no, entry = 0 */
    pager_word = pager_word | PF_T20_DN;                /* set eval done */
    xpte = ((int32) ((ptr & PTE_PPMASK) << PAG_V_PN)) | PTBL_V;
    if (mode & PTF_WR) {                                /* write? */
        if (acc & PTE_T20_W) {                          /* writable? */
            xpte = xpte | PTBL_M;                       /* set PTE M */
            cste = cste | CST_M;                        /* set CST M */
            }
        else {                                          /* no, trap */
            PAGE_FAIL_TRAP;
            }
        }
    if (cst)                                            /* write CST entry */
        WriteP (csta, cste);
    if (mode & PTF_MAP) pager_word = pager_word |       /* map? more in pf wd */
        ((xpte & PTBL_M)? PF_T20_M: 0) |                /* M, W, C bits */
        ((acc & PTE_T20_W)? PF_T20_W: 0) |
        ((acc & PTE_T20_C)? PF_C: 0);
    if (!(mode & PTF_CON))                              /* set tbl if ~cons */
        tbl[vpn] = xpte;
    return xpte;
    }                                                   /* end TOPS20 paging */
}

/* Set up pointers for AC, memory, and process table access */

void set_dyn_ptrs (void)
{
int32 t;

if (PAGING) {
    ac_cur = &acs[UBR_GETCURAC (ubr) * AC_NUM];
    ac_prv = &acs[UBR_GETPRVAC (ubr) * AC_NUM];
    if (TSTF (F_USR))
        ptbl_cur = ptbl_prv = &uptbl[0];
    else {
        ptbl_cur = &eptbl[0];
        ptbl_prv = TSTF (F_UIO)? &uptbl[0]: &eptbl[0];
        }
    }
else {
    ac_cur = ac_prv = &acs[0];
    ptbl_cur = ptbl_prv = &physptbl[0];
    }
t = EBR_GETEBR (ebr);
epta = t << PAG_V_PN;
if (Q_ITS)
    upta = (int32) ubr & PAMASK;
else {
    t = UBR_GETUBR (ubr);
    upta = t << PAG_V_PN;
    }
return;
}

/* MAP instruction, TOPS-10 and TOPS-20 only

   According to the KS-10 ucode, map with paging disabled sets
   "accessible, writeable, software", regardless of whether
   TOPS-10 or TOPS-20 paging is implemented
*/

d10 map (a10 ea, int32 prv)
{
int32 xpte;
d10 val = (TSTF (F_USR)? PF_USER: 0);

if (!PAGING)
    return (val | PF_T10_A | PF_T10_W | PF_T10_S | ea);
xpte = ptbl_fill (ea, prv? ptbl_prv: ptbl_cur, PTF_MAP); /* get exp pte */
if (xpte)
    val = (pager_word & ~PAMASK) | PAG_XPTEPA (xpte, ea);
else {
    if (pager_word & PF_HARD)                           /* hard error */
        val = pager_word;
    else val = val | PF_VIRT | ea;                      /* inaccessible */
    }
return val;
}

/* Mapping routine for console */

a10 conmap (a10 ea, int32 mode, int32 sw)
{
int32 xpte, *tbl;

if (!PAGING)
    return ea;
set_dyn_ptrs ();                                        /* in case changed */
if (sw & SWMASK ('E'))
    tbl = eptbl;
else if (sw & SWMASK ('U'))
    tbl = uptbl;
else tbl = ptbl_cur;
xpte = ptbl_fill (ea, tbl, mode);
if (xpte)
    return PAG_XPTEPA (xpte, ea);
else return MAXMEMSIZE;
}

/* Common pager instructions */

t_bool clrpt (a10 ea, int32 prv)
{
int32 vpn = PAG_GETVPN (ea);                            /* get page num */

if (Q_ITS) {                                            /* ITS? */
    uptbl[vpn & ~1] = 0;                                /* clear double size */
    uptbl[vpn | 1] = 0;                                 /* entries in */
    eptbl[vpn & ~1] = 0;                                /* both page tables */
    eptbl[vpn | 1] = 0;
    }
else {
    uptbl[vpn] = 0;                                     /* clear entries in */
    eptbl[vpn] = 0;                                     /* both page tables */
    }
return FALSE;
} 

t_bool wrebr (a10 ea, int32 prv)
{
ebr = ea & EBR_MASK;                                    /* store EBR */
pag_reset (&pag_dev);                                   /* clear page tables */
set_dyn_ptrs ();                                        /* set dynamic ptrs */
return FALSE;
}

t_bool rdebr (a10 ea, int32 prv)
{
Write (ea, (ebr & EBR_MASK), prv);
return FALSE;
}

t_bool wrubr (a10 ea, int32 prv)
{
d10 val = Read (ea, prv);
d10 ubr_mask = (Q_ITS)? PAMASK: UBR_UBRMASK;            /* ITS: ubr is wd addr */

if (val & UBR_SETACB)                                   /* set AC's? */
    ubr = ubr & ~UBR_ACBMASK;
else val = val & ~UBR_ACBMASK;                          /* no, keep old val */
if (val & UBR_SETUBR) {                                 /* set UBR? */
    ubr = ubr & ~ubr_mask;
    pag_reset (&pag_dev);                               /* yes, clr pg tbls */
    }
else val = val & ~ubr_mask;                             /* no, keep old val */
ubr = (ubr | val) & (UBR_ACBMASK | ubr_mask);
set_dyn_ptrs ();
return FALSE;
}

t_bool rdubr (a10 ea, int32 prv)
{
ubr = ubr & (UBR_ACBMASK | (Q_ITS? PAMASK: UBR_UBRMASK));
Write (ea, UBRWORD, prv);
return FALSE;
}

t_bool wrhsb (a10 ea, int32 prv)
{
hsb = Read (ea, prv) & PAMASK;
return FALSE;
}

t_bool rdhsb (a10 ea, int32 prv)
{
Write (ea, hsb, prv);
return FALSE;
}

/* TOPS20 pager instructions */

t_bool wrspb (a10 ea, int32 prv)
{
spt = Read (ea, prv);
return FALSE;
}

t_bool rdspb (a10 ea, int32 prv)
{
Write (ea, spt, prv);
return FALSE;
}

t_bool wrcsb (a10 ea, int32 prv)
{
cst = Read (ea, prv);
return FALSE;
}

t_bool rdcsb (a10 ea, int32 prv)
{
Write (ea, cst, prv);
return FALSE;
}

t_bool wrpur (a10 ea, int32 prv)
{
pur = Read (ea, prv);
return FALSE;
}

t_bool rdpur (a10 ea, int32 prv)
{
Write (ea, pur, prv);
return FALSE;
}

t_bool wrcstm (a10 ea, int32 prv)
{
cstm = Read (ea, prv);
if ((cpu_unit.flags & UNIT_T20) && (ea == 040127))
    cstm = INT64_C(0770000000000);
return FALSE;
}

t_bool rdcstm (a10 ea, int32 prv)
{
Write (ea, cstm, prv);
return FALSE;
}

/* ITS pager instructions
   The KS10 does not implement the JPC option.
*/

t_bool clrcsh (a10 ea, int32 prv)
{
return FALSE;
}

t_bool ldbr1 (a10 ea, int32 prv)
{
dbr1 = ea;
pag_reset (&pag_dev);
return FALSE;
}

t_bool sdbr1 (a10 ea, int32 prv)
{
Write (ea, dbr1, prv);
return FALSE;
}

t_bool ldbr2 (a10 ea, int32 prv)
{
dbr2 = ea;
pag_reset (&pag_dev);
return FALSE;
}

t_bool sdbr2 (a10 ea, int32 prv)
{
Write (ea, dbr2, prv);
return FALSE;
}

t_bool ldbr3 (a10 ea, int32 prv)
{
dbr3 = ea;
pag_reset (&pag_dev);
return FALSE;
}

t_bool sdbr3 (a10 ea, int32 prv)
{
Write (ea, dbr3, prv);
return FALSE;
}

t_bool ldbr4 (a10 ea, int32 prv)
{
dbr4 = ea;
pag_reset (&pag_dev);
return FALSE;
}

t_bool sdbr4 (a10 ea, int32 prv)
{
Write (ea, dbr4, prv);
return FALSE;
}

t_bool wrpcst (a10 ea, int32 prv)
{
pcst = Read (ea, prv);
return FALSE;
}

t_bool rdpcst (a10 ea, int32 prv)
{
Write (ea, pcst, prv);
return FALSE;
}

t_bool lpmr (a10 ea, int32 prv)
{
d10 val;

val = Read (ADDA (ea, 2), prv);
dbr1 = (a10) (Read (ea, prv) & AMASK);
dbr2 = (a10) (Read (ADDA (ea, 1), prv) & AMASK);
quant = val;
pag_reset (&pag_dev);
return FALSE;
}

t_bool spm (a10 ea, int32 prv)
{

ReadM (ADDA (ea, 2), prv);
Write (ea, dbr1, prv);
Write (ADDA (ea, 1), dbr2, prv);
Write (ADDA (ea, 2), quant, prv);
return FALSE;
}

/* Simulator interface routines */

t_stat pag_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
int32 tbln = uptr - pag_unit;

if (addr >= PTBL_MEMSIZE)
    return SCPE_NXM;
*vptr = tbln? uptbl[addr]: eptbl[addr];;
return SCPE_OK;
}

t_stat pag_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
int32 tbln = uptr - pag_unit;

if (addr >= PTBL_MEMSIZE)
    return SCPE_NXM;
if (tbln)
    uptbl[addr] = (int32) val & PTBL_MASK;
else eptbl[addr] = (int32) val & PTBL_MASK;
return SCPE_OK;
}

t_stat pag_reset (DEVICE *dptr)
{
int32 i;

for (i = 0; i < PTBL_MEMSIZE; i++) {
    eptbl[i] = uptbl[i] = 0;
    physptbl[i] = (i << PAG_V_PN) + PTBL_M + PTBL_V;
    }
return SCPE_OK;
}
