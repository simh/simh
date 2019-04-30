/* vax440_sysdev.c: MicroVAX 4000-60 system-specific logic

   Copyright (c) 2019, Matt Burke
   This module incorporates code from SimH, Copyright (c) 1998-2008, Robert M Supnik

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
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name(s) of the author(s) shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author(s).

   This module contains the MicroVAX 4000-60 system-specific registers and devices.

   sysd         system devices
*/

#include "vax_defs.h"
#include "sim_ether.h"
#include <time.h>

#ifdef DONT_USE_INTERNAL_ROM
#if defined (VAX_46)
#define BOOT_CODE_FILENAME "ka46a.bin"
#elif defined (VAX_47)
#define BOOT_CODE_FILENAME "ka47a.bin"
#elif defined (VAX_48)
#define BOOT_CODE_FILENAME "ka48a.bin"
#endif
#else /* !DONT_USE_INTERNAL_ROM */
#if defined (VAX_46)
#include "vax_ka46a_bin.h" /* Defines BOOT_CODE_FILENAME and BOOT_CODE_ARRAY, etc */
#elif defined (VAX_47)
#include "vax_ka47a_bin.h" /* Defines BOOT_CODE_FILENAME and BOOT_CODE_ARRAY, etc */
#elif defined (VAX_48)
#include "vax_ka48a_bin.h" /* Defines BOOT_CODE_FILENAME and BOOT_CODE_ARRAY, etc */
#endif
#endif /* DONT_USE_INTERNAL_ROM */


t_stat vax460_boot (int32 flag, CONST char *ptr);

/* Special boot command, overrides regular boot */

CTAB vax460_cmd[] = {
    { "BOOT", &vax460_boot, RU_BOOT,
      "bo{ot}                   boot simulator\n", NULL, &run_cmd_message },
    { NULL }
    };

/* KA460 configuration & test register */

#define CFGT_MEM        0x003F                          /* memory option */
#define CFGT_V_VID      6
#define CFGT_M_VID      0x3
#define CFGT_VID        (CFGT_M_VID << CFGT_V_VID)      /* video option */
#define CFGT_L3C        0x0100                          /* line 3 console */
#define CFGT_V_SIM      9
#define CFGT_M_SIM      0x3F
#define CFGT_SIM        (CFGT_M_SIM << CFGT_V_SIM)      /* SIMM type */

/* KA460 parity control register - checked */

#define PARCTL_CPEN     0x00000001                      /* system wide parity */
#define PARCTL_REV      0x000000F0
#define PARCTL_NPEN     0x00000100                      /* NI parity en */
#define PARCTL_NPERR    0x00000200                      /* NI parity err */
#define PARCTL_NMAP     0x00000400
#define PARCTL_SPEN     0x00010000                      /* SCSI parity en */
#define PARCTL_SPERR    0x00020000                      /* SCSI parity err */
#define PARCTL_SMAP     0x00040000
#define PARCTL_INVEN    0x01000000
#define PARCTL_AGS      0x02000000                      /* AG stall */
#define PARCTL_ADP      0x80000000
#define PARCTL_RD       0x830707F1
#define PARCTL_WR       0x01010101

/* KA460 secondary cache control register - checked */

#define SCCR_CENA       0x00000001
#define SCCR_TERR       0x00000002
#define SCCR_WBMODE     0x00000080
#define SCCR_SPECIO     0x00000100                      /* SPECIO inhibit */
#define SCCR_FONFUL     0x00000200
#define SCCR_REV        0x00000400
#define SCCR_RD         0x00000783
#define SCCR_WR         0x00000101

/* KA460 Memory system error register */

#define MSER_PE         0x00000001                      /* Parity Enable */
#define MSER_WWP        0x00000002                      /* Write Wrong Parity */
#define MSER_PER        0x00000040                      /* Parity Error */
#define MSER_MCD0       0x00000100                      /* Mem Code 0 */
#define MSER_MBZ        0xFFFFFEBC
#define MSER_RD         (MSER_PE | MSER_WWP | MSER_PER | \
                         MSER_PER | MSER_MCD0)
#define MSER_WR         (MSER_PE | MSER_WWP)
#define MSER_RS         (MSER_PER)

/* KA460 memory error address reg */

#define MEAR_FAD        0x00007FFF                      /* failing addr */
#define MEAR_RD         (MEAR_FAD)

#define ROM_VEC         0x8                             /* ROM longword for first device vector */

/* DMA map registers */

#define DMANMAPR        32768                           /* number of map reg */
#define DMAMAP_VLD      0x80000000                      /* valid */
#if defined (VAX_46) || defined (VAX_47)
#define DMAMAP_PAG      0x0003FFFF                      /* mem page */
#elif defined (VAX_48)
#define DMAMAP_PAG      0x0000FFFF                      /* mem page */
#endif

extern int32 tmr_int;
extern DEVICE lk_dev, vs_dev;
extern uint32 *rom;

uint32 *isdn = NULL;                                    /* ISDN/audio registers */
uint32 *invfl = NULL;                                   /* invalidate filter */
uint32 *cache2ds = NULL;                                /* cache 2 data store */
uint32 *cache2ts = NULL;                                /* cache 2 tag store */
int32 conisp, conpc, conpsl;                            /* console reg */
int32 ka_hltcod = 0;                                    /* KA460 halt code */
int32 ka_mapbase = 0;                                   /* KA460 map base */
int32 ka_cfgtst = 0x90;                                 /* KA460 config/test */
int32 ka_led = 0;                                       /* KA460 diag display */
int32 ka_parctl = 0xF0;                                 /* KA460 parity control */
int32 mem_cnfg = 0;
int32 CADR = 0;                                         /* cache disable reg */
int32 SCCR = 0;                                         /* secondary cache control */
int32 sys_model = 0;                                    /* MicroVAX or VAXstation */
int32 int_req[IPL_HLVL] = { 0 };                        /* interrupt requests */
int32 int_mask = 0;                                     /* interrupt mask */
uint32 tmr_tir = 0;                                     /* curr interval */

t_stat sysd_reset (DEVICE *dptr);
const char *sysd_description (DEVICE *dptr);
int32 ka_rd (int32 pa);
void ka_wr (int32 pa, int32 val, int32 lnt);
int32 con_halt (int32 code, int32 cc);

extern int32 iccs_rd (void);
extern int32 rom_rd (int32 pa);
extern int32 nvr_rd (int32 pa);
extern int32 nar_rd (int32 pa);
extern int32 dz_rd (int32 pa);
extern int32 or_rd (int32 pa);
extern int32 rz_rd (int32 pa);
extern int32 xs_rd (int32 pa);
extern void iccs_wr (int32 dat);
extern void nvr_wr (int32 pa, int32 val, int32 lnt);
extern void dz_wr (int32 pa, int32 val, int32 lnt);
extern void rz_wr (int32 pa, int32 val, int32 lnt);
extern void xs_wr (int32 pa, int32 val, int32 lnt);

UNIT sysd_unit = { UDATA (NULL, 0, 0) };

REG sysd_reg[] = {
    { HRDATAD (CONISP,  conisp,     32, "console ISP") },
    { HRDATAD (CONPC,   conpc,      32, "console PD") },
    { HRDATAD (CONPSL,  conpsl,     32, "console PSL") },
    { HRDATAD (CFGTST,  ka_cfgtst,  16, "KA460 config/test") },
    { HRDATAD (HLTCOD,  ka_hltcod,  32, "KA460 halt code") },
    { HRDATAD (MAPBASE, ka_mapbase, 32, "KA460 DMA map base") },
    { HRDATAD (LED,     ka_led,     16, "KA460 diag display") },
    { HRDATAD (PARCTL,  ka_parctl,  32, "KA460 parity control") },
    { NULL }
    };

MTAB sysd_mod[] = {
    { 0 },
    };

DEVICE sysd_dev = {
    "SYSD", &sysd_unit, sysd_reg, sysd_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &sysd_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, 
    &sysd_description
    };

/* Find highest priority outstanding interrupt */

int32 eval_int (void)
{
int32 ipl = PSL_GETIPL (PSL);
int32 i, t;

static const int32 sw_int_mask[IPL_SMAX] = {
    0xFFFE, 0xFFFC, 0xFFF8, 0xFFF0,                     /* 0 - 3 */
    0xFFE0, 0xFFC0, 0xFF80, 0xFF00,                     /* 4 - 7 */
    0xFE00, 0xFC00, 0xF800, 0xF000,                     /* 8 - B */
    0xE000, 0xC000, 0x8000                              /* C - E */
    };

if (hlt_pin)                                            /* hlt pin int */
    return IPL_HLTPIN;
if ((ipl < IPL_CLK) && tmr_int)                         /* clock int */
    return IPL_CLK;
if (ipl < IPL_HW) {                                     /* chk hwre int */
    if (int_req[0] & int_mask)
        return IPL_HW;
    }
if (ipl >= IPL_SMAX)                                    /* ipl >= sw max? */
    return 0;
if ((t = SISR & sw_int_mask[ipl]) == 0)                 /* eligible req */
    return 0;
for (i = IPL_SMAX; i > ipl; i--) {                      /* check swre int */
    if ((t >> i) & 1)                                   /* req != 0? int */
        return i;
    }
return 0;
}

/* Return vector for highest priority hardware interrupt at IPL lvl */

int32 get_vector (int32 lvl)
{
int32 i;
int32 int_unmask = int_req[0] & int_mask;

if (lvl == IPL_CLK) {                                   /* clock? */
    tmr_int = 0;                                        /* clear req */
    return SCB_INTTIM;                                  /* return vector */
    }
if (lvl > IPL_HMAX) {                                   /* error req lvl? */
    ABORT (STOP_UIPL);                                  /* unknown intr */
    }
for (i = 7; int_unmask && (i >= 0); i--) {
    if ((int_unmask >> i) & 1) {
        int_req[0] = int_req[0] & ~(1u << i);
        return rom[ROM_VEC + i] & 0x3FF;                /* get vector from ROM */
        }
    }
return 0;
}

/* Map an address via the translation map */

t_bool dma_map_addr (uint32 da, uint32 *ma)
{
int32 dblk = (da >> VA_V_VPN);                          /* DMA blk */
if (dblk <= DMANMAPR) {
    int32 dmap = ReadL (ka_mapbase + (dblk << 2));
    if (dmap & DMAMAP_VLD) {                            /* valid? */
        *ma = ((dmap & DMAMAP_PAG) << VA_V_VPN) + VA_GETOFF (da);
        if (ADDR_IS_MEM (*ma))                          /* legit addr */
            return TRUE;
        }
    }
return FALSE;
}

/* DMA buffer routines, aligned access

   Map_ReadB    -       fetch byte buffer from memory
   Map_ReadW    -       fetch word buffer from memory
   Map_WriteB   -       store byte buffer into memory
   Map_WriteW   -       store word buffer into memory
*/

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf)
{
int32 i;
uint32 ma, dat;

if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = ma = 0; i < bc; i++, buf++) {              /* by bytes */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma))            /* inv or NXM? */
                return (bc - i);
            }
        *buf = ReadB (ma);
        ma = ma + 1;
        }
    }
else {
    for (i = ma = 0; i < bc; i = i + 4, buf++) {        /* by longwords */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma))            /* inv or NXM? */
                return (bc - i);
            }
        dat = ReadL (ma);                               /* get lw */
        *buf++ = dat & BMASK;                           /* low 8b */
        *buf++ = (dat >> 8) & BMASK;                    /* next 8b */
        *buf++ = (dat >> 16) & BMASK;                   /* next 8b */
        *buf = (dat >> 24) & BMASK;
        ma = ma + 4;
        }
    }
return 0;
}

int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf)
{
int32 i;
uint32 ma,dat;

ba = ba & ~01;
bc = bc & ~01;
if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = ma = 0; i < bc; i = i + 2, buf++) {        /* by words */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma))            /* inv or NXM? */
                return (bc - i);
            }
        *buf = ReadW (ma);
        ma = ma + 2;
        }
    }
else {
    for (i = ma = 0; i < bc; i = i + 4, buf++) {        /* by longwords */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma))            /* inv or NXM? */
                return (bc - i);
            }
        dat = ReadL (ma);                               /* get lw */
        *buf++ = dat & WMASK;                           /* low 16b */
        *buf = (dat >> 16) & WMASK;                     /* high 16b */
        ma = ma + 4;
        }
    }
return 0;
}

int32 Map_WriteB (uint32 ba, int32 bc, uint8 *buf)
{
int32 i;
uint32 ma, dat;

if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = ma = 0; i < bc; i++, buf++) {              /* by bytes */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma))            /* inv or NXM? */
                return (bc - i);
            }
        WriteB (ma, *buf);
        ma = ma + 1;
        }
    }
else {
    for (i = ma = 0; i < bc; i = i + 4, buf++) {        /* by longwords */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma))            /* inv or NXM? */
                return (bc - i);
            }
        dat = (uint32) *buf++;                          /* get low 8b */
        dat = dat | (((uint32) *buf++) << 8);           /* merge next 8b */
        dat = dat | (((uint32) *buf++) << 16);          /* merge next 8b */
        dat = dat | (((uint32) *buf) << 24);            /* merge hi 8b */
        WriteL (ma, dat);                               /* store lw */
        ma = ma + 4;
        }
    }
return 0;
}

int32 Map_WriteW (uint32 ba, int32 bc, uint16 *buf)
{
int32 i;
uint32 ma, dat;

ba = ba & ~01;
bc = bc & ~01;
if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = ma = 0; i < bc; i = i + 2, buf++) {        /* by words */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma))            /* inv or NXM? */
                return (bc - i);
            }
        WriteW (ma, *buf);
        ma = ma + 2;
        }
    }
else {
    for (i = ma = 0; i < bc; i = i + 4, buf++) {        /* by longwords */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma))            /* inv or NXM? */
                return (bc - i);
            }
        dat = (uint32) *buf++;                          /* get low 16b */
        dat = dat | (((uint32) *buf) << 16);            /* merge hi 16b */
        WriteL (ma, dat);                               /* store lw */
        ma = ma + 4;
        }
    }
return 0;
}

int32 isdn_rd (int32 pa)
{
int32 rg = (pa - 0x200D0000) >> 2;
return isdn[rg];
}

void isdn_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - 0x200D0000) >> 2;
if (lnt < L_LONG) {                                     /* byte or word? */
    int32 sc = (pa & 3) << 3;                           /* merge */
    int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
    isdn[rg] = ((val & mask) << sc) | (isdn[rg] & ~(mask << sc));
    }
else isdn[rg] = val;
}

int32 cfg_rd (int32 pa)
{
int32 val = ka_cfgtst;
t_addr mem = MEMSIZE;
uint32 sc;

#if defined (VAX_46) || defined (VAX_47)
mem -= (1u << 23);                                      /* 8MB on system board */
for (sc = 0; mem > 0; sc+=2) {
    if (mem >= (1u << 25)) {
        val |= (0x3 << sc);                             /* add two 16MB SIMMs */
        val |= (0x3 << (sc + CFGT_V_SIM));
        mem -= (1u << 25);
        }
    else {
        val |= (0x3 << sc);                             /* add two 4MB SIMMs */
        val = (val & ~CFGT_SIM) | ((val & CFGT_SIM) << 2); /* must be installed before 16MB SIMMs */
        mem -= (1u << 23);
        }
    }
#elif defined (VAX_48)
val |= 0x1;                                             /* bit zero always set */
for (sc = 1; mem > 0; sc++) {
    val |= (0x1 << sc);                                 /* add two 4MB SIMMs */
    mem -= (1u << 23);
    }
#endif
return val;
}

void ioreset_wr (int32 pa, int32 val, int32 lnt)
{
reset_all (6);
}

/* Read KA460 specific IPR's */

int32 ReadIPR (int32 rg)
{
int32 val;

switch (rg) {

    case MT_ICCS:                                       /* ICCS */
        val = iccs_rd ();
        break;

    case MT_MCESR:                                      /* MCESR (not impl) */
        val = 0;
        break;

    case MT_ACCS:                                       /* ACCS (not impl) */
        val = 0;
        break;

    case MT_CONISP:                                     /* console ISP */
        val = conisp;
        break;

    case MT_CONPC:                                      /* console PC */
        val = conpc;
        break;

    case MT_CONPSL:                                     /* console PSL */
        val = conpsl;
        break;

    case MT_CADR:                                       /* CADR */
        val = CADR & 0xFF;
        break;

    case MT_CAER:                                       /* CAER */
        val = 0;
        break;

    case MT_PCTAG:                                      /* PCTAG (not impl) */
        val = 0;
        break;

    case MT_PCIDX:                                      /* PCIDX (not impl) */
        val = 0;
        break;

    case MT_PCERR:                                      /* PCERR (not impl) */
        val = 0;
        break;

    case MT_PCSTS:                                      /* PCSTS (not impl) */
        val = 0;
        break;

    case MT_SID:                                        /* SID */
        val = VAX4X_SID | VAX4X_UREV;
        break;

    default:
        RSVD_OPND_FAULT(ReadIPR);
        }

return val;
}

/* Write KA460 specific IPR's */

void WriteIPR (int32 rg, int32 val)
{
switch (rg) {

    case MT_ICCS:                                       /* ICCS */
        iccs_wr (val);
        break;

    case MT_NICR:                                       /* NICR (not impl) */
        break;

    case MT_MCESR:                                      /* MCESR (not impl) */
        break;

    case MT_ACCS:                                       /* ACCS (not impl) */
        break;

    case MT_CONISP:                                     /* console ISP */
        conisp = val;
        break;

    case MT_CONPC:                                      /* console PC */
        conpc = val;
        break;

    case MT_CONPSL:                                     /* console PSL */
        conpsl = val;
        break;

    case MT_CADR:                                       /* CADR */
        CADR = (val & CADR_RW) | CADR_MBO;
        break;

    case MT_CAER:                                       /* CAER */
        break;

    case MT_PCTAG:                                      /* PCTAG (not impl) */
        break;

    case MT_PCIDX:                                      /* PCIDX (not impl) */
        break;

    case MT_PCERR:                                      /* PCERR (not impl) */
        break;

    case MT_PCSTS:                                      /* PCSTS (not impl) */
        break;

    default:
        RSVD_OPND_FAULT(WriteIPR);
        }

return;
}

int32 invfl_rd (int32 pa)
{
int32 rg = (pa - 0x20200000) >> 2;
return invfl[rg];
}

void invfl_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - 0x20200000) >> 2;
if (lnt < L_LONG) {                                     /* byte or word? */
    int32 sc = (pa & 3) << 3;                           /* merge */
    int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
    invfl[rg] = ((val & mask) << sc) | (invfl[rg] & ~(mask << sc));
    }
else invfl[rg] = val;
}

int32 cache2ds_rd (int32 pa)
{
int32 rg = (pa - 0x08000000) >> 2;
return cache2ds[rg];
}

void cache2ds_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - 0x08000000) >> 2;
if (lnt < L_LONG) {                                     /* byte or word? */
    int32 sc = (pa & 3) << 3;                           /* merge */
    int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
    cache2ds[rg] = ((val & mask) << sc) | (cache2ds[rg] & ~(mask << sc));
    }
else cache2ds[rg] = val;
}

int32 cache2ts_rd (int32 pa)
{
int32 rg = (pa - 0x22000000) >> 2;
return cache2ts[rg];
}

void cache2ts_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - 0x22000000) >> 2;
if (lnt < L_LONG) {                                     /* byte or word? */
    int32 sc = (pa & 3) << 3;                           /* merge */
    int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
    cache2ts[rg] = ((val & mask) << sc) | (cache2ts[rg] & ~(mask << sc));
    }
else cache2ts[rg] = val;
}

int32 dma_map_rd (int32 pa)
{
int32 rg = (pa - DMABASE);
int32 val = ReadL (ka_mapbase + rg);
return val;
}

void dma_map_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - DMABASE);
WriteL (ka_mapbase + rg, val);
}

int32 sccr_rd (int32 pa)
{
return SCCR & SCCR_RD;
}

void sccr_wr (int32 pa, int32 val, int32 lnt)
{
if (lnt < L_LONG) {                                     /* byte or word? */
    int32 sc = (pa & 3) << 3;                           /* merge */
    int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
    SCCR = (((val & mask) << sc) | (SCCR & ~(mask << sc))) & SCCR_WR;
    }
else SCCR = val & SCCR_WR;
}

int32 memrg_rd (int32 pa)
{
int32 rg = (pa - 0x20101800) >> 2;

switch (rg) {

    case 0:                                             /* MEMCNFG */
        return mem_cnfg;

    case 1:                                             /* MEMSTAT */
        return 0;

    case 2:                                             /* MEMCUR */
        return 0;

    case 3:                                             /* MEMERR */
        return 0;
        }

return 0;
}

void memrg_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - 0x20101800) >> 2;

switch (rg) {

    case 0:                                             /* MEMCNFG */
        mem_cnfg = val;
        break;

    case 1:                                             /* MEMSTAT - RO? */
        break;

    case 2:                                             /* MEMCUR - RO? */
        break;

    case 3:                                             /* MEMERR - RO? */
        break;
        }
return;
}

int32 null_rd (int32 pa)
{
return 0;
}

void null_wr (int32 pa, int32 val, int32 lnt)
{
}

/* Read/write I/O register space

   These routines are the 'catch all' for address space map.  Any
   address that doesn't explicitly belong to memory, I/O, or ROM
   is given to these routines for processing.
*/

struct reglink {                                        /* register linkage */
    uint32      low;                                    /* low addr */
    uint32      high;                                   /* high addr */
    int32       (*read)(int32 pa);                      /* read routine */
    void        (*write)(int32 pa, int32 val, int32 lnt); /* write routine */
    };

struct reglink regtable[] = {
    { DMABASE, DMABASE+DMASIZE, &dma_map_rd, &dma_map_wr },
    { 0x200D0000, 0x200D4000, &isdn_rd, &isdn_wr },     /* ISDN/audio */
    { XSBASE, XSBASE+XSSIZE, &xs_rd, &xs_wr },
    { DZBASE, DZBASE+DZSIZE, &dz_rd, dz_wr },           /* needs extra register */
    { RZBASE, RZBASE+RZSIZE, &rz_rd, rz_wr },
    { ORBASE, ORBASE+ORSIZE, &or_rd, NULL },
    { NARBASE, NARBASE+NARSIZE, &nar_rd, NULL },
    { CFGBASE, CFGBASE+CFGSIZE, &cfg_rd, &ioreset_wr },
    { 0x20200000, 0x20220000, &invfl_rd, &invfl_wr },   /* Invalidate filter */
    { 0x23000000, 0x23000004, &sccr_rd, &sccr_wr },
    { 0x36800000, 0x36800004, &null_rd, &null_wr },     /* Turbo CSR */
    { 0x08000000, 0x08040000, &cache2ds_rd, &cache2ds_wr },  /* Cache second data store */
    { 0x22000000, 0x22040000, &cache2ts_rd, &cache2ts_wr },  /* Cache second tag store - should be at 0x2D000000? */
    { 0x20101800, 0x20101810, &memrg_rd, &memrg_wr },
    { 0x20101A00, 0x20102000, NULL, &null_wr },         /* invalidate single - check size */
    { ROMBASE, ROMBASE+ROMSIZE, &rom_rd, NULL },
    { NVRBASE, NVRBASE+NVRSIZE, &nvr_rd, &nvr_wr },
    { KABASE, KABASE+KASIZE, &ka_rd, &ka_wr },          /* needs extending */
    { 0, 0, NULL, NULL }
    };

/* ReadReg - read register space

   Inputs:
        pa      =       physical address
        lnt     =       length (BWLQ) - ignored
   Output:
        longword of data
*/

int32 ReadReg (uint32 pa, int32 lnt)
{
struct reglink *p;

for (p = &regtable[0]; p->low != 0; p++) {
    if ((pa >= p->low) && (pa < p->high) && p->read)
        return p->read (pa);
    }
return 0xFFFFFFFF;
}

/* ReadRegU - read register space, unaligned

   Inputs:
        pa      =       physical address
        lnt     =       length in bytes (1, 2, or 3)
   Output:
        returned data, not shifted
*/

int32 ReadRegU (uint32 pa, int32 lnt)
{
return ReadReg (pa & ~03, L_LONG);
}

/* WriteReg - write register space

   Inputs:
        pa      =       physical address
        val     =       data to write, right justified in 32b longword
        lnt     =       length (BWLQ)
   Outputs:
        none
*/

void WriteReg (uint32 pa, int32 val, int32 lnt)
{
struct reglink *p;

for (p = &regtable[0]; p->low != 0; p++) {
    if ((pa >= p->low) && (pa < p->high) && p->write) {
        p->write (pa, val, lnt);  
        return;
        }
    }
return;
}

/* WriteRegU - write register space, unaligned

   Inputs:
        pa      =       physical address
        val     =       data to write, right justified in 32b longword
        lnt     =       length (1, 2, or 3)
   Outputs:
        none
*/

void WriteRegU (uint32 pa, int32 val, int32 lnt)
{
int32 sc = (pa & 03) << 3;
int32 dat = ReadReg (pa & ~03, L_LONG);

dat = (dat & ~(insert[lnt] << sc)) | ((val & insert[lnt]) << sc);
WriteReg (pa & ~03, dat, L_LONG);
return;
}

/* KA460 registers */

int32 ka_rd (int32 pa)
{
int32 rg = (pa - KABASE) >> 2;

switch (rg) {

    case 0:                                             /* halt code */
        return ka_hltcod;

    case 2:                                             /* DMA map base */
        return ka_mapbase;

    case 3:                                             /* int req, int mask */
        return ((int_req[0] & BMASK) << 24) | \
            (int_mask & BMASK);

    case 5:                                             /* parity ctl */
        /* Schip revision in bits (7:4) */
        return ka_parctl & PARCTL_RD;

    case 7:                                             /* diag timer */
        tmr_tir = (tmr_tir + 5) & 0xFFFF;
        return (tmr_tir << 16) | tmr_tir;
        }

return 0;
}

void ka_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - KABASE) >> 2;

switch (rg) {

    case 0:                                             /* halt code */
        ka_hltcod = val;
        break;

    case 2:                                             /* DMA map base */
        ka_mapbase = val;
        break;

    case 3:
        switch (pa & 3) {
            case 0:                                     /* int mask */
                int_mask = val & BMASK;
                SET_IRQL;
                break;

            case 1:
            case 2:
                break;

            case 3:                                     /* int clear */
                int_req[0] = int_req[0] & ~(val & BMASK);
                break;
                }
        break;

    case 4:                                             /* LED */
        ka_led = val;
        break;

    case 5:                                             /* parity ctl */
        ka_parctl = (ka_parctl & ~PARCTL_WR) | (val & PARCTL_WR);
        break;

    case 7:                                             /* diag timer */
        tmr_tir = (val >> 16);
        }
return;
}

/* Machine check */

int32 machine_check (int32 p1, int32 opc, int32 cc, int32 delta)
{
int32 p2, acc;

if (in_ie) {
    in_ie = 0;
    return con_halt (CON_DBLMCK, cc);                   /* double machine check */
    }
if (p1 & 0x80)                                          /* mref? set v/p */
    p1 = p1 + mchk_ref;
p2 = mchk_va + 4;                                       /* save vap */
cc = intexc (SCB_MCHK, cc, 0, IE_SVE);                  /* take exception */
acc = ACC_MASK (KERN);                                  /* in kernel mode */
in_ie = 1;
SP = SP - 28;                                           /* push 7 words */
Write (SP, 24, L_LONG, WA);                             /* # bytes */
Write (SP + 4, p1, L_LONG, WA);                         /* mcheck type */
Write (SP + 8, p2, L_LONG, WA);                         /* address */
Write (SP + 12, 0, L_LONG, WA);                         /* VIBA */
Write (SP + 16, 0, L_LONG, WA);                         /* ICCS..SISR */
Write (SP + 20, 0, L_LONG, WA);                         /* state */
Write (SP + 24, 0, L_LONG, WA);                         /* SC */
in_ie = 0;
return cc;
}

/* Console entry */

int32 con_halt (int32 code, int32 cc)
{
int32 temp;

conisp = IS;                                            /* save ISP */
conpc = PC;                                             /* save PC */
conpsl = ((PSL | cc) & 0xFFFF00FF) | code;              /* PSL, param */
temp = (PSL >> PSL_V_CUR) & 0x7;                        /* get is'cur */
if (temp > 4)                                           /* invalid? */
    conpsl = conpsl | CON_BADPSL;
else STK[temp] = SP;                                    /* save stack */
if (mapen)                                              /* mapping on? */
    conpsl = conpsl | CON_MAPON;
mapen = 0;                                              /* turn off map */
SP = IS;                                                /* set SP from IS */
PSL = PSL_IS | PSL_IPL1F;                               /* PSL = 41F0000 */
JUMP (ROMBASE);                                         /* PC = 20040000 */
return 0;                                               /* new cc = 0 */
}


/* Special boot command - linked into SCP by initial reset

   Syntax: BOOT {CPU}

*/

t_stat vax460_boot (int32 flag, CONST char *ptr)
{
char gbuf[CBUFSIZE];

get_glyph (ptr, gbuf, 0);                           /* get glyph */
if (gbuf[0] && strcmp (gbuf, "CPU"))
    return SCPE_ARG;                                /* Only can specify CPU device */
return run_cmd (flag, "CPU");
}


/* Bootstrap */

t_stat cpu_boot (int32 unitno, DEVICE *dptr)
{
t_stat r;

PC = ROMBASE;
PSL = PSL_IS | PSL_IPL1F;
conisp = 0;
conpc = 0;
conpsl = PSL_IS | PSL_IPL1F | CON_PWRUP;
if (rom == NULL)
    return SCPE_IERR;
if (*rom == 0) {                                        /* no boot? */
    r = cpu_load_bootcode (BOOT_CODE_FILENAME, BOOT_CODE_ARRAY, BOOT_CODE_SIZE, TRUE, 0);
    if (r != SCPE_OK)
        return r;
    }
return SCPE_OK;
}

/* SYSD reset */

t_stat sysd_reset (DEVICE *dptr)
{
ka_mapbase = 0;
ka_cfgtst = CFGT_L3C;
ka_led = 0;
ka_parctl = 0xF0;
tmr_tir = 0;

if (isdn == NULL)                                       /* dummy mem for ISDN */
    isdn = (uint32 *) calloc (0x4000 >> 2, sizeof (uint32));
if (isdn == NULL)
    return SCPE_MEM;
if (invfl == NULL)                                      /* dummy mem for invalidate filter */
    invfl = (uint32 *) calloc (0x8000, sizeof (uint32));
if (invfl == NULL)
    return SCPE_MEM;
if (cache2ds == NULL)                                   /* dummy mem for cache data store */
    cache2ds = (uint32 *) calloc (0x10002, sizeof (uint32));
if (cache2ds == NULL)
    return SCPE_MEM;
if (cache2ts == NULL)                                   /* dummy mem for cache tag store */
    cache2ts = (uint32 *) calloc (0x10000, sizeof (uint32));
if (cache2ts == NULL)
    return SCPE_MEM;

sim_vm_cmd = vax460_cmd;

return SCPE_OK;
}

const char *sysd_description (DEVICE *dptr)
{
return "system devices";
}

t_stat auto_config (const char *name, int32 nctrl)
{
return SCPE_OK;
}

t_stat build_dib_tab (void)
{
return SCPE_OK;
}

t_stat cpu_set_model (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
char gbuf[CBUFSIZE];

if ((cptr == NULL) || (!*cptr))
    return SCPE_ARG;
cptr = get_glyph (cptr, gbuf, 0);
if (MATCH_CMD(gbuf, "MICROVAX") == 0) {
    sys_model = 0;
#if defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)
    lk_dev.flags = lk_dev.flags | DEV_DIS;               /* disable keyboard */
    vs_dev.flags = vs_dev.flags | DEV_DIS;               /* disable mouse */
#endif
    strcpy (sim_name, "MicroVAX 3100-80 (KA47)");
    reset_all (0);                                       /* reset everything */
    }
#if defined (VAX_46) || defined (VAX_48)
else if (MATCH_CMD(gbuf, "VAXSTATION") == 0) {
#if defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)
    sys_model = 1;
    lk_dev.flags = lk_dev.flags & ~DEV_DIS;              /* enable keyboard */
    vs_dev.flags = vs_dev.flags & ~DEV_DIS;              /* enable mouse */
#if defined (VAX_46)
    strcpy (sim_name, "VAXstation 4000-60 (KA46)");
#else   /* VAX_48 */
    strcpy (sim_name, "VAXstation 4000-VLC (KA48)");
#endif
    reset_all (0);                                       /* reset everything */
#else
    return sim_messagef (SCPE_ARG, "Simulator built without Graphic Device Support\n");
#endif
    }
#endif
else
    return SCPE_ARG;
return SCPE_OK;
}

t_stat cpu_print_model (FILE *st)
{
fprintf (st, "%s", sim_name);
#if defined (VAX_46)
fprintf (st, "VAXstation 4000-60 (KA46)");
#elif defined (VAX_47)
fprintf (st, "MicroVAX 3100-80 (KA47)");
#elif defined (VAX_48)
fprintf (st, "VAXstation 4000-VLC (KA48)");
#endif
return SCPE_OK;
}

t_stat cpu_model_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Initial memory size is 16MB.\n\n");
fprintf (st, "The simulator is booted with the BOOT command:\n\n");
fprintf (st, "   sim> BOOT\n\n");
return SCPE_OK;
}
