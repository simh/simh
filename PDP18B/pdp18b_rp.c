/* pdp18b_rp.c: RP15/RP02 disk pack simulator

   Copyright (c) 1993-2016, Robert M Supnik

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

   rp           RP15/RP02/RP03 disk pack

   15-Mar-16    RMS     Added RP03 support
                        Fixed handling of done flag
   07-Mar-16    RMS     Revised for dynamically allocated memory
   13-Sep-15    RMS     Added APIVEC register
   14-Jan-04    RMS     Revised IO device call interface
   06-Feb-03    RMS     Revised IOT decoding, fixed bug in initiation
   05-Oct-02    RMS     Added DIB, device number support
   06-Jan-02    RMS     Revised enable/disable support
   29-Nov-01    RMS     Added read only unit support
   25-Nov-01    RMS     Revised interrupt structure
                        Changed FLG to array
   26-Apr-01    RMS     Added device enable/disable support
   14-Apr-99    RMS     Changed t_addr to unsigned
   29-Jun-96    RMS     Added unit enable/disable support
*/

#include "pdp18b_defs.h"

/* Constants */

#define RP_NUMWD        256                             /* words/sector */
#define RP_NUMSC        10                              /* sectors/surface */
#define RP_NUMSF        20                              /* surfaces/cylinder */
#define RP02_NUMCY      203                             /* cylinders/drive */
#define RP03_NUMCY      406
#define RP_NUMDR        8                               /* drives/controller */
#define RP02_SIZE       (RP02_NUMCY * RP_NUMSF * RP_NUMSC * RP_NUMWD)
#define RP03_SIZE       (RP03_NUMCY * RP_NUMSF * RP_NUMSC * RP_NUMWD)
#define RP_QCYL(f)      (((f) & UNIT_RP03)? RP03_NUMCY: RP02_NUMCY)
#define RP_QSIZE(f)     (((f) & UNIT_RP03)? RP03_SIZE: RP02_SIZE)

/* Unit specific flags */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* hwre write lock */
#define UNIT_V_RP03     (UNIT_V_UF + 1)                 /* RP03 */
#define UNIT_WLK        (1u << UNIT_V_WLK)
#define UNIT_RP03       (1u << UNIT_V_RP03)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */

/* Parameters in the unit descriptor */

#define CYL             u3                              /* current cylinder */
#define FUNC            u4                              /* function */

/* Status register A */

#define STA_V_UNIT      15                              /* unit select */
#define STA_M_UNIT      07
#define STA_V_FUNC      12                              /* function */
#define STA_M_FUNC      07
#define  FN_IDLE        0
#define  FN_READ        1
#define  FN_WRITE       2
#define  FN_RECAL       3
#define  FN_SEEK        4
#define  FN_RDALL       5
#define  FN_WRALL       6
#define  FN_WRCHK       7
#define  FN_2ND         010                             /* second state flag */
#define STA_IED         0004000                         /* int enable done */
#define STA_IEA         0002000                         /* int enable attn */
#define STA_GO          0001000                         /* go */
#define STA_WPE         0000400                         /* write lock error */
#define STA_NXC         0000200                         /* nx cyl error */
#define STA_NXF         0000100                         /* nx surface error */
#define STA_NXS         0000040                         /* nx sector error */
#define STA_HNF         0000020                         /* hdr not found */
#define STA_SUWP        0000010                         /* sel unit wrt lock */
#define STA_SUSI        0000004                         /* sel unit seek inc */
#define STA_DON         0000002                         /* done */
#define STA_ERR         0000001                         /* error */

#define STA_RW          0777000                         /* read/write */
#define STA_EFLGS       (STA_WPE | STA_NXC | STA_NXF | STA_NXS | \
                         STA_HNF | STA_SUSI)            /* error flags */
#define STA_DYN         (STA_SUWP | STA_SUSI)           /* per unit status */
#define GET_UNIT(x)     (((x) >> STA_V_UNIT) & STA_M_UNIT)
#define GET_FUNC(x)     (((x) >> STA_V_FUNC) & STA_M_FUNC)

/* Status register B */

#define STB_V_ATT0      17                              /* unit 0 attention */
#define STB_ATTN        0776000                         /* attention flags */
#define STB_SUFU        0001000                         /* sel unit unsafe */
#define STB_PGE         0000400                         /* programming error */
#define STB_EOP         0000200                         /* end of pack */
#define STB_TME         0000100                         /* timing error */
#define STB_FME         0000040                         /* format error */
#define STB_WCE         0000020                         /* write check error */
#define STB_WPE         0000010                         /* word parity error */
#define STB_LON         0000004                         /* long parity error */
#define STB_SUSU        0000002                         /* sel unit seeking */
#define STB_SUNR        0000001                         /* sel unit not rdy */

#define STB_EFLGS       (STB_SUFU | STB_PGE | STB_EOP | STB_TME | STB_FME | \
                         STB_WCE | STB_WPE | STB_LON )  /* error flags */
#define STB_DYN         (STB_SUFU | STB_SUSU | STB_SUNR) /* per unit */

/* Disk address */

#define DA_V_SECT       0                               /* sector */
#define DA_M_SECT       017
#define DA_V_C256       4
#define DA_C256         (1 << DA_V_C256)
#define DA_V_SURF       5
#define DA_M_SURF       037
#define DA_V_CYL        10                              /* cylinder */
#define DA_M_CYL        0377
#define GET_SECT(x)     (((x) >> DA_V_SECT) & DA_M_SECT)
#define GET_SURF(x)     (((x) >> DA_V_SURF) & DA_M_SURF)
#define GET_CYL(x)      ((((x) >> DA_V_CYL) & DA_M_CYL) + \
                         (((x) & DA_C256)? 256: 0))
#define GET_DA(x)       ((((GET_CYL (x) * RP_NUMSF) + GET_SURF (x)) * \
                        RP_NUMSC) + GET_SECT (x))

/* Current cylinder */

#define CCYL_RP03       0400000                         /* RP03 flag */

#define RP_MIN 2
#define MAX(x,y) (((x) > (y))? (x): (y))

extern int32 *M;
extern int32 int_hwre[API_HLVL+1], nexm;
extern int32 api_vec[API_HLVL][32];
extern UNIT cpu_unit;

int32 rp_sta = 0;                                       /* status A */
int32 rp_stb = 0;                                       /* status B */
int32 rp_ma = 0;                                        /* memory address */
int32 rp_da = 0;                                        /* disk address */
int32 rp_wc = 0;                                        /* word count */
int32 rp_busy = 0;                                      /* busy */
int32 rp_stopioe = 1;                                   /* stop on error */
int32 rp_swait = 10;                                    /* seek time */
int32 rp_rwait = 10;                                    /* rotate time */

int32 rp63 (int32 dev, int32 pulse, int32 dat);
int32 rp64 (int32 dev, int32 pulse, int32 dat);
int32 rp_iors (void);
t_stat rp_svc (UNIT *uptr);
void rp_updsta (int32 newa, int32 newb);
t_stat rp_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat rp_reset (DEVICE *dptr);
t_stat rp_attach (UNIT *uptr, CONST char *cptr);
t_stat rp_detach (UNIT *uptr);

/* RP15 data structures

   rp_dev       RP device descriptor
   rp_unit      RP unit list
   rp_reg       RP register list
   rp_mod       RP modifier list
*/

DIB rp_dib = { DEV_RP, 2, &rp_iors, { &rp63, &rp64 } };

UNIT rp_unit[] = {
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RP02_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RP02_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RP02_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RP02_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RP02_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RP02_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RP02_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, RP02_SIZE) }
    };

REG rp_reg[] = {
    { ORDATAD (STA, rp_sta, 18, "status A") },
    { ORDATAD (STB, rp_stb, 18, "status B") },
    { ORDATAD (DA, rp_da, 18, "disk address") },
    { ORDATAD (MA, rp_ma, 18, "current memory address") },
    { ORDATAD (WC, rp_wc, 18, "word count") },
    { FLDATAD (INT, int_hwre[API_RP], INT_V_RP, "interrupt pending flag") },
    { FLDATAD (BUSY, rp_busy, 0, "control busy flag") },
    { FLDATAD (STOP_IOE, rp_stopioe, 0, "stop on I/O error") },
    { DRDATAD (STIME, rp_swait, 24, "seek time per cylinder"), PV_LEFT },
    { DRDATAD (RTIME, rp_rwait, 24, "rotational delay"), PV_LEFT },
    { ORDATA (DEVNO, rp_dib.dev, 6), REG_HRO },
    { ORDATA (APIVEC, api_vec[API_RP][INT_V_RP], 6), REG_HRO },
    { NULL }
    };

MTAB rp_mod[] = {
    { UNIT_RP03, 0,         "RP02", "RP02", &rp_set_size },
    { UNIT_RP03, UNIT_RP03, "RP03", "RP03", &rp_set_size },
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO", &set_devno, &show_devno },
    { 0 }
    };

DEVICE rp_dev = {
    "RP", rp_unit, rp_reg, rp_mod,
    RP_NUMDR, 8, 26, 1, 8, 18,
    NULL, NULL, &rp_reset,
    NULL, &rp_attach, &rp_detach,
    &rp_dib, DEV_DISABLE
    };

/* IOT routines */

int32 rp63 (int32 dev, int32 pulse, int32 dat)
{
int32 sb = pulse & 060;                                 /* subopcode */

rp_updsta (0, 0);
if (pulse & 01) {
    if ((sb == 000) &&                                  /* DPSF */
        ((rp_sta & (STA_DON | STA_ERR)) || (rp_stb & STB_ATTN)))
        dat = IOT_SKP | dat;
    else if ((sb == 020) && (rp_stb & STB_ATTN))        /* DPSA */
        dat = IOT_SKP | dat;
    else if ((sb == 040) && (rp_sta & STA_DON))         /* DPSJ */
        dat = IOT_SKP | dat;
    else if ((sb == 060) && (rp_sta & STA_ERR))         /* DPSE */
        dat = IOT_SKP | dat;
    }
if (pulse & 02) {
    if (sb == 000)                                      /* DPOSA */
        dat = dat | rp_sta;
    else if (sb == 020)                                 /* DPOSB */
        dat = dat | rp_stb;
    }
if (pulse & 04) {
    if (rp_busy) {                                      /* busy? */
        rp_updsta (0, STB_PGE);                         /* prog error */
        return dat;
        }
    else if (sb == 000) {                               /* DPLA */
        int32 u = GET_UNIT (rp_sta);
        rp_da = dat & DMASK;
        if (GET_SECT (rp_da) >= RP_NUMSC)
            rp_updsta (STA_NXS, 0);
        if (GET_SURF (rp_da) >= RP_NUMSF)
            rp_updsta (STA_NXF, 0);
        if (GET_CYL (rp_da) >= RP_QCYL (rp_unit[u].flags))
            rp_updsta (STA_NXC, 0);
        }
    else if (sb == 020) {                               /* DPCS */
        rp_sta = rp_sta & ~(STA_HNF | STA_DON);         /* clr err, done */
        rp_stb = rp_stb & ~(STB_FME | STB_WPE | STB_LON | STB_WCE |
            STB_TME | STB_PGE | STB_EOP);
        rp_updsta (0, 0);
        }
    else if (sb == 040)                                 /* DPCA */
        rp_ma = dat & DMASK;
    else if (sb == 060)                                 /* DPWC */
        rp_wc = dat & DMASK;
    }
return dat;
}

/* IOT 64 */

int32 rp64 (int32 dev, int32 pulse, int32 dat)
{
int32 u, f, c, sb;
UNIT *uptr;

sb = pulse & 060;
if (pulse & 01) {
    if (sb == 020)                                      /* DPSN */
        dat = IOT_SKP | dat;
    }
if (pulse & 02) {
    if (sb == 000) {                                    /* DPOU */
        u = GET_UNIT (rp_sta);
        uptr = rp_dev.units + u;                        /* select unit */
        dat = dat | uptr->CYL | ((uptr->flags & UNIT_RP03)? CCYL_RP03: 0);
        }
    else if (sb == 020)                                 /* DPOA */
        dat = dat | rp_da;
    else if (sb == 040)                                 /* DPOC */
        dat = dat | rp_ma;
    else if (sb == 060)                                 /* DPOW */
        dat = dat | rp_wc;
    }
if (pulse & 04) {
    if (sb == 000) {                                    /* DPCF */
        rp_reset (&rp_dev);                             /* reset dev */
        return dat;
        }
    if (rp_busy != 0) {                                 /* others: busy? */
        rp_updsta (0, STB_PGE);                         /* prog error */
        return dat;
        }
    if (sb == 020)                                      /* DPLZ */
        rp_sta = rp_sta & (dat | ~STA_RW);
    else if (sb == 040)                                 /* DPLO */
         rp_sta = rp_sta | (dat & STA_RW);
    else if (sb == 060)                                 /* DPLF */
        rp_sta = (rp_sta & ~STA_RW) | (dat & STA_RW);
    u = GET_UNIT (rp_sta);                              /* get unit num */
    uptr = rp_dev.units + u;                            /* select unit */
    if ((rp_sta & STA_GO) && !sim_is_active (uptr)) {
        f = uptr->FUNC = GET_FUNC (rp_sta);             /* get function */
        rp_busy = 1;                                    /* set ctrl busy */
        rp_sta = rp_sta & ~STA_HNF;                     /* clear flag */
        rp_stb = rp_stb & ~(STB_FME | STB_WPE | STB_LON | STB_WCE |
            STB_TME | STB_PGE | STB_EOP | (1 << (STB_V_ATT0 - u)));
        if (((uptr->flags & UNIT_ATT) == 0) || (f == FN_IDLE) ||
            (f == FN_SEEK) || (f == FN_RECAL))
            sim_activate (uptr, RP_MIN);                /* short delay */
        else {
            c = GET_CYL (rp_da);
            c = abs (c - uptr->CYL) * rp_swait;         /* seek time */
            sim_activate (uptr, MAX (RP_MIN, c + rp_rwait));
            rp_sta = rp_sta & ~STA_DON;                 /* clear done */
            }
        }
    }
rp_updsta (0, 0);
return dat;
}

/* Unit service

   If function = idle, clear busy
   If seek or recal initial state, clear busy, compute seek time,
        put on cylinder, set second state
   If unit not attached, give error
   If seek or recal second state, set attention line, compute errors
   Else complete data transfer command

   The unit control block contains the function and cylinder for
   the current command.
*/

static int32 fill[RP_NUMWD] = { 0 };
t_stat rp_svc (UNIT *uptr)
{
int32 f, u, comp, cyl, sect, surf;
int32 err, pa, da, wc, awc, i;

u = (int32) (uptr - rp_dev.units);                      /* get drv number */
f = uptr->FUNC;                                         /* get function */
if (f == FN_IDLE) {                                     /* idle? */
    rp_busy = 0;                                        /* clear busy */
    return SCPE_OK;
    }

if ((f == FN_SEEK) || (f == FN_RECAL)) {                /* seek or recal? */
    rp_busy = 0;                                        /* not busy */
    cyl = (f == FN_SEEK)? GET_CYL (rp_da): 0;           /* get cylinder */
    sim_activate (uptr, MAX (RP_MIN, abs (cyl - uptr->CYL) * rp_swait));
    uptr->CYL = cyl;                                    /* on cylinder */
    uptr->FUNC = FN_SEEK | FN_2ND;                      /* set second state */
    rp_updsta (0, 0);                                   /* update status */
    return SCPE_OK;
    }

if (f == (FN_SEEK | FN_2ND)) {                          /* seek done? */
    rp_updsta (0, rp_stb | (1 << (STB_V_ATT0 - u)));    /* set attention */
    return SCPE_OK;
    }

if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
    rp_updsta (STA_DON, STB_SUFU);                      /* done, unsafe */
    return IORETURN (rp_stopioe, SCPE_UNATT);
    }

if ((f == FN_WRITE) && (uptr->flags & UNIT_WPRT)) {     /* write locked? */
    rp_updsta (STA_DON | STA_WPE, 0);                   /* error */
    return SCPE_OK;
    }

if (GET_SECT (rp_da) >= RP_NUMSC)
    rp_updsta (STA_NXS, 0);
if (GET_SURF (rp_da) >= RP_NUMSF)
    rp_updsta (STA_NXF, 0);
if (GET_CYL (rp_da) >= RP_QCYL (uptr->flags))
    rp_updsta (STA_NXC, 0);
if (rp_sta & (STA_NXS | STA_NXF | STA_NXC)) {           /* or bad disk addr? */
    rp_updsta (STA_DON, STB_SUFU);                      /* done, unsafe */
    return SCPE_OK;
    }

uptr->CYL = GET_CYL (rp_da);                            /* on cylinder */
pa = rp_ma & AMASK;                                     /* get mem addr */
da = GET_DA (rp_da) * RP_NUMWD;                         /* get disk addr */
wc = 01000000 - rp_wc;                                  /* get true wc */
if (((uint32) (pa + wc)) > MEMSIZE) {                   /* memory overrun? */
    nexm = 1;                                           /* set nexm flag */
    wc = MEMSIZE - pa;                                  /* limit xfer */
    }
if ((da + wc) > RP_QSIZE (uptr->flags)) {               /* disk overrun? */
    rp_updsta (0, STB_EOP);                             /* error */
    wc = RP_QSIZE (uptr->flags) - da;                   /* limit xfer */
    }

err = fseek (uptr->fileref, da * sizeof (int32), SEEK_SET);

if ((f == FN_READ) && (err == 0)) {                     /* read? */
    awc = fxread (&M[pa], sizeof (int32), wc, uptr->fileref);
    for ( ; awc < wc; awc++)
        M[pa + awc] = 0;
    err = ferror (uptr->fileref);
    }

if ((f == FN_WRITE) && (err == 0)) {                    /* write? */
    fxwrite (&M[pa], sizeof (int32), wc, uptr->fileref);
    err = ferror (uptr->fileref);
    if ((err == 0) && (i = (wc & (RP_NUMWD - 1)))) {
        fxwrite (fill, sizeof (int32), i, uptr->fileref);
        err = ferror (uptr->fileref);
        }
    }

if ((f == FN_WRCHK) && (err == 0)) {                    /* write check? */
    for (i = 0; (err == 0) && (i < wc); i++)  {
        awc = fxread (&comp, sizeof (int32), 1, uptr->fileref);
        if (awc == 0)
            comp = 0;
        if (comp != M[pa + i])
            rp_updsta (0, STB_WCE);
        }
    err = ferror (uptr->fileref);
    }

rp_wc = (rp_wc + wc) & DMASK;                           /* final word count */
rp_ma = (rp_ma + wc) & DMASK;                           /* final mem addr */
da = (da + wc + (RP_NUMWD - 1)) / RP_NUMWD;             /* final sector num */
cyl = da / (RP_NUMSC * RP_NUMSF);                       /* get cyl */
if (cyl >= RP_QCYL (uptr->flags))                       /* cyl ovflo wraps */
    cyl = 0;
surf = (da % (RP_NUMSC * RP_NUMSF)) / RP_NUMSC;         /* get surface */
sect = (da % (RP_NUMSC * RP_NUMSF)) % RP_NUMSC;         /* get sector */
rp_da = ((cyl & DA_M_CYL) << DA_V_CYL) | (surf << DA_V_SURF) | (sect << DA_V_SECT);
if (cyl >= 256)                                         /* cyl >= 8 bits? */
    rp_da = rp_da | DA_C256;
rp_busy = 0;                                            /* clear busy */
rp_updsta (STA_DON, 0);                                 /* set done */

if (err != 0) {                                         /* error? */
    sim_perror ("RP I/O error");
    clearerr (uptr->fileref);
    return IORETURN (rp_stopioe, SCPE_IOERR);
    }
return SCPE_OK;
}

/* Update status */

void rp_updsta (int32 newa, int32 newb)
{
int32 f;
UNIT *uptr;

uptr = rp_dev.units + GET_UNIT (rp_sta);
rp_sta = (rp_sta & ~(STA_DYN | STA_ERR)) | newa;
rp_stb = (rp_stb & ~STB_DYN) | newb;
if (uptr->flags & UNIT_WPRT)
    rp_sta = rp_sta | STA_SUWP;
if ((uptr->flags & UNIT_ATT) == 0)
    rp_stb = rp_stb | STB_SUFU | STB_SUNR;
else if (sim_is_active (uptr)) {
    f = (uptr->FUNC) & STA_M_FUNC;
    if ((f == FN_SEEK) || (f == FN_RECAL))
        rp_stb = rp_stb | STB_SUSU | STB_SUNR;
    }
else if (uptr->CYL >= RP_QCYL (uptr->flags))
    rp_sta = rp_sta | STA_SUSI;
if ((rp_sta & STA_EFLGS) || (rp_stb & STB_EFLGS))
    rp_sta = rp_sta | STA_ERR;
if (((rp_sta & (STA_ERR | STA_DON)) && (rp_sta & STA_IED)) ||
    ((rp_stb & STB_ATTN) && (rp_sta & STA_IEA)))
    SET_INT (RP);
else CLR_INT (RP);
return;
}

/* Reset routine */

t_stat rp_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

rp_sta = rp_stb = rp_da = rp_wc = rp_ma = rp_busy = 0;
CLR_INT (RP);
for (i = 0; i < RP_NUMDR; i++) {
    uptr = rp_dev.units + i;
    sim_cancel (uptr);
    uptr->CYL = uptr->FUNC = 0;
    }
return SCPE_OK;
}

/* IORS routine */

int32 rp_iors (void)
{
return ((rp_sta & (STA_ERR | STA_DON)) ||  (rp_stb & STB_ATTN))? IOS_RP: 0;
}

/* Attach unit */

t_stat rp_attach (UNIT *uptr, CONST char *cptr)
{
t_stat reason;

uptr->capac = RP_QSIZE (uptr->flags);
reason = attach_unit (uptr, cptr);
rp_updsta (0, 0);
return reason;
}

/* Detach unit */

t_stat rp_detach (UNIT *uptr)
{
t_stat reason;

reason = detach_unit (uptr);
rp_updsta (0, 0);
return reason;
}

/* Set size routine */

t_stat rp_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = RP_QSIZE (val);
return SCPE_OK;
}
