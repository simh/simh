/* sigma_mt.c: Sigma 732X 9-track magnetic tape

   Copyright (c) 2007-2017, Robert M. Supnik

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
t_stat mt_chan_err (uint32 st);
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
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write enable tape drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "Write lock tape drive" },
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

/* Magtape: IO dispatch routine */

uint32 mt_disp (uint32 op, uint32 dva, uint32 *dvst)
{
uint32 un = DVA_GETUNIT (dva);
UNIT *uptr = &mt_unit[un];

if ((un >= MT_NUMDR) ||                                 /* inv unit num? */
    (uptr-> flags & UNIT_DIS))                          /* disabled unit? */
    return DVT_NODEV;
switch (op) {                                           /* case on op */

    case OP_SIO:                                        /* start I/O */
        *dvst = mt_tio_status (un);                     /* get status */
        if ((*dvst & (DVS_CST|DVS_DST)) == 0) {         /* ctrl + dev idle? */
            uptr->UCMD = MCM_INIT;                      /* start dev thread */
            sim_activate (uptr, chan_ctl_time);
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
        un = mt_clr_int (mt_dib.dva);                   /* clr int, get unit */
        *dvst = (mt_tdv_status (un) & MTAI_MASK) |      /* device status */
            (un & MTAI_INT) |                           /* device int flag */
            ((un & DVA_M_UNIT) << DVT_V_UN);            /* unit number */
        break;

    default:
        *dvst = 0;
        return SCPE_IERR;
        }

return 0;
}

/* Unit service */

t_stat mtu_svc (UNIT *uptr)
{
uint32 cmd = uptr->UCMD;
uint32 un = uptr - mt_unit;
uint32 c;
uint32 st;
int32 t;
t_mtrlnt tbc;
t_stat r;

if (cmd == MCM_INIT) {                                  /* init state */
    if ((t = sim_activate_time (uptr + MT_REW)) != 0) { /* rewinding? */
        sim_activate (uptr, t);                         /* retry later */
        return SCPE_OK;
        }
    st = chan_get_cmd (mt_dib.dva, &cmd);               /* get command */
    if (CHS_IFERR (st))                                 /* channel error? */
        return mt_chan_err (st);
    if ((cmd & 0x80) ||                                 /* invalid cmd? */
        (mt_op[cmd] == 0)) {
        uptr->UCMD = MCM_END;                           /* end state */
        sim_activate (uptr, chan_ctl_time);             /* resched ctlr */
        return SCPE_OK;
        }
    else {                                              /* valid cmd */
        if ((mt_op[cmd] & O_REV) &&                     /* reverse op */
            (mt_unit[un].UST & MTDV_BOT)) {             /* at load point? */
            chan_uen (mt_dib.dva);                      /* channel end */
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
    st = chan_end (mt_dib.dva);                         /* set channel end */
    if (CHS_IFERR (st))                                 /* channel error? */
        return mt_chan_err (st);
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
    chan_uen (mt_dib.dva);                              /* unusual end */
    return SCPE_OK;
    }

r = SCPE_OK;
switch (cmd) {                                          /* case on command */

    case MCM_SFWR:                                      /* space forward */
        if ((r = sim_tape_sprecf (uptr, &tbc)))         /* spc rec fwd, err? */
            r = mt_map_err (uptr, r);                   /* map error */
        break;

    case MCM_SBKR:                                      /* space reverse */
        if ((r = sim_tape_sprecr (uptr, &tbc)))         /* spc rec rev, err? */
            r = mt_map_err (uptr, r);                   /* map error */
        break;

    case MCM_SFWF:                                      /* space fwd file */
        while ((r = sim_tape_sprecf (uptr, &tbc)) == MTSE_OK) ;
        if (r != MTSE_TMK)                              /* stopped by tmk? */
            r = mt_map_err (uptr, r);                   /* no, map error */
        else r = SCPE_OK;
        break;

    case MCM_SBKF:                                       /* space rev file */
        while ((r = sim_tape_sprecr (uptr, &tbc)) == MTSE_OK) ;
        if (r != MTSE_TMK)                              /* stopped by tmk? */
            r = mt_map_err (uptr, r);                   /* no, map error */
        else r = SCPE_OK;
        break;

    case MCM_WTM:                                       /* write eof */
        if ((r = sim_tape_wrtmk (uptr)))                /* write tmk, err? */
            r = mt_map_err (uptr, r);                   /* map error */
        uptr->UST |= MTDV_EOF;                          /* set eof */
        break;

    case MCM_RWU:                                       /* rewind unload */
        r = sim_tape_detach (uptr);
        break;

    case MCM_REW:                                       /* rewind */
    case MCM_RWI:                                       /* rewind and int */
        if ((r = sim_tape_rewind (uptr)))               /* rewind */
            r = mt_map_err (uptr, r);                   /* map error */
        mt_unit[un + MT_REW].UCMD = uptr->UCMD;         /* copy command */
        sim_activate (uptr + MT_REW, mt_rwtime);        /* sched compl */
        break;

    case MCM_READ:                                      /* read */
        if (mt_blim == 0) {                             /* first read? */
            r = sim_tape_rdrecf (uptr, mt_xb, &mt_blim, MT_MAXFR);
            if (r != MTSE_OK) {                         /* tape error? */
                r = mt_map_err (uptr, r);               /* map error */
                break;
                }
            mt_bptr = 0;                                /* init rec ptr */
            }
        c = mt_xb[mt_bptr++];                           /* get char */
        st = chan_WrMemB (mt_dib.dva, c);               /* write to memory */
        if (CHS_IFERR (st))                             /* channel error? */
            return mt_chan_err (st);
        if ((st != CHS_ZBC) && (mt_bptr != mt_blim)) {  /* not done? */
            sim_activate (uptr, mt_time);               /* continue thread */
            return SCPE_OK;
            }
        if (((st == CHS_ZBC) ^ (mt_bptr == mt_blim)) && /* length err? */ 
              chan_set_chf (mt_dib.dva, CHF_LNTE))      /* uend taken? */
            return SCPE_OK;                             /* finished */
        break;                                          /* normal end */

    case MCM_RDBK:                                      /* read reverse */
        if (mt_blim == 0) {                             /* first read? */
            r = sim_tape_rdrecr (uptr, mt_xb, &mt_blim, MT_MAXFR);
            if (r != MTSE_OK) {                         /* tape error? */
                r = mt_map_err (uptr, r);               /* map error */
                break;
                }
            mt_bptr = mt_blim;                          /* init rec ptr */
            }
        c = mt_xb[--mt_bptr];                           /* get char */
        st = chan_WrMemBR (mt_dib.dva, c);              /* write mem rev */
        if (CHS_IFERR (st))                             /* channel error? */
            return mt_chan_err (st);
        if ((st != CHS_ZBC) && (mt_bptr != 0)) {        /* not done? */
            sim_activate (uptr, mt_time);               /* continue thread */
            return SCPE_OK;
            }
        if (((st == CHS_ZBC) ^ (mt_bptr == 0)) &&       /* length err? */
              chan_set_chf (mt_dib.dva, CHF_LNTE))      /* uend taken? */
            return SCPE_OK;                             /* finished */
        break;                                          /* normal end */

    case MCM_WRITE:                                     /* write */
        st = chan_RdMemB (mt_dib.dva, &c);              /* read char */
        if (CHS_IFERR (st)) {                           /* channel error? */
            mt_flush_buf (uptr);                        /* flush buffer */
            return mt_chan_err (st);
            }
        mt_xb[mt_blim++] = c;                           /* store in buffer */
        if (st != CHS_ZBC) {                            /* end record? */
             sim_activate (uptr, mt_time);              /* continue thread */
             return SCPE_OK;
             }
        r = mt_flush_buf (uptr);                        /* flush buffer */
        break;
        }

if (r != SCPE_OK)                                       /* error? abort */
    return CHS_IFERR(r)? SCPE_OK: r;
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
t_stat st;

if (mt_blim == 0)                                       /* any output? */
    return SCPE_OK;
if ((st = sim_tape_wrrecf (uptr, mt_xb, mt_blim)))      /* write, err? */
    return mt_map_err (uptr, st);                       /* map error */
return SCPE_OK;
}

/* Map tape error status - returns chan error or SCP status */

t_stat mt_map_err (UNIT *uptr, t_stat st)
{
switch (st) {

    case MTSE_FMT:                                      /* illegal fmt */
    case MTSE_UNATT:                                    /* not attached */
    case MTSE_WRP:                                      /* write protect */
        chan_set_chf (mt_dib.dva, CHF_XMME);            /* set err, fall through */
    case MTSE_OK:                                       /* no error */
        chan_uen (mt_dib.dva);                          /* uend */
        return SCPE_IERR;

    case MTSE_TMK:                                      /* end of file */
        uptr->UST |= MTDV_EOF;                          /* set eof flag */
        chan_uen (mt_dib.dva);                          /* uend */
        return CHS_INACTV;

    case MTSE_IOERR:                                    /* IO error */
        uptr->UST |= MTDV_DTE;                          /* set DTE flag */
        chan_set_chf (mt_dib.dva, CHF_XMDE);
        chan_uen (mt_dib.dva);                          /* force uend */
        return SCPE_IOERR;

    case MTSE_INVRL:                                    /* invalid rec lnt */
        uptr->UST |= MTDV_DTE;                          /* set DTE flag */
        chan_set_chf (mt_dib.dva, CHF_XMDE);
        chan_uen (mt_dib.dva);                          /* force uend */
        return SCPE_MTRLNT;

    case MTSE_RECE:                                     /* record in error */
    case MTSE_EOM:                                      /* end of medium */
        uptr->UST |= MTDV_DTE;                          /* set DTE flag */
        return chan_set_chf (mt_dib.dva, CHF_XMDE);     /* possible error */

    case MTSE_BOT:                                      /* reverse into BOT */
        uptr->UST |= MTDV_BOT;                          /* set BOT */
        chan_uen (mt_dib.dva);                          /* uend */
        return CHS_INACTV;
        }                                               /* end switch */

return SCPE_OK;
}

/* MT status routine */

uint32 mt_tio_status (uint32 un)
{
uint32 i, st;
UNIT *uptr = &mt_unit[un];

st = (uptr->flags & UNIT_ATT)? DVS_AUTO: 0;             /* AUTO */
if (sim_is_active (uptr) ||                             /* unit busy */
    sim_is_active (uptr + MT_REW))                      /* or rewinding? */
    st |= DVS_DBUSY;
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

t_stat mt_chan_err (uint32 st)
{
chan_uen (mt_dib.dva);                                  /* uend */
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

/* Reset routine */

t_stat mt_reset (DEVICE *dptr)
{
uint32 i;

for (i = 0; i < MT_NUMDR; i++) {
    sim_cancel (&mt_unit[i]);                           /* stop unit */
    sim_cancel (&mt_unit[i + MT_REW]);                  /* stop rewind */
    mt_unit[i].UST = 0;
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

r = sim_tape_attach (uptr, cptr);
if (r != SCPE_OK) return r;
uptr->UST = MTDV_BOT;
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
