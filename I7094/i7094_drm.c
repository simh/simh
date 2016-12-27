/* i7094_drm.c: 7289/7320A drum simulator

   Copyright (c) 2005-2011, Robert M Supnik

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

   drm          7289/7320A "fast" drum

   23-Mar-12    RMS     Corrected disk addressing and logical disk crossing
   25-Mar-11    RMS     Updated based on RPQ

   This simulator implements a subset of the functionality of the 7289, as
   required by CTSS.

   - The drum channel/controller behaves like a hybrid of the 7607 and the 7909.
     It responds to SCD (like the 7909), gets its address from the channel
     program (like the 7909), but responds to IOCD/IOCP (like the 7607) and
     sets channel flags (like the 7607).
   - The drum channel supports at least 2 drums.  The maximum is 4 or less.
     Physical drums are numbered from 0.
   - Each drum has a capacity of 192K 36b words.  This is divided into 6
     "logical" drum of 32KW each.  Each "logical" drum has 16 2048W "sectors".
     Logical drums are numbered from 1.
   - The drum allows transfers across sector boundaries, but not logical
     drum boundaries.
   - The drum's only supports IOCD, IOCP, and IOCT.  IOCT (and chaining mode)
     are not used by CTSS.

   Limitations in this simulator:

   - Chain mode is not implemented.
   - LPCR is not implemented.

   For speed, the entire drum is buffered in memory.
*/

#include "i7094_defs.h"
#include <math.h>

#define DRM_NUMDR       4                               /* drums/controller */

/* Drum geometry */

#define DRM_NUMWDG      1024                            /* words/group */
#define DRM_GPMASK      (DRM_NUMWDG - 1)                /* group mask */
#define DRM_NUMWDS      2048                            /* words/sector */
#define DRM_SCMASK      (DRM_NUMWDS - 1)                /* sector mask */
#define DRM_NUMSC       16                              /* sectors/log drum */
#define DRM_NUMWDL      (DRM_NUMWDS * DRM_NUMSC)        /* words/log drum */
#define DRM_LDMASK      (DRM_NUMWDL - 1)                /* logical disk mask */
#define DRM_NUMLD       6                               /* log drums/phys drum */
#define DRM_SIZE        (DRM_NUMLD * DRM_NUMWDL)        /* words/phys drum */
#define GET_POS(x)      ((int) fmod (sim_gtime() / ((double) (x)), \
                        ((double) DRM_NUMWDS)))
#define GET_PROT(x)     ((x[drm_phy] >> (drm_log - 1)) & 1)

/* Drum address from channel */

#define DRM_V_PHY       30                              /* physical drum sel */
#define DRM_M_PHY       03
#define DRM_V_LOG       18                              /* logical drum sel */
#define DRM_M_LOG       07
#define DRM_V_WDA       0                               /* word address */
#define DRM_M_WDA       (DRM_NUMWDL - 1)
#define DRM_GETPHY(x)   (((uint32) ((x) >> DRM_V_PHY)) & DRM_M_PHY)
#define DRM_GETLOG(x)   ((((uint32) (x)) >> DRM_V_LOG) & DRM_M_LOG)
#define DRM_GETWDA(x)   ((((uint32) (x)) >> DRM_V_WDA) & DRM_M_WDA)
#define DRM_GETDA(l,x)  ((((l) - 1) * DRM_NUMWDL) + (x))

/* SCD word */

#define DRMS_V_IOC      35                              /* IO check */
#define DRMS_V_INV      33                              /* invalid command */
#define DRMS_V_PHY      31                              /* physical drum */
#define DRMS_V_LOG      28                              /* logical drum */
#define DRMS_V_WDA      13                              /* disk address */
#define DRMS_V_WRP      22                              /* write protect */
#define DRMS_V_LPCR     18                              /* LPRCR */
#define DRMS_M_LPCR     017

/* Drum controller states */

#define DRM_IDLE        0
#define DRM_1ST         1
#define DRM_FILL        2
#define DRM_DATA        3
#define DRM_EOD         4

uint32 drm_ch = CH_G;                                   /* drum channel */
uint32 drm_da = 0;                                      /* drum address */
uint32 drm_phy = 0;                                     /* physical drum */
uint32 drm_log = 0;                                     /* logical drum */
uint32 drm_sta = 0;                                     /* state */
uint32 drm_op = 0;                                      /* operation */
t_uint64 drm_chob = 0;                                  /* output buf */
uint32 drm_chob_v = 0;                                  /* valid */
uint32 drm_prot[DRM_NUMDR] = { 0 };                     /* drum protect sw */
int32 drm_time = 10;                                    /* inter-word time */

extern uint32 ind_ioc;

t_stat drm_svc (UNIT *uptr);
t_stat drm_reset (DEVICE *dptr);
t_stat drm_chsel (uint32 ch, uint32 sel, uint32 unit);
t_stat drm_chwr (uint32 ch, t_uint64 val, uint32 flags);
t_bool drm_da_incr (void);

/* DRM data structures

   drm_dev      DRM device descriptor
   drm_unit     DRM unit descriptor
   drm_reg      DRM register list
*/

DIB drm_dib = { &drm_chsel, &drm_chwr };

UNIT drm_unit[] = {
    { UDATA (&drm_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+
             UNIT_MUSTBUF+UNIT_DISABLE, DRM_SIZE) },
    { UDATA (&drm_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+
             UNIT_MUSTBUF+UNIT_DISABLE, DRM_SIZE) },
    { UDATA (&drm_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+
             UNIT_MUSTBUF+UNIT_DISABLE+UNIT_DIS, DRM_SIZE) },
    { UDATA (&drm_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+
             UNIT_MUSTBUF+UNIT_DISABLE+UNIT_DIS, DRM_SIZE) },
    };

REG drm_reg[] = {
    { ORDATA (STATE, drm_sta, 3) },
    { ORDATA (UNIT,drm_phy, 2), REG_RO },
    { ORDATA (LOG, drm_log, 3), REG_RO },
    { ORDATA (DA, drm_da, 15) },
    { FLDATA (OP, drm_op, 0) },
    { ORDATA (CHOB, drm_chob, 36) },
    { FLDATA (CHOBV, drm_chob_v, 0) },
    { ORDATA (PROT0, drm_prot[0], 6) },
    { ORDATA (PROT1, drm_prot[1], 6) },
    { ORDATA (PROT2, drm_prot[2], 6) },
    { ORDATA (PROT3, drm_prot[3], 6) },
    { DRDATA (TIME, drm_time, 24), REG_NZ + PV_LEFT },
    { DRDATA (CHAN, drm_ch, 3), REG_HRO },
    { NULL }
    };

MTAB drm_mtab[] = {
    { MTAB_XTD|MTAB_VDV, 0, "CHANNEL", NULL, NULL, &ch_show_chan },
    { 0 }
    };

DEVICE drm_dev = {
    "DRM", drm_unit, drm_reg, drm_mtab,
    DRM_NUMDR, 8, 18, 1, 8, 36,
    NULL, NULL, &drm_reset,
    NULL, NULL, NULL,
    &drm_dib, DEV_DIS
    };

/* Channel select routine */

t_stat drm_chsel (uint32 ch, uint32 sel, uint32 unit)
{
drm_ch = ch;                                            /* save channel */
if (sel & CHSL_NDS)                                     /* nds? nop */
    return ch6_end_nds (ch);

switch (sel) {                                          /* case on cmd */

    case CHSL_RDS:                                      /* read */
    case CHSL_WRS:                                      /* write */
        if (drm_sta != DRM_IDLE)                        /* busy? */
            return ERR_STALL;
        drm_sta = DRM_1ST;                              /* initial state */
        if (sel == CHSL_WRS)                            /* set read/write */
            drm_op = 1;
        else drm_op = 0;                                /* LCHx sends addr */
        break;                                          /* wait for addr */

    default:                                            /* other */
        return STOP_ILLIOP;
        }
return SCPE_OK;
}

/* Channel diagnostic store routine */

t_uint64 drm_sdc (uint32 ch)
{
t_uint64 val;


val = (((t_uint64) ind_ioc) << DRMS_V_IOC) |
    (((t_uint64) drm_phy) << DRMS_V_PHY) |
    (((t_uint64) drm_log) << DRMS_V_LOG) |
    (((t_uint64) (drm_da & ~ DRM_GPMASK)) << DRMS_V_WDA) |
    (((t_uint64) GET_PROT(drm_prot)) << DRMS_V_WRP);
return val;
}

/* Channel write routine */

t_stat drm_chwr (uint32 ch, t_uint64 val, uint32 flags)
{
int32 cp, dp;

if (drm_sta == DRM_1ST) {
    drm_phy = DRM_GETPHY (val);                         /* get unit */
    drm_log = DRM_GETLOG (val);                         /* get logical disk */
    drm_da = DRM_GETWDA (val);                          /* get drum word addr */
    if ((drm_unit[drm_phy].flags & UNIT_DIS) ||         /* disabled unit? */
        (drm_log == 0) || (drm_log > DRM_NUMLD) ||      /* invalid log drum? */
        ((drm_op != 0) && (GET_PROT (drm_prot) != 0))) { /* write to prot drum? */
        ch6_err_disc (ch, U_DRM, CHF_TRC);              /* disconnect */
        drm_sta = DRM_IDLE;
        return SCPE_OK;
        }
    cp = GET_POS (drm_time);                            /* current pos in sec */
    dp = (drm_da & DRM_SCMASK) - cp;                    /* delta to desired pos */
    if (dp <= 0)                                        /* if neg, add rev */
        dp = dp + DRM_NUMWDS;
    sim_activate (&drm_unit[drm_phy], dp * drm_time);   /* schedule */
    if (drm_op) {                                       /* if write, get word */
        ch6_req_wr (ch, U_DRM);
        drm_sta = DRM_FILL;                             /* sector fill */
        }
    else drm_sta = DRM_DATA;                            /* data transfer */
    drm_chob = 0;                                       /* clr, inval buffer */
    drm_chob_v = 0;
    }
else {
    drm_chob = val & DMASK;
    drm_chob_v = 1;
    }
return SCPE_OK;
}

/* Unit service - this code assumes the entire drum is buffered */

t_stat drm_svc (UNIT *uptr)
{
uint32 i;
t_uint64 *fbuf = (t_uint64 *) uptr->filebuf;
uint32 da = DRM_GETDA (drm_log, drm_da);

if ((uptr->flags & UNIT_BUF) == 0) {                    /* not buf? */
    ch6_err_disc (drm_ch, U_DRM, CHF_TRC);              /* set TRC, disc */
    drm_sta = DRM_IDLE;                                 /* drum is idle */
    return SCPE_UNATT;
    }

switch (drm_sta) {                                      /* case on state */

    case DRM_FILL:                                      /* write, clr group */
        for (i = da & ~DRM_GPMASK; i <= (da | DRM_GPMASK); i++)
            fbuf[i] = 0;                                /* clear group */
        if (i >= uptr-> hwmark)
            uptr->hwmark = i + 1;
        drm_sta = DRM_DATA;                             /* now data */
                                                        /* fall through */
    case DRM_DATA:                                      /* data */
        if (drm_op) {                                   /* write? */
            if (drm_chob_v)                             /* valid? clear */
                drm_chob_v = 0;
            else if (ch6_qconn (drm_ch, U_DRM))         /* no, chan conn? */
                ind_ioc = 1;                            /* io check */
            fbuf[da] = drm_chob;                        /* get data */
            if (da >= uptr->hwmark)
                uptr->hwmark = da + 1;
            if (!drm_da_incr ())                        /* room for more? */
                ch6_req_wr (drm_ch, U_DRM);
            }
        else{                                           /* read */
            ch6_req_rd (drm_ch, U_DRM, fbuf[da], 0);    /* send word to channel */
            drm_da_incr ();
            }
        sim_activate (uptr, drm_time);                  /* next word */
        break;

    case DRM_EOD:                                       /* end logical disk */
        if (ch6_qconn (drm_ch, U_DRM))                  /* drum still conn? */
            ch6_err_disc (drm_ch, U_DRM, CHF_EOF);      /* set EOF, disc */
        drm_sta = DRM_IDLE;                             /* drum is idle */
        break;
        }                                               /* end case */

return SCPE_OK;
}

/* Increment drum address - return true, set new state if end of logical disk */

t_bool drm_da_incr (void)
{
drm_da = (drm_da + 1) & DRM_LDMASK;
if (drm_da != 0)
    return FALSE;
drm_sta = DRM_EOD;
return TRUE;
}

/* Reset routine */

t_stat drm_reset (DEVICE *dptr)
{
uint32 i;

drm_phy = 0;
drm_log = 0;
drm_da = 0;
drm_op = 0;
drm_sta = DRM_IDLE;
drm_chob = 0;
drm_chob_v = 0;
for (i = 0; i < dptr->numunits; i++)
    sim_cancel (dptr->units + i);
return SCPE_OK;
}
