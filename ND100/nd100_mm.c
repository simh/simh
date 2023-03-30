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


#include "sim_defs.h"

#include "nd100_defs.h"

#define MAXMEMSIZE      512*1024

t_stat mm_reset(DEVICE *dptr);

uint16 PM[MAXMEMSIZE];
uint16 PCR[16];         /* Paging control register */
uint16 ptmap[4][64];    /* protection */
uint16 pmmap[4][64];    /* memory mapping */

#define ISDIS()         (mm_dev.flags & DEV_DIS)

UNIT mm_unit = { UDATA(NULL, UNIT_FIX+UNIT_DISABLE+UNIT_BINK, 0) };

REG mm_reg[] = {
        { BRDATA(PCR, PCR, 8, 16, 16) },
        { NULL }
};


DEVICE mm_dev = {
        "MM", &mm_unit, mm_reg, 0,
        1, 8, 16, 1, 8, 16,
        0, 0, &mm_reset,
        NULL, NULL, NULL,
        NULL, DEV_DISABLE+DEV_DIS
};

/*
 * Read a byte. 0 in lr is left byte, 1 is right byte.
 */
uint8
rdbyte(int vaddr, int lr/* , int how*/)
{
        uint16 val = rdmem(vaddr);

        return lr ? val & 0377 : val >> 8;
}

/*
 * Write a byte.  0 in lr is left byte, 1 is right byte.
 */
void
wrbyte(int vaddr, int val, int lr/* , int how*/)
{
        uint16 ov = rdmem(vaddr);

        val &= 0377; /* sanity */
        ov = lr ? (ov & 0177400) | val : (ov & 0377) | (val << 8);
        wrmem(vaddr, ov);
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


uint16
rdmem(int vaddr/* , int how*/)
{
        int pt;

        vaddr &= 0177777;

        if ((vaddr >= 0177000) && is_shadow(vaddr))
                return shadowrd(vaddr);
        /* Mark page as read */
        if (ISPON()) {
                pt = (PCR[curlvl] >> 8) & 03;
                ptmap[pt][vaddr >> 10] |= PT_PGU;
        }

//      if (ISPON() == 0)
                return PM[vaddr];

}

void
wrmem(int vaddr, int val/* , int how*/)
{
        vaddr &= 0177777;
        if ((vaddr >= 0177000) && is_shadow(vaddr)) {
                shadowwr(vaddr, val);
                return;
        }
        PM[vaddr] = val;
}

void
mm_wrpcr()
{
        if (ISDIS())
                return;
        PCR[(regA >> 3) & 017] = regA & 03603;
}

void
mm_rdpcr()
{
        if (ISDIS())
                return;
        regA = PCR[(regA >> 3) & 017];
}


t_stat
mm_reset(DEVICE *dptr)
{
        return 0;
}
