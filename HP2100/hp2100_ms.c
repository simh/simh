/* hp2100_ms.c: HP 2100 13181B/13183B Digital Magnetic Tape Unit Interface simulator

   Copyright (c) 1993-2016, Robert M. Supnik
   Copyright (c) 2017-2018, J. David Bryan

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
   AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   MS           13181B Digital Magnetic Tape Unit Interface
                13183B Digital Magnetic Tape Unit Interface

   28-Feb-18    JDB     Added the BMTL
   23-Feb-18    JDB     Eliminated "msc_boot" references to A and S registers
   20-Jul-17    JDB     Removed "msc_stopioe" variable and register
   11-Jul-17    JDB     Renamed "ibl_copy" to "cpu_ibl"
   15-Mar-17    JDB     Trace flags are now global
                        Changed DEBUG_PRI calls to tprintfs
   13-Mar-17    JDB     Deprecated LOCKED/WRITEENABLED for ATTACH -R
   10-Mar-17    JDB     Added IOBUS to the debug table
   27-Feb-17    JDB     ibl_copy no longer returns a status code
   17-Jan-17    JDB     Modified to use "odd_parity" array in hp2100_sys.c
   13-May-16    JDB     Modified for revised SCP API function parameter types
   30-Dec-14    JDB     Added S-register parameters to ibl_copy
   24-Dec-14    JDB     Use T_ADDR_FMT with t_addr values for 64-bit compatibility
                        Added casts for explicit downward conversions
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
         (13181-90901, November 1982)
     - 13183B Digital Magnetic Tape Unit Interface Kit Operating and Service Manual
         (13183-90901, November 1983)
     - SIMH Magtape Representation and Handling
         (Bob Supnik, 30-Aug-2006)
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

int32 *const timers [] = { &msc_btime, &msc_ctime, &msc_gtime,
                           &msc_itime, &msc_rtime, &msc_xtime };

const TIMESET msc_times [3] = {
    { 161512, 14044, 175553, 24885, 878,  88 },         /* 13181B */
    { 252800, 17556, 105333, 27387, 878,  44 },         /* 13183B */
    {      1,  1000,      1,     1, 100,  10 }          /* FAST */
    };

DEVICE msd_dev, msc_dev;

IOHANDLER msdio;
IOHANDLER mscio;

t_stat msc_svc (UNIT *uptr);
t_stat msc_reset (DEVICE *dptr);
t_stat msc_attach (UNIT *uptr, CONST char *cptr);
t_stat msc_detach (UNIT *uptr);
t_stat msc_online (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
t_stat msc_boot (int32 unitno, DEVICE *dptr);
t_stat ms_write_gap (UNIT *uptr);
t_stat ms_map_err (UNIT *uptr, t_stat st);
t_stat ms_settype (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ms_showtype (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat ms_set_timing (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ms_show_timing (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat ms_set_reelsize (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ms_show_reelsize (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
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

MTAB msd_mod [] = {
/*    Entry Flags          Value  Print String  Match String  Validation    Display        Descriptor       */
/*    -------------------  -----  ------------  ------------  ------------  -------------  ---------------- */
    { MTAB_XDV,              2u,  "SC",         "SC",         &hp_set_dib,  &hp_show_dib,  (void *) &ms_dib },
    { MTAB_XDV | MTAB_NMO,  ~2u,  "DEVNO",      "DEVNO",      &hp_set_dib,  &hp_show_dib,  (void *) &ms_dib },
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
    { FLDATA (CTYPE, ms_ctype, 0), REG_HRO },
    { ORDATA (SC, msc_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, msc_dib.select_code, 6), REG_HRO },
    { NULL }
    };

/* Modifier list.

   The LOCKED and WRITEENABLED modifiers are deprecated.  The supported method
   of write-protecting a tape drive is to attach the tape image with the -R
   (read-only) switch or by setting the host operating system's read-only
   attribute on the tape image file.  This simulates removing the write ring
   from the tape reel before mounting it on the drive.  There is no hardware
   method of write-protecting a mounted and positioned tape reel.


   Implementation notes:

    1. The UNIT_RO modifier displays "write ring" if the flag is not set.  There
       is no corresponding entry for the opposite condition because "read only"
       is automatically printed after the attached filename.
*/

MTAB msc_mod [] = {
/*    Mask Value     Match Value    Print String      Match String     Validation    Display  Descriptor */
/*    -------------  -------------  ----------------  ---------------  ------------  -------  ---------- */
    { UNIT_RO,       0,             "write ring",     NULL,            NULL,         NULL,    NULL       },

    { UNIT_OFFLINE,  UNIT_OFFLINE,  "offline",        "OFFLINE",       NULL,         NULL,    NULL       },
    { UNIT_OFFLINE,  0,             "online",         "ONLINE",        &msc_online,  NULL,    NULL       },

    { MTUF_WLK,      0,             NULL,             "WRITEENABLED",  NULL,         NULL,    NULL       },
    { MTUF_WLK,      MTUF_WLK,      NULL,             "LOCKED",        NULL,         NULL,    NULL       },


/*    Entry Flags          Value  Print String  Match String  Validation         Display             Descriptor       */
/*    -------------------  -----  ------------  ------------  -----------------  ------------------  ---------------- */
    { MTAB_XUN,              0,   "CAPACITY",   "CAPACITY",   &ms_set_reelsize,  &ms_show_reelsize,  NULL             },
    { MTAB_XUN | MTAB_NMO,   1,   "REEL",       "REEL",       &ms_set_reelsize,  &ms_show_reelsize,  NULL             },
    { MTAB_XUN,              0,   "FORMAT",     "FORMAT",     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL             },

    { MTAB_XDV,              0,   NULL,         "13181A/B",   &ms_settype,       NULL,               NULL             },
    { MTAB_XDV,              1,   NULL,         "13183A/B",   &ms_settype,       NULL,               NULL             },
    { MTAB_XDV,              0,   "TYPE",       NULL,         NULL,              &ms_showtype,       NULL             },

    { MTAB_XDV,              0,   NULL,         "REALTIME",   &ms_set_timing,    NULL,               NULL             },
    { MTAB_XDV,              1,   NULL,         "FASTTIME",   &ms_set_timing,    NULL,               NULL             },
    { MTAB_XDV,              0,   "TIMING",     NULL,         NULL,              &ms_show_timing,    NULL             },

    { MTAB_XDV,              2u,  "SC",         "SC",         &hp_set_dib,       &hp_show_dib,       (void *) &ms_dib },
    { MTAB_XDV | MTAB_NMO,  ~2u,  "DEVNO",      "DEVNO",      &hp_set_dib,       &hp_show_dib,       (void *) &ms_dib },

    { 0 }
    };

DEBTAB msc_deb[] = {
    { "CMDS",  DEB_CMDS    },
    { "RWS",   DEB_RWS     },
    { "CPU",   DEB_CPU     },
    { "IOBUS", TRACE_IOBUS },                   /* interface I/O bus signals and data words */
    { NULL,    0           }
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
            data = (uint16) (msc_sta & ~STA_DYN);       /* get card status */

            if ((uptr->flags & UNIT_OFFLINE) == 0) {    /* online? */
                data = data | (uint16) uptr->UST;       /* add unit status */

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

            if (ms_ctype == A13183)                     /* 13183? */
                data = data | STA_PE | (uint16) (msc_usl << STA_V_SEL);

            tprintf (msc_dev, DEB_CPU, "Status = %06o\n", data);

            stat_data = IORETURN (SCPE_OK, data);       /* merge in return status */
            break;


        case ioIOO:                                         /* I/O data output */
            msc_buf = IODATA (stat_data);                   /* clear supplied status */

            tprintf (msc_dev, DEB_CPU, "Command = %06o\n", msc_buf);

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
                tprintf (msc_dev, DEB_CMDS, "Unit %d selected\n", msc_usl);
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

                    tprintf (msc_dev, DEB_CMDS, "Controller cleared\n");

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

                    tprintf (msc_dev, DEB_CMDS, "Unit %d command %03o (%s) scheduled, "
                                                "pos = %" T_ADDR_FMT "d, time = %d\n",
                             msc_usl, uptr->FNC, ms_cmd_name (uptr->FNC),
                             uptr->pos, sched_time);
                    }

                else
                    tprintf (msc_dev, DEB_CMDS, "Unit select (NOP)\n");

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
    return SCPE_OK;
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
        tprintf (msc_dev, DEB_RWS, "Unit %d wrote gap\n", unum);

        r = ms_write_gap (uptr);                        /* write tape gap*/

        if (r || (uptr->FNC != FNC_GFM))                /* if error or not GFM */
            break;                                      /*   then bail out now */
                                                        /* else drop into WFM */
    case FNC_WFM:                                       /* write file mark */
        if ((ms_timing == 0) && sim_tape_bot (uptr)) {  /* realistic timing + BOT? */
            tprintf (msc_dev, DEB_RWS, "Unit %d wrote initial gap\n", unum);

            st = ms_write_gap (uptr);                   /* write initial gap*/
            if (st != MTSE_OK) {                        /* error? */
                r = ms_map_err (uptr, st);              /* map error */
                break;                                  /* terminate operation */
                }
            }
    DO_WFM:
        tprintf (msc_dev, DEB_RWS, "Unit %d wrote file mark\n", unum);

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
            tprintf (msc_dev, DEB_RWS, "Unit %d read %d word record\n",
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
                msc_sta = msc_sta | STA_ODD;            /* set ODD for 13183 */
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
                tprintf (msc_dev, DEB_RWS, "Unit %d wrote initial gap\n", unum);

                st = ms_write_gap (uptr);               /* write initial gap */
                if (st != MTSE_OK) {                    /* error? */
                    r = ms_map_err (uptr, st);          /* map error */
                    break;                              /* terminate operation */
                    }
                }
            }
        else {                                          /* not 1st, next char */
            if (ms_ptr < DBSIZE) {                      /* room in buffer? */
                msxb[ms_ptr] = (uint8) (msd_buf >> 8);  /* store 2 char */
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
            tprintf (msc_dev, DEB_RWS, "Unit %d wrote %d word record\n",
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
        tprintf (msc_dev, DEB_CMDS, "Unit %d command %03o is unknown (NOP)\n",
                 unum, uptr->FNC);
        break;
        }

mscio (&msc_dib, ioENF, 0);                             /* set flag */
msc_sta = msc_sta & ~STA_BUSY;                          /* update status */

tprintf (msc_dev, DEB_CMDS, "Unit %d command %03o (%s) complete\n",
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

tprintf (msc_dev, DEB_RWS, "Unit %d tape library status = %d\n", unum, st);

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
            msc_sta = msc_sta | STA_ODD;                /* EOF also sets ODD for 13181B */
        break;

    case MTSE_INVRL:                                    /* invalid rec lnt */
        msc_sta = msc_sta | STA_PAR;
        return SCPE_MTRLNT;

    case MTSE_IOERR:                                    /* IO error */
        msc_sta = msc_sta | STA_PAR;                    /* error */
        return SCPE_IOERR;

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
        tprintf (msc_dev, DEB_RWS, "Unit %d wrote %d word partial record\n", i, ms_ptr / 2);

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

t_stat msc_attach (UNIT *uptr, CONST char *cptr)
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

t_stat msc_online (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
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

t_stat ms_set_timing (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if ((val < 0) || (val > 1) || (cptr != NULL)) return SCPE_ARG;
ms_timing = val;
ms_config_timing ();
return SCPE_OK;
}

/* Show controller timing */

t_stat ms_show_timing (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (ms_timing) fputs ("fast timing", st);
else fputs ("realistic timing", st);
return SCPE_OK;
}

/* Set controller type */

t_stat ms_settype (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
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

t_stat ms_showtype (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (ms_ctype == A13183)
    fprintf (st, "13183B");
else
    fprintf (st, "13181B");
return SCPE_OK;
}

/* Set unit reel size

   val = 0 -> SET MSCn CAPACITY=n
   val = 1 -> SET MSCn REEL=n */

t_stat ms_set_reelsize (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
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

t_stat ms_show_reelsize (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
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


/* 7970B/7970E bootstrap loaders (BMTL and 12992D).

   The Basic Magnetic Tape Loader (BMTL) reads an absolute binary program from
   tape into memory.  Before execution, the S register must be set as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   - |      file number      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   If S-register bits 5-0 are zero, the file located at the current tape
   position is read.  If the bits are non-zero, the tape is rewound, and the
   file number (1 - n) specified by the bits is read.

   The 12992D boot loader ROM reads an absolute program from tape into memory.
   If S-register bit 0 is 0, the file located at the current tape position is
   read.  If bit 0 is 1, the tape is rewound, and the file number (1 - n)
   specified by the A-register value is read.

   For either loader, the tape format must be absolute binary, and a tape mark
   must end the file.  Loader execution ends with one of the following
   instructions:

     * HLT 00 - a tape read (parity) error occurred.
     * HLT 11 - a checksum error occurred; A/B = the calculated/tape value.
     * HLT 77 - the end of the file was reached with a successful read.
*/

static const LOADER_ARRAY ms_loaders = {
    {                               /* HP 21xx Basic Magnetic Tape Loader (BMTL) */
      000,                          /*   loader starting index */
      IBL_NA,                       /*   DMA index */
      IBL_NA,                       /*   FWA index */
      { 0102501,                    /*   77700:  MTAPE LIA 1               */
        0013775,                    /*   77701:        AND 77775           */
        0003007,                    /*   77702:        CMA,INA,SZA,RSS     */
        0027714,                    /*   77703:        JMP 77714           */
        0073777,                    /*   77704:        STA 77777           */
        0067771,                    /*   77705:        LDB 77771           */
        0017761,                    /*   77706:        JSB 77761           */
        0102311,                    /*   77707:        SFS 11              */
        0027707,                    /*   77710:        JMP 77707           */
        0067773,                    /*   77711:        LDB 77773           */
        0037777,                    /*   77712:        ISZ 77777           */
        0027706,                    /*   77713:        JMP 77706           */
        0067772,                    /*   77714:        LDB 77772           */
        0017761,                    /*   77715:        JSB 77761           */
        0103710,                    /*   77716:        STC 10,C            */
        0017740,                    /*   77717:        JSB 77740           */
        0005727,                    /*   77720:        BLF,BLF             */
        0007004,                    /*   77721:        CMB,INB             */
        0077777,                    /*   77722:        STB 77777           */
        0017740,                    /*   77723:        JSB 77740           */
        0074000,                    /*   77724:        STB 0               */
        0077776,                    /*   77725:        STB 77776           */
        0017740,                    /*   77726:        JSB 77740           */
        0177776,                    /*   77727:        STB 77776,I         */
        0040001,                    /*   77730:        ADA 1               */
        0037776,                    /*   77731:        ISZ 77776           */
        0037777,                    /*   77732:        ISZ 77777           */
        0027726,                    /*   77733:        JMP 77726           */
        0017740,                    /*   77734:        JSB 77740           */
        0054000,                    /*   77735:        CPB 0               */
        0017740,                    /*   77736:        JSB 77740           */
        0102011,                    /*   77737:        HLT 11              */
        0000000,                    /*   77740:        NOP                 */
        0102310,                    /*   77741:        SFS 10              */
        0027745,                    /*   77742:        JMP 77745           */
        0107510,                    /*   77743:        LIB 10,C            */
        0127740,                    /*   77744:        JMP 77740,I         */
        0102311,                    /*   77745:        SFS 11              */
        0027741,                    /*   77746:        JMP 77741           */
        0102511,                    /*   77747:        LIA 11              */
        0013774,                    /*   77750:        AND 77774           */
        0067777,                    /*   77751:        LDB 77777           */
        0001727,                    /*   77752:        ALF,ALF             */
        0002020,                    /*   77753:        SSA                 */
        0102077,                    /*   77754:        HLT 77              */
        0002003,                    /*   77755:        SZA,RSS             */
        0006002,                    /*   77756:        SZB                 */
        0102000,                    /*   77757:        HLT 0               */
        0027714,                    /*   77760:        JMP 77714           */
        0000000,                    /*   77761:        NOP                 */
        0106611,                    /*   77762:        OTB 11              */
        0102511,                    /*   77763:        LIA 11              */
        0001323,                    /*   77764:        RAR,RAR             */
        0001310,                    /*   77765:        RAR,SLA             */
        0027762,                    /*   77766:        JMP 77762           */
        0103711,                    /*   77767:        STC 11,C            */
        0127761,                    /*   77770:        JMP 77761,I         */
        0001501,                    /*   77771:        OCT 1501            */
        0001423,                    /*   77772:        OCT 1423            */
        0000203,                    /*   77773:        OCT 203             */
        0016263,                    /*   77774:        OCT 16263           */
        0000077,                    /*   77775:        OCT 77              */
        0000000,                    /*   77776:        NOP                 */
        0000000 } },                /*   77777:        NOP                 */

    {                               /* HP 1000 Loader ROM (12992D) */
      IBL_START,                    /*   loader starting index */
      IBL_DMA,                      /*   DMA index */
      IBL_FWA,                      /*   FWA index */
      { 0106501,                    /*   77700:  ST    LIB 1              ; read sw */
        0006011,                    /*   77701:        SLB,RSS            ; bit 0 set? */
        0027714,                    /*   77702:        JMP RD             ; no read */
        0003004,                    /*   77703:        CMA,INA            ; A is ctr */
        0073775,                    /*   77704:        STA WC             ; save */
        0067772,                    /*   77705:        LDA SL0RW          ; sel 0, rew */
        0017762,                    /*   77706:  FF    JSB CMD            ; do cmd */
        0102311,                    /*   77707:        SFS CC             ; done? */
        0027707,                    /*   77710:        JMP *-1            ; wait */
        0067774,                    /*   77711:        LDB FFC            ; get file fwd */
        0037775,                    /*   77712:        ISZ WC             ; done files? */
        0027706,                    /*   77713:        JMP FF             ; no */
        0067773,                    /*   77714:  RD    LDB RDCMD          ; read cmd */
        0017762,                    /*   77715:        JSB CMD            ; do cmd */
        0103710,                    /*   77716:        STC DC,C           ; start dch */
        0102211,                    /*   77717:        SFC CC             ; read done? */
        0027752,                    /*   77720:        JMP STAT           ; no, get stat */
        0102310,                    /*   77721:        SFS DC             ; any data? */
        0027717,                    /*   77722:        JMP *-3            ; wait */
        0107510,                    /*   77723:        LIB DC,C           ; get rec cnt */
        0005727,                    /*   77724:        BLF,BLF            ; move to lower */
        0007000,                    /*   77725:        CMB                ; make neg */
        0077775,                    /*   77726:        STA WC             ; save */
        0102211,                    /*   77727:        SFC CC             ; read done? */
        0027752,                    /*   77730:        JMP STAT           ; no, get stat */
        0102310,                    /*   77731:        SFS DC             ; any data? */
        0027727,                    /*   77732:        JMP *-3            ; wait */
        0107510,                    /*   77733:        LIB DC,C           ; get load addr */
        0074000,                    /*   77734:        STB 0              ; start csum */
        0077762,                    /*   77735:        STA CMD            ; save address */
        0027742,                    /*   77736:        JMP *+4            */
        0177762,                    /*   77737:  NW    STB CMD,I          ; store data */
        0040001,                    /*   77740:        ADA 1              ; add to csum */
        0037762,                    /*   77741:        ISZ CMD            ; adv addr ptr */
        0102310,                    /*   77742:        SFS DC             ; any data? */
        0027742,                    /*   77743:        JMP *-1            ; wait */
        0107510,                    /*   77744:        LIB DC,C           ; get word */
        0037775,                    /*   77745:        ISZ WC             ; done? */
        0027737,                    /*   77746:        JMP NW             ; no */
        0054000,                    /*   77747:        CPB 0              ; csum ok? */
        0027717,                    /*   77750:        JMP RD+3           ; yes, cont */
        0102011,                    /*   77751:        HLT 11             ; no, halt */
        0102511,                    /*   77752:  ST    LIA CC             ; get status */
        0001727,                    /*   77753:        ALF,ALF            ; get eof bit */
        0002020,                    /*   77754:        SSA                ; set? */
        0102077,                    /*   77755:        HLT 77             ; done */
        0001727,                    /*   77756:        ALF,ALF            ; put status back */
        0001310,                    /*   77757:        RAR,SLA            ; read ok? */
        0102000,                    /*   77760:        HLT 0              ; no */
        0027714,                    /*   77761:        JMP RD             ; read next */
        0000000,                    /*   77762:  CMD   NOP                */
        0106611,                    /*   77763:        OTB CC             ; output cmd */
        0102511,                    /*   77764:        LIA CC             ; check for reject */
        0001323,                    /*   77765:        RAR,RAR            */
        0001310,                    /*   77766:        RAR,SLA            */
        0027763,                    /*   77767:        JMP CMD+1          ; try again */
        0103711,                    /*   77770:        STC CC,C           ; start command */
        0127762,                    /*   77771:        JMP CMD,I          ; exit */
        0001501,                    /*   77772:  SL0RW OCT 1501           ; select 0, rewind */
        0001423,                    /*   77773:  RDCMD OCT 1423           ; read record */
        0000203,                    /*   77774:  FFC   OCT 203            ; space forward file */
        0000000,                    /*   77775:  WC    NOP                */
        0000000,                    /*   77776:        NOP                */
        0000000 } }                 /*   77777:        NOP                */
    };


/* Device boot routine.

   This routine is called directly by the BOOT MSC and LOAD MSC commands to copy
   the device bootstrap into the upper 64 words of the logical address space.
   It is also called indirectly by a BOOT CPU or LOAD CPU command when the
   specified HP 1000 loader ROM socket contains a 12992D ROM.

   When called in response to a BOOT MSC or LOAD MSC command, the "unitno"
   parameter indicates the unit number specified in the BOOT command or is zero
   for the LOAD command, and "dptr" points at the MSC device structure.  The
   bootstrap supports loading only from unit 0, and the command will be rejected
   if another unit is specified (e.g., BOOT MSC1).  Otherwise, depending on the
   current CPU model, the BMTL or 12992D loader ROM will be copied into memory
   and configured for the MSD/MSC select code pair.  If the CPU is a 1000, the S
   register will be set as it would be by the front-panel microcode.

   When called for a BOOT/LOAD CPU command, the "unitno" parameter indicates the
   select code to be used for configuration, and "dptr" will be NULL.  As above,
   the BMTL or 12992D loader ROM will be copied into memory and configured for
   the specified select code. The S register is assumed to be set correctly on
   entry and is not modified.

   For the 12992D boot loader ROM for the HP 1000, the S register is set as
   follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | ROM # | 0   0 |      select code      | 0   0   0   0   0 | F |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     F = Read current/specified file (0/1)

   If bit 0 is 0, the file located at the current tape position is read.  If bit
   0 is 1, the tape is rewound, and the file number (1 - n) specified by the
   A-register content is read.
*/

t_stat msc_boot (int32 unitno, DEVICE *dptr)
{
static const HP_WORD ms_preserved  = 0000000u;              /* no S-register bits are preserved */
static const HP_WORD ms_reposition = 0000001u;              /* S-register bit 0 set for a repositioning boot */

if (dptr == NULL)                                           /* if we are being called for a BOOT/LOAD CPU */
    return cpu_copy_loader (ms_loaders, unitno,             /*   then copy the boot loader to memory */
                            IBL_S_NOCLEAR, IBL_S_NOSET);    /*     but do not alter the S register */

else if (unitno != 0)                                       /* otherwise a BOOT MSC for a non-zero unit */
    return SCPE_NOFNC;                                      /*   is rejected as unsupported */

else                                                            /* otherwise this is a BOOT/LOAD MSC */
    return cpu_copy_loader (ms_loaders, msd_dib.select_code,    /*   so copy the boot loader to memory */
                            ms_preserved,                       /*     and configure the S register if 1000 CPU */
                            sim_switches & SWMASK ('S') ? ms_reposition : 0);
}


/* Calculate tape record CRC and LRC characters */

static uint32 calc_crc_lrc (uint8 *buffer, t_mtrlnt length)
{
uint32 i;
HP_WORD byte, crc, lrc;

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
