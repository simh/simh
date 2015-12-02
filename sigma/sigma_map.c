/* sigma_map.c: XDS Sigma memory access routines

   Copyright (c) 2007, Robert M Supnik

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

#include "sigma_defs.h"

#define BVA_REG         (RF_NUM << 2)
#define BPAMASK         ((cpu_tab[cpu_model].pamask << 2) | 0x3)
#define NUM_MUNITS      (MAXMEMSIZE / CPU_MUNIT_SIZE)

/* Sigma 8-9 memory status words */

#define S89_SR0_BADLMS  0x00800000                      /* bad LMS */
#define S89_SR0_RD      (S89_SR0_BADLMS)
#define S89_SR0_V_PORTS 12

#define S89_SR1_FIXED   0x50C40000                      /* always 1 */
#define S89_SR1_M_MEMU  0xF                             /* mem unit */
#define S89_SR1_V_MEMU  24
#define S89_SR1_MARG    0x00F80000                      /* margin write */
#define S89_SR1_MAROFF  2                               /* offset to read */

/* 5X0 memory status words */

#define S5X0_SR0_FIXED  0x40000000
#define S5X0_SR0_BADLMS 0x00000004                      /* bad LMS */
#define S5X0_SR0_RD     (S5X0_SR0_BADLMS)
#define S5X0_SR0_V_PORTS 21

#define S5X0_SR1_FIXED  0xB0000000                      /* fixed */
#define S5X0_SR1_M_MEMU 0x7                             /* mem unit */
#define S5X0_SR1_V_MEMU 25
#define S5X0_SR1_V_SA   18                              /* start addr */

#define S8

typedef struct {
    uint32          width;                              /* item width */
    uint32          dmask;                              /* data mask */
    uint32          cmask;                              /* control start mask */
    uint32          lnt;                                /* array length */
    uint32          opt;                                /* option control */
    } mmc_ctl_t;

uint16 mmc_rel[VA_NUM_PAG];
uint8 mmc_acc[VA_NUM_PAG];
uint8 mmc_wlk[PA_NUM_PAG];

uint32 mem_sr0[NUM_MUNITS];
uint32 mem_sr1[NUM_MUNITS];

mmc_ctl_t mmc_tab[8] = {
    {  0, 0,     0,         0 },
    {  2, 0x003, 0,         MMC_L_CS1, CPUF_WLK },      /* map 1: 2b locks */
    {  2, 0x003, MMC_M_CS2, MMC_L_CS2, CPUF_MAP },      /* map 2: 2b access ctls */
    {  4, 0x00F, MMC_M_CS3, MMC_L_CS3, CPUF_WLK },      /* map 3: 4b locks */
    {  8, 0x0FF, MMC_M_CS4, MMC_L_CS4, CPUF_MAP },      /* map 4: 8b relocation */
    { 16, 0x7FF, MMC_M_CS5, MMC_L_CS5, CPUF_MAP },      /* map 5: 16b relocation */
    {  0, 0,     0,         0 },
    {  0, 0,     0,         0 }
    };

extern uint32 *R;
extern uint32 *M;
extern uint32 PSW1, PSW2, PSW4;
extern uint32 CC, PSW2_WLK;
extern uint32 stop_op;
extern uint32 cpu_model;
extern uint32 chan_num;
extern UNIT cpu_unit;
extern cpu_var_t cpu_tab[];

uint32 map_reloc (uint32 bva, uint32 acc, uint32 *bpa);
uint32 map_viol (uint32 bva, uint32 bpa, uint32 tr);
t_stat map_reset (DEVICE *dptr);
uint32 map_las (uint32 rn, uint32 bva);

/* Map data structures

   map_dev      map device descriptor
   map_unit     map units
   map_reg      map register list
*/

UNIT map_unit = { UDATA (NULL, 0, 0) };

REG map_reg[] = {
    { BRDATA (REL, mmc_rel, 16, 13, VA_NUM_PAG) },
    { BRDATA (ACC, mmc_acc, 16, 2, VA_NUM_PAG) },
    { BRDATA (WLK, mmc_wlk, 16, 4, PA_NUM_PAG) },
    { BRDATA (SR0, mem_sr0, 16, 32, NUM_MUNITS) },
    { BRDATA (SR1, mem_sr1, 16, 32, NUM_MUNITS) },
    { NULL }
    };

DEVICE map_dev = {
    "MAP", &map_unit, map_reg, NULL,
    1, 16, 16, 1, 16, 32,
    NULL, NULL, &map_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

/* Read and write virtual routines - per length */

uint32 ReadB (uint32 bva, uint32 *dat, uint32 acc)
{
uint32 bpa, sc, tr;

sc = 24 - ((bva & 3) << 3);
if (bva < BVA_REG)                                      /* register access */
    *dat = (R[bva >> 2] >> sc) & BMASK;
else {                                                  /* memory access */
    if ((tr = map_reloc (bva, acc, &bpa)) != 0)         /* relocate addr */
        return tr;
    *dat = (M[bpa >> 2] >> sc) & BMASK;
    }                                                   /* end else memory */
return 0;
}

uint32 ReadH (uint32 bva, uint32 *dat, uint32 acc)
{
uint32 bpa, tr;

if (bva < BVA_REG) {                                    /* register access */
    if (bva & 2)
        *dat = R[bva >> 2] & HMASK;
    else *dat = (R[bva >> 2] >> 16) & HMASK;
    }
else {                                                  /* memory access */
    if ((tr = map_reloc (bva, acc, &bpa)) != 0)         /* relocate addr */
        return tr;
    if (bva & 2)
        *dat = M[bpa >> 2] & HMASK;
    else *dat = (M[bpa >> 2] >> 16) & HMASK;
    }                                                   /* end else memory */
return 0;
}

uint32 ReadW (uint32 bva, uint32 *dat, uint32 acc)
{
uint32 bpa, tr;

if (bva < BVA_REG)                                      /* register access */
    *dat = R[bva >> 2];
else {                                                  /* memory access */
    if ((tr = map_reloc (bva, acc, &bpa)) != 0)         /* relocate addr */
        return tr;
    *dat = M[bpa >> 2];
    }                                                   /* end else memory */
return 0;
}

uint32 ReadD (uint32 bva, uint32 *dat, uint32 *dat1, uint32 acc)
{
uint32 bpa, tr;

if (bva < BVA_REG) {                                    /* register access */
    *dat = R[(bva >> 2) & ~1];                          /* force alignment */
    *dat1 = R[(bva >> 2) | 1];
    }
else {                                                  /* memory access */
    if ((tr = map_reloc (bva, acc, &bpa)) != 0)         /* relocate addr */
        return tr;
    *dat = M[(bpa >> 2) & ~1];                          /* force alignment */
    *dat1 = M[(bpa >> 2) | 1];
    }                                                   /* end else memory */

return 0;
}

uint32 WriteB (uint32 bva, uint32 dat, uint32 acc)
{
uint32 bpa, sc, tr;

sc = 24 - ((bva & 3) << 3);
if (bva < BVA_REG)                                      /* register access */
    R[bva >> 2] = (R[bva >> 2] & ~(BMASK << sc)) | ((dat & BMASK) << sc);
else {                                                  /* memory access */
    if ((tr = map_reloc (bva, acc, &bpa)) != 0)         /* relocate addr */
        return tr;
    M[bpa >> 2] = (M[bpa >> 2] & ~(BMASK << sc)) | ((dat & BMASK) << sc);
    }                                                   /* end else memory */
PSW2 |= PSW2_RA;                                        /* state altered */
return 0;
}

uint32 WriteH (uint32 bva, uint32 dat, uint32 acc)
{
uint32 bpa, tr;

if (bva < BVA_REG) {                                    /* register access */
    if (bva & 2)
        R[bva >> 2] = (R[bva >> 2] & ~HMASK) | (dat & HMASK);
    else R[bva >> 2] = (R[bva >> 2] & HMASK) | ((dat & HMASK) << 16);
    }                                                   /* end if register */
else {                                                  /* memory access */
    if ((tr = map_reloc (bva, acc, &bpa)) != 0)         /* relocate addr */
        return tr;
    if (bva & 2)
        M[bpa >> 2] = (M[bpa >> 2] & ~HMASK) | (dat & HMASK);
    else M[bpa >> 2] = (M[bpa >> 2] & HMASK) | ((dat & HMASK) << 16);
    }                                                   /* end else memory */
PSW2 |= PSW2_RA;                                        /* state altered */
return 0;
}

uint32 WriteW (uint32 bva, uint32 dat, uint32 acc)
{
uint32 bpa, tr;

if (bva < BVA_REG)                                      /* register access */
        R[bva >> 2] = dat & WMASK;
else {                                                  /* memory access */
    if ((tr = map_reloc (bva, acc, &bpa)) != 0)         /* relocate addr */
        return tr;
    M[bpa >> 2] = dat & WMASK;
    }                                                   /* end else memory */
PSW2 |= PSW2_RA;                                        /* state altered */
return 0;
}

uint32 WriteD (uint32 bva, uint32 dat, uint32 dat1, uint32 acc)
{
uint32 bpa, tr;

if (bva < BVA_REG) {                                    /* register access */
    R[(bva >> 2) & ~1] = dat & WMASK;                   /* force alignment */
    R[(bva >> 2) | 1] = dat1 & WMASK;
    }
else {                                                  /* memory access */
    if ((tr = map_reloc (bva, acc, &bpa)) != 0)         /* relocate addr */
        return tr;
    M[(bpa >> 2) & ~1] = dat & WMASK;                   /* force alignment */
    M[(bpa >> 2) | 1] = dat1 & WMASK;
    }                                                   /* end else memory */
PSW2 |= PSW2_RA;                                        /* state altered */
return 0;
}

/* General virtual read for instruction history */

uint32 ReadHist (uint32 bva, uint32 *dat, uint32 *dat1, uint32 acc, uint32 lnt)
{
switch (lnt) {                                          /* case on length */

    case BY:                                            /* byte */
        return ReadB (bva, dat, acc);

    case HW:                                            /* halfword */
        return ReadH (bva, dat, acc);

    case WD:                                            /* word */
        return ReadW (bva, dat, acc);

    case DW:                                            /* doubleword first */
        return ReadD (bva, dat, dat1, acc);
        }                                               /* end case length */

return SCPE_IERR;
}

/* Specialized virtual read and write word routines -
   treats all addresses as memory addresses */

uint32 ReadMemVW (uint32 bva, uint32 *dat, uint32 acc)
{
uint32 bpa;
uint32 tr;

if ((tr = map_reloc (bva, acc, &bpa)) != 0)             /* relocate addr */
    return tr;
*dat = M[bpa >> 2] & WMASK;
return 0;
}

uint32 WriteMemVW (uint32 bva, uint32 dat, uint32 acc)
{
uint32 bpa;
uint32 tr;

if ((tr = map_reloc (bva, acc, &bpa)) != 0)             /* relocate addr */
    return tr;
M[bpa >> 2] = dat & WMASK;
return 0;
}

/* Relocation routine */

uint32 map_reloc (uint32 bva, uint32 acc, uint32 *bpa)
{
if ((acc != 0) && (PSW1 & PSW1_MM)) {                   /* virt, map on? */
    uint32 vpag = BVA_GETPAG (bva);                     /* virt page num */
    *bpa = ((mmc_rel[vpag] << BVA_V_PAG) + BVA_GETOFF (bva)) & BPAMASK;
    if (((PSW1 & PSW1_MS) ||                            /* slave mode? */
         (PSW2 & (PSW2_MA9|PSW2_MA5X0))) &&             /* master prot? */
        (mmc_acc[vpag] >= acc))                         /* access viol? */
       return map_viol (bva, *bpa, TR_MPR);
    }
else *bpa = bva;                                        /* no, physical */
if ((acc == VW) && PSW2_WLK) {                          /* write check? */
    uint32 ppag = BPA_GETPAG (*bpa);                    /* phys page num */
    if (PSW2_WLK && mmc_wlk[ppag] &&                    /* lock, key != 0 */
        (PSW2_WLK != mmc_wlk[ppag]))                    /* lock != key? */
        return map_viol (bva, *bpa, TR_WLK);
    }
if (BPA_IS_NXM (*bpa))                                  /* memory exist? */
    return TR_NXM;                                      /* don't set TSF */
return 0;
}

/* Memory management error */

uint32 map_viol (uint32 bva, uint32 bpa, uint32 tr)
{
uint32 vpag = BVA_GETPAG (bva);                         /* virt page num */

if (QCPU_S9)                                            /* Sigma 9? */
    PSW2 = (PSW2 & ~PSW2_TSF) | (vpag << PSW2_V_TSF);   /* save address */
PSW4 = bva >> 2;                                        /* 5X0 address */
if ((tr == TR_WLK) && !QCPU_5X0)                        /* wlock on S5-9? */
    tr = TR_MPR;                                        /* mem prot trap */
if (BPA_IS_NXM (bpa))                                   /* also check NXM */
    tr |= TR_NXM;                                       /* on MPR or WLK */
return tr;
}

/* Physical byte access routines */

uint32 ReadPB (uint32 ba, uint32 *wd)
{
uint32 sc;

ba = ba & BPAMASK;
if (BPA_IS_NXM (ba))
    return TR_NXM;
sc = 24 - ((ba & 3) << 3);
*wd = (M[ba >> 2] >> sc) & BMASK;
return 0;
}

uint32 WritePB (uint32 ba, uint32 wd)
{
uint32 sc;

ba = ba & BPAMASK;
if (BPA_IS_NXM (ba))
    return TR_NXM;
sc = 24 - ((ba & 3) << 3);
M[ba >> 2] = (M[ba >> 2] & ~(BMASK << sc)) | ((wd & BMASK) << sc);
return 0;
}

/* Physical word access routines */

uint32 ReadPW (uint32 pa, uint32 *wd)
{
pa = pa & cpu_tab[cpu_model].pamask;
if (MEM_IS_NXM (pa))
    return TR_NXM;
*wd = M[pa];
return 0;
}

uint32 WritePW (uint32 pa, uint32 wd)
{
pa = pa & cpu_tab[cpu_model].pamask;
if (MEM_IS_NXM (pa))
    return TR_NXM;
M[pa] = wd;
return 0;
}

/* LRA - load real address (extended memory systems only) */

uint32 map_lra (uint32 rn, uint32 IR)
{
uint32 lnt, bva, bpa, vpag, ppag;
uint32 tr;

lnt = CC >> 2;                                          /* length */
CC = 0;                                                 /* clear */
if ((tr = Ea (IR, &bva, VR, lnt)) != 0) {               /* get eff addr */
    if (tr == TR_NXM)                                   /* NXM trap? */
        CC = CC1|CC2;
    R[rn] = bva >> 2;                                   /* fails */
    }
else if (bva < BVA_REG) {                               /* reg ref? */
    CC = CC1|CC2;
    R[rn] = bva >> 2;                                   /* fails */
    }
else {
    vpag = BVA_GETPAG (bva);                            /* virt page num */
    bpa = ((mmc_rel[vpag] << BVA_V_PAG) + BVA_GETOFF (bva)) & BPAMASK;
    ppag = BPA_GETPAG (bpa);                            /* phys page num */
    if (MEM_IS_NXM (bpa))                               /* NXM? */
        CC = CC1|CC2;
    R[rn] = (QCPU_S9? (mmc_wlk[ppag] << 24): 0) |       /* result */
        (bpa >> lnt);
    CC |= mmc_acc[vpag];                                /* access prot */
    }
return 0;
}

/* MMC - load memory map control */

uint32 map_mmc (uint32 rn, uint32 map)
{
uint32 tr;
uint32 wd, i, map_width, maps_per_word, map_cmask, cs;

map_width = mmc_tab[map].width;                         /* width in bits */
maps_per_word = 32 / map_width;
if (map != 1)                                           /* maps 2-7? */
    map_cmask = mmc_tab[map].cmask;                     /* std ctl mask */
else map_cmask = cpu_tab[cpu_model].mmc_cm_map1;        /* model based */
if ((map_width == 0) ||                                 /* validate map */
    ((cpu_unit.flags & mmc_tab[map].opt) == 0) ||
    ((map == 3) && !QCPU_5X0) ||
    ((map == 5) && !QCPU_BIGM)) {
    if (QCPU_S89_5X0)                                   /* S89, 5X0 trap */
        return TR_INVMMC;
    return stop_op? STOP_ILLEG: 0;
    }
do {
    cs = (R[rn|1] >> MMC_V_CS) & map_cmask;             /* ptr into map */
    if ((tr = ReadW ((R[rn] << 2) & BVAMASK, &wd, VR)) != 0)
        return tr;
    for (i = 0; i < maps_per_word; i++) {               /* loop thru word */
        wd = ((wd << map_width) | (wd >> (32 - map_width))) & WMASK;
        switch (map) {

        case 1: case 3:                                 /* write locks */
            mmc_wlk[cs] = wd & mmc_tab[map].dmask;
            break;

        case 2:                                         /* access ctls */
            mmc_acc[cs] = wd & mmc_tab[map].dmask;
            break;

        case 4: case 5:                                 /* relocation */
            mmc_rel[cs] = wd & mmc_tab[map].dmask;
            break;
            };
        cs = (cs + 1) % mmc_tab[map].lnt;               /* incr mod lnt */
        }                                               /* end for */
    R[rn] = (R[rn] + 1) & WMASK;                        /* incr mem ptr */
    R[rn|1] = (R[rn|1] & ~(MMC_CNT | (map_cmask << MMC_V_CS))) |
        (((MMC_GETCNT (R[rn|1]) - 1) & MMC_M_CNT) << MMC_V_CNT) |
        ((cs & map_cmask) << MMC_V_CS);
    } while (MMC_GETCNT (R[rn|1]) != 0);
return SCPE_OK;
}

/* LAS instruction (reused by LMS), without condition code settings */

uint32 map_las (uint32 rn, uint32 bva)
{
uint32 opnd, tr;

if ((bva < (RF_NUM << 2)) && QCPU_5X0)                  /* on 5X0, reg */
    ReadW (bva, &opnd, VR);                             /* refs ignored */
else {                                                  /* go to mem */
    if ((tr = ReadMemVW (bva, &opnd, VR)) != 0)         /* read word */
        return tr;
   if ((tr = WriteMemVW (bva, opnd | WSIGN, VW)) != 0)  /* set bit */
        return tr;
   }
R[rn] = opnd;                                           /* store */
return 0;
}

/* Load memory status */

uint32 map_lms (uint32 rn, uint32 bva)
{
uint32 tr, wd, low, ppag;
uint32 memu = (bva >> 2) / CPU_MUNIT_SIZE;

if (CC == 0)                                            /* LAS */
    return map_las (rn, bva);
if (CC == 1) {                                          /* read no par */
    if ((tr = ReadW (bva, &wd, PH)) != 0)
        return tr;
    R[rn] = wd;
    for (CC = CC3; wd != 0; CC ^= CC3) {                /* calc odd par */
        low = wd & -((int32) wd);
        wd = wd & ~low;
        }
    return 0;
    }

ppag = BPA_GETPAG (bva);                                /* phys page num */
wd = mem_sr0[memu];                                     /* save sr0 */
if (QCPU_S89)
    switch (CC) {                                       /* Sigma 8-9 */
    case 0x2:                                           /* read bad par */
        if ((tr = ReadW (bva, &wd, VR)) != 0)
            return tr;
        R[rn] = wd;
        break;
    case 0x7:                                           /* set margins */
        mem_sr1[memu] = S89_SR1_FIXED |
            ((memu & S89_SR1_M_MEMU) << S89_SR1_V_MEMU) |
            ((R[rn] & S89_SR1_MARG) >> S89_SR1_MAROFF);
        break;
    case 0xB:                                           /* read sr0, clr */
        mem_sr0[memu] = mem_sr1[memu] = 0;
    case 0x8:                                           /* read sr0 */
        R[rn] = (wd & S89_SR0_RD) |
            (((1u << (chan_num + 1)) - 1) << (S89_SR0_V_PORTS - (chan_num + 1)));
            break;
    case 0x9:                                           /* read sr1 */
        R[rn] = mem_sr1[memu];
        break;
    case 0xA: case 0xE:                                 /* read sr2 */
        R[rn] = 0;
        break;
    case 0xF:                                           /* clear word */
        return WriteW (bva, 0, VW);
        break;
    default:
        mem_sr0[memu] |= S89_SR0_BADLMS;
        break;
        }
else switch (CC) {                                      /* 5X0 */
    case 0x2:                                           /* clear word */
        return WriteW (bva, 0, VW);
    case 0x6:                                           /* read wlk */
        R[rn] = (mmc_wlk[ppag & ~1] << 4) | mmc_wlk[ppag | 1];
        break;
    case 0x7:                                           /* write wlk */
        mmc_wlk[ppag & ~1] = (R[rn] >> 4) & 0xF;
        mmc_wlk[ppag | 1] = R[rn] & 0xF;
        break;
    case 0xC:                                           /* read sr0, clr */
        mem_sr0[memu] = 0;
    case 0x8:                                           /* read sr0 */
        R[rn] = S5X0_SR0_FIXED | (wd & S5X0_SR0_RD) |
            (((1u << (chan_num + 1)) - 1) << (S5X0_SR0_V_PORTS - (chan_num + 1)));
        break;
    case 0xA:                                           /* read sr1 */
        R[rn] = S5X0_SR1_FIXED |
            ((memu & S5X0_SR1_M_MEMU) << S5X0_SR1_V_MEMU) |
            (memu << S5X0_SR1_V_SA);
        break;
    case 0xE:                                           /* trash mem */
        return WriteW (bva, R[rn] & ~0xFF, VW);
    default:
       mem_sr0[memu] |= S5X0_SR0_BADLMS;
       break;
        }
return 0;
}

/* Device reset */

t_stat map_reset (DEVICE *dptr)
{
uint32 i;

for (i = 0; i < VA_NUM_PAG; i++) {                      /* clear mmc arrays */
    mmc_rel[i] = 0;
    mmc_acc[i] = 0;
    }
for (i = 0; i < PA_NUM_PAG; i++)
    mmc_wlk[i] = 0;
return SCPE_OK;
}
