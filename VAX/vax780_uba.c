/* vax780_uba.c: VAX 11/780 Unibus adapter

   Copyright (c) 2004-2012, Robert M Supnik

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

   uba                  DW780 Unibus adapter

   25-Mar-12    RMS     Added parameter to int_ack prototype (Mark Pizzolata)
   19-Nov-08    RMS     Moved I/O support routines to I/O library
   28-May-08    RMS     Inlined physical memory routines
   25-Jan-08    RMS     Fixed declarations (Mark Pizzolato)
*/

#include "vax_defs.h"

/* Unibus adapter */

#define UBA_NDPATH      16                              /* number of data paths */
#define UBA_NMAPR       496                             /* number of map reg */

/* Unibus configuration register */

#define UBACNF_OF       0x00
#define UBACNF_ADPDN    0x00800000                      /* adap pdn - ni */
#define UBACNF_ADPUP    0x00400000                      /* adap pup - ni */
#define UBACNF_UBINIT   0x00040000                      /* UB INIT - ni */
#define UBACNF_UBPDN    0x00020000                      /* UB pdn */
#define UBACNF_UBIC     0x00010000                      /* UB init done */
#define UBACNF_CODE     0x00000028                      /* adapter code */
#define UBACNF_RD       (SBI_FAULTS|UBACNF_W1C)
#define UBACNF_W1C      0x00C70000

/* Control register */

#define UBACR_OF        0x01
#define UBACR_V_DSB     26                              /* map disable */
#define UBACR_M_DSB     0x1F
#define UBACR_GETDSB(x) (((x) >> (UBACR_V_DSB - 4)) & (UBACR_M_DSB << 4))
#define UBACR_IFS       0x00000040                      /* SBI field intr */
#define UBACR_BRIE      0x00000020                      /* BR int enable */
#define UBACR_USEFIE    0x00000010                      /* UB to SBI int enb */
#define UBACR_SUEFIE    0x00000008                      /* SBI to UB int enb */
#define UBACR_CNFIE     0x00000004                      /* config int enb */
#define UBACR_UPF       0x00000002                      /* power fail UB */
#define UBACR_ADINIT    0x00000001                      /* adapter init */
#define UBACR_RD        ((UBACR_M_DSB << UBACR_V_DSB) | 0x0000007E)
#define UBACR_WR        (UBACR_RD)

#define UBA_USEFIE_SR   (UBASR_RDTO|UBASR_RDS|UBASR_CRD|UBASR_CXTER| \
                         UBASR_CXTO|UBASR_DPPE|UBASR_IVMR|UBASR_MRPF)
#define UBA_SUEFIE_SR   (UBASR_UBSTO|UBASR_UBTMO)
#define UBA_CNFIE_CR    (UBACNF_ADPDN|UBACNF_ADPUP|UBACNF_UBINIT|\
                         UBACNF_UBPDN|UBACNF_UBIC)

/* Status register */

#define UBASR_OF        0x02
#define UBASR_V_BR4     24                              /* filled br's - ni */
#define UBASR_RDTO      0x00000400                      /* read tmo - ni */
#define UBASR_RDS       0x00000200                      /* read sub - ni */
#define UBASR_CRD       0x00000100                      /* read crd - ni */
#define UBASR_CXTER     0x00000080                      /* cmd xfr err - ni */
#define UBASR_CXTO      0x00000040                      /* cmd xfr tmo - ni */
#define UBASR_DPPE      0x00000020                      /* parity err - ni */
#define UBASR_IVMR      0x00000010                      /* invalid map reg */
#define UBASR_MRPF      0x00000008                      /* map reg par - ni */
#define UBASR_LEB       0x00000004                      /* locked err */
#define UBASR_UBSTO     0x00000002                      /* UB select tmo - ni */
#define UBASR_UBTMO     0x00000001                      /* UB nxm */
#define UBASR_RD        0x0F0007FF
#define UBASR_W1C       0x000007FF

/* Diagnostic register */

#define UBADR_OF        0x03
#define UBADR_SPARE     0x80000000                      /* spare */
#define UBADR_DINTR     0x40000000                      /* disable intr */
#define UBADR_DMP       0x20000000
#define UBADR_DDPP      0x10000000
#define UBADR_MICOK     0x08000000                      /* microseq ok */
#define UBADR_RD        0xF8000000
#define UBADR_WR        0xF0000000
#define UBADR_CNF_RD    0x07FFFFFF

/* Failing map entry - read only */

#define UBAFMER_OF      0x04
#define UBAFMER_OF1     0x06
#define UBAFMER_RD      0x1FF

/* Failing Unibus address - read only */

#define UBAFUBAR_OF     0x05
#define UBAFUBAR_OF1    0x07
#define UBAFUBAR_RD     0xFFFF

/* Spare registers - read/write */

#define UBABRSVR_OF     0x08

/* Vector registers - read only */

#define UBABRRVR_OF     0x0C
#define UBA_UVEC        0x80000000
#define UBA_VEC_MASK    0x1FC                           /* Vector value mask */

/* Data path registers */

#define UBADPR_OF       0x010
#define UBADPR_BNE      0x80000000                      /* buf not empty - ni */
#define UBADPR_BTE      0x40000000                      /* buf xfr err - ni */
#define UBADPR_DIR      0x20000000                      /* buf rd/wr */
#define UBADPR_STATE    0x00FF0000                      /* byte full state - ni */
#define UBADPR_UA       0x0000FFFF                      /* Unibus addr<17:2> */
#define UBADPR_UA       0x0000FFFF                      /* last UB addr */
#define UBADPR_RD       0xC0FFFFFF
#define UBADPR_W1C      0xC0000000

/* Map registers */

#define UBAMAP_OF       0x200
#define UBAMAP_VLD      0x80000000                      /* valid */
#define UBAMAP_LWAE     0x04000000                      /* LW access enb - ni */
#define UBAMAP_ODD      0x02000000                      /* odd byte */
#define UBAMAP_V_DP     21                              /* data path */
#define UBAMAP_M_DP     0xF
#define UBAMAP_DP       (UBAMAP_M_DP << UBAMAP_V_DP)
#define UBAMAP_GETDP(x) (((x) >> UBAMAP_V_DP) & UBAMAP_M_DP)
#define UBAMAP_PAG      0x001FFFFF
#define UBAMAP_RD       (0x86000000 | UBAMAP_DP | UBAMAP_PAG)
#define UBAMAP_WR       (UBAMAP_RD)

/* Debug switches */

#define UBA_DEB_RRD     0x01                            /* reg reads */
#define UBA_DEB_RWR     0x02                            /* reg writes */
#define UBA_DEB_MRD     0x04                            /* map reads */
#define UBA_DEB_MWR     0x08                            /* map writes */
#define UBA_DEB_XFR     0x10                            /* transfers */
#define UBA_DEB_ERR     0x20                            /* errors */

int32 int_req[IPL_HLVL] = { 0 };                        /* intr, IPL 14-17 */
uint32 uba_cnf = 0;                                     /* config reg */
uint32 uba_cr = 0;                                      /* control reg */
uint32 uba_sr = 0;                                      /* status reg */
uint32 uba_dr = 0;                                      /* diag ctrl */
uint32 uba_int = 0;                                     /* UBA interrupt */
uint32 uba_fmer = 0;                                    /* failing map reg */
uint32 uba_fubar = 0;                                   /* failing Unibus addr */
uint32 uba_svr[4] = { 0 };                              /* diag registers */
uint32 uba_rvr[4] = { 0 };                              /* vector reg */
uint32 uba_dpr[UBA_NDPATH] = { 0 };                     /* number data paths */
uint32 uba_map[UBA_NMAPR] = { 0 };                      /* map registers */
uint32 uba_aiip = 0;                                    /* adapter init in prog */
uint32 uba_uiip = 0;                                    /* Unibus init in prog */
uint32 uba_aitime = 250;                                /* adapter init time */
uint32 uba_uitime = 12250;                              /* Unibus init time */
int32 autcon_enb = 1;                                   /* autoconfig enable */

extern int32 autcon_enb;
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
void uba_inv_map (int32 ublk);
void uba_eval_int (void);
void uba_adap_set_int (int32 flg);
void uba_adap_clr_int ();
void uba_set_dpr (uint32 ua, t_bool wr);
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
    { HRDATAD (IPL14,   int_req[0],             32, "IPL 14 Interrupt Request"), REG_RO },
    { HRDATAD (IPL15,   int_req[1],             32, "IPL 15 Interrupt Request"), REG_RO },
    { HRDATAD (IPL16,   int_req[2],             32, "IPL 16 Interrupt Request"), REG_RO },
    { HRDATAD (IPL17,   int_req[3],             32, "IPL 17 Interrupt Request"), REG_RO },
    { HRDATAD (CNFR,    uba_cnf,                32, "config register") },
    { HRDATAD (CR,      uba_cr,                 32, "control register") },
    { HRDATAD (SR,      uba_sr,                 32, "status register") },
    { HRDATAD (DR,      uba_dr,                 32, "diagnostic control register") },
    { FLDATAD (INT,     uba_int,                 0, "UBA interrupt") },
    { FLDATAD (NEXINT,  nexus_req[IPL_UBA], TR_UBA, "") },
    { FLDATAD (AIIP,    uba_aiip,                0, "adapter interrupt in progress") },
    { FLDATAD (UIIP,    uba_uiip,                0, "Unibus interrupt in progress") },
    { HRDATAD (FMER,    uba_fmer,               32, "failing map register") },
    { HRDATAD (FUBAR,   uba_fubar,              32, "failing Unibus address") },
    { HRDATAD (BRSVR0,  uba_svr[0],             32, "diagnostic register 0") },
    { HRDATAD (BRSVR1,  uba_svr[1],             32, "diagnostic register 1") },
    { HRDATAD (BRSVR2,  uba_svr[2],             32, "diagnostic register 2") },
    { HRDATAD (BRSVR3,  uba_svr[3],             32, "diagnostic register 3") },
    { HRDATAD (BRRVR4,  uba_rvr[0],             32, "vector register 0") },
    { HRDATAD (BRRVR5,  uba_rvr[1],             32, "vector register 1") },
    { HRDATAD (BRRVR6,  uba_rvr[2],             32, "vector register 2") },
    { HRDATAD (BRRVR7,  uba_rvr[3],             32, "vector register 3") },
    { BRDATAD (DPR,     uba_dpr,        16, 32, 16, "number data paths") },
    { BRDATAD (MAP,     uba_map,       16, 32, 496, "Unibus map registers") },
    { DRDATAD (AITIME,  uba_aitime,             24, "adapter init time"), PV_LEFT + REG_NZ },
    { DRDATAD (UITIME,  uba_uitime,             24, "Unibus init time"), PV_LEFT + REG_NZ },
    { FLDATA  (AUTOCON, autcon_enb,              0), REG_HRO },
    { NULL }
    };

MTAB uba_mod[] = {
    { MTAB_XTD|MTAB_VDV, TR_UBA, "NEXUS", NULL,
      NULL, &show_nexus, NULL, "Display nexus" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "IOSPACE", NULL,
      NULL, &show_iospace, NULL, "Display IO address space assignments" },
    { MTAB_XTD|MTAB_VDV, 1, "AUTOCONFIG", "AUTOCONFIG",
      &set_autocon, &show_autocon, NULL, "Enable/Display autoconfiguration" },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOAUTOCONFIG",
      &set_autocon, NULL, NULL, "Disable autoconfiguration" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "VIRTUAL", NULL,
      NULL, &uba_show_virt, NULL, "Show physical address translation for Unibus\n"
  "                                address arg" },
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

if ((pa & 3) || (lnt != L_LONG)) {                      /* unaligned or not lw? */
    sim_printf (">>UBA: invalid adapter read mask, pa = %X, lnt = %d\r\n", pa, lnt);
    sbi_set_errcnf ();                                  /* err confirmation */
    return SCPE_OK;
    }
ofs = NEXUS_GETOFS (pa);                                /* get offset */
if (uba_aiip && (ofs != UBACNF_OF)&& (ofs != UBADR_OF)) /* init in prog? */
    return SCPE_NXM;                                    /* only cnf, dr */
if (ofs >= UBAMAP_OF) {                                 /* map? */
    idx = ofs - UBAMAP_OF;
    if (idx >= UBA_NMAPR)                               /* valid? */
        return SCPE_NXM;
    *val = uba_map[idx] & UBAMAP_RD;
    if (DEBUG_PRI (uba_dev, UBA_DEB_MRD))
        fprintf (sim_deb, ">>UBA: map %d read, value = %X\n", idx, *val);
    return SCPE_OK;
    }

switch (ofs) {                                          /* case on offset */

    case UBACNF_OF:                                     /* CNF */
        *val = (uba_cnf & UBACNF_RD) | UBACNF_CODE;
        break;

    case UBACR_OF:                                      /* CR */
        *val = uba_cr & UBACR_RD;
        break;

    case UBASR_OF:                                      /* SR */
        *val = uba_sr & UBASR_RD;
        break;

    case UBADR_OF:                                      /* DR */
        *val = (uba_dr & UBADR_RD) | UBADR_MICOK |
            ((uba_cnf & UBADR_CNF_RD) | UBACNF_CODE);
        break;

    case UBAFMER_OF: case UBAFMER_OF1:                  /* FMER */
        *val = uba_fmer & UBAFMER_RD;
        break;

    case UBAFUBAR_OF: case UBAFUBAR_OF1:                /* FUBAR */
        *val = uba_fubar & UBAFUBAR_RD;
        break;

    case UBABRSVR_OF + 0: case UBABRSVR_OF + 1:         /* BRSVR */
    case UBABRSVR_OF + 2: case UBABRSVR_OF + 3:
        idx = ofs - UBABRSVR_OF;
        *val = uba_svr[idx];
        break;

    case UBABRRVR_OF + 0: case UBABRRVR_OF + 1:         /* BRRVR */
    case UBABRRVR_OF + 2: case UBABRRVR_OF + 3:
        idx = ofs - UBABRRVR_OF;
        uba_rvr[idx] = uba_get_ubvector (idx);
        *val = uba_rvr[idx];
        break;

    case UBADPR_OF + 0:                                 /* DPR */
        *val = 0;                                       /* direct */
        break;

    case UBADPR_OF + 1:
    case UBADPR_OF + 2:  case UBADPR_OF + 3:
    case UBADPR_OF + 4:  case UBADPR_OF + 5:
    case UBADPR_OF + 6:  case UBADPR_OF + 7:
    case UBADPR_OF + 8:  case UBADPR_OF + 9:
    case UBADPR_OF + 10: case UBADPR_OF + 11:
    case UBADPR_OF + 12: case UBADPR_OF + 13:
    case UBADPR_OF + 14: case UBADPR_OF + 15:
        idx = ofs - UBADPR_OF;
        *val = uba_dpr[idx] & UBADPR_RD;
        break;

    default:
        return SCPE_NXM;
        }

if (DEBUG_PRI (uba_dev, UBA_DEB_RRD))
    fprintf (sim_deb, ">>UBA: reg %d read, value = %X\n", ofs, *val);
return SCPE_OK;
}

/* Write Unibus adapter register */

t_stat uba_wrreg (int32 val, int32 pa, int32 lnt)
{
int32 idx, ofs, old_cr;

if ((pa & 3) || (lnt != L_LONG)) {                      /* unaligned or not lw? */
    sim_printf (">>UBA: invalid adapter write mask, pa = %X, lnt = %d\r\n", pa, lnt);
    sbi_set_errcnf ();                                  /* err confirmation */
    return SCPE_OK;
    }
ofs = NEXUS_GETOFS (pa);                                /* get offset */
if (uba_aiip && (ofs != UBACNF_OF) && (ofs != UBADR_OF) &&
    (ofs != UBACR_OF) && (ofs != UBASR_OF))
    return SCPE_NXM;
if (ofs >= UBAMAP_OF) {                                 /* map? */
    idx = ofs - UBAMAP_OF;
    if (idx >= UBA_NMAPR)                               /* valid? */
        return SCPE_NXM;
    uba_map[idx] = val & UBAMAP_WR;
    if (DEBUG_PRI (uba_dev, UBA_DEB_MWR))
        fprintf (sim_deb, ">>UBA: map %d write, value = %X\n", idx, val);
    return SCPE_OK;
    }

switch (ofs) {                                          /* case on offset */

    case UBACNF_OF:                                     /* CNF */
        uba_cnf = uba_cnf & ~(val & UBACNF_W1C);        /* W1C bits */
        uba_adap_clr_int ();                            /* possible clr int */
        break;

    case UBACR_OF:                                      /* CR */
        old_cr = uba_cr;
        if (val & UBACR_ADINIT) {                       /* adapter init */
            uba_reset (&uba_dev);                       /* reset adapter */
            uba_aiip = 1;                               /* set init in prog */
            uba_ubpdn (uba_aitime);                     /* power fail UB */
            }
        if ((val & UBACR_UPF) && !(old_cr & UBACR_UPF)  /* Unibus power clear */
            && !sim_is_active (&uba_unit))
            uba_ubpdn (uba_aitime + uba_uitime);        /* power fail UB */
        uba_cr = (uba_cr & ~UBACR_WR) | (val & UBACR_WR);
        uba_adap_set_int (uba_cr & ~old_cr);            /* possible int set */
        uba_adap_clr_int ();                            /* possible int clr */
        break;

    case UBASR_OF:                                      /* SR */
        uba_sr = uba_sr & ~(val & UBASR_W1C);           /* W1C bits */
        uba_adap_clr_int ();                            /* possible clr int */
        break;

    case UBADR_OF:                                      /* DR */
        uba_dr = (uba_dr & ~UBADR_WR) | (val & UBADR_WR);
        uba_cnf = uba_cnf & ~(val & UBACNF_W1C);
        uba_adap_clr_int ();                            /* possible clr int */
        break;

    case UBABRSVR_OF + 0: case UBABRSVR_OF + 1:         /* BRSVR */
    case UBABRSVR_OF + 2: case UBABRSVR_OF + 3:
        idx = ofs - UBABRSVR_OF;
        uba_svr[idx] = val;
        break;

    case UBADPR_OF + 0:                                 /* DPR */
        break;                                          /* direct */

    case UBADPR_OF + 1:
    case UBADPR_OF + 2:  case UBADPR_OF + 3:
    case UBADPR_OF + 4:  case UBADPR_OF + 5:
    case UBADPR_OF + 6:  case UBADPR_OF + 7:
    case UBADPR_OF + 8:  case UBADPR_OF + 9:
    case UBADPR_OF + 10: case UBADPR_OF + 11:
    case UBADPR_OF + 12: case UBADPR_OF + 13:
    case UBADPR_OF + 14: case UBADPR_OF + 15:
        idx = ofs - UBADPR_OF;
        uba_dpr[idx] = uba_dpr[idx] & ~(val & UBADPR_W1C);
        break;

    default:
        return SCPE_NXM;
        }

if (DEBUG_PRI (uba_dev, UBA_DEB_RWR))
    fprintf (sim_deb, ">>UBA: reg %d write, value = %X\n", ofs, val);
return SCPE_OK;
}

/* Read and write Unibus I/O space */

int32 ReadUb (uint32 pa)
{
int32 idx, val;

if (ADDR_IS_IOP (pa) && !uba_uiip) {                    /* iopage,!init */
    idx = (pa & IOPAGEMASK) >> 1;
    if (iodispR[idx]) {
        iodispR[idx] (&val, pa, READ);
        return val;
        }
    }
uba_ub_nxm (pa);                                        /* UB nxm */
return 0;
}

void WriteUb (uint32 pa, int32 val, int32 mode)
{
int32 idx;

if (ADDR_IS_IOP (pa) && !uba_uiip) {                    /* iopage,!init */
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
    sim_printf (">>UBA: invalid read mask, pa = %x, lnt = %d\n", pa, lnt);
    sbi_set_errcnf ();                                  /* err confirmation */
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
else if ((lnt == L_WORD) && ((pa & 1) == 0))            /* aligned word? */
     WriteUb (pa, val, WRITE);                          /* DATO */
else {
    sim_printf (">>UBA: invalid write mask, pa = %x, lnt = %d\n", pa, lnt);
    sbi_set_errcnf ();                                  /* err confirmation */
    }
SET_IRQL;                                               /* update ints */
return;
}

/* Update UBA nexus interrupts */

void uba_eval_int (void)
{
int32 i;

for (i = 0; i < (IPL_HMAX - IPL_HMIN); i++)             /* clear all UBA req */
    nexus_req[i] &= ~(1 << TR_UBA);
if (((uba_dr & UBADR_DINTR) == 0) && !uba_uiip &&       /* intr enabled? */
    (uba_cr & UBACR_IFS) && (uba_cr & UBACR_BRIE)) {
    for (i = 0; i < (IPL_HMAX - IPL_HMIN); i++) {
        if (int_req[i])
            nexus_req[i] |= (1 << TR_UBA);
        }
    }
if (uba_int)                                            /* adapter int? */
    SET_NEXUS_INT (UBA);
return;
}

/* Return vector for Unibus interrupt at relative IPL level [0-3] */

int32 uba_get_ubvector (int32 lvl)
{
int32 i, vec;

vec = 0;
if ((lvl == (IPL_UBA - IPL_HMIN)) && uba_int) {         /* UBA lvl, int? */
    vec = UBA_UVEC;                                     /* set flag */
    uba_int = 0;                                        /* clear int */
    }
if (((uba_dr & UBADR_DINTR) == 0) && !uba_uiip &&       /* intr enabled? */
    (uba_cr & UBACR_IFS) && (uba_cr & UBACR_BRIE)) {
    for (i = 0; int_req[lvl] && (i < 32); i++) {
        if ((int_req[lvl] >> i) & 1) {
            int_req[lvl] = int_req[lvl] & ~(1u << i);
            if (int_ack[lvl][i])
                return ((UBA_VEC_MASK|UBA_UVEC) & (vec | int_ack[lvl][i]()));
            return ((UBA_VEC_MASK|UBA_UVEC) & (vec | int_vec[lvl][i]));
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
    if (pbc > (bc - i))                                 /* limit to rem xfr */
        pbc = bc - i;
    if (DEBUG_PRI (uba_dev, UBA_DEB_XFR))
        fprintf (sim_deb, ">>UBA: 8b read, ma = %X, bc = %X\n", ma, pbc);
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
    uba_set_dpr (ba + i + pbc - L_BYTE, FALSE);
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
    if (DEBUG_PRI (uba_dev, UBA_DEB_XFR))
        fprintf (sim_deb, ">>UBA: 16b read, ma = %X, bc = %X\n", ma, pbc);
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
    uba_set_dpr (ba + i + pbc - L_WORD, FALSE);
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
    if (DEBUG_PRI (uba_dev, UBA_DEB_XFR))
        fprintf (sim_deb, ">>UBA: 8b write, ma = %X, bc = %X\n", ma, pbc);
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
    uba_set_dpr (ba + i + pbc - L_BYTE, TRUE);
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
    if (DEBUG_PRI (uba_dev, UBA_DEB_XFR))
        fprintf (sim_deb, ">>UBA: 16b write, ma = %X, bc = %X\n", ma, pbc);
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
    uba_set_dpr (ba + i + pbc - L_WORD, TRUE);
    }
return 0;
}

/* Map an address via the translation map */

t_bool uba_map_addr (uint32 ua, uint32 *ma)
{
uint32 ublk, umap;

ublk = ua >> VA_V_VPN;                                  /* Unibus blk */
if ((ublk < UBACR_GETDSB (uba_cr)) ||                   /* map disabled? */
    (ublk >= UBA_NMAPR))                                /* unimplemented? */
    return FALSE;
umap = uba_map[ublk];                                   /* get map */
if (umap & UBAMAP_VLD) {                                /* valid? */
    *ma = ((umap & UBAMAP_PAG) << VA_V_VPN) + VA_GETOFF (ua);
    if ((umap & UBAMAP_DP) && (umap & UBAMAP_ODD))      /* buffered dp? */
        *ma = *ma + 1;                                  /* byte offset? */
    return (ADDR_IS_MEM (*ma));                         /* legit addr */
    }
uba_inv_map (ua);                                       /* invalid map */
return FALSE;
}

/* Map an address via the translation map - console version (no status changes) */

t_bool uba_map_addr_c (uint32 ua, uint32 *ma)
{
uint32 ublk, umap;

ublk = ua >> VA_V_VPN;                                  /* Unibus blk */
if ((ublk < UBACR_GETDSB (uba_cr)) ||                   /* map disabled? */
    (ublk >= UBA_NMAPR))                                /* unimplemented? */
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

/* At end of page or transfer, update DPR register, in case next page
   gets an error */

void uba_set_dpr (uint32 ua, t_bool wr)
{
uint32 ublk, umap, dpr;

ublk = ua >> VA_V_VPN;                                  /* Unibus blk */
if (ublk >= UBA_NMAPR)                                  /* paranoia */
    return;
umap = uba_map[ublk];                                   /* get map */
dpr = UBAMAP_GETDP (umap);                              /* get bdp */
if (dpr) uba_dpr[dpr] = (uba_dpr[dpr] & ~(UBADPR_UA|UBADPR_DIR)) |
    (wr? UBADPR_DIR: 0) |
    (((ua >> 2) + ((umap & UBAMAP_ODD)? 1: 0)) & UBADPR_UA);
return;
}

/* Error routines

   uba_ub_nxm           SBI read/write to nx Unibus address
   uba_inv_map          Unibus reference to invalid map reg
*/

void uba_ub_nxm (int32 ua)
{
if ((uba_sr & UBASR_UBTMO) == 0) {
    uba_sr |= UBASR_UBTMO;
    uba_adap_set_int (uba_cr & UBACR_SUEFIE);
    uba_fubar = (ua >> 2) & UBAFUBAR_RD;
    }
else uba_sr |= UBASR_LEB;
if (DEBUG_PRI (uba_dev, UBA_DEB_ERR))
    fprintf (sim_deb, ">>UBA: nxm error, ua = %X\n", ua);
return;
}

void uba_inv_map (int32 ublk)
{
if ((uba_sr & UBASR_IVMR) == 0) {
    uba_sr |= UBASR_IVMR;
    uba_adap_set_int (uba_cr & UBACR_USEFIE);
    uba_fmer = ublk & UBAFMER_RD;
    }
else uba_sr |= UBASR_LEB;
if (DEBUG_PRI (uba_dev, UBA_DEB_ERR))
    fprintf (sim_deb, ">>UBA: inv map error, ublk = %X\n", ublk);
return;
}

/* Unibus power fail routines */

void uba_ubpdn (int32 time)
{
int32 i;
DEVICE *dptr;

uba_cnf = (uba_cnf & ~UBACNF_UBIC) | UBACNF_UBPDN;      /* update cnf */
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
    uba_cnf = (uba_cnf & ~UBACNF_UBPDN) | UBACNF_UBIC;
    uba_adap_set_int (uba_cr & UBACR_CNFIE);            /* possible int */
    }
return SCPE_OK;
}

/* Interrupt routines */

void uba_adap_set_int (int32 flg)
{
if (((flg & UBACR_SUEFIE) && (uba_sr & UBA_SUEFIE_SR)) ||
    ((flg & UBACR_USEFIE) && (uba_sr & UBA_USEFIE_SR)) ||
    ((flg & UBACR_CNFIE) && (uba_cr & UBA_CNFIE_CR))) {
    uba_int = 1;
    if (DEBUG_PRI (uba_dev, UBA_DEB_ERR)) fprintf (sim_deb, 
        ">>UBA: adapter int req, sr = %X, cr = %X\n", uba_sr, uba_cr);
    }
return;
}

void uba_adap_clr_int ()
{
if ((!(uba_cr & UBACR_SUEFIE) || !(uba_sr & UBA_SUEFIE_SR)) &&
    (!(uba_cr & UBACR_USEFIE) || !(uba_sr & UBA_USEFIE_SR)) &&
    (!(uba_cr & UBACR_CNFIE) || !(uba_cr & UBA_CNFIE_CR)))
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
    uba_svr[i] = 0;
    uba_rvr[i] = 0;
    }
for (i = 0; i < UBA_NMAPR; i++)
    uba_map[i] = 0;
for (i = 0; i < UBA_NDPATH; i++)
    uba_dpr[i] = 0;
uba_sr = 0;
uba_cr = 0;
uba_dr = 0;
uba_cnf = UBACNF_UBIC;
return SCPE_OK;
}

t_stat uba_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Unibus Adapter (UBA)\n\n");
fprintf (st, "The Unibus adapter (UBA) simulates the DW780.\n");
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
const char *cptr = (const char *) desc;
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
