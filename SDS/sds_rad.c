/* sds_rad.c: SDS 940 fixed head disk simulator

   Copyright (c) 2001-2008, Robert M. Supnik

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

   rad          fixed head disk

   The fixed head disk is a head-per-track disk, with up to four disks.  Each
   disk is divided into two logical units.  Reads and writes cannot cross logical
   unit boundaries.  The fixed head disk transfers 12b characters, rather than 6b
   characters.  To minimize overhead, the disk is buffered in memory.
*/

#include "sds_defs.h"
#include <math.h>

/* Constants */

#define RAD_CHAN        CHAN_E                          /* Connected I/O controller */
#define RAD_NUMWD       64                              /* words/sector */
#define RAD_NUMSC       64                              /* sectors/track */
#define RAD_NUMTR       64                              /* tracks/log unit */
#define RAD_NUMLU       8                               /* log units/ctrl */
#define RAD_SCSIZE      (RAD_NUMLU*RAD_NUMTR*RAD_NUMSC) /* sectors/disk */
#define RAD_AMASK       (RAD_SCSIZE - 1)                /* sec addr mask */
#define RAD_SIZE        (RAD_SCSIZE * RAD_NUMWD)        /* words/disk */
#define RAD_GETLUN(x)   ((x) / (RAD_NUMTR * RAD_NUMSC))
#define RAD_SCMASK      (RAD_NUMSC - 1)                 /* sector mask */
#define RAD_TRSCMASK    ((RAD_NUMSC * RAD_NUMTR) - 1)   /* track/sec mask */

#define GET_SECTOR(x)   ((int) fmod (sim_gtime() / ((double) (x)), \
                        ((double) RAD_NUMSC)))

extern uint32 xfr_req;
extern uint32 alert;
extern int32 stop_invins, stop_invdev, stop_inviop;
int32 rad_err = 0;                                      /* error */
int32 rad_nobi = 0;                                     /* !incr x track */
int32 rad_da = 0;                                       /* disk address */
int32 rad_sba = 0;                                      /* sec byte addr */
int32 rad_wrp = 0;                                      /* write prot */
int32 rad_time = 2;                                     /* time per 12b */
int32 rad_stopioe = 1;                                  /* stop on error */
DSPT rad_tplt[] = {                                     /* template */
    { 1, 0 },
    { 1, DEV_OUT },
    { 0, 0 }
    };

DEVICE rad_dev;
t_stat rad_svc (UNIT *uptr);
t_stat rad_reset (DEVICE *dptr);
t_stat rad_boot (int32 unitno, DEVICE *dptr);
t_stat rad_fill (int32 sba);
void rad_end_op (int32 fl);
int32 rad_adjda (int32 sba, int32 inc);
t_stat rad (uint32 fnc, uint32 inst, uint32 *dat);

/* RAD data structures

   rad_dib      device information block
   rad_dev      device descriptor
   rad_unit     unit descriptor
   rad_reg      register list
*/

DIB rad_dib = { RAD_CHAN, DEV_RAD, XFR_RAD, rad_tplt, &rad };

UNIT rad_unit = {
    UDATA (&rad_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF,
           RAD_SIZE)
    };

REG rad_reg[] = {
    { ORDATA (DA, rad_da, 15) },
    { GRDATA (SA, rad_sba, 8, 6, 1) },
    { FLDATA (BP, rad_sba, 0) },
    { FLDATA (XFR, xfr_req, XFR_V_RAD) },
    { FLDATA (NOBD, rad_nobi, 0) },
    { FLDATA (ERR, rad_err, 0) },
    { ORDATA (PROT, rad_wrp, 8) },
    { DRDATA (TIME, rad_time, 24), REG_NZ + PV_LEFT },
    { FLDATA (STOP_IOE, rad_stopioe, 0) },
    { NULL }
    };

MTAB rad_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "CHANNEL", "CHANNEL",
      &set_chan, &show_chan, NULL },
    { 0 }
    };

DEVICE rad_dev = {
    "RAD", &rad_unit, rad_reg, rad_mod,
    1, 8, 21, 1, 8, 24,
    NULL, NULL, &rad_reset,
    &rad_boot, NULL, NULL,
    &rad_dib, DEV_DISABLE
    };

/* Fixed head disk routine

   conn -       inst = EOM0, dat = NULL
   eom1 -       inst = EOM1, dat = NULL
   sks -        inst = SKS, dat = ptr to result
   disc -       inst = device number, dat = NULL
   wreor -      inst = device number, dat = NULL
   read -       inst = device number, dat = ptr to data
   write -      inst = device number, dat = ptr to result
*/

t_stat rad (uint32 fnc, uint32 inst, uint32 *dat)
{
int32 t, lun, new_ch;
uint32 p;
uint32 *fbuf = rad_unit.filebuf;

switch (fnc) {                                          /* case function */

    case IO_CONN:                                       /* connect */
        new_ch = I_GETEOCH (inst);                      /* get new chan */
        if (new_ch != rad_dib.chan)                     /* wrong chan? */
            return SCPE_IERR;
        if (CHC_GETCPW (inst) > 1)                      /* 1-2 char/word? */
            return STOP_INVIOP;
        if (sim_is_active (&rad_unit) || (alert == POT_RADA)) /* protocol viol? */
            return STOP_INVIOP;
        rad_err = 0;                                    /* clr error */
        rad_sba = 0;                                    /* clr sec bptr */
        chan_set_flag (rad_dib.chan, CHF_12B);          /* 12B mode */
        t = (rad_da & RAD_SCMASK) - GET_SECTOR (rad_time * RAD_NUMWD);
        if (t <= 0)                                     /* seek */
            t = t + RAD_NUMSC;
        sim_activate (&rad_unit, t * rad_time * (RAD_NUMWD / 2));
        xfr_req = xfr_req & ~XFR_RAD;                   /* clr xfr flg */
        break;

    case IO_EOM1:                                       /* EOM mode 1 */
        new_ch = I_GETEOCH (inst);                      /* get new chan */
        if (new_ch != rad_dib.chan)                     /* wrong chan? */
            return SCPE_IERR;
        if ((inst & 00600) == 00200)                    /* alert for sec */
            alert = POT_RADS;
        else if ((inst & 06600) == 0) {                 /* alert for addr */
            if (sim_is_active (&rad_unit))              /* busy? */
                rad_err = 1;
            else {
                rad_nobi = (inst & 01000)? 1: 0;        /* save inc type */
                alert = POT_RADA;                       /* set alert */
                }
            }
        break;

    case IO_DISC:                                       /* disconnect */
        rad_end_op (0);                                 /* normal term */
        if (inst & DEV_OUT)                             /* fill write */
            return rad_fill (rad_sba);
        break;

    case IO_WREOR:                                      /* write eor */
        rad_end_op (CHF_EOR);                           /* eor term */
        return rad_fill (rad_sba);                      /* fill write */

    case IO_SKS:                                        /* SKS */
        new_ch = I_GETSKCH (inst);                      /* sks chan */
        if (new_ch != rad_dib.chan)                     /* wrong chan? */
            return SCPE_IERR;
        t = I_GETSKCND (inst);                          /* sks cond */
        lun = RAD_GETLUN (rad_da);
        if (((t == 000) && !sim_is_active (&rad_unit)) || /* 10026: ready */
            ((t == 004) && !rad_err) ||                 /* 11026: !err */
            ((t == 014) && !(rad_wrp & (1 << lun))))    /* 13026: !wrprot */
            *dat = 1;
        break;

    case IO_READ:                                       /* read */
        p = (rad_da * RAD_NUMWD) + (rad_sba >> 1);      /* buf wd addr */
        xfr_req = xfr_req & ~XFR_RAD;                   /* clr xfr req */
        if ((rad_unit.flags & UNIT_BUF) == 0) {         /* not buffered? */
            rad_end_op (CHF_ERR | CHF_EOR);             /* set rad err */
            CRETIOE (rad_stopioe, SCPE_UNATT);
            }
        if (p >= rad_unit.capac) {                      /* end of disk? */
            rad_end_op (CHF_ERR | CHF_EOR);             /* set rad err */
            return SCPE_OK;
            }
        if (rad_sba & 1)                                /* odd byte? */
            *dat = fbuf[p] & 07777;
        else *dat = (fbuf[p] >> 12) & 07777;            /* even */
        rad_sba = rad_adjda (rad_sba, 1);               /* next byte */
        break;

    case IO_WRITE:
        p = (rad_da * RAD_NUMWD) + (rad_sba >> 1);
        xfr_req = xfr_req & ~XFR_RAD;                   /* clr xfr req */
        if ((rad_unit.flags & UNIT_BUF) == 0) {         /* not buffered? */
            rad_end_op (CHF_ERR | CHF_EOR);             /* set rad err */
            CRETIOE (rad_stopioe, SCPE_UNATT);
            }
        if ((p >= rad_unit.capac) ||                    /* end of disk? */
            (rad_wrp & (1 << RAD_GETLUN (rad_da)))) {   /* write prot? */
            rad_end_op (CHF_ERR | CHF_EOR);             /* set rad err */
            return SCPE_OK;
            }
        if (rad_sba & 1)                                /* odd byte? */
            fbuf[p] = fbuf[p] | (*dat & 07777);
        else fbuf[p] = (*dat & 07777) << 12;            /* even */
        if (p >= rad_unit.hwmark)                       /* mark hiwater */
            rad_unit.hwmark = p + 1;
        rad_sba = rad_adjda (rad_sba, 1);               /* next byte */
        break;

    default:
        CRETINS;
        }

return SCPE_OK;
}

/* PIN routine */

t_stat pin_rads (uint32 num, uint32 *dat)
{
*dat = GET_SECTOR (rad_time * RAD_NUMWD);               /* ret curr sec */
return SCPE_OK;
}

/* POT routine */

t_stat pot_rada (uint32 num, uint32 *dat)
{
rad_da = (*dat) & RAD_AMASK;                            /* save dsk addr */
return SCPE_OK;
}

/* Unit service and read/write */

t_stat rad_svc (UNIT *uptr)
{
xfr_req = xfr_req | XFR_RAD;                            /* set xfr req */
sim_activate (&rad_unit, rad_time);                     /* activate */
return SCPE_OK;
}

/* Fill incomplete sector */

t_stat rad_fill (int32 sba)
{
uint32 p = rad_da * RAD_NUMWD;
uint32 *fbuf = rad_unit.filebuf;
int32 wa = (sba + 1) >> 1;                              /* whole words */

if (sba && (p < rad_unit.capac)) {                      /* fill needed? */
    for ( ; wa < RAD_NUMWD; wa++)
        fbuf[p + wa] = 0;
    if ((p + wa) >= rad_unit.hwmark)
        rad_unit.hwmark = p + wa + 1;
    rad_adjda (sba, RAD_NUMWD - 1);                     /* inc da */
    }
return SCPE_OK;
}

/* Adjust disk address */

int32 rad_adjda (int32 sba, int32 inc)
{
sba = sba + inc;
if (rad_sba >= (RAD_NUMWD * 2)) {                       /* next sector? */
    if (rad_nobi) rad_da = (rad_da & ~RAD_SCMASK) +     /* within band? */
        ((rad_da + 1) & RAD_SCMASK);
    else rad_da = (rad_da & ~RAD_TRSCMASK) +            /* cross band */
        ((rad_da + 1) & RAD_TRSCMASK);
    sba = 1;                                            /* start new sec */
    }
return sba;
}

/* Terminate disk operation */

void rad_end_op (int32 fl)
{
if (fl)                                                 /* set flags */
    chan_set_flag (rad_dib.chan, fl);
xfr_req = xfr_req & ~XFR_RAD;                           /* clear xfr */
sim_cancel (&rad_unit);                                 /* stop */
if (fl & CHF_ERR) {                                     /* error? */
    chan_disc (rad_dib.chan);                           /* disconnect */
    rad_err = 1;                                        /* set rad err */
    }
return;
}

/* Reset routine */

t_stat rad_reset (DEVICE *dptr)
{
chan_disc (rad_dib.chan);                               /* disconnect */
rad_nobi = 0;                                           /* clear state */
rad_da = 0;
rad_sba = 0;
xfr_req = xfr_req & ~XFR_RAD;                           /* clr xfr req */
sim_cancel (&rad_unit);                                 /* deactivate */
return SCPE_OK;
}

/* Boot routine - simulate FILL console command */

t_stat rad_boot (int32 unitno, DEVICE *dptr)
{
extern uint32 P, M[];

if (unitno)                                             /* only unit 0 */
    return SCPE_ARG;
if (rad_dib.chan != CHAN_W)                             /* only on W channel */
    return SCPE_IOERR;
M[0] = 077777771;                                       /* -7B */
M[1] = 007100000;                                       /* LDX 0 */
M[2] = 000203226;                                       /* EOM 3226B */
M[3] = 003200002;                                       /* WIM 2 */
M[4] = 000100002;                                       /* BRU 2 */
P = 1;                                                  /* start at 1 */
return SCPE_OK;
}
