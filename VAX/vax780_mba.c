/* vax780_mba.c: VAX 11/780 Massbus adapter

   Copyright (c) 2004-2008, Robert M Supnik

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

   mba0, mba1           RH780 Massbus adapter

   28-May-08    RMS     Inlined physical memory routines
*/

#include "vax_defs.h"

/* Massbus */

#define MBA_NMAPR       256                             /* number of map reg */
#define MBA_V_RTYPE     10                              /* nexus addr: reg type */
#define MBA_M_RTYPE     0x3
#define  MBART_INT      0x0                             /* internal */
#define  MBART_EXT      0x1                             /* external */
#define  MBART_MAP      0x2                             /* map */
#define MBA_V_INTOFS    2                               /* int reg: reg ofs */
#define MBA_M_INTOFS    0xFF
#define MBA_V_DRV       7                               /* ext reg: drive num */
#define MBA_M_DRV       0x7
#define MBA_V_DEVOFS    2                               /* ext reg: reg ofs */
#define MBA_M_DEVOFS    0x1F
#define MBA_RTYPE(x)    (((x) >> MBA_V_RTYPE) & MBA_M_RTYPE)
#define MBA_INTOFS(x)   (((x) >> MBA_V_INTOFS) & MBA_M_INTOFS)
#define MBA_EXTDRV(x)   (((x) >> MBA_V_DRV) & MBA_M_DRV)
#define MBA_EXTOFS(x)   (((x) >> MBA_V_DEVOFS) & MBA_M_DEVOFS)

/* Massbus configuration register */

#define MBACNF_OF       0x0
#define MBACNF_ADPDN    0x00800000                      /* adap pdn - ni */
#define MBACNF_ADPUP    0x00400000                      /* adap pup - ni */
#define MBACNF_CODE     0x00000020
#define MBACNF_RD       (SBI_FAULTS|MBACNF_W1C)
#define MBACNF_W1C      0x00C00000

/* Control register */

#define MBACR_OF        0x1
#define MBACR_MNT       0x00000008                      /* maint */
#define MBACR_IE        0x00000004                      /* int enable */
#define MBACR_ABORT     0x00000002                      /* abort */
#define MBACR_INIT      0x00000001
#define MBACR_RD        0x0000000E
#define MBACR_WR        0x0000000E

/* Status register */

#define MBASR_OF        0x2
#define MBASR_DTBUSY    0x80000000                      /* DT busy RO */
#define MBASR_NRCONF    0x40000000                      /* no conf - ni W1C */
#define MBASR_CRD       0x20000000                      /* CRD - ni W1C */
#define MBASR_CBH       0x00800000                      /* CBHUNG - ni W1C */
#define MBASR_PGE       0x00080000                      /* prog err - W1C int */
#define MBASR_NFD       0x00040000                      /* nx drive - W1C int */
#define MBASR_MCPE      0x00020000                      /* ctl perr - ni W1C int */
#define MBASR_ATA       0x00010000                      /* attn - W1C int */
#define MBASR_SPE       0x00004000                      /* silo perr - ni W1C int */
#define MBASR_DTCMP     0x00002000                      /* xfr done - W1C int */
#define MBASR_DTABT     0x00001000                      /* abort - W1C int */
#define MBASR_DLT       0x00000800                      /* dat late - ni W1C abt */
#define MBASR_WCEU      0x00000400                      /* wrchk upper - W1C abt */
#define MBASR_WCEL      0x00000200                      /* wrchk lower - W1C abt */
#define MBASR_MXF       0x00000100                      /* miss xfr - ni W1C abt */
#define MBASR_MBEXC     0x00000080                      /* except - ni W1C abt */
#define MBASR_MBDPE     0x00000040                      /* dat perr - ni W1C abt */
#define MBASR_MAPPE     0x00000020                      /* map perr - ni W1C abt */
#define MBASR_INVM      0x00000010                      /* inv map - W1C abt */
#define MBASR_ERCONF    0x00000008                      /* err conf - ni W1C abt */
#define MBASR_RDS       0x00000004                      /* RDS - ni W1C abt */
#define MBASR_ITMO      0x00000002                      /* timeout - W1C abt */
#define MBASR_RTMO      0x00000001                      /* rd timeout - W1C abt */
#define MBASR_RD        0xE08F7FFF
#define MBASR_W1C       0x608F7FFF
#define MBASR_ABORTS    0x00000FFF
#define MBASR_ERRORS    0x608E49FF
#define MBASR_INTR      0x000F7000

/* Virtual address register */

#define MBAVA_OF        0x3
#define MBAVA_RD        0x0001FFFF
#define MBAVA_WR        (MBAVA_RD)

/* Byte count */

#define MBABC_OF        0x4
#define MBABC_WR        0x0000FFFF
#define MBABC_V_MBC     16                              /* MB count */

/* Diagnostic register */

#define MBADR_OF        0x5
#define MBADR_RD        0xFFFFFFFF
#define MBADR_WR        0xFFC00000

/* Selected map entry - read only */

#define MBASMR_OF       0x6
#define MBASMR_RD       (MBAMAP_RD)

/* Command register (SBI) - read only */

#define MBACMD_OF       0x7

/* External registers */

#define MBA_CS1         0x00                            /* device CSR1 */
#define MBA_CS1_WR      0x3F                            /* writeable bits */
#define MBA_CS1_DT      0x28                            /* >= for data xfr */

/* Map registers */

#define MBAMAP_VLD      0x80000000                      /* valid */
#define MBAMAP_PAG      0x001FFFFF
#define MBAMAP_RD       (MBAMAP_VLD | MBAMAP_PAG)
#define MBAMAP_WR       (MBAMAP_RD)

/* Debug switches */

#define MBA_DEB_RRD     0x01                            /* reg reads */
#define MBA_DEB_RWR     0x02                            /* reg writes */
#define MBA_DEB_MRD     0x04                            /* map reads */
#define MBA_DEB_MWR     0x08                            /* map writes */
#define MBA_DEB_XFR     0x10                            /* transfers */
#define MBA_DEB_ERR     0x20                            /* errors */

uint32 mba_cnf[MBA_NUM];                                /* config reg */
uint32 mba_cr[MBA_NUM];                                 /* control reg */
uint32 mba_sr[MBA_NUM];                                 /* status reg */
uint32 mba_va[MBA_NUM];                                 /* virt addr */
uint32 mba_bc[MBA_NUM];                                 /* byte count */
uint32 mba_dr[MBA_NUM];                                 /* diag reg */
uint32 mba_smr[MBA_NUM];                                /* sel map reg */
uint32 mba_map[MBA_NUM][MBA_NMAPR];                     /* map */

extern uint32 nexus_req[NEXUS_HLVL];
extern UNIT cpu_unit;
extern FILE *sim_log;
extern FILE *sim_deb;
extern int32 sim_switches;

t_stat mba_reset (DEVICE *dptr);
t_stat mba_rdreg (int32 *val, int32 pa, int32 mode);
t_stat mba_wrreg (int32 val, int32 pa, int32 lnt);
t_bool mba_map_addr (uint32 va, uint32 *ma, uint32 mb);
void mba_set_int (uint32 mb);
void mba_clr_int (uint32 mb);
void mba_upd_sr (uint32 set, uint32 clr, uint32 mb);
DIB mba0_dib, mba1_dib;

/* Massbus register dispatches */

static t_stat (*mbregR[MBA_NUM])(int32 *dat, int32 ad, int32 md);
static t_stat (*mbregW[MBA_NUM])(int32 dat, int32 ad, int32 md);
static int32 (*mbabort[MBA_NUM])(void);

/* Massbus adapter data structures

   mba_dev      MBA device descriptors
   mbax_unit    MBA unit
   mbax_reg     MBA register list
*/

DIB mba0_dib = { TR_MBA0, 0, &mba_rdreg, &mba_wrreg, 0, NVCL (MBA0) };

UNIT mba0_unit = { UDATA (NULL, 0, 0) };

REG mba0_reg[] = {
    { HRDATA (CNFR, mba_cnf[0], 32) },
    { HRDATA (CR, mba_cr[0], 4) },
    { HRDATA (SR, mba_sr[0], 32) },
    { HRDATA (VA, mba_va[0], 17) },
    { HRDATA (BC, mba_bc[0], 16) },
    { HRDATA (DR, mba_dr[0], 32) },
    { HRDATA (SMR, mba_dr[0], 32) },
    { BRDATA (MAP, mba_map[0], 16, 32, MBA_NMAPR) },
    { FLDATA (NEXINT, nexus_req[IPL_MBA0], TR_MBA0) },
    { NULL }
    };

MTAB mba0_mod[] = {
    { MTAB_XTD|MTAB_VDV, TR_MBA0, "NEXUS", NULL,
      NULL, &show_nexus },
    { 0 }
    };

DIB mba1_dib = { TR_MBA1, 0, &mba_rdreg, &mba_wrreg, 0, NVCL (MBA1) };

UNIT mba1_unit = { UDATA (NULL, 0, 0) };

MTAB mba1_mod[] = {
    { MTAB_XTD|MTAB_VDV, TR_MBA1, "NEXUS", NULL,
      NULL, &show_nexus },
    { 0 }
    };

REG mba1_reg[] = {
    { HRDATA (CNFR, mba_cnf[1], 32) },
    { HRDATA (CR, mba_cr[1], 4) },
    { HRDATA (SR, mba_sr[1], 32) },
    { HRDATA (VA, mba_va[1], 17) },
    { HRDATA (BC, mba_bc[1], 16) },
    { HRDATA (DR, mba_dr[1], 32) },
    { HRDATA (SMR, mba_dr[1], 32) },
    { BRDATA (MAP, mba_map[1], 16, 32, MBA_NMAPR) },
    { FLDATA (NEXINT, nexus_req[IPL_MBA1], TR_MBA1) },
    { NULL }
    };

DEBTAB mba_deb[] = {
    { "REGREAD", MBA_DEB_RRD },
    { "REGWRITE", MBA_DEB_RWR },
    { "MAPREAD", MBA_DEB_MRD },
    { "MAPWRITE", MBA_DEB_MWR },
    { "XFER", MBA_DEB_XFR },
    { "ERROR", MBA_DEB_ERR },
    { NULL, 0 }
    };

DEVICE mba_dev[] = {
    {
    "MBA0", &mba0_unit, mba0_reg, mba0_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &mba_reset,
    NULL, NULL, NULL,
    &mba0_dib, DEV_NEXUS | DEV_DEBUG, 0,
    mba_deb, 0, 0
    },
    {
    "MBA1", &mba1_unit, mba1_reg, mba1_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &mba_reset,
    NULL, NULL, NULL,
    &mba1_dib, DEV_NEXUS | DEV_DEBUG, 0,
    mba_deb, 0, 0
    }
    };

/* Read Massbus adapter register */

t_stat mba_rdreg (int32 *val, int32 pa, int32 lnt)
{
int32 mb, ofs, drv, rtype;
uint32 t;
t_stat r;

mb = NEXUS_GETNEX (pa) - TR_MBA0;                       /* get MBA */
if ((pa & 3) || (lnt != L_LONG)) {                      /* unaligned or not lw? */
    printf (">>MBA%d: invalid adapter read mask, pa = %X, lnt = %d\r\n", mb, pa, lnt);
    sbi_set_errcnf ();                                  /* err confirmation */
    return SCPE_OK;
    }
if (mb >= MBA_NUM)                                      /* valid? */
    return SCPE_NXM;
rtype = MBA_RTYPE (pa);                                 /* get reg type */

switch (rtype) {                                        /* case on type */

    case MBART_INT:                                     /* internal */
        ofs = MBA_INTOFS (pa);                          /* check range */
        switch (ofs) {

        case MBACNF_OF:                                 /* CNF */
            *val = (mba_cnf[mb] & MBACNF_RD) | MBACNF_CODE;
            break;

        case MBACR_OF:                                  /* CR */
            *val = mba_cr[mb] & MBACR_RD;
            break;

        case MBASR_OF:                                  /* SR */
            *val = mba_sr[mb] & MBASR_RD;
            break;

        case MBAVA_OF:                                  /* VA */
            *val = mba_va[mb] & MBAVA_RD;
            break;

        case MBABC_OF:                                  /* BC */
            t = mba_bc[mb] & MBABC_WR;
            *val = (t << MBABC_V_MBC) | t;
             break;

        case MBADR_OF:                                  /* DR */
            *val = mba_dr[mb] & MBADR_RD;
             break;

        case MBASMR_OF:                                 /* SMR */
            *val = mba_smr[mb] & MBASMR_RD;
            break;

        case MBACMD_OF:                                 /* CMD */
            *val = 0;
             break;

        default:
            return SCPE_NXM;
            }
        if (DEBUG_PRI (mba_dev[mb], MBA_DEB_RRD))
            fprintf (sim_deb, ">>MBA%d: int reg %d read, value = %X\n", mb, ofs, *val);
        break;

    case MBART_EXT:                                     /* external */
        if (!mbregR[mb])                                /* device there? */
            return SCPE_NXM;
        drv = MBA_EXTDRV (pa);                          /* get dev num */
        ofs = MBA_EXTOFS (pa);                          /* get reg offs */
        r = mbregR[mb] (val, ofs, drv);                 /* call device */
        if (r == MBE_NXD)                               /* nx drive? */
            mba_upd_sr (MBASR_NFD, 0, mb);
        else if (r == MBE_NXR)                          /* nx reg? */
            return SCPE_NXM;
        *val |= (mba_sr[mb] & ~WMASK);                  /* upper 16b from SR */
        if (DEBUG_PRI (mba_dev[mb], MBA_DEB_RRD))
            fprintf (sim_deb, ">>MBA%d: drv %d ext reg %d read, value = %X\n", mb, drv, ofs, *val);
        break; 

    case MBART_MAP:                                     /* map */
        ofs = MBA_INTOFS (pa);
        *val = mba_map[mb][ofs] & MBAMAP_RD;
        if (DEBUG_PRI (mba_dev[mb], MBA_DEB_MRD))
            fprintf (sim_deb, ">>MBA%d: map %d read, value = %X\n", mb, ofs, *val);
        break;

    default:
        return SCPE_NXM;
        }

return SCPE_OK;
}

/* Write Massbus adapter register */

t_stat mba_wrreg (int32 val, int32 pa, int32 lnt)
{
int32 mb, ofs, drv, rtype;
t_stat r;
t_bool cs1dt;

mb = NEXUS_GETNEX (pa) - TR_MBA0;                       /* get MBA */
if ((pa & 3) || (lnt != L_LONG)) {                      /* unaligned or not lw? */
    printf (">>MBA%d: invalid adapter write mask, pa = %X, lnt = %d\r\n", mb, pa, lnt);
    sbi_set_errcnf ();                                  /* err confirmation */
    return SCPE_OK;
    }
if (mb >= MBA_NUM)                                      /* valid? */
    return SCPE_NXM;
rtype = MBA_RTYPE (pa);                                 /* get reg type */

switch (rtype) {                                        /* case on type */

    case MBART_INT:                                     /* internal */
        ofs = MBA_INTOFS (pa);                          /* check range */
        switch (ofs) {

        case MBACNF_OF:                                 /* CNF */
            mba_cnf[mb] &= ~(val & MBACNF_W1C);
            break;

        case MBACR_OF:                                  /* CR */
            if (val & MBACR_INIT)                       /* init? */
                mba_reset (&mba_dev[mb]);               /* reset MBA */
            if ((val & MBACR_ABORT) &&
                (mba_sr[mb] & MBASR_DTBUSY)) {
                if (mbabort[mb])                        /* abort? */
                    mbabort[mb] ();
                mba_upd_sr (MBASR_DTABT, 0, mb);
                }
            if ((val & MBACR_MNT) &&
                (mba_sr[mb] & MBASR_DTBUSY)) {
                mba_upd_sr (MBASR_PGE, 0, mb);          /* mnt & xfer? */
                val = val & ~MBACR_MNT;
                }
            if ((val & MBACR_IE) == 0)
                mba_clr_int (mb);
            mba_cr[mb] = (mba_cr[mb] & ~MBACR_WR) |
                (val & MBACR_WR);
            break;

        case MBASR_OF:                                  /* SR */
            mba_sr[mb] = mba_sr[mb] & ~(val & MBASR_W1C);
            break;

        case MBAVA_OF:                                  /* VA */
            if (mba_sr[mb] & MBASR_DTBUSY)              /* err if xfr */
                mba_upd_sr (MBASR_PGE, 0, mb);
            else mba_va[mb] = val & MBAVA_WR;
            break;

        case MBABC_OF:                                  /* BC */
            if (mba_sr[mb] & MBASR_DTBUSY)              /* err if xfr */
                mba_upd_sr (MBASR_PGE, 0, mb);
            else mba_bc[mb] = val & MBABC_WR;
            break;

        case MBADR_OF:                                  /* DR */
            mba_dr[mb] = (mba_dr[mb] & ~MBADR_WR) |
                (val & MBADR_WR);
            break;

        default:
            return SCPE_NXM;
            }
        if (DEBUG_PRI (mba_dev[mb], MBA_DEB_RWR))
            fprintf (sim_deb, ">>MBA%d: int reg %d write, value = %X\n", mb, ofs, val);
        break;

    case MBART_EXT:                                     /* external */
        if (!mbregW[mb])                                /* device there? */
            return SCPE_NXM;
        drv = MBA_EXTDRV (pa);                          /* get dev num */
        ofs = MBA_EXTOFS (pa);                          /* get reg offs */
        cs1dt = (ofs == MBA_CS1) && (val & CSR_GO) &&   /* starting xfr? */
           ((val & MBA_CS1_WR) >= MBA_CS1_DT);
        if (cs1dt && (mba_sr[mb] & MBASR_DTBUSY)) {     /* xfr while busy? */
            mba_upd_sr (MBASR_PGE, 0, mb);              /* prog error */
            break;
            }
        r = mbregW[mb] (val & WMASK, ofs, drv);         /* write dev reg */
        if (r == MBE_NXD)                               /* nx drive? */
            mba_upd_sr (MBASR_NFD, 0, mb);
        else if (r == MBE_NXR)                          /* nx reg? */
            return SCPE_NXM;
        if (cs1dt && (r == SCPE_OK))                    /* did dt start? */     
            mba_sr[mb] = (mba_sr[mb] | MBASR_DTBUSY) & ~MBASR_W1C;
        if (DEBUG_PRI (mba_dev[mb], MBA_DEB_RWR))
            fprintf (sim_deb, ">>MBA%d: drv %d ext reg %d write, value = %X\n", mb, drv, ofs, val);
        break; 

    case MBART_MAP:                                     /* map */
        ofs = MBA_INTOFS (pa);
        mba_map[mb][ofs] = val & MBAMAP_WR;
        if (DEBUG_PRI (mba_dev[mb], MBA_DEB_MWR))
            fprintf (sim_deb, ">>MBA%d: map %d write, value = %X\n", mb, ofs, val);
        break;

    default:
        return SCPE_NXM;
        }

return SCPE_OK;
}

/* Massbus I/O routine

   mb_rdbufW    -       fetch word buffer from memory
   mb_wrbufW    -       store word buffer into memory
   mb_chbufW    -       compare word buffer with memory

   Returns number of bytes successfully transferred/checked
*/

int32 mba_rdbufW (uint32 mb, int32 bc, uint16 *buf)
{
int32 i, j, ba, mbc, pbc;
uint32 pa, dat;

if (mb >= MBA_NUM)                                      /* valid MBA? */
    return 0;
ba = mba_va[mb];                                        /* get virt addr */
mbc = (MBABC_WR + 1) - mba_bc[mb];                      /* get Mbus bc */
if (bc > mbc)                                           /* use smaller */
    bc = mbc;
for (i = 0; i < bc; i = i + pbc) {                      /* loop by pages */
    if (!mba_map_addr (ba + i, &pa, mb))                /* page inv? */
        break;
    if (!ADDR_IS_MEM (pa)) {                            /* NXM? */
        mba_upd_sr (MBASR_RTMO, 0, mb);
        break;
        }
    pbc = VA_PAGSIZE - VA_GETOFF (pa);                  /* left in page */
    if (pbc > (bc - i))                                 /* limit to rem xfr */
        pbc = bc - i;
    if (DEBUG_PRI (mba_dev[mb], MBA_DEB_XFR))
        fprintf (sim_deb, ">>MBA%d: read, pa = %X, bc = %X\n", mb, pa, pbc);
    if ((pa | pbc) & 1) {                               /* aligned word? */
        for (j = 0; j < pbc; pa++, j++) {               /* no, bytes */
            if ((i + j) & 1) {                          /* odd byte? */
                *buf = (*buf & BMASK) | (ReadB (pa) << 8);
                buf++;
                }
            else *buf = (*buf & ~BMASK) | ReadB (pa);
            }
        }
    else if ((pa | pbc) & 3) {                          /* aligned LW? */
        for (j = 0; j < pbc; pa = pa + 2, j = j + 2) {  /* no, words */
            *buf++ = ReadW (pa);                        /* get word */
            }
        }
    else {                                              /* yes, do by LW */
        for (j = 0; j < pbc; pa = pa + 4, j = j + 4) {
            dat = ReadL (pa);                           /* get lw */
            *buf++ = dat & WMASK;                       /* low 16b */
            *buf++ = (dat >> 16) & WMASK;               /* high 16b */
            }
        }
    }
mba_bc[mb] = (mba_bc[mb] + i) & MBABC_WR;
mba_va[mb] = (mba_va[mb] + i) & MBAVA_WR;
return i;
}

int32 mba_wrbufW (uint32 mb, int32 bc, uint16 *buf)
{
int32 i, j, ba, mbc, pbc;
uint32 pa, dat;

if (mb >= MBA_NUM)                                      /* valid MBA? */
    return 0;
ba = mba_va[mb];                                        /* get virt addr */
mbc = (MBABC_WR + 1) - mba_bc[mb];                      /* get Mbus bc */
if (bc > mbc)                                           /* use smaller */
    bc = mbc;
for (i = 0; i < bc; i = i + pbc) {                      /* loop by pages */
    if (!mba_map_addr (ba + i, &pa, mb))                /* page inv? */
        break;
    if (!ADDR_IS_MEM (pa)) {                            /* NXM? */
        mba_upd_sr (MBASR_RTMO, 0, mb);
        break;
        }
    pbc = VA_PAGSIZE - VA_GETOFF (pa);                  /* left in page */
    if (pbc > (bc - i))                                 /* limit to rem xfr */
        pbc = bc - i;
    if (DEBUG_PRI (mba_dev[mb], MBA_DEB_XFR))
        fprintf (sim_deb, ">>MBA%d: write, pa = %X, bc = %X\n", mb, pa, pbc);
    if ((pa | pbc) & 1) {                               /* aligned word? */
        for (j = 0; j < pbc; pa++, j++) {               /* no, bytes */
            if ((i + j) & 1) {
                WriteB (pa, (*buf >> 8) & BMASK);
                buf++;
                }
            else WriteB (pa, *buf & BMASK);
            }
        }
    else if ((pa | pbc) & 3) {                          /* aligned LW? */
        for (j = 0; j < pbc; pa = pa + 2, j = j + 2) {  /* no, words */
            WriteW (pa, *buf);                          /* write word */
            buf++;
            }
        }
    else {                                              /* yes, do by LW */
        for (j = 0; j < pbc; pa = pa + 4, j = j + 4) {
            dat = (uint32) *buf++;                      /* get low 16b */
            dat = dat | (((uint32) *buf++) << 16);      /* merge hi 16b */
            WriteL (pa, dat);                           /* store LW */
            }
        }
    }
mba_bc[mb] = (mba_bc[mb] + i) & MBABC_WR;
mba_va[mb] = (mba_va[mb] + i) & MBAVA_WR;
return i;
}

int32 mba_chbufW (uint32 mb, int32 bc, uint16 *buf)
{
int32 i, j, ba, mbc, pbc;
uint32 pa, dat, cmp;

if (mb >= MBA_NUM)                                      /* valid MBA? */
    return 0;
ba = mba_va[mb];                                        /* get virt addr */
mbc = (MBABC_WR + 1) - mba_bc[mb];                      /* get Mbus bc */
if (bc > mbc)                                           /* use smaller */
    bc = mbc;
for (i = 0; i < bc; i = i + pbc) {                      /* loop by pages */
    if (!mba_map_addr (ba + i, &pa, mb))                /* page inv? */
        break;
    if (!ADDR_IS_MEM (pa)) {                            /* NXM? */
        mba_upd_sr (MBASR_RTMO, 0, mb);
        break;
        }
    pbc = VA_PAGSIZE - VA_GETOFF (pa);                  /* left in page */
    if (DEBUG_PRI (mba_dev[mb], MBA_DEB_XFR))
        fprintf (sim_deb, ">>MBA%d: check, pa = %X, bc = %X\n", mb, pa, pbc);
    if (pbc > (bc - i))                                 /* limit to rem xfr */
        pbc = bc - i;
    for (j = 0; j < pbc; j++, pa++) {                   /* byte by byte */
        cmp = ReadB (pa);
        if ((i + j) & 1)
            dat = (*buf++ >> 8) & BMASK;
        else dat = *buf & BMASK;
        if (cmp != dat) {
            mba_upd_sr ((j & 1)? MBASR_WCEU: MBASR_WCEL, 0, mb);
            break;
            }                                           /* end if */
        }                                               /* end for j */
    }                                                   /* end for i */
mba_bc[mb] = (mba_bc[mb] + i) & MBABC_WR;
mba_va[mb] = (mba_va[mb] + i) & MBAVA_WR;
return i;
}

/* Map an address via the translation map */

t_bool mba_map_addr (uint32 va, uint32 *ma, uint32 mb)
{
uint32 vblk = (va >> VA_V_VPN);                         /* map index */
uint32 mmap = mba_map[mb][vblk];                        /* get map */

mba_smr[mb] = mmap;                                     /* save map reg */
if (mmap & MBAMAP_VLD) {                                /* valid? */
    *ma = ((mmap & MBAMAP_PAG) << VA_V_VPN) + VA_GETOFF (va);
    return 1;                                           /* legit addr */
    }
mba_upd_sr (MBASR_INVM, 0, mb);                         /* invalid map */
return 0;
}

/* Device access, status, and interrupt routines */

void mba_set_don (uint32 mb)
{
mba_upd_sr (MBASR_DTCMP, 0, mb);
return;
}

void mba_upd_ata (uint32 mb, uint32 val)
{
if (val)
    mba_upd_sr (MBASR_ATA, 0, mb);
else mba_upd_sr (0, MBASR_ATA, mb);
return;
}

void mba_set_exc (uint32 mb)
{
mba_upd_sr (MBASR_MBEXC, 0, mb);
if (DEBUG_PRI (mba_dev[mb], MBA_DEB_ERR))
    fprintf (sim_deb, ">>MBA%d: EXC write\n", mb);
return;
}

int32 mba_get_bc (uint32 mb)
{
if (mb >= MBA_NUM)
    return 0;
return (MBABC_WR + 1) - mba_bc[mb];
}

void mba_set_int (uint32 mb)
{
DIB *dibp;

if (mb >= MBA_NUM)
    return;
dibp = (DIB *) mba_dev[mb].ctxt;
if (dibp)
    nexus_req[dibp->vloc >> 5] |= (1u << (dibp->vloc & 0x1F));
return;
}

void mba_clr_int (uint32 mb)
{
DIB *dibp;

if (mb >= MBA_NUM)
    return;
dibp = (DIB *) mba_dev[mb].ctxt;
if (dibp)
    nexus_req[dibp->vloc >> 5] &= ~(1u << (dibp->vloc & 0x1F));
return;
}

void mba_upd_sr (uint32 set, uint32 clr, uint32 mb)
{
if (mb >= MBA_NUM)
    return;
if (set & MBASR_ABORTS)
    set |= (MBASR_DTCMP|MBASR_DTABT);
if (set & (MBASR_DTCMP|MBASR_DTABT))
    mba_sr[mb] &= ~MBASR_DTBUSY;
mba_sr[mb] = (mba_sr[mb] | set) & ~clr;
if ((set & MBASR_INTR) && (mba_cr[mb] & MBACR_IE))
    mba_set_int (mb);
if ((set & MBASR_ERRORS) && (DEBUG_PRI (mba_dev[mb], MBA_DEB_ERR)))
    fprintf (sim_deb, ">>MBA%d: CS error = %X\n", mb, mba_sr[mb]);
return;
}

/* Reset Massbus adapter */

t_stat mba_reset (DEVICE *dptr)
{
int32 i, mb;
DIB *dibp;

dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
mb = dibp->ba - TR_MBA0;
if ((mb < 0) || (mb >= MBA_NUM))
    return SCPE_IERR;
mba_cnf[mb] = 0;
mba_cr[mb] &= MBACR_MNT;
mba_sr[mb] = 0;
mba_bc[mb] = 0;
mba_va[mb] = 0;
mba_dr[mb] = 0;
mba_smr[mb] = 0;
if (sim_switches & SWMASK ('P')) {
    for (i = 0; i < MBA_NMAPR; i++)
        mba_map[mb][i] = 0;
    }
if (mbabort[mb])                                        /* reset device */
    mbabort[mb] ();
return SCPE_OK;
}

/* Show Massbus adapter number */

t_stat mba_show_num (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr = find_dev_from_unit (uptr);
DIB *dibp;

if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
fprintf (st, "Massbus adapter %d", dibp->ba);
return SCPE_OK;
}

/* Enable/disable Massbus adapter */

void mba_set_enbdis (uint32 mb, t_bool dis)
{
if (mb >= MBA_NUM)                                      /* valid MBA? */
    return;
if (dis)
    mba_dev[mb].flags |= DEV_DIS;
else mba_dev[mb].flags &= ~DEV_DIS;
return;
}

/* Init Mbus tables */

void init_mbus_tab (void)
{
uint32 i;

for (i = 0; i < MBA_NUM; i++) {
    mbregR[i] = NULL;
    mbregW[i] = NULL;
    mbabort[i] = NULL;
    }
return;
}

/* Build dispatch tables */

t_stat build_mbus_tab (DEVICE *dptr, DIB *dibp)
{
uint32 idx;

if ((dptr == NULL) || (dibp == NULL))                   /* validate args */
    return SCPE_IERR;
idx = dibp->ba;                                         /* Mbus # */
if (idx >= MBA_NUM)
    return SCPE_STOP;
if ((mbregR[idx] && dibp->rd &&                         /* conflict? */
    (mbregR[idx] != dibp->rd)) ||
    (mbregW[idx] && dibp->wr &&
    (mbregW[idx] != dibp->wr)) ||
    (mbabort[idx] && dibp->ack[0] &&
    (mbabort[idx] != dibp->ack[0]))) {
        printf ("Massbus %s assignment conflict at %d\n",
                sim_dname (dptr), dibp->ba);
        if (sim_log)
            fprintf (sim_log, "Massbus %s assignment conflict at %d\n",
                     sim_dname (dptr), dibp->ba);
        return SCPE_STOP;
        }
if (dibp->rd)                                           /* set rd dispatch */
    mbregR[idx] = dibp->rd;
if (dibp->wr)                                           /* set wr dispatch */
    mbregW[idx] = dibp->wr;
if (dibp->ack[0])                                       /* set abort dispatch */
    mbabort[idx] = dibp->ack[0];
return SCPE_OK;
}

