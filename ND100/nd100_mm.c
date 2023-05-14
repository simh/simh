/*
 * Copyright (c) 2023 Anders Magnusson.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <setjmp.h>

#include "sim_defs.h"
#include "nd100_defs.h"
/*
 * Nord-100 Memory Management I
 *
 * The paging system on Nord-100 have two modes; normal and extended.
 * In normal mode it is Nord-10-compatible (with a maximum of 512KW memory)
 * and in extended it can handle up to 16MW.
 * 
 * It is quite extensive with both individual page protection (RWX) and
 * ring protection, where it has four rings. Ring 0 has lowest prio, ring 3
 * has highest.
 *
 * Page size is 1KW, so 64 pages per address space (page table, PT).
 * A process can use an extra page table as well, called alternate page table.
 * It is intended to allow large processes to use a separate data space.
 * There are four page tables, the PCR tells which table(s) to use.
 *
 * Which protection ring and which page tables a user belongs to is
 * configured in the internal Paging Control Register (PCR).  There is
 * one PCR per interrupt level. The PCR looks like this:
 *
 *    +-------------------+-------+-----+----------+--+-----+
 *    |      Unused       |   PT  | APT |  Level   | 0| Ring|
 *    +-------------------+-------+-----+----------+--+-----+
 *      15  14  13  12  11  10  9  8  7  6  5  4  3  2  1  0
 *
 *    PT/APT tells which page table should be used.  There are four of them.
 *    Level is not stored in the PCR, it is only used to tell which level
 *      a PCR belongs to.
 *    Ring is the user ring.  Must always be >= the page ring.
 *
 * The page tables are located in "shadow memory".  Shadow memory is the
 * top pages (>= 177400 in normal mode and >= 177000 in extended) only 
 * accessible when running as ring 3.
 * Page table for normal mode looks as below:
 *
 *    +---+---+---+---+---+-------+-------------------------+
 *    |WPM|RPM|FPM|WIP|PGU|  Ring |  Physical Page Number   |
 *    +---+---+---+---+---+-------+-------------------------+
 *      15  14  13  12  11  10  9  8  7  6  5  4  3  2  1  0
 *
 *    WPM/RPM/FPM allows for Write/Read/Fetch.
 *    WIP is set by HW and means "Written in page".
 *    PGU is set by HW and means "Page used".
 *    Ring is the ring the page belongs to.  Must be <= User ring.
 *
 * Note that the executing program get its ring level from the page where
 * the instruction were fetched from, so if the User ring is 3 but the
 * page ring is 2 then the progra cannot access the page tables.
 */

#define MAXMEMSIZE      (512*1024)

t_stat mm_reset(DEVICE *dptr);

uint16 PM[MAXMEMSIZE];
uint16 PCR[16];         /* Paging control register */
uint16 ptmap[4][64];    /* protection */
uint16 pmmap[4][64];    /* memory mapping */
uint16 pea, pes, pgs;
int pea_locked, pgs_locked;     /* flag to lock register after error */
int userring;           /* current user ring */

extern jmp_buf env;

#define ISDIS()         (mm_dev.flags & DEV_DIS)

UNIT mm_unit = { UDATA(NULL, UNIT_FIX+UNIT_DISABLE+UNIT_BINK, 0) };

REG mm_reg[] = {
        { BRDATA(PCR, PCR, 8, 16, 16) },
        { ORDATA(PEA, pea, 16) },
        { ORDATA(PES, pes, 16) },
        { ORDATA(PGS, pgs, 16) },
        { NULL }
};


DEVICE mm_dev = {
        "MM", &mm_unit, mm_reg, 0,
        1, 8, 16, 1, 8, 16,
        0, 0, &mm_reset,
        NULL, NULL, NULL,
        NULL, DEV_DISABLE
};

/*
 * Internal registers located on the MM module.
 */
int
mm_tra(int reg)
{
        int rv = 0;

        switch (reg) {
        case IRR_PES:
                regA = pes;
                break;

        case IRR_PGS:
                regA = pgs;
                pgs_locked = 0;
                break;

        case IRR_PGC:
                regA = PCR[(regA >> 3) & 017];
                break;

        case IRR_PEA:
                regA = pea;
                pea_locked = 0;
                break;

        default:
                rv = STOP_UNHINS;
        }
        return rv;
}


/*
 * Read a byte. 0 in lr is left byte, 1 is right byte.
 */
uint8
rdbyte(int vaddr, int lr, int how)
{
        uint16 val = rdmem(vaddr, how);

        return lr ? val & 0377 : val >> 8;
}

/*
 * Write a byte.  0 in lr is left byte, 1 is right byte.
 */
void
wrbyte(int vaddr, int val, int lr, int how)
{
        uint16 ov = rdmem(vaddr, how);

        val &= 0377; /* sanity */
        ov = lr ? (ov & 0177400) | val : (ov & 0377) | (val << 8);
        wrmem(vaddr, ov, how);
}

/*
 * Access shadow memory. if:
 * sexi == 0 && v >= 0177400 && (myring == 3 || pon == 0)
 *      or
 * sexi == 1 && v >= 0177000 && (myring == 3 || pon == 0)
 */
static int
is_shadow(int vaddr)
{
        if ((PCR[curlvl] & 03) < 3 && ISPON())
                return 0; /* not valid */
        if (vaddr > 0177777)
                return 0;
        if (ISSEX())
                return 1;
        return (vaddr >= 0177400);
}

/*
 * Fetch a word from the shadow mem.
 */
static int
shadowrd(int v)
{
        int pt;
        int x = 0;

        if (ISSEX())
                x = v & 1, v >>= 1;
        pt = (v >> 6) & 03;
        v &= 077;

        if (ISSEX())
                return x ? pmmap[pt][v] : ptmap[pt][v];
        return ptmap[pt][v]|pmmap[pt][v];
}

/*
 * Write a word to the shadow mem.
 */
static void
shadowwr(int v, int dat)
{
        int pt;
        int x = 0;

        if (ISSEX())
                x = v & 1, v >>= 1;
        pt = (v >> 6) & 03;
        v &= 077;

        if (ISSEX()) {
                if (x)
                        pmmap[pt][v] = dat;
                else
                        ptmap[pt][v] = dat;
        } else {
                pmmap[pt][v] = dat & 0777;
                ptmap[pt][v] = dat & 0177000;
        }
}

/*
 * MOR - memory out of range.  Addressing non-existent memory.
 */
void
morerr(int addr, int why, int pesval)
{
        if (pea_locked == 0) {
                pea = (uint16)addr;
                pes = (uint16)(addr >> 16) | pesval;
                pea_locked = 1;
        }
        intrpt14(IIE_MOR, why);
}

/*
 * Physical memory read when doing DMA.
 * The only error that can occur is non-existent memory (MOR).
 * returns -1 if MOR, value otherwise.
 */
int
dma_rdmem(int addr)
{
        addr &= 0xffffff;
        if (addr < MAXMEMSIZE)
                return PM[addr];
        morerr(addr, PM_DMA, PES_DMA);
        return -1;
}

int
dma_wrmem(int addr, int val)
{
        addr &= 0xffffff;
        if (addr < MAXMEMSIZE) {
                PM[addr] = val;
                return 0;
        }
        morerr(addr, PM_DMA, PES_DMA);
        return -1;
}

/*
 * Read direct from physical (24-bit-addr) memory.
 */
uint16
prdmem(int vaddr, int how)
{
        if ((vaddr & 0xffffff) < MAXMEMSIZE)
                return PM[vaddr];
        morerr(vaddr, PM_CPU, how == M_FETCH ? PES_FETCH : 0);
        return 0;
}

static void
pgsupd(int pgnr, int pnr, int flg)
{
        if (pgs_locked)
                return;
        pgs = (pgnr << 6) | pnr | flg;
        pgs_locked = 1;
}

uint16
rdmem(int vaddr, int how)
{
        uint16 *ptmapp;
        int sh, pagetablenr, pagering, pagenr, permit, p;

        vaddr &= 0177777; /* Sanity */

        /* Shadow memory? */
        if ((vaddr >= 0177000) && is_shadow(vaddr))
                return shadowrd(vaddr);
        /* Physical memory? */
        if (ISPON() == 0)
                return prdmem(vaddr, 0);

        /* Paging on. */
        permit = (how == M_FETCH ? PT_FPM : PT_RPM);
        userring = PCR[curlvl] & 03;
        sh = how == M_APT ? 7 : 9;
        pagetablenr = (PCR[curlvl] >> sh) & 03;
        pagenr = vaddr >> 10;
        ptmapp = &ptmap[pagetablenr][pagenr];
        pagering = (*ptmapp >> 9) & 03;
        p = (permit == PT_FPM ? PGS_FF : 0);
        if ((*ptmapp & (PT_WPM|PT_RPM|PT_FPM)) == 0) {
                /* page fault */
                pgsupd(pagetablenr, pagenr, p | PGS_PM);
                intrpt14(IIE_PF, PM_CPU);
        } else if ((*ptmapp & permit) == 0) {
                pgsupd(pagetablenr, pagenr, p | PGS_PM);
                intrpt14(IIE_PV, PM_CPU);
        } else if (pagering > userring) {
                pgsupd(pagetablenr, pagenr, p);
                intrpt14(IIE_PV, PM_CPU);
        } else {
                /* Mark page as read */
                *ptmapp |= PT_PGU;
                vaddr = (pmmap[pagetablenr][pagenr] << 10) | (vaddr & 01777);
                return prdmem(vaddr, 0);
        }
        return 0;
}

/*
 * Write direct to physical (24-bit-addr) memory.
 */
void
pwrmem(int vaddr, int val, int how)
{
        if ((vaddr & 0xffffff) < MAXMEMSIZE)
                PM[vaddr] = val;
        else
                morerr(vaddr, PM_CPU, how == M_FETCH ? PES_FETCH : 0);
}

void
wrmem(int vaddr, int val, int how)
{
        uint16 *ptmapp;
        int sh, pagetablenr, pagering, permit, pagenr;

        vaddr &= 0177777;
        if ((vaddr >= 0177000) && is_shadow(vaddr)) {
                shadowwr(vaddr, val);
                return;
        }
        if (ISPON() == 0) {
                pwrmem(vaddr, val, PM_CPU);
                return;
        }
        /* Paging on. */
        permit = PT_WPM;
        userring = PCR[curlvl] & 03;
        sh = how == M_APT ? 7 : 9;
        pagetablenr = (PCR[curlvl] >> sh) & 03;
        pagenr = vaddr >> 10;
        ptmapp = &ptmap[pagetablenr][pagenr];
        pagering = (*ptmapp >> 9) & 03;
        if ((*ptmapp & (PT_WPM|PT_RPM|PT_FPM)) == 0) {
                /* page fault */
                pgsupd(pagetablenr, pagenr, PGS_PM);
                intrpt14(IIE_PF, PM_CPU);
        } else if ((*ptmapp & permit) == 0) {
                pgsupd(pagetablenr, pagenr, PGS_PM);
                intrpt14(IIE_PV, PM_CPU);
        } else if (pagering > userring) {
                pgsupd(pagetablenr, pagenr, 0);
                intrpt14(IIE_PV, PM_CPU);
        } else {
                /* Mark page as written */
                *ptmapp |= (PT_PGU|PT_WIP);
                vaddr = (pmmap[pagetablenr][pagenr] << 10) | (vaddr & 01777);
                pwrmem(vaddr, val, PM_CPU);
        }
}

void
mm_wrpcr()
{
        if (ISDIS())
                return;
        PCR[(regA >> 3) & 017] = regA & 03603;
}

t_stat
mm_reset(DEVICE *dptr)
{
        return 0;
}

/*
 * Check if instruction is privileged enough to execute,
 * otherwise give priv instruction fault.
 */
void
mm_privcheck(void)
{
        if (ISPON() == 0)
                return;
        if (userring > 1)
                return;
        intrpt14(IIE_PI, PM_CPU);
}
