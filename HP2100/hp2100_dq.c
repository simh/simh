/* hp2100_dq.c: HP 2100 12565A disk simulator

   Copyright (c) 1993-2006, Bill McDermith
   Copyright (c) 2004-2008 J. David Bryan

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

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   DQ           12565A 2883 disk system

   10-Aug-08    JDB     Added REG_FIT to register variables < 32-bit size
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   01-Mar-05    JDB     Added SET UNLOAD/LOAD
   07-Oct-04    JDB     Fixed enable/disable from either device
                        Shortened xtime from 5 to 3 (drive avg 156KW/second)
                        Fixed not ready/any error status
                        Fixed RAR model
   21-Apr-04    RMS     Fixed typo in boot loader (found by Dave Bryan)
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Fixed SR setting in IBL
                        Revised IBL loader
                        Implemented DMA SRQ (follows FLG)
   25-Apr-03    RMS     Fixed bug in status check
   10-Nov-02    RMS     Added boot command, rebuilt like 12559/13210
   09-Jan-02    WOM     Copied dp driver and mods for 2883

   Reference:
   - 12565A Disc Interface Kit Operating and Service Manual (12565-90003, Aug-1973)


   Differences between 12559/13210 and 12565 controllers
   - 12565 stops transfers on address miscompares; 12559/13210 only stops writes
   - 12565 does not set error on positioner busy
   - 12565 does not set positioner busy if already on cylinder
   - 12565 does not need eoc logic, it will hit an invalid head number

   The controller's "Record Address Register" (RAR) contains the CHS address of
   the last Position or Load Address command executed.  The RAR is shared among
   all drives on the controller.  In addition, each drive has an internal
   position register that contains the last cylinder and head position
   transferred to the drive during Position command execution (sector operations
   always start with the RAR sector position).

   In a real drive, the address field of the sector under the head is read and
   compared to the RAR.  When they match, the target sector is under the head
   and is ready for reading or writing.  If a match doesn't occur, an Address
   Error is indicated.  In the simulator, the address field is obtained from the
   drive's current position register during a read, i.e., the "on-disc" address
   field is assumed to match the current position.

   The following implemented behaviors have been inferred from secondary sources
   (diagnostics, operating system drivers, etc.), due to absent or contradictory
   authoritative information; future correction may be needed:

     1. Read Address command starts at the sector number in the RAR.
*/

#include "hp2100_defs.h"

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_UNLOAD   (UNIT_V_UF + 1)                 /* heads unloaded */
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_UNLOAD     (1 << UNIT_V_UNLOAD)
#define FNC             u3                              /* saved function */
#define DRV             u4                              /* drive number (DC) */
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write prot */

#define DQ_N_NUMWD      7
#define DQ_NUMWD        (1 << DQ_N_NUMWD)               /* words/sector */
#define DQ_NUMSC        23                              /* sectors/track */
#define DQ_NUMSF        20                              /* tracks/cylinder */
#define DQ_NUMCY        203                             /* cylinders/disk */
#define DQ_SIZE         (DQ_NUMSF * DQ_NUMCY * DQ_NUMSC * DQ_NUMWD)
#define DQ_NUMDRV       2                               /* # drives */

/* Command word */

#define CW_V_FNC        12                              /* function */
#define CW_M_FNC        017
#define CW_GETFNC(x)    (((x) >> CW_V_FNC) & CW_M_FNC)
/*                      000                             /* unused */
#define  FNC_STA        001                             /* status check */
#define  FNC_RCL        002                             /* recalibrate */
#define  FNC_SEEK       003                             /* seek */
#define  FNC_RD         004                             /* read */
#define  FNC_WD         005                             /* write */
#define  FNC_RA         006                             /* read address */
#define  FNC_WA         007                             /* write address */
#define  FNC_CHK        010                             /* check */
#define  FNC_LA         013                             /* load address */
#define  FNC_AS         014                             /* address skip */

#define  FNC_SEEK1      020                             /* fake - seek1 */
#define  FNC_SEEK2      021                             /* fake - seek2 */
#define  FNC_SEEK3      022                             /* fake - seek3 */
#define  FNC_CHK1       023                             /* fake - check1 */
#define  FNC_LA1        024                             /* fake - ldaddr1 */

#define CW_V_DRV        0                               /* drive */
#define CW_M_DRV        01
#define CW_GETDRV(x)    (((x) >> CW_V_DRV) & CW_M_DRV)

/* Disk address words */

#define DA_V_CYL        0                               /* cylinder */
#define DA_M_CYL        0377
#define DA_GETCYL(x)    (((x) >> DA_V_CYL) & DA_M_CYL)
#define DA_V_HD         8                               /* head */
#define DA_M_HD         037
#define DA_GETHD(x)     (((x) >> DA_V_HD) & DA_M_HD)
#define DA_V_SC         0                               /* sector */
#define DA_M_SC         037
#define DA_GETSC(x)     (((x) >> DA_V_SC) & DA_M_SC)
#define DA_CKMASK       0777                            /* check mask */

/* Status in dqc_sta[drv] - (d) = dynamic */

#define STA_DID         0000200                         /* drive ID (d) */
#define STA_NRDY        0000100                         /* not ready (d) */
#define STA_EOC         0000040                         /* end of cylinder */
#define STA_AER         0000020                         /* addr error */
#define STA_FLG         0000010                         /* flagged */
#define STA_BSY         0000004                         /* seeking */
#define STA_DTE         0000002                         /* data error */
#define STA_ERR         0000001                         /* any error */
#define STA_ANYERR      (STA_NRDY | STA_EOC | STA_AER | STA_FLG | STA_DTE)

FLIP_FLOP dqc_command = CLEAR;                          /* cch command flip-flop */
FLIP_FLOP dqc_control = CLEAR;                          /* cch control flip-flop */
FLIP_FLOP dqc_flag = CLEAR;                             /* cch flag flip-flop */
FLIP_FLOP dqc_flagbuf = CLEAR;                          /* cch flag buffer flip-flop */

int32 dqc_busy = 0;                                     /* cch xfer */
int32 dqc_cnt = 0;                                      /* check count */
int32 dqc_stime = 100;                                  /* seek time */
int32 dqc_ctime = 100;                                  /* command time */
int32 dqc_xtime = 3;                                    /* xfer time */
int32 dqc_dtime = 2;                                    /* dch time */

FLIP_FLOP dqd_command = CLEAR;                          /* dch command flip-flop */
FLIP_FLOP dqd_control = CLEAR;                          /* dch control flip-flop */
FLIP_FLOP dqd_flag = CLEAR;                             /* dch flag flip-flop */
FLIP_FLOP dqd_flagbuf = CLEAR;                          /* dch flag buffer flip-flop */

int32 dqd_obuf = 0, dqd_ibuf = 0;                       /* dch buffers */
int32 dqc_obuf = 0;                                     /* cch buffers */
int32 dqd_xfer = 0;                                     /* xfer in prog */
int32 dqd_wval = 0;                                     /* write data valid */
int32 dq_ptr = 0;                                       /* buffer ptr */
uint8 dqc_rarc = 0;                                     /* RAR cylinder */
uint8 dqc_rarh = 0;                                     /* RAR head */
uint8 dqc_rars = 0;                                     /* RAR sector */
uint8 dqc_ucyl[DQ_NUMDRV] = { 0 };                      /* unit cylinder */
uint8 dqc_uhed[DQ_NUMDRV] = { 0 };                      /* unit head */
uint16 dqc_sta[DQ_NUMDRV] = { 0 };                      /* unit status */
uint16 dqxb[DQ_NUMWD];                                  /* sector buffer */

DEVICE dqd_dev, dqc_dev;
uint32 dqdio (uint32 select_code, IOSIG signal, uint32 data);
uint32 dqcio (uint32 select_code, IOSIG signal, uint32 data);
t_stat dqc_svc (UNIT *uptr);
t_stat dqd_svc (UNIT *uptr);
t_stat dqc_reset (DEVICE *dptr);
t_stat dqc_attach (UNIT *uptr, char *cptr);
t_stat dqc_detach (UNIT* uptr);
t_stat dqc_load_unload (UNIT *uptr, int32 value, char *cptr, void *desc);
t_stat dqc_boot (int32 unitno, DEVICE *dptr);
void dq_god (int32 fnc, int32 drv, int32 time);
void dq_goc (int32 fnc, int32 drv, int32 time);

/* DQD data structures

   dqd_dev      DQD device descriptor
   dqd_unit     DQD unit list
   dqd_reg      DQD register list
*/

DIB dq_dib[] = {
    { DQD, &dqdio },
    { DQC, &dqcio }
    };

#define dqd_dib dq_dib[0]
#define dqc_dib dq_dib[1]

UNIT dqd_unit = { UDATA (&dqd_svc, 0, 0) };

REG dqd_reg[] = {
    { ORDATA (IBUF, dqd_ibuf, 16) },
    { ORDATA (OBUF, dqd_obuf, 16) },
    { BRDATA (DBUF, dqxb, 8, 16, DQ_NUMWD) },
    { DRDATA (BPTR, dq_ptr, DQ_N_NUMWD) },
    { FLDATA (CMD, dqd_command, 0) },
    { FLDATA (CTL, dqd_control, 0) },
    { FLDATA (FLG, dqd_flag,    0) },
    { FLDATA (FBF, dqd_flagbuf, 0) },
    { FLDATA (XFER, dqd_xfer, 0) },
    { FLDATA (WVAL, dqd_wval, 0) },
    { ORDATA (DEVNO, dqd_dib.devno, 6), REG_HRO },
    { NULL }
    };

MTAB dqd_mod[] = {
    { MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
      &hp_setdev, &hp_showdev, &dqd_dev },
    { 0 }
    };

DEVICE dqd_dev = {
    "DQD", &dqd_unit, dqd_reg, dqd_mod,
    1, 10, DQ_N_NUMWD, 1, 8, 16,
    NULL, NULL, &dqc_reset,
    NULL, NULL, NULL,
    &dqd_dib, DEV_DISABLE
    };

/* DQC data structures

   dqc_dev      DQC device descriptor
   dqc_unit     DQC unit list
   dqc_reg      DQC register list
   dqc_mod      DQC modifier list
*/

UNIT dqc_unit[] = {
    { UDATA (&dqc_svc, UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_UNLOAD, DQ_SIZE) },
    { UDATA (&dqc_svc, UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_UNLOAD, DQ_SIZE) }
    };

REG dqc_reg[] = {
    { ORDATA (OBUF, dqc_obuf, 16) },
    { ORDATA (BUSY, dqc_busy, 2), REG_RO },
    { ORDATA (CNT, dqc_cnt, 9) },
    { FLDATA (CMD, dqc_command, 0) },
    { FLDATA (CTL, dqc_control, 0) },
    { FLDATA (FLG, dqc_flag,    0) },
    { FLDATA (FBF, dqc_flagbuf, 0) },
    { DRDATA (RARC, dqc_rarc, 8), PV_RZRO | REG_FIT },
    { DRDATA (RARH, dqc_rarh, 5), PV_RZRO | REG_FIT },
    { DRDATA (RARS, dqc_rars, 5), PV_RZRO | REG_FIT },
    { BRDATA (CYL, dqc_ucyl, 10, 8, DQ_NUMDRV), PV_RZRO },
    { BRDATA (HED, dqc_uhed, 10, 5, DQ_NUMDRV), PV_RZRO },
    { BRDATA (STA, dqc_sta, 8, 16, DQ_NUMDRV) },
    { DRDATA (CTIME, dqc_ctime, 24), PV_LEFT },
    { DRDATA (DTIME, dqc_dtime, 24), PV_LEFT },
    { DRDATA (STIME, dqc_stime, 24), PV_LEFT },
    { DRDATA (XTIME, dqc_xtime, 24), REG_NZ + PV_LEFT },
    { URDATA (UFNC, dqc_unit[0].FNC, 8, 8, 0,
              DQ_NUMDRV, REG_HRO) },
    { ORDATA (DEVNO, dqc_dib.devno, 6), REG_HRO },
    { NULL }
    };

MTAB dqc_mod[] = {
    { UNIT_UNLOAD, UNIT_UNLOAD, "heads unloaded", "UNLOADED", dqc_load_unload },
    { UNIT_UNLOAD, 0, "heads loaded", "LOADED", dqc_load_unload },
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
    { MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
      &hp_setdev, &hp_showdev, &dqd_dev },
    { 0 }
    };

DEVICE dqc_dev = {
    "DQC", dqc_unit, dqc_reg, dqc_mod,
    DQ_NUMDRV, 8, 24, 1, 8, 16,
    NULL, NULL, &dqc_reset,
    &dqc_boot, &dqc_attach, &dqc_detach,
    &dqc_dib, DEV_DISABLE
    };


/* Data channel I/O signal handler */

uint32 dqdio (uint32 select_code, IOSIG signal, uint32 data)
{
const IOSIG base_signal = IOBASE (signal);              /* derive base signal */

switch (base_signal) {                                  /* dispatch base I/O signal */

    case ioCLF:                                         /* clear flag flip-flop */
        dqd_flag = dqd_flagbuf = CLEAR;
        break;


    case ioSTF:                                         /* set flag flip-flop */
    case ioENF:                                         /* enable flag */
        dqd_flag = dqd_flagbuf = SET;
        break;


    case ioSFC:                                         /* skip if flag is clear */
        setstdSKF (dqd);
        break;


    case ioSFS:                                         /* skip if flag is set */
        setstdSKF (dqd);
        break;


    case ioIOI:                                         /* I/O data input */
        data = dqd_ibuf;
        break;


    case ioIOO:                                         /* I/O data output */
        dqd_obuf = data;

        if (!dqc_busy || dqd_xfer)
            dqd_wval = 1;                               /* if !overrun, valid */
        break;


    case ioPOPIO:                                       /* power-on preset to I/O */
        dqd_flag = dqd_flagbuf = SET;                   /* set flag and flag buffer */
        dqd_obuf = 0;                                   /* clear output buffer */
                                                        /* fall into CRS handler */

    case ioCRS:                                         /* control reset */
        dqd_command = CLEAR;                            /* clear command */
                                                        /* fall into CLC handler */

    case ioCLC:                                         /* clear control flip-flop */
        dqd_control = CLEAR;                            /* clear control */
        dqd_xfer = 0;                                   /* clr xfer */
        break;


    case ioSTC:                                         /* set control flip-flop */
        dqd_command = SET;                              /* set ctl, cmd */
        dqd_control = SET;

        if (dqc_busy && !dqd_xfer)                      /* overrun? */
            dqc_sta[dqc_busy - 1] |= STA_DTE;
        break;


    case ioSIR:                                         /* set interrupt request */
        setstdPRL (select_code, dqd);                   /* set standard PRL signal */
        setstdIRQ (select_code, dqd);                   /* set standard IRQ signal */
        setstdSRQ (select_code, dqd);                   /* set standard SRQ signal */
        break;


    case ioIAK:                                         /* interrupt acknowledge */
        dqd_flagbuf = CLEAR;
        break;


    default:                                            /* all other signals */
        break;                                          /*   are ignored */
    }


if (signal > ioCLF)                                     /* multiple signals? */
    dqdio (select_code, ioCLF, 0);                      /* issue CLF */
else if (signal > ioSIR)                                /* signal affected interrupt status? */
    dqdio (select_code, ioSIR, 0);                      /* set interrupt request */

return data;
}


/* Command channel I/O signal handler.

   Implementation notes:

    1. The input buffer register is not connected to the disc controller.
       Pullups on the card and an inversion result in reading zeros when IOI is
       signalled.
*/

uint32 dqcio (uint32 select_code, IOSIG signal, uint32 data)
{
const IOSIG base_signal = IOBASE (signal);              /* derive base signal */
int32 fnc, drv;

switch (base_signal) {                                  /* dispatch base I/O signal */

    case ioCLF:                                         /* clear flag flip-flop */
        dqc_flag = dqc_flagbuf = CLEAR;
        break;


    case ioSTF:                                         /* set flag flip-flop */
    case ioENF:                                         /* enable flag */
        dqc_flag = dqc_flagbuf = SET;
        break;


    case ioSFC:                                         /* skip if flag is clear */
        setstdSKF (dqc);
        break;


    case ioSFS:                                         /* skip if flag is set */
        setstdSKF (dqc);
        break;


    case ioIOI:                                         /* I/O data input */
        data = 0;                                       /* no data */
        break;


    case ioIOO:                                         /* I/O data output */
        dqc_obuf = data;
        break;


    case ioPOPIO:                                       /* power-on preset to I/O */
        dqc_flag = dqc_flagbuf = SET;                   /* set flag and flag buffer */
        dqc_obuf = 0;                                   /* clear output buffer */
                                                        /* fall into CRS handler */

    case ioCRS:                                         /* control reset */
                                                        /* fall into CLC handler */

    case ioCLC:                                         /* clear control flip-flop */
        dqc_command = CLEAR;                            /* clear command */
        dqc_control = CLEAR;                            /* clear control */

        if (dqc_busy)
            sim_cancel (&dqc_unit[dqc_busy - 1]);

        sim_cancel (&dqd_unit);                         /* cancel dch */
        dqd_xfer = 0;                                   /* clr dch xfer */
        dqc_busy = 0;                                   /* clr busy */
        break;


    case ioSTC:                                         /* set control flip-flop */
        dqc_control = SET;                              /* set ctl */

        if (!dqc_command) {                             /* cmd clr? */
            dqc_command = SET;                          /* set cmd */
            drv = CW_GETDRV (dqc_obuf);                 /* get fnc, drv */
            fnc = CW_GETFNC (dqc_obuf);                 /* from cmd word */

            switch (fnc) {                              /* case on fnc */
                case FNC_SEEK: case FNC_RCL:            /* seek, recal */
                case FNC_CHK:                           /* check */
                    dqc_sta[drv] = 0;                   /* clear status */
                case FNC_STA: case FNC_LA:              /* rd sta, load addr */
                    dq_god (fnc, drv, dqc_dtime);       /* sched dch xfer */
                    break;
                case FNC_RD: case FNC_WD:               /* read, write */
                case FNC_RA: case FNC_WA:               /* rd addr, wr addr */
                case FNC_AS:                            /* address skip */
                    dq_goc (fnc, drv, dqc_ctime);       /* sched drive */
                    break;
                }                                       /* end case */
            }                                           /* end if !CMD */
        break;


    case ioSIR:                                         /* set interrupt request */
        setstdPRL (select_code, dqc);                   /* set standard PRL signal */
        setstdIRQ (select_code, dqc);                   /* set standard IRQ signal */
        setstdSRQ (select_code, dqc);                   /* set standard SRQ signal */
        break;


    case ioIAK:                                         /* interrupt acknowledge */
        dqc_flagbuf = CLEAR;
        break;


    default:                                            /* all other signals */
        break;                                          /*   are ignored */
    }


if (signal > ioCLF)                                     /* multiple signals? */
    dqcio (select_code, ioCLF, 0);                      /* issue CLF */
else if (signal > ioSIR)                                /* signal affected interrupt status? */
    dqcio (select_code, ioSIR, 0);                      /* set interrupt request */

return data;
}


/* Start data channel operation */

void dq_god (int32 fnc, int32 drv, int32 time)
{
dqd_unit.DRV = drv;                                     /* save unit */
dqd_unit.FNC = fnc;                                     /* save function */
sim_activate (&dqd_unit, time);
return;
}

/* Start controller operation */

void dq_goc (int32 fnc, int32 drv, int32 time)
{
int32 t;

if (t = sim_is_active (&dqc_unit[drv])) {               /* still seeking? */
    sim_cancel (&dqc_unit[drv]);                        /* cancel */
    time = time + t;                                    /* include seek time */
    }
dqc_sta[drv] = 0;                                       /* clear status */
dq_ptr = 0;                                             /* init buf ptr */
dqc_busy = drv + 1;                                     /* set busy */
dqd_xfer = 1;                                           /* xfer in prog */
dqc_unit[drv].FNC = fnc;                                /* save function */
sim_activate (&dqc_unit[drv], time);                    /* activate unit */
return;
}

/* Data channel unit service

   This routine handles the data channel transfers.  It also handles
   data transfers that are blocked by seek in progress.

   uptr->DRV    =       target drive
   uptr->FNC    =       target function

   Seek substates
        seek    -       transfer cylinder
        seek1   -       transfer head/surface, sched drive
   Recalibrate substates
        rcl     -       clear cyl/head/surface, sched drive
   Load address
        la      -       transfer cylinder
        la1     -       transfer head/surface, finish operation
   Status check -       transfer status, finish operation
   Check data
        chk     -       transfer sector count, sched drive
*/

t_stat dqd_svc (UNIT *uptr)
{
int32 drv, st;

drv = uptr->DRV;                                        /* get drive no */

switch (uptr->FNC) {                                    /* case function */

    case FNC_LA:                                        /* arec, need cyl */
    case FNC_SEEK:                                      /* seek, need cyl */
        if (dqd_command) {                              /* dch active? */
            dqc_rarc = DA_GETCYL (dqd_obuf);            /* set RAR from cyl word */
            dqd_wval = 0;                               /* clr data valid */
            dqd_command = CLEAR;                        /* clr dch cmd */
            dqdio (dqd_dib.devno, ioENF, 0);            /* set dch flg */
            if (uptr->FNC == FNC_LA) uptr->FNC = FNC_LA1;
            else uptr->FNC = FNC_SEEK1;                 /* advance state */
            }
        sim_activate (uptr, dqc_xtime);                 /* no, wait more */
        break;

    case FNC_LA1:                                       /* arec, need hd/sec */
    case FNC_SEEK1:                                     /* seek, need hd/sec */
        if (dqd_command) {                              /* dch active? */
            dqc_rarh = DA_GETHD (dqd_obuf);             /* set RAR from head */
            dqc_rars = DA_GETSC (dqd_obuf);             /* set RAR from sector */
            dqd_wval = 0;                               /* clr data valid */
            dqd_command = CLEAR;                        /* clr dch cmd */
            dqdio (dqd_dib.devno, ioENF, 0);            /* set dch flg */
            if (uptr->FNC == FNC_LA1) {
                dqc_command = CLEAR;                    /* clr cch cmd */
                dqcio (dqc_dib.devno, ioENF, 0);        /* set cch flg */
                break;                                  /* done if Load Address */
                }
            if (sim_is_active (&dqc_unit[drv])) break;  /* if busy, seek check */
            st = abs (dqc_rarc - dqc_ucyl[drv]) * dqc_stime;
            if (st == 0) st = dqc_xtime;                /* if on cyl, min time */
            else dqc_sta[drv] = dqc_sta[drv] | STA_BSY; /* set busy */
            dqc_ucyl[drv] = dqc_rarc;                   /* transfer RAR */
            dqc_uhed[drv] = dqc_rarh;
            sim_activate (&dqc_unit[drv], st);          /* schedule op */
            dqc_unit[drv].FNC = FNC_SEEK2;              /* advance state */
            }
        else sim_activate (uptr, dqc_xtime);            /* no, wait more */
        break;

    case FNC_RCL:                                       /* recalibrate */
        dqc_rarc = dqc_rarh = dqc_rars = 0;             /* clear RAR */
        if (sim_is_active (&dqc_unit[drv])) break;      /* ignore if busy */
        st = dqc_ucyl[drv] * dqc_stime;                 /* calc diff */
        if (st == 0) st = dqc_xtime;                    /* if on cyl, min time */
        else dqc_sta[drv] = dqc_sta[drv] | STA_BSY;     /* set busy */
        sim_activate (&dqc_unit[drv], st);              /* schedule drive */
        dqc_ucyl[drv] = dqc_uhed[drv] = 0;              /* clear drive pos */
        dqc_unit[drv].FNC = FNC_SEEK2;                  /* advance state */
        break;

    case FNC_STA:                                       /* read status */
        if (dqd_command) {                              /* dch active? */
            if ((dqc_unit[drv].flags & UNIT_UNLOAD) == 0)  /* drive up? */
                dqd_ibuf = dqc_sta[drv] & ~STA_DID;
            else dqd_ibuf = STA_NRDY;
            if (dqd_ibuf & STA_ANYERR)                  /* errors? set flg */
                dqd_ibuf = dqd_ibuf | STA_ERR;
            if (drv) dqd_ibuf = dqd_ibuf | STA_DID;
            dqc_command = CLEAR;                        /* clr cch cmd */
            dqd_command = CLEAR;                        /* clr dch cmd */
            dqdio (dqd_dib.devno, ioENF, 0);            /* set dch flg */
            dqc_sta[drv] = dqc_sta[drv] & ~STA_ANYERR;  /* clr sta flags */
            }
        else sim_activate (uptr, dqc_xtime);            /* wait more */
        break;

    case FNC_CHK:                                       /* check, need cnt */
        if (dqd_command) {                              /* dch active? */
            dqc_cnt = dqd_obuf & DA_CKMASK;             /* get count */
            dqd_wval = 0;                               /* clr data valid */
            dq_goc (FNC_CHK1, drv, dqc_ctime);          /* sched drv */
            }
        else sim_activate (uptr, dqc_xtime);            /* wait more */
        break;

    default:
        return SCPE_IERR;
        }

return SCPE_OK;
}

/* Drive unit service

   This routine handles the data transfers.

   Seek substates
        seek2   -       done
   Recalibrate substate
        rcl1    -       done
   Check data substates
        chk1    -       finish operation
   Read
   Read address
   Address skip (read without header check)
   Write
   Write address
*/

#define GETDA(x,y,z) \
    (((((x) * DQ_NUMSF) + (y)) * DQ_NUMSC) + (z)) * DQ_NUMWD

t_stat dqc_svc (UNIT *uptr)
{
int32 da, drv, devc, devd, err;

err = 0;                                                /* assume no err */
drv = uptr - dqc_dev.units;                             /* get drive no */
devc = dqc_dib.devno;                                   /* get cch devno */
devd = dqd_dib.devno;                                   /* get dch devno */
if (uptr->flags & UNIT_UNLOAD) {                        /* drive down? */
    dqc_command = CLEAR;                                /* clr cch cmd */
    dqcio (dqc_dib.devno, ioENF, 0);                    /* set cch flg */
    dqc_sta[drv] = 0;                                   /* clr status */
    dqc_busy = 0;                                       /* ctlr is free */
    dqd_xfer = dqd_wval = 0;
    return SCPE_OK;
    }
switch (uptr->FNC) {                                    /* case function */

    case FNC_SEEK2:                                     /* seek done */
        if (dqc_ucyl[drv] >= DQ_NUMCY) {                /* out of range? */
            dqc_sta[drv] = dqc_sta[drv] | STA_BSY | STA_ERR;  /* seek check */
            dqc_ucyl[drv] = 0;                          /* seek to cyl 0 */
            }
        else dqc_sta[drv] = dqc_sta[drv] & ~STA_BSY;    /* drive not busy */
    case FNC_SEEK3:
        if (dqc_busy || dqc_flag) {                     /* ctrl busy? */
            uptr->FNC = FNC_SEEK3;                      /* next state */
            sim_activate (uptr, dqc_xtime);             /* ctrl busy? wait */
            }
        else {
            dqc_command = CLEAR;                        /* clr cch cmd */
            dqcio (dqc_dib.devno, ioENF, 0);            /* set cch flg */
            }
        return SCPE_OK;

    case FNC_RA:                                        /* read addr */
        if (!dqd_command) break;                        /* dch clr? done */
        if (dq_ptr == 0) dqd_ibuf = dqc_ucyl[drv];      /* 1st word? */
        else if (dq_ptr == 1) {                         /* second word? */
            dqd_ibuf = (dqc_uhed[drv] << DA_V_HD) |     /* use drive head */
                (dqc_rars << DA_V_SC);                  /* and RAR sector */
            dqc_rars = (dqc_rars + 1) % DQ_NUMSC;       /* incr sector */
            }
        else break;
        dq_ptr = dq_ptr + 1;
        dqd_command = CLEAR;                            /* clr dch cmd */
        dqdio (dqd_dib.devno, ioENF, 0);                /* set dch flg */
        sim_activate (uptr, dqc_xtime);                 /* sched next word */
        return SCPE_OK;

    case FNC_AS:                                        /* address skip */
    case FNC_RD:                                        /* read */
    case FNC_CHK1:                                      /* check */
        if (dq_ptr == 0) {                              /* new sector? */
            if (!dqd_command && (uptr->FNC != FNC_CHK1)) break;
            if ((dqc_rarc != dqc_ucyl[drv]) ||          /* RAR cyl miscompare? */
                (dqc_rarh != dqc_uhed[drv]) ||          /* RAR head miscompare? */
                (dqc_rars >= DQ_NUMSC)) {               /* bad sector? */
                dqc_sta[drv] = dqc_sta[drv] | STA_AER;  /* no record found err */
                break;
                }
            if (dqc_rarh >= DQ_NUMSF) {                 /* bad head? */
                dqc_sta[drv] = dqc_sta[drv] | STA_EOC;  /* end of cyl err */
                break;
                }
            da = GETDA (dqc_rarc, dqc_rarh, dqc_rars);  /* calc disk addr */
            dqc_rars = (dqc_rars + 1) % DQ_NUMSC;       /* incr sector */
            if (dqc_rars == 0)                          /* wrap? incr head */
                dqc_uhed[drv] = dqc_rarh = dqc_rarh + 1;
            if (err = fseek (uptr->fileref, da * sizeof (int16),
                SEEK_SET)) break;
            fxread (dqxb, sizeof (int16), DQ_NUMWD, uptr->fileref);
            if (err = ferror (uptr->fileref)) break;
            }
        dqd_ibuf = dqxb[dq_ptr++];                      /* get word */
        if (dq_ptr >= DQ_NUMWD) {                       /* end of sector? */
            if (uptr->FNC == FNC_CHK1) {                /* check? */
                dqc_cnt = (dqc_cnt - 1) & DA_CKMASK;    /* decr count */
                if (dqc_cnt == 0) break;                /* if zero, done */
                }
            dq_ptr = 0;                                 /* wrap buf ptr */
            }
        if (dqd_command && dqd_xfer) {                  /* dch on, xfer? */
            dqdio (dqd_dib.devno, ioENF, 0);            /* set flag */
            }
        dqd_command = CLEAR;                            /* clr dch cmd */
        sim_activate (uptr, dqc_xtime);                 /* sched next word */
        return SCPE_OK;

    case FNC_WA:                                        /* write address */
    case FNC_WD:                                        /* write */
        if (dq_ptr == 0) {                              /* sector start? */
            if (!dqd_command && !dqd_wval) break;       /* xfer done? */
            if (uptr->flags & UNIT_WPRT) {              /* write protect? */
                dqc_sta[drv] = dqc_sta[drv] | STA_FLG;
                break;                                  /* done */
                }
            if ((dqc_rarc != dqc_ucyl[drv]) ||          /* RAR cyl miscompare? */
                (dqc_rarh != dqc_uhed[drv]) ||          /* RAR head miscompare? */
                (dqc_rars >= DQ_NUMSC)) {               /* bad sector? */
                dqc_sta[drv] = dqc_sta[drv] | STA_AER;  /* no record found err */
                break;
                }
            if (dqc_rarh >= DQ_NUMSF) {                 /* bad head? */
                dqc_sta[drv] = dqc_sta[drv] | STA_EOC;  /* end of cyl err */
                break;
                }
            }
        dqxb[dq_ptr++] = dqd_wval? dqd_obuf: 0;         /* store word/fill */
        dqd_wval = 0;                                   /* clr data valid */
        if (dq_ptr >= DQ_NUMWD) {                       /* buffer full? */
            da = GETDA (dqc_rarc, dqc_rarh, dqc_rars);  /* calc disk addr */
            dqc_rars = (dqc_rars + 1) % DQ_NUMSC;       /* incr sector */
            if (dqc_rars == 0)                          /* wrap? incr head */
                dqc_uhed[drv] = dqc_rarh = dqc_rarh + 1;
            if (err = fseek (uptr->fileref, da * sizeof (int16),
                SEEK_SET)) return TRUE;
            fxwrite (dqxb, sizeof (int16), DQ_NUMWD, uptr->fileref);
            if (err = ferror (uptr->fileref)) break;
            dq_ptr = 0;
            }
        if (dqd_command && dqd_xfer) {                  /* dch on, xfer? */
            dqdio (dqd_dib.devno, ioENF, 0);            /* set flag */
            }
        dqd_command = CLEAR;                            /* clr dch cmd */
        sim_activate (uptr, dqc_xtime);                 /* sched next word */
        return SCPE_OK;

    default:
        return SCPE_IERR;
        }                                               /* end case fnc */

dqc_command = CLEAR;                                    /* clr cch cmd */
dqcio (dqc_dib.devno, ioENF, 0);                        /* set cch flg */
dqc_busy = 0;                                           /* ctlr is free */
dqd_xfer = dqd_wval = 0;
if (err != 0) {                                         /* error? */
    perror ("DQ I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Reset routine */

t_stat dqc_reset (DEVICE *dptr)
{
int32 drv;

hp_enbdis_pair (dptr,                                   /* make pair cons */
    (dptr == &dqd_dev)? &dqc_dev: &dqd_dev);

if (sim_switches & SWMASK ('P')) {                      /* PON reset? */
    dqd_ibuf = 0;                                       /* clear buffers */
    dqd_obuf = 0;
    dqc_obuf = 0;
    dqc_rarc = dqc_rarh = dqc_rars = 0;                 /* clear RAR */
    }

if (dptr == &dqc_dev)                                   /* command channel reset? */
    dqcio (dqc_dib.devno, ioPOPIO, 0);                  /* send POPIO signal to command channel */
else                                                    /* data channel reset */
    dqdio (dqd_dib.devno, ioPOPIO, 0);                  /* send POPIO signal to data channel */

dqc_busy = 0;                                           /* reset controller state */
dqd_xfer = 0;
dqd_wval = 0;
dq_ptr = 0;

sim_cancel (&dqd_unit);                                 /* cancel dch */

for (drv = 0; drv < DQ_NUMDRV; drv++) {                 /* loop thru drives */
    sim_cancel (&dqc_unit[drv]);                        /* cancel activity */
    dqc_unit[drv].FNC = 0;                              /* clear function */
    dqc_ucyl[drv] = dqc_uhed[drv] = 0;                  /* clear drive pos */
    dqc_sta[drv] = 0;                                   /* clear status */
    }

return SCPE_OK;
}

/* Attach routine */

t_stat dqc_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = attach_unit (uptr, cptr);                           /* attach unit */
if (r == SCPE_OK) dqc_load_unload (uptr, 0, NULL, NULL);/* if OK, load heads */
return r;
}

/* Detach routine */

t_stat dqc_detach (UNIT* uptr)
{
dqc_load_unload (uptr, UNIT_UNLOAD, NULL, NULL);        /* unload heads */
return detach_unit (uptr);                              /* detach unit */
}

/* Load and unload heads */

t_stat dqc_load_unload (UNIT *uptr, int32 value, char *cptr, void *desc)
{
if ((uptr->flags & UNIT_ATT) == 0) return SCPE_UNATT;   /* must be attached to load */
if (value == UNIT_UNLOAD)                               /* unload heads? */
    uptr->flags = uptr->flags | UNIT_UNLOAD;            /* indicate unload */
else uptr->flags = uptr->flags & ~UNIT_UNLOAD;          /* indicate load */
return SCPE_OK;
}

/* 7900/7901/2883/2884 bootstrap routine (HP 12992A ROM) */

const BOOT_ROM dq_rom = {
    0102501,                    /*ST LIA 1              ; get switches */
    0106501,                    /*   LIB 1 */
    0013765,                    /*   AND D7             ; isolate hd */
    0005750,                    /*   BLF,CLE,SLB */
    0027741,                    /*   JMP RD */
    0005335,                    /*   RBR,SLB,ERB        ; <13>->E, set = 2883 */
    0027717,                    /*   JMP IS */
    0102611,                    /*LP OTA CC             ; do 7900 status to */
    0103711,                    /*   STC CC,C           ; clear first seek */
    0102310,                    /*   SFS DC */
    0027711,                    /*   JMP *-1 */
    0002004,                    /*   INA                ; get next drive */
    0053765,                    /*   CPA D7             ; all cleared? */
    0002001,                    /*   RSS */
    0027707,                    /*   JMP LP */
    0067761,                    /*IS LDB SEEKC          ; get seek comnd */
    0106610,                    /*   OTB DC             ; issue cyl addr (0) */
    0103710,                    /*   STC DC,C           ; to dch */
    0106611,                    /*   OTB CC             ; seek cmd */
    0103711,                    /*   STC CC,C           ; to cch */
    0102310,                    /*   SFS DC             ; addr wd ok? */
    0027724,                    /*   JMP *-1            ; no, wait */
    0006400,                    /*   CLB */
    0102501,                    /*   LIA 1              ; get switches */
    0002051,                    /*   SEZ,SLA,RSS        ; subchan = 1 or ISS */
    0047770,                    /*   ADB BIT9           ; head 2 */
    0106610,                    /*   OTB DC             ; head/sector */
    0103710,                    /*   STC DC,C           ; to dch */
    0102311,                    /*   SFS CC             ; seek done? */
    0027734,                    /*   JMP *-1            ; no, wait */
    0063731,                    /*   LDA ISSRD          ; get read read */
    0002341,                    /*   SEZ,CCE,RSS        ; iss disc? */
    0001100,                    /*   ARS                ; no, make 7900 read */
    0067776,                    /*RD LDB DMACW          ; DMA control */
    0106606,                    /*   OTB 6 */
    0067762,                    /*   LDB ADDR1          ; memory addr */
    0077741,                    /*   STB RD             ; make non re-executable */
    0106602,                    /*   OTB 2 */
    0102702,                    /*   STC 2              ; flip DMA ctrl */
    0067764,                    /*   LDB COUNT          ; word count */
    0106602,                    /*   OTB 2 */
    0002041,                    /*   SEZ,RSS */
    0027766,                    /*   JMP NW */
    0102611,                    /*   OTA CC             ; to cch */
    0103710,                    /*   STC DC,C           ; start dch */
    0103706,                    /*   STC 6,C            ; start DMA */
    0103711,                    /*   STC CC,C           ; start cch */
    0037773,                    /*   ISZ SK */
    0027773,                    /*   JMP SK */
    0030000,                    /*SEEKC 030000 */
    0102011,                    /*ADDR1 102011 */
    0102055,                    /*ADDR2 102055 */
    0164000,                    /*COUNT -6144. */
    0000007,                    /*D7    7 */
    0106710,                    /*NW CLC DC             ; set 'next wd is cmd' flag */
    0001720,                    /*   ALF,ALF            ; move to head number loc */
    0001000,                    /*BIT9 ALS */
    0103610,                    /*   OTA DC,C           ; output cold load cmd */
    0103706,                    /*   STC 6,C            ; start DMA */
    0102310,                    /*   SFS DC             ; done? */
    0027773,                    /*   JMP *-1            ; no, wait */
    0117763,                    /*XT JSB ADDR2,I        ; start program */
    0120010,                    /*DMACW 120000+DC */
    0000000                     /*   -ST */
    };

t_stat dqc_boot (int32 unitno, DEVICE *dptr)
{
int32 dev;

if (unitno != 0) return SCPE_NOFNC;                     /* only unit 0 */
dev = dqd_dib.devno;                                    /* get data chan dev */
if (ibl_copy (dq_rom, dev)) return SCPE_IERR;           /* copy boot to memory */
SR = (SR & IBL_OPT) | IBL_DQ | (dev << IBL_V_DEV);      /* set SR */
return SCPE_OK;
}
