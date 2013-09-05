/* pdp1_drm.c: PDP-1 drum simulator

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

   drp          Type 23 parallel drum
   drm          Type 24 serial drum

   03-Sep-13    RMS     Added explicit void * cast
   21-Dec-06    RMS     Added 16-chan SBS support
   08-Dec-03    RMS     Added parallel drum support
                        Fixed bug in DBL/DCN decoding
   26-Oct-03    RMS     Cleaned up buffer copy code
   23-Jul-03    RMS     Fixed incorrect logical, missing activate
   05-Dec-02    RMS     Cloned from pdp18b_drm.c
*/

#include "pdp1_defs.h"
#include <math.h>

/* Serial drum constants */

#define DRM_NUMWDS      256                             /* words/sector */
#define DRM_NUMSC       2                               /* sectors/track */
#define DRM_NUMTR       256                             /* tracks/drum */
#define DRM_NUMWDT      (DRM_NUMWDS * DRM_NUMSC)        /* words/track */
#define DRM_SIZE        (DRM_NUMTR * DRM_NUMWDT)        /* words/drum */
#define DRM_SMASK       ((DRM_NUMTR * DRM_NUMSC) - 1)   /* sector mask */

/* Parallel drum constants */

#define DRP_NUMWDT      4096                            /* words/track */
#define DRP_NUMTK       32                              /* tracks/drum */
#define DRP_SIZE        (DRP_NUMWDT * DRP_NUMTK)        /* words/drum */
#define DRP_V_RWE       17                              /* read/write enable */
#define DRP_V_FLD       12                              /* drum field */
#define DRP_M_FLD       037
#define DRP_TAMASK      07777                           /* track address */
#define DRP_WCMASK      07777                           /* word count */
#define DRP_MAINCM      07777                           /* mem addr incr */
#define DRP_GETRWE(x)   (((x) >> DRP_V_RWE) & 1)
#define DRP_GETRWF(x)   (((x) >> DRP_V_FLD) & DRP_M_FLD)

/* Parameters in the unit descriptor */

#define FUNC            u4                              /* function */
#define DRM_READ        000                             /* read */
#define DRM_WRITE       010                             /* write */
#define DRP_RW          000                             /* read/write */
#define DRP_BRK         001                             /* break on address */

#define GET_POS(x)      ((int) fmod (sim_gtime() / ((double) (x)), \
                        ((double) DRM_NUMWDT)))

extern int32 M[];
extern int32 iosta;
extern int32 stop_inst;
extern UNIT cpu_unit;

/* Serial drum variables */

uint32 drm_da = 0;                                      /* track address */
uint32 drm_ma = 0;                                      /* memory address */
uint32 drm_err = 0;                                     /* error flag */
uint32 drm_wlk = 0;                                     /* write lock */
int32 drm_time = 4;                                     /* inter-word time */
int32 drm_sbs = 0;                                      /* SBS level */
int32 drm_stopioe = 1;                                  /* stop on error */

/* Parallel drum variables */

uint32 drp_rde = 0;                                     /* read enable */
uint32 drp_wre = 0;                                     /* write enable */
uint32 drp_rdf = 0;                                     /* read field */
uint32 drp_wrf = 0;                                     /* write field */
uint32 drp_ta = 0;                                      /* track address */
uint32 drp_wc = 0;                                      /* word count */
uint32 drp_ma = 0;                                      /* memory address */
uint32 drp_err = 0;                                     /* error */
int32 drp_time = 2;                                     /* inter-word time */
int32 drp_stopioe = 1;                                  /* stop on error */

/* Forward declarations */

t_stat drm_svc (UNIT *uptr);
t_stat drm_reset (DEVICE *dptr);
t_stat drp_svc (UNIT *uptr);
t_stat drp_reset (DEVICE *dptr);

/* DRM data structures

   drm_dev      DRM device descriptor
   drm_unit     DRM unit descriptor
   drm_reg      DRM register list
*/

UNIT drm_unit = {
    UDATA (&drm_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF,
           DRM_SIZE)
    };

REG drm_reg[] = {
    { ORDATA (DA, drm_da, 9) },
    { ORDATA (MA, drm_ma, 16) },
    { FLDATA (DONE, iosta, IOS_V_DRM) },
    { FLDATA (ERR, drm_err, 0) },
    { ORDATA (WLK, drm_wlk, 32) },
    { DRDATA (TIME, drm_time, 24), REG_NZ + PV_LEFT },
    { DRDATA (SBSLVL, drm_sbs, 4), REG_HRO },
    { FLDATA (STOP_IOE, drm_stopioe, 0) },
    { NULL }
    };

MTAB drm_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "APILVL", "APILVL",
      &dev_set_sbs, &dev_show_sbs, (void *) &drm_sbs },
    { 0 }
    };

DEVICE drm_dev = {
    "DRM", &drm_unit, drm_reg, drm_mod,
    1, 8, 20, 1, 8, 18,
    NULL, NULL, &drm_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE
    };

/* DRP data structures

   drp_dev      DRP device descriptor
   drp_unit     DRP unit descriptor
   drp_reg      DRP register list
*/

UNIT drp_unit = {
    UDATA (&drp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF,
           DRM_SIZE)
    };

REG drp_reg[] = {
    { ORDATA (TA, drp_ta, 12) },
    { ORDATA (RDF, drp_rdf, 5) },
    { FLDATA (RDE, drp_rde, 0) },
    { FLDATA (WRF, drp_wrf, 5) },
    { FLDATA (WRE, drp_wre, 0) },
    { ORDATA (MA, drp_ma, 16) },
    { ORDATA (WC, drp_wc, 12) },
    { FLDATA (BUSY, iosta, IOS_V_DRP) },
    { FLDATA (ERR, drp_err, 0) },
    { DRDATA (TIME, drp_time, 24), REG_NZ + PV_LEFT },
    { FLDATA (STOP_IOE, drp_stopioe, 0) },
    { DRDATA (SBSLVL, drm_sbs, 4), REG_HRO },
    { NULL }
    };

DEVICE drp_dev = {
    "DRP", &drp_unit, drp_reg, NULL,
    1, 8, 20, 1, 8, 18,
    NULL, NULL, &drp_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DIS
    };

/* IOT routines */

int32 drm (int32 IR, int32 dev, int32 dat)
{
int32 t;
int32 pulse = (IR >> 6) & 037;

if ((drm_dev.flags & DEV_DIS) == 0) {                   /* serial enabled? */
    if ((pulse != 001) && (pulse != 011))               /* invalid pulse? */
        return (stop_inst << IOT_V_REASON) | dat;       /* stop if requested */
    switch (dev) {                                      /* switch on device */

        case 061:                                       /* DWR, DRD */
            drm_ma = dat & AMASK;                       /* load mem addr */
            drm_unit.FUNC = pulse & DRM_WRITE;          /* save function */
            break;

        case 062:                                       /* DBL, DCN */
            if ((pulse & 010) == 0)                     /* DBL? */
                drm_da = dat & DRM_SMASK;               /* load sector # */
            iosta = iosta & ~IOS_DRM;                   /* clear flags */
            drm_err = 0;
            t = ((drm_da % DRM_NUMSC) * DRM_NUMWDS) - GET_POS (drm_time);
            if (t <= 0)                                 /* wrap around? */
                t = t + DRM_NUMWDT;
            sim_activate (&drm_unit, t);                /* start operation */
            break;

        case 063:                                       /* DTD */
            if (pulse == 011)
                return (stop_inst << IOT_V_REASON) | dat;
            if (iosta & IOS_DRM)                        /* skip if done */
                return (dat | IOT_SKP);
            break;

        case 064:                                       /* DSE, DSP */
            if ((drm_err == 0) || (pulse & 010))        /* no error, par test? */
                return (dat | IOT_SKP);
            }                                           /* end case */

    return dat;
    }                                                   /* end if serial */

if ((drp_dev.flags & DEV_DIS) == 0) {                   /* parallel enabled? */
    switch (dev) {                                      /* switch on device */

        case 061:                                       /* DIA, DBA */
            drp_err = 0;                                /* clear error */
            iosta = iosta & ~IOS_DRP;                   /* not busy */
            drp_rde = DRP_GETRWE (dat);                 /* set read enable */
            drp_rdf = DRP_GETRWF (dat);                 /* set read field */
            drp_ta = dat & DRP_TAMASK;                  /* set track addr */
            if (IR & 02000) {                           /* DBA? */
                t = drp_ta - GET_POS (drp_time);        /* delta words */
                if (t <= 0)                             /* wrap around? */
                    t = t + DRP_NUMWDT;
                sim_activate (&drp_unit, t);            /* start operation */
                drp_unit.FUNC = DRP_BRK;                /* mark as break */
                }
            else drp_unit.FUNC = DRP_RW;                /* no, read/write */
            break;

        case 062:                                       /* DWC, DRA */
            if (IR & 02000) dat = GET_POS (drp_time) |  /* DRA, get position */
                (drp_err? 0400000: 0);
            else {                                      /* DWC */
                drp_wre = DRP_GETRWE (dat);             /* set write enable */
                drp_wrf = DRP_GETRWF (dat);             /* set write field */
                drp_wc = dat & DRP_WCMASK;              /* set word count */
                }
            break;

        case 063:                                       /* DCL */
            drp_ma = dat & AMASK;                       /* set mem address */
            t = drp_ta - GET_POS (drp_time);            /* delta words */
            if (t <= 0)                                 /* wrap around? */
                t = t + DRP_NUMWDT;
            sim_activate (&drp_unit, t);                /* start operation */
            iosta = iosta | IOS_DRP;                    /* set busy */
            break;

        case 064:                                       /* not assigned */
            return (stop_inst << IOT_V_REASON) | dat;   /* stop if requested */
            }                                           /* end case */

    return dat;
    }                                                   /* end if parallel */

return (stop_inst << IOT_V_REASON) | dat;               /* stop if requested */
}

/* Serial unit service - this code assumes the entire drum is buffered */

t_stat drm_svc (UNIT *uptr)
{
uint32 i, da;
uint32 *fbuf = (uint32 *) uptr->filebuf;

if ((uptr->flags & UNIT_BUF) == 0) {                    /* not buf? abort */
    drm_err = 1;                                        /* set error */
    iosta = iosta | IOS_DRM;                            /* set done */
    dev_req_int (drm_sbs);                              /* req intr */
    return IORETURN (drm_stopioe, SCPE_UNATT);
    }

da = drm_da * DRM_NUMWDS;                               /* compute dev addr */
for (i = 0; i < DRM_NUMWDS; i++, da++) {                /* do transfer */
    if (uptr->FUNC == DRM_READ) {                       /* read? */
        if (MEM_ADDR_OK (drm_ma))                       /* if !nxm */
            M[drm_ma] = fbuf[da];                       /* read word */
        }
    else {                                              /* write */
        if ((drm_wlk >> (drm_da >> 4)) & 1)
            drm_err = 1;
        else {                                          /* not locked */
            fbuf[da] = M[drm_ma];						/* write word */
            if (da >= uptr->hwmark)
                uptr->hwmark = da + 1;
            }
        }
    drm_ma = (drm_ma + 1) & AMASK;                      /* incr mem addr */
    }
drm_da = (drm_da + 1) & DRM_SMASK;                      /* incr dev addr */
iosta = iosta | IOS_DRM;                                /* set done */
dev_req_int (drm_sbs);                                  /* req intr */
return SCPE_OK;
}

/* Reset routine */

t_stat drm_reset (DEVICE *dptr)
{
if ((drm_dev.flags & DEV_DIS) == 0)
    drp_dev.flags = drp_dev.flags | DEV_DIS;
drm_da = drm_ma = drm_err = 0;
iosta = iosta & ~IOS_DRM;
sim_cancel (&drm_unit);
drm_unit.FUNC = 0;
return SCPE_OK;
}

/* Parallel unit service - this code assumes the entire drum is buffered */

t_stat drp_svc (UNIT *uptr)
{
uint32 i, lim;
uint32 *fbuf = uptr->filebuf;

if ((uptr->flags & UNIT_BUF) == 0) {                    /* not buf? abort */
    drp_err = 1;                                        /* set error */
    iosta = iosta & ~IOS_DRP;                           /* clear busy */
    if (uptr->FUNC)                                     /* req intr */
        dev_req_int (drm_sbs);
    return IORETURN (drp_stopioe, SCPE_UNATT);
    }

if (uptr->FUNC == DRP_RW) {                             /* read/write? */
    lim = drp_wc? drp_wc: DRP_TAMASK + 1;               /* eff word count */
    for (i = 0; i < lim; i++) {                         /* do transfer */
        if (drp_wre)                                    /* write enabled? */
            fbuf[(drp_wrf << DRP_V_FLD) | drp_ta] = M[drp_ma];
        if (drp_rde && MEM_ADDR_OK (drp_ma))            /* read enabled? */
            M[drp_ma] = fbuf[(drp_rdf << DRP_V_FLD) | drp_ta];
        drp_ta = (drp_ta + 1) & DRP_TAMASK;             /* incr track addr */
        drp_ma = ((drp_ma & ~DRP_MAINCM) | ((drp_ma + 1) & DRP_MAINCM));
        }                                               /* end for */
    }                                                   /* end if */
iosta = iosta & ~IOS_DRP;                               /* clear busy */
if (uptr->FUNC)                                         /* req intr */
    dev_req_int (drm_sbs);
return SCPE_OK;
}

/* Reset routine */

t_stat drp_reset (DEVICE *dptr)
{
if ((drp_dev.flags & DEV_DIS) == 0)
    drm_dev.flags = drm_dev.flags | DEV_DIS;
drp_ta = 0;
drp_rde = drp_rdf = drp_wre = drp_wrf = 0;
drp_err = 0;
drp_ma = 0;
drp_wc = 0;
iosta = iosta & ~IOS_DRP;
sim_cancel (&drp_unit);
drp_unit.FUNC = 0;
return SCPE_OK;
}
