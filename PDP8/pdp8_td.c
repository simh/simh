/* pdp8_td.c: PDP-8 simple DECtape controller (TD8E) simulator

   Copyright (c) 1993-2013, Robert M Supnik

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

   This module was inspired by Gerold Pauler's TD8E simulator for Doug Jones'
   PDP8 simulator but tracks the hardware implementation more closely.

   td           TD8E/TU56 DECtape

   17-Sep-13    RMS     Changed to use central set_bootpc routine
   23-Mar-11    RMS     Fixed SDLC to clear AC (from Dave Gesswein)
   23-Jun-06    RMS     Fixed switch conflict in ATTACH
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   09-Jan-04    RMS     Changed sim_fsize calling sequence, added STOP_OFFR

   PDP-8 DECtapes are represented in memory by fixed length buffer of 12b words.
   Three file formats are supported:

        18b/36b                 256 words per block [256 x 18b]
        16b                     256 words per block [256 x 16b]
        12b                     129 words per block [129 x 12b]

   When a 16b or 18/36b DECtape file is read in, it is converted to 12b format.

   DECtape motion is measured in 3b lines.  Time between lines is 33.33us.
   Tape density is nominally 300 lines per inch.  The format of a DECtape (as
   taken from the TD8E formatter) is:

        reverse end zone        8192 reverse end zone codes ~ 10 feet
        reverse buffer          200 interblock codes
        block 0
         :
        block n
        forward buffer          200 interblock codes
        forward end zone        8192 forward end zone codes ~ 10 feet

   A block consists of five 18b header words, a tape-specific number of data
   words, and five 18b trailer words.  All systems except the PDP-8 use a
   standard block length of 256 words; the PDP-8 uses a standard block length
   of 86 words (x 18b = 129 words x 12b).

   Because a DECtape file only contains data, the simulator cannot support
   write timing and mark track and can only do a limited implementation
   of non-data words.  Read assumes that the tape has been conventionally
   written forward:

        header word 0           0
        header word 1           block number (for forward reads)
        header words 2,3        0
        header word 4           checksum (for reverse reads)
        :
        trailer word 4          checksum (for forward reads)
        trailer words 3,2       0
        trailer word 1          block number (for reverse reads)
        trailer word 0          0

   Write modifies only the data words and dumps the non-data words in the
   bit bucket.
*/

#include "pdp8_defs.h"

#define DT_NUMDR        2                               /* #drives */
#define UNIT_V_8FMT     (UNIT_V_UF + 0)                 /* 12b format */
#define UNIT_V_11FMT    (UNIT_V_UF + 1)                 /* 16b format */
#define UNIT_8FMT       (1 << UNIT_V_8FMT)
#define UNIT_11FMT      (1 << UNIT_V_11FMT)
#define STATE           u3                              /* unit state */
#define LASTT           u4                              /* last time update */
#define WRITTEN         u5                              /* device buffer is dirty and needs flushing */

/* System independent DECtape constants */

#define DT_LPERMC       6                               /* lines per mark track */
#define DT_EZLIN        (8192 * DT_LPERMC)              /* end zone length */
#define DT_BFLIN        (200 * DT_LPERMC)               /* end zone buffer */
#define DT_HTLIN        (5 * DT_LPERMC)                 /* lines per hdr/trlr */

/* 16b, 18b, 36b DECtape constants */

#define D18_WSIZE       6                               /* word size in lines */
#define D18_BSIZE       384                             /* block size in 12b */
#define D18_TSIZE       578                             /* tape size */
#define D18_LPERB       (DT_HTLIN + (D18_BSIZE * DT_WSIZE) + DT_HTLIN)
#define D18_FWDEZ       (DT_EZLIN + (D18_LPERB * D18_TSIZE))
#define D18_CAPAC       (D18_TSIZE * D18_BSIZE)         /* tape capacity */

#define D18_NBSIZE      ((D18_BSIZE * D8_WSIZE) / D18_WSIZE)
#define D18_FILSIZ      (D18_NBSIZE * D18_TSIZE * sizeof (int32))
#define D11_FILSIZ      (D18_NBSIZE * D18_TSIZE * sizeof (int16))

/* 12b DECtape constants */

#define D8_WSIZE        4                               /* word size in lines */
#define D8_BSIZE        129                             /* block size in 12b */
#define D8_TSIZE        1474                            /* tape size */
#define D8_LPERB        (DT_HTLIN + (D8_BSIZE * DT_WSIZE) + DT_HTLIN)
#define D8_FWDEZ        (DT_EZLIN + (D8_LPERB * D8_TSIZE))
#define D8_CAPAC        (D8_TSIZE * D8_BSIZE)           /* tape capacity */
#define D8_FILSIZ       (D8_CAPAC * sizeof (int16))

/* This controller */

#define DT_CAPAC        D8_CAPAC                        /* default */
#define DT_WSIZE        D8_WSIZE

/* Calculated constants, per unit */

#define DTU_BSIZE(u)    (((u)->flags & UNIT_8FMT)? D8_BSIZE: D18_BSIZE)
#define DTU_TSIZE(u)    (((u)->flags & UNIT_8FMT)? D8_TSIZE: D18_TSIZE)
#define DTU_LPERB(u)    (((u)->flags & UNIT_8FMT)? D8_LPERB: D18_LPERB)
#define DTU_FWDEZ(u)    (((u)->flags & UNIT_8FMT)? D8_FWDEZ: D18_FWDEZ)
#define DTU_CAPAC(u)    (((u)->flags & UNIT_8FMT)? D8_CAPAC: D18_CAPAC)

#define DT_LIN2BL(p,u)  (((p) - DT_EZLIN) / DTU_LPERB (u))
#define DT_LIN2OF(p,u)  (((p) - DT_EZLIN) % DTU_LPERB (u))

/* Command register */

#define TDC_UNIT        04000                           /* unit select */
#define TDC_FWDRV       02000                           /* fwd/rev */
#define TDC_STPGO       01000                           /* stop/go */
#define TDC_RW          00400                           /* read/write */
#define TDC_MASK        07400                           /* implemented */
#define TDC_GETUNIT(x)  (((x) & TDC_UNIT)? 1: 0)

/* Status register */

#define TDS_WLO         00200                           /* write lock */
#define TDS_TME         00100                           /* timing/sel err */

/* Mark track register and codes */

#define MTK_MASK        077
#define MTK_REV_END     055                             /* rev end zone */
#define MTK_INTER       025                             /* interblock */
#define MTK_FWD_BLK     026                             /* fwd block */
#define MTK_REV_GRD     032                             /* reverse guard */
#define MTK_FWD_PRE     010                             /* lock, etc */
#define MTK_DATA        070                             /* data */
#define MTK_REV_PRE     073                             /* lock, etc */
#define MTK_FWD_GRD     051                             /* fwd guard */
#define MTK_REV_BLK     045                             /* rev block */
#define MTK_FWD_END     022                             /* fwd end zone */

/* DECtape state */

#define STA_STOP        0                               /* stopped */
#define STA_DEC         2                               /* decelerating */
#define STA_ACC         4                               /* accelerating */
#define STA_UTS         6                               /* up to speed */
#define STA_DIR         1                               /* fwd/rev */

#define ABS(x)          (((x) < 0)? (-(x)): (x))
#define MTK_BIT(c,p)    (((c) >> (DT_LPERMC - 1 - ((p) % DT_LPERMC))) & 1)

/* State and declarations */

int32 td_cmd = 0;                                       /* command */
int32 td_dat = 0;                                       /* data */
int32 td_mtk = 0;                                       /* mark track */
int32 td_slf = 0;                                       /* single line flag */
int32 td_qlf = 0;                                       /* quad line flag */
int32 td_tme = 0;                                       /* timing error flag */
int32 td_csum = 0;                                      /* save check sum */
int32 td_qlctr = 0;                                     /* quad line ctr */
int32 td_ltime = 20;                                    /* interline time */
int32 td_dctime = 40000;                                /* decel time */
int32 td_stopoffr = 0;
static uint8 tdb_mtk[DT_NUMDR][D18_LPERB];              /* mark track bits */

int32 td77 (int32 IR, int32 AC);
t_stat td_svc (UNIT *uptr);
t_stat td_reset (DEVICE *dptr);
t_stat td_attach (UNIT *uptr, CONST char *cptr);
void td_flush (UNIT *uptr);
t_stat td_detach (UNIT *uptr);
t_stat td_boot (int32 unitno, DEVICE *dptr);
t_bool td_newsa (int32 newf);
t_bool td_setpos (UNIT *uptr);
int32 td_header (UNIT *uptr, int32 blk, int32 line);
int32 td_trailer (UNIT *uptr, int32 blk, int32 line);
int32 td_read (UNIT *uptr, int32 blk, int32 line);
void td_write (UNIT *uptr, int32 blk, int32 line, int32 datb);
int32 td_set_mtk (int32 code, int32 u, int32 k);
t_stat td_show_pos (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
const char *td_description (DEVICE *dptr);

extern uint16 M[];

/* TD data structures

   td_dev       DT device descriptor
   td_unit      DT unit list
   td_reg       DT register list
   td_mod       DT modifier list
*/

DIB td_dib = { DEV_TD8E, 1, { &td77 } };

UNIT td_unit[] = {
    { UDATA (&td_svc, UNIT_8FMT+UNIT_FIX+UNIT_ATTABLE+
             UNIT_DISABLE+UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&td_svc, UNIT_8FMT+UNIT_FIX+UNIT_ATTABLE+
             UNIT_DISABLE+UNIT_ROABLE, DT_CAPAC) }
    };

REG td_reg[] = {
    { GRDATAD (TDCMD, td_cmd, 8, 4, 8, "command register") },
    { ORDATAD (TDDAT, td_dat, 12, "data register") },
    { ORDATAD (TDMTK, td_mtk, 6, "mark track register") },
    { FLDATAD (TDSLF, td_slf, 0, "single line flag") },
    { FLDATAD (TDQLF, td_qlf, 0, "quad line flag") },
    { FLDATAD (TDTME, td_tme, 0, "timing error flag") },
    { ORDATAD (TDQL, td_qlctr, 2, "quad line counter") },
    { ORDATA (TDCSUM, td_csum, 6), REG_RO },
    { DRDATAD (LTIME, td_ltime, 31, "time between lines"), REG_NZ | PV_LEFT },
    { DRDATAD (DCTIME, td_dctime, 31, "time to decelerate to a full stop"), REG_NZ | PV_LEFT },
    { URDATAD (POS, td_unit[0].pos, 10, T_ADDR_W, 0,
              DT_NUMDR, PV_LEFT | REG_RO, "positions, in lines, units 0 and 1") },
    { URDATAD (STATT, td_unit[0].STATE, 8, 18, 0,
              DT_NUMDR, REG_RO, "unit state, units 0 and 1") },
    { URDATA (LASTT, td_unit[0].LASTT, 10, 32, 0,
              DT_NUMDR, REG_HRO) },
    { FLDATAD (STOP_OFFR, td_stopoffr, 0, "stop on off-reel error") },
    { ORDATA (DEVNUM, td_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB td_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write enable drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "Write lock drive" },
    { UNIT_8FMT + UNIT_11FMT, 0, "18b", NULL, NULL },
    { UNIT_8FMT + UNIT_11FMT, UNIT_8FMT, "12b", NULL, NULL },
    { UNIT_8FMT + UNIT_11FMT, UNIT_11FMT, "16b", NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "POSITION", NULL, NULL, &td_show_pos },
    { 0 }
    };

DEVICE td_dev = {
    "TD", td_unit, td_reg, td_mod,
    DT_NUMDR, 8, 24, 1, 8, 12,
    NULL, NULL, &td_reset,
    &td_boot, &td_attach, &td_detach,
    &td_dib, DEV_DISABLE | DEV_DIS, 0,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &td_description
    };

/* IOT routines */

int32 td77 (int32 IR, int32 AC)
{
int32 pulse = IR & 07;
int32 u = TDC_GETUNIT (td_cmd);                         /* get unit */
int32 diff, t;

switch (pulse) {

    case 01:                                            /* SDSS */
        if (td_slf)
            return AC | IOT_SKP;
        break;

    case 02:                                            /* SDST */
        if (td_tme)
            return AC | IOT_SKP;
        break;

    case 03:                                            /* SDSQ */
        if (td_qlf)
            return AC | IOT_SKP;
        break;

    case 04:                                            /* SDLC */
        td_tme = 0;                                     /* clear tim err */
        diff = (td_cmd ^ AC) & TDC_MASK;                /* cmd changes */
        td_cmd = AC & TDC_MASK;                         /* update cmd */
        if ((diff != 0) && (diff != TDC_RW)) {          /* signif change? */
            if (td_newsa (td_cmd))                      /* new command */
                return AC | (IORETURN (td_stopoffr, STOP_DTOFF) << IOT_V_REASON);
            }
        AC = 0;
        break;

    case 05:                                            /* SDLD */
        td_slf = 0;                                     /* clear flags */
        td_qlf = 0;
        td_qlctr = 0;
        td_dat = AC;                                    /* load data reg */
        break;

    case 06:                                            /* SDRC */
        td_slf = 0;                                     /* clear flags */
        td_qlf = 0;
        td_qlctr = 0;
        t = td_cmd | td_mtk;                            /* form status */
        if (td_tme || !(td_unit[u].flags & UNIT_ATT))   /* tim/sel err? */
            t = t | TDS_TME;
        if (td_unit[u].flags & UNIT_WPRT)               /* write locked? */
            t = t | TDS_WLO;
        return t;                                       /* return status */

    case 07:                                            /* SDRD */
        td_slf = 0;                                     /* clear flags */
        td_qlf = 0;
        td_qlctr = 0;
        return td_dat;                                  /* return data */
        }

return AC;
}

/* Command register change (start/stop, forward/reverse, new unit)

   1. If change in motion, stop to start
        - schedule up to speed
        - set function as next state
   2. If change in motion, start to stop, or change in direction
        - schedule stop
*/

t_bool td_newsa (int32 newf)
{
int32 prev_mving, new_mving, prev_dir, new_dir;
UNIT *uptr;

uptr = td_dev.units + TDC_GETUNIT (newf);               /* new unit */
if ((uptr->flags & UNIT_ATT) == 0)                      /* new unit attached? */
    return FALSE;

new_mving = ((newf & TDC_STPGO) != 0);                  /* new moving? */
prev_mving = (uptr->STATE != STA_STOP);                 /* previous moving? */
new_dir = ((newf & TDC_FWDRV) != 0);                    /* new dir? */
prev_dir = ((uptr->STATE & STA_DIR) != 0);              /* previous dir? */

td_mtk = 0;                                             /* mark trk reg cleared */

if (!prev_mving && !new_mving)                          /* stop from stop? */
    return FALSE;

if (new_mving && !prev_mving) {                         /* start from stop? */
    if (td_setpos (uptr))                               /* update pos */
        return TRUE;
    sim_cancel (uptr);                                  /* stop current */
    sim_activate (uptr, td_dctime - (td_dctime >> 2));  /* sched accel */
    uptr->STATE = STA_ACC | new_dir;                    /* set status */
    td_slf = td_qlf = td_qlctr = 0;                     /* clear state */
    return FALSE;
    }

if ((prev_mving && !new_mving) ||                       /* stop from moving? */
    (prev_dir != new_dir)) {                            /* dir chg while moving? */
    if (uptr->STATE >= STA_ACC) {                       /* not stopping? */
        if (td_setpos (uptr))                           /* update pos */
            return TRUE;
        sim_cancel (uptr);                              /* stop current */
        sim_activate (uptr, td_dctime);                 /* schedule decel */
        uptr->STATE = STA_DEC | prev_dir;               /* set status */
        td_slf = td_qlf = td_qlctr = 0;                 /* clear state */
        }
    return FALSE;
    }

return FALSE;   
}

/* Update DECtape position

   DECtape motion is modeled as a constant velocity, with linear
   acceleration and deceleration.  The motion equations are as follows:

        t       =       time since operation started
        tmax    =       time for operation (accel, decel only)
        v       =       at speed velocity in lines (= 1/td_ltime)

   Then:
        at speed dist = t * v
        accel dist = (t^2 * v) / (2 * tmax)
        decel dist = (((2 * t * tmax) - t^2) * v) / (2 * tmax)

   This routine uses the relative (integer) time, rather than the absolute
   (floating point) time, to allow save and restore of the start times.
*/

t_bool td_setpos (UNIT *uptr)
{
uint32 new_time, ut, ulin, udelt;
int32 delta = 0;

new_time = sim_grtime ();                               /* current time */
ut = new_time - uptr->LASTT;                            /* elapsed time */
if (ut == 0)                                            /* no time gone? exit */
    return FALSE;
uptr->LASTT = new_time;                                 /* update last time */
switch (uptr->STATE & ~STA_DIR) {                       /* case on motion */

    case STA_STOP:                                      /* stop */
        delta = 0;
        break;

    case STA_DEC:                                       /* slowing */
        ulin = ut / (uint32) td_ltime;
        udelt = td_dctime / td_ltime;
        delta = ((ulin * udelt * 2) - (ulin * ulin)) / (2 * udelt);
        break;

    case STA_ACC:                                       /* accelerating */
        ulin = ut / (uint32) td_ltime;
        udelt = (td_dctime - (td_dctime >> 2)) / td_ltime;
        delta = (ulin * ulin) / (2 * udelt);
        break;

    case STA_UTS:                                       /* at speed */
        delta = ut / (uint32) td_ltime;
        break;
        }

if (uptr->STATE & STA_DIR)                              /* update pos */
    uptr->pos = uptr->pos - delta;
else uptr->pos = uptr->pos + delta;
if (((int32) uptr->pos < 0) ||
    ((int32) uptr->pos > (DTU_FWDEZ (uptr) + DT_EZLIN))) {
    detach_unit (uptr);                                 /* off reel */
    sim_cancel (uptr);                                  /* no timing pulses */
    return TRUE;
    }
return FALSE;
}

/* Unit service - unit is either changing speed, or it is up to speed */

t_stat td_svc (UNIT *uptr)
{
int32 mot = uptr->STATE & ~STA_DIR;
int32 dir = uptr->STATE & STA_DIR;
int32 unum = uptr - td_dev.units;
int32 su = TDC_GETUNIT (td_cmd);
int32 mtkb, datb;

/* Motion cases

   Decelerating - if go, next state must be accel as specified by td_cmd
   Accelerating - next state must be up to speed, fall through
   Up to speed - process line */

if (mot == STA_STOP)                                    /* stopped? done */
    return SCPE_OK;
if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
    uptr->STATE = uptr->pos = 0;                        /* also done */
    return SCPE_UNATT;
    }

switch (mot) {                                          /* case on motion */

    case STA_DEC:                                       /* deceleration */
        if (td_setpos (uptr))                           /* upd pos; off reel? */
            return IORETURN (td_stopoffr, STOP_DTOFF);
        if ((unum != su) || !(td_cmd & TDC_STPGO))      /* not sel or stop? */
            uptr->STATE = 0;                            /* stop */
        else {                                          /* selected and go */
            uptr->STATE = STA_ACC |                     /* accelerating */
                ((td_cmd & TDC_FWDRV)? STA_DIR: 0);     /* in new dir */
            sim_activate (uptr, td_dctime - (td_dctime >> 2));
            }
        return SCPE_OK;

    case STA_ACC:                                       /* accelerating */
        if (td_setpos (uptr))                           /* upd pos; off reel? */
            return IORETURN (td_stopoffr, STOP_DTOFF);
        uptr->STATE = STA_UTS | dir;                    /* set up to speed */
        break;

    case STA_UTS:                                       /* up to speed */
        if (dir)                                        /* adjust position */
            uptr->pos = uptr->pos - 1;
        else uptr->pos = uptr->pos + 1;
        uptr->LASTT = sim_grtime ();                    /* save time */
        if (((int32) uptr->pos < 0) ||                  /* off reel? */
           (uptr->pos >= (((uint32) DTU_FWDEZ (uptr)) + DT_EZLIN))) {
            detach_unit (uptr);
            return IORETURN (td_stopoffr, STOP_DTOFF);
            }
        break;                                          /* check function */
        }

/* At speed - process the current line

   Once the TD8E is running at speed, it operates line by line.  If reading,
   the current mark track bit is shifted into the mark track register, and
   the current data nibble (3b) is shifted into the data register.  If
   writing, the current mark track bit is shifted into the mark track
   register, the top nibble from the data register is written to tape, and
   the data register is shifted up.  The complexity here comes from 
   synthesizing the mark track, based on tape position, and the header data. */

sim_activate (uptr, td_ltime);                          /* sched next line */
if (unum != su)                                         /* not sel? done */
    return SCPE_OK;
td_slf = 1;                                             /* set single */
td_qlctr = (td_qlctr + 1) % DT_WSIZE;                   /* count words */
if (td_qlctr == 0) {                                    /* lines mod 4? */
    if (td_qlf) {                                       /* quad line set? */
        td_tme = 1;                                     /* timing error */
        td_cmd = td_cmd & ~TDC_RW;                      /* clear write */
        }
    else td_qlf = 1;                                    /* no, set quad */
    }

datb = 0;                                               /* assume no data */
if (uptr->pos < (DT_EZLIN - DT_BFLIN))                  /* rev end zone? */
    mtkb = MTK_BIT (MTK_REV_END, uptr->pos);
else if (uptr->pos < DT_EZLIN)                          /* rev buffer? */
    mtkb = MTK_BIT (MTK_INTER, uptr->pos);
else if (uptr->pos < ((uint32) DTU_FWDEZ (uptr))) {     /* data zone? */
    int32 blkno = DT_LIN2BL (uptr->pos, uptr);          /* block # */
    int32 lineno = DT_LIN2OF (uptr->pos, uptr);         /* line # within block */
    if (lineno < DT_HTLIN) {                            /* header? */
        if ((td_cmd & TDC_RW) == 0)                     /* read? */                     
            datb = td_header (uptr, blkno, lineno);     /* get nibble */
        }
    else if (lineno < (DTU_LPERB (uptr) - DT_HTLIN)) {  /* data? */
        if (td_cmd & TDC_RW)                            /* write? */
            td_write (uptr, blkno,                      /* write data nibble */
                      lineno - DT_HTLIN,                /* data rel line num */
                      (td_dat >> 9) & 07);
        else datb = td_read (uptr, blkno,               /* no, read */
                             lineno - DT_HTLIN);
        }
    else if ((td_cmd & TDC_RW) == 0)                    /* trailer; read? */
        datb = td_trailer (uptr, blkno, lineno -        /* get trlr nibble */
                           (DTU_LPERB (uptr) - DT_HTLIN));
    mtkb = tdb_mtk[unum][lineno];
    }
else if (uptr->pos < (((uint32) DTU_FWDEZ (uptr)) + DT_BFLIN))
    mtkb = MTK_BIT (MTK_INTER, uptr->pos);              /* fwd buffer? */
else mtkb = MTK_BIT (MTK_FWD_END, uptr->pos);           /* fwd end zone */

if (dir) {                                              /* reverse? */
    mtkb = mtkb ^ 01;                                   /* complement mark bit, */
    datb = datb ^ 07;                                   /* data bits */
    }
td_mtk = ((td_mtk << 1) | mtkb) & MTK_MASK;             /* shift mark reg */
td_dat = ((td_dat << 3) | datb) & 07777;                /* shift data reg */
return SCPE_OK;
}

/* Header read - reads out 18b words in 3b increments

        word    lines           contents
        0       0-5             0
        1       6-11            block number
        2       12-17           0
        3       18-23           0
        4       24-29           reverse checksum (0777777)
*/

int32 td_header (UNIT *uptr, int32 blk, int32 line)
{
int32 nibp;

switch (line) {

    case 8: case 9: case 10: case 11:                   /* block num */
        nibp = 3 * (DT_LPERMC - 1 - (line % DT_LPERMC));
        return (blk >> nibp) & 07;

    case 24: case 25: case 26: case 27: case 28: case 29: /* rev csum */
        return 07;                                      /* 777777 */

    default:
        return 0;
        }
}

/* Trailer read - reads out 18b words in 3b increments
   Checksum is stored to avoid double calculation

   word         lines           contents
   0            0-5             forward checksum (lines 0-1, rest 0)
   1            6-11            0
   2            12-17           0
   3            18-23           reverse block mark
   4            24-29           0

   Note that the reverse block mark (when read forward) appears
   as the complement obverse (3b nibbles swapped end for end and
   complemented).
*/

int32 td_trailer (UNIT *uptr, int32 blk, int32 line)
{
int32 nibp, i, ba;
int16 *fbuf= (int16 *) uptr->filebuf;

switch (line) {

    case 0:
        td_csum = 07777;                                /* init csum */
        ba = blk * DTU_BSIZE (uptr);
        for (i = 0; i < DTU_BSIZE (uptr); i++)          /* loop thru buf */
            td_csum = (td_csum ^ ~fbuf[ba + i]) & 07777;
        td_csum = ((td_csum >> 6) ^ td_csum) & 077;
        return (td_csum >> 3) & 07;

    case 1:
        return (td_csum & 07);

    case 18: case 19: case 20: case 21:
        nibp = 3 * (line % DT_LPERMC);
        return ((blk >> nibp) & 07) ^ 07;

    default:
        return 0;
        }
}

/* Data read - convert block number/data line # to offset in data array */

int32 td_read (UNIT *uptr, int32 blk, int32 line)
{
int16 *fbuf = (int16 *) uptr->filebuf;                  /* buffer */
uint32 ba = blk * DTU_BSIZE (uptr);                     /* block base */
int32 nibp = 3 * (DT_WSIZE - 1 - (line % DT_WSIZE));    /* nibble pos */

ba = ba + (line / DT_WSIZE);                            /* block addr */
return (fbuf[ba] >> nibp) & 07;                         /* get data nibble */
}

/* Data write - convert block number/data line # to offset in data array */

void td_write (UNIT *uptr, int32 blk, int32 line, int32 dat)
{
int16 *fbuf = (int16 *) uptr->filebuf;                  /* buffer */
uint32 ba = blk * DTU_BSIZE (uptr);                     /* block base */
int32 nibp = 3 * (DT_WSIZE - 1 - (line % DT_WSIZE));    /* nibble pos */

ba = ba + (line / DT_WSIZE);                            /* block addr */
fbuf[ba] = (fbuf[ba] & ~(07 << nibp)) | (dat << nibp);  /* upd data nibble */
uptr->WRITTEN = TRUE;
if (ba >= uptr->hwmark)                                 /* upd length */
    uptr->hwmark = ba + 1;
return;
}

/* Reset routine */

t_stat td_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

for (i = 0; i < DT_NUMDR; i++) {                        /* stop all activity */
    uptr = td_dev.units + i;
    if (sim_is_running) {                               /* CAF? */
        if (uptr->STATE >= STA_ACC) {                   /* accel or uts? */
            if (td_setpos (uptr))                       /* update pos */
                continue;
            sim_cancel (uptr);
            sim_activate (uptr, td_dctime);             /* sched decel */
            uptr->STATE = STA_DEC | (uptr->STATE & STA_DIR);
            }
         }
    else {
        sim_cancel (uptr);                              /* sim reset */
        uptr->STATE = 0;  
        uptr->LASTT = sim_grtime ();
        }
    }
td_slf = td_qlf = td_qlctr = 0;                         /* clear state */
td_cmd = td_dat = td_mtk = 0;
td_csum = 0;
return SCPE_OK;
}

/* Bootstrap routine - OS/8 only 

   1) Read reverse until reverse end zone (mark track is complement obverse)
   2) Read forward until mark track code 031.  This is a composite code from
      the last 4b of the forward block number and the first two bits of the
      reverse guard (01 -0110 01- 1010).  There are 16 lines before the first
      data word.
   3) Store data words from 7354 to end of page.  This includes header and
      trailer words.
   4) Continue at location 7400.
*/

#define BOOT_START      07300
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (int16))

static const uint16 boot_rom[] = {
    01312,                      /* ST,  TAD L4MT        ;=2000, reverse */
    04312,                      /*      JMS L4MT        ; rev lk for 022 */
    04312,                      /*      JMS L4MT        ; fwd lk for 031 */
    06773,                      /* DAT, SDSQ            ; wait for 12b */
    05303,                      /*      JMP .-1 */
    06777,                      /*      SDRD            ; read word */
    03726,                      /*      DCA I BUF       ; store */
    02326,                      /*      ISZ BUF         ; incr ptr */
    05303,                      /*      JMP DAT         ; if not 0, cont */
    05732,                      /*      JMP I SCB       ; jump to boot */
    02000,                      /* L4MT,2000            ; overwritten */
    01300,                      /*      TAD ST          ; =1312, go */
    06774,                      /*      SDLC            ; new command */
    06771,                      /* MTK, SDSS            ; wait for mark */
    05315,                      /*      JMP .-1 */
    06776,                      /*      SDRC            ; get mark code */
    00331,                      /*      AND K77         ; mask to 6b */
    01327,                      /* CMP, TAD MCD         ; got target code? */
    07640,                      /*      SZA CLA         ; skip if yes */
    05315,                      /*      JMP MTK         ; wait for mark */
    02321,                      /*      ISZ CMP         ; next target */
    05712,                      /*      JMP I L4MT      ; exit */
    07354,                      /* BUF, 7354            ; loading point */
    07756,                      /* MCD, -22             ; target 1 */
    07747,                      /*      -31             ; target 2 */
    00077,                      /*      77              ; mask */
    07400                       /* SCB, 7400            ; secondary boot */
    };

t_stat td_boot (int32 unitno, DEVICE *dptr)
{
size_t i;

if (unitno)
    return SCPE_ARG;                                    /* only unit 0 */
if (td_dib.dev != DEV_TD8E)
    return STOP_NOTSTD;                                 /* only std devno */
td_unit[unitno].pos = DT_EZLIN;
for (i = 0; i < BOOT_LEN; i++)
    M[BOOT_START + i] = boot_rom[i];
cpu_set_bootpc (BOOT_START);
return SCPE_OK;
}

/* Attach routine

   Determine 12b, 16b, or 18b/36b format
   Allocate buffer
   If 16b or 18b, read 16b or 18b format and convert to 12b in buffer
   If 12b, read data into buffer
   Set up mark track bit array
*/

t_stat td_attach (UNIT *uptr, CONST char *cptr)
{
uint32 pdp18b[D18_NBSIZE];
uint16 pdp11b[D18_NBSIZE], *fbuf;
int32 i, k, mtkpb;
int32 u = uptr - td_dev.units;
t_stat r;
uint32 ba, sz;

r = attach_unit (uptr, cptr);                           /* attach */
if (r != SCPE_OK)                                       /* fail? */
    return r;
if ((sim_switches & SIM_SW_REST) == 0) {                /* not from rest? */
    uptr->flags = (uptr->flags | UNIT_8FMT) & ~UNIT_11FMT;
    if (sim_switches & SWMASK ('F'))                    /* att 18b? */
        uptr->flags = uptr->flags & ~UNIT_8FMT;
    else if (sim_switches & SWMASK ('S'))               /* att 16b? */
        uptr->flags = (uptr->flags | UNIT_11FMT) & ~UNIT_8FMT;
    else if (!(sim_switches & SWMASK ('A')) &&          /* autosize? */
        (sz = sim_fsize (uptr->fileref))) {
        if (sz == D11_FILSIZ)
            uptr->flags = (uptr->flags | UNIT_11FMT) & ~UNIT_8FMT;
        else if (sz > D8_FILSIZ)
            uptr->flags = uptr->flags & ~UNIT_8FMT;
        }
    }
uptr->capac = DTU_CAPAC (uptr);                         /* set capacity */
uptr->filebuf = calloc (uptr->capac, sizeof (int16));
if (uptr->filebuf == NULL) {                            /* can't alloc? */
    detach_unit (uptr);
    return SCPE_MEM;
    }
fbuf = (uint16 *) uptr->filebuf;                        /* file buffer */
sim_printf ("%s%d: ", sim_dname (&td_dev), u);
if (uptr->flags & UNIT_8FMT)
    sim_printf ("12b format");
else if (uptr->flags & UNIT_11FMT)
    sim_printf ("16b format");
else sim_printf ("18b/36b format");
sim_printf (", buffering file in memory\n");
uptr->io_flush = td_flush;
if (uptr->flags & UNIT_8FMT)                            /* 12b? */
    uptr->hwmark = fxread (uptr->filebuf, sizeof (uint16),
            uptr->capac, uptr->fileref);
else {                                                  /* 16b/18b */
    for (ba = 0; ba < uptr->capac; ) {                  /* loop thru file */
        if (uptr->flags & UNIT_11FMT) {
            k = fxread (pdp11b, sizeof (uint16), D18_NBSIZE, uptr->fileref);
            for (i = 0; i < k; i++)
                pdp18b[i] = pdp11b[i];
            }
        else k = fxread (pdp18b, sizeof (uint32), D18_NBSIZE, uptr->fileref);
        if (k == 0)
            break;
        for ( ; k < D18_NBSIZE; k++)
            pdp18b[k] = 0;
        for (k = 0; k < D18_NBSIZE; k = k + 2) {        /* loop thru blk */
            fbuf[ba] = (pdp18b[k] >> 6) & 07777;
            fbuf[ba + 1] = ((pdp18b[k] & 077) << 6) |
                ((pdp18b[k + 1] >> 12) & 077);
            fbuf[ba + 2] = pdp18b[k + 1] & 07777;
            ba = ba + 3;
            }                                           /* end blk loop */
        }                                               /* end file loop */
    uptr->hwmark = ba;
    }                                                   /* end else */
uptr->flags = uptr->flags | UNIT_BUF;                   /* set buf flag */
uptr->pos = DT_EZLIN;                                   /* beyond leader */
uptr->LASTT = sim_grtime ();                            /* last pos update */
uptr->STATE = STA_STOP;                                 /* stopped */

mtkpb = (DTU_BSIZE (uptr) * DT_WSIZE) / DT_LPERMC;      /* mtk codes per blk */
k = td_set_mtk (MTK_INTER, u, 0);                       /* fill mark track */
k = td_set_mtk (MTK_FWD_BLK, u, k);                     /* bit array */
k = td_set_mtk (MTK_REV_GRD, u, k);
for (i = 0; i < 4; i++)
    k = td_set_mtk (MTK_FWD_PRE, u, k);
for (i = 0; i < (mtkpb - 4); i++)
    k = td_set_mtk (MTK_DATA, u, k);
for (i = 0; i < 4; i++)
    k = td_set_mtk (MTK_REV_PRE, u, k);
k = td_set_mtk (MTK_FWD_GRD, u, k);
k = td_set_mtk (MTK_REV_BLK, u, k);
k = td_set_mtk (MTK_INTER, u, k);
return SCPE_OK;
}

/* Detach routine

   If 12b, write buffer to file
   If 16b or 18b, convert 12b buffer to 16b or 18b and write to file
   Deallocate buffer
*/

void td_flush (UNIT* uptr)
{
uint32 pdp18b[D18_NBSIZE];
uint16 pdp11b[D18_NBSIZE], *fbuf;
int32 i, k;
uint32 ba;

if (uptr->WRITTEN && uptr->hwmark && ((uptr->flags & UNIT_RO)== 0)) {    /* any data? */
    sim_printf ("%s: writing buffer to file: %s\n", sim_uname (uptr), uptr->filename);
    rewind (uptr->fileref);                             /* start of file */
    fbuf = (uint16 *) uptr->filebuf;                    /* file buffer */
    if (uptr->flags & UNIT_8FMT)                        /* PDP8? */
        fxwrite (uptr->filebuf, sizeof (uint16),        /* write file */
            uptr->hwmark, uptr->fileref);
    else {                                              /* 16b/18b */
        for (ba = 0; ba < uptr->hwmark; ) {             /* loop thru buf */
            for (k = 0; k < D18_NBSIZE; k = k + 2) {
                pdp18b[k] = ((uint32) (fbuf[ba] & 07777) << 6) |
                    ((uint32) (fbuf[ba + 1] >> 6) & 077);
                pdp18b[k + 1] = ((uint32) (fbuf[ba + 1] & 077) << 12) |
                    ((uint32) (fbuf[ba + 2] & 07777));
                ba = ba + 3;
                }                                       /* end loop blk */
            if (uptr->flags & UNIT_11FMT) {             /* 16b? */
                for (i = 0; i < D18_NBSIZE; i++)
                    pdp11b[i] = pdp18b[i];
                fxwrite (pdp11b, sizeof (uint16),
                    D18_NBSIZE, uptr->fileref);
                }
            else fxwrite (pdp18b, sizeof (uint32),
                D18_NBSIZE, uptr->fileref);
            }                                           /* end loop buf */
        }                                               /* end else */
    if (ferror (uptr->fileref))
        sim_perror ("I/O error");
    }
uptr->WRITTEN = FALSE;                                  /* no longer dirty */
}

t_stat td_detach (UNIT* uptr)
{
int u = (int)(uptr - td_dev.units);

if (!(uptr->flags & UNIT_ATT))
    return SCPE_OK;
if (uptr->hwmark && ((uptr->flags & UNIT_RO)== 0))      /* any data? */
    td_flush (uptr);
free (uptr->filebuf);                                   /* release buf */
uptr->flags = uptr->flags & ~UNIT_BUF;                  /* clear buf flag */
uptr->filebuf = NULL;                                   /* clear buf ptr */
uptr->flags = (uptr->flags | UNIT_8FMT) & ~UNIT_11FMT;  /* default fmt */
uptr->capac = DT_CAPAC;                                 /* default size */
uptr->pos = uptr->STATE = 0;
sim_cancel (uptr);                                      /* no more pulses */
return detach_unit (uptr);
}

/* Set mark track code into bit array */

int32 td_set_mtk (int32 code, int32 u, int32 k)
{
int32 i;

for (i = 5; i >= 0; i--)
    tdb_mtk[u][k++] = (code >> i) & 1;
return k;
}

/* Show position */

t_stat td_show_pos (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if ((uptr->flags & UNIT_ATT) == 0) return SCPE_UNATT;
if (uptr->pos < DT_EZLIN)                               /* rev end zone? */
    fprintf (st, "Reverse end zone\n");
else if (uptr->pos < ((uint32) DTU_FWDEZ (uptr))) {     /* data zone? */
    int32 blkno = DT_LIN2BL (uptr->pos, uptr);          /* block # */
    int32 lineno = DT_LIN2OF (uptr->pos, uptr);         /* line # within block */
    fprintf (st, "Block %d, line %d, ", blkno, lineno);
    if (lineno < DT_HTLIN)                              /* header? */
        fprintf (st, "header cell %d, nibble %d\n",
            lineno / DT_LPERMC, lineno % DT_LPERMC);
    else if (lineno < (DTU_LPERB (uptr) - DT_HTLIN))    /* data? */
        fprintf (st, "data word %d, nibble %d\n",
            (lineno - DT_HTLIN) / DT_WSIZE, (lineno - DT_HTLIN) % DT_WSIZE);
    else fprintf (st, "trailer cell %d, nibble %d\n",
        (lineno - (DTU_LPERB (uptr) - DT_HTLIN)) / DT_LPERMC,
        (lineno - (DTU_LPERB (uptr) - DT_HTLIN)) % DT_LPERMC);
    }
else fprintf (st, "Forward end zone\n");                /* fwd end zone */
return SCPE_OK;
}

const char *td_description (DEVICE *dptr)
{
return "TD8E/TU56 DECtape";
}
