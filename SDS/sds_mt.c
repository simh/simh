/* sds_mt.c: SDS 940 magnetic tape simulator

   Copyright (c) 2001-2016, Robert M. Supnik

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

   mt           7 track magnetic tape

   09-Oct-16    RMS     Added precise gap erase
   19-Mar-12    RMS     Fixed bug in scan function decode (Peter Schorn)
   16-Feb-06    RMS     Added tape capacity checking
   07-Dec-04    RMS     Added read-only file support
   25-Apr-03    RMS     Revised for extended file support
   28-Mar-03    RMS     Added multiformat support
   28-Feb-03    RMS     Revised for magtape library

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

#include "sds_defs.h"
#include "sim_tape.h"

#define MT_MAXFR        (32768 * 4)
#define MT_NUMDR        8                               /* number drives */
#define MT_UNIT         07
#define botf            u3                              /* bot tape flag */
#define eotf            u4                              /* eot tape flag */

extern uint32 xfr_req;
extern int32 stop_invins, stop_invdev, stop_inviop;
int32 mt_inst = 0;                                      /* saved instr */
int32 mt_eof = 0;                                       /* end of file */
int32 mt_gap = 0;                                       /* in gap */
int32 mt_skip = 0;                                      /* skip rec */
int32 mt_bptr = 0;                                      /* buf ptr */
int32 mt_blnt = 0;                                      /* buf length */
int32 mt_ctime = 10;                                    /* char time */
int32 mt_gtime = 1000;                                  /* gap time */
int32 mt_stopioe = 1;                                   /* stop on err */
uint8 mtxb[MT_MAXFR];                                   /* record buffer */
DSPT mt_tplt[] = {                                      /* template */
    { MT_NUMDR, 0 },
    { MT_NUMDR, DEV_MTS },
    { MT_NUMDR, DEV_OUT },
    { MT_NUMDR, DEV_MTS+DEV_OUT },
    { 0, 0 }
    };

t_stat mt_svc (UNIT *uptr);
t_stat mt_reset (DEVICE *dptr);
t_stat mt_boot (int32 unitno, DEVICE *dptr);
t_stat mt_attach (UNIT *uptr, CONST char *cptr);
t_stat mt_detach (UNIT *uptr);
t_stat mt_readrec (UNIT *uptr);
t_mtrlnt mt_readbc (UNIT *uptr);
void mt_readend (UNIT *uptr);
t_stat mt_wrend (uint32 dev);
void mt_set_err (UNIT *uptr);
t_stat mt (uint32 fnc, uint32 inst, uint32 *dat);

static const char sds_to_bcd[64] = {
    012, 001, 002, 003, 004, 005, 006, 007,
    010, 011, 012, 013, 014, 015, 016, 017,
    060, 061, 062, 063, 064, 065, 066, 067,
    070, 071, 072, 073, 074, 075, 076, 077,
    040, 041, 042, 043, 044, 045, 046, 047,
    050, 051, 052, 053, 054, 055, 056, 057,
    020, 021, 022, 023, 024, 025, 026, 027,
    030, 031, 032, 033, 034, 035, 036, 037
    };

static const char bcd_to_sds[64] = {
    000, 001, 002, 003, 004, 005, 006, 007,
    010, 011, 000, 013, 014, 015, 016, 017,
    060, 061, 062, 063, 064, 065, 066, 067,
    070, 071, 072, 073, 074, 075, 076, 077,
    040, 041, 042, 043, 044, 045, 046, 047,
    050, 051, 052, 053, 054, 055, 056, 057,
    020, 021, 022, 023, 024, 025, 026, 027,
    030, 031, 032, 033, 034, 035, 036, 037
    };

/* MT data structures

   mt_dev       MT device descriptor
   mt_unit      MT unit descriptor
   mt_reg       MT register list
*/

DIB mt_dib = { CHAN_W, DEV_MT, XFR_MT0, mt_tplt, &mt };

UNIT mt_unit[] = {
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) }
    };

REG mt_reg[] = {
    { BRDATA (BUF, mtxb, 8, 8, MT_MAXFR) },
    { DRDATA (BPTR, mt_bptr, 18), PV_LEFT },
    { DRDATA (BLNT, mt_blnt, 18), PV_LEFT },
    { FLDATA (XFR, xfr_req, XFR_V_MT0) },
    { ORDATA (INST, mt_inst, 24) },
    { FLDATA (EOF, mt_eof, 0) },
    { FLDATA (GAP, mt_gap, 0) },
    { FLDATA (SKIP, mt_skip, 0) },
    { DRDATA (CTIME, mt_ctime, 24), REG_NZ + PV_LEFT },
    { DRDATA (GTIME, mt_gtime, 24), REG_NZ + PV_LEFT },
    { URDATA (POS, mt_unit[0].pos, 10, T_ADDR_W, 0,
              MT_NUMDR, PV_LEFT | REG_RO) },
    { URDATA (BOT, mt_unit[0].botf, 10, 1, 0, MT_NUMDR, REG_RO) },
    { URDATA (EOT, mt_unit[0].eotf, 10, 1, 0, MT_NUMDR, REG_RO) },
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
    { MTAB_XTD|MTAB_VDV, 0, "CHANNEL", "CHANNEL",
      &set_chan, &show_chan, NULL },
    { 0 }
    };

DEVICE mt_dev = {
    "MT", mt_unit, mt_reg, mt_mod,
    MT_NUMDR, 10, 31, 1, 8, 8,
    NULL, NULL, &mt_reset,
    &mt_boot, &mt_attach, NULL,
    &mt_dib, DEV_DISABLE | DEV_TAPE
    };

/* Mag tape routine

   conn -       inst = EOM0, dat = NULL
   eom1 -       inst = EOM1, dat = NULL
   sks -        inst = SKS, dat = ptr to result
   disc -       inst = device number, dat = NULL
   wreor -      inst = device number, dat = NULL
   read -       inst = device number, dat = ptr to data
   write -      inst = device number, dat = ptr to result
*/

t_stat mt (uint32 fnc, uint32 inst, uint32 *dat)
{
int32 u = inst & MT_UNIT;                               /* get unit */
UNIT *uptr = mt_dev.units + u;                          /* get unit ptr */
int32 t, new_ch;
uint8 chr;
t_stat r;

switch (fnc) {                                          /* case function */

    case IO_CONN:                                       /* connect */
        new_ch = I_GETEOCH (inst);                      /* get new chan */
        if (new_ch != mt_dib.chan)                      /* wrong chan? */
            return SCPE_IERR;
        if (mt_gap) {                                   /* in gap? */
            mt_gap = 0;                                 /* clr gap flg */
            sim_cancel (uptr);                          /* cancel timer */
            }
        else if (sim_is_active (uptr))                  /* busy? */
            CRETIOP;
        uptr->eotf = 0;                                 /* clr eot flag */      
        mt_eof = 0;                                     /* clr eof flag */
        mt_skip = 0;                                    /* clr skp flag */
        mt_bptr = mt_blnt = 0;                          /* init buffer */
        if ((inst & DEV_MTS)?                           /* scan or erase? */
            ((inst & CHC_REV) && (!(inst & DEV_OUT)) && /* scan reverse and */
                (CHC_GETCPW (inst) < 3)):               /* not 4 char/wd? */
            (inst & CHC_REV))                           /* rw & rev? */
            return STOP_INVIOP;
        mt_inst = inst;                                 /* save inst */
        if ((inst & DEV_MTS) && !(inst & DEV_OUT))      /* scanning? */
            chan_set_flag (mt_dib.chan, CHF_SCAN);      /* set chan flg */
        xfr_req = xfr_req & ~XFR_MT0;                   /* clr xfr flag */
        sim_activate (uptr, mt_gtime);                  /* start timer */
        break;

    case IO_EOM1:                                       /* EOM mode 1 */
        new_ch = I_GETEOCH (inst);                      /* get new chan */
        if (new_ch != mt_dib.chan)                      /* wrong chan? */
            CRETIOP;
        t = inst & 07670;                               /* get command */
        if ((t == 04010) && !sim_is_active (uptr)) {    /* rewind? */
            sim_tape_rewind (uptr);                     /* rewind unit */
            uptr->eotf = 0;                             /* clr eot */
            uptr->botf = 1;                             /* set bot */
            }
        else if ((t == 03610) && sim_is_active (uptr) &&/* skip rec? */
            ((mt_inst & DEV_OUT) == 0))
            mt_skip = 1;                                /* set flag */
        else CRETINS;
        break;

    case IO_DISC:                                       /* disconnect */
        sim_cancel (uptr);                              /* no more xfr's */
        if (inst & DEV_OUT) {                           /* write? */
            if ((r = mt_wrend (inst)))                  /* end record */
                return r;
            }
        break;

    case IO_WREOR:                                      /* write eor */
        chan_set_flag (mt_dib.chan, CHF_EOR);           /* set eor flg */
        if ((r = mt_wrend (inst)))                      /* end record */
            return r;
        mt_gap = 1;                                     /* in gap */
        sim_activate (uptr, mt_gtime);                  /* start timer */        
        break;

    case IO_SKS:                                        /* SKS */
        new_ch = I_GETSKCH (inst);                      /* get chan # */
        if (new_ch != mt_dib.chan)                      /* wrong chan? */
            return SCPE_IERR;
        if ((inst & (DEV_OUT | DEV_MTS)) == 0) {        /* not sks 1n? */
            t = I_GETSKCND (inst);                      /* get skip cond */
            switch (t) {                                /* case sks cond */
            case 001:                                   /* sks 1021n */
                *dat = 1;                               /* not magpak */
                break;
            case 002:                                   /* sks 1041n */
                if (!(uptr->flags & UNIT_ATT) ||        /* not ready */
                    sim_is_active (uptr))
                    *dat = 1;
                break;
            case 004:                                   /* sks 1101n */
                if (!uptr->eotf)                        /* not EOT */
                    *dat = 1;
                break;
            case 010:                                   /* sks 1201n */
                if (!uptr->botf)                        /* not BOT */
                    *dat = 1;
                break;
            case 013:                                   /* sks 12610 */
                if (!mt_gap)                            /* not in gap */
                    *dat = 1;
                break;
            case 017:                                   /* sks 13610 */
                if (!mt_eof)                            /* not EOF */
                    *dat = 1;
                break;
            case 020:                                   /* sks 1401n */
                if (!sim_tape_wrp (uptr))               /* not wrp */
                    *dat = 1;
                break;
            case 031:                                   /* sks 1621n */
            case 033:                                   /* sks 1661n */
                *dat = 1;                               /* not 556bpi */
            case 035:                                   /* sks 1721n */
                break;                                  /* not 800bpi */
                }
            }                                           /* end if */
        break;

    case IO_READ:                                       /* read */
        xfr_req = xfr_req & ~XFR_MT0;                   /* clr xfr flag */
        if (mt_blnt == 0) {                             /* first read? */
            r = mt_readrec (uptr);                      /* get data */
            if ((r != SCPE_OK) || (mt_blnt == 0))       /* err, inv reclnt? */
                return r;
            }
        uptr->botf = 0;                                 /* off BOT */
        if (mt_inst & CHC_REV)                          /* get next rev */
            chr = mtxb[--mt_bptr] & 077;
        else chr = mtxb[mt_bptr++] & 077;               /* get next fwd */
        if (!(mt_inst & CHC_BIN))                       /* bcd? */
            chr = bcd_to_sds[chr];
        *dat = chr & 077;                               /* give to chan */
        if ((mt_inst & CHC_REV)? (mt_bptr <= 0):        /* rev or fwd, */
            (mt_bptr >= mt_blnt))                       /* recd done? */
            mt_readend (uptr);
        break;

    case IO_WRITE:                                      /* write */
        uptr->botf = 0;                                 /* off BOT */
        chr = (*dat) & 077;
        xfr_req = xfr_req & ~XFR_MT0;                   /* clr xfr flag */
        if (!(mt_inst & CHC_BIN))                       /* bcd? */
            chr = sds_to_bcd[chr];
        if (mt_bptr < MT_MAXFR)                         /* insert in buf */
            mtxb[mt_bptr++] = chr;
        break;

    default:
        CRETINS;
        }

return SCPE_OK;
} 

/* Unit service */

t_stat mt_svc (UNIT *uptr)
{
if (mt_gap) {                                           /* gap timeout */
    mt_gap = 0;                                         /* clr gap flg */
    chan_disc (mt_dib.chan);                            /* disc chan */
    }
else if (mt_skip)                                       /* skip record */
    mt_readend (uptr);
else {                                                  /* normal xfr */
    xfr_req = xfr_req | XFR_MT0;                        /* set xfr req */
    sim_activate (uptr, mt_ctime);                      /* reactivate */
    }
return SCPE_OK;
}

/* Read start (get new record) */

t_stat mt_readrec (UNIT *uptr)
{
t_mtrlnt tbc;
t_stat st;

if ((uptr->flags & UNIT_ATT) == 0) {                    /* attached? */
    mt_set_err (uptr);                                  /* no, err, disc */
    return SCPE_UNATT;
    }
if (mt_inst & CHC_REV)                                  /* reverse? */
    st = sim_tape_rdrecr (uptr, mtxb, &tbc, MT_MAXFR);  /* read rec rev */
else {                                                  /* no, fwd */
    t_bool passed_eot = sim_tape_eot (uptr);            /* passed EOT? */
    st = sim_tape_rdrecf (uptr, mtxb, &tbc, MT_MAXFR);
    if (!passed_eot && sim_tape_eot (uptr))             /* just passed eot? */
        uptr->eotf = 1;
    }
if (st == MTSE_TMK) {                                   /* tape mark? */
    mt_eof = 1;                                         /* set eof flag */
    mtxb[0] = mtxb[1] = 017;                            /* EOR char */
    mt_blnt = 2;                                        /* store 2 */
    return SCPE_OK;
    }
if (st != MTSE_OK) {                                    /* other error? */
    mt_set_err (uptr);                                  /* err, disc */
    if (st == MTSE_IOERR)                               /* IO error? */
        return SCPE_IOERR;
    if (st == MTSE_INVRL)                               /* inv rec lnt? */
        return SCPE_MTRLNT;
    if (st == MTSE_EOM)                                 /* eom? set eot */
        uptr->eotf = 1;
    return SCPE_OK;
    }
mt_blnt = tbc;                                          /* set buf lnt */
return SCPE_OK;
}

/* Read done (eof, end of record) */

void mt_readend (UNIT *uptr)
{
sim_cancel (uptr);                                      /* stop timer */
mt_skip = 0;                                            /* clr skp flg */
chan_set_flag (mt_dib.chan, CHF_EOR);                   /* end record */
if (mt_eof)                                             /* EOF? */
    chan_disc (mt_dib.chan);
else {
    mt_gap = 1;                                         /* no, in gap */
    sim_activate (uptr, mt_gtime);                      /* start timer */
    }
return;
}

/* Write complete (end of record or disconnect) */

t_stat mt_wrend (uint32 dev)
{
UNIT *uptr = mt_dev.units + (dev & MT_UNIT);
t_stat st;
t_bool passed_eot;

sim_cancel (uptr);                                      /* no more xfr's */
if (mt_bptr == 0)                                       /* buf empty? */
    return SCPE_OK;
if (!(uptr->flags & UNIT_ATT)) {                        /* attached? */
    mt_set_err (uptr);                                  /* no, err, disc */
    return SCPE_UNATT;
    }
if (sim_tape_wrp (uptr)) {                              /* write lock? */
    mt_set_err (uptr);                                  /* yes, err, disc */
    return SCPE_OK;
    }
passed_eot = sim_tape_eot (uptr);                       /* passed EOT? */
if (dev & DEV_MTS) {                                    /* erase? */
    if (mt_inst & CHC_REV)                              /* reverse? */
        st = sim_tape_errecr (uptr, mt_bptr);
    else st = sim_tape_errecf (uptr, mt_bptr);          /* no, forward */
    }
else {                                                  /* can't be reverse */
    if ((mt_bptr == 1) && (mtxb[0] == 017) &&           /* 1 char BCD write */
        (!(mt_inst & CHC_BIN)))                         /* of 017B? */
        st = sim_tape_wrtmk (uptr);                     /* write tape mark */
    else st = sim_tape_wrrecf (uptr, mtxb, mt_bptr);    /* write record */
    }
if (!passed_eot && sim_tape_eot (uptr))                 /* just passed EOT? */
    uptr->eotf = 1;
mt_bptr = 0;
if (st != MTSE_OK)                                      /* error? */
    mt_set_err (uptr);
if (st == MTSE_IOERR)
    return SCPE_IOERR;
return SCPE_OK;
}

/* Fatal error */

void mt_set_err (UNIT *uptr)
{
chan_set_flag (mt_dib.chan, CHF_EOR | CHF_ERR);         /* eor, error */
chan_disc (mt_dib.chan);                                /* disconnect */
xfr_req = xfr_req & ~XFR_MT0;                           /* clear xfr */
sim_cancel (uptr);                                      /* stop */
mt_bptr = 0;                                            /* buf empty */
return;
}
/* Reset routine */

t_stat mt_reset (DEVICE *dptr)
{
int32 i;

chan_disc (mt_dib.chan);                                /* disconnect */
mt_eof = 0;                                             /* clear state */
mt_gap = 0;
mt_skip = 0;
mt_inst = 0;
mt_bptr = mt_blnt = 0;
xfr_req = xfr_req & ~XFR_MT0;                           /* clr xfr flag */
for (i = 0; i < MT_NUMDR; i++) {                        /* deactivate */
    sim_cancel (&mt_unit[i]);
    sim_tape_reset (&mt_unit[i]);
    mt_unit[i].eotf = 0;
    }
return SCPE_OK;
}

/* Attach and detach routines */

t_stat mt_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

r = sim_tape_attach (uptr, cptr);
if (r != SCPE_OK)
    return r;
uptr->botf = 1;
uptr->eotf = 0;
return SCPE_OK;
}

t_stat mt_detach (UNIT *uptr)
{
uptr->botf = uptr->eotf = 0;
return sim_tape_detach (uptr);
}

/* Boot routine - simulate FILL console command */

t_stat mt_boot (int32 unitno, DEVICE *dptr)
{
extern uint32 P, M[];

if (unitno)                                             /* only unit 0 */
    return SCPE_ARG;
M[0] = 077777771;                                       /* -7B */
M[1] = 007100000;                                       /* LDX 0 */
M[2] = 000203610;                                       /* EOM 3610B */
M[3] = 003200002;                                       /* WIM 2 */
M[4] = 000100002;                                       /* BRU 2 */
P = 1;                                                  /* start at 1 */
return SCPE_OK;
}
