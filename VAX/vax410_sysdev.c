/* vax410_sysdev.c: MicroVAX 2000 system-specific logic

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

   This module contains the MicroVAX 2000 system-specific registers and devices.

   sysd         system devices
*/

#include "vax_defs.h"
#include <time.h>

#ifdef DONT_USE_INTERNAL_ROM
#define BOOT_CODE_FILENAME "ka410.bin"
#else /* !DONT_USE_INTERNAL_ROM */
#include "vax_ka410_bin.h" /* Defines BOOT_CODE_FILENAME and BOOT_CODE_ARRAY, etc */
#endif /* DONT_USE_INTERNAL_ROM */


t_stat vax410_boot (int32 flag, CONST char *ptr);

/* Special boot command, overrides regular boot */

CTAB vax410_cmd[] = {
    { "BOOT", &vax410_boot, RU_BOOT,
      "bo{ot}                   boot simulator\n", NULL, &run_cmd_message },
    { NULL }
    };

/* KA410 configuration & test register */

#define CFGT_MEM        0x07                            /* memory option */
#define CFGT_VID        0x08                            /* video option */
#define CFGT_CUR        0x10                            /* cursor test */
#define CFGT_L3C        0x20                            /* line 3 console */
#define CFGT_NET        0x40                            /* network option */
#define CFGT_TYP        0x80                            /* multi-char user */

/* KA410 Memory system error register */

#define MSER_PE         0x00000001                      /* Parity Enable */
#define MSER_WWP        0x00000002                      /* Write Wrong Parity */
#define MSER_PER        0x00000040                      /* Parity Error */
#define MSER_MCD0       0x00000100                      /* Mem Code 0 */
#define MSER_MBZ        0xFFFFFEBC
#define MSER_RD         (MSER_PE | MSER_WWP | MSER_PER | \
                         MSER_PER | MSER_MCD0)
#define MSER_WR         (MSER_PE | MSER_WWP)
#define MSER_RS         (MSER_PER)

/* KA410 memory error address reg */

#define MEAR_FAD        0x00007FFF                      /* failing addr */
#define MEAR_RD         (MEAR_FAD)

#define ROM_VEC         0x8                             /* ROM longword for first device vector */

extern int32 tmr_int;
extern UNIT clk_unit;
extern int32 tmr_poll;
extern uint32 vc_sel, vc_org;
extern DEVICE va_dev, vc_dev, lk_dev, vs_dev;
extern DEVICE xs_dev;
extern uint32 *rom;

uint32 *ddb = NULL;                                     /* 16k disk buffer */
int32 conisp, conpc, conpsl;                            /* console reg */
int32 ka_hltcod = 0;                                    /* KA410 halt code */
int32 ka_mser = 0;                                      /* KA410 mem sys err */
int32 ka_mear = 0;                                      /* KA410 memory err */
int32 ka_cfgtst = 0;                                    /* KA410 config/test */
int32 buf_sel = 0;                                      /* buffer select */
int32 sys_model = 0;                                    /* MicroVAX or VAXstation */
int32 int_req[IPL_HLVL] = { 0 };                        /* interrupt requests */
int32 int_mask = 0;                                     /* interrupt mask */

t_stat sysd_reset (DEVICE *dptr);
const char *sysd_description (DEVICE *dptr);
int32 ka_rd (int32 pa);
void ka_wr (int32 pa, int32 val, int32 lnt);
int32 con_halt (int32 code, int32 cc);

extern t_stat or_map (uint32 index, uint8 *rom, t_addr size);
extern t_stat or_unmap (uint32 index);
extern void rom_wr_B (int32 pa, int32 val);
extern int32 iccs_rd (void);
extern int32 rom_rd (int32 pa);
extern int32 nvr_rd (int32 pa);
extern int32 nar_rd (int32 pa);
extern int32 dz_rd (int32 pa);
extern int32 rd_rd (int32 pa);
extern int32 rz_rd (int32 pa);
extern int32 or_rd (int32 pa);
extern int32 xs_rd (int32 pa);
extern int32 va_rd (int32 pa);
extern int32 vc_mem_rd (int32 pa);
extern void iccs_wr (int32 dat);
extern void nvr_wr (int32 pa, int32 val, int32 lnt);
extern void rd_wr (int32 pa, int32 val, int32 lnt);
extern void dz_wr (int32 pa, int32 val, int32 lnt);
extern void vc_wr (int32 pa, int32 val, int32 lnt);
extern void xs_wr (int32 pa, int32 val, int32 lnt);
extern void rz_wr (int32 pa, int32 val, int32 lnt);
extern void va_wr (int32 pa, int32 val, int32 lnt);
extern void vc_mem_wr (int32 pa, int32 val, int32 lnt);

/* SYSD data structures

   sysd_dev     SYSD device descriptor
   sysd_unit    SYSD units
   sysd_reg     SYSD register list
*/

UNIT sysd_unit = { UDATA (NULL, 0, 0) };

REG sysd_reg[] = {
    { HRDATAD (CONISP, conisp,    32, "console ISP") },
    { HRDATAD (CONPC,  conpc,     32, "console PD") },
    { HRDATAD (CONPSL, conpsl,    32, "console PSL") },
    { HRDATAD (HLTCOD, ka_hltcod, 16, "KA410 halt code") },
    { HRDATAD (MSER,   ka_mser,    8, "KA410 mem sys err") },
    { HRDATAD (MEAR,   ka_mear,    8, "KA410 mem err") },
    { HRDATAD (CFGTST, ka_cfgtst,  8, "KA410 config/test register") },
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

/* DMA buffer routines, aligned access

   Map_ReadB    -       fetch byte buffer from memory
   Map_ReadW    -       fetch word buffer from memory
   Map_WriteB   -       store byte buffer into memory
   Map_WriteW   -       store word buffer into memory
*/

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf)
{
int32 i;
uint32 ma = ba;
uint32 dat;

if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = 0; i < bc; i++, buf++) {                   /* by bytes */
        *buf = ReadB (ma);
        ma = ma + 1;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
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
uint32 ma = ba;
uint32 dat;

ba = ba & ~01;
bc = bc & ~01;
if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = 0; i < bc; i = i + 2, buf++) {             /* by words */
        *buf = ReadW (ma);
        ma = ma + 2;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
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
uint32 ma = ba;
uint32 dat;

if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = 0; i < bc; i++, buf++) {                   /* by bytes */
        WriteB (ma, *buf);
        ma = ma + 1;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
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
uint32 ma = ba;
uint32 dat;

ba = ba & ~01;
bc = bc & ~01;
if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = 0; i < bc; i = i + 2, buf++) {             /* by words */
        WriteW (ma, *buf);
        ma = ma + 2;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
        dat = (uint32) *buf++;                          /* get low 16b */
        dat = dat | (((uint32) *buf) << 16);            /* merge hi 16b */
        WriteL (ma, dat);                               /* store lw */
        ma = ma + 4;
        }
    }
return 0;
}

void ddb_WriteB (uint32 ba, uint32 bc, uint8 *buf)
{
uint32 i, id, sc, mask, dat;

if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = 0; i < bc; i++, buf++) {                   /* by bytes */
        id = (ba >> 2) & 0xFFF;
        sc = (ba & 3) << 3;
        mask = 0xFF << sc;
        ddb[id] = (ddb[id] & ~mask) | (*buf << sc);
        ba++;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
        id = (ba >> 2) & 0xFFF;
        dat = (uint32) *buf++;                          /* get low 8b */
        dat = dat | (((uint32) *buf++) << 8);           /* merge next 8b */
        dat = dat | (((uint32) *buf++) << 16);          /* merge next 8b */
        dat = dat | (((uint32) *buf) << 24);            /* merge hi 8b */
        ddb[id] = dat;                                  /* store lw */
        ba = ba + 4;
        }
    }
}

void ddb_WriteW (uint32 ba, uint32 bc, uint16 *buf)
{
uint32 i, id, dat;

ba = ba & ~01;
bc = bc & ~01;
if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = 0; i < bc; i = i + 2, buf++) {             /* by words */
        id = (ba >> 2) & 0xFFF;
        ddb[id] = (ba & 2)? (ddb[id] & 0xFFFF) | (*buf << 16):
            (ddb[id] & ~0xFFFF) | *buf;
        ba = ba + 2;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
        id = (ba >> 2) & 0xFFF;
        dat = (uint32) *buf++;                          /* get low 16b */
        dat = dat | (((uint32) *buf) << 16);            /* merge hi 16b */
        ddb[id] = dat;                                  /* store lw */
        ba = ba + 4;
        }
    }
}

void ddb_ReadB (uint32 ba, uint32 bc, uint8 *buf)
{
uint32 i, id, sc, dat;

if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = 0; i < bc; i++, buf++) {                   /* by bytes */
        id = (ba >> 2) & 0xFFF;
        sc = (ba & 3) << 3;
        *buf = (ddb[id] >> sc) & BMASK;
        ba++;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
        id = (ba >> 2) & 0xFFF;
        dat = ddb[id];                                  /* get lw */
        *buf++ = dat & BMASK;                           /* low 8b */
        *buf++ = (dat >> 8) & BMASK;                    /* next 8b */
        *buf++ = (dat >> 16) & BMASK;                   /* next 8b */
        *buf = (dat >> 24) & BMASK;
        ba = ba + 4;
        }
    }
}

void ddb_ReadW (uint32 ba, uint32 bc, uint16 *buf)
{
uint32 i, id, dat;

ba = ba & ~01;
bc = bc & ~01;
if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = 0; i < bc; i = i + 2, buf++) {             /* by words */
        id = (ba >> 2) & 0xFFF;
        *buf = (ba & 2)? ((ddb[id] >> 16) & 0xFFFF) :
            (ddb[id] & 0xFFFF);
        ba = ba + 2;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
        id = (ba >> 2) & 0xFFF;
        dat = ddb[id];                                  /* get lw */
        *buf++ = dat & WMASK;                           /* low 16b */
        *buf = (dat >> 16) & WMASK;                     /* high 16b */
        ba = ba + 4;
        }
    }
}

int32 ddb_rd (int32 pa)
{
int32 rg = (pa - D16BASE) >> 2;
return ddb[rg];
}

void ddb_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - D16BASE) >> 2;
if (lnt < L_LONG) {                                     /* byte or word? */
    int32 sc = (pa & 3) << 3;                           /* merge */
    int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
    ddb[rg] = ((val & mask) << sc) | (ddb[rg] & ~(mask << sc));
    }
else ddb[rg] = val;
}

int32 cfg_rd (int32 pa)
{
return ka_cfgtst;
}

void ioreset_wr (int32 pa, int32 val, int32 lnt)
{
reset_all (7);
}

/* Read KA410 specific IPR's */

int32 ReadIPR (int32 rg)
{
int32 val;

switch (rg) {

    case MT_ICCS:                                       /* ICCS */
        val = iccs_rd ();
        break;

    case MT_NICR:
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

    case MT_SID:                                        /* SID */
        val = VAX410_SID | VAX410_UREV;
        break;

    default:
        RSVD_OPND_FAULT;
        }

return val;
}

/* Write KA410 specific IPR's */

void WriteIPR (int32 rg, int32 val)
{
switch (rg) {

    case MT_ICCS:                                       /* ICCS */
        iccs_wr (val);
        break;

    case MT_NICR:
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

    default:
        RSVD_OPND_FAULT;
        }

return;
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
    int32      width;                                   /* data path width */
    };

struct reglink regtable[] = {
    { VCBASE, VCBASE+VCSIZE, &vc_mem_rd, &vc_mem_wr, L_LONG },
    { VABASE, VABASE+VASIZE, &va_rd, &va_wr, L_WORD },
    { D16BASE, D16BASE+D16SIZE, &ddb_rd, &ddb_wr, L_LONG },
    { RDBASE, RDBASE+RDSIZE, &rd_rd, &rd_wr, L_LONG },
    { RZBASE, RZBASE+RZSIZE, &rz_rd, &rz_wr, L_LONG },
    { XSBASE, XSBASE+XSSIZE, &xs_rd, &xs_wr, L_LONG },
    { DZBASE, DZBASE+DZSIZE, &dz_rd, dz_wr, L_LONG },
    { CURBASE, CURBASE+CURSIZE, NULL, &vc_wr, L_LONG },
    { ORBASE, ORBASE+ORSIZE, &or_rd, NULL, L_LONG },
    { NARBASE, NARBASE+NARSIZE, &nar_rd, NULL, L_LONG },
    { CFGBASE, CFGBASE+CFGSIZE, &cfg_rd, &ioreset_wr, L_LONG },
    { ROMBASE, ROMBASE+ROMSIZE, &rom_rd, NULL, L_LONG },
    { NVRBASE, NVRBASE+NVRSIZE, &nvr_rd, &nvr_wr, L_LONG },
    { KABASE, KABASE+KASIZE, &ka_rd, &ka_wr, L_LONG },
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
int32 val;

for (p = &regtable[0]; p->low != 0; p++) {
    if ((pa >= p->low) && (pa < p->high) && p->read) {
        val = p->read (pa);
        if (p->width < L_LONG) {
            if (lnt < L_LONG)
                val = val << ((pa & 2)? 16: 0);
            else val = (p->read (pa + 2) << 16) | val;
            }
        return val;
        }
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
struct reglink *p;
int32 val;

for (p = &regtable[0]; p->low != 0; p++) {
    if ((pa >= p->low) && (pa < p->high) && p->read) {
        if (p->width < L_LONG) {
            val = p->read (pa);
            if ((lnt + (pa & 1)) <= 2)
                val = val << ((pa & 2)? 16: 0);
            else val = (p->read (pa + 2) << 16) | val;
            }
        else {
            if (lnt == L_BYTE)
                val = p->read (pa & ~03);
            else val = (p->read (pa & ~03) & WMASK) | (p->read ((pa & ~03) + 2) & (WMASK << 16));
            }
        return val;
        }
    }
return 0xFFFFFFFF;
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
        if (lnt > p->width) {
            p->write (pa, val & WMASK, L_WORD);
            p->write (pa + 2, (val >> 16) & WMASK, L_WORD);
            }
        else p->write (pa, val, lnt);
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
struct reglink *p;

for (p = &regtable[0]; p->low != 0; p++) {
    if ((pa >= p->low) && (pa < p->high) && p->write) {
        if (p->width < L_LONG) {
            switch (lnt) {
            case L_BYTE:                                /* byte */
                p->write (pa, val & BMASK, L_BYTE);
                break;

            case L_WORD:                                /* word */
                if (pa & 1) {                           /* odd addr */
                    p->write (pa, val & BMASK, L_BYTE);
                    p->write (pa + 1, (val >> 8) & BMASK, L_BYTE);
                    }
                else p->write (pa, val & WMASK, L_WORD);
                break;

            case 3:                                     /* tribyte */
                if (pa & 1) {                           /* odd addr */
                    p->write (pa, val & BMASK, L_BYTE); /* byte then word */
                    p->write (pa + 1, (val >> 8) & WMASK, L_WORD);
                    }
                else {                                  /* even */
                    p->write (pa, val & WMASK, L_WORD);  /* word then byte */
                    p->write (pa + 2, (val >> 16) & BMASK, L_BYTE);
                    }
                break;
                }
            }
        else if (p->read) {
            int32 sc = (pa & 03) << 3;
            int32 dat = p->read (pa & ~03);

            dat = (dat & ~(insert[lnt] << sc)) | ((val & insert[lnt]) << sc);
            p->write (pa & ~03, dat, L_LONG);
            }
        return;
        }
    }
return;
}

/* KA410 registers */

int32 ka_rd (int32 pa)
{
int32 rg = (pa - KABASE) >> 2;

switch (rg) {

    case 0:                                             /* HLTCOD */
        return ka_hltcod;

    case 1:                                             /* MSER */
        return ka_mser & MSER_RD;

    case 2:                                             /* MEAR */
        return ka_mear & MEAR_RD;

    case 3:                                             /* INT_REQ, VDC_SEL, VDC_ORG, INT_MSK */
        return ((int_req[0] & BMASK) << 24) | \
            ((vc_sel & 1) << 16) | \
            ((vc_org & BMASK) << 8) | \
            (int_mask & BMASK);
        }

return 0;
}

void ka_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - KABASE) >> 2;

switch (rg) {

    case 0:                                             /* HLTCOD */
        ka_hltcod = val;
        break;

    case 1:                                             /* MSER */
        ka_mser = (ka_mser & ~MSER_WR) | (val & MSER_WR);
        ka_mser = ka_mser & ~(val & MSER_RS);
        break;

    case 2:                                             /* MEAR */
        /* read only? */
        break;

    case 3:
        switch (pa & 3) {
            case 0:                                     /* INT_MSK */
                int_mask = val & BMASK;
                SET_IRQL;
                break;

            case 1:                                     /* VDC_ORG */
                vc_org = val & BMASK;
                break;

            case 2:                                     /* VDC_SEL */
                vc_sel = val & 1;
                break;

            case 3:                                     /* INT_CLR */
                int_req[0] = int_req[0] & ~(val & BMASK);
                break;
                }
        break;
        }
return;
}

/* Machine check */

int32 machine_check (int32 p1, int32 opc, int32 cc, int32 delta)
{
int32 st, p2, acc;

if (in_ie) {
    in_ie = 0;
    return con_halt(CON_DBLMCK, cc);                    /* double machine check */
    }
if (p1 & 0x80)                                          /* mref? set v/p */
    p1 = p1 + mchk_ref;
p2 = mchk_va + 4;                                       /* save vap */
st = 0;
if (p1 & 0x80)                                          /* mref? */
    cc = intexc (SCB_MCHK, cc, 0, IE_EXC);              /* take normal exception */
else cc = intexc (SCB_MCHK, cc, 0, IE_SVE);             /* take severe exception */
acc = ACC_MASK (KERN);                                  /* in kernel mode */
in_ie = 1;
SP = SP - 16;                                           /* push 4 words */
Write (SP, 12, L_LONG, WA);                             /* # bytes */
Write (SP + 4, p1, L_LONG, WA);                         /* mcheck type */
Write (SP + 8, p2, L_LONG, WA);                         /* address */
Write (SP + 12, st, L_LONG, WA);                        /* state */
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

t_stat vax410_boot (int32 flag, CONST char *ptr)
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
DEVICE *cdptr;
int32 i;

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
rom_wr_B (ROMBASE+4, sys_model ? 2 : 1);                /* Set Magic Byte to determine system type */
rom_wr_B (ROMBASE+0x14B6, (int32)(sys_model ? 'B' : 'A'));
for (i = 0; i < OR_COUNT; i++)                          /* unmap all option ROMs */
    or_unmap (i);
for (i = 0; (cdptr = sim_devices[i]) != NULL; i++) {    /* loop over all devices */
    DIB *cdibp = (DIB *)(cdptr->ctxt);

    if (!cdibp || (cdptr->flags & DEV_DIS))             /* device enabled and has DIB? */
        continue;

    if (cdibp->rom_array != NULL)                       /* device has an option ROM? */
        or_map (cdibp->rom_index, cdibp->rom_array, cdibp->rom_size);
    }
return SCPE_OK;
}

/* SYSD reset */

t_stat sysd_reset (DEVICE *dptr)
{
ka_mser = 0;
ka_mear = 0;
ka_cfgtst = (CFGT_TYP | CFGT_CUR);
if (MEMSIZE > (1u << 21))                               /* more than 2MB? */
    ka_cfgtst |= ((MEMSIZE >> 21) & CFGT_MEM);
if ((vc_dev.flags & DEV_DIS) == 0)                      /* mono video enabled? */
    ka_cfgtst &= ~CFGT_TYP;
if ((va_dev.flags & DEV_DIS) == 0) {                    /* video option present? */
    ka_cfgtst &= ~CFGT_TYP;
    ka_cfgtst |= CFGT_VID;
    }
if ((xs_dev.flags & DEV_DIS) == 0)                      /* network option present? */
    ka_cfgtst |= CFGT_NET;
if (DZ_L3C && (sys_model == 0))                         /* line 3 console */
    ka_cfgtst |= CFGT_L3C;

if (ddb == NULL)
    ddb = (uint32 *) calloc (D16SIZE >> 2, sizeof (uint32));
if (ddb == NULL)
    return SCPE_MEM;

sim_vm_cmd = vax410_cmd;

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
    va_dev.flags = vc_dev.flags | DEV_DIS;               /* disable GPX */
    vc_dev.flags = vc_dev.flags | DEV_DIS;               /* disable MVO */
    lk_dev.flags = lk_dev.flags | DEV_DIS;               /* disable keyboard */
    vs_dev.flags = vs_dev.flags | DEV_DIS;               /* disable mouse */
#endif
    strcpy (sim_name, "MicroVAX 2000 (KA410)");
    reset_all (0);                                       /* reset everything */
    }
else if (MATCH_CMD(gbuf, "VAXSTATION") == 0) {
#if defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)
    sys_model = 1;
    va_dev.flags = va_dev.flags | DEV_DIS;               /* disable GPX */
    vc_dev.flags = vc_dev.flags & ~DEV_DIS;              /* enable MVO */
    lk_dev.flags = lk_dev.flags & ~DEV_DIS;              /* enable keyboard */
    vs_dev.flags = vs_dev.flags & ~DEV_DIS;              /* enable mouse */
    strcpy (sim_name, "VAXstation 2000 (KA410)");
    reset_all (0);                                       /* reset everything */
#else
    return sim_messagef (SCPE_ARG, "Simulator built without Graphic Device Support\n");
#endif
    }
else if (MATCH_CMD(gbuf, "VAXSTATIONGPX") == 0) {
#if defined (USE_SIM_VIDEO) && defined (HAVE_LIBSDL)
    sys_model = 1;
    vc_dev.flags = vc_dev.flags | DEV_DIS;               /* disable MVO */
    va_dev.flags = va_dev.flags & ~DEV_DIS;              /* enable GPX */
    lk_dev.flags = lk_dev.flags & ~DEV_DIS;              /* enable keyboard */
    vs_dev.flags = vs_dev.flags & ~DEV_DIS;              /* enable mouse */
    strcpy (sim_name, "VAXstation 2000 GPX (KA410)");
    reset_all (0);                                       /* reset everything */
#else
    return sim_messagef (SCPE_ARG, "Simulator built without Graphic Device Support\n");
#endif
    }
else
    return SCPE_ARG;
return SCPE_OK;
}

t_stat cpu_print_model (FILE *st)
{
fprintf (st, "%s", sim_name);
return SCPE_OK;
}

t_stat cpu_model_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Initial memory size is 16MB.\n\n");
fprintf (st, "The simulator is booted with the BOOT command:\n\n");
fprintf (st, "   sim> BOOT\n\n");
return SCPE_OK;
}
