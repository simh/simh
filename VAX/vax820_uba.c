/* vax820_uba.c: VAXBI Unibus adapter (DWBUA)

   Copyright (c) 2019, Matt Burke
   This module incorporates code from SimH, Copyright (c) 2004-2008, Robert M Supnik

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

   uba                  DWBUA Unibus adapter
*/

#include "vax_defs.h"

/* Unibus adapter */

#define UBA_NDPATH      6                               /* number of data paths */
#define UBA_NMAPR       512                             /* number of map reg */
#define UBA_NMAPU       496                             /* number of usable map reg */

/* BI general purpose register 0 */
#define BIGPR0_IEN      0x00FF0000                      /* internal error number */
#define BIGPR0_UPU      0x00000001                      /* unibus power up */

/* Control/Status register */

#define UBACSR_OF       0x1C8
#define UBACSR_ERR      0x80000000                      /* error */
#define UBACSR_BIF      0x10000000                      /* VAXBI failure */
#define UBACSR_TO       0x08000000                      /* unibus ssyn timeout */
#define UBACSR_UIE      0x04000000                      /* unibus interlock error */
#define UBACSR_IMR      0x02000000                      /* invalid map reg */
#define UBACSR_BDP      0x01000000                      /* bad buffered datapath */
#define UBACSR_EIE      0x00100000                      /* error interrupt en */
#define UBACSR_UPI      0x00020000                      /* unibus power init */
#define UBACSR_DMP      0x00010000                      /* register dump */
#define UBACSR_MBO      0x00008000                      /* must be one */
#define UBACSR_IEN      0x000000FF                      /* internal error - NI */
#define UBACSR_WR       (UBACSR_EIE)
#define UBACSR_W1C      (UBACSR_BIF | UBACSR_TO | UBACSR_UIE | \
                         UBACSR_IMR | UBACSR_BDP)
#define UBACSR_ERRS     (UBACSR_BIF | UBACSR_TO | UBACSR_UIE | \
                         UBACSR_IMR | UBACSR_BDP)

/* Vector offset register */

#define UBAVO_OF        0x1C9
#define UBAVO_VEC       0x00003E00

/* Failing Unibus address - read only */

#define UBAFUBAR_OF     0x1CA
#define UBAFUBAR_RD     0xFFFF

/* VAXBI failed address - read only */

#define UBABIFA_OF      0x1CB

/* Microdiagnostic registers */

#define UBADR_OF        0x1CC

/* Data path registers */

#define UBADPR_OF       0x1D4
#define UBADPR_V_SEL    21                              /* datapath select */
#define UBADPR_M_SEL    0x7
#define UBADPR_PURGE    0x00000001                      /* purge datapath */
#define UBADPR_RD       (UBADPR_M_SEL << UBADPR_V_SEL)

/* Buffered data path space */

#define UBABDPS_OF      0x1E4

/* Map registers */

#define UBAMAP_OF       0x200
#define UBAMAP_VLD      0x80000000                      /* valid */
#define UBAMAP_IOAD     0x40000000                      /* i/o address */
#define UBAMAP_LWAE     0x04000000                      /* LW access enb - ni */
#define UBAMAP_ODD      0x02000000                      /* odd byte */
#define UBAMAP_V_DP     21                              /* data path */
#define UBAMAP_M_DP     0x7
#define UBAMAP_DP       (UBAMAP_M_DP << UBAMAP_V_DP)
#define UBAMAP_GETDP(x) (((x) >> UBAMAP_V_DP) & UBAMAP_M_DP)
#define UBAMAP_PAG      0x001FFFFF
#define UBAMAP_RD       (0xC6000000 | UBAMAP_DP | UBAMAP_PAG)
#define UBAMAP_WR       (UBAMAP_RD)

/* Debug switches */

#define UBA_DEB_RRD     0x01                            /* reg reads */
#define UBA_DEB_RWR     0x02                            /* reg writes */
#define UBA_DEB_MRD     0x04                            /* map reads */
#define UBA_DEB_MWR     0x08                            /* map writes */
#define UBA_DEB_XFR     0x10                            /* transfers */
#define UBA_DEB_ERR     0x20                            /* errors */

int32 int_req[IPL_HLVL] = { 0 };                        /* intr, IPL 14-17 */
BIIC uba_biic;                                          /* BIIC standard registers */
uint32 uba_csr = 0;                                     /* control/status reg */
uint32 uba_vo = 0;                                      /* vector offset */
uint32 uba_int = 0;                                     /* UBA interrupt */
uint32 uba_fubar = 0;                                   /* failing Unibus addr */
uint32 uba_bifa = 0;                                    /* BI failing addr */
uint32 uba_dpr[UBA_NDPATH] = { 0 };                     /* number data paths */
uint32 uba_map[UBA_NMAPR] = { 0 };                      /* map registers */
uint32 uba_aiip = 0;                                    /* adapter init in prog */
uint32 uba_uiip = 0;                                    /* Unibus init in prog */
uint32 uba_aitime = 250;                                /* adapter init time */
uint32 uba_uitime = 12250;                              /* Unibus init time */
int32 autcon_enb = 1;                                   /* autoconfig enable */

extern uint32 nexus_req[NEXUS_HLVL];

t_stat uba_svc (UNIT *uptr);
t_stat uba_reset (DEVICE *dptr);
t_stat uba_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *uba_description (DEVICE *dptr);
t_stat uba_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat uba_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat uba_rdreg (int32 *val, int32 pa, int32 mode);
t_stat uba_wrreg (int32 val, int32 pa, int32 lnt);
int32 uba_get_ubvector (int32 lvl);
void uba_ub_nxm (int32 ua);
void uba_bi_nxm (int32 ba);
void uba_inv_map (int32 ublk);
void uba_eval_int (void);
void uba_adap_set_int ();
void uba_adap_clr_int ();
void uba_ubpdn (int32 time);
t_bool uba_map_addr (uint32 ua, uint32 *ma);
t_stat set_autocon (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat show_autocon (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat show_iospace (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat uba_show_virt (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

extern int32 eval_int (void);
extern t_stat build_dib_tab (void);

/* Unibus IO page dispatches */

t_stat (*iodispR[IOPAGESIZE >> 1])(int32 *dat, int32 ad, int32 md);
t_stat (*iodispW[IOPAGESIZE >> 1])(int32 dat, int32 ad, int32 md);
DIB *iodibp[IOPAGESIZE >> 1];

/* Unibus interrupt request to interrupt action map */

int32 (*int_ack[IPL_HLVL][32])(void);                   /* int ack routines */

/* Unibus interrupt request to vector map */

int32 int_vec[IPL_HLVL][32];                            /* int req to vector */

/* Unibus adapter data structures

   uba_dev      UBA device descriptor
   uba_unit     UBA units
   uba_reg      UBA register list
*/

DIB uba_dib = { TR_UBA, 0, &uba_rdreg, &uba_wrreg, 0, 0 };

UNIT uba_unit = { UDATA (&uba_svc, 0, 0) };

REG uba_reg[] = {
    { HRDATA (IPL14, int_req[0], 32), REG_RO },
    { HRDATA (IPL15, int_req[1], 32), REG_RO },
    { HRDATA (IPL16, int_req[2], 32), REG_RO },
    { HRDATA (IPL17, int_req[3], 32), REG_RO },
    { HRDATA (CSR, uba_csr, 32) },
    { HRDATA (VO, uba_vo, 32) },
    { FLDATA (INT, uba_int, 0) },
    { FLDATA (NEXINT, nexus_req[IPL_UBA], TR_UBA) },
    { HRDATA (FUBAR, uba_fubar, 32) },
    { HRDATA (BIFA, uba_bifa, 32) },
    { HRDATA (BICSR, uba_biic.csr, 32) },
    { HRDATA (BIBER, uba_biic.ber, 32) },
    { HRDATA (BIECR, uba_biic.eicr, 32) },
    { HRDATA (BIDEST, uba_biic.idest, 32) },
    { HRDATA (BISRC, uba_biic.isrc, 32) },
    { HRDATA (BIMSK, uba_biic.imsk, 32) },
    { HRDATA (BIUIIC, uba_biic.uiic, 32) },
    { BRDATA (DPR, uba_dpr, 16, 32, 16) },
    { BRDATA (MAP, uba_map, 16, 32, UBA_NMAPR) },
    { FLDATA (AUTOCON, autcon_enb, 0), REG_HRO },
    { NULL }
    };

MTAB uba_mod[] = {
    { MTAB_XTD|MTAB_VDV, TR_UBA, "NEXUS", NULL,
      NULL, &show_nexus },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "IOSPACE", NULL,
      NULL, &show_iospace },
    { MTAB_XTD|MTAB_VDV, 1, "AUTOCONFIG", "AUTOCONFIG",
      &set_autocon, &show_autocon },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOAUTOCONFIG",
      &set_autocon, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "VIRTUAL", NULL,
      NULL, &uba_show_virt },
    { 0 }
    };

DEBTAB uba_deb[] = {
    { "REGREAD", UBA_DEB_RRD },
    { "REGWRITE", UBA_DEB_RWR },
    { "MAPREAD", UBA_DEB_MRD },
    { "MAPWRITE", UBA_DEB_MWR },
    { "XFER", UBA_DEB_XFR },
    { "ERROR", UBA_DEB_ERR },
    { NULL, 0 }
    };

DEVICE uba_dev = {
    "UBA", &uba_unit, uba_reg, uba_mod,
    1, 16, UBADDRWIDTH, 2, 16, 16,
    &uba_ex, &uba_dep, &uba_reset,
    NULL, NULL, NULL,
    &uba_dib, DEV_NEXUS | DEV_DEBUG, 0,
    uba_deb, NULL, NULL, &uba_help, NULL, NULL,
    &uba_description
    };

/* Read Unibus adapter register - aligned lw only */

t_stat uba_rdreg (int32 *val, int32 pa, int32 lnt)
{
int32 idx, ofs;

ofs = NEXUS_GETOFS (pa);                                /* get offset */
if (uba_aiip && (ofs >= UBACSR_OF)) {                   /* init in prog? */
    *val = 0;
    return SCPE_OK;                                     /* only BIIC */
    }
if ((ofs >= UBABDPS_OF) && (ofs < UBABDPS_OF + 0x10)) {
    *val = 0;
    return SCPE_OK;
    }
if (ofs >= UBAMAP_OF) {                                 /* map? */
    idx = ofs - UBAMAP_OF;
    if (idx >= UBA_NMAPR)                               /* valid? */
        return SCPE_NXM;
    *val = uba_map[idx] & UBAMAP_RD;
    sim_debug (UBA_DEB_MRD, &uba_dev, "map %d read, value = %X, PC = %X\n", idx, *val, fault_PC);
    return SCPE_OK;
    }

switch (ofs) {                                          /* case on offset */

    case BI_DTYPE:
        *val = DTYPE_DWBUA;
        break;

    case BI_CSR:
        *val = uba_biic.csr & BICSR_RD;
        break;

    case BI_BER:
        *val = uba_biic.ber & BIBER_RD;
        break;

    case BI_EICR:
        *val = uba_biic.eicr & BIECR_RD;
        break;

    case BI_IDEST:
        *val = uba_biic.idest & BIID_RD;
        break;

    case BI_IMSK:
    case BI_FIDEST:
    case BI_ISRC:
        *val = 0;
        break;

    case BI_SA:
        *val = uba_biic.sa;
        break;

    case BI_EA:
        *val = uba_biic.ea;
        break;

    case BI_BCIC:
        *val = uba_biic.bcic & BIBCI_RD;
        break;

    case BI_UIIC:
        *val = uba_biic.uiic;
        break;

    case BI_GPR0:
        *val = uba_biic.gpr0;
        break;

    case BI_GPR1:
    case BI_GPR2:
    case BI_GPR3:
        *val = 0;
        break;

    case UBACSR_OF:                                     /* CSR */
        *val = uba_csr | UBACSR_MBO;
        if (uba_csr & UBACSR_ERRS)                      /* any errors? */
            *val |= UBACSR_ERR;                         /* yes, set logical OR bit */
        break;

    case UBAVO_OF:                                      /* VO */
        *val = uba_vo & UBAVO_VEC; // should be UBAVO_RD?
        break;

    case UBAFUBAR_OF:                                   /* FUBAR */
        *val = uba_fubar & UBAFUBAR_RD;
        break;

    case UBABIFA_OF:                                    /* BIFA */
        *val = uba_bifa;
        break;

    case UBADR_OF + 0:                                  /* DR */
    case UBADR_OF + 1:
    case UBADR_OF + 2:
    case UBADR_OF + 3:
    case UBADR_OF + 4:
        *val = 0;
        break;

    case UBADPR_OF + 0:                                 /* DPR */
    case UBADPR_OF + 1:
    case UBADPR_OF + 2:
    case UBADPR_OF + 3:
    case UBADPR_OF + 4:
    case UBADPR_OF + 5:
        idx = ofs - UBADPR_OF;
        *val = uba_dpr[idx] & UBADPR_RD;
        break;

    case UBADPR_OF + 6:
    case UBADPR_OF + 7:
        uba_csr |= UBACSR_BDP;
        break;

    default:
        return SCPE_NXM;
        }

sim_debug (UBA_DEB_RRD, &uba_dev, "reg %d read, value = %X, PC = %X\n", ofs, *val, fault_PC);
return SCPE_OK;
}

/* Write Unibus adapter register */

t_stat uba_wrreg (int32 val, int32 pa, int32 lnt)
{
int32 idx, ofs;

ofs = NEXUS_GETOFS (pa);                                /* get offset */
if (uba_aiip && (ofs >= UBACSR_OF)) {                   /* init in prog? */
    return SCPE_OK;                                     /* only BIIC */
    }
if (ofs >= UBAMAP_OF) {                                 /* map? */
    idx = ofs - UBAMAP_OF;
    if (idx >= UBA_NMAPR)                               /* valid? */
        return SCPE_NXM;
    uba_map[idx] = val & UBAMAP_WR;
    sim_debug (UBA_DEB_MWR, &uba_dev, "map %d write, value = %X, PC = %X\n", idx, val, fault_PC);
    return SCPE_OK;
    }

switch (ofs) {                                          /* case on offset */

    case BI_CSR:
        if (val & BICSR_RST)                            /* unibus power init */
            uba_reset (&uba_dev);                       /* reset adapter */
        uba_biic.csr = (uba_biic.csr & ~BICSR_RW) | (val & BICSR_RW);
        break;

    case BI_BER:
        uba_biic.ber = uba_biic.ber & ~(val & BIBER_W1C);
        break;

    case BI_EICR:
        uba_biic.eicr = (uba_biic.eicr & ~BIECR_RW) | (val & BIECR_RW);
        uba_biic.eicr = uba_biic.eicr & ~(val & BIECR_W1C);
        break;

    case BI_IDEST:
        uba_biic.idest = val & BIID_RW;
        break;

    case BI_BCIC:
        uba_biic.bcic = val & BIBCI_RW;
        break;

    case BI_UIIC:
        break;

    case BI_GPR0:
    case BI_GPR1:
    case BI_GPR2:
    case BI_GPR3:
        break;

    case UBACSR_OF:                                     /* CSR */
        if (val & UBACSR_UPI) {                         /* unibus power init */
            uba_aiip = 1;                               /* set init in prog */
            uba_ubpdn (uba_aitime);                     /* power fail UB */
            }
        uba_csr = (uba_csr & ~UBACSR_WR) | (val & UBACSR_WR);
        uba_csr = uba_csr & ~(val & UBACSR_W1C);
        break;

    case UBAVO_OF:                                      /* VO */
        uba_vo = val & UBAVO_VEC;
        break;

    case UBADPR_OF + 0:                                 /* DPR */
    case UBADPR_OF + 1:
    case UBADPR_OF + 2:
    case UBADPR_OF + 3:
    case UBADPR_OF + 4:
    case UBADPR_OF + 5:
        break;

    case UBADPR_OF + 6:
    case UBADPR_OF + 7:
        uba_csr |= UBACSR_BDP;
        break;

    default:
        return SCPE_NXM;
        }

sim_debug (UBA_DEB_RWR, &uba_dev, "reg %d write, value = %X, PC = %X\n", ofs, val, fault_PC);
return SCPE_OK;
}

/* Read and write Unibus I/O space */

int32 ReadUb (uint32 pa)
{
int32 idx, val;

if (ADDR_IS_IOP (pa)) {                                 /* iopage,!init */
    idx = (pa & IOPAGEMASK) >> 1;
    if (iodispR[idx]) {
        iodispR[idx] (&val, pa, READ);
        return val;
        }
    }
uba_biic.ber = uba_biic.ber | BIBER_RDS;
uba_ub_nxm (pa);                                        /* UB nxm */
return 0;
}

void WriteUb (uint32 pa, int32 val, int32 mode)
{
int32 idx;

if (ADDR_IS_IOP (pa)) {                                 /* iopage,!init */
    idx = (pa & IOPAGEMASK) >> 1;
    if (iodispW[idx]) {
        iodispW[idx] (val, pa, mode);
        return;
        }
    }
uba_ub_nxm (pa);                                        /* UB nxm */
return;
}

/* ReadIO - read from IO - UBA only responds to byte, aligned word

   Inputs:
        pa      =       physical address
        lnt     =       length (BWLQ)
   Output:
        longword of data
*/

int32 ReadIO (uint32 pa, int32 lnt)
{
uint32 iod;

if ((lnt == L_BYTE) ||                                  /* byte? */
    ((lnt == L_WORD) && ((pa & 1) == 0))) {             /* aligned word? */
    iod = ReadUb (pa);                                  /* DATI from Unibus */
    if (pa & 2)                                         /* position */
        iod = iod << 16;
    }
else {
    printf (">>UBA: invalid read mask, pa = %x, lnt = %d\n", pa, lnt);
    //TODO: Set error bit?
    iod = 0;
    }
SET_IRQL;
return iod;
}

/* WriteIO - write to IO - UBA only responds to byte, aligned word

   Inputs:
        pa      =       physical address
        val     =       data to write, right justified in 32b longword
        lnt     =       length (BWL)
   Outputs:
        none
*/

void WriteIO (uint32 pa, int32 val, int32 lnt)
{
if (lnt == L_BYTE)                                      /* byte? DATOB */
    WriteUb (pa, val, WRITEB);
else if (((lnt == L_WORD) || (lnt == L_LONG)) && ((pa & 1) == 0))/* aligned word? */
     WriteUb (pa, val, WRITE);                          /* DATO */
else {
    printf (">>UBA: invalid write mask, pa = %x, lnt = %d\n", pa, lnt);
    //TODO: Set error bit?
    }
SET_IRQL;                                               /* update ints */
return;
}

/* Update UBA nexus interrupts */

void uba_eval_int (void)
{
int32 i, lvl;

// TODO: Check BIIC register?
if (uba_int) {
    lvl = (uba_biic.eicr >> BIECR_V_LVL) & BIECR_M_LVL;
    for (i = 0; i < (IPL_HMAX - IPL_HMIN); i++) {
        if (lvl & (1u << i)) {
            nexus_req[i] |= (1 << TR_UBA);
            }
        }
    }
else {
    for (i = 0; i < (IPL_HMAX - IPL_HMIN); i++)             /* clear all UBA req */
        nexus_req[i] &= ~(1 << TR_UBA);
    for (i = 0; i < (IPL_HMAX - IPL_HMIN); i++) {
        if (int_req[i])
            nexus_req[i] |= (1 << TR_UBA);
        }
    }
//if (uba_int)                                            /* adapter int? */
//    SET_NEXUS_INT (UBA);
return;
}

/* Return vector for Unibus interrupt at relative IPL level [0-3] */

int32 uba_get_ubvector (int32 lvl)
{
int32 i, vec;

if ((uba_biic.eicr & (1u << (lvl + BIECR_V_LVL))) && uba_int) {         /* UBA err lvl, int? */
//if ((lvl == (IPL_UBA - IPL_HMIN)) && uba_int) {         /* UBA lvl, int? */
    vec = uba_biic.eicr & BIECR_VEC;
    uba_int = 0;                                        /* clear int */
    }
else {
    vec = uba_vo & UBAVO_VEC;
    for (i = 0; int_req[lvl] && (i < 32); i++) {
        if ((int_req[lvl] >> i) & 1) {
            int_req[lvl] = int_req[lvl] & ~(1u << i);
            if (int_ack[lvl][i])
                return (vec | int_ack[lvl][i]());
            return (vec | int_vec[lvl][i]);
            }
        }
    }
return vec;
}

/* Unibus I/O buffer routines

   Map_ReadB    -       fetch byte buffer from memory
   Map_ReadW    -       fetch word buffer from memory
   Map_WriteB   -       store byte buffer into memory
   Map_WriteW   -       store word buffer into memory
*/

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf)
{
int32 i, j, pbc;
uint32 ma, dat;

ba = ba & UBADDRMASK;                                   /* mask UB addr */
for (i = 0; i < bc; i = i + pbc) {                      /* loop by pages */
    if (!uba_map_addr (ba + i, &ma))                    /* page inv or NXM? */
        return (bc - i);
    pbc = VA_PAGSIZE - VA_GETOFF (ma);                  /* left in page */
    if (pbc > (bc - i))                                  /* limit to rem xfr */
        pbc = bc - i;
    sim_debug (UBA_DEB_XFR, &uba_dev, "8b read, ba = %X, ma = %X, bc = %X\n", ba, ma, pbc);
    if ((ma | pbc) & 3) {                               /* aligned LW? */
        for (j = 0; j < pbc; ma++, j++) {               /* no, do by bytes */
            *buf++ = ReadB (ma);
            }
        }
    else {                                              /* yes, do by LW */
        for (j = 0; j < pbc; ma = ma + 4, j = j + 4) {
            dat = ReadL (ma);                           /* get lw */
            *buf++ = dat & BMASK;                       /* low 8b */
            *buf++ = (dat >> 8) & BMASK;                /* next 8b */
            *buf++ = (dat >> 16) & BMASK;               /* next 8b */
            *buf++ = (dat >> 24) & BMASK;
            }
        }
    }
return 0;
}

int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf)
{
int32 i, j, pbc;
uint32 ma, dat;

ba = ba & UBADDRMASK;                                   /* mask UB addr */
bc = bc & ~01;
for (i = 0; i < bc; i = i + pbc) {                      /* loop by pages */
    if (!uba_map_addr (ba + i, &ma))                    /* page inv or NXM? */
        return (bc - i);
    pbc = VA_PAGSIZE - VA_GETOFF (ma);                  /* left in page */
    if (pbc > (bc - i))                                 /* limit to rem xfr */
        pbc = bc - i;
    sim_debug (UBA_DEB_XFR, &uba_dev, "16b read, ba = %X, ma = %X, bc = %X\n", ba, ma, pbc);
    if ((ma | pbc) & 1) {                               /* aligned word? */
        for (j = 0; j < pbc; ma++, j++) {               /* no, do by bytes */
            if ((i + j) & 1) {                          /* odd byte? */
                *buf = (*buf & BMASK) | (ReadB (ma) << 8);
                buf++;
                }
            else *buf = (*buf & ~BMASK) | ReadB (ma);
            }
        }
    else if ((ma | pbc) & 3) {                          /* aligned LW? */
        for (j = 0; j < pbc; ma = ma + 2, j = j + 2) {  /* no, words */
            *buf++ = ReadW (ma);                        /* get word */
            }
        }
    else {                                              /* yes, do by LW */
        for (j = 0; j < pbc; ma = ma + 4, j = j + 4) {
            dat = ReadL (ma);                           /* get lw */
            *buf++ = dat & WMASK;                       /* low 16b */
            *buf++ = (dat >> 16) & WMASK;               /* high 16b */
            }
        }
    }
return 0;
}

int32 Map_WriteB (uint32 ba, int32 bc, const uint8 *buf)
{
int32 i, j, pbc;
uint32 ma, dat;

ba = ba & UBADDRMASK;                                   /* mask UB addr */
for (i = 0; i < bc; i = i + pbc) {                      /* loop by pages */
    if (!uba_map_addr (ba + i, &ma))                    /* page inv or NXM? */
        return (bc - i);
    pbc = VA_PAGSIZE - VA_GETOFF (ma);                  /* left in page */
    if (pbc > (bc - i))                                 /* limit to rem xfr */
        pbc = bc - i;
    sim_debug (UBA_DEB_XFR, &uba_dev, "8b write, ba = %X, ma = %X, bc = %X\n", ba, ma, pbc);
    if ((ma | pbc) & 3) {                               /* aligned LW? */
        for (j = 0; j < pbc; ma++, j++) {               /* no, do by bytes */
            WriteB (ma, *buf);
            buf++;
            }
        }
    else {                                              /* yes, do by LW */
        for (j = 0; j < pbc; ma = ma + 4, j = j + 4) {
            dat = (uint32) *buf++;                      /* get low 8b */
            dat = dat | (((uint32) *buf++) << 8);       /* merge next 8b */
            dat = dat | (((uint32) *buf++) << 16);      /* merge next 8b */
            dat = dat | (((uint32) *buf++) << 24);      /* merge hi 8b */
            WriteL (ma, dat);                           /* store lw */
            }
        }
    }
return 0;
}

int32 Map_WriteW (uint32 ba, int32 bc, const uint16 *buf)
{
int32 i, j, pbc;
uint32 ma, dat;

ba = ba & UBADDRMASK;                                   /* mask UB addr */
bc = bc & ~01;
for (i = 0; i < bc; i = i + pbc) {                      /* loop by pages */
    if (!uba_map_addr (ba + i, &ma))                    /* page inv or NXM? */
        return (bc - i);
    pbc = VA_PAGSIZE - VA_GETOFF (ma);                  /* left in page */
    if (pbc > (bc - i))                                 /* limit to rem xfr */
        pbc = bc - i;
    sim_debug (UBA_DEB_XFR, &uba_dev, "16b write, ba = %X, ma = %X, bc = %X\n", ba, ma, pbc);
    if ((ma | pbc) & 1) {                               /* aligned word? */
        for (j = 0; j < pbc; ma++, j++) {               /* no, bytes */
            if ((i + j) & 1) {
                WriteB (ma, (*buf >> 8) & BMASK);
                buf++;
                }
            else WriteB (ma, *buf & BMASK);
            }
        }
    else if ((ma | pbc) & 3) {                          /* aligned LW? */
        for (j = 0; j < pbc; ma = ma + 2, j = j + 2) {  /* no, words */
            WriteW (ma, *buf);                          /* write word */
            buf++;
            }
        }
    else {                                              /* yes, do by LW */
        for (j = 0; j < pbc; ma = ma + 4, j = j + 4) {
            dat = (uint32) *buf++;                      /* get low 16b */
            dat = dat | (((uint32) *buf++) << 16);      /* merge hi 16b */
            WriteL (ma, dat);                           /* store LW */
            }
        }
    }
return 0;
}

/* Map an address via the translation map */

t_bool uba_map_addr (uint32 ua, uint32 *ma)
{
uint32 ublk, umap, dpr;

ublk = ua >> VA_V_VPN;                                  /* Unibus blk */
//if ((ublk < UBACR_GETDSB (uba_cr)) ||                   /* map disabled? */
if  (ublk >= UBA_NMAPR)                                 /* unimplemented? */
    return FALSE;
umap = uba_map[ublk];                                   /* get map */
if (umap == 0xFFFFFFFF)
    return FALSE;                                       /* ignore transaction */
if (umap & UBAMAP_VLD) {                                /* valid? */
    *ma = ((umap & UBAMAP_PAG) << VA_V_VPN) + VA_GETOFF (ua);
    if ((umap & UBAMAP_DP) && (umap & UBAMAP_ODD)) {    /* buffered dp? */
        if (umap & UBAMAP_LWAE) {
            dpr = UBAMAP_GETDP (umap);                  /* get datapath */
            if ((dpr == 6) || (dpr == 7))
                return FALSE;                           /* ignore transfer */
            }
        *ma = *ma + 1;                                  /* byte offset? */
        }
    if (ADDR_IS_MEM (*ma))                              /* valid mem address */
        return TRUE;
    if ((umap & UBAMAP_IOAD) && (ADDR_IS_IO (*ma)))     /* valid i/o address */
        return TRUE;
    uba_bi_nxm (*ma);
    return FALSE;
    }
uba_inv_map (ua);                                       /* invalid map */
return FALSE;
}

/* Map an address via the translation map - console version (no status changes) */

t_bool uba_map_addr_c (uint32 ua, uint32 *ma)
{
uint32 ublk, umap;

ublk = ua >> VA_V_VPN;                                  /* Unibus blk */
//if ((ublk < UBACR_GETDSB (uba_cr)) ||                   /* map disabled? */
if  (ublk >= UBA_NMAPR)                                 /* unimplemented? */
    return FALSE;
umap = uba_map[ublk];                                   /* get map */
if (umap & UBAMAP_VLD) {                                /* valid? */
    *ma = ((umap & UBAMAP_PAG) << VA_V_VPN) + VA_GETOFF (ua);
    if ((umap & UBAMAP_DP) && (umap & UBAMAP_ODD))      /* buffered dp? */
        *ma = *ma + 1;                                  /* byte offset? */
    return TRUE;                                        /* legit addr */
    }
return FALSE;
}

/* Error routines

   uba_ub_nxm           SBI read/write to nx Unibus address
   uba_inv_map          Unibus reference to invalid map reg
*/

void uba_ub_nxm (int32 ua)
{
if ((uba_csr & UBACSR_TO) == 0) {
    uba_csr |= UBACSR_TO;
    uba_fubar = (ua >> 2) & UBAFUBAR_RD;
    uba_adap_set_int ();
    }
sim_debug (UBA_DEB_ERR, &uba_dev,
    "nxm error, ua = %X, PC = %X\n", ua, fault_PC);
return;
}

void uba_bi_nxm (int32 ba)
{
if ((uba_biic.ber & BIBER_BTO) == 0) {
    uba_biic.ber |= BIBER_BTO;
    uba_bifa = ba;
    uba_adap_set_int ();
    }
sim_debug (UBA_DEB_ERR, &uba_dev,
    "BI nxm error, ba = %X, PC = %X\n", ba, fault_PC);
return;
}

void uba_inv_map (int32 ublk)
{
if ((uba_csr & UBACSR_IMR) == 0) {
    uba_csr |= UBACSR_IMR;
    uba_adap_set_int ();
    }
sim_debug (UBA_DEB_ERR, &uba_dev,
    "inv map error, ublk = %X\n", ublk);
return;
}

/* Unibus power fail routines */

void uba_ubpdn (int32 time)
{
int32 i;
DEVICE *dptr;

uba_biic.gpr0 = uba_biic.gpr0 & ~BIGPR0_UPU;            /* UB power down */
sim_activate (&uba_unit, time);                         /* schedule */
uba_uiip = 1;                                           /* UB init in prog */
for (i = 0; sim_devices[i] != NULL; i++) {              /* reset Unibus */
    dptr = sim_devices[i];
    if (dptr->reset && (dptr->flags & DEV_UBUS))
        dptr->reset (dptr);
    }
return;
}

/* Init timeout service routine */

t_stat uba_svc (UNIT *uptr)
{
if (uba_aiip) {                                         /* adapter init? */
    uba_aiip = 0;                                       /* clear in prog */
    sim_activate (uptr, uba_uitime);                    /* schedule UB */
    }
else {
    uba_uiip = 0;                                       /* no, UB */
    uba_biic.gpr0 = uba_biic.gpr0 | BIGPR0_UPU;         /* UB power up */
    }
uba_adap_set_int ();                                    /* possible int */
return SCPE_OK;
}

/* Interrupt routines */

void uba_adap_set_int ()
{
if (uba_csr & UBACSR_EIE) {
    uba_int = 1;
    sim_debug (UBA_DEB_ERR, &uba_dev,
        "adapter int req, csr = %X\n", uba_csr);
    }
return;
}

void uba_adap_clr_int ()
{
if (!(uba_csr & UBACSR_EIE))
    uba_int = 0;
return;
}

/* Reset Unibus adapter */

t_stat uba_reset (DEVICE *dptr)
{
int32 i;

uba_int = 0;
uba_aiip = uba_uiip = 0;
sim_cancel (&uba_unit);
for (i = 0; i < IPL_HLVL; i++) {
    nexus_req[i] &= ~(1 << TR_UBA);
    int_req[i] = 0;
    }
for (i = 0; i < UBA_NMAPR; i++) {                       /* clear map registers */
    if (i < UBA_NMAPU)
       uba_map[i] = 0;
    else
       uba_map[i] = 0xffffffff;
    }
for (i = 0; i < UBA_NDPATH; i++)                        /* setup datapaths */
    uba_dpr[i] = (i << UBADPR_V_SEL);
uba_csr = 0;
uba_biic.csr = (1u << BICSR_V_IF) | BICSR_STS | (TR_UBA & BICSR_NODE);
uba_biic.ber = 0;
uba_biic.eicr = 0;
uba_biic.idest = 0;
uba_biic.sa = UBADDRBASE;
uba_biic.ea = UBADDRBASE + WINSIZE;
uba_biic.uiic = BIICR_EXV;
uba_biic.gpr0 = BIGPR0_UPU;
return SCPE_OK;
}

t_stat uba_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Unibus Adapter (UBA)\n\n");
fprintf (st, "The Unibus adapter (UBA) simulates the DWBUA.\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe UBA implements main memory examination and modification via the Unibus\n");
fprintf (st, "map.  The data width is always 16b:\n\n");
fprintf (st, "EXAMINE UBA 0/10                examine main memory words corresponding\n");
fprintf (st, "                                to Unibus addresses 0-10\n");
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *uba_description (DEVICE *dptr)
{
return "Unibus adapter";
}

/* Memory examine via map (word only) */

t_stat uba_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 ua = (uint32) exta, pa;

if ((vptr == NULL) || (ua >= UBADDRSIZE))
    return SCPE_ARG;
if (uba_map_addr_c (ua, &pa) && ADDR_IS_MEM (pa)) {
    *vptr = (uint32) ReadW (pa);
    return SCPE_OK;
    }
return SCPE_NXM;
}

/* Memory deposit via map (word only) */

t_stat uba_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 ua = (uint32) exta, pa;

if (ua >= UBADDRSIZE)
    return SCPE_ARG;
if (uba_map_addr_c (ua, &pa) && ADDR_IS_MEM (pa)) {
    WriteW (pa, (int32) val);
    return SCPE_OK;
    }
return SCPE_NXM;
}

/* Show UBA virtual address */

t_stat uba_show_virt (FILE *of, UNIT *uptr, int32 val, CONST void *desc)
{
t_stat r;
char *cptr = (char *) desc;
uint32 ua, pa;

if (cptr) {
    ua = (uint32) get_uint (cptr, 16, UBADDRSIZE - 1, &r);
    if (r == SCPE_OK) {
        if (uba_map_addr_c (ua, &pa))
            fprintf (of, "Unibus %-X = physical %-X\n", ua, pa);
        else fprintf (of, "Unibus %-X: invalid mapping\n", ua);
        return SCPE_OK;
        }
    }
fprintf (of, "Invalid argument\n");
return SCPE_OK;
}
