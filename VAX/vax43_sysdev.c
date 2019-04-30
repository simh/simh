/* vax43_sysdev.c: MicroVAX 3100 system-specific logic

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

   This module contains the MicroVAX 3100 system-specific registers and devices.

   sysd         system devices
*/

#include "vax_defs.h"
#include "sim_ether.h"
#include <time.h>

#ifdef DONT_USE_INTERNAL_ROM
#define BOOT_CODE_FILENAME "ka43a.bin"
#else /* !DONT_USE_INTERNAL_ROM */
#include "vax_ka43a_bin.h" /* Defines BOOT_CODE_FILENAME and BOOT_CODE_ARRAY, etc */
#endif /* DONT_USE_INTERNAL_ROM */


t_stat vax43a_boot (int32 flag, CONST char *ptr);

/* Special boot command, overrides regular boot */

CTAB vax43a_cmd[] = {
    { "BOOT", &vax43a_boot, RU_BOOT,
      "bo{ot}                   boot simulator\n", NULL, &run_cmd_message },
    { NULL }
    };

/* KA43A configuration & test register */

#define CFGT_MEM        0x0007                          /* memory option */
#define CFGT_VID        0x0008                          /* video option present */
#define CFGT_CUR        0x0010                          /* cursor test */
#define CFGT_L3C        0x0020                          /* line 3 console */
#define CFGT_NET        0x0040                          /* network option present */
#define CFGT_TYP        0x0080                          /* system type */
#define CFGT_V_DSK      8
#define CFGT_M_DSK      0xF
#define CFGT_DSK        (CFGT_M_DSK << CFGT_V_DSK)      /* disk mask */
#define CFGT_RX23       0x1000                          /* 0 = RX23, 1 = RX33 */
#define CFGT_V_STC      14
#define CFGT_M_STC      0x3
#define CFGT_STC        (CFGT_M_STC << CFGT_V_STC)      /* storage controller */

#define STC_SCSI        0                               /* SCSI/SCSI controller */
#define STC_ST506       1                               /* ST506/SCSI controller */

/* KA43A Memory system error register */

#define MSER_PE         0x00000001                      /* Parity Enable */
#define MSER_WWP        0x00000002                      /* Write Wrong Parity */
#define MSER_PER        0x00000040                      /* Parity Error */
#define MSER_MCD0       0x00000100                      /* Mem Code 0 */
#define MSER_MBZ        0xFFFFFEBC
#define MSER_RD         (MSER_PE | MSER_WWP | MSER_PER | \
                         MSER_PER | MSER_MCD0)
#define MSER_WR         (MSER_PE | MSER_WWP)
#define MSER_RS         (MSER_PER)

/* KA43A memory error address reg */

#define MEAR_FAD        0x00007FFF                      /* failing addr */
#define MEAR_RD         (MEAR_FAD)

#define ROM_VEC         0x8                             /* ROM longword for first device vector */

#define TMR_INC         10000                           /* usec/interval */

extern int32 tmr_int;
extern UNIT clk_unit;
extern int32 tmr_poll;
extern uint32 vc_sel, vc_org;
extern DEVICE vc_dev, ve_dev, lk_dev, vs_dev;
extern DEVICE xs_dev;
extern uint32 *rom;

uint32 *ddb = NULL;                                     /* 128k disk buffer */
int32 conisp, conpc, conpsl;                            /* console reg */
int32 ka_hltcod = 0;                                    /* KA43A halt code */
int32 ka_mser = 0;                                      /* KA43A mem sys err */
int32 ka_mear = 0;                                      /* KA43A memory err */
int32 ka_cfgtst = 0;                                    /* KA43A config/test */
int32 CADR = 0;                                         /* cache disable reg */
int32 SESR = 0;                                         /* software error summary reg */
int32 sys_model = 0;                                    /* MicroVAX or VAXstation */
int32 int_req[IPL_HLVL] = { 0 };                        /* interrupt requests */
int32 int_mask = 0;                                     /* interrupt mask */
uint32 tmr_tir = 0;                                     /* curr interval */
t_bool tmr_inst = FALSE;                                /* wait instructions vs usecs */
uint32 diag[128] = { 0 };
int32 cdg_dat[CDASIZE >> 2];                            /* cache data */
int32 ctg_dat[CTGSIZE >> 2];                            /* cache tags */

t_stat tmr_svc (UNIT *uptr);
t_stat sysd_reset (DEVICE *dptr);
const char *sysd_description (DEVICE *dptr);
int32 ka_rd (int32 pa);
void ka_wr (int32 pa, int32 val, int32 lnt);
int32 con_halt (int32 code, int32 cc);
int32 tmr_tir_rd (void);
void tmr_sched ();

extern t_stat or_map (uint32 index, uint8 *rom, t_addr size);
extern t_stat or_unmap (uint32 index);
extern void rom_wr_B (int32 pa, int32 val);
extern int32 iccs_rd (void);
extern int32 rom_rd (int32 pa);
extern int32 nvr_rd (int32 pa);
extern int32 nar_rd (int32 pa);
extern int32 dz_rd (int32 pa);
extern int32 or_rd (int32 pa);
extern int32 rd_rd (int32 pa);
extern int32 rz_rd (int32 pa);
extern int32 xs_rd (int32 pa);
extern int32 vc_mem_rd (int32 pa);
extern int32 ve_rd (int32 pa);
extern void iccs_wr (int32 dat);
extern void nvr_wr (int32 pa, int32 val, int32 lnt);
extern void dz_wr (int32 pa, int32 val, int32 lnt);
extern void rd_wr (int32 pa, int32 val, int32 lnt);
extern void rz_wr (int32 pa, int32 data, int32 lnt);
extern void xs_wr (int32 pa, int32 val, int32 lnt);
extern void vc_wr (int32 pa, int32 val, int32 lnt);
extern void vc_mem_wr (int32 pa, int32 val, int32 lnt);
extern void ve_wr (int32 pa, int32 val, int32 lnt);

/* SYSD data structures

   sysd_dev     SYSD device descriptor
   sysd_unit    SYSD units
   sysd_reg     SYSD register list
*/

UNIT sysd_unit = { UDATA (&tmr_svc, 0, 0) };

REG sysd_reg[] = {
    { HRDATAD (CONISP, conisp, 32, "console ISP") },
    { HRDATAD (CONPC,  conpc,  32, "console PD") },
    { HRDATAD (CONPSL, conpsl, 32, "console PSL") },
    { HRDATAD (HLTCOD, ka_hltcod, 16, "KA43A halt code") },
    { HRDATAD (MSER,   ka_mser,  8, "KA43A mem sys err") },
    { HRDATAD (MEAR,   ka_mear,  8, "KA43A mem err") },
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
        id = (ba >> 2) & 0x7FFF;
        sc = (ba & 3) << 3;
        mask = 0xFF << sc;
        ddb[id] = (ddb[id] & ~mask) | (*buf << sc);
        ba++;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
        id = (ba >> 2) & 0x7FFF;
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
        id = (ba >> 2) & 0x7FFF;
        ddb[id] = (ba & 2)? (ddb[id] & 0xFFFF) | (*buf << 16):
            (ddb[id] & ~0xFFFF) | *buf;
        ba = ba + 2;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
        id = (ba >> 2) & 0x7FFF;
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
        id = (ba >> 2) & 0x7FFF;
        sc = (ba & 3) << 3;
        *buf = (ddb[id] >> sc) & BMASK;
        ba++;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
        id = (ba >> 2) & 0x7FFF;
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
        id = (ba >> 2) & 0x7FFF;
        *buf = (ba & 2)? ((ddb[id] >> 16) & 0xFFFF) :
            (ddb[id] & 0xFFFF);
        ba = ba + 2;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
        id = (ba >> 2) & 0x7FFF;
        dat = ddb[id];                                  /* get lw */
        *buf++ = dat & WMASK;                           /* low 16b */
        *buf = (dat >> 16) & WMASK;                     /* high 16b */
        ba = ba + 4;
        }
    }
}

int32 ddb_rd (int32 pa)
{
int32 rg = ((pa - D128BASE) >> 2);
rg = rg & 0x7FFF;
return ddb[rg];
}

void ddb_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = ((pa - D128BASE) >> 2);
rg = rg & 0x7FFF;
if (lnt < L_LONG) {                                 /* byte or word? */
    int32 sc = (pa & 3) << 3;                       /* merge */
    int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
    ddb[rg] = ((val & mask) << sc) | (ddb[rg] & ~(mask << sc));
    }
else ddb[rg] = val;
return;
}

int32 sesr_rd (int32 pa)
{
return 0;
}

void sesr_wr (int32 pa, int32 val, int32 lnt)
{
return;
}

int32 cdg_rd (int32 pa)
{
int32 row = CDG_GETROW (pa);

return cdg_dat[row];
}

void cdg_wr (int32 pa, int32 val, int32 lnt)
{
int32 row = CDG_GETROW (pa);

if (lnt < L_LONG) {                                     /* byte or word? */
    int32 sc = (pa & 3) << 3;                           /* merge */
    int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
    int32 t = cdg_dat[row];
    val = ((val & mask) << sc) | (t & ~(mask << sc));
    }
cdg_dat[row] = val;                                     /* store data */
}

int32 ctg_rd (int32 pa)
{
return 0;
}

void ctg_wr (int32 pa, int32 val, int32 lnt)
{
return;
}

int32 diag_rd (int32 pa)
{
return ReadL (pa & 0xFFFFFF);
}

void diag_wr (int32 pa, int32 val, int32 lnt)
{
if (lnt >= L_LONG)                                      /* long, quad? */
    WriteL (pa & 0xFFFFFF, val);
else if (lnt == L_WORD)                                 /* word? */
    WriteW (pa & 0xFFFFFF, val);
else WriteB (pa & 0xFFFFFF, val);                       /* byte */
return;
}

int32 cfg_rd (int32 pa)
{
return ka_cfgtst;
}

void ioreset_wr (int32 pa, int32 val, int32 lnt)
{
reset_all (7);
}

/* Read KA43A specific IPR's */

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

    case MT_ICR:                                        /* for NetBSD */
        val = 0;
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

    case MT_TXCS:                                       /* for Ultrix */
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
        val = VAX43A_SID | VAX43A_UREV;
        break;

    default:
        RSVD_OPND_FAULT;
        }

return val;
}

/* Write KA43A specific IPR's */

void WriteIPR (int32 rg, int32 val)
{
switch (rg) {

    case MT_ICCS:                                       /* ICCS */
        iccs_wr (val);
        break;

    case MT_NICR:                                       /* for VAXELN */
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

    case MT_TXCS:                                       /* for Ultrix */
        break;

    case MT_TXDB:                                       /* for Ultrix */
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
    };

struct reglink regtable[] = {
    { VEBASE, VEBASE+VESIZE, &ve_rd, &ve_wr },
    { VCBASE, VCBASE+VCSIZE, &vc_mem_rd, &vc_mem_wr },
    { RZBASE, RZBASE+RZSIZE, &rz_rd, &rz_wr },
    { RZBASE, RZBBASE+RZSIZE, &rz_rd, &rz_wr },
    { XSBASE, XSBASE+XSSIZE, &xs_rd, &xs_wr },
    { DZBASE, DZBASE+DZSIZE, &dz_rd, dz_wr },
    { CURBASE, CURBASE+CURSIZE, NULL, &vc_wr },
    { D128BASE, D128BASE+D128SIZE, &ddb_rd, &ddb_wr },
    { ORBASE, ORBASE+ORSIZE, &or_rd, NULL },
    { NARBASE, NARBASE+NARSIZE, &nar_rd, NULL },
    { CFGBASE, CFGBASE+CFGSIZE, &cfg_rd, &ioreset_wr },
    { ROMBASE, ROMBASE+ROMSIZE, &rom_rd, NULL },
    { NVRBASE, NVRBASE+NVRSIZE, &nvr_rd, &nvr_wr },
    { 0x21100000, 0x21100004, &sesr_rd, &sesr_wr },
    { CDGBASE, CDGBASE+CDGSIZE, &cdg_rd, &cdg_wr },
    { CTGBASE, CTGBASE+CTGSIZE, &ctg_rd, &ctg_wr },
    { 0x28000000, 0x2A000000, &diag_rd, &diag_wr },
    { KABASE, KABASE+KASIZE, &ka_rd, &ka_wr },
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

/* KA43A registers */

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

    case 7:                                             /* timer */
        return ((tmr_tir_rd ()) << 16);
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

    case 4:                                             /* LED */
        break;

    case 7:                                             /* timer */
        tmr_tir = (val >> 16);
        break;
        }
return;
}

int32 tmr_tir_rd (void)
{
uint32 usecs_remaining, cur_tir;

if ((ADDR_IS_ROM(fault_PC)) &&                          /* running from ROM and */
    (tmr_inst))                                         /* waiting instructions? */
    usecs_remaining = sim_activate_time (&sysd_unit) - 1;
else
    usecs_remaining = (uint32)sim_activate_time_usecs (&sysd_unit);
cur_tir = (~usecs_remaining + 1) & 0xFFFF;
return cur_tir;
}

/* Unit service */

t_stat tmr_svc (UNIT *uptr)
{
tmr_sched ();                                           /* reactivate */
return SCPE_OK;
}

/* Timer scheduling */

void tmr_sched ()
{
uint32 usecs_sched = tmr_tir ? (~tmr_tir + 1) : 0xFFFF;
tmr_tir = 0;

if ((ADDR_IS_ROM(fault_PC)) &&                      /* running from ROM and */
    (usecs_sched < TMR_INC)) {                      /* short delay? */
    tmr_inst = TRUE;                                /* wait for instructions */
    sim_activate (&sysd_unit, usecs_sched);
    }
else {
    tmr_inst = FALSE;
    sim_activate_after (&sysd_unit, usecs_sched);
    }
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

t_stat vax43a_boot (int32 flag, CONST char *ptr)
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
sim_cancel (&sysd_unit);
ka_mser = 0;
ka_mear = 0;
ka_cfgtst = (CFGT_TYP | CFGT_CUR);
ka_cfgtst |= ((MEMSIZE >> 22) - 1);                     /* memory option */
if ((ve_dev.flags & DEV_DIS) == 0)                      /* video option present? */
    ka_cfgtst |= CFGT_VID;
if ((xs_dev.flags & DEV_DIS) == 0)                      /* network option present? */
    ka_cfgtst |= CFGT_NET;
if (DZ_L3C && (sys_model == 0))                         /* line 3 console */
    ka_cfgtst |= CFGT_L3C;
tmr_tir = 0;
tmr_inst = FALSE;
tmr_sched ();                                           /* activate */

if (ddb == NULL)
    ddb = (uint32 *) calloc (D128SIZE >> 2, sizeof (uint32));
if (ddb == NULL)
    return SCPE_MEM;

tmr_sched ();
sim_vm_cmd = vax43a_cmd;

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
if ((MATCH_CMD(gbuf, "VAXSERVER") == 0) ||
    (MATCH_CMD(gbuf, "MICROVAX") == 0)) {                /* needed by VC,VE */
    sys_model = 0;
#if defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)
    vc_dev.flags = vc_dev.flags | DEV_DIS;               /* disable MVO */
    ve_dev.flags = vc_dev.flags | DEV_DIS;               /* disable SPX */
    lk_dev.flags = lk_dev.flags | DEV_DIS;               /* disable keyboard */
    vs_dev.flags = vs_dev.flags | DEV_DIS;               /* disable mouse */
#endif
    strcpy (sim_name, "VAXserver 3100 M76 (KA43-A)");
    reset_all (0);                                       /* reset everything */
    }
else if (MATCH_CMD(gbuf, "VAXSTATION") == 0) {
#if defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)
    sys_model = 1;
    ve_dev.flags = ve_dev.flags | DEV_DIS;               /* disable SPX */
    vc_dev.flags = vc_dev.flags & ~DEV_DIS;              /* enable MVO */
    lk_dev.flags = lk_dev.flags & ~DEV_DIS;              /* enable keyboard */
    vs_dev.flags = vs_dev.flags & ~DEV_DIS;              /* enable mouse */
    strcpy (sim_name, "VAXstation 3100 M76 (KA43-A)");
    reset_all (0);                                       /* reset everything */
#else
    return sim_messagef (SCPE_ARG, "Simulator built without Graphic Device Support\n");
#endif
    }
else if (MATCH_CMD(gbuf, "VAXSTATIONSPX") == 0) {
#if defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)
    sys_model = 1;
    vc_dev.flags = vc_dev.flags | DEV_DIS;               /* disable MVO */
    ve_dev.flags = ve_dev.flags & ~DEV_DIS;              /* enable SPX */
    lk_dev.flags = lk_dev.flags & ~DEV_DIS;              /* enable keyboard */
    vs_dev.flags = vs_dev.flags & ~DEV_DIS;              /* enable mouse */
    strcpy (sim_name, "VAXstation 3100 M76/SPX (KA43-A)");
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
