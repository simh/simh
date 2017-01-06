/* id_mt.c: Interdata magnetic tape simulator

   Copyright (c) 2001-2008, Robert M Supnik

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

   mt           M46-494 dual density 9-track magtape controller

   16-Feb-06    RMS     Added tape capacity checking
   18-Mar-05    RMS     Added attached test to detach routine
   07-Dec-04    RMS     Added read-only file support
   25-Apr-03    RMS     Revised for extended file support
   28-Mar-03    RMS     Added multiformat support
   28-Feb-03    RMS     Revised for magtape library
   20-Feb-03    RMS     Fixed read to stop selch on error

   Magnetic tapes are represented as a series of variable 8b records
   of the form:

        32b record length in bytes - exact number
        byte 0
        byte 1
        :
        byte n-2
        byte n-1
        32b record length in bytes - exact number

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a single record length of 0.
   End of tape is two consecutive end of file marks.
*/

#include "id_defs.h"
#include "sim_tape.h"

#define UST             u3                              /* unit status */
#define UCMD            u4                              /* unit command */
#define MT_MAXFR        (1 << 24)                       /* max transfer */

/* Command - in UCMD */

#define MTC_SPCR        0x11                            /* backspace */
#define MTC_SKFR        0x13                            /* space file rev */
#define MTC_CLR         0x20                            /* clear */
#define MTC_RD          0x21                            /* read */
#define MTC_WR          0x22                            /* write */
#define MTC_SKFF        0x23                            /* space file fwd */
#define MTC_WEOF        0x30                            /* write eof */
#define MTC_REW         0x38                            /* rewind */
#define MTC_MASK        0x3F
#define MTC_STOP1       0x40                            /* stop, set EOM */
#define MTC_STOP2       0x80                            /* stop, set NMTN */

/* Status byte, * = in UST */

#define STA_ERR         0x80                            /* error */
#define STA_EOF         0x40                            /* end of file */
#define STA_EOT         0x20                            /* *end of tape */
#define STA_NMTN        0x10                            /* *no motion */
#define STA_UFLGS       (STA_EOT|STA_NMTN)              /* unit flags */
#define STA_MASK        (STA_ERR|STA_EOF|STA_BSY|STA_EOM)
#define SET_EX          (STA_ERR|STA_EOF|STA_NMTN)

extern uint32 int_req[INTSZ], int_enb[INTSZ];

uint8 mtxb[MT_MAXFR];                                   /* xfer buffer */
uint32 mt_bptr = 0;                                     /* pointer */
uint32 mt_blnt = 0;                                     /* length */
uint32 mt_sta = 0;                                      /* status byte */
uint32 mt_db = 0;                                       /* data buffer */
uint32 mt_xfr = 0;                                      /* data xfr in prog */
uint32 mt_arm[MT_NUMDR] = { 0 };                        /* intr armed */
int32 mt_wtime = 10;                                    /* byte latency */
int32 mt_rtime = 1000;                                  /* record latency */
int32 mt_stopioe = 1;                                   /* stop on error */
uint8 mt_tplte[] = { 0, o_MT0, o_MT0*2, o_MT0*3, TPL_END };

static const uint8 bad_cmd[64] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1
    };

uint32 mt (uint32 dev, uint32 op, uint32 dat);
t_stat mt_svc (UNIT *uptr);
t_stat mt_reset (DEVICE *dptr);
t_stat mt_attach (UNIT *uptr, CONST char *cptr);
t_stat mt_detach (UNIT *uptr);
t_stat mt_boot (int32 unitno, DEVICE *dptr);
t_stat mt_map_err (UNIT *uptr, t_stat st);

/* MT data structures

   mt_dev       MT device descriptor
   mt_unit      MT unit list
   mt_reg       MT register list
   mt_mod       MT modifier list
*/

DIB mt_dib = { d_MT, 0, v_MT, mt_tplte, &mt, NULL };

UNIT mt_unit[] = {
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) }
    };

REG mt_reg[] = {
    { HRDATA (STA, mt_sta, 8) },
    { HRDATA (BUF, mt_db, 8) },
    { BRDATA (DBUF, mtxb, 16, 8, MT_MAXFR) },
    { HRDATA (DBPTR, mt_bptr, 16) },
    { HRDATA (DBLNT, mt_blnt, 17), REG_RO },
    { FLDATA (XFR, mt_xfr, 0) },
    { GRDATA (IREQ, int_req[l_MT], 16, MT_NUMDR, i_MT) },
    { GRDATA (IENB, int_enb[l_MT], 16, MT_NUMDR, i_MT) },
    { BRDATA (IARM, mt_arm, 16, 1, MT_NUMDR) },
    { FLDATA (STOP_IOE, mt_stopioe, 0) },
    { DRDATA (WTIME, mt_wtime, 24), PV_LEFT + REG_NZ },
    { DRDATA (RTIME, mt_rtime, 24), PV_LEFT + REG_NZ },
    { URDATA (UST, mt_unit[0].UST, 16, 8, 0, MT_NUMDR, 0) },
    { URDATA (CMD, mt_unit[0].UCMD, 16, 8, 0, MT_NUMDR, 0) },
    { URDATA (POS, mt_unit[0].pos, 10, T_ADDR_W, 0,
              MT_NUMDR, PV_LEFT | REG_RO) },
    { HRDATA (DEVNO, mt_dib.dno, 8), REG_HRO },
    { HRDATA (SELCH, mt_dib.sch, 1), REG_HRO },
    { NULL }
    };

MTAB mt_mod[] = {
    { MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL },
    { MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
      &sim_tape_set_fmt, &sim_tape_show_fmt, NULL },
    { MTAB_XTD|MTAB_VUN, 0, "CAPACITY", "CAPACITY",
      &sim_tape_set_capac, &sim_tape_show_capac, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "SELCH", "SELCH",
      &set_sch, &show_sch, NULL },
    { 0 }
    };

DEVICE mt_dev = {
    "MT", mt_unit, mt_reg, mt_mod,
    MT_NUMDR, 10, 31, 1, 16, 8,
    NULL, NULL, &mt_reset,
    &mt_boot, &mt_attach, &mt_detach,
    &mt_dib, DEV_DISABLE | DEV_TAPE
    };

/* Magtape: IO routine */

uint32 mt (uint32 dev, uint32 op, uint32 dat)
{
uint32 i, f, t;
uint32 u = (dev - mt_dib.dno) / o_MT0;
UNIT *uptr = mt_dev.units + u;

switch (op) {                                           /* case IO op */

    case IO_ADR:                                        /* select */
        sch_adr (mt_dib.sch, dev);                      /* inform sel ch */
        return BY;                                      /* byte only */

    case IO_RD:                                         /* read data */
        if (mt_xfr)                                     /* xfr? set busy */
            mt_sta = mt_sta | STA_BSY;
        return mt_db;                                   /* return data */

    case IO_WD:                                         /* write data */
        if (mt_xfr) {                                   /* transfer? */
            mt_sta = mt_sta | STA_BSY;                  /* set busy */
            if ((uptr->UCMD & (MTC_STOP1 | MTC_STOP2)) &&
                ((uptr->UCMD & MTC_MASK) == MTC_WR))    /* while stopping? */
                mt_sta = mt_sta | STA_ERR;              /* write overrun */
            }
        mt_db = dat & DMASK8;                           /* store data */
        break;

    case IO_SS:                                         /* status */
        mt_sta = mt_sta & STA_MASK;                     /* ctrl status */
        if (uptr->flags & UNIT_ATT)                     /* attached? */
            t = mt_sta | (uptr->UST & STA_UFLGS);       /* yes, unit status */
        else t = mt_sta | STA_DU;                       /* no, dev unavail */
        if (t & SET_EX)                                 /* test for ex */
            t = t | STA_EX;
        return t;

    case IO_OC:                                         /* command */
        mt_arm[u] = int_chg (v_MT + u, dat, mt_arm[u]);
        f = dat & MTC_MASK;                             /* get cmd */
        if (f == MTC_CLR) {                             /* clear? */
            mt_reset (&mt_dev);                         /* reset world */
            break;
            }
        if (((uptr->flags & UNIT_ATT) == 0) ||          /* ignore if unatt */
            bad_cmd[f] ||                               /* or bad cmd */
           (((f == MTC_WR) || (f == MTC_WEOF)) &&       /* or write */
            sim_tape_wrp (uptr)))                       /* and protected */
            break;
        for (i = 0; i < MT_NUMDR; i++) {                /* check other drvs */
            if (sim_is_active (&mt_unit[i]) &&          /* active? */
                (mt_unit[i].UCMD != MTC_REW)) {         /* not rewind? */
                sim_cancel (&mt_unit[i]);               /* stop */
                mt_unit[i].UCMD = 0;
                }
            }
        if (sim_is_active (uptr) &&                     /* unit active? */
           !(uptr->UCMD & (MTC_STOP1 | MTC_STOP2)))     /* not stopping? */
            break;                                      /* ignore */
        if ((f == MTC_WR) || (f == MTC_REW))            /* write, rew: bsy=0 */
            mt_sta = 0;
        else mt_sta = STA_BSY;                          /* bsy=1,nmtn,eom,err=0 */
        mt_bptr = mt_blnt = 0;                          /* not yet started */
        if ((f == MTC_RD) || (f == MTC_WR))             /* data xfr? */
            mt_xfr =  1;                                /* set xfr flag */
        else mt_xfr = 0;
        uptr->UCMD = f;                                 /* save cmd */
        uptr->UST = 0;                                  /* clr tape stat */
        sim_activate (uptr, mt_rtime);                  /* start op */
        break;
        }

return 0;
}

/* Unit service

   A given operation can generate up to three interrupts

   - EOF generates an interrupt when set (read, space, wreof)
     BUSY will still be set, EOM and NMTN will be clear
   - After operation complete + delay, EOM generates an interrupt
     BUSY will be clear, EOM will be set, NMTN will be clear
   - After a further delay, NMTN generates an interrupt
     BUSY will be clear, EOM and NMTN will be set

   Rewind generates an interrupt when NMTN sets
*/

t_stat mt_svc (UNIT *uptr)
{
uint32 i;
int32 u = uptr - mt_dev.units;
uint32 dev = mt_dib.dno + (u * o_MT0);
t_mtrlnt tbc;
t_bool passed_eot;
t_stat st, r = SCPE_OK;

if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
    uptr->UCMD = 0;                                     /* clr cmd */
    uptr->UST = 0;                                      /* set status */
    mt_xfr = 0;                                         /* clr op flags */
    mt_sta = STA_ERR | STA_EOM;                         /* set status */
    if (mt_arm[u])                                      /* interrupt */
        SET_INT (v_MT + u);
    return IORETURN (mt_stopioe, SCPE_UNATT);
    }

if (uptr->UCMD & MTC_STOP2) {                           /* stop, gen NMTN? */
    uptr->UCMD = 0;                                     /* clr cmd */
    uptr->UST = uptr->UST | STA_NMTN;                   /* set nmtn */
    mt_xfr = 0;                                         /* clr xfr */
    if (mt_arm[u])                                      /* set intr */
        SET_INT (v_MT + u);
    return SCPE_OK;
    }

if (uptr->UCMD & MTC_STOP1) {                           /* stop, gen EOM? */
    uptr->UCMD = uptr->UCMD | MTC_STOP2;                /* clr cmd */
    mt_sta = (mt_sta & ~STA_BSY) | STA_EOM;             /* clr busy, set eom */
    if (mt_arm[u])                                      /* set intr */
        SET_INT (v_MT + u);
    sim_activate (uptr, mt_rtime);                      /* schedule */
    return SCPE_OK;
    }

passed_eot = sim_tape_eot (uptr);                       /* passed EOT? */
switch (uptr->UCMD) {                                   /* case on function */

    case MTC_REW:                                       /* rewind */
        sim_tape_rewind (uptr);                         /* reposition */
        uptr->UCMD = 0;                                 /* clr cmd */
        uptr->UST = STA_NMTN | STA_EOT;                 /* update status */
        mt_sta = mt_sta & ~STA_BSY;                     /* don't set EOM */
        if (mt_arm[u])                                  /* interrupt */
            SET_INT (v_MT + u);
        return SCPE_OK;

/* For read, busy = 1 => buffer empty
   For write, busy = 1 => buffer full
   For read, data transfers continue for the full length of the
        record, or the maximum size of the transfer buffer
   For write, data transfers continue until a write is attempted
        and the buffer is empty
*/

    case MTC_RD:                                        /* read */
        if (mt_blnt == 0) {                             /* first time? */
            st = sim_tape_rdrecf (uptr, mtxb, &tbc, MT_MAXFR); /* read rec */
            if (st == MTSE_RECE)                        /* rec in err? */
                mt_sta = mt_sta | STA_ERR;
            else if (st != SCPE_OK) {                   /* other error? */
                r = mt_map_err (uptr, st);              /* map error */
                if (sch_actv (mt_dib.sch, dev))         /* if sch, stop */
                    sch_stop (mt_dib.sch);
                break;
                }
            mt_blnt = tbc;                              /* set buf lnt */
            }

        if (sch_actv (mt_dib.sch, dev)) {               /* sch active? */
            i = sch_wrmem (mt_dib.sch, mtxb, mt_blnt);  /* store rec in mem */
            if (sch_actv (mt_dib.sch, dev))             /* sch still active? */
                sch_stop (mt_dib.sch);                  /* stop chan, long rd */
            else if (i < mt_blnt)                       /* process entire rec? */
                mt_sta = mt_sta | STA_ERR;              /* no, overrun error */
            }
        else if (mt_bptr < mt_blnt) {                   /* no, if !eor */
            if (!(mt_sta & STA_BSY))                    /* busy still clr? */
                mt_sta = mt_sta | STA_ERR;              /* read overrun */
            mt_db = mtxb[mt_bptr++];                    /* get next byte */
            mt_sta = mt_sta & ~STA_BSY;                 /* !busy = buf full */
            if (mt_arm[u])                              /* set intr */
                SET_INT (v_MT + u);
            sim_activate (uptr, mt_wtime);              /* reschedule */
            return SCPE_OK;
            }
        break;                                          /* record done */

    case MTC_WR:                                        /* write */
        if (sch_actv (mt_dib.sch, dev)) {               /* sch active? */
            mt_bptr = sch_rdmem (mt_dib.sch, mtxb, MT_MAXFR); /* get rec */
            if (sch_actv (mt_dib.sch, dev))             /* not done? */
                sch_stop (mt_dib.sch);                  /* stop chan */
            }
        else if (mt_sta & STA_BSY) {                    /* no, if !eor */
            if (mt_bptr < MT_MAXFR)                     /* if room */
                mtxb[mt_bptr++] = mt_db;                /* store in buf */
            mt_sta = mt_sta & ~STA_BSY;                 /* !busy = buf emp */
            if (mt_arm[u])                              /* set intr */
                SET_INT (v_MT + u);
            sim_activate (uptr, mt_wtime);              /* reschedule */
            return SCPE_OK;
            }

        if (mt_bptr) {                                  /* any chars? */
            if ((st = sim_tape_wrrecf (uptr, mtxb, mt_bptr)))/* write, err? */
                r = mt_map_err (uptr, st);              /* map error */
            }
        break;                                          /* record done */

    case MTC_WEOF:                                      /* write eof */
        if ((st = sim_tape_wrtmk (uptr)))               /* write tmk, err? */
            r = mt_map_err (uptr, st);                  /* map error */
        mt_sta = mt_sta | STA_EOF;                      /* set eof */
        if (mt_arm[u])                                  /* set intr */
            SET_INT (v_MT + u);
        break;

    case MTC_SKFF:                                      /* skip file fwd */
        while ((st = sim_tape_sprecf (uptr, &tbc)) == MTSE_OK) ;
        if (st == MTSE_TMK) {                           /* stopped by tmk? */
            mt_sta = mt_sta | STA_EOF;                  /* set eof */
            if (mt_arm[u])                              /* set intr */
                SET_INT (v_MT + u);
            }
        else r = mt_map_err (uptr, st);                 /* map error */
        break;

    case MTC_SKFR:                                      /* skip file rev */
        while ((st = sim_tape_sprecr (uptr, &tbc)) == MTSE_OK) ;
        if (st == MTSE_TMK) {                           /* stopped by tmk? */
            mt_sta = mt_sta | STA_EOF;                  /* set eof */
            if (mt_arm[u])                              /* set intr */
                SET_INT (v_MT + u);
            }
        else r = mt_map_err (uptr, st);                 /* map error */
        break;

    case MTC_SPCR:                                      /* backspace */
        if ((st = sim_tape_sprecr (uptr, &tbc)))        /* skip rec rev, err? */
            r = mt_map_err (uptr, st);                  /* map error */
        break;
        }                                               /* end case */

if (!passed_eot && sim_tape_eot (uptr))                 /* just passed EOT? */
    uptr->UST = uptr->UST | STA_EOT;
uptr->UCMD = uptr->UCMD | MTC_STOP1;                    /* set stop stage 1 */
sim_activate (uptr, mt_rtime);                          /* schedule */
return r;
}

/* Map tape error status */

t_stat mt_map_err (UNIT *uptr, t_stat st)
{
int32 u = uptr - mt_dev.units;

switch (st) {

    case MTSE_FMT:                                      /* illegal fmt */
    case MTSE_UNATT:                                    /* not attached */
        mt_sta = mt_sta | STA_ERR;
    case MTSE_OK:                                       /* no error */
        return SCPE_IERR;

    case MTSE_TMK:                                      /* end of file */
        mt_sta = mt_sta | STA_EOF;                      /* set eof */
        if (mt_arm[u])                                  /* set intr */
            SET_INT (v_MT + u);
        break;

    case MTSE_IOERR:                                    /* IO error */
        mt_sta = mt_sta | STA_ERR;                      /* set err */
        if (mt_stopioe)
            return SCPE_IOERR;
        break;

    case MTSE_INVRL:                                    /* invalid rec lnt */
        mt_sta = mt_sta | STA_ERR;
        return SCPE_MTRLNT;

    case MTSE_WRP:                                      /* write protect */
    case MTSE_RECE:                                     /* record in error */
    case MTSE_EOM:                                      /* end of medium */
        mt_sta = mt_sta | STA_ERR;                      /* set err */
        break;

    case MTSE_BOT:                                      /* reverse into BOT */
        uptr->UST = uptr->UST | STA_EOT;                /* set err */
        break;
        }                                               /* end switch */

return SCPE_OK;
}

/* Reset routine */

t_stat mt_reset (DEVICE *dptr)
{
uint32 u;
UNIT *uptr;

mt_bptr = mt_blnt = 0;                                  /* clr buf */
mt_sta = STA_BSY;                                       /* clr flags */
mt_xfr = 0;                                             /* clr controls */
for (u = 0; u < MT_NUMDR; u++) {                        /* loop thru units */
    CLR_INT (v_MT + u);                                 /* clear int */
    CLR_ENB (v_MT + u);                                 /* disable int */
    mt_arm[u] = 0;                                      /* disarm int */
    uptr = mt_dev.units + u;
    sim_tape_reset (uptr);                              /* clear pos flag */
    sim_cancel (uptr);                                  /* cancel activity */
    uptr->UST = (uptr->UST & STA_UFLGS) | STA_NMTN;     /* init status */
    uptr->UCMD = 0;                                     /* init cmd */
    }
return SCPE_OK;
}

/* Attach routine */

t_stat mt_attach (UNIT *uptr, CONST char *cptr)
{
int32 u = uptr - mt_dev.units;
t_stat r;

r = sim_tape_attach (uptr, cptr);
if (r != SCPE_OK)
    return r;
uptr->UST = STA_EOT;
if (mt_arm[u])
    SET_INT (v_MT + u);
return r;
}

/* Detach routine */

t_stat mt_detach (UNIT* uptr)
{
int32 u = uptr - mt_dev.units;
t_stat r;

if (!(uptr->flags & UNIT_ATT))
    return SCPE_OK;
r = sim_tape_detach (uptr);
if (r != SCPE_OK)
    return r;
if (mt_arm[u])
    SET_INT (v_MT + u);
uptr->UST = 0;
return SCPE_OK;
}

/* Bootstrap routine */

#define BOOT_START      0x50
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (uint8))

static uint8 boot_rom[] = {
    0xD5, 0x00, 0x00, 0xCF,                             /* ST:  AL CF */
    0x43, 0x00, 0x00, 0x80                              /*      BR 80 */
    };

t_stat mt_boot (int32 unitno, DEVICE *dptr)
{
extern DIB sch_dib;
uint32 sch_dev;

if (decrom[0xD5] & dec_flgs)                            /* AL defined? */
    return SCPE_NOFNC;
sim_tape_rewind (&mt_unit[unitno]);                     /* rewind */
sch_dev = sch_dib.dno + mt_dib.sch;                     /* sch dev # */
IOWriteBlk (BOOT_START, BOOT_LEN, boot_rom);            /* copy boot */
IOWriteB (AL_DEV, mt_dib.dno + (unitno * o_MT0));       /* set dev no for unit */
IOWriteB (AL_IOC, 0xA1);                                /* set dev cmd */
IOWriteB (AL_SCH, sch_dev);                             /* set dev no for chan */
PC = BOOT_START;
return SCPE_OK;
}
