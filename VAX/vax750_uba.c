/* vax750_uba.c: VAX 11/750 Unibus adapter

   Copyright (c) 2012-2017, Matt Burke
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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name(s) of the author(s) shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author(s).

   uba                  DW750 Unibus adapter

   21-Oct-2012  MB      First Version
*/

#include "vax_defs.h"

/* Unibus adapter */

#define UBA_NMAPR       512                             /* number of map reg */

/* Control/Status registers */

#define UBACSR1_OF      0x01
#define UBACSR2_OF      0x02
#define UBACSR3_OF      0x03
#define UBACSR_PUR      0x00000001                      /* Purge request */
#define UBACSR_UCE      0x20000000                      /* Uncorrectable err */
#define UBACSR_NXM      0x40000000                      /* NXM */
#define UBACSR_ERR      0x80000000                      /* Error flag */
#define UBACSR_RD       (UBACSR_PUR | UBACSR_UCE | UBACSR_NXM | \
                         UBACSR_ERR)
#define UBACSR_W1C      (UBACSR_UCE | UBACSR_NXM)

#define UBA_VEC_MASK    0x1FC                           /* Vector value mask */

/* Map registers */

#define UBAMAP_OF       0x200
#define UBAMAP_VLD      0x80000000                      /* valid */
#define UBAMAP_ODD      0x02000000                      /* odd byte */
#define UBAMAP_V_DP     21                              /* data path */
#define UBAMAP_M_DP     0x3
#define UBAMAP_DP       (UBAMAP_M_DP << UBAMAP_V_DP)
#define UBAMAP_GETDP(x) (((x) >> UBAMAP_V_DP) & UBAMAP_M_DP)
#define UBAMAP_PAG      0x00007FFF
#define UBAMAP_RD       (0x82000000 | UBAMAP_DP | UBAMAP_PAG)
#define UBAMAP_WR       (UBAMAP_RD)

/* Debug switches */

#define UBA_DEB_RRD     0x01                            /* reg reads */
#define UBA_DEB_RWR     0x02                            /* reg writes */
#define UBA_DEB_MRD     0x04                            /* map reads */
#define UBA_DEB_MWR     0x08                            /* map writes */
#define UBA_DEB_XFR     0x10                            /* transfers */
#define UBA_DEB_ERR     0x20                            /* errors */

int32 int_req[IPL_HLVL] = { 0 };                        /* intr, IPL 14-17 */
uint32 uba_csr1 = 0;                                    /* csr reg 1 */
uint32 uba_csr2 = 0;                                    /* csr reg 2 */
uint32 uba_csr3 = 0;                                    /* csr reg 3 */
uint32 uba_int = 0;                                     /* UBA interrupt */
uint32 uba_map[UBA_NMAPR] = { 0 };                      /* map registers */
int32 autcon_enb = 1;                                   /* autoconfig enable */

extern uint32 nexus_req[NEXUS_HLVL];

t_stat uba_reset (DEVICE *dptr);
const char *uba_description (DEVICE *dptr);
t_stat uba_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat uba_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat uba_rdreg (int32 *val, int32 pa, int32 mode);
t_stat uba_wrreg (int32 val, int32 pa, int32 lnt);
int32 uba_get_ubvector (int32 lvl);
void uba_eval_int (void);
void uba_ioreset (void);
t_bool uba_map_addr (uint32 ua, uint32 *ma);
t_stat uba_show_virt (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat uba_show_map (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

extern int32 eval_int (void);
extern t_stat build_dib_tab (void);
extern void cmi_set_tmo (void);

/* Unibus IO page dispatches */

t_stat (*iodispR[IOPAGESIZE >> 1])(int32 *dat, int32 ad, int32 md);
t_stat (*iodispW[IOPAGESIZE >> 1])(int32 dat, int32 ad, int32 md);
DIB *iodibp[IOPAGESIZE >> 1];

/* Unibus interrupt request to interrupt action map */

int32 (*int_ack[IPL_HLVL][32])(void);                   /* int ack routines */

/* Unibus interrupt request to vector map */

int32 int_vec[IPL_HLVL][32];                            /* int req to vector */
int32 int_vec_set[IPL_HLVL][32] = { 0 };                /* bits to set in vector */

/* Unibus adapter data structures

   uba_dev      UBA device descriptor
   uba_unit     UBA units
   uba_reg      UBA register list
*/

DIB uba_dib = { TR_UBA, 0, &uba_rdreg, &uba_wrreg, 0, 0 };

UNIT uba_unit = { UDATA (0, 0, 0) };

REG uba_reg[] = {
    { HRDATAD (IPL17,          int_req[3],     32, "IPL 17 interrupt flags"), REG_RO },
    { HRDATAD (IPL16,          int_req[2],     32, "IPL 16 interrupt flags"), REG_RO },
    { HRDATAD (IPL15,          int_req[1],     32, "IPL 15 interrupt flags"), REG_RO },
    { HRDATAD (IPL14,          int_req[0],     32, "IPL 14 interrupt flags"), REG_RO },
    { HRDATAD (CSR1,             uba_csr1,     32, "Control/Status register for BDP #1") },
    { HRDATAD (CSR2,             uba_csr2,     32, "Control/Status register for BDP #2") },
    { HRDATAD (CSR3,             uba_csr3,     32, "Control/Status register for BDP #3") },
    { FLDATAD (INT,               uba_int,      0, "Interrupt pending") },
    { FLDATAD (NEXINT, nexus_req[IPL_UBA], TR_UBA, "Nexus interrupt pending") },
    { BRDATAD (MAP,               uba_map, 16, 32, UBA_NMAPR, "Unibus map registers") },
    { FLDATA  (AUTOCON,        autcon_enb, 0), REG_HRO },
    { NULL }
    };

MTAB uba_mod[] = {
    { MTAB_XTD|MTAB_VDV, TR_UBA, "NEXUS", NULL,
      NULL, &show_nexus, NULL, "Display nexus" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "IOSPACE", NULL,
      NULL, &show_iospace, NULL, "Display I/O space address map" },
    { MTAB_XTD|MTAB_VDV, 1, "AUTOCONFIG", "AUTOCONFIG",
      &set_autocon, &show_autocon, NULL, "Enable/Display  autoconfiguration" },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOAUTOCONFIG",
      &set_autocon, NULL, NULL, "Disable autoconfiguration" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "VIRTUAL", NULL,
      NULL, &uba_show_virt, NULL, "Display translation for Unibus address arg" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "MAP", NULL,
      NULL, &uba_show_map, NULL, "Display Unibus Map Register(s)" },
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
    uba_deb, NULL, NULL, NULL, NULL, NULL, 
    &uba_description
    };

/* Read Unibus adapter register - aligned lw only */

t_stat uba_rdreg (int32 *val, int32 pa, int32 lnt)
{
int32 idx, ofs;

ofs = NEXUS_GETOFS (pa);                                /* get offset */
if (ofs >= UBAMAP_OF) {                                 /* map? */
    idx = ofs - UBAMAP_OF;
    if (idx >= UBA_NMAPR)                               /* valid? */
        return SCPE_NXM;
    *val = uba_map[idx] & UBAMAP_RD;
    if (DEBUG_PRI (uba_dev, UBA_DEB_MRD))
        fprintf (sim_deb, ">>UBA: map %d read, value = %X at PC = %08X\n", idx, *val, fault_PC);
    return SCPE_OK;
    }

switch (ofs) {                                          /* case on offset */

    case UBACSR1_OF:                                    /* CSR1 */
        *val = (uba_csr1 & UBACSR_RD);
        break;

    case UBACSR2_OF:                                    /* CSR2 */
        *val = (uba_csr2 & UBACSR_RD);
        break;

    case UBACSR3_OF:                                    /* CSR3 */
        *val = (uba_csr3 & UBACSR_RD);
        break;

    default:
        *val = 0;
        break;
        }

if (DEBUG_PRI (uba_dev, UBA_DEB_RRD))
    fprintf (sim_deb, ">>UBA: reg %d read, value = %X at PC = %08X\n", ofs, *val, fault_PC);
return SCPE_OK;
}

/* Write Unibus adapter register */

t_stat uba_wrreg (int32 val, int32 pa, int32 lnt)
{
int32 idx, ofs;

ofs = NEXUS_GETOFS (pa);                                /* get offset */
if (ofs >= UBAMAP_OF) {                                 /* map? */
    idx = ofs - UBAMAP_OF;
    if (idx >= UBA_NMAPR)                               /* valid? */
        return SCPE_NXM;
    uba_map[idx] = val & UBAMAP_WR;
    if (DEBUG_PRI (uba_dev, UBA_DEB_MWR))
        fprintf (sim_deb, ">>UBA: map %d write, value = %X at PC = %08X\n", idx, val, fault_PC);
    return SCPE_OK;
    }

switch (ofs) {                                          /* case on offset */

    case UBACSR1_OF:                                    /* CSR1 */
        uba_csr1 = uba_csr1 & ~(val & UBACSR_W1C);
        break;

    case UBACSR2_OF:                                    /* CSR2 */
        uba_csr2 = uba_csr2 & ~(val & UBACSR_W1C);
        break;

    case UBACSR3_OF:                                    /* CSR3 */
        uba_csr3 = uba_csr3 & ~(val & UBACSR_W1C);
        break;

    default:
        break;
    }

if (DEBUG_PRI (uba_dev, UBA_DEB_RWR))
    fprintf (sim_deb, ">>UBA: reg %d write, value = %X at PC = %08X\n", ofs, val, fault_PC);
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
cmi_set_tmo();
MACH_CHECK(MCHK_BPE);
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
cmi_set_tmo();
mem_err = 1;                                            /* interrupt */
SET_IRQL;
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
else
    sim_printf (">>UBA: invalid write mask, pa = %x, lnt = %d, val = 0x%x\n", pa, lnt, val);
SET_IRQL;                                               /* update ints */
return;
}

/* Update UBA nexus interrupts */

void uba_eval_int (void)
{
int32 i;

for (i = 0; i < (IPL_HMAX - IPL_HMIN); i++)             /* clear all UBA req */
    nexus_req[i] &= ~(1 << TR_UBA);
for (i = 0; i < (IPL_HMAX - IPL_HMIN); i++) {
    if (int_req[i])
        nexus_req[i] |= (1 << TR_UBA);
    }
if (uba_int)                                            /* adapter int? */
    SET_NEXUS_INT (UBA);
return;
}

/* Return vector for Unibus interrupt at relative IPL level [0-3] */

int32 uba_get_ubvector (int32 lvl)
{
int32 i;

if ((lvl == IPL_UBA) && uba_int) {                      /* UBA lvl, int? */
    uba_int = 0;                                        /* clear int */
    }
for (i = 0; int_req[lvl] && (i < 32); i++) {
    if ((int_req[lvl] >> i) & 1) {
        int32 vec;
        
        int_req[lvl] = int_req[lvl] & ~(1u << i);
        if (int_ack[lvl][i])
            vec = int_ack[lvl][i]();
        else
            vec = int_vec[lvl][i];
        vec |= int_vec_set[lvl][i];
        vec &= (int_vec_set[lvl][i] | UBA_VEC_MASK);
        return vec;
        }
    }
return 0;
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
    }
return 0;
}

/* Map an address via the translation map */

t_bool uba_map_addr (uint32 ua, uint32 *ma)
{
uint32 ublk, umap;

ublk = ua >> VA_V_VPN;                                  /* Unibus blk */
if (ublk >= UBA_NMAPR)                                  /* unimplemented? */
    return FALSE;
umap = uba_map[ublk];                                   /* get map */
if (umap & UBAMAP_VLD) {                                /* valid? */
    *ma = ((umap & UBAMAP_PAG) << VA_V_VPN) + VA_GETOFF (ua);
    if ((umap & UBAMAP_DP) && (umap & UBAMAP_ODD))      /* buffered dp? */
        *ma = *ma + 1;                                  /* byte offset? */
    return (ADDR_IS_MEM (*ma));                         /* legit addr */
    }
return FALSE;
}

/* Map an address via the translation map - console version (no status changes) */

t_bool uba_map_addr_c (uint32 ua, uint32 *ma)
{
uint32 ublk, umap;

ublk = ua >> VA_V_VPN;                                  /* Unibus blk */
if (ublk >= UBA_NMAPR)                                  /* unimplemented? */
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

/* Reset Unibus devices */

void uba_ioreset (void)
{
int32 i;
DEVICE *dptr;

for (i = 0; sim_devices[i] != NULL; i++) {              /* reset Unibus */
    dptr = sim_devices[i];
    if (dptr->reset && (dptr->flags & DEV_UBUS))
        dptr->reset (dptr);
    }
return;
}

/* Reset Unibus adapter */

t_stat uba_reset (DEVICE *dptr)
{
int32 i;

for (i = 0; i < IPL_HLVL; i++) {
    nexus_req[i] &= ~(1 << TR_UBA);
    int_req[i] = 0;
    }
for (i = 0; i < UBA_NMAPR; i++)
    uba_map[i] = UBAMAP_VLD|i;
uba_csr1 = 0;
uba_csr2 = 0;
uba_csr3 = 0;
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

/* Show UBA map register(s) */

t_stat uba_show_map (FILE *of, UNIT *uptr, int32 val, CONST void *desc)
{
return show_bus_map (of, (const char *)desc, uba_map, UBA_NMAPR, "Unibus", UBAMAP_VLD);
}
