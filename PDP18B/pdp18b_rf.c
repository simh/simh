/* pdp18b_rf.c: fixed head disk simulator

   Copyright (c) 1993-2013, Robert M Supnik

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

   rf           (PDP-9) RF09/RF09
                (PDP-15) RF15/RS09

   03-Sep-13    RMS     Added explicit void * cast
   04-Oct-06    RMS     Fixed bug, DSCD does not clear function register
   15-May-06    RMS     Fixed bug in autosize attach (David Gesswein)
   14-Jan-04    RMS     Revised IO device call interface
                        Changed sim_fsize calling sequence
   26-Oct-03    RMS     Cleaned up buffer copy code
   26-Jul-03    RMS     Fixed bug in set size routine
   14-Mar-03    RMS     Fixed variable platter interaction with save/restore
   03-Mar-03    RMS     Fixed autosizing
   12-Feb-03    RMS     Removed 8 platter sizing hack
   05-Feb-03    RMS     Fixed decode bugs, added variable and autosizing
   05-Oct-02    RMS     Added DIB, dev number support
   06-Jan-02    RMS     Revised enable/disable support
   25-Nov-01    RMS     Revised interrupt structure
   24-Nov-01    RMS     Changed WLK to array
   26-Apr-01    RMS     Added device enable/disable support
   15-Feb-01    RMS     Fixed 3 cycle data break sequencing
   30-Nov-99    RMS     Added non-zero requirement to rf_time
   14-Apr-99    RMS     Changed t_addr to unsigned

   The RFxx is a head-per-track disk.  It uses the multicycle data break
   facility.  To minimize overhead, the entire RFxx is buffered in memory.

   Two timing parameters are provided:

   rf_time      Interword timing.  Must be non-zero.
   rf_burst     Burst mode.  If 0, DMA occurs cycle by cycle; otherwise,
                DMA occurs in a burst.
*/

#include "pdp18b_defs.h"
#include <math.h>

#define UNIT_V_AUTO     (UNIT_V_UF + 0)                 /* autosize */
#define UNIT_V_PLAT     (UNIT_V_UF + 1)                 /* #platters - 1 */
#define UNIT_M_PLAT     07
#define UNIT_PLAT       (UNIT_M_PLAT << UNIT_V_PLAT)
#define UNIT_GETP(x)    ((((x) >> UNIT_V_PLAT) & UNIT_M_PLAT) + 1)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define UNIT_PLAT       (UNIT_M_PLAT << UNIT_V_PLAT)

/* Constants */

#define RF_NUMWD        2048                            /* words/track */
#define RF_NUMTR        128                             /* tracks/disk */
#define RF_DKSIZE       (RF_NUMTR * RF_NUMWD)           /* words/disk */
#define RF_NUMDK        8                               /* disks/controller */
#define RF_WMASK        (RF_NUMWD - 1)                  /* word mask */
#define RF_WC           036                             /* word count */
#define RF_CA           037                             /* current addr */

/* Function/status register */

#define RFS_ERR         0400000                         /* error */
#define RFS_HDW         0200000                         /* hardware error */
#define RFS_APE         0100000                         /* addr parity error */
#define RFS_MXF         0040000                         /* missed transfer */
#define RFS_WCE         0020000                         /* write check error */
#define RFS_DPE         0010000                         /* data parity error */
#define RFS_WLO         0004000                         /* write lock error */
#define RFS_NED         0002000                         /* non-existent disk */
#define RFS_DCH         0001000                         /* data chan timing */
#define RFS_PGE         0000400                         /* programming error */
#define RFS_DON         0000200                         /* transfer complete */
#define RFS_V_FNC       1                               /* function */
#define RFS_M_FNC       03
#define RFS_FNC         (RFS_M_FNC << RFS_V_FNC)
#define  FN_NOP         0
#define  FN_READ        1
#define  FN_WRITE       2
#define  FN_WCHK        3
#define RFS_IE          0000001                         /* interrupt enable */

#define RFS_CLR         0000170                         /* always clear */
#define RFS_EFLGS       (RFS_HDW | RFS_APE | RFS_MXF | RFS_WCE | \
						 RFS_DPE | RFS_WLO | RFS_NED )  /* error flags */
#define RFS_FR          (RFS_FNC|RFS_IE)
#define GET_FNC(x)      (((x) >> RFS_V_FNC) & RFS_M_FNC)
#define GET_POS(x)      ((int) fmod (sim_gtime () / ((double) (x)), \
						((double) RF_NUMWD)))
#define RF_BUSY         (sim_is_active (&rf_unit))

extern int32 M[];
extern int32 int_hwre[API_HLVL+1];
extern UNIT cpu_unit;

int32 rf_sta = 0;                                       /* status register */
int32 rf_da = 0;                                        /* disk address */
int32 rf_dbuf = 0;                                      /* data buffer */
int32 rf_wlk[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };           /* write lock */
int32 rf_time = 10;                                     /* inter-word time */
int32 rf_burst = 1;                                     /* burst mode flag */
int32 rf_stopioe = 1;                                   /* stop on error */

DEVICE rf_dev;
int32 rf70 (int32 dev, int32 pulse, int32 dat);
int32 rf72 (int32 dev, int32 pulse, int32 dat);
int32 rf_iors (void);
t_stat rf_svc (UNIT *uptr);
t_stat rf_reset (DEVICE *dptr);
int32 rf_updsta (int32 new);
t_stat rf_attach (UNIT *uptr, char *cptr);
t_stat rf_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);

/* RF data structures

   rf_dev       RF device descriptor
   rf_unit      RF unit descriptor
   rf_reg       RF register list
*/

DIB rf_dib = { DEV_RF, 3, &rf_iors, { &rf70, NULL, &rf72 } };

UNIT rf_unit = {
    UDATA (&rf_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_AUTO,
           RF_DKSIZE)
    };

REG rf_reg[] = {
    { ORDATA (STA, rf_sta, 18) },
    { ORDATA (DA, rf_da, 22) },
    { ORDATA (WC, M[RF_WC], 18) },
    { ORDATA (CA, M[RF_CA], 18) },
    { ORDATA (BUF, rf_dbuf, 18) },
    { FLDATA (INT, int_hwre[API_RF], INT_V_RF) },
    { BRDATA (WLK, rf_wlk, 8, 16, RF_NUMDK) },
    { DRDATA (TIME, rf_time, 24), PV_LEFT + REG_NZ },
    { FLDATA (BURST, rf_burst, 0) },
    { FLDATA (STOP_IOE, rf_stopioe, 0) },
    { DRDATA (CAPAC, rf_unit.capac, 31), PV_LEFT + REG_HRO },
    { ORDATA (DEVNO, rf_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB rf_mod[] = {
    { UNIT_PLAT, (0 << UNIT_V_PLAT), NULL, "1P", &rf_set_size },
    { UNIT_PLAT, (1 << UNIT_V_PLAT), NULL, "2P", &rf_set_size },
    { UNIT_PLAT, (2 << UNIT_V_PLAT), NULL, "3P", &rf_set_size },
    { UNIT_PLAT, (3 << UNIT_V_PLAT), NULL, "4P", &rf_set_size },
    { UNIT_PLAT, (4 << UNIT_V_PLAT), NULL, "5P", &rf_set_size },
    { UNIT_PLAT, (5 << UNIT_V_PLAT), NULL, "6P", &rf_set_size },
    { UNIT_PLAT, (6 << UNIT_V_PLAT), NULL, "7P", &rf_set_size },
    { UNIT_PLAT, (7 << UNIT_V_PLAT), NULL, "8P", &rf_set_size },
    { UNIT_AUTO, UNIT_AUTO, "autosize", "AUTOSIZE", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO", &set_devno, &show_devno },
    { 0 }
    };

DEVICE rf_dev = {
    "RF", &rf_unit, rf_reg, rf_mod,
    1, 8, 21, 1, 8, 18,
    NULL, NULL, &rf_reset,
    NULL, &rf_attach, NULL,
    &rf_dib, DEV_DISABLE
    };

/* IOT routines */

int32 rf70 (int32 dev, int32 pulse, int32 dat)
{
int32 t, sb;

sb = pulse & 060;                                       /* subopcode */
if (pulse & 01) {
    if ((sb == 000) && (rf_sta & (RFS_ERR | RFS_DON)))  /* DSSF */
        dat = IOT_SKP | dat;
    else if (sb == 020)                                 /* DSCC */
        rf_reset (&rf_dev);
    else if (sb == 040) {                               /* DSCF */
        if (RF_BUSY)                                    /* busy inhibits */
            rf_sta = rf_sta | RFS_PGE;
        else rf_sta = rf_sta & ~(RFS_FNC | RFS_IE);     /* clear func */
        }
    }
if (pulse & 02) {
    if (RF_BUSY)                                        /* busy sets PGE */
        rf_sta = rf_sta | RFS_PGE;
    else if (sb == 000)                                 /* DRBR */
        dat = dat | rf_dbuf;
    else if (sb == 020)                                 /* DRAL */
        dat = dat | (rf_da & DMASK);
    else if (sb == 040)                                 /* DSFX */
        rf_sta = rf_sta ^ (dat & (RFS_FNC | RFS_IE));   /* xor func */
    else if (sb == 060)                                 /* DRAH */
        dat = dat | (rf_da >> 18) | ((rf_sta & RFS_NED)? 010: 0);
    }
if (pulse & 04) {
    if (RF_BUSY)                                        /* busy sets PGE */
        rf_sta = rf_sta | RFS_PGE;
    else if (sb == 000)                                 /* DLBR */
        rf_dbuf = dat & DMASK;
    else if (sb == 020)                                 /* DLAL */
        rf_da = (rf_da & ~DMASK) | (dat & DMASK);
    else if (sb == 040) {                               /* DSCN */
        rf_sta = rf_sta & ~RFS_DON;                     /* clear done */
        if (GET_FNC (rf_sta) != FN_NOP) {
            t = (rf_da & RF_WMASK) - GET_POS (rf_time); /* delta to new */
            if (t < 0)                                  /* wrap around? */
                t = t + RF_NUMWD;
            sim_activate (&rf_unit, t * rf_time);       /* schedule op */
            }
        }
    else if (sb == 060) {                               /* DLAH */
        rf_da = (rf_da & DMASK) | ((dat & 07) << 18);
        if ((uint32) rf_da >= rf_unit.capac)            /* for sizing */
            rf_updsta (RFS_NED);
        }
    }
rf_updsta (0);                                          /* update status */
return dat;
}

int32 rf72 (int32 dev, int32 pulse, int32 dat)
{
int32 sb = pulse & 060;

if (pulse & 02) {
    if (sb == 000)                                      /* DLOK */
        dat = dat | GET_POS (rf_time) |
            (sim_is_active (&rf_unit)? 0400000: 0);
    else if (sb == 040) {                               /* DSCD */
        if (RF_BUSY)                                    /* busy inhibits */
            rf_sta = rf_sta | RFS_PGE;
        else rf_sta = rf_sta & RFS_FR;
        rf_updsta (0);
        }
    else if (sb == 060) {                               /* DSRS */
        if (RF_BUSY)                                    /* busy sets PGE */
            rf_sta = rf_sta | RFS_PGE;
        dat = dat | rf_updsta (0);
        }
    }
return dat;
}

/* Unit service - assumes the entire disk is buffered */

t_stat rf_svc (UNIT *uptr)
{
int32 f, pa, d, t;
int32 *fbuf = (int32 *) uptr->filebuf;

if ((uptr->flags & UNIT_BUF) == 0) {                    /* not buf? abort */
    rf_updsta (RFS_NED | RFS_DON);                      /* set nxd, done */
    return IORETURN (rf_stopioe, SCPE_UNATT);
    }

f = GET_FNC (rf_sta);                                   /* get function */
do {
    if ((uint32) rf_da >= uptr->capac) {                /* disk overflow? */
        rf_updsta (RFS_NED);                            /* nx disk error */
        break;
        }
    M[RF_WC] = (M[RF_WC] + 1) & DMASK;                  /* incr word count */
        pa = M[RF_CA] = (M[RF_CA] + 1) & AMASK;         /* incr mem addr */
    if ((f == FN_READ) && MEM_ADDR_OK (pa))             /* read? */
        M[pa] = fbuf[rf_da];
    if ((f == FN_WCHK) && (M[pa] != fbuf[rf_da])) {     /* write check? */
        rf_updsta (RFS_WCE);                            /* flag error */
        break;
        }
    if (f == FN_WRITE) {                                /* write? */
        d = (rf_da >> 18) & 07;                         /* disk */
        t = (rf_da >> 14) & 017;                        /* track groups */
        if ((rf_wlk[d] >> t) & 1) {                     /* write locked? */
            rf_updsta (RFS_WLO);
            break;
            }
        else {                                          /* not locked */
            fbuf[rf_da] = M[pa];						/* write word */
            if (((uint32) rf_da) >= uptr->hwmark)
                uptr->hwmark = rf_da + 1;
            }
        }
    rf_da = rf_da + 1;                                  /* incr disk addr */
    } while ((M[RF_WC] != 0) && (rf_burst != 0));       /* brk if wc, no brst */

if ((M[RF_WC] != 0) && ((rf_sta & RFS_ERR) == 0))       /* more to do? */
    sim_activate (&rf_unit, rf_time);                   /* sched next */
else rf_updsta (RFS_DON);
return SCPE_OK;
}

/* Update status */

int32 rf_updsta (int32 new)
{
rf_sta = (rf_sta | new) & ~(RFS_ERR | RFS_CLR);
if (rf_sta & RFS_EFLGS)
    rf_sta = rf_sta | RFS_ERR;
if ((rf_sta & (RFS_ERR | RFS_DON)) && (rf_sta & RFS_IE))
     SET_INT (RF);
else CLR_INT (RF);
return rf_sta;
}

/* Reset routine */

t_stat rf_reset (DEVICE *dptr)
{
rf_sta = rf_da = rf_dbuf = 0;
rf_updsta (0);
sim_cancel (&rf_unit);
return SCPE_OK;
}

/* IORS routine */

int32 rf_iors (void)
{
return ((rf_sta & (RFS_ERR | RFS_DON))? IOS_RF: 0);
}

/* Attach routine */

t_stat rf_attach (UNIT *uptr, char *cptr)
{
uint32 p, sz;
uint32 ds_bytes = RF_DKSIZE * sizeof (int32);

if ((uptr->flags & UNIT_AUTO) && (sz = sim_fsize_name (cptr))) {
    p = (sz + ds_bytes - 1) / ds_bytes;
    if (p >= RF_NUMDK)
        p = RF_NUMDK - 1;
    uptr->flags = (uptr->flags & ~UNIT_PLAT) |
        (p << UNIT_V_PLAT);
    }
uptr->capac = UNIT_GETP (uptr->flags) * RF_DKSIZE;
return attach_unit (uptr, cptr);
}

/* Change disk size */

t_stat rf_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (val < 0)
    return SCPE_IERR;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = UNIT_GETP (val) * RF_DKSIZE;
uptr->flags = uptr->flags & ~UNIT_AUTO;
return SCPE_OK;
}
