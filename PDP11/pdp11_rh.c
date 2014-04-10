/* pdp11_rh.c: PDP-11 Massbus adapter simulator

   Copyright (c) 2005-2013, Robert M Supnik

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

   rha, rhb             RH11/RH70 Massbus adapter

   02-Sep-13    RMS     Added third Massbus adapter, debug printouts
   19-Mar-12    RMS     Fixed declaration of cpu_opt (Mark Pizzolato)
   02-Feb-08    RMS     Fixed DMA memory address limit test (John Dundas)
   17-May-07    RMS     Moved CS1 drive enable to devices
   21-Nov-05    RMS     Added enable/disable routine
   07-Jul-05    RMS     Removed extraneous externs

   WARNING: The interupt logic of the RH11/RH70 is unusual and must be
   simulated with great precision.  The RH11 has an internal interrupt
   request flop, CSTB INTR, which is controlled as follows:

   - Writing IE and DONE simultaneously sets CSTB INTR
   - Controller clear, INIT, and interrupt acknowledge clear CSTB INTR
     (and also clear IE)
   - A transition of DONE from 0 to 1 sets CSTB INTR from IE

   The output of CSTB INTR is OR'd with the AND of RPCS1<SC,DONE,IE> to
   create the interrupt request signal.  Thus,

   - The DONE interrupt is edge sensitive, but the SC interrupt is
     level sensitive.
   - The DONE interrupt, once set, is not disabled if IE is cleared,
     but the SC interrupt is.
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#error "PDP-10 uses pdp10_rp.c and pdp10_tu.c!"

#elif defined (VM_VAX)                                  /* VAX version */
#error "VAX uses vax780_mba.c!"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif

/* CS1 - base + 000 - control/status 1 */

#define CS1_OF          0
#define CS1_GO          CSR_GO                          /* go */
#define CS1_V_FNC       1                               /* function pos */
#define CS1_M_FNC       037                             /* function mask */
#define CS1_FNC         (CS1_M_FNC << CS1_V_FNC)
#define FNC_XFER        024                             /* >=? data xfr */
#define CS1_IE          CSR_IE                          /* int enable */
#define CS1_DONE        CSR_DONE                        /* ready */
#define CS1_V_UAE       8                               /* Unibus addr ext */
#define CS1_M_UAE       03
#define CS1_UAE         (CS1_M_UAE << CS1_V_UAE)
#define CS1_MCPE        0020000                         /* Mbus par err NI */
#define CS1_TRE         0040000                         /* transfer err */
#define CS1_SC          0100000                         /* special cond */
#define CS1_MBZ         0012000
#define CS1_DRV         (CS1_FNC | CS1_GO)
#define GET_FNC(x)      (((x) >> CS1_V_FNC) & CS1_M_FNC)

/* WC - base + 002 - word count */

#define WC_OF           1

/* BA - base + 004 - base address */

#define BA_OF           2
#define BA_MBZ          0000001                         /* must be zero */

/* CS2 - base + 010 - control/status 2 */

#define CS2_OF          3
#define CS2_V_UNIT      0                               /* unit pos */
#define CS2_M_UNIT      07                              /* unit mask */
#define CS2_UNIT        (CS2_M_UNIT << CS2_V_UNIT)
#define CS2_UAI         0000010                         /* addr inhibit */
#define CS2_PAT         0000020                         /* parity test NI */
#define CS2_CLR         0000040                         /* controller clear */
#define CS2_IR          0000100                         /* input ready */
#define CS2_OR          0000200                         /* output ready */
#define CS2_MDPE        0000400                         /* Mbus par err NI */
#define CS2_MXF         0001000                         /* missed xfer NI */
#define CS2_PGE         0002000                         /* program err */
#define CS2_NEM         0004000                         /* nx mem err */
#define CS2_NED         0010000                         /* nx drive err */
#define CS2_PE          0020000                         /* parity err NI */
#define CS2_WCE         0040000                         /* write check err */
#define CS2_DLT         0100000                         /* data late NI */
#define CS2_MBZ         (CS2_CLR)
#define CS2_RW          (CS2_UNIT | CS2_UAI | CS2_PAT | CS2_MXF | CS2_PE)
#define CS2_ERR         (CS2_MDPE | CS2_MXF | CS2_PGE | CS2_NEM | \
                         CS2_NED | CS2_PE | CS2_WCE | CS2_DLT)
#define GET_UNIT(x)     (((x) >> CS2_V_UNIT) & CS2_M_UNIT)

/* DB - base + 022 - data buffer */

#define DB_OF           4

/* BAE - base + 050/34 - bus address extension */

#define BAE_OF          5
#define AE_M_MAE        0                               /* addr ext pos */
#define AE_V_MAE        077                             /* addr ext mask */
#define AE_MBZ          0177700

/* CS3 - base + 052/36 - control/status 3 */

#define CS3_OF          6
#define CS3_APE         0100000                         /* addr perr - NI */
#define CS3_DPO         0040000                         /* data perr odd - NI */
#define CS3_DPE         0020000                         /* data perr even - NI */
#define CS3_WCO         0010000                         /* wchk err odd */
#define CS3_WCE         0004000                         /* wchk err even */
#define CS3_DBL         0002000                         /* dbl word xfer - NI */
#define CS3_IPCK        0000017                         /* wrong par - NI */
#define CS3_ERR         (CS3_APE|CS3_DPO|CS3_DPE|CS3_WCO|CS3_WCE)
#define CS3_MBZ         0001660
#define CS3_RW          (CS1_IE | CS3_IPCK)

#define MBA_OFSMASK     077                             /* max 32 reg */
#define INT             0000                            /* int reg flag */
#define EXT             0100                            /* ext reg flag */

/* Declarations */

#define RH11            (cpu_opt & OPT_RH11)

typedef struct {
    uint32 cs1;                                         /* ctrl/status 1 */
    uint32 wc;                                          /* word count */
    uint32 ba;                                          /* bus addr */
    uint32 cs2;                                         /* ctrl/status 2 */
    uint32 db;                                          /* data buffer */
    uint32 bae;                                         /* addr ext */
    uint32 cs3;                                         /* ctrl/status 3 */
    uint32 iff;                                         /* int flip flop */
    } MBACTX;

MBACTX massbus[MBA_NUM];

extern uint32 cpu_opt;
extern int32 cpu_bme;
extern uint16 *M;
extern int32 int_req[IPL_HLVL];
extern t_addr cpu_memsize;

t_stat mba_reset (DEVICE *dptr);
t_stat mba_rd (int32 *val, int32 pa, int32 access);
t_stat mba_wr (int32 val, int32 pa, int32 access);
t_stat mba_set_type (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat mba_show_type (FILE *st, UNIT *uptr, int32 val, void *desc);
int32 mba0_inta (void);
int32 mba1_inta (void);
int32 mba2_inta (void);
void mba_set_int (uint32 mb);
void mba_clr_int (uint32 mb);
void mba_upd_cs1 (uint32 set, uint32 clr, uint32 mb);
void mba_set_cs2 (uint32 flg, uint32 mb);
int32 mba_map_pa (int32 pa, int32 *ofs);
t_stat rh_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *rh_description (DEVICE *dptr);

extern uint32 Map_Addr (uint32 ba);

/* Massbus register dispatches */

static t_stat (*mbregR[MBA_NUM])(int32 *dat, int32 ad, int32 md);
static t_stat (*mbregW[MBA_NUM])(int32 dat, int32 ad, int32 md);
static int32 (*mbabort[MBA_NUM])(void);

/* Unibus to register offset map */

static int32 mba_mapofs[(MBA_OFSMASK + 1) >> 1] = {
 INT|0,  INT|1,  INT|2,  EXT|5,  INT|3,  EXT|1,  EXT|2,  EXT|4,
 EXT|7,  INT|4,  EXT|3,  EXT|6,  EXT|8,  EXT|9,  EXT|10, EXT|11,
 EXT|12, EXT|13, EXT|14, EXT|15, EXT|16, EXT|17, EXT|18, EXT|19,
 EXT|20, EXT|21, EXT|22, EXT|23, EXT|24, EXT|25, EXT|26, EXT|27
 };

/* Massbus adapter data structures

   mbax_dev     RHx device descriptor
   mbax_unit    RHx units
   mbax_reg     RHx register list
*/

#define IOLN_RP         054

DIB mba0_dib = {
    IOBA_AUTO, IOLN_RP, &mba_rd, &mba_wr,
    1, IVCL (RP), VEC_AUTO, { &mba0_inta }, IOLN_RP,
    };

UNIT mba0_unit = { UDATA (NULL, 0, 0) };

REG mba0_reg[] = {
    { ORDATA (CS1, massbus[0].cs1, 16) },
    { ORDATA (WC, massbus[0].wc, 16) },
    { ORDATA (BA, massbus[0].ba, 16) },
    { ORDATA (CS2, massbus[0].cs2, 16) },
    { ORDATA (DB, massbus[0].db, 16) },
    { ORDATA (BAE, massbus[0].bae, 6) },
    { ORDATA (CS3, massbus[0].cs3, 16) },
    { FLDATA (IFF, massbus[0].iff, 0) },
    { FLDATA (INT, IREQ (RP), INT_V_RP) },
    { FLDATA (SC, massbus[0].cs1, CSR_V_ERR) },
    { FLDATA (DONE, massbus[0].cs1, CSR_V_DONE) },
    { FLDATA (IE, massbus[0].cs1, CSR_V_IE) },
    { ORDATA (DEVADDR, mba0_dib.ba, 32), REG_HRO },
    { ORDATA (DEVVEC, mba0_dib.vec, 16), REG_HRO },
    { NULL }
    };

MTAB mba0_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0100, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR",
      &set_vec, &show_vec, NULL },
    { 0 }
    };

#define IOLN_TU         040

DIB mba1_dib = {
    IOBA_AUTO, IOLN_TU, &mba_rd, &mba_wr,
    1, IVCL (TU), VEC_AUTO, { &mba1_inta }, IOLN_TU
    };

UNIT mba1_unit = { UDATA (NULL, 0, 0) };

REG mba1_reg[] = {
    { ORDATA (CS1, massbus[1].cs1, 16) },
    { ORDATA (WC, massbus[1].wc, 16) },
    { ORDATA (BA, massbus[1].ba, 16) },
    { ORDATA (CS2, massbus[1].cs2, 16) },
    { ORDATA (DB, massbus[1].db, 16) },
    { ORDATA (BAE, massbus[1].bae, 6) },
    { ORDATA (CS3, massbus[1].cs3, 16) },
    { FLDATA (IFF, massbus[1].iff, 0) },
    { FLDATA (INT, IREQ (TU), INT_V_TU) },
    { FLDATA (SC, massbus[1].cs1, CSR_V_ERR) },
    { FLDATA (DONE, massbus[1].cs1, CSR_V_DONE) },
    { FLDATA (IE, massbus[1].cs1, CSR_V_IE) },
    { ORDATA (DEVADDR, mba1_dib.ba, 32), REG_HRO },
    { ORDATA (DEVVEC, mba1_dib.vec, 16), REG_HRO },
    { NULL }
    };

MTAB mba1_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0040, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR",
      &set_vec, &show_vec, NULL },
    { 0 }
    };

#define IOLN_RS         040

DIB mba2_dib = {
    IOBA_AUTO, IOLN_RS, &mba_rd, &mba_wr,
    1, IVCL (RS), VEC_AUTO, { &mba2_inta }, IOLN_RS
    };

UNIT mba2_unit = { UDATA (NULL, 0, 0) };

REG mba2_reg[] = {
    { ORDATA (CS1, massbus[2].cs1, 16) },
    { ORDATA (WC, massbus[2].wc, 16) },
    { ORDATA (BA, massbus[2].ba, 16) },
    { ORDATA (CS2, massbus[2].cs2, 16) },
    { ORDATA (DB, massbus[2].db, 16) },
    { ORDATA (BAE, massbus[2].bae, 6) },
    { ORDATA (CS3, massbus[2].cs3, 16) },
    { FLDATA (IFF, massbus[2].iff, 0) },
    { FLDATA (INT, IREQ (TU), INT_V_TU) },
    { FLDATA (SC, massbus[2].cs1, CSR_V_ERR) },
    { FLDATA (DONE, massbus[2].cs1, CSR_V_DONE) },
    { FLDATA (IE, massbus[2].cs1, CSR_V_IE) },
    { ORDATA (DEVADDR, mba2_dib.ba, 32), REG_HRO },
    { ORDATA (DEVVEC, mba2_dib.vec, 16), REG_HRO },
    { NULL }
    };

MTAB mba2_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0040, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR",
      &set_vec, &show_vec, NULL },
    { 0 }
    };

DEVICE mba_dev[] = {
    {
    "RHA", &mba0_unit, mba0_reg, mba0_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &mba_reset,
    NULL, NULL, NULL,
    &mba0_dib, DEV_DEBUG | DEV_DISABLE | DEV_UBUS | DEV_QBUS, 0,
    NULL, NULL, NULL, &rh_help, NULL, NULL,
    &rh_description 
    },
    {
    "RHB", &mba1_unit, mba1_reg, mba1_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &mba_reset,
    NULL, NULL, NULL,
    &mba1_dib, DEV_DEBUG | DEV_DISABLE | DEV_UBUS | DEV_QBUS, 0,
    NULL, NULL, NULL, &rh_help, NULL, NULL,
    &rh_description 
    },
    {
    "RHC", &mba2_unit, mba2_reg, mba2_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &mba_reset,
    NULL, NULL, NULL,
    &mba2_dib, DEV_DEBUG | DEV_DISABLE | DEV_UBUS | DEV_QBUS, 0,
    NULL, NULL, NULL, &rh_help, NULL, NULL,
    &rh_description 
    }
    };

/* Read Massbus adapter register */

t_stat mba_rd (int32 *val, int32 pa, int32 mode)
{
int32 ofs, dat, mb, drv;
t_stat r;

mb = mba_map_pa (pa, &ofs);                             /* get mb number */
if ((mb < 0) || (ofs < 0))                              /* valid? */
    return SCPE_NXM;
drv = GET_UNIT (massbus[mb].cs2);                       /* get drive */
mba_upd_cs1 (0, 0, mb);                                 /* update CS1 */

if (ofs & EXT) {                                        /* external? */
    if (!mbregR[mb])                                    /* device there? */
        return SCPE_NXM;
    r = mbregR[mb] (val, ofs & ~EXT, drv);              /* call device */
    if (r == MBE_NXD)                                   /* nx drive? */
        mba_set_cs2 (CS2_NED, mb);
    else if (r == MBE_NXR)                              /* nx reg? */
        return SCPE_NXM;
    return SCPE_OK; 
    }

switch (ofs) {                                          /* case on reg */

    case CS1_OF:                                        /* CS1 */
        if (!mbregR[mb])                                /* nx device? */
            return SCPE_NXM;
        r = mbregR[mb] (&dat, ofs, drv);                /* get dev cs1 */
        if (r == MBE_NXD)                               /* nx drive? */
            mba_set_cs2 (CS2_NED, mb);
        *val = massbus[mb].cs1 | dat;
        break;

    case WC_OF:                                         /* WC */
        *val = massbus[mb].wc;
        break;

    case BA_OF:                                         /* BA */
        *val = massbus[mb].ba & ~BA_MBZ;
        break;

    case CS2_OF:                                        /* CS2 */
        *val = massbus[mb].cs2 = (massbus[mb].cs2 & ~CS2_MBZ) | CS2_IR | CS2_OR;
        break;

    case DB_OF:                                         /* DB */
        *val = massbus[mb].db;
        break;

    case BAE_OF:                                        /* BAE */
        *val = massbus[mb].bae = massbus[mb].bae & ~AE_MBZ;
        break;

    case CS3_OF:                                        /* CS3 */
        *val = massbus[mb].cs3 = (massbus[mb].cs3 & ~(CS1_IE | CS3_MBZ)) |
            (massbus[mb].cs1 & CS1_IE);
        break;

    default:                                            /* huh? */
        return SCPE_NXM;
        }

return SCPE_OK;
}

t_stat mba_wr (int32 val, int32 pa, int32 access)
{
int32 ofs, cs1f, drv, mb;
t_stat r;
t_bool cs1dt;

mb = mba_map_pa (pa, &ofs);                             /* get mb number */
if ((mb < 0) || (ofs < 0))                              /* valid? */
    return SCPE_NXM;
drv = GET_UNIT (massbus[mb].cs2);                       /* get drive */

if (ofs & EXT) {                                        /* external? */
    if (!mbregW[mb])                                    /* device there? */
        return SCPE_NXM;
    if ((access == WRITEB) && (pa & 1))                 /* byte writes */
        val = val << 8;                                 /* don't work */
    r = mbregW[mb] (val, ofs & ~EXT, drv);              /* write dev reg */
    if (r == MBE_NXD)                                   /* nx drive? */
        mba_set_cs2 (CS2_NED, mb);
    else if (r == MBE_NXR)                              /* nx reg? */
        return SCPE_NXM;
    mba_upd_cs1 (0, 0, mb);                             /* update status */
    return SCPE_OK;
    } 

cs1f = 0;                                               /* no int on cs1 upd */
switch (ofs) {                                          /* case on reg */

    case CS1_OF:                                        /* CS1 */
        if (!mbregW[mb])                                /* device exist? */
            return SCPE_NXM;
        if ((access == WRITEB) && (pa & 1))
            val = val << 8;
        if (val & CS1_TRE) {                            /* error clear? */
            massbus[mb].cs1 &= ~CS1_TRE;                /* clr CS1<TRE> */
            massbus[mb].cs2 &= ~CS2_ERR;                /* clr CS2<15:8> */
            massbus[mb].cs3 &= ~CS3_ERR;                /* clr CS3<15:11> */
            }
        if ((access == WRITE) || (pa & 1)) {            /* hi byte write? */
            if (massbus[mb].cs1 & CS1_DONE)             /* done set? */
                massbus[mb].cs1 = (massbus[mb].cs1 & ~CS1_UAE) | (val & CS1_UAE);
            }
        if ((access == WRITE) || !(pa & 1)) {           /* lo byte write? */
            if ((val & CS1_DONE) && (val & CS1_IE))     /* to DONE+IE? */
                massbus[mb].iff = 1;                    /* set CSTB INTR */
            massbus[mb].cs1 = (massbus[mb].cs1 & ~CS1_IE) | (val & CS1_IE);
            cs1dt = (val & CS1_GO) && (GET_FNC (val) >= FNC_XFER);
            if (cs1dt && ((massbus[mb].cs1 & CS1_DONE) == 0))  /* dt, done clr? */
                mba_set_cs2 (CS2_PGE, mb);              /* prgm error */
            else {
                r = mbregW[mb] (val & 077, ofs, drv);   /* write dev CS1 */
                if (r == MBE_NXD)                       /* nx drive? */
                    mba_set_cs2 (CS2_NED, mb);
                else if (r == MBE_NXR)                  /* nx reg? */
                    return SCPE_NXM;
                else if (cs1dt && (r == SCPE_OK)) {     /* xfer, no err? */
                    massbus[mb].cs1 &= ~(CS1_TRE | CS1_MCPE | CS1_DONE);
                    massbus[mb].cs2 &= ~CS2_ERR;        /* clear errors */
                    massbus[mb].cs3 &= ~(CS3_ERR | CS3_DBL);
                    if (DEBUG_PRS (mba_dev[mb]))
                        fprintf (sim_deb, ">>RH%d STRT: cs1=%o, cs2=%o,ba=%o, wc=%o\n",
                            mb, massbus[mb].cs1, massbus[mb].cs2, massbus[mb].ba, massbus[mb].wc);
                    }
                }
            }
        massbus[mb].cs3 = (massbus[mb].cs3 & ~CS1_IE) | /* update CS3 */
            (massbus[mb].cs1 & CS1_IE);
        massbus[mb].bae = (massbus[mb].bae & ~CS1_M_UAE) | /* update BAE */
            ((massbus[mb].cs1 >> CS1_V_UAE) & CS1_M_UAE);
        break;  

    case WC_OF:                                         /* WC */
        if (access == WRITEB)
            val = (pa & 1)?
            (massbus[mb].wc & 0377) | (val << 8):
            (massbus[mb].wc & ~0377) | val;
        massbus[mb].wc = val;
        break;

    case BA_OF:                                         /* BA */
        if (access == WRITEB)
            val = (pa & 1)?
            (massbus[mb].ba & 0377) | (val << 8):
            (massbus[mb].ba & ~0377) | val;
        massbus[mb].ba = val & ~BA_MBZ;
        break;

    case CS2_OF:                                        /* CS2 */
        if ((access == WRITEB) && (pa & 1))
            val = val << 8;
        if (val & CS2_CLR)                              /* init? */
            mba_reset (&mba_dev[mb]);
        else {
            if ((val & ~massbus[mb].cs2) & (CS2_PE | CS2_MXF))
                cs1f = CS1_SC;                          /* diagn intr */
            if (access == WRITEB)                       /* merge val */
                val = (massbus[mb].cs2 & ((pa & 1)? 0377: 0177400)) | val;
            massbus[mb].cs2 = (massbus[mb].cs2 & ~CS2_RW) |
                (val & CS2_RW) | CS2_IR | CS2_OR;
            }
        break;

    case DB_OF:                                         /* DB */
        if (access == WRITEB)
            val = (pa & 1)?
            (massbus[mb].db & 0377) | (val << 8):
            (massbus[mb].db & ~0377) | val;
        massbus[mb].db = val;
        break;

    case BAE_OF:                                       /* BAE */
        if ((access == WRITEB) && (pa & 1))
            break;
        massbus[mb].bae = val & ~AE_MBZ;
        massbus[mb].cs1 = (massbus[mb].cs1 & ~CS1_UAE) | /* update CS1 */
            ((massbus[mb].bae << CS1_V_UAE) & CS1_UAE);
        break;

    case CS3_OF:                                        /* CS3 */
        if ((access == WRITEB) && (pa & 1))
            break;
        massbus[mb].cs3 = (massbus[mb].cs3 & ~CS3_RW) | (val & CS3_RW);
        massbus[mb].cs1 = (massbus[mb].cs1 & ~CS1_IE) | /* update CS1 */
            (massbus[mb].cs3 & CS1_IE);
        break;

    default:
        return SCPE_NXM;
        }

mba_upd_cs1 (cs1f, 0, mb);                              /* update status */
return SCPE_OK;
}

/* Massbus I/O routines

   mb_rdbufW    -       fetch word buffer from memory
   mb_wrbufW    -       store word buffer into memory
   mb_chbufW    -       compare word buffer with memory

   Returns number of bytes successfully transferred/checked
*/

int32 mba_rdbufW (uint32 mb, int32 bc, uint16 *buf)
{
int32 i, j, ba, mbc, pbc;
uint32 pa;

bc = bc & ~1;                                           /* bc even */
if (mb >= MBA_NUM)                                      /* valid MBA? */
    return 0;
ba = (massbus[mb].bae << 16) | massbus[mb].ba;          /* get busaddr */
mbc = (0200000 - massbus[mb].wc) << 1;                  /* MB byte count */
if (bc > mbc)                                           /* use smaller */
    bc = mbc;
for (i = 0; i < bc; i = i + pbc) {                      /* loop by pages */
    if (RH11 && cpu_bme)                                /* map addr */
        pa = Map_Addr (ba);
    else pa = ba;
    if (!ADDR_IS_MEM (pa)) {                            /* NXM? */
        mba_set_cs2 (CS2_NEM, mb);                      /* set error */
        break;
        }
    pbc = UBM_PAGSIZE - UBM_GETOFF (pa);                /* left in page */
    if (pbc > (bc - i))                                 /* limit to rem xfr */
        pbc = bc - i;
    for (j = 0; j < pbc; j = j + 2) {                   /* loop by words */
        *buf++ = M[pa >> 1];                            /* fetch word */
        if (!(massbus[mb].cs2 & CS2_UAI)) {             /* if not inhb */
            ba = ba + 2;                                /* incr ba, pa */
            pa = pa + 2;
            }
        }
    }
massbus[mb].wc = (massbus[mb].wc + (bc >> 1)) & DMASK;   /* update wc */
massbus[mb].ba = ba & DMASK;                             /* update ba */
massbus[mb].bae = (ba >> 16) & ~AE_MBZ;                  /* upper 6b */
massbus[mb].cs1 = (massbus[mb].cs1 & ~ CS1_UAE) |         /* update CS1 */
    ((massbus[mb].bae << CS1_V_UAE) & CS1_UAE);
return i;
}

int32 mba_wrbufW (uint32 mb, int32 bc, uint16 *buf)
{
int32 i, j, ba, mbc, pbc;
uint32 pa;

bc = bc & ~1;                                           /* bc even */
if (mb >= MBA_NUM)                                      /* valid MBA? */
    return 0;
ba = (massbus[mb].bae << 16) | massbus[mb].ba;          /* get busaddr */
mbc = (0200000 - massbus[mb].wc) << 1;                  /* MB byte count */
if (bc > mbc)                                           /* use smaller */
    bc = mbc;
for (i = 0; i < bc; i = i + pbc) {                      /* loop by pages */
    if (RH11 && cpu_bme)                                /* map addr */
        pa = Map_Addr (ba);
    else pa = ba;
    if (!ADDR_IS_MEM (pa)) {                            /* NXM? */
        mba_set_cs2 (CS2_NEM, mb);                      /* set error */
        break;
        }
    pbc = UBM_PAGSIZE - UBM_GETOFF (pa);                /* left in page */
    if (pbc > (bc - i))                                 /* limit to rem xfr */
        pbc = bc - i;
    for (j = 0; j < pbc; j = j + 2) {                   /* loop by words */
        M[pa >> 1] = *buf++;                            /* put word */
        if (!(massbus[mb].cs2 & CS2_UAI)) {             /* if not inhb */
            ba = ba + 2;                                /* incr ba, pa */
            pa = pa + 2;
            }
        }
    }
massbus[mb].wc = (massbus[mb].wc + (bc >> 1)) & DMASK;  /* update wc */
massbus[mb].ba = ba & DMASK;                            /* update ba */
massbus[mb].bae = (ba >> 16) & ~AE_MBZ;                 /* upper 6b */
massbus[mb].cs1 = (massbus[mb].cs1 & ~ CS1_UAE) |       /* update CS1 */
    ((massbus[mb].bae << CS1_V_UAE) & CS1_UAE);
return i;
}

int32 mba_chbufW (uint32 mb, int32 bc, uint16 *buf)
{
int32 i, j, ba, mbc, pbc;
uint32 pa;

bc = bc & ~1;                                           /* bc even */
if (mb >= MBA_NUM)                                      /* valid MBA? */
    return 0;
ba = (massbus[mb].bae << 16) | massbus[mb].ba;          /* get busaddr */
mbc = (0200000 - massbus[mb].wc) << 1;                  /* MB byte count */
if (bc > mbc)                                           /* use smaller */
    bc = mbc;
for (i = 0; i < bc; i = i + pbc) {                      /* loop by pages */
    if (RH11 && cpu_bme) pa = Map_Addr (ba);            /* map addr */
    else pa = ba;
    if (!ADDR_IS_MEM (pa)) {                            /* NXM? */
        mba_set_cs2 (CS2_NEM, mb);                      /* set error */
        break;
        }
    pbc = UBM_PAGSIZE - UBM_GETOFF (pa);                /* left in page */
    if (pbc > (bc - i))                                 /* limit to rem xfr */
        pbc = bc - i;
    for (j = 0; j < pbc; j = j + 2) {                   /* loop by words */
        massbus[mb].db = *buf++;                        /* get dev word */
        if (M[pa >> 1] != massbus[mb].db) {             /* miscompare? */
            mba_set_cs2 (CS2_WCE, mb);                  /* set error */
            massbus[mb].cs3 = massbus[mb].cs3 |         /* set even/odd */
                ((pa & 1)? CS3_WCO: CS3_WCE);
            break;
            }
        if (!(massbus[mb].cs2 & CS2_UAI)) {             /* if not inhb */
            ba = ba + 2;                                /* incr ba, pa */
            pa = pa + 2;
            }
        }
    }
massbus[mb].wc = (massbus[mb].wc + (bc >> 1)) & DMASK;  /* update wc */
massbus[mb].ba = ba & DMASK;                            /* update ba */
massbus[mb].bae = (ba >> 16) & ~AE_MBZ;                 /* upper 6b */
massbus[mb].cs1 = (massbus[mb].cs1 & ~ CS1_UAE) |       /* update CS1 */
    ((massbus[mb].bae << CS1_V_UAE) & CS1_UAE);
return i;
}

/* Device access, status, and interrupt routines */

void mba_set_don (uint32 mb)
{
mba_upd_cs1 (CS1_DONE, 0, mb);
if (DEBUG_PRS (mba_dev[mb]))
    fprintf (sim_deb, ">>RH%d DONE: cs1=%o, cs2=%o,ba=%o, wc=%o\n",
        mb, massbus[mb].cs1, massbus[mb].cs2, massbus[mb].ba, massbus[mb].wc);
return;
}

void mba_upd_ata (uint32 mb, uint32 val)
{
if (val)
    mba_upd_cs1 (CS1_SC, 0, mb);
else mba_upd_cs1 (0, CS1_SC, mb);
return;
}

void mba_set_exc (uint32 mb)
{
mba_upd_cs1 (CS1_TRE | CS1_DONE, 0, mb);
return;
}

int32 mba_get_bc (uint32 mb)
{
if (mb >= MBA_NUM)
    return 0;
return ((0200000 - massbus[mb].wc) << 1);
}

int32 mba_get_csr (uint32 mb)
{
DIB *dibp;

if (mb >= MBA_NUM)
    return 0;
dibp = (DIB *) mba_dev[mb].ctxt;
return dibp->ba;
}

void mba_set_int (uint32 mb)
{
DIB *dibp;

if (mb >= MBA_NUM)
    return;
dibp = (DIB *) mba_dev[mb].ctxt;
int_req[dibp->vloc >> 5] |= (1 << (dibp->vloc & 037));
return;
}

void mba_clr_int (uint32 mb)
{
DIB *dibp;

if (mb >= MBA_NUM)
    return;
dibp = (DIB *) mba_dev[mb].ctxt;
int_req[dibp->vloc >> 5] &= ~(1 << (dibp->vloc & 037));
return;
}

void mba_upd_cs1 (uint32 set, uint32 clr, uint32 mb)
{
if (mb >= MBA_NUM)
    return;
if ((set & ~massbus[mb].cs1) & CS1_DONE)                    /* DONE 0 to 1? */
    massbus[mb].iff = (massbus[mb].cs1 & CS1_IE)? 1: 0;     /* CSTB INTR <- IE */
massbus[mb].cs1 = (massbus[mb].cs1 & ~(clr | CS1_MCPE | CS1_MBZ | CS1_DRV)) | set;
if (massbus[mb].cs2 & CS2_ERR)
    massbus[mb].cs1 = massbus[mb].cs1 | CS1_TRE | CS1_SC;
else if (massbus[mb].cs1 & CS1_TRE)
    massbus[mb].cs1 = massbus[mb].cs1 | CS1_SC;
if (massbus[mb].iff ||
    ((massbus[mb].cs1 & CS1_SC) &&
     (massbus[mb].cs1 & CS1_DONE) &&
     (massbus[mb].cs1 & CS1_IE)))
    mba_set_int (mb);
else mba_clr_int (mb);
return;
}

void mba_set_cs2 (uint32 flag, uint32 mb)
{
if (mb >= MBA_NUM)
    return;
massbus[mb].cs2 = massbus[mb].cs2 | flag;
mba_upd_cs1 (0, 0, mb);
return;
}

/* Interrupt acknowledge */

int32 mba0_inta (void)
{
massbus[0].cs1 &= ~CS1_IE;                              /* clear int enable */
massbus[0].cs3 &= ~CS1_IE;                              /* in both registers */
massbus[0].iff = 0;                                     /* clear CSTB INTR */
return mba0_dib.vec;                                    /* acknowledge */
}

int32 mba1_inta (void)
{
massbus[1].cs1 &= ~CS1_IE;                              /* clear int enable */
massbus[1].cs3 &= ~CS1_IE;                              /* in both registers */
massbus[1].iff = 0;                                     /* clear CSTB INTR */
return mba1_dib.vec;                                    /* acknowledge */
}

int32 mba2_inta (void)
{
massbus[2].cs1 &= ~CS1_IE;                              /* clear int enable */
massbus[2].cs3 &= ~CS1_IE;                              /* in both registers */
massbus[2].iff = 0;                                     /* clear CSTB INTR */
return mba2_dib.vec;                                    /* acknowledge */
}

/* Map physical address to Massbus number, offset */

int32 mba_map_pa (int32 pa, int32 *ofs)
{
int32 i, uo, ba, lnt;
DIB *dibp;

for (i = 0; i < MBA_NUM; i++) {                         /* loop thru ctrls */
    dibp = (DIB *) mba_dev[i].ctxt;                     /* get DIB */
    ba = dibp->ba;
    lnt = dibp->lnt;
    if ((pa >= ba) &&                                   /* in range? */
        (pa < (ba + lnt))) {
        if (pa < (ba + (lnt - 4))) {                    /* not last two? */
            uo = ((pa - ba) & MBA_OFSMASK) >> 1;        /* get Unibus offset */
            *ofs = mba_mapofs[uo];                      /* map thru PROM */
            return i;                                   /* return ctrl idx */
            }
        else if (RH11)                                  /* RH11? done */
            return -1;
        else {                                          /* RH70 */
            uo = (pa - (ba + (lnt - 4))) >> 1;          /* offset relative */
            *ofs = BAE_OF + uo;                         /* to BAE */
            return i;
            }
        }
    }
return -1;
}

/* Reset Massbus adapter */

t_stat mba_reset (DEVICE *dptr)
{
uint32 mb;

mb = dptr - mba_dev;
if (mb >= MBA_NUM)
    return SCPE_NOFNC;
massbus[mb].cs1 = CS1_DONE;
massbus[mb].wc = 0;
massbus[mb].ba = 0;
massbus[mb].cs2 = 0;
massbus[mb].db = 0;
massbus[mb].bae= 0;
massbus[mb].cs3 = 0;
massbus[mb].iff = 0;
mba_clr_int (mb);
if (mbabort[mb])
    mbabort[mb] ();
return auto_config (0, 0);
}

/* Enable/disable Massbus adapter */

void mba_set_enbdis (uint32 mb, t_bool dis)
{
t_bool orig;
if (mb >= MBA_NUM)                                      /* valid MBA? */
    return;
orig = mba_dev[mb].flags & DEV_DIS;
if (dis)
    mba_dev[mb].flags |= DEV_DIS;
else mba_dev[mb].flags &= ~DEV_DIS;
if (orig ^ dis)
    mba_reset (&mba_dev[mb]);                           /* reset on change */
return;
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
        sim_printf ("Massbus %s assignment conflict at %d\n",
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

t_stat rh_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
const char *const text =
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
" RH70/RH11 Massbus adapters (RHA, RHB, RHC)\n"
"\n"
" The RH70/RH11 Massbus adapters interface Massbus peripherals to the memory\n"
" bus or Unibus of the CPU.  The simulator provides three Massbus adapters.\n"
" The first, RHA, is configured for the RP family of disk drives.  The\n"
" second, RHB, is configured for the TU family of tape controllers.  The\n"
" third, RHC, is configured for the RS family of fixed head disks.  By\n"
" default, RHA is enabled, and RHB and RHC are disabled.  In a Unibus system,\n"
" the RH adapters implement 22b addressing for the 11/70 and 18b addressing\n"
" for all other models.  In a Qbus system, the RH adapters always implement\n"
" 22b addressing.\n"
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
"\n";
fprintf (st, "%s", text);
fprint_show_help (st, dptr);
fprintf (st, "\nEach RH adapter implements these registers:\n");
fprint_reg_help (st, dptr);
return SCPE_OK;
}

char *rh_description (DEVICE *dptr)
{
if (dptr == &mba_dev[0])
    return "RH70/RH11 Massbus adapter (for RP)";
else
    if (dptr == &mba_dev[1])
        return "RH70/RH11 Massbus adapter (for TU)";
    else
        return "RH70/RH11 Massbus adapter (for RS)";
}
