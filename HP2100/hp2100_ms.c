/* hp2100_ms.c: HP 2100 13181A/13183A magnetic tape simulator

   Copyright (c) 1993-2014, Robert M. Supnik

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

   MS           13181A 7970B 800bpi nine track magnetic tape
                13183A 7970E 1600bpi nine track magnetic tape

   11-Dec-14    JDB     Updated for new erase gap API, added CRCC/LRCC support
   10-Jan-13    MP      Added DEV_TAPE to DEVICE flags
   09-May-12    JDB     Separated assignments from conditional expressions
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
                        Added CNTLR_TYPE cast to ms_settype
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   11-Aug-08    JDB     Revised to use AR instead of saved_AR in boot
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   18-Sep-06    JDB     Fixed 2nd CLR after WC causing another write
                        Improve debug reporting, add debug flags
   14-Sep-06    JDB     Removed local BOT flag, now uses sim_tape_bot
   30-Aug-06    JDB     Added erase gap support, improved tape lib err reporting
   07-Jul-06    JDB     Added CAPACITY as alternate for REEL
                        Fixed EOT test for unlimited reel size
   16-Feb-06    RMS     Revised for new EOT test
   22-Jul-05    RMS     Fixed compiler warning on Solaris (from Doug Glyn)
   01-Mar-05    JDB     Added SET OFFLINE; rewind/offline now does not detach
   07-Oct-04    JDB     Fixed enable/disable from either device
   14-Aug-04    JDB     Fixed many functional and timing problems (from Dave Bryan)
                        - fixed erroneous execution of rejected command
                        - fixed erroneous execution of select-only command
                        - fixed erroneous execution of clear command
                        - fixed odd byte handling for read
                        - fixed spurious odd byte status on 13183A EOF
                        - modified handling of end of medium
                        - added detailed timing, with fast and realistic modes
                        - added reel sizes to simulate end of tape
                        - added debug printouts
   06-Jul-04    RMS     Fixed spurious timing error after CLC (found by Dave Bryan)
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Fixed SR setting in IBL
                        Revised IBL loader
                        Implemented DMA SRQ (follows FLG)
   25-Apr-03    RMS     Revised for extended file support
   28-Mar-03    RMS     Added multiformat support
   28-Feb-03    RMS     Revised for magtape library
   18-Oct-02    RMS     Added BOOT command, added 13183A support
   30-Sep-02    RMS     Revamped error handling
   29-Aug-02    RMS     Added end of medium support
   30-May-02    RMS     Widened POS to 32b
   22-Apr-02    RMS     Added maximum record length test

   References:
   - 13181B Digital Magnetic Tape Unit Interface Kit Operating and Service Manual
            (13181-90901, Nov-1982)
   - 13183B Digital Magnetic Tape Unit Interface Kit Operating and Service Manual
            (13183-90901, Nov-1983)
   - SIMH Magtape Representation and Handling (Bob Supnik, 30-Aug-2006)
*/


#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "sim_tape.h"

#define UNIT_V_OFFLINE  (MTUF_V_UF + 0)                 /* unit offline */
#define UNIT_OFFLINE    (1 << UNIT_V_OFFLINE)

#define MS_NUMDR        4                               /* number of drives */
#define DB_N_SIZE       16                              /* max data buf */
#define DBSIZE          (1 << DB_N_SIZE)                /* max data cmd */
#define FNC             u3                              /* function */
#define UST             u4                              /* unit status */
#define REEL            u5                              /* tape reel size */

#define BPI_13181       MT_DENS_800                     /* 800 bpi for 13181 cntlr */
#define BPI_13183       MT_DENS_1600                    /* 1600 bpi for 13183 cntlr */
#define GAP_13181       48                              /* gap is 4.8 inches for 13181 cntlr */
#define GAP_13183       30                              /* gap is 3.0 inches for 13183 cntlr */
#define TCAP            (300 * 12 * 800)                /* 300 ft capacity at 800 bpi */

/* Debug flags */

#define DEB_CMDS        (1 << 0)                        /* command init and compl */
#define DEB_CPU         (1 << 1)                        /* CPU I/O */
#define DEB_RWS         (1 << 2)                        /* tape reads, writes, status */

/* Command - msc_fnc */

#define FNC_CLR         00110                           /* clear */
#define FNC_GAP         00015                           /* write gap */
#define FNC_GFM         00215                           /* gap+file mark */
#define FNC_RC          00023                           /* read */
#define FNC_WC          00031                           /* write */
#define FNC_FSR         00003                           /* forward space */
#define FNC_BSR         00041                           /* backward space */
#define FNC_FSF         00203                           /* forward file */
#define FNC_BSF         00241                           /* backward file */
#define FNC_REW         00101                           /* rewind */
#define FNC_RWS         00105                           /* rewind and offline */
#define FNC_WFM         00211                           /* write file mark */
#define FNC_RFF         00223                           /* read file fwd (diag) */
#define FNC_RRR         00061                           /* read record rev (diag) */
#define FNC_CMPL        00400                           /* completion state */
#define FNC_V_SEL       9                               /* select */
#define FNC_M_SEL       017
#define FNC_GETSEL(x)   (((x) >> FNC_V_SEL) & FNC_M_SEL)

#define FNF_MOT         00001                           /* motion */
#define FNF_OFL         00004
#define FNF_WRT         00010                           /* write */
#define FNF_REV         00040                           /* reverse */
#define FNF_RWD         00100                           /* rewind */
#define FNF_CHS         00400                           /* change select */

#define FNC_SEL         ((FNC_M_SEL << FNC_V_SEL) | FNF_CHS)

/* Status - stored in msc_sta, unit.UST (u), or dynamic (d) */

#define STA_PE          0100000                         /* 1600 bpi (d) */
#define STA_V_SEL       13                              /* unit sel (d) */
#define STA_M_SEL       03
#define STA_SEL         (STA_M_SEL << STA_V_SEL)
#define STA_ODD         0004000                         /* odd bytes */
#define STA_REW         0002000                         /* rewinding (u) */
#define STA_TBSY        0001000                         /* transport busy (d) */
#define STA_BUSY        0000400                         /* ctrl busy */
#define STA_EOF         0000200                         /* end of file */
#define STA_BOT         0000100                         /* beg of tape (d) */
#define STA_EOT         0000040                         /* end of tape (d) */
#define STA_TIM         0000020                         /* timing error */
#define STA_REJ         0000010                         /* programming error */
#define STA_WLK         0000004                         /* write locked (d) */
#define STA_PAR         0000002                         /* parity error */
#define STA_LOCAL       0000001                         /* local (d) */
#define STA_DYN         (STA_PE  | STA_SEL | STA_TBSY | STA_BOT | \
                         STA_EOT | STA_WLK | STA_LOCAL)

/* Controller types */

typedef enum {
    A13181,
    A13183
    } CNTLR_TYPE;

CNTLR_TYPE ms_ctype = A13181;                           /* ctrl type */
int32 ms_timing = 1;                                    /* timing type */

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } msc = { CLEAR, CLEAR, CLEAR };

int32 msc_sta = 0;                                      /* status */
int32 msc_buf = 0;                                      /* buffer */
int32 msc_usl = 0;                                      /* unit select */
int32 msc_1st = 0;                                      /* first service */
int32 msc_stopioe = 1;                                  /* stop on error */

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } msd = { CLEAR, CLEAR, CLEAR };

int32 msd_buf = 0;                                      /* data buffer */
uint8 msxb[DBSIZE] = { 0 };                             /* data buffer */
t_mtrlnt ms_ptr = 0, ms_max = 0;                        /* buffer ptrs */
t_bool ms_crc = FALSE;                                  /* buffer ready for CRC calc */


/* Hardware timing at 45 IPS                  13181                  13183
   (based on 1580 instr/msec)          instr   msec    SCP   instr    msec    SCP
                                      --------------------   --------------------
   - BOT start delay        : btime = 161512  102.22   184   252800  160.00   288
   - motion cmd start delay : ctime =  14044    8.89    16    17556   11.11    20
   - GAP traversal time     : gtime = 175553  111.11   200   105333   66.67   120
   - IRG traversal time     : itime =  24885   15.75     -    27387   17.33     -
   - rewind initiation time : rtime =    878    0.56     1      878    0.56     1
   - data xfer time / word  : xtime =     88   55.56us   -       44  27.78us    -

   NOTE: The 13181-60001 Rev. 1629 tape diagnostic fails test 17B subtest 6 with
         "E116 BYTE TIME SHORT" if the correct data transfer time is used for
          13181A interface.  Set "xtime" to 115 (instructions) to pass that
          diagnostic.  Rev. 2040 of the tape diagnostic fixes this problem and
          passes with the correct data transfer time.
*/

int32 msc_btime = 0;                                    /* BOT start delay */
int32 msc_ctime = 0;                                    /* motion cmd start delay */
int32 msc_gtime = 0;                                    /* GAP traversal time */
int32 msc_itime = 0;                                    /* IRG traversal time */
int32 msc_rtime = 0;                                    /* rewind initiation time */
int32 msc_xtime = 0;                                    /* data xfer time / word */

typedef int32 TIMESET[6];                               /* set of controller times */

int32 *const timers[] = { &msc_btime, &msc_ctime, &msc_gtime,
                          &msc_itime, &msc_rtime, &msc_xtime };

const TIMESET msc_times[3] = {
    { 161512, 14044, 175553, 24885, 878,  88 },         /* 13181A */
    { 252800, 17556, 105333, 27387, 878,  44 },         /* 13183A */
    {      1,  1000,      1,     1, 100,  10 }          /* FAST */
    };

DEVICE msd_dev, msc_dev;

IOHANDLER msdio;
IOHANDLER mscio;

t_stat msc_svc (UNIT *uptr);
t_stat msc_reset (DEVICE *dptr);
t_stat msc_attach (UNIT *uptr, char *cptr);
t_stat msc_detach (UNIT *uptr);
t_stat msc_online (UNIT *uptr, int32 value, char *cptr, void *desc);
t_stat msc_boot (int32 unitno, DEVICE *dptr);
t_stat ms_write_gap (UNIT *uptr);
t_stat ms_map_err (UNIT *uptr, t_stat st);
t_stat ms_settype (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat ms_showtype (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat ms_set_timing (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat ms_show_timing (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat ms_set_reelsize (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat ms_show_reelsize (FILE *st, UNIT *uptr, int32 val, void *desc);
void ms_config_timing (void);
char *ms_cmd_name (uint32 cmd);
t_stat ms_clear (void);
static uint32 calc_crc_lrc (uint8 *buffer, t_mtrlnt length);


/* MSD data structures

   msd_dev      MSD device descriptor
   msd_unit     MSD unit list
   msd_reg      MSD register list
*/

DIB ms_dib[] = {
    { &msdio, MSD },
    { &mscio, MSC }
    };

#define msd_dib ms_dib[0]
#define msc_dib ms_dib[1]

UNIT msd_unit = { UDATA (NULL, 0, 0) };

REG msd_reg[] = {
    { ORDATA (BUF, msd_buf, 16) },
    { FLDATA (CTL, msd.control, 0) },
    { FLDATA (FLG, msd.flag, 0) },
    { FLDATA (FBF, msd.flagbuf, 0) },
    { BRDATA (DBUF, msxb, 8, 8, DBSIZE) },
    { DRDATA (BPTR, ms_ptr, DB_N_SIZE + 1) },
    { DRDATA (BMAX, ms_max, DB_N_SIZE + 1) },
    { ORDATA (SC, msd_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, msd_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB msd_mod[] = {
    { MTAB_XTD | MTAB_VDV,            1, "SC",    "SC",    &hp_setsc,  &hp_showsc,  &msd_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "DEVNO", "DEVNO", &hp_setdev, &hp_showdev, &msd_dev },
    { 0 }
    };

DEVICE msd_dev = {
    "MSD", &msd_unit, msd_reg, msd_mod,
    1, 10, DB_N_SIZE, 1, 8, 8,
    NULL, NULL, &msc_reset,
    NULL, NULL, NULL,
    &msd_dib, DEV_DISABLE
    };

/* MSC data structures

   msc_dev      MSC device descriptor
   msc_unit     MSC unit list
   msc_reg      MSC register list
   msc_mod      MSC modifier list
   msc_deb      MSC debug flags
*/

UNIT msc_unit[] = {
    { UDATA (&msc_svc, UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_OFFLINE, 0) },
    { UDATA (&msc_svc, UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_OFFLINE, 0) },
    { UDATA (&msc_svc, UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_OFFLINE, 0) },
    { UDATA (&msc_svc, UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_OFFLINE, 0) }
    };

REG msc_reg[] = {
    { ORDATA (STA, msc_sta, 12) },
    { ORDATA (BUF, msc_buf, 16) },
    { ORDATA (USEL, msc_usl, 2) },
    { FLDATA (FSVC, msc_1st, 0) },
    { FLDATA (CTL, msc.control, 0) },
    { FLDATA (FLG, msc.flag, 0) },
    { FLDATA (FBF, msc.flagbuf, 0) },
    { URDATA (POS, msc_unit[0].pos, 10, T_ADDR_W, 0, MS_NUMDR, PV_LEFT) },
    { URDATA (FNC, msc_unit[0].FNC, 8, 8, 0, MS_NUMDR, REG_HRO) },
    { URDATA (UST, msc_unit[0].UST, 8, 12, 0, MS_NUMDR, REG_HRO) },
    { URDATA (REEL, msc_unit[0].REEL, 10, 2, 0, MS_NUMDR, REG_HRO) },
    { DRDATA (BTIME, msc_btime, 24), REG_NZ + PV_LEFT },
    { DRDATA (CTIME, msc_ctime, 24), REG_NZ + PV_LEFT },
    { DRDATA (GTIME, msc_gtime, 24), REG_NZ + PV_LEFT },
    { DRDATA (ITIME, msc_itime, 24), REG_NZ + PV_LEFT },
    { DRDATA (RTIME, msc_rtime, 24), REG_NZ + PV_LEFT },
    { DRDATA (XTIME, msc_xtime, 24), REG_NZ + PV_LEFT },
    { FLDATA (TIMING, ms_timing, 0), REG_HRO },
    { FLDATA (STOP_IOE, msc_stopioe, 0) },
    { FLDATA (CTYPE, ms_ctype, 0), REG_HRO },
    { ORDATA (SC, msc_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, msc_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB msc_mod[] = {
    { UNIT_OFFLINE, UNIT_OFFLINE, "offline", "OFFLINE", NULL },
    { UNIT_OFFLINE, 0, "online", "ONLINE", msc_online },
    { MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL },
    { MTAB_XTD | MTAB_VUN, 0, "CAPACITY", "CAPACITY",
       &ms_set_reelsize, &ms_show_reelsize, NULL },
    { MTAB_XTD | MTAB_VUN | MTAB_NMO, 1, "REEL", "REEL",
      &ms_set_reelsize, &ms_show_reelsize, NULL },
    { MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
      &sim_tape_set_fmt, &sim_tape_show_fmt, NULL },
    { MTAB_XTD | MTAB_VDV, 0, NULL, "13181A",
      &ms_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, 1, NULL, "13183A",
      &ms_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, 0, "TYPE", NULL,
      NULL, &ms_showtype, NULL },
    { MTAB_XTD | MTAB_VDV, 0, NULL, "REALTIME",
      &ms_set_timing, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, 1, NULL, "FASTTIME",
      &ms_set_timing, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, 0, "TIMING", NULL,
      NULL, &ms_show_timing, NULL },
    { MTAB_XTD | MTAB_VDV,            1, "SC",    "SC",    &hp_setsc,  &hp_showsc,  &msd_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "DEVNO", "DEVNO", &hp_setdev, &hp_showdev, &msd_dev },
    { 0 }
    };

DEBTAB msc_deb[] = {
    { "CMDS", DEB_CMDS },
    { "CPU", DEB_CPU },
    { "RWS", DEB_RWS },
    { NULL, 0 }
    };

DEVICE msc_dev = {
    "MSC", msc_unit, msc_reg, msc_mod,
    MS_NUMDR, 10, 31, 1, 8, 8,
    NULL, NULL, &msc_reset,
    &msc_boot, &msc_attach, &msc_detach,
    &msc_dib, DEV_DISABLE | DEV_DEBUG | DEV_TAPE,
    0, msc_deb, NULL, NULL
    };


/* Data channel I/O signal handler */

uint32 msdio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
uint32   check;
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */
        case ioCLF:                                     /* clear flag flip-flop */
            msd.flag = msd.flagbuf = CLEAR;
            break;

        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            msd.flag = msd.flagbuf = SET;
            break;

        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (msd);
            break;

        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (msd);
            break;

        case ioIOI:                                     /* I/O data input */
            if (ms_crc) {                               /* ready for CRC? */
                check = calc_crc_lrc (msxb, ms_max);    /* calculate CRCC and LRCC */
                msd_buf = check >> 8 & 0177400          /* position CRCC in upper byte */
                            | check & 0377;             /*   and LRCC in lower byte */
                }

            stat_data = IORETURN (SCPE_OK, msd_buf);    /* merge in return status */
            break;

        case ioIOO:                                     /* I/O data output */
            msd_buf = IODATA (stat_data);               /* store data */
            break;

        case ioPOPIO:                                   /* power-on preset to I/O */
            ms_clear ();                                /* issue CLR to controller */
            break;

        case ioCRS:                                     /* control reset */
            msd.flag = msd.flagbuf = SET;               /* set flag and flag buffer */
                                                        /* fall into CLC handler */
        case ioCLC:                                     /* clear control flip-flop */
            msd.control = CLEAR;
            break;

        case ioSTC:                                     /* set control flip-flop */
            ms_crc = FALSE;                             /* reset CRC ready */
            msd.control = SET;
            break;

        case ioEDT:                                     /* end data transfer */
            msd.flag = msd.flagbuf = CLEAR;             /* same as CLF */
            break;

        case ioSIR:                                     /* set interrupt request */
            setstdPRL (msd);                            /* set standard PRL signal */
            setstdIRQ (msd);                            /* set standard IRQ signal */
            setstdSRQ (msd);                            /* set standard SRQ signal */
            break;

        case ioIAK:                                     /* interrupt acknowledge */
            msd.flagbuf = CLEAR;
            break;

        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Command channel I/O signal handler.

   Implementation notes:

    1. Commands are usually initiated with an STC cc,C instruction.  The CLR
       command completes immediately and sets the flag.  This requires that we
       ignore the CLF part (but still process the SIR).

    2. The command channel card clears its flag and flag buffer on EDT, but as
       it never asserts SRQ, it will never get EDT.  Under simulation, we omit
       the EDT handler.

    3. In hardware, the command channel card passes PRH to PRL.  The data card
       actually drives PRL with both channels' control and flag states.  That
       is, the priority chain is broken at the data card, even when the command
       card is interrupting.  This works in hardware, but we must break PRL at
       the command card under simulation to allow the command card to interrupt.
*/

uint32 mscio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
static const uint8 map_sel[16] = {
    0, 0, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3
    };
uint16 data;
int32 sched_time;
UNIT *uptr = msc_dev.units + msc_usl;

IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            msc.flag = msc.flagbuf = CLEAR;
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            msc.flag = msc.flagbuf = SET;
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (msc);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (msc);
            break;


        case ioIOI:                                     /* I/O data input */
            data = msc_sta & ~STA_DYN;                  /* get card status */

            if ((uptr->flags & UNIT_OFFLINE) == 0) {    /* online? */
                data = data | uptr->UST;                /* add unit status */

                if (sim_tape_bot (uptr))                /* BOT? */
                    data = data | STA_BOT;

                if (sim_is_active (uptr) &&             /* TBSY unless RWD at BOT */
                    !((uptr->FNC & FNF_RWD) && sim_tape_bot (uptr)))
                    data = data | STA_TBSY;

                if (sim_tape_wrp (uptr))                /* write prot? */
                    data = data | STA_WLK;

                if (sim_tape_eot (uptr))                /* EOT? */
                    data = data | STA_EOT;
                }

            else
                data = data | STA_TBSY | STA_LOCAL;

            if (ms_ctype == A13183)                     /* 13183A? */
                data = data | STA_PE | (msc_usl << STA_V_SEL);

            if (DEBUG_PRI (msc_dev, DEB_CPU))
                fprintf (sim_deb, ">>MSC LIx: Status = %06o\n", data);

            stat_data = IORETURN (SCPE_OK, data);       /* merge in return status */
            break;


        case ioIOO:                                         /* I/O data output */
            msc_buf = IODATA (stat_data);                   /* clear supplied status */

            if (DEBUG_PRI (msc_dev, DEB_CPU))
                fprintf (sim_deb, ">>MSC OTx: Command = %06o\n", msc_buf);

            msc_sta = msc_sta & ~STA_REJ;                   /* clear reject */

            if ((msc_buf & 0377) == FNC_CLR)                /* clear always ok */
                break;

            if (msc_sta & STA_BUSY) {                       /* busy? reject */
                msc_sta = msc_sta | STA_REJ;                /* dont chg select */
                break;
                }

            if (msc_buf & FNF_CHS) {                        /* select change */
                msc_usl = map_sel[FNC_GETSEL (msc_buf)];    /* is immediate */
                uptr = msc_dev.units + msc_usl;
                if (DEBUG_PRI (msc_dev, DEB_CMDS))
                    fprintf (sim_deb, ">>MSC OTx: Unit %d selected\n", msc_usl);
                }

            if (((msc_buf & FNF_MOT) && sim_is_active (uptr)) ||
                ((msc_buf & FNF_REV) && sim_tape_bot (uptr)) ||
                ((msc_buf & FNF_WRT) && sim_tape_wrp (uptr)))
                msc_sta = msc_sta | STA_REJ;                /* reject? */

            break;


        case ioCRS:                                     /* control reset */
            msc.flag = msc.flagbuf = SET;               /* set flag and flag buffer */
                                                        /* fall into CLC handler */

        case ioCLC:                                     /* clear control flip-flop */
            msc.control = CLEAR;
            break;


        case ioSTC:                                     /* set control flip-flop */
            if (!(msc_sta & STA_REJ)) {                 /* last cmd rejected? */
                if ((msc_buf & 0377) == FNC_CLR) {      /* clear? */
                    ms_clear ();                        /* issue CLR to controller */

                    msc.control = SET;                  /* set CTL for STC */
                    msc.flag = msc.flagbuf = SET;       /* set FLG for completion */

                    working_set = working_set & ~ioCLF; /* eliminate possible CLF */

                    if (DEBUG_PRI (msc_dev, DEB_CMDS))
                        fputs (">>MSC STC: Controller cleared\n", sim_deb);

                    break;                              /* command completes immediately */
                    }

                uptr->FNC = msc_buf & 0377;             /* save function */

                if (uptr->FNC & FNF_RWD) {              /* rewind? */
                    if (!sim_tape_bot (uptr))           /* not at BOT? */
                        uptr->UST = STA_REW;            /* set rewinding */

                    sched_time = msc_rtime;             /* set response time */
                    }

                else {
                    if (sim_tape_bot (uptr))            /* at BOT? */
                        sched_time = msc_btime;         /* use BOT start time */

                    else if ((uptr->FNC == FNC_GAP) || (uptr->FNC == FNC_GFM))
                        sched_time = msc_gtime;         /* use gap traversal time */

                    else sched_time = 0;

                    if (uptr->FNC != FNC_GAP)
                        sched_time += msc_ctime;        /* add base command time */
                    }

                if (msc_buf & ~FNC_SEL) {               /* NOP for unit sel alone */
                    sim_activate (uptr, sched_time);    /* else schedule op */

                    if (DEBUG_PRI (msc_dev, DEB_CMDS))
                        fprintf (sim_deb,
                            ">>MSC STC: Unit %d command %03o (%s) scheduled, "
                            "pos = %d, time = %d\n",
                            msc_usl, uptr->FNC, ms_cmd_name (uptr->FNC),
                            uptr->pos, sched_time);
                    }

                else if (DEBUG_PRI (msc_dev, DEB_CMDS))
                    fputs (">>MSC STC: Unit select (NOP)\n", sim_deb);

                msc_sta = STA_BUSY;                     /* ctrl is busy */
                msc_1st = 1;
                msc.control = SET;                      /* go */
                }
            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (msc);                            /* set standard PRL signal */
            setstdIRQ (msc);                            /* set standard IRQ signal */
            setstdSRQ (msc);                            /* set standard SRQ signal */
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            msc.flagbuf = CLEAR;
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
   else, do operation, set done, interrupt.

   In addition to decreasing the timing intervals, the FASTTIME option enables
   two additional optimizations: WFM for GFM substitution, and BOT gap
   elimination.  If FASTTIME is selected, gap and file mark (GFM) commands are
   processed as WFM (write file mark) commands.  That is, the preceding GAP is
   not performed.  Also, the initial gap that normally precedes the first data
   record or EOF mark at the beginning of the tape is omitted.  These omissions
   result in smaller tape image files.  If REALTIME is selected, the gaps are
   included.  Note that the gaps (and realistic timing) are necessary to pass
   the 7970 diagnostics.
*/

t_stat msc_svc (UNIT *uptr)
{
int32 unum;
t_mtrlnt tbc;
t_stat st, r = SCPE_OK;

unum = uptr - msc_unit;                                 /* get unit number */

if ((uptr->FNC != FNC_RWS) && (uptr->flags & UNIT_OFFLINE)) {  /* offline? */
    msc_sta = (msc_sta | STA_REJ) & ~STA_BUSY;          /* reject */
    mscio (&msc_dib, ioENF, 0);                         /* set flag */
    return IOERROR (msc_stopioe, SCPE_UNATT);
    }

switch (uptr->FNC) {                                    /* case on function */

    case FNC_RWS:                                       /* rewind offline */
        sim_tape_rewind (uptr);                         /* rewind tape */
        uptr->flags = uptr->flags | UNIT_OFFLINE;       /* set offline */
        uptr->UST = 0;                                  /* clear REW status */
        break;                                          /* we're done */

    case FNC_REW:                                       /* rewind */
        if (uptr->UST & STA_REW) {                      /* rewind in prog? */
            uptr->FNC |= FNC_CMPL;                      /* set compl state */
            sim_activate (uptr, msc_ctime);             /* sched completion */
            }
        break;                                          /* anyway, ctrl done */

    case FNC_REW | FNC_CMPL:                            /* complete rewind */
        sim_tape_rewind (uptr);                         /* rewind tape */
        uptr->UST = 0;                                  /* clear REW status */
        return SCPE_OK;                                 /* drive is free */

    case FNC_GFM:                                       /* gap + file mark */
        if (ms_timing == 1)                             /* fast timing? */
            goto DO_WFM;                                /* do plain file mark */
                                                        /* else fall into GAP */
    case FNC_GAP:                                       /* erase gap */
        if (DEBUG_PRI (msc_dev, DEB_RWS))
            fprintf (sim_deb,
                ">>MSC svc: Unit %d wrote gap\n",
                unum);
        if ((r = ms_write_gap (uptr)) ||                /* write tape gap; error? */
            (uptr->FNC != FNC_GFM))                     /* not GFM? */
            break;                                      /* bail out now */
                                                        /* else drop into WFM */
    case FNC_WFM:                                       /* write file mark */
        if ((ms_timing == 0) && sim_tape_bot (uptr)) {  /* realistic timing + BOT? */
            if (DEBUG_PRI (msc_dev, DEB_RWS))
                fprintf (sim_deb,
                    ">>MSC svc: Unit %d wrote initial gap\n",
                    unum);
            st = ms_write_gap (uptr);                   /* write initial gap*/
            if (st != MTSE_OK) {                        /* error? */
                r = ms_map_err (uptr, st);              /* map error */
                break;                                  /* terminate operation */
                }
            }
    DO_WFM:
        if (DEBUG_PRI (msc_dev, DEB_RWS))
            fprintf (sim_deb,
                ">>MSC svc: Unit %d wrote file mark\n",
                unum);
        st = sim_tape_wrtmk (uptr);                     /* write tmk */
        if (st != MTSE_OK)                              /* error? */
            r = ms_map_err (uptr, st);                  /* map error */
        msc_sta = STA_EOF;                              /* set EOF status */
        break;

    case FNC_FSR:                                       /* space forward */
        st = sim_tape_sprecf (uptr, &tbc);              /* space rec fwd */
        if (st != MTSE_OK)                              /* error? */
            r = ms_map_err (uptr, st);                  /* map error */
        if (tbc & 1)
            msc_sta = msc_sta | STA_ODD;
        else msc_sta = msc_sta & ~STA_ODD;
        break;

    case FNC_BSR:                                       /* space reverse */
        st = sim_tape_sprecr (uptr, &tbc);              /* space rec rev*/
        if (st != MTSE_OK)                              /* error? */
            r = ms_map_err (uptr, st);                  /* map error */
        if (tbc & 1)
            msc_sta = msc_sta | STA_ODD;
        else msc_sta = msc_sta & ~STA_ODD;
        break;

    case FNC_FSF:                                       /* space fwd file */
        while ((st = sim_tape_sprecf (uptr, &tbc)) == MTSE_OK) {
            if (sim_tape_eot (uptr)) break;             /* EOT stops */
            }
        r = ms_map_err (uptr, st);                      /* map error */
        break;

    case FNC_BSF:                                       /* space rev file */
        while ((st = sim_tape_sprecr (uptr, &tbc)) == MTSE_OK) ;
        r = ms_map_err (uptr, st);                      /* map error */
        break;

    case FNC_RFF:                                       /* diagnostic read */
    case FNC_RC:                                        /* read */
        if (msc_1st) {                                  /* first svc? */
            msc_1st = ms_ptr = ms_max = 0;              /* clr 1st flop */
            st = sim_tape_rdrecf (uptr, msxb, &ms_max, DBSIZE); /* read rec */
            if (DEBUG_PRI (msc_dev, DEB_RWS))
                fprintf (sim_deb,
                    ">>MSC svc: Unit %d read %d word record\n",
                    unum, ms_max / 2);
            if (st == MTSE_RECE) msc_sta = msc_sta | STA_PAR;   /* rec in err? */
            else if (st != MTSE_OK) {                   /* other error? */
                r = ms_map_err (uptr, st);              /* map error */
                if (r == SCPE_OK) {                     /* recoverable? */
                    sim_activate (uptr, msc_itime);     /* sched IRG */
                    uptr->FNC |= FNC_CMPL;              /* set completion */
                    return SCPE_OK;
                    }
                break;                                  /* err, done */
                }
            if (ms_ctype == A13183)
                msc_sta = msc_sta | STA_ODD;            /* set ODD for 13183A */
            }
        if (msd.control && (ms_ptr < ms_max)) {         /* DCH on, more data? */
            if (msd.flag) msc_sta = msc_sta | STA_TIM | STA_PAR;
            msd_buf = ((uint16) msxb[ms_ptr] << 8) |
                      ((ms_ptr + 1 == ms_max) ? 0 : msxb[ms_ptr + 1]);
            ms_ptr = ms_ptr + 2;
            msdio (&msd_dib, ioENF, 0);                 /* set flag */
            sim_activate (uptr, msc_xtime);             /* re-activate */
            return SCPE_OK;
            }
        if (ms_max & 1)                                 /* set ODD by rec len */
            msc_sta = msc_sta | STA_ODD;
        else msc_sta = msc_sta & ~STA_ODD;
        sim_activate (uptr, msc_itime);                 /* sched IRG */
        if (uptr->FNC == FNC_RFF)                       /* diagnostic? */
            msc_1st = 1;                                /* restart */
        else {
            uptr->FNC |= FNC_CMPL;                      /* set completion */
            ms_crc = TRUE;                              /*   and CRC ready */
            }
        return SCPE_OK;

    case FNC_RFF | FNC_CMPL:                            /* diagnostic read completion */
    case FNC_RC  | FNC_CMPL:                            /* read completion */
        break;

    case FNC_WC:                                        /* write */
        if (msc_1st) {                                  /* first service? */
            msc_1st = ms_ptr = 0;                       /* no data xfer on first svc */
            if ((ms_timing == 0) && sim_tape_bot (uptr)) {  /* realistic timing + BOT? */
                if (DEBUG_PRI (msc_dev, DEB_RWS))
                    fprintf (sim_deb,
                        ">>MSC svc: Unit %d wrote initial gap\n",
                        unum);
                st = ms_write_gap (uptr);               /* write initial gap */
                if (st != MTSE_OK) {                    /* error? */
                    r = ms_map_err (uptr, st);          /* map error */
                    break;                              /* terminate operation */
                    }
                }
            }
        else {                                          /* not 1st, next char */
            if (ms_ptr < DBSIZE) {                      /* room in buffer? */
                msxb[ms_ptr] = msd_buf >> 8;            /* store 2 char */
                msxb[ms_ptr + 1] = msd_buf & 0377;
                ms_ptr = ms_ptr + 2;
                }
            else msc_sta = msc_sta | STA_PAR;
           }
        if (msd.control) {                              /* xfer flop set? */
            msdio (&msd_dib, ioENF, 0);                 /* set flag */
            sim_activate (uptr, msc_xtime);             /* re-activate */
            return SCPE_OK;
            }
        if (ms_ptr) {                                   /* any data? write */
            if (DEBUG_PRI (msc_dev, DEB_RWS))
                fprintf (sim_deb,
                    ">>MSC svc: Unit %d wrote %d word record\n",
                    unum, ms_ptr / 2);
            st = sim_tape_wrrecf (uptr, msxb, ms_ptr);  /* write */
            if (st != MTSE_OK) {
                r = ms_map_err (uptr, st);              /* map error */
                break;
                }
            }
        sim_activate (uptr, msc_itime);                 /* sched IRG */
        uptr->FNC |= FNC_CMPL;                          /* set completion */
        ms_max = ms_ptr;                                /* indicate buffer complete */
        ms_crc = TRUE;                                  /*   and CRC may be generated */
        return SCPE_OK;

    case FNC_WC | FNC_CMPL:                             /* write completion */
        break;

    case FNC_RRR:                                       /* not supported */
    default:                                            /* unknown command */
        if (DEBUG_PRI (msc_dev, DEB_CMDS))
            fprintf (sim_deb,
                ">>MSC svc: Unit %d command %03o is unknown (NOP)\n",
                unum, uptr->FNC);
        break;
        }

mscio (&msc_dib, ioENF, 0);                             /* set flag */
msc_sta = msc_sta & ~STA_BUSY;                          /* update status */
if (DEBUG_PRI (msc_dev, DEB_CMDS))
     fprintf (sim_deb,
        ">>MSC svc: Unit %d command %03o (%s) complete\n",
        unum, uptr->FNC & 0377, ms_cmd_name (uptr->FNC));
return r;
}


/* Write an erase gap */

t_stat ms_write_gap (UNIT *uptr)
{
t_stat st;
uint32 gap_len = ms_ctype ? GAP_13183 : GAP_13181;      /* establish gap length */

st = sim_tape_wrgap (uptr, gap_len);                    /* write gap */

if (st != MTSE_OK)
    return ms_map_err (uptr, st);                       /* map error if failure */
else
    return SCPE_OK;
}


/* Map tape error status */

t_stat ms_map_err (UNIT *uptr, t_stat st)
{
int32 unum = uptr - msc_unit;                           /* get unit number */

if (DEBUG_PRI (msc_dev, DEB_RWS))
    fprintf (sim_deb,
        ">>MSC err: Unit %d tape library status = %d\n",
        unum, st);

switch (st) {

    case MTSE_FMT:                                      /* illegal fmt */
        msc_sta = msc_sta | STA_REJ;                    /* reject cmd */
        return SCPE_FMT;                                /* format error */

    case MTSE_UNATT:                                    /* unattached */
        msc_detach (uptr);                              /* resync status (ignore rtn) */
        msc_sta = msc_sta | STA_REJ;                    /* reject cmd */
        return SCPE_UNATT;                              /* unit unattached */

    case MTSE_OK:                                       /* no error */
        return SCPE_IERR;                               /* never get here! */

    case MTSE_EOM:                                      /* end of medium */
    case MTSE_TMK:                                      /* end of file */
        msc_sta = msc_sta | STA_EOF;

        if (ms_ctype == A13181)
            msc_sta = msc_sta | STA_ODD;                /* EOF also sets ODD for 13181A */
        break;

    case MTSE_INVRL:                                    /* invalid rec lnt */
        msc_sta = msc_sta | STA_PAR;
        return SCPE_MTRLNT;

    case MTSE_IOERR:                                    /* IO error */
        msc_sta = msc_sta | STA_PAR;                    /* error */
        if (msc_stopioe) return SCPE_IOERR;
        break;

    case MTSE_RECE:                                     /* record in error */
        msc_sta = msc_sta | STA_PAR;                    /* error */
        break;

    case MTSE_WRP:                                      /* write protect */
        msc_sta = msc_sta | STA_REJ;                    /* reject */
        break;
        }

return SCPE_OK;
}


/* Controller clear */

t_stat ms_clear (void)
{
int32 i;
t_stat st;
UNIT *uptr;

for (i = 0; i < MS_NUMDR; i++) {                        /* look for write in progr */
    uptr = &msc_unit [i];                               /* get pointer to unit */

    if (sim_is_active (uptr) &&                         /* unit active? */
        (uptr->FNC == FNC_WC) &&                        /*   and last cmd write? */
        (ms_ptr > 0)) {                                 /*   and partial buffer? */
        if (DEBUG_PRI (msc_dev, DEB_RWS))
            fprintf (sim_deb,
                ">>MSC rws: Unit %d wrote %d word partial record\n", i, ms_ptr / 2);

        st = sim_tape_wrrecf (uptr, msxb, ms_ptr | MTR_ERF);

        if (st != MTSE_OK)
            ms_map_err (uptr, st);                      /* discard any error */

        ms_ptr = 0;                                     /* clear partial */
        }

    if ((uptr->UST & STA_REW) == 0)
        sim_cancel (uptr);                              /* stop if not rew */
    }

msc_sta = msc_1st = 0;                                  /* clr ctlr status */

return SCPE_OK;
}


/* Reset routine */

t_stat msc_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;
DIB *dibptr = (DIB *) dptr->ctxt;                       /* DIB pointer */

hp_enbdis_pair (dptr,                                   /* make pair cons */
    (dptr == &msd_dev) ? &msc_dev : &msd_dev);

if (sim_switches & SWMASK ('P'))                        /* initialization reset? */
    ms_config_timing ();

IOPRESET (dibptr);                                      /* PRESET device (does not use PON) */

msc_buf = msd_buf = 0;
msc_sta = msc_usl = 0;
msc_1st = 0;

for (i = 0; i < MS_NUMDR; i++) {
    uptr = msc_dev.units + i;
    sim_tape_reset (uptr);
    sim_cancel (uptr);
    uptr->UST = 0;

    if (sim_switches & SWMASK ('P'))                    /* if this is an initialization reset */
        sim_tape_set_dens (uptr,                        /*   then tell the tape library the density in use */
                           ms_ctype ? BPI_13183 : BPI_13181,
                           NULL, NULL);
    }

return SCPE_OK;
}


/* Attach routine */

t_stat msc_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = sim_tape_attach (uptr, cptr);                       /* attach unit */
if (r == SCPE_OK)
    uptr->flags = uptr->flags & ~UNIT_OFFLINE;          /* set online */
return r;
}

/* Detach routine */

t_stat msc_detach (UNIT* uptr)
{
uptr->UST = 0;                                          /* clear status */
uptr->flags = uptr->flags | UNIT_OFFLINE;               /* set offline */
return sim_tape_detach (uptr);                          /* detach unit */
}

/* Online routine */

t_stat msc_online (UNIT *uptr, int32 value, char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT) return SCPE_OK;
else return SCPE_UNATT;
}

/* Configure timing */

void ms_config_timing (void)
{
uint32 i, tset;

tset = (ms_timing << 1) | (ms_timing ? 0 : ms_ctype);   /* select timing set */
for (i = 0; i < (sizeof (timers) / sizeof (timers[0])); i++)
    *timers[i] = msc_times[tset][i];                    /* assign times */
}

/* Set controller timing */

t_stat ms_set_timing (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if ((val < 0) || (val > 1) || (cptr != NULL)) return SCPE_ARG;
ms_timing = val;
ms_config_timing ();
return SCPE_OK;
}

/* Show controller timing */

t_stat ms_show_timing (FILE *st, UNIT *uptr, int32 val, void *desc)
{
if (ms_timing) fputs ("fast timing", st);
else fputs ("realistic timing", st);
return SCPE_OK;
}

/* Set controller type */

t_stat ms_settype (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 i;

if ((val < 0) || (val > 1) || (cptr != NULL)) return SCPE_ARG;
for (i = 0; i < MS_NUMDR; i++) {
    if (msc_unit[i].flags & UNIT_ATT) return SCPE_ALATT;
    }
ms_ctype = (CNTLR_TYPE) val;
ms_config_timing ();                                    /* update for new type */

sim_tape_set_dens (uptr, ms_ctype ? BPI_13183 : BPI_13181,  /* tell the tape library the density in use */
                   NULL, NULL);

return SCPE_OK;
}

/* Show controller type */

t_stat ms_showtype (FILE *st, UNIT *uptr, int32 val, void *desc)
{
if (ms_ctype == A13183)
    fprintf (st, "13183A");
else
    fprintf (st, "13181A");
return SCPE_OK;
}

/* Set unit reel size

   val = 0 -> SET MSCn CAPACITY=n
   val = 1 -> SET MSCn REEL=n */

t_stat ms_set_reelsize (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 reel;
t_stat status;

if (val == 0) {
    status = sim_tape_set_capac (uptr, val, cptr, desc);
    if (status == SCPE_OK) uptr->REEL = 0;
    return status;
    }

if (cptr == NULL) return SCPE_ARG;
reel = (int32) get_uint (cptr, 10, 2400, &status);
if (status != SCPE_OK) return status;
else switch (reel) {

     case 0:
        uptr->REEL = 0;                                 /* type 0 = unlimited/custom */
        break;

    case 600:
        uptr->REEL = 1;                                 /* type 1 = 600 foot */
        break;

    case 1200:
        uptr->REEL = 2;                                 /* type 2 = 1200 foot */
        break;

    case 2400:
        uptr->REEL = 3;                                 /* type 3 = 2400 foot */
        break;

    default:
        return SCPE_ARG;
        }

uptr->capac = uptr->REEL ? (TCAP << uptr->REEL) << ms_ctype : 0;
return SCPE_OK;
}

/* Show unit reel size

   val = 0 -> SHOW MSC or SHOW MSCn or SHOW MSCn CAPACITY
   val = 1 -> SHOW MSCn REEL */

t_stat ms_show_reelsize (FILE *st, UNIT *uptr, int32 val, void *desc)
{
t_stat status = SCPE_OK;

if (uptr->REEL == 0) status = sim_tape_show_capac (st, uptr, val, desc);
else fprintf (st, "%4d foot reel", 300 << uptr->REEL);
if (val == 1) fputc ('\n', st);                         /* MTAB_NMO omits \n */
return status;
}

/* Translate command to mnemonic for debug logging

   The command names and descriptions are taken from the 13181 interface
   manual. */

char *ms_cmd_name (uint32 cmd)
{

switch (cmd & 0377) {
    case FNC_WC:  return "WCC";         /* Write command */
    case FNC_WFM: return "WFM";         /* Write file mark */
    case FNC_RC:  return "RRF";         /* Read record forward */
    case FNC_FSR: return "FSR";         /* Forward space record */
    case FNC_FSF: return "FSF";         /* Forward space file */
    case FNC_GAP: return "GAP";         /* Write gap */
    case FNC_BSR: return "BSR";         /* Backspace record */
    case FNC_BSF: return "BSF";         /* Backspace file */
    case FNC_REW: return "REW";         /* Rewind */
    case FNC_RWS: return "RWO";         /* Rewind off-line */
    case FNC_CLR: return "CLR";         /* Clear controller */
    case FNC_GFM: return "GFM";         /* Gap file mark */
    case FNC_RFF: return "RFF";         /* Read forward until file mark (diag) */
    case FNC_RRR: return "RRR";         /* Read record in reverse (diag) */

    default:      return "???";         /* Unknown command */
    }
}

/* 7970B/7970E bootstrap routine (HP 12992D ROM) */

const BOOT_ROM ms_rom = {
    0106501,                    /*ST LIB 1              ; read sw */
    0006011,                    /*   SLB,RSS            ; bit 0 set? */
    0027714,                    /*   JMP RD             ; no read */
    0003004,                    /*   CMA,INA            ; A is ctr */
    0073775,                    /*   STA WC             ; save */
    0067772,                    /*   LDA SL0RW          ; sel 0, rew */
    0017762,                    /*FF JSB CMD            ; do cmd */
    0102311,                    /*   SFS CC             ; done? */
    0027707,                    /*   JMP *-1            ; wait */
    0067774,                    /*   LDB FFC            ; get file fwd */
    0037775,                    /*   ISZ WC             ; done files? */
    0027706,                    /*   JMP FF             ; no */
    0067773,                    /*RD LDB RDCMD          ; read cmd */
    0017762,                    /*   JSB CMD            ; do cmd */
    0103710,                    /*   STC DC,C           ; start dch */
    0102211,                    /*   SFC CC             ; read done? */
    0027752,                    /*   JMP STAT           ; no, get stat */
    0102310,                    /*   SFS DC             ; any data? */
    0027717,                    /*   JMP *-3            ; wait */
    0107510,                    /*   LIB DC,C           ; get rec cnt */
    0005727,                    /*   BLF,BLF            ; move to lower */
    0007000,                    /*   CMB                ; make neg */
    0077775,                    /*   STA WC             ; save */
    0102211,                    /*   SFC CC             ; read done? */
    0027752,                    /*   JMP STAT           ; no, get stat */
    0102310,                    /*   SFS DC             ; any data? */
    0027727,                    /*   JMP *-3            ; wait */
    0107510,                    /*   LIB DC,C           ; get load addr */
    0074000,                    /*   STB 0              ; start csum */
    0077762,                    /*   STA CMD            ; save address */
    0027742,                    /*   JMP *+4 */
    0177762,                    /*NW STB CMD,I          ; store data */
    0040001,                    /*   ADA 1              ; add to csum */
    0037762,                    /*   ISZ CMD            ; adv addr ptr */
    0102310,                    /*   SFS DC             ; any data? */
    0027742,                    /*   JMP *-1            ; wait */
    0107510,                    /*   LIB DC,C           ; get word */
    0037775,                    /*   ISZ WC             ; done? */
    0027737,                    /*   JMP NW             ; no */
    0054000,                    /*   CPB 0              ; csum ok? */
    0027717,                    /*   JMP RD+3           ; yes, cont */
    0102011,                    /*   HLT 11             ; no, halt */
    0102511,                    /*ST LIA CC             ; get status */
    0001727,                    /*   ALF,ALF            ; get eof bit */
    0002020,                    /*   SSA                ; set? */
    0102077,                    /*   HLT 77             ; done */
    0001727,                    /*   ALF,ALF            ; put status back */
    0001310,                    /*   RAR,SLA            ; read ok? */
    0102000,                    /*   HLT 0              ; no */
    0027714,                    /*   JMP RD             ; read next */
    0000000,                    /*CMD 0 */
    0106611,                    /*   OTB CC             ; output cmd */
    0102511,                    /*   LIA CC             ; check for reject */
    0001323,                    /*   RAR,RAR */
    0001310,                    /*   RAR,SLA */
    0027763,                    /*   JMP CMD+1          ; try again */
    0103711,                    /*   STC CC,C           ; start command */
    0127762,                    /*   JMP CMD,I          ; exit */
    0001501,                    /*SL0RW 001501          ; select 0, rewind */
    0001423,                    /*RDCMD 001423          ; read record */
    0000203,                    /*FFC   000203          ; space forward file */
    0000000,                    /*WC    000000 */
    0000000,
    0000000
    };

t_stat msc_boot (int32 unitno, DEVICE *dptr)
{
int32 dev;

if (unitno != 0) return SCPE_NOFNC;                     /* only unit 0 */
dev = msd_dib.select_code;                              /* get data chan dev */
if (ibl_copy (ms_rom, dev)) return SCPE_IERR;           /* copy boot to memory */
SR = (SR & IBL_OPT) | IBL_MS | (dev << IBL_V_DEV);      /* set SR */
if ((sim_switches & SWMASK ('S')) && AR) SR = SR | 1;   /* skip? */
return SCPE_OK;
}

/* Calculate tape record CRC and LRC characters */

#define E               0400                            /* parity bit for odd parity */
#define O               0000                            /* parity bit for odd parity */

static const uint16 odd_parity [256] = {                /* parity table */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E,     /* 000-017 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /* 020-037 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /* 040-067 */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E,     /* 060-077 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /* 100-117 */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E,     /* 120-137 */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E,     /* 140-157 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /* 160-177 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /* 200-217 */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E,     /* 220-237 */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E,     /* 240-267 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /* 260-277 */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E,     /* 300-317 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /* 320-337 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /* 340-357 */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E      /* 360-377 */
    };

static uint32 calc_crc_lrc (uint8 *buffer, t_mtrlnt length)
{
uint32 i;
uint16 byte, crc, lrc;

lrc = crc = 0;

for (i = 0; i < length; i++) {
    byte = odd_parity [buffer [i]] | buffer [i];

    crc = crc ^ byte;
    lrc = lrc ^ byte;

    if (crc & 1)
        crc = crc >> 1 ^ 0474;
    else
        crc = crc >> 1;
    }

crc = crc ^ 0727;
lrc = lrc ^ crc;

return (uint32) crc << 16 | lrc;
}
