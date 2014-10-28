/* hp2100_mt.c: HP 2100 12559A magnetic tape simulator

   Copyright (c) 1993-2013, Robert M. Supnik

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

   MT           12559A 3030 nine track magnetic tape

   10-Jan-13    MP      Added DEV_TAPE to DEVICE flags
   09-May-12    JDB     Separated assignments from conditional expressions
   25-Mar-12    JDB     Removed redundant MTAB_VUN from "format" MTAB entry
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
   28-Mar-11    JDB     Tidied up signal handling
   29-Oct-10    JDB     Fixed command scanning error in mtcio ioIOO handler
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   04-Sep-08    JDB     Fixed missing flag after CLR command
   02-Sep-08    JDB     Moved write enable and format commands from MTD to MTC
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   07-Oct-04    JDB     Allow enable/disable from either device
   14-Aug-04    RMS     Modified handling of end of medium (suggested by Dave Bryan)
   06-Jul-04    RMS     Fixed spurious timing error after CLC (found by Dave Bryan)
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Implemented DMA SRQ (follows FLG)
   21-Dec-03    RMS     Adjusted msc_ctime for TSB (from Mike Gemeny)
   25-Apr-03    RMS     Revised for extended file support
   28-Mar-03    RMS     Added multiformat support
   28-Feb-03    RMS     Revised for magtape library
   30-Sep-02    RMS     Revamped error handling
   28-Aug-02    RMS     Added end of medium support
   30-May-02    RMS     Widened POS to 32b
   22-Apr-02    RMS     Added maximum record length test
   20-Jan-02    RMS     Fixed bug on last character write
   03-Dec-01    RMS     Added read only unit, extended SET/SHOW support
   07-Sep-01    RMS     Moved function prototypes
   30-Nov-00    RMS     Made variable names unique
   04-Oct-98    RMS     V2.4 magtape format

   References:
   - 12559A 9-Track Magnetic Tape Unit Interface Kit Operating and Service Manual
            (12559-9001, Jul-1970)
   - SIMH Magtape Representation and Handling (Bob Supnik, 30-Aug-2006)


   The 3030 was one of HP's earliest tape drives.  The 12559A controller
   supported a single 800 bpi, 9-track drive, operating at 75 inches per second.
   It had two unusual characteristics:

    - The controller accepted only one byte per I/O word, rather than packing
      two bytes per word.

    - The drive could not read or write fewer than 12 bytes per record.

   The first behavior meant that DMA operation required the byte-unpacking
   feature of the 12578A DMA card for the 2116 computer.  The second meant that
   software drivers had to pad short records with blanks or nulls.

   Implementation notes:

    1. The HP 3030 Magnetic Tape Subsystem diagnostic, part number 20433-60001,
       has never been located, so this simulator has not been fully tested.  It
       does pass a functional test under DOS-III using driver DVR22.
*/


#include "hp2100_defs.h"
#include "sim_tape.h"

#define DB_V_SIZE       16                              /* max data buf */
#define DBSIZE          (1 << DB_V_SIZE)                /* max data cmd */

/* Command - mtc_fnc */

#define FNC_CLR         0300                            /* clear */
#define FNC_WC          0031                            /* write */
#define FNC_RC          0023                            /* read */
#define FNC_GAP         0011                            /* write gap */
#define FNC_FSR         0003                            /* forward space */
#define FNC_BSR         0041                            /* backward space */
#define FNC_REW         0201                            /* rewind */
#define FNC_RWS         0101                            /* rewind and offline */
#define FNC_WFM         0035                            /* write file mark */

/* Status - stored in mtc_sta, (d) = dynamic */

#define STA_LOCAL       0400                            /* local (d) */
#define STA_EOF         0200                            /* end of file */
#define STA_BOT         0100                            /* beginning of tape */
#define STA_EOT         0040                            /* end of tape */
#define STA_TIM         0020                            /* timing error */
#define STA_REJ         0010                            /* programming error */
#define STA_WLK         0004                            /* write locked (d) */
#define STA_PAR         0002                            /* parity error */
#define STA_BUSY        0001                            /* busy (d) */

struct {
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } mtd = { CLEAR, CLEAR };

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } mtc = { CLEAR, CLEAR, CLEAR };

int32 mtc_fnc = 0;                                      /* function */
int32 mtc_sta = 0;                                      /* status register */
int32 mtc_dtf = 0;                                      /* data xfer flop */
int32 mtc_1st = 0;                                      /* first svc flop */
int32 mtc_ctime = 40;                                   /* command wait */
int32 mtc_gtime = 1000;                                 /* gap stop time */
int32 mtc_xtime = 15;                                   /* data xfer time */
int32 mtc_stopioe = 1;                                  /* stop on error */
uint8 mtxb[DBSIZE] = { 0 };                             /* data buffer */
t_mtrlnt mt_ptr = 0, mt_max = 0;                        /* buffer ptrs */
static const uint32 mtc_cmd[] = {
 FNC_WC, FNC_RC, FNC_GAP, FNC_FSR, FNC_BSR, FNC_REW, FNC_RWS, FNC_WFM };
static const uint32 mtc_cmd_count = sizeof (mtc_cmd) / sizeof (mtc_cmd[0]);

DEVICE mtd_dev, mtc_dev;

IOHANDLER mtdio;
IOHANDLER mtcio;

t_stat mtc_svc (UNIT *uptr);
t_stat mt_reset (DEVICE *dptr);
t_stat mtc_attach (UNIT *uptr, char *cptr);
t_stat mtc_detach (UNIT *uptr);
t_stat mt_map_err (UNIT *uptr, t_stat st);
t_stat mt_clear (void);

/* MTD data structures

   mtd_dev      MTD device descriptor
   mtd_unit     MTD unit list
   mtd_reg      MTD register list
*/

DIB mt_dib[] = {
    { &mtdio, MTD },
    { &mtcio, MTC }
    };

#define mtd_dib mt_dib[0]
#define mtc_dib mt_dib[1]

UNIT mtd_unit = { UDATA (NULL, 0, 0) };

REG mtd_reg[] = {
    { FLDATA (FLG, mtd.flag,    0) },
    { FLDATA (FBF, mtd.flagbuf, 0) },
    { BRDATA (DBUF, mtxb, 8, 8, DBSIZE) },
    { DRDATA (BPTR, mt_ptr, DB_V_SIZE + 1) },
    { DRDATA (BMAX, mt_max, DB_V_SIZE + 1) },
    { ORDATA (SC, mtd_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, mtd_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB mtd_mod[] = {
    { MTAB_XTD | MTAB_VDV,            1, "SC",    "SC",    &hp_setsc,  &hp_showsc,  &mtd_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "DEVNO", "DEVNO", &hp_setdev, &hp_showdev, &mtd_dev },
    { 0 }
    };

DEVICE mtd_dev = {
    "MTD", &mtd_unit, mtd_reg, mtd_mod,
    1, 10, 16, 1, 8, 8,
    NULL, NULL, &mt_reset,
    NULL, NULL, NULL,
    &mtd_dib, DEV_DISABLE | DEV_DIS
    };

/* MTC data structures

   mtc_dev      MTC device descriptor
   mtc_unit     MTC unit list
   mtc_reg      MTC register list
   mtc_mod      MTC modifier list
*/

UNIT mtc_unit = { UDATA (&mtc_svc, UNIT_ATTABLE + UNIT_ROABLE, 0) };

REG mtc_reg[] = {
    { ORDATA (FNC, mtc_fnc, 8) },
    { ORDATA (STA, mtc_sta, 9) },
    { ORDATA (BUF, mtc_unit.buf, 8) },
    { FLDATA (CTL, mtc.control, 0) },
    { FLDATA (FLG, mtc.flag,    0) },
    { FLDATA (FBF, mtc.flagbuf, 0) },
    { FLDATA (DTF, mtc_dtf, 0) },
    { FLDATA (FSVC, mtc_1st, 0) },
    { DRDATA (POS, mtc_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (CTIME, mtc_ctime, 24), REG_NZ + PV_LEFT },
    { DRDATA (GTIME, mtc_gtime, 24), REG_NZ + PV_LEFT },
    { DRDATA (XTIME, mtc_xtime, 24), REG_NZ + PV_LEFT },
    { FLDATA (STOP_IOE, mtc_stopioe, 0) },
    { ORDATA (SC, mtc_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, mtc_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB mtc_mod[] = {
    { MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL },
    { MTAB_XTD | MTAB_VDV, 0, "FORMAT", "FORMAT",
      &sim_tape_set_fmt, &sim_tape_show_fmt, NULL },
    { MTAB_XTD | MTAB_VDV,            1, "SC",    "SC",    &hp_setsc,  &hp_showsc,  &mtd_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "DEVNO", "DEVNO", &hp_setdev, &hp_showdev, &mtd_dev },
    { 0 }
    };

DEVICE mtc_dev = {
    "MTC", &mtc_unit, mtc_reg, mtc_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &mt_reset,
    NULL, &mtc_attach, &mtc_detach,
    &mtc_dib, DEV_DISABLE | DEV_DIS | DEV_TAPE
    };


/* Data channel I/O signal handler

   The 12559A data channel interface has a number of non-standard features:

     - The card does not drive PRL or IRQ.
     - The card does not respond to IAK.
     - There is no control flip-flop; CLC resets the data transfer flip-flop.
     - POPIO issues a CLR command and clears the flag and flag buffer flip-flops.
     - CRS is not used.

   Implementation notes:

    1. The data channel has a flag buffer flip-flop (necessary for the proper
       timing of the flag flip-flop), but the data channel does not interrupt,
       so the flag buffer serves no other purpose.
*/

uint32 mtdio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            mtd.flag = mtd.flagbuf = CLEAR;
            break;

        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            mtd.flag = mtd.flagbuf = SET;
            break;

        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (mtd);
            break;

        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (mtd);
            break;

        case ioIOI:                                         /* I/O data input */
            stat_data = IORETURN (SCPE_OK, mtc_unit.buf);   /* merge in return status */
            break;

        case ioIOO:                                     /* I/O data output */
            mtc_unit.buf = IODATA (stat_data) & DMASK8; /* store data */
            break;

        case ioPOPIO:                                   /* power-on preset to I/O */
            mt_clear ();                                /* issue CLR to controller */
            mtd.flag = mtd.flagbuf = CLEAR;             /* clear flag and flag buffer */
            break;

        case ioCLC:                                     /* clear control flip-flop */
            mtc_dtf = 0;                                /* clr xfer flop */
            mtd.flag = mtd.flagbuf = CLEAR;             /* clear flag and flag buffer */
            break;

        case ioSIR:                                     /* set interrupt request */
            setstdSRQ (mtd);                            /* set standard SRQ signal */
            break;

        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Command channel I/O signal handler.

   The 12559A command interface is reasonably standard, although POPIO clears,
   rather than sets, the flag and flag buffer flip-flops.  One unusual feature
   is that commands are initiated when they are output to the interface with
   OTA/B, rather than waiting until control is set with STC.  STC simply enables
   command-channel interrupts.

   Implementation notes:

    1. In hardware, the command channel card passes PRH to PRL.  The data card
       actually drives PRL with the command channel's control and flag states.
       That is, the priority chain is broken at the data card, although the
       command card is interrupting.  This works in hardware, but we must break
       PRL at the command card under simulation to allow the command card to
       interrupt.

    2. In hardware, the CLR command takes 5 milliseconds to complete.  During
       this time, the BUSY bit is set in the status word.  Under simulation, we
       complete immediately, and the BUSY bit never sets..
*/

uint32 mtcio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
uint16 data;
uint32 i;
int32 valid;
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            mtc.flag = mtc.flagbuf = CLEAR;
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            mtc.flag = mtc.flagbuf = SET;
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (mtc);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (mtc);
            break;


        case ioIOI:                                     /* I/O data input */
            data = mtc_sta & ~(STA_LOCAL | STA_WLK | STA_BUSY);

            if (mtc_unit.flags & UNIT_ATT) {            /* construct status */
                if (sim_is_active (&mtc_unit))
                    data = data | STA_BUSY;

                if (sim_tape_wrp (&mtc_unit))
                    data = data | STA_WLK;
                }
            else
                data = data | STA_BUSY | STA_LOCAL;

            stat_data = IORETURN (SCPE_OK, data);       /* merge in return status */
            break;


        case ioIOO:                                         /* I/O data output */
            data = IODATA (stat_data) & DMASK8;
            mtc_sta = mtc_sta & ~STA_REJ;                   /* clear reject */

            if (data == FNC_CLR) {                          /* clear? */
                mt_clear ();                                /* send CLR to controller */

                mtd.flag = mtd.flagbuf = CLEAR;             /* clear data flag and flag buffer */
                mtc.flag = mtc.flagbuf = SET;               /* set command flag and flag buffer */
                break;                                      /* command completes immediately */
                }

            for (i = valid = 0; i < mtc_cmd_count; i++)     /* is fnc valid? */
                if (data == mtc_cmd[i]) {
                    valid = 1;
                    break;
                    }

            if (!valid || sim_is_active (&mtc_unit) ||      /* is cmd valid? */
               ((mtc_sta & STA_BOT) && (data == FNC_BSR)) ||
               (sim_tape_wrp (&mtc_unit) &&
                 ((data == FNC_WC) || (data == FNC_GAP) || (data == FNC_WFM))))
                mtc_sta = mtc_sta | STA_REJ;

            else {
                sim_activate (&mtc_unit, mtc_ctime);        /* start tape */
                mtc_fnc = data;                             /* save function */
                mtc_sta = STA_BUSY;                         /* unit busy */
                mt_ptr = 0;                                 /* init buffer ptr */

                mtcio (&mtc_dib, ioCLF, 0);                 /* clear flags */
                mtcio (&mtd_dib, ioCLF, 0);

                mtc_1st = 1;                                /* set 1st flop */
                mtc_dtf = 1;                                /* set xfer flop */
                }
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            mtc.flag = mtc.flagbuf = CLEAR;             /* clear flag and flag buffer */
            break;


        case ioCRS:                                     /* control reset */
        case ioCLC:                                     /* clear control flip-flop */
            mtc.control = CLEAR;
            break;


        case ioSTC:                                     /* set control flip-flop */
            mtc.control = SET;
            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (mtc);                            /* set standard PRL signal */
            setstdIRQ (mtc);                            /* set standard IRQ signal */
            setstdSRQ (mtc);                            /* set standard SRQ signal */
            break;

        case ioIAK:                                     /* interrupt acknowledge */
            mtc.flagbuf = CLEAR;
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Unit service

   If rewind done, reposition to start of tape, set status
   else, do operation, set done, interrupt

   Can't be write locked, can only write lock detached unit
*/

t_stat mtc_svc (UNIT *uptr)
{
t_mtrlnt tbc;
t_stat st, r = SCPE_OK;

if ((mtc_unit.flags & UNIT_ATT) == 0) {                 /* offline? */
    mtc_sta = STA_LOCAL | STA_REJ;                      /* rejected */
    mtcio (&mtc_dib, ioENF, 0);                         /* set cch flg */
    return IOERROR (mtc_stopioe, SCPE_UNATT);
    }

switch (mtc_fnc) {                                      /* case on function */

    case FNC_REW:                                       /* rewind */
        sim_tape_rewind (uptr);                         /* BOT */
        mtc_sta = STA_BOT;                              /* update status */
        break;

    case FNC_RWS:                                       /* rewind and offline */
        sim_tape_rewind (uptr);                         /* clear position */
        return sim_tape_detach (uptr);                  /* don't set cch flg */

    case FNC_WFM:                                       /* write file mark */
        st = sim_tape_wrtmk (uptr);                     /* write tmk */
        if (st != MTSE_OK)                              /* error? */
            r = mt_map_err (uptr, st);                  /* map error */
        mtc_sta = STA_EOF;                              /* set EOF status */
        break;

    case FNC_GAP:                                       /* erase gap */
        break;

    case FNC_FSR:                                       /* space forward */
        st = sim_tape_sprecf (uptr, &tbc);              /* space rec fwd */
        if (st != MTSE_OK)                              /* error? */
            r = mt_map_err (uptr, st);                  /* map error */
        break;

    case FNC_BSR:                                       /* space reverse */
        st = sim_tape_sprecr (uptr, &tbc);              /* space rec rev */
        if (st != MTSE_OK)                              /* error? */
            r = mt_map_err (uptr, st);                  /* map error */
        break;

    case FNC_RC:                                        /* read */
        if (mtc_1st) {                                  /* first svc? */
            mtc_1st = mt_ptr = 0;                       /* clr 1st flop */
            st = sim_tape_rdrecf (uptr, mtxb, &mt_max, DBSIZE); /* read rec */
            if (st == MTSE_RECE) mtc_sta = mtc_sta | STA_PAR;   /* rec in err? */
            else if (st != MTSE_OK) {                   /* other error? */
                r = mt_map_err (uptr, st);              /* map error */
                if (r == SCPE_OK) {                     /* recoverable? */
                    sim_activate (uptr, mtc_gtime);     /* sched IRG */
                    mtc_fnc = 0;                        /* NOP func */
                    return SCPE_OK;
                    }
                break;                                  /* non-recov, done */
                }
            if (mt_max < 12) {                          /* record too short? */
                mtc_sta = mtc_sta | STA_PAR;            /* set flag */
                break;
                }
            }
        if (mtc_dtf && (mt_ptr < mt_max)) {             /* more chars? */
            if (mtd.flag) mtc_sta = mtc_sta | STA_TIM;
            mtc_unit.buf = mtxb[mt_ptr++];              /* fetch next */
            mtdio (&mtd_dib, ioENF, 0);                 /* set dch flg */
            sim_activate (uptr, mtc_xtime);             /* re-activate */
            return SCPE_OK;
            }
        sim_activate (uptr, mtc_gtime);                 /* schedule gap */
        mtc_fnc = 0;                                    /* nop */
        return SCPE_OK;

    case FNC_WC:                                                /* write */
        if (mtc_1st) mtc_1st = 0;                       /* no xfr on first */
        else {
            if (mt_ptr < DBSIZE) {                      /* room in buffer? */
                mtxb[mt_ptr++] = mtc_unit.buf;
                mtc_sta = mtc_sta & ~STA_BOT;           /* clear BOT */
                }
            else mtc_sta = mtc_sta | STA_PAR;
            }
        if (mtc_dtf) {                                  /* xfer flop set? */
            mtdio (&mtd_dib, ioENF, 0);                 /* set dch flg */
            sim_activate (uptr, mtc_xtime);             /* re-activate */
            return SCPE_OK;
            }
        if (mt_ptr) {                                   /* write buffer */
            st = sim_tape_wrrecf (uptr, mtxb, mt_ptr);  /* write */
            if (st != MTSE_OK) {                        /* error? */
                r = mt_map_err (uptr, st);              /* map error */
                break;                                  /* done */
                }
            }
        sim_activate (uptr, mtc_gtime);                 /* schedule gap */
        mtc_fnc = 0;                                    /* nop */
        return SCPE_OK;

    default:                                            /* unknown */
        break;
        }

mtcio (&mtc_dib, ioENF, 0);                             /* set cch flg */
mtc_sta = mtc_sta & ~STA_BUSY;                          /* not busy */
return r;
}

/* Map tape error status */

t_stat mt_map_err (UNIT *uptr, t_stat st)
{
switch (st) {

    case MTSE_FMT:                                      /* illegal fmt */
    case MTSE_UNATT:                                    /* unattached */
        mtc_sta = mtc_sta | STA_REJ;                    /* reject */
    case MTSE_OK:                                       /* no error */
        return SCPE_IERR;                               /* never get here! */

    case MTSE_EOM:                                      /* end of medium */
    case MTSE_TMK:                                      /* end of file */
        mtc_sta = mtc_sta | STA_EOF;                    /* eof */
        break;

    case MTSE_IOERR:                                    /* IO error */
        mtc_sta = mtc_sta | STA_PAR;                    /* error */
        if (mtc_stopioe) return SCPE_IOERR;
        break;

    case MTSE_INVRL:                                    /* invalid rec lnt */
        mtc_sta = mtc_sta | STA_PAR;
        return SCPE_MTRLNT;

    case MTSE_RECE:                                     /* record in error */
        mtc_sta = mtc_sta | STA_PAR;                    /* error */
        break;

    case MTSE_BOT:                                      /* reverse into BOT */
        mtc_sta = mtc_sta | STA_BOT;                    /* set status */
        break;

    case MTSE_WRP:                                      /* write protect */
        mtc_sta = mtc_sta | STA_REJ;                    /* reject */
        break;
        }

return SCPE_OK;
}


/* Controller clear */

t_stat mt_clear (void)
{
t_stat st;

if (sim_is_active (&mtc_unit) &&                        /* write in prog? */
    (mtc_fnc == FNC_WC) && (mt_ptr > 0)) {              /* yes, bad rec */
    st = sim_tape_wrrecf (&mtc_unit, mtxb, mt_ptr | MTR_ERF);
    if (st != MTSE_OK)
        mt_map_err (&mtc_unit, st);
    }

if (((mtc_fnc == FNC_REW) || (mtc_fnc == FNC_RWS)) && sim_is_active (&mtc_unit))
    sim_cancel (&mtc_unit);

mtc_1st = mtc_dtf = 0;
mtc_sta = mtc_sta & STA_BOT;

return SCPE_OK;
}


/* Reset routine */

t_stat mt_reset (DEVICE *dptr)
{
DIB *dibptr = (DIB *) dptr->ctxt;                       /* DIB pointer */

hp_enbdis_pair (dptr,                                   /* make pair cons */
    (dptr == &mtd_dev) ? &mtc_dev : &mtd_dev);

IOPRESET (dibptr);                                      /* PRESET device (does not use PON) */

mtc_fnc = 0;
mtc_1st = mtc_dtf = 0;

sim_cancel (&mtc_unit);                                 /* cancel activity */
sim_tape_reset (&mtc_unit);

if (mtc_unit.flags & UNIT_ATT)
    mtc_sta = (sim_tape_bot (&mtc_unit)? STA_BOT: 0) |
              (sim_tape_wrp (&mtc_unit)? STA_WLK: 0);
else
    mtc_sta = STA_LOCAL | STA_BUSY;

return SCPE_OK;
}


/* Attach routine */

t_stat mtc_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = sim_tape_attach (uptr, cptr);                       /* attach unit */
if (r != SCPE_OK) return r;                             /* update status */
mtc_sta = STA_BOT;
return r;
}

/* Detach routine */

t_stat mtc_detach (UNIT* uptr)
{
mtc_sta = 0;                                            /* update status */
return sim_tape_detach (uptr);                          /* detach unit */
}
