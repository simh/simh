/* sds_drm.c: SDS 940 Project Genie drum simulator

   Copyright (c) 2002-2013, Robert M. Supnik

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

   drm          drum

   03-Sep-13    RMS     Added explicit void * cast

   The drum is buffered in memory.

   Note: the Project Genie documentation and the actual monitor sources disagree
   on the I/O instruction definitions for the drum.  The simulator follows the
   monitor sources, as follows:

   DCC    OP      00230404B       RESET DRUM CHANNEL
   DSC    OP      00230204B       START DRUM CHANNEL (NO CHAIN)
   DRA    OP      00230504B       READ DRUM TIMING COUNTER INTO 21B
   DSR    OP      04030204B       SKIP IF DRUM NOT BUSY
   DSE    OP      04037404B       SKIP IF NO DRUM ERROR
*/

#include "sds_defs.h"
#include <math.h>

/* Constants */

#define DRM_N_WD        11                              /* word addr width */
#define DRM_V_WD        0                               /* position */
#define DRM_M_WD        ((1 << DRM_N_WD) - 1)           /* word mask */
#define DRM_NUMWD       (1 << DRM_N_WD)                 /* words/sector */
#define DRM_NUMGP       236                             /* gap/sector */
#define DRM_PHYWD       (DRM_NUMWD + DRM_NUMGP)         /* phys wds/sector */
#define DRM_N_SC        3                               /* sect addr width */
#define DRM_V_SC        (DRM_N_WD)                      /* position */
#define DRM_M_SC        ((1 << DRM_N_SC) - 1)           /* sector mask */
#define DRM_NUMSC       (1 << DRM_N_SC)                 /* sectors/track */
#define DRM_N_TR        7                               /* track addr width */
#define DRM_V_TR        (DRM_N_WD+DRM_N_SC)             /* position */
#define DRM_M_TR        ((1 << DRM_N_TR) - 1)           /* track mask */
#define DRM_NUMTR       84                              /* tracks/drum */
#define DRM_N_ADDR      (DRM_N_WD+DRM_N_SC+DRM_N_TR)    /* drum addr width */
#define DRM_SWMASK      ((1 << (DRM_N_WD+DRM_N_SC)) - 1)/* sector+word mask */
#define DRM_DAMASK      ((1 << DRM_N_ADDR) - 1)         /* drum addr mask */
#define DRM_SIZE        (DRM_NUMTR*DRM_NUMSC*DRM_NUMWD) /* words/disk */
#define DRM_WCMASK      037777                          /* wc mask */
#define DRM_GETSC(x)    (((x) >> DRM_V_SC) & DRM_M_SC)

#define DRM_PC          020
#define DRM_AD          021
#define DRM_ADAT        (1 << (DRM_N_WD + DRM_N_SC))    /* data flag */

#define DRM_SFET        0                               /* fetch state */
#define DRM_SFCA        1                               /* fetch CA */
#define DRM_SFDA        2                               /* fetch DA */
#define DRM_SXFR        3                               /* xfer */

#define DRM_V_OP        21                              /* drum op */
#define DRM_M_OP        07
#define DRM_V_RW        20
#define DRM_GETOP(x)    (((x) >> DRM_V_OP) & DRM_M_OP)
#define DRM_GETRW(x)    (((x) >> DRM_V_RW) & 1)
#define  DRM_OXF        0                               /* xfer */
#define  DRM_OCX        1                               /* cond xfer */
#define  DRM_OBR        2                               /* branch */
#define  DRM_ORS        3                               /* reset error */
#define  DRM_END        4                               /* end prog */
#define  DRM_EIE        5                               /* end int if err */
#define  DRM_EIU        7                               /* end int uncond */

#define GET_TWORD(x)    ((int32) fmod (sim_gtime() / ((double) (x)), \
                        ((double) (DRM_NUMSC * DRM_PHYWD))))

extern uint32 M[];                                      /* memory */
extern uint32 alert, int_req;
extern int32 stop_invins, stop_invdev, stop_inviop;
uint32 drm_da = 0;                                      /* disk address */
uint32 drm_ca = 0;                                      /* core address */
uint32 drm_wc = 0;                                      /* word count */
int32 drm_par = 0;                                      /* cumulative par */
int32 drm_err = 0;                                      /* error */
int32 drm_rw = 0;                                       /* read/write */
int32 drm_sta = 0;                                      /* drum state */
int32 drm_ftime = 3;                                    /* time to fetch */
int32 drm_xtime = 1;                                    /* time to xfr */
int32 drm_stopioe = 1;                                  /* stop on error */

t_stat drm (uint32 fnc, uint32 inst, uint32 *dat);
t_stat drm_svc (UNIT *uptr);
t_stat drm_reset (DEVICE *dptr);

/* DRM data structures

   drm_dev      device descriptor
   drm_unit     unit descriptor
   drm_reg      register list
*/

DIB drm_dib = { -1, DEV3_GDRM, 0, NULL, &drm };

UNIT drm_unit = {
    UDATA (&drm_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF,
           DRM_SIZE)
    };

REG drm_reg[] = {
    { ORDATA (DA, drm_da, DRM_N_ADDR) },
    { ORDATA (CA, drm_ca, 16) },
    { ORDATA (WC, drm_wc, 14) },
    { ORDATA (PAR, drm_par, 12) },
    { FLDATA (RW, drm_rw, 0) },
    { FLDATA (ERR, drm_err, 0) },
    { ORDATA (STA, drm_sta, 2) },
    { DRDATA (FTIME, drm_ftime, 24), REG_NZ + PV_LEFT },
    { DRDATA (XTIME, drm_xtime, 24), REG_NZ + PV_LEFT },
    { FLDATA (STOP_IOE, drm_stopioe, 0) },
    { NULL }
    };

DEVICE drm_dev = {
    "DRM", &drm_unit, drm_reg, NULL,
    1, 8, DRM_N_ADDR, 1, 8, 24,
    NULL, NULL, &drm_reset,
    NULL, NULL, NULL,
    &drm_dib, DEV_DISABLE | DEV_DIS
    };

/* Drum routine -  EOM/SKS 3xx04 */

t_stat drm (uint32 fnc, uint32 inst, uint32 *dat)
{
int32 t, op = inst & 07700;

switch (fnc) {

    case IO_CONN:                                       /* connect */
        if (op == 00400)                                /* EOM 404 = reset */
            return drm_reset (&drm_dev);
        if (op == 00500) {                              /* EOM 504 = read DA */
            if (sim_is_active (&drm_unit))
                return SCPE_OK; /* must be idle */
            t = GET_TWORD (drm_xtime);                  /* get position */
            if (t < DRM_NUMGP)                          /* in gap? */
                M[DRM_AD] = DRM_NUMWD - t;
            else M[DRM_AD] = (t - DRM_NUMGP) | DRM_ADAT;/* in data */
            }
        else if (op == 00200) {                         /* EOM 204 = start */
            if (sim_is_active (&drm_unit))              /* must be idle */
                return SCPE_OK;
            drm_sta = DRM_SFET;                         /* state = fetch */
            sim_activate (&drm_unit, drm_ftime);        /* activate */
            }
        else CRETINS;
        break;

    case IO_SKS:                                        /* SKS */
        if (((op == 07400) && !drm_err) ||              /* 37404: no err */
            ((op == 00200) && !sim_is_active (&drm_unit))) /* 30204: idle */
            *dat = 1;
        break;

    default:
        return SCPE_IERR;
        }

return SCPE_OK;
}

/* Unit service */

t_stat drm_svc (UNIT *uptr)
{
int32 t, rda;
uint32 dpc, dwd;
uint32 *fbuf = (uint32 *) uptr->filebuf;

if (drm_sta != DRM_SXFR) {                              /* fetch drum prog? */
    dpc = M[DRM_PC];                                    /* get drum PC */
    dwd = M[dpc & PAMASK];                              /* get drum inst */
    M[DRM_PC] = (dpc + 1) & PAMASK;                     /* update drum PC */
    if (drm_sta == DRM_SFCA) {                          /* fetch core addr? */
        drm_rw = DRM_GETRW (dwd);                       /* set op */
        drm_ca = dwd & PAMASK;                          /* set core addr */
        drm_sta = DRM_SFDA;                             /* next is disk addr */
        }
    else if (drm_sta == DRM_SFDA) {                     /* fetch disk addr? */
        drm_da = dwd & DRM_DAMASK;                      /* set disk addr */
        drm_sta = DRM_SXFR;                             /* next is xfer */
        drm_par = 0;                                    /* init parity */
        rda = (drm_da & DRM_SWMASK) + (DRM_GETSC (drm_da) * DRM_NUMGP);
        t = rda - GET_TWORD (drm_xtime);                /* difference */
        if (t <= 0)                                     /* add trk lnt */
            t = t + (DRM_NUMSC * DRM_PHYWD);
        sim_activate (&drm_unit, t * drm_xtime);        /* activate */
        }
    else {
        switch (DRM_GETOP (dwd)) {

        case DRM_OCX:                                   /* cond xfr */
            if (drm_err) {                              /* error? */
                int_req = int_req | INT_DRM;            /* req int */
                return SCPE_OK;                         /* done */
                }
        case DRM_OXF:                                   /* transfer */
            drm_wc = dwd & DRM_WCMASK;                  /* save wc */
            drm_sta = DRM_SFCA;                         /* next state */
            break;

        case DRM_OBR:                                   /* branch */
            M[DRM_PC] = dwd & PAMASK;                   /* new drum PC */
            break;

        case DRM_END:                                   /* end */
            return SCPE_OK;

        case DRM_EIE:                                   /* end, int if err */
            if (!drm_err)
                return SCPE_OK;

        case DRM_EIU:                                   /* end, int uncond */
            int_req = int_req | INT_DRM;
            return SCPE_OK;
            }                                           /* end switch */
        }                                               /* end else sta */
    sim_activate (uptr, drm_ftime);                     /* fetch next word */
    }                                                   /* end if !xfr */
else {                                                  /* transfer word */
    if ((uptr->flags & UNIT_BUF) == 0) {                /* not buffered? */
        drm_err = 1;                                    /* error */
        CRETIOE (drm_stopioe, SCPE_UNATT);
        }
    if (drm_rw) {                                       /* write? */
        dwd = M[drm_ca];                                /* get mem word */
        fbuf[drm_da] = dwd;                             /* write to drum */
        if (drm_da >= uptr->hwmark)
            uptr->hwmark = drm_da + 1;
        }
    else {                                              /* read */
        dwd = fbuf[drm_da];                             /* get drum word */
        M[drm_ca] = dwd;                                /* write to mem */
        }
    drm_da = drm_da + 1;                                /* inc drum addr */
    if (drm_da >= DRM_SIZE)                             /* wrap */
        drm_da = 0;
    drm_ca = (drm_ca + 1) & PAMASK;                     /* inc core addr */
    drm_wc = (drm_wc - 1) & DRM_WCMASK;                 /* dec word cnt */
    drm_par = drm_par ^ (dwd >> 12);                    /* parity */
    drm_par = ((drm_par << 1) | (drm_par >> 11)) & 07777;
    drm_par = drm_par ^ (dwd & 07777);
    if (drm_wc) {                                       /* more to do */
        if (drm_da & DRM_M_WD)
            sim_activate (uptr, drm_xtime);
        else sim_activate (uptr, drm_xtime * DRM_NUMGP);
        }
    else {                                              /* end xfr */
#if defined (DRM_PAR)
        if ((drm_da & DRM_M_WD) && drm_rw) {            /* wr end mid sector? */
            M[drm_da] = drm_par << 12;                  /* clobber data */
            if (drm_da >= uptr->hwmark)
                uptr->hwmark = drm_da + 1;
            }
#endif
        drm_sta = DRM_SFET;                             /* back to fetch */
        sim_activate (uptr, drm_ftime);                 /* schedule */
        }                                               /* end else end xfr */
    }                                                   /* end else xfr */
return SCPE_OK;
}

/* Reset routine */

t_stat drm_reset (DEVICE *dptr)
{
drm_da = 0;                                             /* clear state */
drm_ca = 0;
drm_wc = 0;
drm_par = 0;
drm_sta = 0;
drm_err = 0;
drm_rw = 0;
int_req = int_req & ~INT_DRM;                           /* clear intr */
sim_cancel (&drm_unit);                                 /* deactivate */
return SCPE_OK;
}
