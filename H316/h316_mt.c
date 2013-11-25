/* h316_mt.c: H316/516 magnetic tape simulator

   Copyright (c) 2003-2012, Robert M. Supnik

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

   mt           516-4100 seven track magnetic tape

    3-Jul-13    RLA     compatibility changes for extended interrupts
   19-Mar-12    RMS     Fixed declaration of chan_req (Mark Pizzolato)
   09-Jun-07    RMS     Fixed bug in write without stop (Theo Engel)
   16-Feb-06    RMS     Added tape capacity checking
   26-Aug-05    RMS     Revised to use API for write lock check
   08-Feb-05    RMS     Fixed error reporting from OCP (Philipp Hachtmann)
   01-Dec-04    RMS     Fixed bug in DMA/DMC support

   Magnetic tapes are represented as a series of variable records
   of the form:

        32b byte count
        byte 0
        byte 1
        :
        byte n-2
        byte n-1
        32b byte count

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a byte count of 0.
*/

#include "h316_defs.h"
#include "sim_tape.h"

#define MT_NUMDR        4                               /* number of drives */
#define DB_N_SIZE       16                              /* max data buf */
#define DBSIZE          (1 << DB_N_SIZE)                /* max data cmd */
#define FNC             u3                              /* function */
#define UST             u4                              /* unit status */

/* Function codes */

#define FNC_RBCD2       000
#define FNC_RBIN2       001
#define FNC_RBIN3       002
#define FNC_DMANM       003
#define FNC_WBCD2       004
#define FNC_WBIN2       005
#define FNC_WEOF        006
#define FNC_IOBUS       007
#define FNC_WBIN3       010
#define FNC_FSR         011
#define FNC_FSF         012
#define FNC_DMAAU       013
#define FNC_REW         014
#define FNC_BSR         015
#define FNC_BSF         016
#define FNC_STOPW       017
#define FNC_2ND         020                             /* second state */
#define FNC_NOP         (FNC_STOPW|FNC_2ND)
#define FNC_EOM         040                             /* end of motion */

/* Status - unit.UST */

#define STA_BOT         0000002                         /* beg of tape */
#define STA_EOT         0000001                         /* end of tape */

extern int32 dev_int, dev_enb;
extern uint32 chan_req;
extern int32 stop_inst;

uint32 mt_buf = 0;                                      /* data buffer */
uint32 mt_usel = 0;                                     /* unit select */
uint32 mt_busy = 0;                                     /* ctlr busy */
uint32 mt_mdirq = 0;                                    /* motion done int req */
uint32 mt_rdy = 0;                                      /* transfer ready (int) */
uint32 mt_err = 0;                                      /* error */
uint32 mt_eof = 0;                                      /* end of file */
uint32 mt_eor = 0;                                      /* transfer done */
uint32 mt_dma = 0;                                      /* DMA/DMC */
uint32 mt_xtime = 16;                                   /* transfer time */
uint32 mt_ctime = 3000;                                 /* start/stop time */
uint32 mt_stopioe = 1;                                  /* stop on I/O error */
uint8 mtxb[DBSIZE] = { 0 };                             /* data buffer */
t_mtrlnt mt_ptr = 0, mt_max = 0;                        /* buffer ptrs */

int32 mtio (int32 inst, int32 fnc, int32 dat, int32 dev);
void mt_updint (uint32 rdy, uint32 mdone);
t_stat mt_svc (UNIT *uptr);
t_stat mt_reset (DEVICE *dptr);
t_stat mt_attach (UNIT *uptr, char *cptr);
t_stat mt_detach (UNIT *uptr);
t_stat mt_map_err (UNIT *uptr, t_stat st);
void mt_wrwd (UNIT *uptr, uint32 dat);

/* MT data structures

   mt_dev       MT device descriptor
   mt_unit      MT unit list
   mt_reg       MT register list
   mt_mod       MT modifier list
*/

DIB mt_dib = { MT, MT_NUMDR, IOBUS, IOBUS, INT_V_MT, INT_V_NONE, &mtio, 0 };

UNIT mt_unit[] = {
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) }
    };

REG mt_reg[] = {
    { ORDATA (BUF, mt_buf, 16) },
    { ORDATA (USEL, mt_usel, 2) },
    { FLDATA (BUSY, mt_busy, 0) },
    { FLDATA (RDY, mt_rdy, 0) },
    { FLDATA (ERR, mt_err, 0) },
    { FLDATA (EOF, mt_eof, 0) },
    { FLDATA (EOR, mt_eor, 0) },
    { FLDATA (MDIRQ, mt_mdirq, 0) },
    { FLDATA (DMA, mt_dma, 0) },
    { FLDATA (INTREQ, dev_int, INT_V_MT) },
    { FLDATA (ENABLE, dev_enb, INT_V_MT) },
    { BRDATA (DBUF, mtxb, 8, 8, DBSIZE) },
    { DRDATA (BPTR, mt_ptr, DB_N_SIZE + 1) },
    { DRDATA (BMAX, mt_max, DB_N_SIZE + 1) },
    { DRDATA (CTIME, mt_ctime, 24), REG_NZ + PV_LEFT },
    { DRDATA (XTIME, mt_xtime, 24), REG_NZ + PV_LEFT },
    { URDATA (POS, mt_unit[0].pos, 10, T_ADDR_W, 0, MT_NUMDR, PV_LEFT) },
    { URDATA (FNC, mt_unit[0].FNC, 8, 8, 0, MT_NUMDR, REG_HRO) },
    { URDATA (UST, mt_unit[0].UST, 8, 2, 0, MT_NUMDR, REG_HRO) },
    { ORDATA (CHAN, mt_dib.chan, 5), REG_HRO },
    { FLDATA (STOP_IOE, mt_stopioe, 0) },
    { NULL }
    };

MTAB mt_mod[] = {
    { MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL }, 
    { MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
      &sim_tape_set_fmt, &sim_tape_show_fmt, NULL },
    { MTAB_XTD|MTAB_VUN, 0, "CAPACITY", "CAPACITY",
      &sim_tape_set_capac, &sim_tape_show_capac, NULL },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "IOBUS",
      &io_set_iobus, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "DMC",
      &io_set_dmc, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "DMA",
      &io_set_dma, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "CHANNEL", NULL,
      NULL, &io_show_chan, NULL },
    { 0 }
    };

DEVICE mt_dev = {
    "MT", mt_unit, mt_reg, mt_mod,
    MT_NUMDR, 10, 31, 1, 8, 8,
    NULL, NULL, &mt_reset,
    NULL, &mt_attach, &mt_detach,
    &mt_dib, DEV_DISABLE
    };

/* IO routine */

int32 mtio (int32 inst, int32 fnc, int32 dat, int32 dev)
{
uint32 i, u = dev & 03;
UNIT *uptr = mt_dev.units + u;
static uint8 wrt_fnc[16] = {                            /* >0 = wr, 1 = chan op */
    0, 0, 0, 0, 1, 1, 2, 0, 1, 0, 0, 0, 0, 0, 0, 0
    };

switch (inst) {                                         /* case on opcode */

    case ioOCP:
        mt_updint (mt_rdy, 0);                          /* clear motion intr */
        mt_eof = 0;                                     /* clear eof */
        switch (fnc) {                                  /* case on function */

        case FNC_DMANM:                                 /* set DMA/DMC */
        case FNC_DMAAU:
            mt_usel = u;                                /* save unit select */
            if (mt_dib.chan)                            /* set DMA if configured */
                mt_dma = 1;
            else mt_dma = 0;
            break;

        case FNC_IOBUS:                                 /* set IOBUS */
            mt_usel = u;                                /* save unit select */
            mt_dma = 0;
            break;

        case FNC_STOPW:                                 /* stop write */
            mt_usel = u;                                /* save unit select */
            mt_updint (0, mt_mdirq);                    /* clear ready */
            if (wrt_fnc[uptr->FNC & 017] == 1)          /* writing? */
                mt_eor = 1;                             /* set transfer done */
            break;          

        default:                                        /* motion command */
            if (mt_busy) return dat;                    /* nop if ctlr busy */
            mt_eor = 0;                                 /* clr transfer done */
            mt_err = 0;                                 /* clr error */
            mt_usel = u;                                /* save unit select */
            if ((uptr->flags & UNIT_ATT) == 0)          /* not attached? */
                return (((mt_stopioe? SCPE_UNATT: SCPE_OK) << IOT_V_REASON) | dat);
            if (sim_is_active (uptr))                   /* nop if busy */
                return dat;
            if (wrt_fnc[fnc] && sim_tape_wrp (uptr))
                return ((STOP_MTWRP << IOT_V_REASON) | dat);
            uptr->FNC = fnc;
            uptr->UST = 0;
            mt_busy = 1;
            for (i = 0; i < MT_NUMDR; i++)              /* clear all EOT flags */
                mt_unit[i].UST = mt_unit[i].UST & ~STA_EOT;
            sim_activate (uptr, mt_ctime);              /* schedule */
            break;
            }
        break;

    case ioINA:                                         /* INA */
        if (fnc)                                        /* fnc 0 only */
            return IOBADFNC (dat);
        if (mt_rdy) {                                   /* ready? */
            mt_rdy = 0;                                 /* clear ready */
            return IOSKIP (dat | mt_buf);               /* ret buf, skip */
            }
        break;

    case ioOTA:                                         /* OTA */
        if (fnc)                                        /* fnc 0 only */
            return IOBADFNC (dat);
        if (mt_rdy) {                                   /* ready? */
            mt_rdy = 0;                                 /* clear ready */
            mt_buf = dat;                               /* store buf */
            return IOSKIP (dat);                        /* skip */
            }
        break;

    case ioSKS:
        uptr = mt_dev.units + mt_usel;                  /* use saved unit sel */
        switch (fnc) {

        case 000:                                       /* ready */
            if (mt_rdy)
                return IOSKIP (dat);
            break;

        case 001:                                       /* !busy */
            if (!mt_busy)
                return IOSKIP (dat);
            break;

        case 002:                                       /* !error */
            if (!mt_err)
                return IOSKIP (dat);
            break;

        case 003:                                       /* !BOT */
            if (!(uptr->UST & STA_BOT))
                return IOSKIP (dat);
            break;

        case 004:                                       /* !interrupting */
            if (!TST_INTREQ (INT_MT))
                return IOSKIP (dat);
            break;

        case 005:                                       /* !EOT */
            if (!(uptr->UST & STA_EOT))
                return IOSKIP (dat);
            break;

        case 006:                                       /* !EOF */
            if (!mt_eof)
                return IOSKIP (dat);
            break;

        case 007:                                       /* !write prot */
            if (!sim_tape_wrp (uptr))
                return IOSKIP (dat);
            break;

        case 011:                                       /* operational */
            if ((uptr->flags & UNIT_ATT) && ((uptr->FNC & 017) != FNC_REW))
                return IOSKIP (dat);
            break;

        case 012:                                       /* skip if !chan 2 */
            return IOSKIP (dat);

        case 013:                                       /* skip if !auto */
            return IOSKIP (dat);

        case 014:                                       /* !rewinding */
            uptr = mt_dev.units + (dev & 03);           /* use specified unit */
            if ((uptr->FNC & 017) != FNC_REW)
                return IOSKIP (dat);
            break;
            }
        break;

    case ioEND:                                         /* end of range */
        mt_eor = 1;                                     /* transfer done */
        break;
        }

return dat;
}

/* Unit service

   If rewind done, reposition to start of tape, set status
   else, do operation, set done, interrupt

   Can't be write locked, can only write lock detached unit
*/

t_stat mt_svc (UNIT *uptr)
{
int32 ch = mt_dib.chan - 1;                             /* DMA/DMC ch */
uint32 i, c1, c2, c3;
t_mtrlnt tbc;
t_bool passed_eot;
t_stat st, r = SCPE_OK;

if ((uptr->flags & UNIT_ATT) == 0) {                    /* offline? */
    mt_err = 1;
    mt_busy = 0;
    mt_updint (0, 1);                                   /* cmd done */
    return IORETURN (mt_stopioe, SCPE_UNATT);
    }

passed_eot = sim_tape_eot (uptr);                       /* passed EOT? */
switch (uptr->FNC) {                                    /* case on function */

    case FNC_REW:                                       /* rewind (initial) */
        mt_busy = 0;                                    /* ctlr not busy */
        uptr->FNC = uptr->FNC | FNC_2ND;
        sim_activate (uptr, mt_ctime);
        return SCPE_OK;                                 /* continue */

    case FNC_REW | FNC_2ND:                             /* rewind done */
        uptr->pos = 0;                                  /* reposition file */
        uptr->UST = STA_BOT;                            /* set BOT */
        uptr->FNC = FNC_NOP;                            /* nop function */
        for (i = 0; i < MT_NUMDR; i++) {                /* last rewind? */
            if ((mt_unit[i].FNC & 017) == FNC_REW)
                return SCPE_OK;
            }
        mt_updint (mt_rdy, 1);                          /* yes, motion done */
        return SCPE_OK;

    case FNC_WEOF:                                      /* write file mark */
        if ((st = sim_tape_wrtmk (uptr)))               /* write tmk, err? */
            r = mt_map_err (uptr, st);                  /* map error */
        break;                                          /* sched end motion */

    case FNC_FSR:                                       /* space fwd rec */
        if ((st = sim_tape_sprecf (uptr, &tbc)))        /* space fwd, err? */
            r = mt_map_err (uptr, st);                  /* map error */
        break;                                          /* sched end motion */

    case FNC_BSR:                                       /* space rev rec */
        if ((st = sim_tape_sprecr (uptr, &tbc)))        /* space rev, err? */
            r = mt_map_err (uptr, st);                  /* map error */
        break;                                          /* sched end motion */

    case FNC_FSF:                                       /* space fwd file */
        while ((st = sim_tape_sprecf (uptr, &tbc)) == MTSE_OK) ;
        r = mt_map_err (uptr, st);                      /* map error */
        break;                                          /* sched end motion */

    case FNC_BSF:                                       /* space rev file */
        while ((st = sim_tape_sprecr (uptr, &tbc)) == MTSE_OK) ;
        r = mt_map_err (uptr, st);                      /* map error */
        break;                                          /* sched end motion */

    case FNC_EOM:                                       /* end of motion */
        uptr->FNC = FNC_NOP;                            /* nop function */
        mt_busy = 0;                                    /* not busy */
        mt_updint (mt_rdy, 1);                          /* end of motion */
        return SCPE_OK;                                 /* done! */

    case FNC_RBCD2: case FNC_RBIN2: case FNC_RBIN3:     /* read first */
        mt_ptr = 0;                                     /* clr buf ptr */
        st = sim_tape_rdrecf (uptr, mtxb, &mt_max, DBSIZE);     /* read rec */
        if (st != MTSE_OK) {                            /* error? */
            r = mt_map_err (uptr, st);                  /* map error */
            break;                                      /* sched end motion */
            }
        uptr->FNC = uptr->FNC | FNC_2ND;                /* next state */
        sim_activate (uptr, mt_xtime);                  /* sched xfer */
        return SCPE_OK;

    case FNC_RBCD2 | FNC_2ND:                           /* read, word */
    case FNC_RBIN2 | FNC_2ND:
    case FNC_RBIN3 | FNC_2ND:
        if (mt_ptr >= mt_max)                           /* record done? */
            break;
        c1 = mtxb[mt_ptr++] & 077;                      /* get 2 chars */
        c2 = mtxb[mt_ptr++] & 077;
        if (uptr->FNC == (FNC_RBCD2 | FNC_2ND)) {       /* BCD? */
            if (c1 == 012) c1 = 0;                      /* change 12 to 0 */
            if (c2 == 012) c2 = 0;
            }
        if (uptr->FNC == (FNC_RBIN3 | FNC_2ND)) {       /* read 3? */
            if (mt_ptr >= mt_max) break;                /* lose wd if not enuf */
            c3 = mtxb[mt_ptr++] & 017;                  /* get 3rd char */
            }
        else c3 = 0;
        sim_activate (uptr, mt_xtime);                  /* no, sched word */
        if (mt_eor)                                     /* xfer done? */
            return SCPE_OK;
        mt_buf = (c1 << 10) | (c2 << 4) | c3;           /* pack chars */
        if (mt_rdy) mt_err = 1;                         /* buf full? err */
        mt_updint (1, mt_mdirq);                        /* set ready */
        if (mt_dma)                                     /* DMC/DMA? req chan */
            SET_CH_REQ (ch);
        return SCPE_OK;                                 /* continue */
        
    case FNC_WBCD2: case FNC_WBIN2: case FNC_WBIN3:     /* write first */
        mt_ptr = 0;                                     /* clear buf ptr */
        mt_updint (1, mt_mdirq);                        /* set ready */
        if (mt_dma)                                     /* DMC/DMA? req chan */
            SET_CH_REQ (ch);
        uptr->FNC = uptr->FNC | FNC_2ND;                /* next state */
        sim_activate (uptr, mt_xtime);                  /* sched xfer */
        return SCPE_OK;                                 /* continue */

    case FNC_WBCD2 | FNC_2ND:                           /* write, word */
    case FNC_WBIN2 | FNC_2ND:
    case FNC_WBIN3 | FNC_2ND:
        if (mt_eor || mt_rdy) {                         /* done or no data? */
            if (!mt_rdy)                                /* write last word */
                mt_wrwd (uptr, mt_buf);
            else mt_rdy = 0;                            /* rdy must be clr */
            if (mt_ptr) {                               /* any data? */
                if ((st = sim_tape_wrrecf (uptr, mtxb, mt_ptr)))/* write, err? */
                    r = mt_map_err (uptr, st);          /* map error */
                }
            break;                                      /* sched end motion */
            }
        mt_wrwd (uptr, mt_buf);                         /* write word */
        sim_activate (uptr, mt_xtime);                  /* no, sched word */
        mt_updint (1, mt_mdirq);                        /* set ready */
        if (mt_dma)                                     /* DMC/DMA? req chan */
            SET_CH_REQ (ch);
        return SCPE_OK;                                 /* continue */

    default:                                            /* unknown */
        break;
        }

/* End of command, process error or schedule end of motion */

if (!passed_eot && sim_tape_eot (uptr))                 /* just passed EOT? */
    uptr->UST = uptr->UST | STA_EOT;
if (r != SCPE_OK) {
    uptr->FNC = FNC_NOP;                                /* nop function */
    mt_busy = 0;                                        /* not busy */
    mt_updint (mt_rdy, 1);                              /* end of motion */
    return r;
    }
uptr->FNC = FNC_EOM;                                    /* sched end motion */
sim_activate (uptr, mt_ctime);
return SCPE_OK;
}

/* Write word to buffer */

void mt_wrwd (UNIT *uptr, uint32 dat)
{
uint32 c1, c2;

c1 = (dat >> 10) & 077;                                 /* get 2 chars */
c2 = (dat >> 4) & 077;
if (uptr->FNC == (FNC_WBCD2 | FNC_2ND)) {               /* BCD? */
    if (c1 == 0)
        c1 = 012;                              /* change 0 to 12 */
    if (c2 == 0)
        c2 = 012;
    }
if (mt_ptr < DBSIZE)                                    /* store 2 char */
    mtxb[mt_ptr++] = c1;
if (mt_ptr < DBSIZE)
    mtxb[mt_ptr++] = c2;
if ((uptr->FNC == (FNC_WBIN3 | FNC_2ND)) &&             /* write 3? */
    (mt_ptr < DBSIZE))
    mtxb[mt_ptr++] = mt_buf & 017;
return;
}

/* Map tape error status */

t_stat mt_map_err (UNIT *uptr, t_stat st)
{
switch (st) {

    case MTSE_FMT:                                      /* illegal fmt */
    case MTSE_UNATT:                                    /* unattached */
        mt_err = 1;                                     /* reject */
    case MTSE_OK:                                       /* no error */
        return SCPE_IERR;                               /* never get here! */

    case MTSE_TMK:                                      /* end of file */
        mt_eof = 1;                                     /* eof */
        break;

    case MTSE_INVRL:                                    /* invalid rec lnt */
        mt_err = 1;
        return SCPE_MTRLNT;

    case MTSE_IOERR:                                    /* IO error */
        mt_err = 1;                                     /* error */
        if (mt_stopioe)
            return SCPE_IOERR;
        break;

    case MTSE_RECE:                                     /* record in error */
    case MTSE_EOM:                                      /* end of medium */
        mt_err = 1;                                     /* error */
        break;

    case MTSE_BOT:                                      /* reverse into BOT */
        uptr->UST = STA_BOT;                            /* set status */
        break;

    case MTSE_WRP:                                      /* write protect */
        mt_err = 1;                                     /* error */
        return STOP_MTWRP;
        }

return SCPE_OK;
}

/* Update interrupts */

void mt_updint (uint32 rdy, uint32 mdirq)
{
mt_rdy = rdy;                                           /* store new ready */
mt_mdirq = mdirq;                                       /* store new motion irq */
if ((mt_rdy && !mt_dma) || mt_mdirq)                    /* update int request */
    SET_INT (INT_MT);
else CLR_INT (INT_MT);
return;
}

/* Reset routine */

t_stat mt_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

mt_buf = 0;                                             /* clear state */
mt_usel = 0;
mt_mdirq = 0;
mt_eor = 0;
mt_busy = 0;
mt_rdy = 0;
mt_eof = 0;
mt_err = 0;
mt_dma = 0;
CLR_INT (INT_MT);                                       /* clear int, enb */
CLR_ENB (INT_MT);
for (i = 0; i < MT_NUMDR; i++) {                        /* loop thru units */
    uptr = mt_dev.units + i;
    sim_tape_reset (uptr);                              /* reset tape */
    sim_cancel (uptr);                                  /* cancel op */
    uptr->UST = uptr->pos? 0: STA_BOT;                  /* update status */
    uptr->FNC = FNC_NOP;
    }
return SCPE_OK;
}

/* Attach routine */

t_stat mt_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = sim_tape_attach (uptr, cptr);                       /* attach unit */
if (r != SCPE_OK)                                       /* update status */
    return r;
uptr->UST = STA_BOT;
return r;
}

/* Detach routine */

t_stat mt_detach (UNIT* uptr)
{
uptr->UST = 0;                                          /* update status */
uptr->FNC = FNC_NOP;                                    /* nop function */
return sim_tape_detach (uptr);                          /* detach unit */
}
