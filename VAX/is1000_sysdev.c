/* is1000_sysdev.c: InfoServer 1000 system-specific logic

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

   This module contains the InfoServer 1000 system-specific registers and devices.

   sysd         system devices
*/

#include "vax_defs.h"
#include "sim_tmxr.h"
#include "sim_ether.h"
#include <time.h>

#ifdef DONT_USE_INTERNAL_ROM
#define BOOT_CODE_FILENAME "is1000.bin"
#else /* !DONT_USE_INTERNAL_ROM */
#include "vax_is1000_bin.h" /* Defines BOOT_CODE_FILENAME and BOOT_CODE_ARRAY, etc */
#endif /* DONT_USE_INTERNAL_ROM */


t_stat is1000_boot (int32 flag, CONST char *ptr);

/* Special boot command, overrides regular boot */

CTAB is1000_cmd[] = {
    { "BOOT", &is1000_boot, RU_BOOT,
      "bo{ot}                   boot simulator\n", NULL, &run_cmd_message },
    { NULL }
    };

/* IS1000 configuration & test register */

#define CFGT_MEM        0x003F                          /* memory option */
#define CFGT_V_VID      6
#define CFGT_M_VID      0x3
#define CFGT_VID        (CFGT_M_VID << CFGT_V_VID)      /* video option */
#define CFGT_L3C        0x0100                          /* line 3 console */
#define CFGT_V_SIM      9
#define CFGT_M_SIM      0x3F
#define CFGT_SIM        (CFGT_M_SIM << CFGT_V_SIM)      /* SIMM type */

/* IS1000 parity control register */

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

/* IS1000 Memory system error register */

#define MSER_WWP        0x00000002                      /* Write Wrong Parity */

#define ROM_VEC         0x8                             /* ROM longword for first device vector */

/* DMA map registers */

#define DMANMAPR        32768                           /* number of map reg */
#define DMAMAP_VLD      0x80000000                      /* valid */
#define DMAMAP_PAG      0x0000FFFF                      /* mem page */

#define CSR_XDONE       0x01
#define CSR_RDONE       0x02
#define TMXR_MULT       1                               /* 100 Hz */
#define TTIBUF_VLD      0x8000                          /* valid */
#define TTIBUF_OVR      0x4000                          /* overrun */
#define TTIBUF_FRM      0x2000                          /* framing error */
#define TTIBUF_RBR      0x0400                          /* receive break */

extern int32 tmr_int;
extern UNIT clk_unit;
extern int32 tmr_poll;
extern uint32 *rom;

uint32 *invfl = NULL;                                   /* invalidate filter */
int32 conisp, conpc, conpsl;                            /* console reg */
int32 ka_hltcod = 0;                                    /* IS1000 halt code */
int32 ka_mapbase = 0;                                   /* IS1000 map base */
int32 ka_boff = 0;                                      /* IS1000 byte offset */
int32 ka_mser = 0;                                      /* IS1000 mem sys err */
int32 ka_mear = 0;                                      /* IS1000 mem err addr */
int32 ka_cfgtst = 0xFFAB;                               /* IS1000 config/test */
int32 ka_parctl = 0xF0;                                 /* IS1000 parity control */
int32 ka_tmr = 0;                                       /* IS1000 diag timer */
int32 CADR = 0;                                         /* cache disable reg */
int32 SCCR = 0;                                         /* secondary cache control */
int32 sys_model = 0;                                    /* MicroVAX or VAXstation */
int32 int_req[IPL_HLVL] = { 0 };                        /* interrupt requests */
int32 int_mask = 0;                                     /* interrupt mask */
int32 vc_sel, vc_org;
int32 dz_csr = 0;                                       /* control/status */
int32 dz_lpr = 0;                                       /* line param */
uint32 dz_buftime;                                      /* time input character arrived */
uint32 dma_csr = 0;
uint32 dma_txc = 0;
uint32 dma_addr = 0;

t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat dz_reset (DEVICE *dptr);
t_stat sysd_reset (DEVICE *dptr);
const char *dz_description (DEVICE *dptr);
const char *sysd_description (DEVICE *dptr);
t_stat dz_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
int32 dz_rd (int32 pa);
int32 ka_rd (int32 pa);
void dz_wr (int32 pa, int32 val, int32 lnt);
void ka_wr (int32 pa, int32 val, int32 lnt);
int32 con_halt (int32 code, int32 cc);

extern int32 iccs_rd (void);
extern int32 rom_rd (int32 pa);
extern int32 nvr_rd (int32 pa);
extern int32 rz_rd (int32 pa);
extern int32 xs_rd (int32 pa);
extern void iccs_wr (int32 dat);
extern void nvr_wr (int32 pa, int32 val, int32 lnt);
extern void rz_wr (int32 pa, int32 val, int32 lnt);
extern void xs_wr (int32 pa, int32 val, int32 lnt);

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit descriptor
   tti_reg      TTI register list
*/

UNIT dz_unit[] = {
    { UDATA (&tti_svc, UNIT_IDLE|TT_MODE_8B, 0), TMLN_SPD_9600_BPS },
    { UDATA (&tto_svc, TT_MODE_8B, 0), SERIAL_OUT_WAIT }
    };

REG dz_reg[] = {
    { HRDATAD (RBUF,     dz_unit[0].buf,         16, "last data item received") },
    { HRDATAD (XBUF,     dz_unit[1].buf,          8, "last data item sent") },
    { HRDATAD (CSR,              dz_csr,          8, "control/status register") },
    { DRDATAD (RPOS,     dz_unit[0].pos,   T_ADDR_W, "number of characters input"), PV_LEFT },
    { DRDATAD (RTIME,   dz_unit[0].wait,         24, "input polling interval"), PV_LEFT },
    { DRDATAD (XPOS,     dz_unit[1].pos,   T_ADDR_W, "number of characters output"), PV_LEFT },
    { DRDATAD (XTIME,   dz_unit[1].wait,         24, "time from I/O initiation to interrupt"), PV_LEFT },
    { FLDATAD (TXINT, int_req[IPL_DZTX], INT_V_DZTX, "transmit interrupt pending flag") },
    { FLDATAD (RXINT, int_req[IPL_DZRX], INT_V_DZRX, "receive interrupt pending flag") },
    { NULL }
    };

MTAB dz_mod[] = {
    { TT_MODE,  TT_MODE_7B, "7b", "7B", NULL, NULL, NULL, "Set 7 bit mode" },
    { TT_MODE,  TT_MODE_8B, "8b", "8B", NULL, NULL, NULL, "Set 8 bit mode" },
    { 0 }
    };

DEVICE dz_dev = {
    "DZ", dz_unit, dz_reg, dz_mod,
    2, 10, 31, 1, 16, 8,
    NULL, NULL, &dz_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, &dz_help, NULL, NULL, 
    &dz_description
    };

/* debugging bitmaps */

#define DBG_REG         0x0001                          /* trace read/write registers */

UNIT sysd_unit = { UDATA (NULL, 0, 0) };

REG sysd_reg[] = {
    { HRDATAD (CONISP, conisp,    32, "console ISP") },
    { HRDATAD (CONPC,  conpc,     32, "console PD") },
    { HRDATAD (CONPSL, conpsl,    32, "console PSL") },
    { HRDATAD (HLTCOD, ka_hltcod, 32, "halt code") },
    { HRDATAD (MSER,   ka_mser,   32, "mem sys err") },
    { HRDATAD (MEAR,   ka_mear,   32, "mem err addr") },
    { HRDATAD (IMSK,   int_mask,   8, "interrupt mask") },
    { NULL }
    };

MTAB sysd_mod[] = {
    { 0 },
    };

DEBTAB sysd_debug[] = {
    { "REG",  DBG_REG,      "Register activity" },
    { 0 }
    };

DEVICE sysd_dev = {
    "SYSD", &sysd_unit, sysd_reg, sysd_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &sysd_reset,
    NULL, NULL, NULL,
    NULL, DEV_DEBUG,
    0, sysd_debug, NULL, NULL, NULL, NULL, NULL, 
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
    if ((ka_mser & MSER_WWP) && (int_req[0] & INT_SC))  /* to make ROM test pass */
        SET_INT (PE);                                   /* parity error */
    if (int_req[0] & int_mask & 0xF)
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
for (i = 3; int_unmask && (i >= 0); i--) {
    if ((int_unmask >> i) & 1) {
        switch (i) {
            case 0: return rom[ROM_VEC + 1] & 0x3FF;    /* get vectors from ROM */
            case 1: return rom[ROM_VEC + 4] & 0x3FF;
            case 2: return rom[ROM_VEC + 6] & 0x3FF;
            case 3: return rom[ROM_VEC + 7] & 0x3FF;
            default: return 0;
            }
        }
    }
return 0;
}

/* Map an address via the translation map */

t_bool dma_map_addr (uint32 da, uint32 *ma, t_bool map)
{
if (map) {                                              /* using map? */
    int32 dblk = (da >> VA_V_VPN);                      /* DMA blk */
    if (dblk <= DMANMAPR) {
        int32 dmap = ReadL (ka_mapbase + (dblk << 2));
        if (mapen) {
            if (dmap & DMAMAP_VLD) {                    /* valid? */
                *ma = ((dmap & DMAMAP_PAG) << VA_V_VPN) + VA_GETOFF (da);
                if (ADDR_IS_MEM (*ma))                  /* legit addr */
                    return TRUE;
                }
            }
        else {
            *ma = (ka_mapbase << 7) + da;
            return TRUE;
            }
        }
    return FALSE;
    }
else
    *ma = da;
return TRUE;
}

/* DMA buffer routines, aligned access

   Map_ReadB    -       fetch byte buffer from memory
   Map_ReadW    -       fetch word buffer from memory
   Map_WriteB   -       store byte buffer into memory
   Map_WriteW   -       store word buffer into memory
*/

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf, t_bool map)
{
int32 i;
uint32 ma, dat;

if (map)                                                /* using map? */
    ba = ba + ka_boff;
if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = ma = 0; i < bc; i++, buf++) {              /* by bytes */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma, map))       /* inv or NXM? */
                return (bc - i);
            }
        *buf = ReadB (ma);
        ma = ma + 1;
        }
    }
else {
    for (i = ma = 0; i < bc; i = i + 4, buf++) {        /* by longwords */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma, map))       /* inv or NXM? */
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

int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf, t_bool map)
{
int32 i;
uint32 ma,dat;

if (map)                                                /* using map? */
    ba = ba + ka_boff;
ba = ba & ~01;
bc = bc & ~01;
if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = ma = 0; i < bc; i = i + 2, buf++) {        /* by words */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma, map))       /* inv or NXM? */
                return (bc - i);
            }
        *buf = ReadW (ma);
        ma = ma + 2;
        }
    }
else {
    for (i = ma = 0; i < bc; i = i + 4, buf++) {        /* by longwords */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma, map))       /* inv or NXM? */
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

int32 Map_WriteB (uint32 ba, int32 bc, uint8 *buf, t_bool map)
{
int32 i;
uint32 ma, dat;

if (map)                                                /* using map? */
    ba = ba + ka_boff;
if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = ma = 0; i < bc; i++, buf++) {              /* by bytes */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma, map))       /* inv or NXM? */
                return (bc - i);
            }
        WriteB (ma, *buf);
        ma = ma + 1;
        }
    }
else {
    for (i = ma = 0; i < bc; i = i + 4, buf++) {        /* by longwords */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma, map))       /* inv or NXM? */
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

int32 Map_WriteW (uint32 ba, int32 bc, uint16 *buf, t_bool map)
{
int32 i;
uint32 ma, dat;

if (map)                                                /* using map? */
    ba = ba + ka_boff;
ba = ba & ~01;
bc = bc & ~01;
if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = ma = 0; i < bc; i = i + 2, buf++) {        /* by words */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma, map))       /* inv or NXM? */
                return (bc - i);
            }
        WriteW (ma, *buf);
        ma = ma + 2;
        }
    }
else {
    for (i = ma = 0; i < bc; i = i + 4, buf++) {        /* by longwords */
        if ((ma & VA_M_OFF) == 0) {                     /* need map? */
            if (!dma_map_addr (ba + i, &ma, map))       /* inv or NXM? */
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

int32 dz_rd (int32 pa)
{
int32 rg = (pa >> 2) & 0x7;
int32 val;

switch (rg) {

    case 0:                                             /* CSR */
        val = dz_csr;
        break;

    case 1:                                             /* LPR? */
        val = dz_lpr;
        break;

    case 2:                                             /* RBUF */
        val = dz_unit[0].buf;                           /* char + error */

        if (dz_csr & CSR_RDONE) {                       /* Input pending ? */
            dz_csr = dz_csr & ~CSR_RDONE;               /* clr done */
            dz_unit[0].buf = dz_unit[0].buf & 0377;     /* clr errors */
            CLR_INT (DZRX);
            sim_activate_after_abs (&dz_unit[0], dz_unit[0].wait);  /* check soon for more input */
            }
        break;

    case 3:                                             /* XBUF */
        val = dz_unit[1].buf;
        break;
        
    default:
        val = 0;
        break;
        }
SET_IRQL;
return val;
}

void dz_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa >> 2) & 0x7;

switch (rg) {

    case 0:                                             /* CSR */
        dz_csr = val;
        break;

    case 1:                                             /* LPR? */
        dz_lpr = val;
        break;

    case 2:                                             /* RBUF */
        dz_csr = dz_csr & ~CSR_RDONE;                   /* clr done */
        break;

    case 3:                                             /* XBUF */
        dz_unit[1].buf = val & 0377;
        dz_csr = dz_csr & ~CSR_XDONE;
        CLR_INT (DZTX);
        sim_activate (&dz_unit[1], dz_unit[1].wait);
        break;
        
    default:
        break;
        }
SET_IRQL;
}

int32 cfg_rd (int32 pa)
{
return ka_cfgtst;
}

void led_wr (int32 pa, int32 val, int32 lnt)
{
}

/* Read IS1000 specific IPR's */

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

    case MT_SID:                                        /* SID */
        val = CVAX_SID | CVAX_UREV;
        break;

    default:
        RSVD_OPND_FAULT;
        }

return val;
}

/* Write IS1000 specific IPR's */

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
    { XSBASE, XSBASE+XSSIZE, &xs_rd, &xs_wr },
    { DZBASE, DZBASE+DZSIZE, &dz_rd, &dz_wr },
    { RZBASE, RZBASE+RZSIZE, &rz_rd, &rz_wr },
    { CFGBASE, CFGBASE+CFGSIZE, &cfg_rd, &led_wr },
    { ROMBASE, ROMBASE+ROMSIZE, &rom_rd, NULL },
    { NVRBASE, NVRBASE+NVRSIZE, &nvr_rd, &nvr_wr },
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

/* IS1000 registers */

int32 ka_rd (int32 pa)
{
int32 rg = (pa >> 2) & 0x1B;                            /* registers appear multiple times */
int32 val = 0;

switch (rg) {

    case 0:                                             /* diag timer */
    case 1:
        ka_tmr = (ka_tmr + 1) & 0xFFFF;
        sim_debug (DBG_REG, &sysd_dev, "ka_rd: TMR = %04X at %08X\n", ka_tmr, fault_PC);
        val = ka_tmr;
        break;

    case 2:                                             /* halt code */
        val = ka_hltcod;
        break;

    case 3:                                             /* MEAR */
        val = ka_mear;
        break;

    case 8:                                             /* int req */
    case 10:
        val = int_req[0];
        break;

    case 9:                                             /* int mask */
    case 11:
        val = int_mask;
        break;

    case 16:                                            /* MSER */
        val = ka_mser;
        break;

    case 17:                                            /* DMA byte offset */
        val = ka_boff;
        break;

    case 18:                                            /* ? */
        val = 0;
        break;

    case 19:                                            /* DMA map base */
        val = ka_mapbase;
        break;
        }

return val;
}

void ka_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa >> 2) & 0x1B;                            /* registers appear multiple times */

switch (rg) {

    case 0:                                             /* diag timer */
    case 1:
        ka_tmr = val;
        sim_debug (DBG_REG, &sysd_dev, "ka_wr: TMR = %04X at %08X\n", ka_tmr, fault_PC);
        break;

    case 2:                                             /* halt code */
        ka_hltcod = val;
        break;

    case 3:                                             /* MEAR */
        break;

    case 8:                                             /* int req */
    case 10:
        int_req[0] = int_req[0] & ~val;
        break;

    case 9:                                             /* int mask */
    case 11:
        int_mask = val;
        SET_IRQL;
        break;

    case 16:                                            /* MSER */
        ka_mser = val;
        break;

    case 17:                                            /* DMA byte offset */
        ka_boff = val;
        break;

    case 18:                                            /* ? */
        break;

    case 19:                                            /* DMA map base */
        ka_mapbase = val;
        break;
        }
return;
}

int32 sysd_hlt_enb (void)
{
return 1;
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

t_stat is1000_boot (int32 flag, CONST char *ptr)
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
ka_hltcod = 0;
ka_cfgtst = 0xFFAB;
ka_mapbase = 0;
ka_boff = 0;
ka_mser = 0;
ka_mear = 0;
ka_tmr = 0;
sim_vm_cmd = is1000_cmd;
return SCPE_OK;
}

const char *sysd_description (DEVICE *dptr)
{
return "system devices";
}

/* Terminal input routines

   tti_svc      process event (character ready)
*/

t_stat tti_svc (UNIT *uptr)
{
int32 c;

sim_clock_coschedule_tmr (uptr, TMR_CLK, TMXR_MULT);    /* continue poll */

if ((dz_csr & CSR_RDONE) &&                             /* input still pending and < 500ms? */
    ((sim_os_msec () - dz_buftime) < 500))
     return SCPE_OK;
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
if (c & SCPE_BREAK) {                                   /* break? */
    if (sysd_hlt_enb ())                                /* if enabled, halt */
        hlt_pin = 1;
    uptr->buf = TTIBUF_FRM | TTIBUF_RBR;
    }
else uptr->buf = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags));
uptr->buf = uptr->buf | TTIBUF_VLD;
dz_buftime = sim_os_msec ();
uptr->pos = uptr->pos + 1;
dz_csr = dz_csr | CSR_RDONE;
SET_INT (DZRX);
return SCPE_OK;
}

/* Terminal output routines

   tto_svc      process event (character typed)
*/

t_stat tto_svc (UNIT *uptr)
{
int32 c;
t_stat r;

c = sim_tt_outcvt (uptr->buf, TT_GET_MODE (uptr->flags));
if (c >= 0) {
    if ((r = sim_putchar_s (c)) != SCPE_OK) {           /* output; error? */
        sim_activate (uptr, uptr->wait);                /* retry */
        return ((r == SCPE_STALL)? SCPE_OK: r);         /* !stall? report */
        }
    }
dz_csr = dz_csr | CSR_XDONE;
SET_INT (DZTX);
uptr->pos = uptr->pos + 1;
return SCPE_OK;
}

t_stat dz_reset (DEVICE *dptr)
{
tmxr_set_console_units (&dz_unit[0], &dz_unit[1]);
dz_unit[0].buf = 0;
dz_unit[1].buf = 0;
dz_csr = CSR_XDONE;
sim_activate (&dz_unit[0], tmr_poll);
sim_cancel (&dz_unit[1]);                               /* deactivate unit */
return SCPE_OK;
}

t_stat dz_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Console Terminal (DZ)\n\n");
fprintf (st, "The terminal input (DZ) polls the console keyboard for input.\n\n");
fprintf (st, "When the console terminal is attached to a Telnet session or the simulator is\n");
fprintf (st, "running from a Windows command prompt, it recognizes BREAK.  If BREAK is\n");
fprintf (st, "entered, and BDR<7> is set (also known as SET CPU NOAUTOBOOT), control returns\n");
fprintf (st, "to the console firmware; otherwise, BREAK is treated as a normal terminal\n");
fprintf (st, "input condition.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *dz_description (DEVICE *dptr)
{
return "console terminal";
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
return SCPE_ARG;
}

t_stat cpu_print_model (FILE *st)
{
fprintf (st, "%s", sim_name);
return SCPE_OK;
}

t_stat cpu_model_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Initial memory size is 4MB.\n\n");
fprintf (st, "The simulator is booted with the BOOT command:\n\n");
fprintf (st, "   sim> BOOT\n\n");
return SCPE_OK;
}
