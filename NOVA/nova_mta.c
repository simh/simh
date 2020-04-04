/* nova_mta.c: NOVA magnetic tape simulator

   Copyright (c) 1993-2017, Robert M. Supnik

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

   mta          magnetic tape

   13-Mar-17    RMS     Annotated fall through in switch
   04-Jul-07    BKR     fixed boot code to properly boot self-boot tapes;
                        boot routine now uses standard DG APL boot code;
                        device name changed to DG's MTA from DEC's MT.
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   18-Mar-05    RMS     Added attached test to detach routine
   22-Nov-03    CEO     DIB returns # records skipped after space fwd
   22-Nov-03    CEO     Removed cancel of tape events in IORST
   25-Apr-03    RMS     Revised for extended file support
   28-Mar-03    RMS     Added multiformat support
   28-Feb-03    RMS     Revised for magtape library
   30-Oct-02    RMS     Fixed BOT handling, added error record handling
   08-Oct-02    RMS     Added DIB
   30-Sep-02    RMS     Revamped error handling
   28-Aug-02    RMS     Added end of medium support
   30-May-02    RMS     Widened POS to 32b
   22-Apr-02    RMS     Added maximum record length test
   06-Jan-02    RMS     Revised enable/disable support
   30-Nov-01    RMS     Added read only unit, extended SET/SHOW support
   24-Nov-01    RMS     Changed POS, USTAT, FLG to an array
   26-Apr-01    RMS     Added device enable/disable support
   18-Apr-01    RMS     Changed to rewind tape before boot
   10-Dec-00    RMS     Added Eclipse support (Charles Owen)
   15-Oct-00    RMS     Editorial changes
   11-Nov-98    CEO     Removed clear of mta_ma on iopC 
   04-Oct-98    RMS     V2.4 magtape format
   18-Jan-97    RMS     V2.3 magtape format
   29-Jun-96    RMS     Added unit enable/disable support

   Magnetic tapes are represented as a series of variable records
   of the form:

        32b byte count                  byte count is little endian
        byte 0
        byte 1
        :
        byte n-2
        byte n-1
        32b byte count

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a byte count of 0 and are
   not duplicated; end of tape by end of file.
*/

#include "nova_defs.h"
#include "sim_tape.h"

#define MTA_NUMDR       8                               /* #drives */
#define USTAT           u3                              /* unit status */
#define MTA_MAXFR       (1 << 16)                       /* max record lnt */
#define WC_SIZE         (1 << 14)                       /* max word count */
#define WC_MASK         (WC_SIZE - 1)

/* Command/unit */

#define CU_CI           0100000                         /* clear interrupt */
#define CU_EP           0002000                         /* poll enable */
#define CU_DE           0001000                         /* disable erase */
#define CU_DA           0000400                         /* disable autoretry */
#define CU_PE           0000400                         /* PE mode */
#define CU_V_CMD        3                               /* command */
#define CU_M_CMD        027
#define  CU_READ         000
#define  CU_REWIND       001
#define  CU_CMODE        002
#define  CU_SPACEF       003
#define  CU_SPACER       004
#define  CU_WRITE        005
#define  CU_WREOF        006
#define  CU_ERASE        007
#define  CU_READNS       020
#define  CU_UNLOAD       021
#define  CU_DMODE        022
#define CU_V_UNIT       0                               /* unit */
#define CU_M_UNIT       07
#define GET_CMD(x)      (((x) >> CU_V_CMD) & CU_M_CMD)
#define GET_UNIT(x)     (((x) >> CU_V_UNIT) & CU_M_UNIT)

/* Status 1 - stored in mta_sta<31:16> or (*) uptr->USTAT<31:16> */

#define STA_ERR1        (0100000u << 16)                /* error */
#define STA_DLT         (0040000 << 16)                 /* data late */
#define STA_REW         (0020000 << 16)                 /* *rewinding */
#define STA_ILL         (0010000 << 16)                 /* illegal */
#define STA_HDN         (0004000 << 16)                 /* high density */
#define STA_DAE         (0002000 << 16)                 /* data error */
#define STA_EOT         (0001000 << 16)                 /* *end of tape */
#define STA_EOF         (0000400 << 16)                 /* *end of file */
#define STA_BOT         (0000200 << 16)                 /* *start of tape */
#define STA_9TK         (0000100 << 16)                 /* nine track */
#define STA_BAT         (0000040 << 16)                 /* bad tape */
#define STA_CHG         (0000010 << 16)                 /* status change */
#define STA_WLK         (0000004 << 16)                 /* *write lock */
#define STA_ODD         (0000002 << 16)                 /* odd character */
#define STA_RDY         (0000001 << 16)                 /* *drive ready */

/* Status 2 - stored in mta_sta<15:0> or (*) uptr->USTAT<15:0> */

#define STA_ERR2        0100000                         /* error */
#define STA_RWY         0040000                         /* runaway tape */
#define STA_FGP         0020000                         /* false gap */
#define STA_CDL         0004000                         /* corrected dlt */
#define STA_V_UNIT      8
#define STA_M_UNIT      07                              /* unit */
#define STA_WCO         0000200                         /* word count ovflo */
#define STA_BDS         0000100                         /* bad signal */
#define STA_OVS         0000040                         /* overskew */
#define STA_CRC         0000020                         /* check error */
#define STA_STE         0000010                         /* single trk error */
#define STA_FPR         0000004                         /* false preamble */
#define STA_FMT         0000002                         /* format error */
#define STA_PEM         0000001                         /* *PE mode */

#define STA_EFLGS1      (STA_DLT | STA_ILL | STA_DAE | STA_EOT | \
                         STA_EOF | STA_BOT | STA_BAT | STA_ODD)
#define STA_EFLGS2      (STA_FGP | STA_CDL | STA_BDS | STA_OVS | \
                         STA_CRC | STA_FPR | STA_FPR)   /* set error 2 */
#define STA_CLR         ((020 << 16) | 0010000)         /* always clear */
#define STA_SET         (STA_HDN | STA_9TK)             /* always set */
#define STA_DYN         (STA_REW | STA_EOT | STA_EOF | STA_BOT | \
                         STA_WLK | STA_RDY | STA_PEM)   /* kept in USTAT */
#define STA_MON         (STA_REW | STA_BOT | STA_WLK | STA_RDY | \
                         STA_PEM)                       /* set status chg */

extern uint16 M[];
extern UNIT cpu_unit;
extern int32 int_req, dev_busy, dev_done, dev_disable;
extern int32 SR, AMASK;

extern t_stat  cpu_boot(int32 unitno, DEVICE * dptr ) ;


int32 mta_ma = 0;                                       /* memory address */
int32 mta_wc = 0;                                       /* word count */
int32 mta_cu = 0;                                       /* command/unit */
int32 mta_sta = 0;                                      /* status register */
int32 mta_ep = 0;                                       /* enable polling */
int32 mta_cwait = 100;                                  /* command latency */
int32 mta_rwait = 100;                                  /* record latency */
uint8 *mtxb = NULL;                                     /* transfer buffer */

int32 mta (int32 pulse, int32 code, int32 AC);
t_stat mta_svc (UNIT *uptr);
t_stat mta_reset (DEVICE *dptr);
t_stat mta_boot (int32 unitno, DEVICE *dptr);
t_stat mta_attach (UNIT *uptr, CONST char *cptr);
t_stat mta_detach (UNIT *uptr);
int32 mta_updcsta (UNIT *uptr);
void mta_upddsta (UNIT *uptr, int32 newsta);
t_stat mta_map_err (UNIT *uptr, t_stat st);
t_stat mta_vlock (UNIT *uptr, int32 val, CONST char *cptr, void *desc);

static const int ctype[32] = {                          /* c vs r timing */
 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1
 };

/* MTA data structures

   mta_dev      MTA device descriptor
   mta_unit     MTA unit list
   mta_reg      MTA register list
   mta_mod      MTA modifier list
*/

DIB mta_dib = { DEV_MTA, INT_MTA, PI_MTA, &mta };

UNIT mta_unit[] = {
    { UDATA (&mta_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&mta_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&mta_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&mta_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&mta_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&mta_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&mta_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&mta_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) }
    };

REG mta_reg[] = {
    { ORDATA (CU, mta_cu, 16) },
    { ORDATA (MA, mta_ma, 16) },
    { ORDATA (WC, mta_wc, 16) },
    { GRDATA (STA1, mta_sta, 8, 16, 16) },
    { ORDATA (STA2, mta_sta, 16) },
    { FLDATA (EP, mta_ep, 0) },
    { FLDATA (BUSY, dev_busy, INT_V_MTA) },
    { FLDATA (DONE, dev_done, INT_V_MTA) },
    { FLDATA (DISABLE, dev_disable, INT_V_MTA) },
    { FLDATA (INT, int_req, INT_V_MTA) },
    { DRDATA (CTIME, mta_cwait, 24), PV_LEFT },
    { DRDATA (RTIME, mta_rwait, 24), PV_LEFT },
    { URDATA (UST, mta_unit[0].USTAT, 8, 32, 0, MTA_NUMDR, 0) },
    { URDATA (POS, mta_unit[0].pos, 8, T_ADDR_W, 0,
              MTA_NUMDR, REG_RO | PV_LEFT) },
    { NULL }
    };

MTAB mta_mod[] = {
    { MTUF_WLK, 0, "write enabled", "WRITEENABLED", &mta_vlock },
    { MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", &mta_vlock },
    { MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
      &sim_tape_set_fmt, &sim_tape_show_fmt, NULL },
    { 0 }
    };

DEVICE mta_dev = {
    "MTA", mta_unit, mta_reg, mta_mod,
    MTA_NUMDR, 10, 31, 1, 8, 8,
    NULL, NULL, &mta_reset,
    &mta_boot, &mta_attach, &mta_detach,
    &mta_dib, DEV_DISABLE | DEV_TAPE
    };

/* IOT routine */

int32 mta (int32 pulse, int32 code, int32 AC)
{
UNIT *uptr;
int32 u, c, rval;

rval = 0;
uptr = mta_dev.units + GET_UNIT(mta_cu);                /* get unit */
switch (code) {                                         /* decode IR<5:7> */

    case ioDIA:                                         /* DIA */
        rval = (mta_updcsta (uptr) >> 16) & DMASK;      /* return status 1 */
        break;

    case ioDOA:                                         /* DOA */
/*      if (AC & CU_CI) ... clear ep int */
        mta_cu = AC;                                    /* save cmd/unit */
        uptr = mta_dev.units + GET_UNIT(mta_cu);        /* get unit */
        mta_updcsta (uptr);                             /* update status */
        break;

    case ioDIB:                                         /* DIB */
        rval = mta_ma & AMASK;                          /* return ma */
        break;

    case ioDOB:                                         /* DOB */
        mta_ma = AC & AMASK;                            /* save ma */
        break;

    case ioDIC:                                         /* DIC */
        rval = mta_updcsta (uptr) & DMASK;              /* return status 2 */
        break;

    case ioDOC:                                         /* DOC */
        mta_wc = ((AC & 040000) << 1) | (AC & 077777);  /* save wc */
        break;
        }                                               /* end switch code */

switch (pulse) {                                        /* decode IR<8:9> */

    case iopS:                                          /* start */
        c = GET_CMD (mta_cu);                           /* get command */
        if (dev_busy & INT_MTA)                         /* ignore if busy */
            break;
        if ((uptr->USTAT & STA_RDY) == 0) {             /* drive not ready? */
            mta_sta = mta_sta | STA_ILL;                /* illegal op */
            dev_busy = dev_busy & ~INT_MTA;             /* clear busy */
            dev_done = dev_done | INT_MTA;              /* set done */
            int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
            }
        else if ((c == CU_REWIND) || (c == CU_UNLOAD)) { /* rewind, unload? */
            mta_upddsta (uptr, (uptr->USTAT &           /* update status */
                ~(STA_BOT | STA_EOF | STA_EOT | STA_RDY)) | STA_REW);
            sim_activate (uptr, mta_rwait);             /* start IO */
            if (c == CU_UNLOAD)
                sim_tape_detach (uptr);
            }
        else {
            mta_sta = 0;                                /* clear errors */
            dev_busy = dev_busy | INT_MTA;              /* set busy */
            dev_done = dev_done & ~INT_MTA;             /* clear done */
            int_req = int_req & ~INT_MTA;               /* clear int */
            if (ctype[c])
                sim_activate (uptr, mta_cwait);
            else {
                mta_upddsta (uptr, uptr->USTAT &
                   ~(STA_BOT | STA_EOF | STA_EOT | STA_RDY));
                sim_activate (uptr, mta_rwait);
                }
            }
        mta_updcsta (uptr);                             /* update status */
        break;

    case iopC:                                          /* clear */
        for (u = 0; u < MTA_NUMDR; u++) {               /* loop thru units */
            uptr = mta_dev.units + u;                   /* cancel IO */
            if (sim_is_active (uptr) && !(uptr->USTAT & STA_REW)) {
                mta_upddsta (uptr, uptr->USTAT | STA_RDY);
                sim_cancel (uptr);
                }
            }
        dev_busy = dev_busy & ~INT_MTA;                 /* clear busy */
        dev_done = dev_done & ~INT_MTA;                 /* clear done */
        int_req = int_req & ~INT_MTA;                   /* clear int */
        mta_sta = mta_cu = 0;                           /* clear registers */
        mta_updcsta (&mta_unit[0]);                     /* update status */
        break;
        }                                               /* end case pulse */

return rval;
}

/* Unit service

   If rewind done, reposition to start of tape, set status
   else, do operation, clear busy, set done, interrupt
*/

t_stat mta_svc (UNIT *uptr)
{
int32 c, p, pa, u;
t_mtrlnt i, cbc, tbc, wc;
uint16 c1, c2;
t_stat st, r = SCPE_OK;

u = uptr - mta_dev.units;                               /* get unit number */
c = GET_CMD (mta_cu);                                   /* command */
wc = WC_SIZE - (mta_wc & WC_MASK);                      /* io wc */

if (uptr->USTAT & STA_REW) {                            /* rewind? */
    sim_tape_rewind (uptr);                             /* update tape */
    mta_upddsta (uptr, (uptr->USTAT & ~STA_REW) | STA_BOT | STA_RDY);
    if (u == GET_UNIT (mta_cu))
        mta_updcsta (uptr);
    return SCPE_OK;
    }

if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
    mta_upddsta (uptr, 0);                              /* unit off line */
    mta_sta = mta_sta | STA_ILL;                        /* illegal operation */
    }
else switch (c) {                                       /* case on command */

    case CU_CMODE:                                      /* controller mode */
        mta_ep = mta_cu & CU_EP;
        break;

    case CU_DMODE:                                      /* drive mode */
        if (!sim_tape_bot (uptr))                       /* must be BOT */
            mta_sta = mta_sta | STA_ILL;
        else mta_upddsta (uptr, (mta_cu & CU_PE)?       /* update drv status */
            uptr->USTAT | STA_PEM: uptr->USTAT & ~ STA_PEM);
        break;

    case CU_READ:                                       /* read */
    case CU_READNS:                                     /* read non-stop */
        st = sim_tape_rdrecf (uptr, mtxb, &tbc, MTA_MAXFR); /* read rec */
        if (st == MTSE_RECE)                            /* rec in err? */
            mta_sta = mta_sta | STA_DAE;
        else if (st != MTSE_OK) {                       /* other error? */
            r = mta_map_err (uptr, st);                 /* map error */
            break;
            }
        cbc = wc * 2;                                   /* expected bc */
        if (tbc & 1)                                    /* odd byte count? */
            mta_sta = mta_sta | STA_ODD;
        if (tbc > cbc)                                  /* too big? */
            mta_sta = mta_sta | STA_WCO;
        else {
            cbc = tbc;                                  /* no, use it */
            wc = (cbc + 1) / 2;                         /* adjust wc */
            }
        for (i = p = 0; i < wc; i++) {                  /* copy buf to mem */
            c1 = mtxb[p++];
            c2 = mtxb[p++];
            pa = MapAddr (0, mta_ma);                   /* map address */
            if (MEM_ADDR_OK (pa))
                M[pa] = (c1 << 8) | c2;
            mta_ma = (mta_ma + 1) & AMASK;
            }
        mta_wc = (mta_wc + wc) & DMASK;
        mta_upddsta (uptr, uptr->USTAT | STA_RDY);
        break;

    case CU_WRITE:                                      /* write */
        tbc = wc * 2;                                   /* io byte count */
        for (i = p = 0; i < wc; i++) {                  /* copy to buffer */
            pa = MapAddr (0, mta_ma);                   /* map address */
            mtxb[p++] = (M[pa] >> 8) & 0377;
            mtxb[p++] = M[pa] & 0377;
            mta_ma = (mta_ma + 1) & AMASK;
            }
        if ((st = sim_tape_wrrecf (uptr, mtxb, tbc))) { /* write rec, err? */
            r = mta_map_err (uptr, st);                 /* map error */
            mta_ma = (mta_ma - wc) & AMASK;             /* restore wc */
            }
        else mta_wc = 0;                                /* clear wc */
        mta_upddsta (uptr, uptr->USTAT | STA_RDY);
        break;

    case CU_WREOF:                                      /* write eof */
        if ((st = sim_tape_wrtmk (uptr)))               /* write tmk, err? */
            r = mta_map_err (uptr, st);                 /* map error */
        else mta_upddsta (uptr, uptr->USTAT | STA_EOF | STA_RDY);
        break;

    case CU_ERASE:                                      /* erase */
        if (sim_tape_wrp (uptr))                        /* write protected? */
            r = mta_map_err (uptr, MTSE_WRP);           /* map error */
        else mta_upddsta (uptr, uptr->USTAT | STA_RDY);
        break;

    case CU_SPACEF:                                     /* space forward */
        do {
            mta_wc = (mta_wc + 1) & DMASK;              /* incr wc */
            if ((st = sim_tape_sprecf (uptr, &tbc))) {  /* space rec fwd, err? */
                r = mta_map_err (uptr, st);             /* map error */
                break;
                }
            } while (mta_wc != 0);
        mta_upddsta (uptr, uptr->USTAT | STA_RDY);
        mta_ma = mta_wc;                                /* word count = # records */
        break;

    case CU_SPACER:                                     /* space reverse */
        do {
            mta_wc = (mta_wc + 1) & DMASK;              /* incr wc */
            if ((st = sim_tape_sprecr (uptr, &tbc))) {  /* space rec rev, err? */
                r = mta_map_err (uptr, st);             /* map error */
                break;
                }
            } while (mta_wc != 0);
        mta_upddsta (uptr, uptr->USTAT | STA_RDY);
        mta_ma = mta_wc;                                /* word count = # records */
        break;

    default:                                            /* reserved */
        mta_sta = mta_sta | STA_ILL;
        mta_upddsta (uptr, uptr->USTAT | STA_RDY);
        break;
        }                                               /* end case */

mta_updcsta (uptr);                                     /* update status */
dev_busy = dev_busy & ~INT_MTA;                         /* clear busy */
dev_done = dev_done | INT_MTA;                          /* set done */
int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
return r;
}

/* Update controller status */

int32 mta_updcsta (UNIT *uptr)                          /* update ctrl */
{
mta_sta = (mta_sta & ~(STA_DYN | STA_CLR | STA_ERR1 | STA_ERR2)) |
    (uptr->USTAT & STA_DYN) | STA_SET;
if (mta_sta & STA_EFLGS1)
    mta_sta = mta_sta | STA_ERR1;
if (mta_sta & STA_EFLGS2)
    mta_sta = mta_sta | STA_ERR2;
return mta_sta;
}

/* Update drive status */

void mta_upddsta (UNIT *uptr, int32 newsta)             /* drive status */
{
int32 change;

if ((uptr->flags & UNIT_ATT) == 0)                      /* offline? */
    newsta = 0;
change = (uptr->USTAT ^ newsta) & STA_MON;              /* changes? */
uptr->USTAT = newsta & STA_DYN;                         /* update status */
if (change) {
/*  if (mta_ep) {                                     *//* if polling */
/*      u = uptr - mta_dev.units;                     *//* unit num */
/*      mta_sta = (mta_sta & ~STA_UNIT) | (u << STA_V_UNIT); */
/*      set polling interupt...                         */
/*      }                                               */
    mta_sta = mta_sta | STA_CHG;                        /* flag change */
    }
return;
}

/* Map tape error status */

t_stat mta_map_err (UNIT *uptr, t_stat st)
{
switch (st) {

    case MTSE_FMT:                                      /* illegal fmt */
        mta_upddsta (uptr, uptr->USTAT | STA_WLK | STA_RDY);
        /* fall through */
    case MTSE_UNATT:                                    /* unattached */
        mta_sta = mta_sta | STA_ILL;
        /* fall through */
    case MTSE_OK:                                       /* no error */
        return SCPE_IERR;                               /* never get here! */

    case MTSE_TMK:                                      /* end of file */
        mta_upddsta (uptr, uptr->USTAT | STA_RDY | STA_EOF);
        break;

    case MTSE_IOERR:                                    /* IO error */
        mta_sta = mta_sta | STA_DAE;                    /* data error */
        mta_upddsta (uptr, uptr->USTAT | STA_RDY);      /* ready */
        return SCPE_IOERR;

    case MTSE_INVRL:                                    /* invalid rec lnt */
        mta_sta = mta_sta | STA_DAE;                    /* data error */
        mta_upddsta (uptr, uptr->USTAT | STA_RDY);      /* ready */
        return SCPE_MTRLNT;

    case MTSE_RECE:                                     /* record in error */
        mta_sta = mta_sta | STA_DAE;                    /* data error */
        mta_upddsta (uptr, uptr->USTAT | STA_RDY);      /* ready */
        break;

    case MTSE_EOM:                                      /* end of medium */
        mta_sta = mta_sta | STA_BAT;                    /* bad tape */
        mta_upddsta (uptr, uptr->USTAT | STA_RDY);      /* ready */
        break;

    case MTSE_BOT:                                      /* reverse into BOT */
        mta_upddsta (uptr, uptr->USTAT | STA_RDY | STA_BOT);
        break;

    case MTSE_WRP:                                      /* write protect */
        mta_upddsta (uptr, uptr->USTAT | STA_WLK | STA_RDY);
        mta_sta = mta_sta | STA_ILL;                    /* illegal operation */
        break;
        }

return SCPE_OK;
}

/* Reset routine */

t_stat mta_reset (DEVICE *dptr)
{
int32 u;
UNIT *uptr;

dev_busy = dev_busy & ~INT_MTA;                         /* clear busy */
dev_done = dev_done & ~INT_MTA;                         /* clear done, int */
int_req = int_req & ~INT_MTA;
mta_cu = mta_wc = mta_ma = mta_sta = 0;                 /* clear registers */
mta_ep = 0;

/* AOS Installer does an IORST after a tape rewind command but before it can
   be serviced, yet expects the tape to have been rewound */

for (u = 0; u < MTA_NUMDR; u++) {                       /* loop thru units */
    uptr = mta_dev.units + u;
    if (sim_is_active (uptr) &&                         /* active and */
       (uptr->flags & STA_REW))                         /* rewinding? */
        sim_tape_rewind (uptr);                         /* update tape */
    sim_tape_reset (uptr);                              /* clear pos flag */
    sim_cancel (uptr);                                  /* cancel activity */
    if (uptr->flags & UNIT_ATT) uptr->USTAT = STA_RDY |
        (uptr->USTAT & STA_PEM) |
        (sim_tape_wrp (uptr)? STA_WLK: 0) |
        (sim_tape_bot (uptr)? STA_BOT: 0);
    else uptr->USTAT = 0;
    }
mta_updcsta (&mta_unit[0]);                             /* update status */
if (mtxb == NULL)
    mtxb = (uint8 *) calloc (MTA_MAXFR, sizeof (uint8));
if (mtxb == NULL)
    return SCPE_MEM;
return SCPE_OK;
}

/* Attach routine */

t_stat mta_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

r = sim_tape_attach (uptr, cptr);
if (r != SCPE_OK)
    return r;
if (!sim_is_active (uptr))
    mta_upddsta (uptr, STA_RDY | STA_BOT | STA_PEM |
        (sim_tape_wrp (uptr)? STA_WLK: 0));
return r;
}

/* Detach routine */

t_stat mta_detach (UNIT* uptr)
{
if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
if (!sim_is_active (uptr))
    mta_upddsta (uptr, 0);
return sim_tape_detach (uptr);
}

/* Write lock/unlock validate routine */

t_stat mta_vlock (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if ((uptr->flags & UNIT_ATT) && (val || sim_tape_wrp (uptr)))
    mta_upddsta (uptr, uptr->USTAT | STA_WLK);
else mta_upddsta (uptr, uptr->USTAT & ~STA_WLK);
return SCPE_OK;
}

/*  Boot routine  */

t_stat mta_boot (int32 unitno, DEVICE *dptr)
    {
    sim_tape_rewind( &mta_unit[unitno] ) ;
    /*
    use common rewind/reset code
        device reset
        rewind 'tape' file
        device
        unit
        controller
     */
    cpu_boot( unitno, dptr ) ;
    SR = 0100000 + DEV_MTA ;
    return ( SCPE_OK );
    }                                                   /*  end of 'mta_boot'  */
