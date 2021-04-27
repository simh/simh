/* sigma_dk.c: 7250/7251-7252 cartridge disk simulator

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

   dk           7250/7251-7252 cartridge disk

   Transfers are always done a sector at a time.
*/

#include "sigma_io_defs.h"
#include <math.h>

#define UTRK            u3                              /* current track */

/* Constants */

#define DK_NUMDR        8                               /* drives/ctlr */
#define DK_WDSC         90                              /* words/sector */
#define DK_SCTK         16                              /* sectors/track */
#define DK_TKUN         408                             /* tracks/unit */
#define DK_WDUN         (DK_WDSC*DK_SCTK*DK_TKUN)       /* words/unit */

/* Address bytes */

#define DKA_V_TK        4                               /* track offset */
#define DKA_M_TK        0x1FF
#define DKA_V_SC        0                               /* sector offset */
#define DKA_M_SC        0xF
#define DKA_GETTK(x)    (((x) >> DKA_V_TK) & DKA_M_TK)
#define DKA_GETSC(x)    (((x) >> DKA_V_SC) & DKA_M_SC)

/* Status byte 3 is current sector */

#define DKS_NBY         3

/* Device state */

#define DKS_INIT        0x101
#define DKS_END         0x102
#define DKS_WRITE       0x01
#define DKS_READ        0x02
#define DKS_SEEK        0x03
#define DKS_SEEK2       0x103
#define DKS_SENSE       0x04
#define DKS_CHECK       0x05
#define DKS_RDEES       0x12
#define DKS_TEST        0x13

/* Device status */

#define DKV_OVR         0x80                            /* overrun - NI */
#define DKV_BADS        0x20                            /* bad track */
#define DKV_WPE         0x10

#define GET_PSC(x)      ((int32) fmod (sim_gtime() / ((double) (x * DK_WDSC)), \
                        ((double) DK_SCTK)))

uint32 dk_cmd = 0;                                      /* state */
uint32 dk_flags = 0;                                    /* status flags */
uint32 dk_ad = 0;                                       /* disk address */
uint32 dk_time = 5;                                     /* inter-word time */
uint32 dk_stime = 20;                                   /* inter-track time */
uint32 dk_stopioe = 1;                                  /* stop on I/O error */

extern uint32 chan_ctl_time;

uint32 dk_disp (uint32 op, uint32 dva, uint32 *dvst);
uint32 dk_tio_status (uint32 un);
uint32 dk_tdv_status (uint32 un);
t_stat dk_chan_err (uint32 st);
t_stat dk_svc (UNIT *uptr);
t_stat dk_reset (DEVICE *dptr);
t_bool dk_inv_ad (uint32 *da);
t_bool dk_inc_ad (void);
t_bool dk_end_sec (UNIT *uptr, uint32 lnt, uint32 exp, uint32 st);

/* DK data structures

   dk_dev       DK device descriptor
   dk_unit      DK unit descriptor
   dk_reg       DK register list
*/

dib_t dk_dib = { DVA_DK, &dk_disp };

UNIT dk_unit[] = {
    { UDATA (&dk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, DK_WDUN) },
    { UDATA (&dk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, DK_WDUN) },
    { UDATA (&dk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, DK_WDUN) },
    { UDATA (&dk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, DK_WDUN) },
    { UDATA (&dk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, DK_WDUN) },
    { UDATA (&dk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, DK_WDUN) },
    { UDATA (&dk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, DK_WDUN) },
    { UDATA (&dk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, DK_WDUN) }
    };

REG dk_reg[] = {
    { HRDATA (CMD, dk_cmd, 9) },
    { HRDATA (FLAGS, dk_flags, 8) },
    { HRDATA (ADDR, dk_ad, 8) },
    { DRDATA (TIME, dk_time, 24), PV_LEFT+REG_NZ },
    { DRDATA (STIME, dk_stime, 24), PV_LEFT+REG_NZ },
    { FLDATA (STOPIOE, dk_stopioe, 0) },
    { HRDATA (DEVNO, dk_dib.dva, 12), REG_HRO },
    { NULL }
    };

MTAB dk_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write enable drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "Write lock drive" },
    { MTAB_XTD|MTAB_VDV, 0, "CHAN", "CHAN",
      &io_set_dvc, &io_show_dvc, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DVA", "DVA",
      &io_set_dva, &io_show_dva, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "CSTATE", NULL,
      NULL, &io_show_cst, NULL },
    { 0 }
    };

DEVICE dk_dev = {
    "DK", dk_unit, dk_reg, dk_mod,
    DK_NUMDR, 16, 22, 1, 16, 32,
    NULL, NULL, &dk_reset,
    NULL, NULL, NULL,
    &dk_dib, DEV_DISABLE
    };

/* DK: IO dispatch routine */

uint32 dk_disp (uint32 op, uint32 dva, uint32 *dvst)
{
uint32 i;
uint32 un = DVA_GETUNIT (dva);
UNIT *uptr;

if ((un >= DK_NUMDR) ||                                 /* inv unit num? */
    (dk_unit[un].flags & UNIT_DIS))                     /* disabled unit? */
    return DVT_NODEV;
switch (op) {                                           /* case on op */

    case OP_SIO:                                        /* start I/O */
        *dvst = dk_tio_status (un);                     /* get status */
        if ((*dvst & (DVS_CST|DVS_DST)) == 0) {         /* ctrl + dev idle? */
            dk_cmd = DKS_INIT;                          /* start dev thread */
            sim_activate (&dk_unit[un], chan_ctl_time);
            }
        break;

    case OP_TIO:                                        /* test status */
        *dvst = dk_tio_status (un);                     /* return status */
        break;

    case OP_TDV:                                        /* test status */
        *dvst = dk_tdv_status (un);                     /* return status */
        break;

    case OP_HIO:                                        /* halt I/O */
        chan_clr_chi (dk_dib.dva);                      /* clr int */
        *dvst = dk_tio_status (un);                     /* get status */
        if ((*dvst & DVS_CST) != 0) {                   /* ctrl busy? */
            for (i = 0; i < DK_NUMDR; i++) {            /* find busy unit */
                uptr = &dk_unit[i];
                if (sim_is_active (uptr)) {             /* active? */
                    sim_cancel (uptr);                  /* stop */
                    chan_uen (dk_dib.dva);              /* uend */
                    }                                   /* end if active */
                }                                       /* end for */
            }
        break;

    case OP_AIO:                                        /* acknowledge int */
        chan_clr_chi (dk_dib.dva);                      /* clr int */
        *dvst = dk_tdv_status (un);                     /* status like TDV */
        break;

    default:
        *dvst = 0;
        return SCPE_IERR;
        }

return 0;
}

/* Unit service  */

t_stat dk_svc (UNIT *uptr)
{
uint32 i, sc, da, wd, wd1, cmd, c[3];
uint32 *fbuf = (uint32 *) uptr->filebuf;
int32 t, dc;
uint32 st;

switch (dk_cmd) {

    case DKS_INIT:                                      /* init state */
        st = chan_get_cmd (dk_dib.dva, &cmd);           /* get command */
        if (CHS_IFERR (st))                             /* channel error? */
            return dk_chan_err (st);
        if ((cmd == 0) ||                               /* invalid cmd? */
            ((cmd > DKS_CHECK) && (cmd != DKS_RDEES) && (cmd != DKS_TEST))) {
            chan_uen (dk_dib.dva);                      /* uend */
            return SCPE_OK;
            }
        dk_flags = 0;                                   /* clear status */
        dk_cmd = cmd & 0x17;                            /* next state */
        if ((cmd == DKS_SEEK) ||                        /* fast cmd? */
            (cmd == DKS_SENSE) ||
            (cmd == DKS_TEST))
            sim_activate (uptr, chan_ctl_time);         /* schedule soon */
        else {                                          /* data transfer */
            sc = DKA_GETSC (dk_ad);                     /* new sector */
            t = sc - GET_PSC (dk_time);                 /* delta to new */
            if (t < 0)                                  /* wrap around? */
                t = t + DK_SCTK;
            sim_activate (uptr, t * dk_time * DK_WDSC); /* schedule op */
            }
        return SCPE_OK;

    case DKS_END:                                       /* end state */
        st = chan_end (dk_dib.dva);                     /* set channel end */
        if (CHS_IFERR (st))                             /* channel error? */
            return dk_chan_err (st);
        if (st == CHS_CCH) {                            /* command chain? */
            dk_cmd = DKS_INIT;                          /* restart thread */
            sim_activate (uptr, chan_ctl_time);
            }
        return SCPE_OK;                                 /* done */

    case DKS_SEEK:                                      /* seek */
        c[0] = c[1] = 0;
        for (i = 0, st = 0; (i < 2) && (st != CHS_ZBC); i++) {
            st = chan_RdMemB (dk_dib.dva, &c[i]);       /* get byte */
            if (CHS_IFERR (st))                         /* channel error? */
                return dk_chan_err (st);
            }
        dk_ad = ((c[0] & 0x7F) << 8) | c[1];            /* new address */
        if (((i != 2) || (st != CHS_ZBC)) &&            /* length error? */
            chan_set_chf (dk_dib.dva, CHF_LNTE))        /* care? */
            return SCPE_OK;
        dc = DKA_GETTK (dk_ad);                         /* desired track */
        t = abs (uptr->UTRK - dc);                      /* get track diff */
        if (t == 0)
            t = 1;
        sim_activate (uptr, t * dk_stime);
        uptr->UTRK = dc;                                 /* put on track */
        dk_cmd = DKS_SEEK2;
        return SCPE_OK;

    case DKS_SEEK2:                                     /* seek complete */
        if (uptr->UTRK >= DK_TKUN) {
            dk_flags |= DKV_BADS;
            chan_uen (dk_dib.dva);
            return SCPE_OK;
            }
        break;                                          /* seek done */
        
    case DKS_SENSE:                                     /* sense */
        c[0] = ((dk_ad >> 8) & 0x7F) | ((uptr->flags & UNIT_RO)? 0x80: 0);
        c[1] = dk_ad & 0xFF;                            /* address */
        c[2] = GET_PSC (dk_time);                       /* curr sector */
        for (i = 0, st = 0; (i < DKS_NBY) && (st != CHS_ZBC); i++) {
            st = chan_WrMemB (dk_dib.dva, c[i]);        /* store char */
            if (CHS_IFERR (st))                         /* channel error? */
                return dk_chan_err (st);
            }
        if (((i != DKS_NBY) || (st != CHS_ZBC)) &&
            chan_set_chf (dk_dib.dva, CHF_LNTE))        /* length error? */
            return SCPE_OK;
        break;

    case DKS_WRITE:                                     /* write */
        if (uptr->flags & UNIT_RO) {                    /* write locked? */
            dk_flags |= DKV_WPE;                        /* set status */
            chan_uen (dk_dib.dva);                      /* uend */
            return SCPE_OK;
            }
        if (dk_inv_ad (&da)) {                          /* invalid addr? */
            chan_uen (dk_dib.dva);                      /* uend */
            return SCPE_OK;
            }
        for (i = 0, st = 0; i < DK_WDSC; da++, i++) {   /* sector loop */
            if (st != CHS_ZBC) {                        /* chan not done? */
                st = chan_RdMemW (dk_dib.dva, &wd);     /* read word */
                if (CHS_IFERR (st)) {                   /* channel error? */
                    dk_inc_ad ();                       /* da increments */
                    return dk_chan_err (st);
                    }
                }
            else wd = 0;
            fbuf[da] = wd;                              /* store in buffer */
            if (da >= uptr->hwmark)                     /* update length */
                uptr->hwmark = da + 1;
            }
         if (dk_end_sec (uptr, i, DK_WDSC, st))         /* transfer done? */
            return SCPE_OK;                             /* err or cont */
         break;

/* Must be done by bytes to get precise miscompare */

    case DKS_CHECK:                                     /* write check */
        if (dk_inv_ad (&da)) {                          /* invalid addr? */
            chan_uen (dk_dib.dva);                      /* uend */
            return SCPE_OK;
            }
        for (i = 0, st = 0; (i < (DK_WDSC * 4)) && (st != CHS_ZBC); ) {
            st = chan_RdMemB (dk_dib.dva, &wd);         /* read byte */
            if (CHS_IFERR (st)) {                       /* channel error? */
                dk_inc_ad ();                           /* da increments */
                return dk_chan_err (st);
                }
            wd1 = (fbuf[da] >> (24 - ((i % 4) * 8))) & 0xFF; /* byte */
            if (wd != wd1) {                            /* check error? */
                dk_inc_ad ();                           /* da increments */
                chan_set_chf (dk_dib.dva, CHF_XMDE);    /* set xmt err flag */
                chan_uen (dk_dib.dva);                  /* force uend */
                return SCPE_OK;
                }
            da = da + ((++i % 4) == 0);                 /* every 4th byte */
            }        
        if (dk_end_sec (uptr, i, DK_WDSC * 4, st))      /* transfer done? */
            return SCPE_OK;                             /* err or cont */
        break;

    case DKS_READ:                                      /* read */
        if (dk_inv_ad (&da)) {                          /* invalid addr? */
            chan_uen (dk_dib.dva);                      /* uend */
                return SCPE_OK;
            }
        for (i = 0, st = 0; (i < DK_WDSC) && (st != CHS_ZBC); da++, i++) {
            st = chan_WrMemW (dk_dib.dva, fbuf[da]);    /* store in mem */
            if (CHS_IFERR (st)) {                       /* channel error? */
                dk_inc_ad ();                           /* da increments */
                return dk_chan_err (st);
                }
            }
        if (dk_end_sec (uptr, i, DK_WDSC, st))          /* transfer done? */
            return SCPE_OK;                             /* err or cont */
        break;
        }

dk_cmd = DKS_END;                                       /* op done, next state */
sim_activate (uptr, chan_ctl_time);
return SCPE_OK;
}

/* Common read/write sector end routine 

   case 1 - more to transfer, not end disk - reschedule, return TRUE
   case 2 - more to transfer, end disk - uend, return TRUE
   case 3 - transfer done, length error - uend, return TRUE
   case 4 - transfer done, no length error - return FALSE (sched end state)
*/

t_bool dk_end_sec (UNIT *uptr, uint32 lnt, uint32 exp, uint32 st)
{
if (st != CHS_ZBC) {                                    /* end record? */
    if (dk_inc_ad ())                                   /* inc addr, ovf? */
        chan_uen (dk_dib.dva);                          /* uend */
    else sim_activate (uptr, dk_time * 16);             /* no, next sector */
    return TRUE;
    }
dk_inc_ad ();                                           /* just incr addr */
if ((lnt != exp) &&                                     /* length error? */
    chan_set_chf (dk_dib.dva, CHF_LNTE))                /* do we care? */
    return TRUE;
return FALSE;                                           /* cmd done */
}

/* DK status routine */

uint32 dk_tio_status (uint32 un)
{
uint32 i;

for (i = 0; i < DK_NUMDR; i++) {                        /* loop thru units */
    if (sim_is_active (&dk_unit[i]))                    /* active? */
        return (DVS_AUTO | DVS_CBUSY | (CC2 << DVT_V_CC) |
            ((i == un)? DVS_DBUSY: 0));
    }
return DVS_AUTO;
}

uint32 dk_tdv_status (uint32 un)
{
return dk_flags | (dk_inv_ad (NULL)? DKV_BADS: 0);
}

/* Validate disk address */

t_bool dk_inv_ad (uint32 *da)
{
uint32 tk = DKA_GETTK (dk_ad);
uint32 sc = DKA_GETSC (dk_ad);

if (tk >= DK_TKUN)                                      /* badtrk? */
    return TRUE;
if (da)                                                 /* return word addr */
    *da = ((tk * DK_SCTK) + sc) * DK_WDSC;
return FALSE;
}

/* Increment disk address */

t_bool dk_inc_ad (void)
{
uint32 tk = DKA_GETTK (dk_ad);
uint32 sc = DKA_GETSC (dk_ad);

sc = sc + 1;                                            /* sector++ */
if (sc >= DK_SCTK) {                                    /* overflow? */
    sc = 0;                                             /* wrap sector */
    tk = tk + 1;                                        /* track++ */
    }
dk_ad = ((tk << DKA_V_TK) |                             /* rebuild dk_ad */
          (sc << DKA_V_SC));
if (tk >= DK_TKUN)                                      /* invalid addr? */
    return TRUE;
return FALSE;
}

/* Channel error */

t_stat dk_chan_err (uint32 st)
{
chan_uen (dk_dib.dva);                                 /* uend */
if (st < CHS_ERR)
    return st;
return SCPE_OK;
}

/* Reset routine */

t_stat dk_reset (DEVICE *dptr)
{
uint32 i;

for (i = 0; i < DK_NUMDR; i++) {
    sim_cancel (&dk_unit[i]);                          /* stop dev thread */
    dk_unit[i].UTRK = 0;
    }
dk_cmd = 0;
dk_flags = 0;
dk_ad = 0;
chan_reset_dev (dk_dib.dva);                           /* clr int, active */
return SCPE_OK;
}
