/* sigma_mt.c: Sigma 732X 9-track magnetic tape

   Copyright (c) 2007-2024, Robert M. Supnik

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

   mt           7320 and 7322/7323 magnetic tape

   17-Feb-24    RMS     Zero delay from SIO to INIT state (Ken Rector)
   11-Feb-24    RMS     Report non-operational if not attached (Ken Rector)
   01-Feb-24    RMS     Fixed nx unit test (Ken Rector)
   01-Nov-23    RMS     Fixed reset not to clear BOT
   31-Mar-23    RMS     Mask unit flag before calling status in AIO (Ken Rector)
   07-Feb-23    RMS     Silenced Mac compiler warnings (Ken Rector)
   15-Dec-22    RMS     Moved SIO interrupt test to devices
   20-Jul-22    RMS     Space record must set EOF flag on tape mark (Ken Rector)
   03-Jul-22    RMS     Fixed error in handling of channel errors (Ken Rector)
   02-Jul-22    RMS     Fixed bugs in multi-unit operation
   07-Jun-22    RMS     Removed unused variables (V4)
   26-Mar-22    RMS     Added extra case points for new MTSE definitions
   23-Mar-20    RMS     Unload should call sim_tape_detach (Mark Pizzolato)
   13-Mar-17    RMS     Annotated fall through in switch

   Magnetic tapes are represented as a series of variable records
   of the form:

        32b byte count
        byte 0
        byte 1
        :
        byte n-2
        byte n-1
        32 byte count

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a byte count of 0.
*/

#include "sigma_io_defs.h"
#include "sim_tape.h"

/* Device definitions */

#define MT_NUMDR        8                               /* #drives */
#define MT_REW          (MT_NUMDR)                      /* rewind threads */
#define UST             u3                              /* unit status */
#define UCMD            u4                              /* unit command */
#define MT_MAXFR        (1 << 16)                       /* max record lnt */

/* Unit commands */

#define MCM_INIT        0x100
#define MCM_END         0x101
#define MCM_WRITE       0x01
#define MCM_READ        0x02
#define MCM_SETC        0x03
#define MCM_SENSE       0x04
#define MCM_RDBK        0x0C
#define MCM_RWI         0x13
#define MCM_RWU         0x23
#define MCM_REW         0x33
#define MCM_SFWR        0x43
#define MCM_SBKR        0x4B
#define MCM_SFWF        0x53
#define MCM_SBKF        0x5B
#define MCM_ERS         0x63
#define MCM_WTM         0x73

/* Command flags */

#define O_ATT           0x01                            /* req attached */
#define O_WRE           0x02                            /* req write enb */
#define O_REV           0x04                            /* reverse oper */
#define O_NMT           0x10                            /* no motion */

/* Device status in UST, ^ = dynamic */

#define MTDV_OVR        0x80                            /* overrun - NI */
#define MTDV_WRE        0x40                            /* write enabled^ */
#define MTDV_WLE        0x20                            /* write lock err */
#define MTDV_EOF        0x10                            /* end of file */
#define MTDV_DTE        0x08                            /* data error */
#define MTDV_BOT        0x04                            /* begin of tape */
#define MTDV_EOT        0x02                            /* end of tape^ */
#define MTDV_REW        0x01                            /* rewinding^ */

#define MTAI_MASK       (MTDV_OVR|MTDV_WLE|MTDV_EOF|MTDV_DTE)
#define MTAI_V_INT      6
#define MTAI_INT        (1u << MTAI_V_INT)

uint32 mt_stopioe = 1;
int32 mt_rwtime = 10000;                                /* rewind latency */
int32 mt_ctime = 100;                                   /* command latency */
int32 mt_time = 10;                                     /* record latency */
uint32 mt_rwi = 0;                                      /* rewind interrupts */
uint32 mt_atn = 0;                                      /* attention interrupt */
t_mtrlnt mt_bptr;
t_mtrlnt mt_blim;
uint8 mt_xb[MT_MAXFR];                                  /* transfer buffer */
uint8 mt_op[128] = {
    0, O_ATT|O_WRE, O_ATT, O_NMT, O_NMT, 0, 0, 0,       /* wr, rd, set, sense */
    0, 0, 0, 0, O_ATT|O_REV, 0, 0, 0,                   /* rd rev */
    0, 0, 0, O_ATT, 0, 0, 0, 0,                         /* rewind & int */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, O_ATT, 0, 0, 0, 0,                         /* rewind offline */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, O_ATT, 0, 0, 0, 0,                         /* rewind */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, O_ATT, 0, 0, 0, 0,                         /* space fwd rec */
    0, 0, 0, O_ATT|O_REV, 0, 0, 0, 0,                   /* space bk rec */
    0, 0, 0, O_ATT, 0, 0, 0, 0,                         /* space fwd file */
    0, 0, 0, O_ATT|O_REV, 0, 0, 0, 0,                   /* space bk file */
    0, 0, 0, O_NMT, 0, 0, 0, 0,                         /* set erase */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, O_ATT|O_WRE, 0, 0, 0, 0,                   /* write tmk */
    0, 0, 0, 0, 0, 0, 0, 0
    };

extern uint32 chan_ctl_time;
extern uint8 ascii_to_ebcdic[128];
extern uint8 ebcdic_to_ascii[256];

uint32 mt_disp (uint32 op, uint32 dva, uint32 *dvst);
uint32 mt_tio_status (uint32 un);
uint32 mt_tdv_status (uint32 un);
t_stat mt_chan_err (uint32 dva, uint32 st);
t_stat mtu_svc (UNIT *uptr);
t_stat mtr_svc (UNIT *uptr);
t_stat mt_reset (DEVICE *dptr);
t_stat mt_attach (UNIT *uptr, CONST char *cptr);
t_stat mt_detach (UNIT *uptr);
t_stat mt_flush_buf (UNIT *uptr);
t_stat mt_map_err (UNIT *uptr, t_stat r);
int32 mt_clr_int (uint32 dva);
void mt_set_rwi (uint32 un);
void mt_clr_rwi (uint32 un);
void mt_set_atn (uint32 un);
void mt_clr_atn (uint32 un);

/* MT data structures

   mt_dev       MT device descriptor
   mt_unit      MT unit descriptors
   mt_reg       MT register list
   mt_mod       MT modifiers list
*/

dib_t mt_dib = { DVA_MT, mt_disp };

/* First 'n' units are tape drives; second 'n' are rewind threads */

UNIT mt_unit[] = {
    { UDATA (&mtu_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mtu_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mtu_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mtu_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mtu_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mtu_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mtu_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mtu_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mtr_svc, UNIT_DIS, 0) },
    { UDATA (&mtr_svc, UNIT_DIS, 0) },
    { UDATA (&mtr_svc, UNIT_DIS, 0) },
    { UDATA (&mtr_svc, UNIT_DIS, 0) },
    { UDATA (&mtr_svc, UNIT_DIS, 0) },
    { UDATA (&mtr_svc, UNIT_DIS, 0) },
    { UDATA (&mtr_svc, UNIT_DIS, 0) },
    { UDATA (&mtr_svc, UNIT_DIS, 0) }
    };

REG mt_reg[] = {
    { BRDATA (BUF, mt_xb, 16, 8, MT_MAXFR) },
    { DRDATA (BPTR, mt_bptr, 17) },
    { DRDATA (BLNT, mt_blim, 17) },
    { HRDATA (RWINT, mt_rwi, MT_NUMDR) },
    { DRDATA (TIME, mt_time, 24), PV_LEFT+REG_NZ },
    { DRDATA (CTIME, mt_ctime, 24), PV_LEFT+REG_NZ },
    { DRDATA (RWTIME, mt_rwtime, 24), PV_LEFT+REG_NZ },
    { URDATA (UST, mt_unit[0].UST, 16, 8, 0, MT_NUMDR, 0) },
    { URDATA (UCMD, mt_unit[0].UCMD, 16, 8, 0, 2 * MT_NUMDR, 0) },
    { URDATA (POS, mt_unit[0].pos, 10, T_ADDR_W, 0,
              MT_NUMDR, PV_LEFT | REG_RO) },
    { FLDATA (STOP_IOE, mt_stopioe, 0) },
    { HRDATA (DEVNO, mt_dib.dva, 12), REG_HRO },
    { NULL }
    };

MTAB mt_mod[] = {
    { MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL }, 
    { MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
      &sim_tape_set_fmt, &sim_tape_show_fmt, NULL },
    { MTAB_XTD|MTAB_VUN, 0, "CAPACITY", "CAPACITY",
      &sim_tape_set_capac, &sim_tape_show_capac, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "CHAN", "CHAN",
      &io_set_dvc, &io_show_dvc, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DVA", "DVA",
      &io_set_dva, &io_show_dva, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "CSTATE", NULL,
      NULL, &io_show_cst, NULL },
    { 0 }
    };

DEVICE mt_dev = {
    "MT", mt_unit, mt_reg, mt_mod,
    MT_NUMDR * 2, 10, T_ADDR_W, 1, 16, 8,
    NULL, NULL, &mt_reset,
    &io_boot, &mt_attach, &mt_detach,
    &mt_dib, DEV_DISABLE | DEV_TAPE
    };

/* Magtape: IO dispatch routine

   For all calls except AIO, dva is the full channel/device/unit address
   For AIO, the handler must return the unit number
*/

uint32 mt_disp (uint32 op, uint32 dva, uint32 *dvst)
{
uint32 un = DVA_GETUNIT (dva);
UNIT *uptr = &mt_unit[un];

if ((un >= MT_NUMDR) ||                                 /* inv unit num? */
    (uptr-> flags & UNIT_DIS)) {                        /* disabled unit? */
    *dvst = DVT_NODEV;
    return 0;
    }
switch (op) {                                           /* case on op */

    case OP_SIO:                                        /* start I/O */
        *dvst = mt_tio_status (un);                     /* get status */
        if (chan_chk_dvi (dva))                         /* int pending? */
            *dvst |= (CC2 << DVT_V_CC);                 /* SIO fails */
        else if ((*dvst & (DVS_CST|DVS_DST)) == 0) {    /* ctrl + dev idle? */
            uptr->UCMD = MCM_INIT;                      /* start dev thread */
            sim_activate (uptr, 0);
            }
        break;

    case OP_TIO:                                        /* test status */
        *dvst = mt_tio_status (un);                     /* return status */
        break;

    case OP_TDV:                                        /* test status */
        *dvst = mt_tdv_status (un);                     /* return status */
        break;

    case OP_HIO:                                        /* halt I/O */
        *dvst = mt_tio_status (un);                     /* get status */
        if ((int32) un == chan_chk_chi (dva))           /* halt active ctlr int? */
            chan_clr_chi (dva);                         /* clear ctlr int */
        if (sim_is_active (uptr)) {                     /* chan active? */
            sim_cancel (uptr);                          /* stop unit */
            chan_uen (dva);                             /* uend */
            }
        mt_clr_rwi (un);                                /* clear rewind int */
        sim_cancel (uptr + MT_REW);                     /* cancel rewind */
        break;

    case OP_AIO:                                        /* acknowledge int */
        un = mt_clr_int (mt_dib.dva);                   /* clr int, get unit and flag */
        *dvst =
            (mt_tdv_status (un & DVA_M_DEVMU) & MTAI_MASK) | /* device status */
            (un & MTAI_INT) |                           /* device int flag */
            ((un & DVA_M_UNIT) << DVT_V_UN);            /* unit number */
        break;

    default:
        *dvst = 0;
        return SCPE_IERR;
        }

return 0;
}

/* Unit service

   1. The full unit address must be reconstructed at entry, for use in interrupt/error routines
   2. The magtape error library returns status code 'r' that overlaps SCP error codes. mt_map_err
      translates/acts upon these code and returns 'st'. This can incude channel error codes,
      so mt_chan_err finally resolves the errors to machine-specific actions (UEND) or an
      error code to return to SCP.
   3. Channel routines always return channel error codes and must invoke mt_chan_err.
   4. Tape mark can be encountered on any read or space command. Except for space file,
      it is treated as an error and causes a UEND.

   The macro CHS_IFERR is TRUE if the error code is a channel error or a fatal SCP error
   and FALSE if the error code is a chennel information code or 0.
*/

t_stat mtu_svc (UNIT *uptr)
{
uint32 cmd = uptr->UCMD;
uint32 un = uptr - mt_unit;
uint32 dva = mt_dib.dva | un;
uint32 c;
int32 t;
t_mtrlnt tbc;
t_stat r, st;

if (cmd == MCM_INIT) {                                  /* init state */
    if ((t = sim_activate_time (uptr + MT_REW)) != 0) { /* rewinding? */
        sim_activate (uptr, t);                         /* retry when done */
        return SCPE_OK;
        }
    st = chan_get_cmd (dva, &cmd);                      /* get command */
    if (CHS_IFERR (st))                                 /* channel error? */
        return mt_chan_err (dva, st);
    if ((cmd & 0x80) ||                                 /* invalid cmd? */
        (mt_op[cmd] == 0)) {
        uptr->UCMD = MCM_END;                           /* end state */
        sim_activate (uptr, chan_ctl_time);             /* resched ctlr */
        return SCPE_OK;
        }
    else {                                              /* valid cmd */
        if ((mt_op[cmd] & O_REV) &&                     /* reverse op */
            (mt_unit[un].UST & MTDV_BOT)) {             /* at load point? */
            chan_uen (dva);                             /* uend */
            return SCPE_OK;
            }
        uptr->UCMD = cmd;                               /* unit state */
        if (!(mt_op[cmd] & O_NMT))                      /* motion? */
            uptr->UST = 0;                              /* clear status */
        }
    mt_blim = 0;                                        /* no buffer yet */
    sim_activate (uptr, chan_ctl_time);                 /* continue thread */
    return SCPE_OK;                                     /* done */
    }

if (cmd == MCM_END) {                                   /* end state */
    st = chan_end (dva);                                /* set channel end */
    if (CHS_IFERR (st))                                 /* channel error? */
        return mt_chan_err (dva, st);
    if (st == CHS_CCH) {                                /* command chain? */
        uptr->UCMD = MCM_INIT;                          /* restart thread */
        sim_activate (uptr, chan_ctl_time);
        }
    else uptr->UCMD = 0;                                /* ctlr idle */
    return SCPE_OK;                                     /* done */
    }

if ((mt_op[cmd] & O_ATT) &&                             /* op req att and */
    ((uptr->flags & UNIT_ATT) == 0)) {                  /* not attached? */
    sim_activate (uptr, mt_ctime);                      /* retry */
    return mt_stopioe? SCPE_UNATT: SCPE_OK;
    }
if ((mt_op[cmd] & O_WRE) &&                             /* write op and */
    sim_tape_wrp (uptr)) {                              /* write protected? */
    uptr->UST |= MTDV_WLE;                              /* set status */
    chan_uen (dva);                                     /* uend */
    return SCPE_OK;
    }

switch (cmd) {                                          /* case on command */

    case MCM_SFWR:                                      /* space forward */
        if ((r = sim_tape_sprecf (uptr, &tbc))) {       /* spc rec fwd, err? */
            st = mt_map_err (uptr, r);                  /* map error */
            if (CHS_IFERR (st))                         /* chan or SCP err? */
                return mt_chan_err (dva, st);           /* uend and stop */
            }
        break;

    case MCM_SBKR:                                      /* space reverse */
        if ((r = sim_tape_sprecr (uptr, &tbc))) {       /* spc rec rev, err? */
            st = mt_map_err (uptr, r);                  /* map error */
            if (CHS_IFERR (st))                         /* chan or SCP err? */
                return mt_chan_err (dva, st);           /* uend and stop */
            }
        break;

    case MCM_SFWF:                                      /* space fwd file */
        while ((r = sim_tape_sprecf (uptr, &tbc)) == MTSE_OK) ;
        if (r != MTSE_TMK) {                            /* no tmk? */
            st = mt_map_err (uptr, r);                  /* map error */
            if (CHS_IFERR (st))                         /* chan or SCP err? */
                return mt_chan_err (dva, st);           /* uend and stop */
            }
        uptr->UST |= MTDV_EOF;                          /* set eof */
        break;

    case MCM_SBKF:                                       /* space rev file */
        while ((r = sim_tape_sprecr (uptr, &tbc)) == MTSE_OK) ;
        if (r != MTSE_TMK) {                            /* no tmk? */
            st = mt_map_err (uptr, r);                  /* map error */
            if (CHS_IFERR (st))                         /* chan or SCP err? */
                return mt_chan_err (dva, st);           /* uend and stop */
            }
        uptr->UST |= MTDV_EOF;                          /* set eof */
        break;

    case MCM_WTM:                                       /* write eof */
        if ((r = sim_tape_wrtmk (uptr))) {              /* write tmk, err? */
            st = mt_map_err (uptr, r);                  /* map error */
            if (CHS_IFERR (st))                         /* chan or SCP err? */
                return mt_chan_err (dva, st);           /* uend and stop */
            }
        uptr->UST |= MTDV_EOF;                          /* set eof */
        break;

    case MCM_RWU:                                       /* rewind unload */
        sim_tape_detach (uptr);                         /* detach tape */
        break;

    case MCM_REW:                                       /* rewind */
    case MCM_RWI:                                       /* rewind and int */
        if ((r = sim_tape_rewind (uptr))) {             /* rewind */
            st = mt_map_err (uptr, r);                  /* map error */
            if (CHS_IFERR (st))                         /* chan or SCP err? */
                return mt_chan_err (dva, st);           /* uend and stop */
            }
        mt_unit[un + MT_REW].UCMD = uptr->UCMD;         /* copy command */
        sim_activate (uptr + MT_REW, mt_rwtime);        /* sched compl */
        break;

    case MCM_READ:                                      /* read */
        if (mt_blim == 0) {                             /* first read? */
            if ((r = sim_tape_rdrecf (uptr, mt_xb, &mt_blim, MT_MAXFR))) {
                st = mt_map_err (uptr, r);              /* map error */
                if (CHS_IFERR (st))                     /* chan or SCP err? */
                    return mt_chan_err (dva, st);       /* uend and stop */
                }
            if (mt_blim == 0)                           /* no data? */
                return mt_chan_err (dva, SCPE_IERR);    /* should NOT happen */
            mt_bptr = 0;                                /* init rec ptr */
            }
        c = mt_xb[mt_bptr++];                           /* get char */
        st = chan_WrMemB (dva, c);                      /* write to memory */
        if (CHS_IFERR (st))                             /* channel error? */
            return mt_chan_err (dva, st);
        if ((st != CHS_ZBC) && (mt_bptr != mt_blim)) {  /* not done? */
            sim_activate (uptr, mt_time);               /* continue thread */
            return SCPE_OK;
            }
        if (((st == CHS_ZBC) ^ (mt_bptr == mt_blim)) && /* length err? */ 
              chan_set_chf (dva, CHF_LNTE))             /* uend taken? */
            return SCPE_OK;                             /* finished */
        break;                                          /* normal end */

    case MCM_RDBK:                                      /* read reverse */
        if (mt_blim == 0) {                             /* first read? */
            if ((r = sim_tape_rdrecr (uptr, mt_xb, &mt_blim, MT_MAXFR))) {
                st = mt_map_err (uptr, r);              /* map error */
                if (CHS_IFERR (st))                     /* chan or SCP err? */
                    return mt_chan_err (dva, st);       /* uend and stop */
                }
            if (mt_blim == 0)                           /* no data? */
                return mt_chan_err (dva, SCPE_IERR);    /* should NOT happen */
            mt_bptr = mt_blim;                          /* init rec ptr */
            }
        c = mt_xb[--mt_bptr];                           /* get char */
        st = chan_WrMemBR (dva, c);                     /* write mem rev */
        if (CHS_IFERR (st))                             /* channel error? */
            return mt_chan_err (dva, st);
        if ((st != CHS_ZBC) && (mt_bptr != 0)) {        /* not done? */
            sim_activate (uptr, mt_time);               /* continue thread */
            return SCPE_OK;
            }
        if (((st == CHS_ZBC) ^ (mt_bptr == 0)) &&       /* length err? */
              chan_set_chf (dva, CHF_LNTE))             /* uend taken? */
            return SCPE_OK;                             /* finished */
        break;                                          /* normal end */

    case MCM_WRITE:                                     /* write */
        st = chan_RdMemB (dva, &c);                     /* read char */
        if (CHS_IFERR (st)) {                           /* channel error? */
            mt_flush_buf (uptr);                        /* flush buffer */
            return mt_chan_err (dva, st);
            }
        mt_xb[mt_blim++] = c;                           /* store in buffer */
        if (st != CHS_ZBC) {                            /* end record? */
             sim_activate (uptr, mt_time);              /* continue thread */
             return SCPE_OK;
             }
        if ((r = mt_flush_buf (uptr)) != 0) {           /* flush buffer */
            st = mt_map_err (uptr, r);                  /* map error */
            if (CHS_IFERR (st))                         /* chan or SCP err? */
                return mt_chan_err (dva, st);           /* uend and stop */
            }
        break;
        }

uptr->UCMD = MCM_END;                                   /* end state */
sim_activate (uptr, mt_ctime);                          /* sched ctlr */
return SCPE_OK;
}

/* Rewind completion - set BOT, interrupt if desired */

t_stat mtr_svc (UNIT *uptr)
{
uint32 un = uptr - mt_unit - MT_REW;

mt_unit[un].UST |= MTDV_BOT;                            /* set BOT */
if (uptr->UCMD == MCM_RWI)                              /* int wanted? */
    mt_set_rwi (un);                                    /* interrupt */
return SCPE_OK;
}

t_stat mt_flush_buf (UNIT *uptr)
{
if (mt_blim == 0)                                       /* any output? */
    return SCPE_OK;
return sim_tape_wrrecf (uptr, mt_xb, mt_blim);          /* write, err? */
}

/* Map tape error status - returns channel inactive, SCPE_OK, or SCP error */

t_stat mt_map_err (UNIT *uptr, t_stat st)
{

uint32 un = uptr - mt_unit;
uint32 dva = mt_dib.dva | un;

switch (st) {

    case MTSE_FMT:                                      /* illegal fmt */
    case MTSE_UNATT:                                    /* not attached */
    case MTSE_WRP:                                      /* write protect */
    default:                                            /* unknown error*/
        chan_set_chf (dva, CHF_XMME);                   /* set err, fall through */
    case MTSE_OK:                                       /* no error */
        return SCPE_IERR;

    case MTSE_TMK:                                      /* end of file */
    case MTSE_EOM:                                      /* end of medium */
        uptr->UST |= MTDV_EOF;                          /* set eof flag */
        return CHS_INACTV;

    case MTSE_IOERR:                                    /* IO error */
        uptr->UST |= MTDV_DTE;                          /* set DTE flag */
        chan_set_chf (dva, CHF_XMDE);
        return SCPE_IOERR;

    case MTSE_INVRL:                                    /* invalid rec lnt */
        uptr->UST |= MTDV_DTE;                          /* set DTE flag */
        chan_set_chf (dva, CHF_XMDE);
        return SCPE_MTRLNT;

    case MTSE_RECE:                                     /* record in error */
        uptr->UST |= MTDV_DTE;                          /* set DTE flag */
        return chan_set_chf (dva, CHF_XMDE);            /* possible error */

    case MTSE_BOT:                                      /* reverse into BOT */
        uptr->UST |= MTDV_BOT;                          /* set BOT */
        return CHS_INACTV;
        }                                               /* end switch */

return SCPE_OK;
}

/* MT status routine */

uint32 mt_tio_status (uint32 un)
{
uint32 i, st;
UNIT *uptr = &mt_unit[un];

st = DVS_AUTO;                                          /* flags */
if (sim_is_active (uptr) ||                             /* unit busy */
    sim_is_active (uptr + MT_REW))                      /* or rewinding? */
    st |= DVS_DBUSY;
else if ((uptr -> flags & UNIT_ATT) == 0)              /* not att => offl */
    st |= DVS_DOFFL;                                 
for (i = 0; i < MT_NUMDR; i++) {                        /* loop thru units */
    if (sim_is_active (&mt_unit[i])) {                  /* active? */
        st |= (DVS_CBUSY | (CC2 << DVT_V_CC));          /* ctrl is busy */
        }
    }
return st;
}

uint32 mt_tdv_status (uint32 un)
{
uint32 st;
UNIT *uptr = &mt_unit[un];

if (uptr->flags & UNIT_ATT) {                           /* attached? */
    st = uptr->UST;                                     /* unit stat */
    if (sim_tape_eot (uptr))                            /* at EOT? */
        st |= MTDV_EOT;
    if (!sim_tape_wrp (uptr))                           /* not wlock? */
        st |= MTDV_WRE;
    }
else st = (CC2 << DVT_V_CC);
if (sim_is_active (uptr + MT_REW))                      /* unit rewinding? */
    st |= (MTDV_REW | (CC2 << DVT_V_CC));
return st;
}


/* Channel error */

t_stat mt_chan_err (uint32 dva, uint32 st)
{
chan_uen (dva);                                         /* uend */
if (st < CHS_ERR)
    return st;
return SCPE_OK;
}

/* Clear controller/device interrupt, return active unit */

int32 mt_clr_int (uint32 dva)
{
int32 iu;

if ((iu = chan_clr_chi (dva)) >= 0) {                   /* chan int? clear */
    if (mt_rwi != 0)                                    /* dev ints? */
        chan_set_dvi (dva);                             /* set them */
    return iu;
    }
for (iu = 0; iu < MT_NUMDR; iu++) {                     /* rewind int? */
    if (mt_rwi & (1u << iu)) {
        mt_clr_rwi ((uint32) iu);
        return (iu | MTAI_INT);
        }
    if (mt_atn & (1u << iu)) {
        mt_clr_atn ((uint32) iu);
        return (iu | MTAI_INT);
        }
    }
return 0;
}

/* Set rewind interrupt */

void mt_set_rwi (uint32 un)
{
mt_rwi |= (1u << un);
chan_set_dvi (mt_dib.dva);                              /* set INP */
return;
}

/* Set ATN interrupt */

void mt_set_atn (uint32 un)
{
mt_atn |= (1u << un);
chan_set_dvi (mt_dib.dva);                              /* set INP */
return;
}

/* Clear rewind interrupt */

void mt_clr_rwi (uint32 un)
{
mt_rwi &= ~(1u << un);                                  /* clear */
if (mt_rwi != 0)                                        /* more? */
    chan_set_dvi (mt_dib.dva);
else if (chan_chk_chi (mt_dib.dva) < 0)                 /* any int? */
    chan_clr_chi (mt_dib.dva);                          /* clr INP */
return;
}

void mt_clr_atn (uint32 un)
{
mt_atn &= ~(1u << un);
if (mt_atn != 0)                                        /* more? */
    chan_set_dvi (mt_dib.dva);
else if (chan_chk_chi (mt_dib.dva) < 0)                 /* any int? */
    chan_clr_chi (mt_dib.dva);                          /* clr INP */
return;
}
    
/* Reset routine */

t_stat mt_reset (DEVICE *dptr)
{
uint32 i;

for (i = 0; i < MT_NUMDR; i++) {
    sim_cancel (&mt_unit[i]);                           /* stop unit */
    sim_cancel (&mt_unit[i + MT_REW]);                  /* stop rewind */
    if (mt_unit[i].flags & UNIT_ATT)                    /* attached? */
        mt_unit[i].UST &= MTDV_BOT;                     /* clr sta exc BOT */
    else mt_unit[i].UST = 0;
    mt_unit[i].UCMD = 0;
    }
mt_rwi = 0;
mt_bptr = 0;
mt_blim = 0;
chan_reset_dev (mt_dib.dva);                            /* clr int, active */
for (i = 0; i < MT_MAXFR; i++)
    mt_xb[i] = 0;
return SCPE_OK;
}

/* Attach routine */

t_stat mt_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;
uint32 un = uptr - mt_unit;

r = sim_tape_attach (uptr, cptr);
if (r != SCPE_OK)
    return r;
uptr->UST = MTDV_BOT;
if (sim_switches & SWMASK('A'))
    mt_set_atn (un);
return r;
}

/* Detach routine */

t_stat mt_detach (UNIT* uptr)
{
if (!(uptr->flags & UNIT_ATTABLE))
    return SCPE_NOATT;
uptr->UST = 0;
sim_cancel (uptr + MT_REW);
return sim_tape_detach (uptr);
}
