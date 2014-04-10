/* vax860_sbia.c: VAX 8600 SBIA

   Copyright (c) 2011-2012, Matt Burke
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

   sbia                 SBI adapter

   26-Dec-2012  MB      First Version
*/

#include "vax_defs.h"

/* SBIA registers */

#define SBICSR_MIE      0x80000000                      /* master int en */
#define SBICSR_SCOEN    0x40000000                      /* SBI cycles out enable */
#define SBICSR_SCIEN    0x20000000                      /* SBI cycles in enable */
#define SBICSR_WR       (SBICSR_MIE | SBICSR_SCOEN | SBICSR_SCIEN)

#define SBIFS_RD        (0x031F0000|SBI_FAULTS)         /* SBI faults */
#define SBIFS_WR        0x03140000
#define SBIFS_W1C       0x00080000

#define SBISC_RD        0xFFFF0000                      /* SBI silo comp */
#define SBISC_WR        0x7FFF0000
#define SBISC_LOCK      0x80000000                      /* lock */

#define SBIMT_RD        0xFFFFFF00                      /* SBI maint */
#define SBIMT_WR        0xFFFFF900

#define SBIER_CRDIE     0x00008000                      /* SBI error, CRD IE */
#define SBIER_CRD       0x00004000                      /* CRD */
#define SBIER_RDS       0x00002000                      /* RDS */
#define SBIER_TMO       0x00001000                      /* timeout */
#define SBIER_STA       0x00000C00                      /* timeout status (0) */
#define SBIER_CNF       0x00000100                      /* error confirm */
#define SBIER_IBRDS     0x00000080
#define SBIER_IBTMO     0x00000040
#define SBIER_IBSTA     0x00000030
#define SBIER_IBCNF     0x00000008
#define SBIER_MULT      0x00000004                      /* multiple errors */
#define SBIER_FREE      0x00000002                      /* SBI free */
#define SBIER_RD        0x0000FDFE
#define SBIER_WR        0x00008000
#define SBIER_W1C       0x000070C0
#define SBIER_TMOW1C    (SBIER_TMO|SBIER_STA|SBIER_CNF|SBIER_MULT)
#define SBIER_IBTW1C    (SBIER_IBTMO|SBIER_STA|SBIER_IBCNF)

#define SBITMO_V_MODE   30                              /* mode */
#define SBITMO_VIRT     0x20000000                      /* physical */

#define SBIQC_MBZ       0xC0000007                      /* MBZ */

uint32 nexus_req[NEXUS_HLVL];                           /* nexus int req */
uint32 sbi_fs = 0;                                      /* SBI fault status */
uint32 sbi_sc = 0;                                      /* SBI silo comparator */
uint32 sbi_mt = 0;                                      /* SBI maintenance */
uint32 sbi_er = 0;                                      /* SBI error status */
uint32 sbi_tmo = 0;                                     /* SBI timeout addr */
uint32 sbi_csr = 0;                                     /* SBI control/status */

extern int32 R[16];
extern int32 PSL;
extern int32 ASTLVL, SISR;
extern jmp_buf save_env;
extern int32 trpirq;
extern int32 p1;
extern int32 mchk_ref;
extern int32 crd_err;
extern int32 fault_PC;                                  /* fault PC */
extern UNIT cpu_unit;

t_stat sbia_reset (DEVICE *dptr);
char *sbia_description (DEVICE *dptr);
void sbi_set_tmo (int32 pa);
t_stat (*nexusR[NEXUS_NUM])(int32 *dat, int32 ad, int32 md);
t_stat (*nexusW[NEXUS_NUM])(int32 dat, int32 ad, int32 md);

extern int32 intexc (int32 vec, int32 cc, int32 ipl, int ei);
extern int32 eval_int (void);

/* SBIA data structures

   sbia_dev     SBIA device descriptor
   sbia_unit    SBIA unit
   sbia_reg     SBIA register list
*/

UNIT sbia_unit = { UDATA (NULL, 0, 0) };

REG sbia_reg[] = {
    { HRDATA (NREQ14, nexus_req[0], 16) },
    { HRDATA (NREQ15, nexus_req[1], 16) },
    { HRDATA (NREQ16, nexus_req[2], 16) },
    { HRDATA (NREQ17, nexus_req[3], 16) },
    { HRDATA (SBIFS, sbi_fs, 32) },
    { HRDATA (SBISC, sbi_sc, 32) },
    { HRDATA (SBIMT, sbi_mt, 32) },
    { HRDATA (SBIER, sbi_er, 32) },
    { HRDATA (SBITMO, sbi_tmo, 32) },
    { HRDATA (SBICSR, sbi_csr, 32) },
    { NULL }
    };

DEVICE sbia_dev = {
    "SBIA", &sbia_unit, sbia_reg, NULL,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &sbia_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, 
    &sbia_description
    };

int32 sbia_rd (int32 pa, int32 lnt)
{
    int32 rg = (pa >> 2) & 0x1F;

    switch (rg) {
    case 0:                                             /* SBICNF */
        return 0x00400010;                              /* 8MB + SBIA Abus code */

    case 1:                                             /* SBICSR */
        return sbi_csr;

    case 2:                                             /* SBIES (not impl) */
    case 3:                                             /* SBIDCR (not impl) */
    case 4:                                             /* DMAI CMD (not impl) */
    case 5:                                             /* DMAI ID (not impl) */
    case 6:                                             /* DMAA CMD (not impl) */
    case 7:                                             /* DMAA ID (not impl) */
    case 8:                                             /* DMAB CMD (not impl) */
    case 9:                                             /* DMAB ID (not impl) */
    case 0xa:                                           /* DMAC CMD (not impl) */
    case 0xb:                                           /* DMAC ID (not impl) */
    case 0xc:                                           /* SBIS (not impl) */
        return 0;

    case 0xd:                                           /* SBIER */
        return sbi_er & SBIER_RD;

    case 0xe:                                           /* SBITA */
        return sbi_tmo;

    case 0xf:                                           /* SBIFS */
        return sbi_fs & SBIFS_RD;

    case 0x10:                                          /* SBISC */
        return sbi_sc & SBISC_RD;

    case 0x11:                                          /* SBIMT */
        return sbi_mt & SBIMT_RD;

    default:                                            /* Anything else is not impl */
        return 0;
   
    }
}

void sbia_wr (int32 pa, int32 val, int32 lnt)
{
    int32 rg = (pa >> 2) & 0x1F;

    switch (rg) {
    case 0:                                             /* SBICNF */
        break;

    case 1:                                             /* SBICSR */
        sim_printf ("sbi_csr wr: %08X\n", val);
        sbi_csr = sbi_csr & SBICSR_WR;
        break;

    case 2:                                             /* SBIES (not impl) */
    case 3:                                             /* SBIDCR (not impl) */
    case 4:                                             /* DMAI CMD (not impl) */
    case 5:                                             /* DMAI ID (not impl) */
    case 6:                                             /* DMAA CMD (not impl) */
    case 7:                                             /* DMAA ID (not impl) */
    case 8:                                             /* DMAB CMD (not impl) */
    case 9:                                             /* DMAB ID (not impl) */
    case 0xa:                                           /* DMAC CMD (not impl) */
    case 0xb:                                           /* DMAC ID (not impl) */
    case 0xc:                                           /* SBIS (not impl) */
        break;

    case 0xd:                                           /* SBIER */
        sbi_er = (sbi_er & ~SBIER_WR) | (val & SBIER_WR);
        sbi_er = sbi_er & ~(val & SBIER_W1C);
        if (val & SBIER_TMO)
            sbi_er = sbi_er & ~SBIER_TMOW1C;
        if (val & SBIER_IBTMO)
            sbi_er = sbi_er & ~SBIER_IBTW1C;
        if ((sbi_er & SBIER_CRDIE) && (sbi_er & SBIER_CRD))
            crd_err = 1;
        else crd_err = 0;
        break;

    case 0xe:                                           /* SBITA */
        break;

    case 0xf:                                           /* SBIFS */
        sbi_fs = (sbi_fs & ~SBIFS_WR) | (val & SBIFS_WR);
        sbi_fs = sbi_fs & ~(val & SBIFS_W1C);
        break;

    case 0x10:                                          /* SBISC */
        sbi_sc = (sbi_sc & ~(SBISC_LOCK|SBISC_WR)) | (val & SBISC_WR);
        break;

    case 0x11:                                          /* SBIMT */
        sbi_mt = (sbi_mt & ~SBIMT_WR) | (val & SBIMT_WR);
        break;
    }
return;
}

t_stat sbi_rd (int32 pa, int32 *val, int32 lnt)
{
int32 nexus;

nexus = NEXUS_GETNEX (pa);                          /* get nexus */
if ((sbi_csr & SBICSR_SCOEN) &&                     /* SBI en? */
    nexusR[nexus] &&                                /* valid? */
    (nexusR[nexus] (val, pa, lnt) == SCPE_OK)) {
    SET_IRQL;
    return SCPE_OK;
    }
else sbi_set_tmo (pa);                              /* timeout */
return SCPE_NXM;
}

t_stat sbi_wr (int32 pa, int32 val, int32 lnt)
{
int32 nexus;

nexus = NEXUS_GETNEX (pa);                          /* get nexus */
if ((sbi_csr & SBICSR_SCOEN) &&                     /* SBI en? */
    nexusW[nexus] &&                                /* valid? */
    (nexusW[nexus] (val, pa, lnt) == SCPE_OK)) {
    SET_IRQL;
    return SCPE_OK;
    }
else sbi_set_tmo (pa);                              /* timeout */
return SCPE_NXM;
}

/* Set SBI timeout - machine checks only on reads */

void sbi_set_tmo (int32 pa)
{
if ((sbi_er & SBIER_TMO) == 0) {                        /* not yet set? */
    sbi_tmo = pa >> 2;                                  /* save addr */
    if (mchk_ref == REF_V)                              /* virt? add mode */
        sbi_tmo |= SBITMO_VIRT | (PSL_GETCUR (PSL) << SBITMO_V_MODE);
    sbi_er |= SBIER_TMO;                                /* set tmo flag */
    }
else sbi_er |= SBIER_MULT;                              /* yes, multiple */
return;
}

/* Set SBI error confirmation - always machine checks */

void sbi_set_errcnf (void)
{
if (sbi_er & SBIER_CNF)
    sbi_er |= SBIER_MULT;
else sbi_er |= SBIER_CNF;
MACH_CHECK (MCHK_RD_F);
return;
}

/* SBI reset */

t_stat sbia_reset (DEVICE *dptr)
{
sbi_fs = 0;
sbi_sc = 0;
sbi_mt = 0;
sbi_er = 0;
sbi_tmo = 0;
sbi_csr = SBICSR_SCOEN | SBICSR_SCIEN;
return SCPE_OK;
}

char *sbia_description (DEVICE *dptr)
{
return "SBI adapter";
}

/* Show nexus */

t_stat show_nexus (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, "nexus=%d", val);
return SCPE_OK;
}

/* Init nexus tables */

void init_nexus_tab (void)
{
uint32 i;

for (i = 0; i < NEXUS_NUM; i++) {
    nexusR[i] = NULL;
    nexusW[i] = NULL;
    }
return;
}

/* Build nexus tables

   Inputs:
        dptr    =       pointer to device
        dibp    =       pointer to DIB
   Outputs:
        status
*/

t_stat build_nexus_tab (DEVICE *dptr, DIB *dibp)
{
uint32 idx;

if ((dptr == NULL) || (dibp == NULL))
    return SCPE_IERR;
idx = dibp->ba;
if (idx >= NEXUS_NUM)
    return SCPE_IERR;
if ((nexusR[idx] && dibp->rd &&                         /* conflict? */
    (nexusR[idx] != dibp->rd)) ||
    (nexusW[idx] && dibp->wr &&
    (nexusW[idx] != dibp->wr))) {
    sim_printf ("Nexus %s conflict at %d\n", sim_dname (dptr), dibp->ba);
    return SCPE_STOP;
    }
if (dibp->rd)                                           /* set rd dispatch */
    nexusR[idx] = dibp->rd;
if (dibp->wr)                                           /* set wr dispatch */
    nexusW[idx] = dibp->wr;
return SCPE_OK;
}
