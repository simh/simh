/* sigma_rad.c: Sigma 7211/7212 or 7231/7232 fixed head disk simulator

   Copyright (c) 2007-2008, Robert M Supnik

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

   rad          7211/7212 or 7231/7232 fixed head disk

   The RAD is a head-per-track disk.  To minimize overhead, the entire RAD
   is buffered in memory.

   Transfers are always done a sector at a time.
*/

#include "sigma_io_defs.h"
#include <math.h>

/* Constants */

#define RAD_7212        0                               /* ctlr type */
#define RAD_7232        1
#define RAD_NUMDR       4                               /* drives/ctlr */
#define RAD_WDSC        256                             /* words/sector */
#define RAD_WDMASK      (RAD_WDSC - 1)
#define RAD_SCTK1       82                              /* sectors/track */
#define RAD_SCTK3       12
#define RAD_TKUN1       64                              /* tracks/unit */
#define RAD_TKUN3       512
#define RAD_WDUNDF      (RAD_WDSC*RAD_SCTK1*RAD_TKUN1)  /* dflt words/unit */
#define RAD_WDUN        (RAD_WDSC*rad_tab[rad_model].sctk*rad_tab[rad_model].tkun)
#define RAD_N_WLK       16                              /* num wlk switches */

/* Address bytes */

#define RADA_V_TK1      7                               /* track offset */
#define RADA_M_TK1      0xFF
#define RADA_V_SC1      0                               /* sector offset */
#define RADA_M_SC1      0x7F
#define RADA_V_TK3      4
#define RADA_M_TK3      0x3FF
#define RADA_V_SC3      0
#define RADA_M_SC3      0xF
#define RADA_GETTK(x)   (((x) >> rad_tab[rad_model].tk_v) & rad_tab[rad_model].tk_m)
#define RADA_GETSC(x)   (((x) >> rad_tab[rad_model].sc_v) & rad_tab[rad_model].sc_m)

/* Address bad flag */

#define RADA_INV        0x80

/* Status byte 3 is current sector */
/* Status byte 4 (7212 only) is failing sector */

#define RADS_NBY1       4                               /* num status bytes */
#define RADS_NBY3       3

/* Device state */

#define RADS_INIT       0x101
#define RADS_END        0x102
#define RADS_WRITE      0x01
#define RADS_READ       0x02
#define RADS_SEEK       0x03
#define RADS_SENSE      0x04
#define RADS_CHECK      0x05
#define RADS_RDEES      0x12

/* Device status */

#define RADV_OVR        0x80                            /* overrun - NI */
#define RADV_BADS       0x20                            /* bad sector */
#define RADV_WPE        0x10

#define GET_PSC(x)      ((int32) fmod (sim_gtime() / ((double) (x * RAD_WDSC)), \
                        ((double) rad_tab[rad_model].sctk)))

/* Model table */

typedef struct {
    uint32        tk_v;                                 /* track extract */
    uint32        tk_m;
    uint32        sc_v;                                 /* sector extract */
    uint32        sc_m;
    uint32        sctk;                                 /* sectors/track */
    uint32        tkun;                                 /* tracks/unit */
    uint32        nbys;                                 /* bytes of status */
    } rad_t;

static rad_t rad_tab[] = {
    { RADA_V_TK1, RADA_M_TK1, RADA_V_SC1, RADA_M_SC1, RAD_SCTK1, RAD_TKUN1, RADS_NBY1 },
    { RADA_V_TK3, RADA_M_TK3, RADA_V_SC3, RADA_M_SC3, RAD_SCTK3, RAD_TKUN3, RADS_NBY3 }
    };

uint32 rad_model = RAD_7212;                            /* model */
uint32 rad_cmd = 0;                                     /* state */
uint32 rad_flags = 0;                                   /* status flags */
uint32 rad_ad = 0;                                      /* rad address */
uint32 rad_wlk = 0;                                     /* write lock */
uint32 rad_time = 2;                                    /* inter-word time */

extern uint32 chan_ctl_time;

uint32 rad_disp (uint32 op, uint32 dva, uint32 *dvst);
uint32 rad_tio_status (uint32 un);
uint32 rad_tdv_status (uint32 un);
t_stat rad_chan_err (uint32 st);
t_stat rad_svc (UNIT *uptr);
t_stat rad_reset (DEVICE *dptr);
t_stat rad_settype (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat rad_showtype (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_bool rad_inv_ad (uint32 *da);
t_bool rad_inc_ad (void);
t_bool rad_end_sec (UNIT *uptr, uint32 lnt, uint32 exp, uint32 st);

/* RAD data structures

   rad_dev       RAD device descriptor
   rad_unit      RAD unit descriptor
   rad_reg       RAD register list
*/

dib_t rad_dib = { DVA_RAD, &rad_disp };

UNIT rad_unit[] = {
    { UDATA (&rad_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+
             UNIT_MUSTBUF+UNIT_DISABLE, RAD_WDUNDF) },
    { UDATA (&rad_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+
             UNIT_MUSTBUF+UNIT_DISABLE+UNIT_DIS, RAD_WDUNDF) },
    { UDATA (&rad_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+
             UNIT_MUSTBUF+UNIT_DISABLE+UNIT_DIS, RAD_WDUNDF) },
    { UDATA (&rad_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+
             UNIT_MUSTBUF+UNIT_DISABLE+UNIT_DIS, RAD_WDUNDF) }
    };

REG rad_reg[] = {
    { HRDATA (CMD, rad_cmd, 9) },
    { HRDATA (FLAGS, rad_flags, 8) },
    { HRDATA (ADDR, rad_ad, 15) },
    { HRDATA (WLK, rad_wlk, RAD_N_WLK) },
    { DRDATA (TIME, rad_time, 24), PV_LEFT },
    { FLDATA (MODEL, rad_model, 0), REG_HRO },
    { HRDATA (DEVNO, rad_dib.dva, 12), REG_HRO },
    { NULL }
    };

MTAB rad_mod[] = {
    { MTAB_XTD | MTAB_VDV, RAD_7212, NULL, "7211",
      &rad_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, RAD_7212, NULL, "7212",
      &rad_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, RAD_7232, NULL, "7231",
      &rad_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, RAD_7232, NULL, "7232",
      &rad_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, 0, "TYPE", NULL,
      NULL, &rad_showtype, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "CHAN", "CHAN",
      &io_set_dvc, &io_show_dvc, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DVA", "DVA",
      &io_set_dva, &io_show_dva, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "CSTATE", NULL,
      NULL, &io_show_cst, NULL },
    { 0 }
    };

DEVICE rad_dev = {
    "RAD", rad_unit, rad_reg, rad_mod,
    RAD_NUMDR, 16, 21, 1, 16, 32,
    NULL, NULL, &rad_reset,
    &io_boot, NULL, NULL,
    &rad_dib, DEV_DISABLE
    };

/* RAD: IO dispatch routine */

uint32 rad_disp (uint32 op, uint32 dva, uint32 *dvst)
{
uint32 i;
uint32 un = DVA_GETUNIT (dva);
UNIT *uptr;

if ((un >= RAD_NUMDR) ||                                /* inv unit num? */
    (rad_unit[un].flags & UNIT_DIS))                    /* disabled unit? */
    return DVT_NODEV;
switch (op) {                                           /* case on op */

    case OP_SIO:                                        /* start I/O */
        *dvst = rad_tio_status (un);                    /* get status */
        if ((*dvst & (DVS_CST|DVS_DST)) == 0) {         /* ctrl + dev idle? */
            rad_cmd = RADS_INIT;                        /* start dev thread */
            sim_activate (&rad_unit[un], chan_ctl_time);
            }
        break;

    case OP_TIO:                                        /* test status */
        *dvst = rad_tio_status (un);                    /* return status */
        break;

    case OP_TDV:                                        /* test status */
        *dvst = rad_tdv_status (un);                    /* return status */
        break;

    case OP_HIO:                                        /* halt I/O */
        chan_clr_chi (rad_dib.dva);                     /* clr int*/
        *dvst = rad_tio_status (un);                    /* get status */
        if ((*dvst & DVS_CST) != 0) {                   /* ctrl busy? */
            for (i = 0; i < RAD_NUMDR; i++) {           /* find busy unit */
                uptr = &rad_unit[i];
                if (sim_is_active (uptr)) {             /* active? */
                    sim_cancel (uptr);                  /* stop */
                    chan_uen (rad_dib.dva);             /* uend */
                    }                                   /* end if active */
                }                                       /* end for */
            }
        break;

    case OP_AIO:                                        /* acknowledge int */
        chan_clr_chi (rad_dib.dva);                     /* clr int */
        *dvst = rad_tdv_status (0);                     /* status like TDV */
        break;

    default:
        *dvst = 0;
        return SCPE_IERR;
        }

return 0;
}

/* Unit service - this code assumes the entire disk is buffered */

t_stat rad_svc (UNIT *uptr)
{
uint32 i, sc, da, cmd, wd, wd1, c[4], gp;
uint32 *fbuf = (uint32 *) uptr->filebuf;
uint32 st;
int32 t;

switch (rad_cmd) {

    case RADS_INIT:                                     /* init state */
        st = chan_get_cmd (rad_dib.dva, &cmd);          /* get command */
        if (CHS_IFERR (st))                             /* channel error? */
            return rad_chan_err (st);
        if ((cmd == 0) ||                               /* invalid cmd? */
            ((cmd > RADS_CHECK) && (cmd != RADS_RDEES))) {
            chan_uen (rad_dib.dva);                     /* uend */
            return SCPE_OK;
            }
        rad_flags = 0;                                  /* clear status */
        rad_cmd = cmd & 0x7;                            /* next state */
        if ((cmd == RADS_SEEK) || (cmd == RADS_SENSE))  /* seek or sense? */
            sim_activate (uptr, chan_ctl_time);         /* schedule soon */
        else {                                          /* data transfer */
            sc = RADA_GETSC (rad_ad);                   /* new sector */
            t = sc - GET_PSC (rad_time);                /* delta to new */
            if (t < 0)                                  /* wrap around? */
                t = t + rad_tab[rad_model].sctk;
            sim_activate (uptr, t * rad_time * RAD_WDSC); /* schedule op */
            }
        return SCPE_OK;

    case RADS_END:                                      /* end state */
        st = chan_end (rad_dib.dva);                    /* set channel end */
        if (CHS_IFERR (st))                             /* channel error? */
            return rad_chan_err (st);
        if (st == CHS_CCH) {                            /* command chain? */
            rad_cmd = RADS_INIT;                        /* restart thread */
            sim_activate (uptr, chan_ctl_time);
            }
        return SCPE_OK;                                 /* done */

    case RADS_SEEK:                                     /* seek */
        c[0] = c[1] = 0;
        for (i = 0, st = 0; (i < 2) && (st != CHS_ZBC); i++) {
            st = chan_RdMemB (rad_dib.dva, &c[i]);      /* get byte */
            if (CHS_IFERR (st))                         /* channel error? */
                return rad_chan_err (st);
            }
        rad_ad = ((c[0] & 0x7F) << 8) | c[1];           /* new address */
        if (((i != 2) || (st != CHS_ZBC)) &&            /* length error? */
            chan_set_chf (rad_dib.dva, CHF_LNTE))       /* care? */
            return SCPE_OK;
        break;

    case RADS_SENSE:                                    /* sense */
        c[0] = ((rad_ad >> 8) & 0x7F) | (rad_inv_ad (NULL)? RADA_INV: 0);
        c[1] = rad_ad & 0xFF;                           /* address */
        c[2] = GET_PSC (rad_time);                      /* curr sector */
        c[3] = 0;
        for (i = 0, st = 0; (i < rad_tab[rad_model].nbys) && (st != CHS_ZBC); i++) {
            st = chan_WrMemB (rad_dib.dva, c[i]);       /* store char */
            if (CHS_IFERR (st))                         /* channel error? */
                return rad_chan_err (st);
            }
        if (((i != rad_tab[rad_model].nbys) || (st != CHS_ZBC)) &&
            chan_set_chf (rad_dib.dva, CHF_LNTE))       /* length error? */
            return SCPE_OK;
        break;

    case RADS_WRITE:                                    /* write */
        gp = (RADA_GETSC (rad_ad) * RAD_N_WLK) /        /* write lock group */
            rad_tab[rad_model].tkun;
        if ((rad_wlk >> gp) & 1) {                      /* write lock set? */
            rad_flags |= RADV_WPE;                      /* set status */
            chan_uen (rad_dib.dva);                     /* uend */
            return SCPE_OK;
            }                                           /* fall through */
        if (rad_inv_ad (&da)) {                         /* invalid addr? */
            chan_uen (rad_dib.dva);                     /* uend */
            return SCPE_OK;
            }
        for (i = 0, st = 0; i < RAD_WDSC; da++, i++) {  /* write */
            if (st != CHS_ZBC) {                        /* chan active? */
                st = chan_RdMemW (rad_dib.dva, &wd);    /* get data */
                if (CHS_IFERR (st)) {                   /* channel error? */
                    rad_inc_ad ();                      /* da increments */
                    return rad_chan_err (st);
                    }
                }
            else wd = 0;
            fbuf[da] = wd;                              /* store in buffer */
            if (da >= uptr->hwmark)                     /* update length */
                uptr->hwmark = da + 1;
            }
       if (rad_end_sec (uptr, i, RAD_WDSC, st))         /* transfer done? */
            return SCPE_OK;
        break;

/* Must be done by bytes to get precise miscompare */

    case RADS_CHECK:                                    /* write check */
        if (rad_inv_ad (&da)) {                         /* invalid addr? */
            chan_uen (rad_dib.dva);                     /* uend */
            return SCPE_OK;
            }
        for (i = 0, st = 0; (i < (RAD_WDSC * 4)) && (st != CHS_ZBC); ) {
            st = chan_RdMemB (rad_dib.dva, &wd);        /* read sector */
            if (CHS_IFERR (st)) {                       /* channel error? */
                rad_inc_ad ();                          /* da increments */
                return rad_chan_err (st);
                }
            wd1 = (fbuf[da] >> (24 - ((i % 4) * 8))) & 0xFF; /* byte */
            if (wd != wd1) {                            /* check error? */
                rad_inc_ad ();                          /* da increments */
                chan_set_chf (rad_dib.dva, CHF_XMDE);   /* set xmt err flag */
                chan_uen (rad_dib.dva);                 /* force uend */
                return SCPE_OK;
                }
            da = da + ((++i % 4) == 0);                 /* every 4th byte */
            }
        if (rad_end_sec (uptr, i, RAD_WDSC * 4, st))    /* transfer done? */
            return SCPE_OK;
        break;

    case RADS_READ:                                     /* read */
        if (rad_inv_ad (&da)) {                         /* invalid addr? */
            chan_uen (rad_dib.dva);                     /* uend */
            return SCPE_OK;
            }
        for (i = 0, st = 0; (i < RAD_WDSC) && (st != CHS_ZBC); da++, i++) {
            st = chan_WrMemW (rad_dib.dva, fbuf[da]);   /* store in mem */
            if (CHS_IFERR (st)) {                       /* channel error? */
                rad_inc_ad ();                          /* da increments */
                return rad_chan_err (st);
                }
            }
        if (rad_end_sec (uptr, i, RAD_WDSC, st))        /* transfer done? */
            return SCPE_OK;
        break;
        }

rad_cmd = RADS_END;                                     /* op done, next state */
sim_activate (uptr, chan_ctl_time);
return SCPE_OK;
}

/* Common read/write sector end routine 

   case 1 - more to transfer, not end disk - reschedule, return TRUE
   case 2 - more to transfer, end disk - uend, return TRUE
   case 3 - transfer done, length error - uend, return TRUE
   case 4 - transfer done, no length error - return FALSE (sched end state)
*/

t_bool rad_end_sec (UNIT *uptr, uint32 lnt, uint32 exp, uint32 st)
{
if (st != CHS_ZBC) {                                    /* end record? */
    if (rad_inc_ad ())                                  /* inc addr, ovf? */
        chan_uen (rad_dib.dva);                         /* uend */
    else sim_activate (uptr, rad_time * 16);            /* no, next sector */
    return TRUE;
    }
rad_inc_ad ();                                          /* just incr addr */
if ((lnt != exp) &&                                     /* length error? */
    chan_set_chf (rad_dib.dva, CHF_LNTE))               /* do we care? */
    return TRUE;
return FALSE;                                           /* cmd done */
}

/* RAD status routine */

uint32 rad_tio_status (uint32 un)
{
uint32 i, st;

st = DVS_AUTO;                                          /* flags */
if (sim_is_active (&rad_unit[un]))                      /* active => busy */
    st |= DVS_DBUSY;
else if ((rad_unit[un].flags & UNIT_ATT) == 0)          /* not att => offl */
    st |= DVS_DOFFL;                                 
for (i = 0; i < RAD_NUMDR; i++) {                       /* loop thru units */
    if (sim_is_active (&rad_unit[i])) {                 /* active? */
        st |= (DVS_CBUSY |(CC2 << DVT_V_CC));           /* ctrl is busy */
        return st;
        }
    }
return st;
}

uint32 rad_tdv_status (uint32 un)
{
uint32 st;

st = rad_flags;
if (rad_inv_ad (NULL))                                  /* bad address? */
    st |= RADV_BADS;
return st;
}

/* Validate disk address */

t_bool rad_inv_ad (uint32 *da)
{
uint32 tk = RADA_GETTK (rad_ad);
uint32 sc = RADA_GETSC (rad_ad);

if ((tk >= rad_tab[rad_model].tkun) ||                  /* bad sec or trk? */
    (sc >= rad_tab[rad_model].sctk)) {
    return TRUE;
    }
if (da)                                                 /* return word addr */
    *da = ((tk * rad_tab[rad_model].sctk) + sc) * RAD_WDSC;
return FALSE;
}

/* Increment disk address */

t_bool rad_inc_ad (void)
{
uint32 tk = RADA_GETTK (rad_ad);
uint32 sc = RADA_GETSC (rad_ad);

sc = sc + 1;                                            /* sector++ */
if (sc >= rad_tab[rad_model].sctk) {                    /* overflow? */
    sc = 0;                                             /* wrap sector */
    tk = tk + 1;                                        /* track++ */
    }
rad_ad = ((tk << rad_tab[rad_model].tk_v) |             /* rebuild rad_ad */
          (sc << rad_tab[rad_model].sc_v));
if (tk >= rad_tab[rad_model].tkun)                      /* overflow? */
    return TRUE;
return FALSE;
}

/* Channel error */

t_stat rad_chan_err (uint32 st)
{
chan_uen (rad_dib.dva);                                 /* uend */
if (st < CHS_ERR)
    return st;
return SCPE_OK;
}

/* Reset routine */

t_stat rad_reset (DEVICE *dptr)
{
uint32 i;

for (i = 0; i < RAD_NUMDR; i++)
    sim_cancel (&rad_unit[i]);                          /* stop dev thread */
rad_cmd = 0;
rad_flags = 0;
rad_ad = 0;
chan_reset_dev (rad_dib.dva);                           /* clr int, active */
return SCPE_OK;
}

/* Set controller type */

t_stat rad_settype (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 i;

for (i = 0; i < RAD_NUMDR; i++) {                       /* all units unatt? */
    if (rad_unit[i].flags & UNIT_ATT)
        return SCPE_ALATT;
    }
rad_model = val;                                        /* update model */
rad_reset (&rad_dev);                                   /* reset */
for (i = 0; i < RAD_NUMDR; i++)                         /* update capacity */
    rad_unit[i].capac = RAD_WDUN;
return SCPE_OK;
}

/* Show controller type */

t_stat rad_showtype (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, (rad_model == RAD_7212)? "7211/7212": "7231/7232");
return SCPE_OK;
}
