/* vax780_mem.c: VAX 11/780 memory controllers

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

   This module contains the VAX 11/780 system-specific registers and devices.

   mctl0, mctl1         MS780C/E memory controllers
*/

#include "vax_defs.h"

/* Memory controller register A */

#define MCRA_OF             0x0
#define MCRA_SUMM           0x00100000                      /* err summ (MS780E) */
#define MCRA_M_SIZE         0x00007E00                      /* array size - field */
#define MCRA_V_SIZE         9
#define MCRA_ILVE           0x00000100                      /* interleave wr enab */
#define MCRA_M_TYPE         0x000000F8                      /* type */
#define MCRA_C_TYPE_16K     0x00000010                      /* 16k uninterleaved (256kb arrays) */
#define MCRA_C_TYPE_4K      0x00000008                      /* 4k uninterleaved (64kb arrays) */
#define MCRA_E_TYPE_256K    0x00000070                      /* 256k uninterleaved (4096kb arrays) */
#define MCRA_E_TYPE_64K     0x00000068                      /* 64k uninterleaved (1024kb arrays) */
#define MCRA_E_TYPE         0x0000006A                      /* 256k upper + lower */
#define MCRA_ILV            0x00000007                      /* interleave */
#define MCRA_RD             (0x00107FFF|SBI_FAULTS)
#define MCRA_WR             0x00000100

/* Memory controller register B */

#define MCRB_OF         0x1
#define MCRB_FP         0xF0000000                      /* file pointers */
#define MCRB_V_SA       15                              /* start addr */
#define MCRB_M_SA       0x1FFF
#define MCRB_SA         (MCRB_M_SA << MCRB_V_SA)
#define MCRB_SAE        0x00004000                      /* start addr wr enab */
#define MCRB_INIT       0x00003000                      /* init state */
#define MCRB_REF        0x00000400                      /* refresh */
#define MCRB_ECC        0x000003FF                      /* ECC for diags */
#define MCRB_RD         0xFFFFF7FF
#define MCRB_WR         0x000043FF

/* Memory controller register C,D */

#define MCRC_OF         0x2
#define MCRD_OF         0x3
#define MCRC_DCRD       0x40000000                      /* disable CRD */
#define MCRC_HER        0x20000000                      /* high error rate */
#define MCRC_ERL        0x10000000                      /* log error */
#define MCRC_C_ER       0x0FFFFFFF                      /* MS780C error */
#define MCRC_E_PE1      0x00080000                      /* MS780E par ctl 1 */
#define MCRC_E_PE0      0x00040000                      /* MS780E par ctl 0 */
#define MCRC_E_CRD      0x00000200                      /* MS780E CRD */
#define MCRC_E_PEW      0x00000100                      /* MS780E par err wr */
#define MCRC_E_USEQ     0x00000080                      /* MS780E seq err */
#define MCRC_C_RD       0x7FFFFFFF
#define MCRC_E_RD       0x700C0380
#define MCRC_WR         0x40000000
#define MCRC_C_W1C      0x30000000
#define MCRC_E_W1C      0x300C0380

#define MCRROM_OF       0x400

uint32 mcr_a[MCTL_NUM];
uint32 mcr_b[MCTL_NUM];
uint32 mcr_c[MCTL_NUM];
uint32 mcr_d[MCTL_NUM];
uint32 rom_lw[MCTL_NUM][ROMSIZE >> 2];

t_stat mctl_reset (DEVICE *dptr);
const char *mctl_description (DEVICE *dptr);
t_stat mctl_rdreg (int32 *val, int32 pa, int32 mode);
t_stat mctl_wrreg (int32 val, int32 pa, int32 mode);

/* MCTLx data structures

   mctlx_dev    MCTLx device descriptor
   mctlx_unit   MCTLx unit
   mctlx_reg    MCTLx register list
*/

DIB mctl0_dib = { TR_MCTL0, 0, &mctl_rdreg, &mctl_wrreg, 0 };

UNIT mctl0_unit = { UDATA (NULL, 0, 0) };

REG mctl0_reg[] = {
    { HRDATA (CRA, mcr_a[0], 32) },
    { HRDATA (CRB, mcr_b[0], 32) },
    { HRDATA (CRC, mcr_c[0], 32) },
    { HRDATA (CRD, mcr_d[0], 32) },
    { BRDATA (ROM, rom_lw[0], 16, 32, ROMSIZE >> 2) },
    { NULL }
    };

MTAB mctl0_mod[] = {
    { MTAB_XTD|MTAB_VDV, TR_MCTL0, "NEXUS", NULL,
      NULL, &show_nexus, NULL, "Display nexus" },
    { 0 }
    };

DIB mctl1_dib = { TR_MCTL1, 0, &mctl_rdreg, &mctl_wrreg, 0 };

UNIT mctl1_unit = { UDATA (NULL, 0, 0) };

MTAB mctl1_mod[] = {
    { MTAB_XTD|MTAB_VDV, TR_MCTL1, "NEXUS", NULL,
      NULL, &show_nexus, NULL, "Display nexus" },
    { 0 }  };

REG mctl1_reg[] = {
    { HRDATA (CRA, mcr_a[1], 32) },
    { HRDATA (CRB, mcr_b[1], 32) },
    { HRDATA (CRC, mcr_c[1], 32) },
    { HRDATA (CRD, mcr_d[1], 32) },
    { BRDATA (ROM, rom_lw[1], 16, 32, ROMSIZE >> 2) },
    { NULL }
    };

DEVICE mctl_dev[] = {
    {
    "MCTL0", &mctl0_unit, mctl0_reg, mctl0_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &mctl_reset,
    NULL, NULL, NULL,
    &mctl0_dib, DEV_NEXUS, 0,
    NULL, NULL, NULL, NULL, NULL, NULL, 
    &mctl_description
    },
    {
    "MCTL1", &mctl1_unit, mctl1_reg, mctl1_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &mctl_reset,
    NULL, NULL, NULL,
    &mctl1_dib, DEV_NEXUS, 0,
    NULL, NULL, NULL, NULL, NULL, NULL, 
    &mctl_description
    }
    };

/* Memory controller register read */

t_stat mctl_rdreg (int32 *val, int32 pa, int32 lnt)
{
int32 mctl, ofs;
t_bool extmem = MEMSIZE > MAXMEMSIZE;

if ((pa & 3) || (lnt != L_LONG)) {                      /* unaligned or not lw? */
    sim_printf (">>MCTL: invalid adapter read mask, pa = %X, lnt = %d\r\n", pa, lnt);
    sbi_set_errcnf ();                                  /* err confirmation */
    return SCPE_OK;
    }
mctl = NEXUS_GETNEX (pa) - TR_MCTL0;                    /* get mctl num */
ofs = NEXUS_GETOFS (pa);                                /* get offset */
if (ofs >= MCRROM_OF) {                                 /* ROM? */
    *val = rom_lw[mctl][ofs - MCRROM_OF];               /* get lw */
    return SCPE_OK;
    }   
switch (ofs) {

    case MCRA_OF:                                       /* CR A */
        *val = mcr_a[mctl] & MCRA_RD;
        break;

    case MCRB_OF:                                       /* CR B */
        *val = (mcr_b[mctl] & MCRB_RD) | MCRB_INIT;
        break;

    case MCRC_OF:                                       /* CR C */
        *val = mcr_c[mctl] & (extmem? MCRC_E_RD: MCRC_C_RD);
        break;

    case MCRD_OF:                                       /* CR D */
        if (!extmem)                                    /* MS780E only */
            return SCPE_NXM;
        *val = mcr_d[mctl] & MCRC_E_RD;
        break;

    default:
        return SCPE_NXM;
        }

return SCPE_OK;
}

/* Memory controller register write */

t_stat mctl_wrreg (int32 val, int32 pa, int32 lnt)
{
int32 mctl, ofs, mask;
t_bool extmem = MEMSIZE > MAXMEMSIZE;

if ((pa & 3) || (lnt != L_LONG)) {                      /* unaligned or not lw? */
    sim_printf (">>MCTL: invalid adapter write mask, pa = %X, lnt = %d\r\n", pa, lnt);
    sbi_set_errcnf ();                                  /* err confirmation */
    return SCPE_OK;
    }
mctl = NEXUS_GETNEX (pa) - TR_MCTL0;                    /* get mctl num */
ofs = NEXUS_GETOFS (pa);                                /* get offset */
switch (ofs) {

    case MCRA_OF:                                       /* CR A */
        mask = MCRA_WR | ((val & MCRA_ILVE)? MCRA_ILV: 0);
        mcr_a[mctl] = (mcr_a[mctl] & ~mask) | (val & mask);
        break;

    case MCRB_OF:                                       /* CR B */
        mask = MCRB_WR | ((val & MCRB_SAE)? MCRB_SA: 0);
        mcr_b[mctl] = (mcr_b[mctl] & ~mask) | (val & mask);
        break;

    case MCRC_OF:                                       /* CR C */
        mcr_c[mctl] = ((mcr_c[mctl] & ~MCRC_WR) | (val & MCRC_WR)) &
            ~(val & (extmem? MCRC_E_W1C: MCRC_C_W1C));
        break;

    case MCRD_OF:                                       /* CR D */
        if (!extmem)                                    /* MS780E only */
            return SCPE_NXM;
        mcr_d[mctl] = ((mcr_d[mctl] & ~MCRC_WR) | (val & MCRC_WR)) &
            ~(val & MCRC_E_W1C);
        break;

    default:
        return SCPE_NXM;
        }

return SCPE_OK;
}

/* Used by CPU and loader */

void rom_wr_B (int32 pa, int32 val)
{
uint32 mctl = NEXUS_GETNEX (pa) - TR_MCTL0;             /* get mctl num */
uint32 ofs = NEXUS_GETOFS (pa) - MCRROM_OF;             /* get offset */
int32 sc = (pa & 3) << 3;

rom_lw[mctl][ofs] = ((val & 0xFF) << sc) | (rom_lw[mctl][ofs] & ~(0xFF << sc));
return;
}

/* MEMCTL reset */

t_stat mctl_reset (DEVICE *dptr)
{
int32 i, amb, akb;
t_bool extmem = MEMSIZE > MAXMEMSIZE;

amb = (int32) (MEMSIZE / MCTL_NUM) >> 20;               /* array size MB */
akb = (int32) (MEMSIZE / MCTL_NUM) >> 10;               /* array size KB */
for (i = 0; i < MCTL_NUM; i++) {                        
    if (extmem)                                         /* Need MS780E? */
        mcr_a[i] = ((amb - 1) << MCRA_V_SIZE) | ((amb <= 16) ? MCRA_E_TYPE_64K : MCRA_E_TYPE_256K);
    else                                                /* Use MS780C */
        mcr_a[i] = (((akb >> 6) - 1) << MCRA_V_SIZE) | ((akb <= 1024) ? MCRA_C_TYPE_4K : MCRA_C_TYPE_16K);
    mcr_b[i] = MCRB_INIT | ((i * akb) << (MCRB_V_SA - 6));
    mcr_c[i] = 0;
    mcr_d[i] = 0;
    }
return SCPE_OK;
}

const char *mctl_description (DEVICE *dptr)
{
static char buf[64];

sprintf (buf, "Memory controller %d", (int)(dptr-mctl_dev));
return buf;
}

t_stat cpu_show_memory (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
struct {
    uint32 capacity;
    const char *option;
    } boards[] = {
        { 4096, "MS780-JD M8374 array"},
        { 1024, "MS780-FD M8373 array"},
        {  256, "MS780-C M8210 array"}, 
        {   64, "MS780-C M8211 array"}, 
        {    0, NULL}};
uint32 i, slot, bd;

for (i = 0; i < MCTL_NUM; i++) {
    uint32 baseaddr = ((mcr_b[i] & MCRB_SA) << 1);

    fprintf (st, "Memory Controller %d - MS780-%s\n", i, ((mcr_a[i]&MCRA_M_TYPE) >> 5) ? "E" : "C");
    switch (mcr_a[i]&MCRA_M_TYPE) {
        case MCRA_C_TYPE_4K:
            bd = 3;         /* 4kbit chips, 64Kbyte arrays */
            break;
        case MCRA_C_TYPE_16K:
            bd = 2;         /* 16kbit chips, 256Kbyte arrays */
            break;
        case MCRA_E_TYPE_64K:
            bd = 1;         /* 64kbit chips, 1Mbyte arrays */
            break;
        case MCRA_E_TYPE_256K:
            bd = 0;         /* 256kbit chips, 4Mbyte arrays */
            break;
        }
    for (slot=0; slot<=((mcr_a[i]&MCRA_M_SIZE)>>MCRA_V_SIZE); slot += ((mcr_a[i]&MCRA_C_TYPE_4K)? 1 : 4)) {
        if (boards[bd].capacity)
            fprintf(st, "Memory slot %d (@0x%08x): %3d %sbytes (%s)\n", slot, baseaddr, boards[bd].capacity/((boards[bd].capacity>=1024) ? 1024 : 1), (boards[bd].capacity>=1024) ? "M" : "K", boards[bd].option);
        baseaddr += boards[bd].capacity<<10;
        }
    }
return SCPE_OK;
}
