/* pdp10_dtc.c: 18b 551 DECtape simulator

   Copyright (c) 2017 Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell.

   dt           (PDP-4, PDP-7) Type 550/555 DECtape
                (PDP-6) Type 551 Dectape
                (PDP-9) TC02/TU55 DECtape
                (PDP-10) TD10/TU55 DECtape
                (PDP-15) TC15/TU56 DECtape

   18b DECtapes are represented in memory by fixed length buffer of 32b words.
   Three file formats are supported:

        18b/36b                 256 words per block [256 x 18b]
        16b                     256 words per block [256 x 16b]
        12b                     129 words per block [129 x 12b]

   When a 16b or 12b DECtape file is read in, it is converted to 18b/36b format.

   DECtape motion is measured in 3b lines.  Time between lines is 33.33us.
   Tape density is nominally 300 lines per inch.  The format of a DECtape (as
   taken from the PDP-7 formatter) is:

        reverse end zone        7144 reverse end zone codes ~ 12 feet
        reverse buffer          200 interblock codes
        block 0
         :
        block n
        forward buffer          200 interblock codes
        forward end zone        7144 forward end zone codes ~ 12 feet

   A block consists of five 18b header words, a tape-specific number of data
   words, and five 18b trailer words.  All systems except the PDP-8 use a
   standard block length of 256 words; the PDP-8 uses a standard block length
   of 86 words (x 18b = 129 words x 12b).  PDP-4/7 DECtapes came in two
   formats.  The first 5 controllers used a 4 word header/trailer (missing
   word 0/4).  All later serial numbers used the standard header.  The later,
   standard header/trailer is simulated here.

   Because a DECtape file only contains data, the simulator cannot support
   write timing and mark track and can only do a limited implementation
   of read all and write all.  Read all assumes that the tape has been
   conventionally written forward:

        header word 0           0
        header word 1           block number (for forward reads)
        header words 2,3        0
        header word 4           checksum (for reverse reads)
        :
        trailer word 4          checksum (for forward reads)
        trailer words 3,2       0
        trailer word 1          block number (for reverse reads)
        trailer word 0          0

   Write all writes only the data words and dumps the interblock words in the
   bit bucket.

*/

#include "kx10_defs.h"
#ifndef NUM_DEVS_DTC
#define NUM_DEVS_DTC 0
#endif

#if (NUM_DEVS_DTC > 0)
#define DTC_DEVNUM      0210
#define DTC_NUMDR       8                           /* #drives */
#define UNIT_V_WLK      (UNIT_V_UF + 0)             /* write locked */
#define UNIT_V_8FMT     (UNIT_V_UF + 1)             /* 12b format */
#define UNIT_V_11FMT    (UNIT_V_UF + 2)             /* 16b format */
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_8FMT       (1 << UNIT_V_8FMT)
#define UNIT_11FMT      (1 << UNIT_V_11FMT)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)        /* write protect */

/* System independent DECtape constants */

#define DT_LPERMC       6                           /* lines per mark track */
#define DT_BLKWD        1                           /* blk no word in h/t */
#define DT_CSMWD        4                           /* checksum word in h/t */
#define DT_HTWRD        5                           /* header/trailer words */
#define DT_EZLIN        (8192 * DT_LPERMC)          /* end zone length */
#define DT_BFLIN        (200 * DT_LPERMC)           /* buffer length */
#define DT_BLKLN        (DT_BLKWD * DT_LPERMC)      /* blk no line in h/t */
#define DT_CSMLN        (DT_CSMWD * DT_LPERMC)      /* csum line in h/t */
#define DT_HTLIN        (DT_HTWRD * DT_LPERMC)      /* header/trailer lines */

/* 16b, 18b, 36b DECtape constants */

#define D18_WSIZE       6                           /* word size in lines */
#define D18_BSIZE       256                         /* block size in 18b */
#define D18_TSIZE       578                         /* tape size */
#define D18_LPERB       (DT_HTLIN + (D18_BSIZE * DT_WSIZE) + DT_HTLIN)
#define D18_FWDEZ       (DT_EZLIN + (D18_LPERB * D18_TSIZE))
#define D18_CAPAC       (D18_TSIZE * D18_BSIZE)     /* tape capacity */
#define D11_FILSIZ      (D18_CAPAC * sizeof (int16))

/* 12b DECtape constants */

#define D8_WSIZE        4                           /* word size in lines */
#define D8_BSIZE        86                          /* block size in 18b */
#define D8_TSIZE        1474                        /* tape size */
#define D8_LPERB        (DT_HTLIN + (D8_BSIZE * DT_WSIZE) + DT_HTLIN)
#define D8_FWDEZ        (DT_EZLIN + (D8_LPERB * D8_TSIZE))
#define D8_CAPAC        (D8_TSIZE * D8_BSIZE)       /* tape capacity */

#define D8_NBSIZE       ((D8_BSIZE * D18_WSIZE) / D8_WSIZE)
#define D8_FILSIZ       (D8_NBSIZE * D8_TSIZE * sizeof (int16))

/* This controller */

#define DT_CAPAC        D18_CAPAC                   /* default */
#define DT_WSIZE        D18_WSIZE

/* Calculated constants, per unit */

#define DTU_BSIZE(u)    (((u)->flags & UNIT_8FMT)? D8_BSIZE: D18_BSIZE)
#define DTU_TSIZE(u)    (((u)->flags & UNIT_8FMT)? D8_TSIZE: D18_TSIZE)
#define DTU_LPERB(u)    (((u)->flags & UNIT_8FMT)? D8_LPERB: D18_LPERB)
#define DTU_FWDEZ(u)    (((u)->flags & UNIT_8FMT)? D8_FWDEZ: D18_FWDEZ)
#define DTU_CAPAC(u)    (((u)->flags & UNIT_8FMT)? D8_CAPAC: D18_CAPAC)

#define DT_LIN2BL(p,u)  (((p) - DT_EZLIN) / DTU_LPERB (u))
#define DT_LIN2OF(p,u)  (((p) - DT_EZLIN) % DTU_LPERB (u))
#define DT_LIN2WD(p,u)  ((DT_LIN2OF (p,u) - DT_HTLIN) / DT_WSIZE)
#define DT_BLK2LN(p,u)  (((p) * DTU_LPERB (u)) + DT_EZLIN)
#define DT_QREZ(u)      (((u)->pos) < DT_EZLIN)
#define DT_QFEZ(u)      (((u)->pos) >= ((uint32) DTU_FWDEZ (u)))
#define DT_QEZ(u)       (DT_QREZ (u) || DT_QFEZ (u))

/* Command register, status A */
#define CMD     u3
#define DTC_FLAG_PIA    07                  /* PI Channel */
#define DTC_V_UNIT      3                   /* unit select */
#define DTC_M_UNIT      07
#define DTC_V_FNC       6
#define DTC_M_FNC       07
#define  FNC_MOVE        00                 /* move */
#define  FNC_RALL        01                 /* read all */
#define  FNC_SRCH        02                 /* search */
#define  FNC_READ        03                 /* read */
#define  FNC_WMRK        04                 /* write timing */
#define  FNC_WALL        05                 /* write All */
#define  FNC_WBLK        06                 /* Write Block */
#define  FNC_WRIT        07                 /* write data */
#define DTC_DELAY       0003000             /* Initial delay time */
#define DTC_TIME        0004000             /* Delay */
#define DTC_RVDRV       0010000             /* Move unit reverse */
#define DTC_START       0020000             /* Start unit */
#define DTC_JDONE       0040000             /* Enable Job done */
#define DTC_ETF         0100000             /* Enable End of tape flag */
#define DTC_SEL         0200000             /* Select unit */

/* Flags in lower bits of u3 (unit position) */
#define DTC_FNC_STOP    010                 /* Unit stopping */
#define DTC_FNC_START   DTC_START           /* Start unit motion */
#define DTC_FNC_REV     DTC_RVDRV           /* Unit to change direction */

#define DTC_GETFNC(x)   (((x) >> DTC_V_FNC) & DTC_M_FNC)
#define DTC_GETUNI(x)   (((x) >> DTC_V_UNIT) & DTC_M_UNIT)


/* Status register B */
#define DTB_DONE        0000001
#define DTB_EOT         0000002
#define DTB_ILL         0000004
#define DTB_PAR         0000010
#define DTB_TIME        0000020
#define DTB_WR          0000040
#define DTB_INCBLK      0000100
#define DTB_NULL        0000200
#define DTB_ACT         0000400
#define DTB_REQ         0001000
#define DTB_DLY         0002000

#define DSTATE   u5              /* Dectape current state */
/* Current Dectape state in u5 */
#define DTC_FEND        0                    /* Tape in endzone */
#define DTC_FBLK        1                    /* In forward block number */
#define DTC_FCHK        2                    /* In forward checksum */
#define DTC_BLOCK       3                    /* In block */
#define DTC_RCHK        4                    /* In reverse checksum */
#define DTC_RBLK        5                    /* In reverse block number */
#define DTC_REND        7                    /* In final endzone */

#define DTC_MOTMASK     0370
#define DTC_MOT         0010                 /* Tape in motion */
#define DTC_REV         0020                 /* Tape in reverse */
#define DTC_XFR         0040                 /* Tranfer block */
#define DTC_STOP        0100                 /* Tape to stop */
#define DTC_ACCL        0200                 /* Tape accel or decl */

#define DTC_V_WORD      8                    /* Shift for word count */
#define DTC_M_WORD      0177                 /* 128 words per block */
#define DTC_V_BLK       16                   /* Shift for Block number */
#define DTC_M_BLK       01777                /* Block mask */

#define DELAY     u4             /* Hold delay time in DT WORDS */
/* Logging */

#define LOG_MS          00200                 /* move, search */
#define LOG_RW          00400                 /* read, write */
#define LOG_RA          01000                 /* read all */
#define LOG_BL          02000                 /* block # lblk */

#define ABS(x)          (((x) < 0)? (-(x)): (x))

#define DT_WRDTIM       15000

int32 dtc_dtsa = 0;                           /* status A */
int32 dtc_dtsb = 0;                           /* status B */
int   dtc_dct = 0;

t_stat         dtc_devio(uint32 dev, uint64 *data);
t_stat         dtc_svc (UNIT *uptr);
t_stat         dtc_boot(int32 unit_num, DEVICE * dptr);
t_stat         dtc_set_dct (UNIT *, int32, CONST char *, void *);
t_stat         dtc_show_dct (FILE *, UNIT *, int32, CONST void *);
t_stat         dtc_reset (DEVICE *dptr);
t_stat         dtc_attach (UNIT *uptr, CONST char *cptr);
t_stat         dtc_detach (UNIT *uptr);

/* DT data structures

   dtc_dev       DTC device descriptor
   dtc_unit      DTC unit list
   dtc_reg       DTC register list
   dtc_mod       DTC modifier list
*/


#if !PDP6
#define D DEV_DIS
#else
#define D 0
#endif

DIB dtc_dib = { DTC_DEVNUM, 2, &dtc_devio, NULL};

UNIT dtc_unit[] = {
    { UDATA (&dtc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dtc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dtc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dtc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dtc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dtc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dtc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dtc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) }
    };

REG dtc_reg[] = {
    { ORDATA (DTSA, dtc_dtsa, 18) },
    { URDATA (POS, dtc_unit[0].pos, 10, T_ADDR_W, 0,
              DTC_NUMDR, PV_LEFT | REG_RO) },
    { NULL }
    };

MTAB dtc_mod[] = {
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
    { UNIT_8FMT + UNIT_11FMT, 0, "18b", NULL, NULL },
    { UNIT_8FMT + UNIT_11FMT, UNIT_8FMT, "12b", NULL, NULL },
    { UNIT_8FMT + UNIT_11FMT, UNIT_11FMT, "16b", NULL, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "DCT", "DCT",
       &dtc_set_dct, &dtc_show_dct, NULL},
    { 0 }
    };

DEBTAB dtc_deb[] = {
    { "CMD", DEBUG_CMD, "Show command execution to devices"},
    { "DATA", DEBUG_DATA, "Show data transfers"},
    { "DETAIL", DEBUG_DETAIL, "Show details about device"},
    { "EXP", DEBUG_EXP, "Show exception information"},
    { "CONI", DEBUG_CONI, "Show coni instructions"},
    { "CONO", DEBUG_CONO, "Show coni instructions"},
    { "DATAIO", DEBUG_DATAIO, "Show datai and datao instructions"},
    { "MOTION", LOG_MS },
    { "DATA", LOG_RW },
    { "READALL", LOG_RA },
    { "BLOCK", LOG_BL },
    { NULL, 0 }
    };

DEVICE dtc_dev = {
    "DTC", dtc_unit, dtc_reg, dtc_mod,
    DTC_NUMDR, 8, 24, 1, 8, 18,
    NULL, NULL, &dtc_reset, &dtc_boot, &dtc_attach, &dtc_detach,
    &dtc_dib, DEV_DISABLE | DEV_DEBUG | D, 0,
    dtc_deb, NULL, NULL
    };

int delay[] = { 0, 50, 100, 500 };

/* IOT routines */
t_stat
dtc_devio(uint32 dev, uint64 *data) {
     int      i;

     switch(dev & 07) {
     case CONI:
          *data = (uint64)dtc_dtsa;
          sim_debug(DEBUG_CONI, &dtc_dev, "DTC %03o CONI %06o PC=%o\n",
               dev, (uint32)*data, PC);
          break;

     case CONO:
          clr_interrupt(dev);
          /* Copy over command and priority */
          dtc_dtsa = *data & (DTC_FLAG_PIA|(DTC_M_FNC << DTC_V_FNC)|DTC_TIME|DTC_RVDRV| \
                         DTC_START|DTC_JDONE|DTC_ETF|DTC_SEL);
          dtc_dtsb = 0;
          sim_debug(DEBUG_CONO, &dtc_dev, "DTC %03o CONO %06o PC=%o\n",
               dev, (uint32)*data, PC);
          i = DTC_GETUNI(*data);
#if DTC_NUMDR < 8
          if (i >= DTC_NUMDR) {
              dtc_dtsb |= DTB_ILL;
              set_interrupt(DTC_DEVNUM, dtc_dtsa);
              return SCPE_OK;
          }
#endif
          if (*data & DTC_DELAY) {
              dtc_dtsb |= DTB_DLY;
              dtc_unit[i].DELAY = delay[(*data >> 9) & 3];
          }

          /* Check if we are selecting a drive or not */
          if (*data & DTC_SEL) {
              if ((dtc_unit[i].flags & UNIT_ATT) == 0) {
                  dtc_dtsb |= DTB_ILL;
                  set_interrupt(DTC_DEVNUM, dtc_dtsa);
                  return SCPE_OK;
              }
              if ((*data & DTC_START) != 0) {
                   /* Start the unit if not already running */
                   dtc_unit[i].CMD = (dtc_dtsa & 0377707);
                   if ((dtc_unit[i].DSTATE & DTC_MOT) == 0) {
                       if (!sim_is_active(&dtc_unit[i])) {
                          sim_activate(&dtc_unit[i], 10000);
                       }
                   }
              } else {
                   dtc_unit[i].CMD |= DTC_FNC_STOP;
              }
              dtc_dtsb |= DTB_REQ;
          } else {
              /* If not selecting, but delaying, give it to a unit to handle */
              if (dtc_dtsb & DTB_DLY) {
                   dtc_unit[i].CMD = (dtc_dtsa & 0007007);
                   if ((dtc_unit[i].DSTATE & DTC_MOT) == 0) {
                       if (!sim_is_active(&dtc_unit[i])) {
                          sim_activate(&dtc_unit[i], 10000);
                       }
                   }
              }
              /* Not selecting any, stop all */
              for (i = 0; i < DTC_NUMDR; i++)
                  dtc_unit[i].CMD = DTC_FNC_STOP;
              dtc_dtsb |= DTB_NULL;
          }
          break;

     case DATAI:
          break;

     case DATAO:
          break;

     case CONI|04:
          *data = dtc_dtsb;
          sim_debug(DEBUG_CONI, &dtc_dev, "DTB %03o CONI %012llo PC=%o\n",
               dev, *data, PC);
          break;

     case CONO|04:
          break;

     case DATAI|4:
          break;
     case DATAO|4:
          break;

     }
     return SCPE_OK;
}

/* Unit service

   Unit must be attached, detach cancels operation
*/

t_stat
dtc_svc (UNIT *uptr)
{
   int        word;
   uint64     data = 0;
   uint32     *fbuf = (uint32 *) uptr->filebuf;         /* file buffer */
   int        u = uptr-dtc_unit;
   int        blk;
   int        off;
    /*
     * Check if in motion or stopping.
     */
    if (uptr->DSTATE & DTC_MOT) {
        /* Check if stoping */
        if ((uptr->CMD & DTC_FNC_STOP) != 0) {
            /* Stop delay */
            sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o stopping\n", u);
            sim_activate(uptr, DT_WRDTIM*10);
            uptr->DSTATE &= ~(DTC_MOT);
            blk = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
            uptr->DSTATE = (0100 << DTC_V_WORD) | DTC_BLOCK | (uptr->DSTATE & DTC_MOTMASK);
            if (uptr->DSTATE & DTC_REV) {
                if (blk <= 0) {
                    blk = 0;
                    uptr->DSTATE = DTC_FEND | (uptr->DSTATE & DTC_MOTMASK);
                } else {
                    blk--;
                }
            } else {
                if (blk <= 01100)
                    blk++;
            }
            dtc_dtsb |= DTB_DONE;
            if ((uptr->CMD & DTC_JDONE) != 0)
                set_interrupt(DTC_DEVNUM, dtc_dtsa);

            /* If we were delaying, all done, notify CPU if it asked to know */
            if ((dtc_dtsb & DTB_DLY) != 0) {
                uptr->DELAY = 0;
                dtc_dtsb |= DTB_TIME;
                dtc_dtsb &= ~DTB_DLY;
                if(uptr->CMD & DTC_TIME)
                   set_interrupt(DTC_DEVNUM, dtc_dtsa);
            }

            dtc_dtsb &= ~(DTB_REQ|DTB_ACT);
            dtc_dtsb |= DTB_NULL;
            uptr->CMD &= 077077;
            uptr->DSTATE |= (blk << DTC_V_BLK);
            return SCPE_OK;
         }

         /* Set tape to move in correct direction */
         if (uptr->CMD & DTC_RVDRV) {
             if ((uptr->DSTATE & DTC_REV) == 0) {
                 sim_activate(uptr, DT_WRDTIM*10);
                 sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o reversing\n", u);
                 uptr->DSTATE |= DTC_REV;
                 uptr->DELAY -= 10;
                 return SCPE_OK;
             }
         } else {
             if ((uptr->DSTATE & DTC_REV) != 0) {
                 sim_activate(uptr, DT_WRDTIM*10);
                 sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o reversing\n", u);
                 uptr->DSTATE &= ~DTC_REV;
                 uptr->DELAY -= 10;
                 return SCPE_OK;
             }
         }

         /* Moving in reverse direction */
         if (uptr->DSTATE & DTC_REV) {
             switch (uptr->DSTATE & 7) {
             case DTC_FEND:                           /* Tape in endzone */
                  /* Set stop */
                  sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o rev forward end\n", u);
                  uptr->u6 = 0;
                  dtc_dtsb |= DTB_EOT|DTB_NULL;
                  dtc_dtsb &= ~(DTB_REQ|DTB_ACT);
                  if (uptr->CMD & DTC_ETF)
                      set_interrupt(DTC_DEVNUM, dtc_dtsa);
                  uptr->CMD |= DTC_FNC_STOP;
                  sim_activate(uptr, DT_WRDTIM*10);
                  if ((dtc_dtsb & DTB_DLY) != 0) {
                      uptr->DELAY = 0;
                      dtc_dtsb &= ~DTB_DLY;
                      dtc_dtsb |= DTB_TIME;
                      if (uptr->CMD & DTC_TIME)
                          set_interrupt(DTC_DEVNUM, dtc_dtsa);
                  }
                  break;

             case DTC_FBLK:                           /* In forward block number */
                  sim_activate(uptr,DT_WRDTIM);
                  uptr->DELAY --;
                  word = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
                  word--;
                  if (word <= 0)
                      uptr->DSTATE = DTC_FEND | (uptr->DSTATE & DTC_MOTMASK);
                  else
                      uptr->DSTATE = DTC_RBLK|(word << DTC_V_BLK) | (uptr->DSTATE & DTC_MOTMASK);
                  sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o rev forward block\n", u);
                  switch (DTC_GETFNC(uptr->CMD)) {
                  case FNC_RALL:
                       if (dtc_dtsb & DTB_ACT) {
                           if (dct_write(dtc_dct, &data, 6) == 0)
                               dtc_dtsb |= DTB_DONE;
                       }
                       break;
                  case FNC_READ:
                  case FNC_WRIT:
                       if (dtc_dtsb & DTB_REQ) {
                           dtc_dtsb &= ~DTB_REQ;
                           dtc_dtsb |= DTB_ACT;
                       }
                       break;
                  case FNC_SRCH:
                  case FNC_MOVE:
                       break;
                  case FNC_WALL:
                  case FNC_WBLK:
                       (void)dct_read(dtc_dct, &data, 6);
                       break;
                  case FNC_WMRK:
                       dtc_dtsb |= DTB_ILL;
                       break;
                  }
                  break;

             case DTC_FCHK:                           /* In forward checksum */
                  sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o rev forward check\n", u);
                  sim_activate(uptr,DT_WRDTIM*2);
                  /* Disconnect if DCT no longer attached */
                  if ((dtc_dtsb & DTB_ACT) != 0 && dct_is_connect(dtc_dct) == 0)
                      dtc_dtsb |= DTB_DONE;
                  uptr->DELAY -= 2;
                  word = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
                  uptr->DSTATE = DTC_FBLK|(word << DTC_V_BLK) | (uptr->DSTATE & DTC_MOTMASK);
                  if ((dtc_dtsb & DTB_DLY) != 0) {
                      if (uptr->DELAY < 0) {
                          dtc_dtsb &= ~DTB_DLY;
                          dtc_dtsb |= DTB_TIME;
                          if (uptr->CMD & DTC_TIME)
                              set_interrupt(DTC_DEVNUM, dtc_dtsa);
                      }
                      break;
                  }
                  switch (DTC_GETFNC(uptr->CMD)) {
                  case FNC_RALL:
                       if (dtc_dtsb & DTB_ACT) {
                           blk = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
                           if (blk < 075)
                               data = 0721200220107LL;
                           else if (blk > 075)
                               data = 0721200233107LL;
                           else
                               data = 0577777777777LL;
                           if (dct_write(dtc_dct, &data, 6) == 0)
                              dtc_dtsb |= DTB_DONE;
                       }
                       break;
                  case FNC_WMRK:
                       dtc_dtsb |= DTB_ILL;
                       break;
                  case FNC_SRCH:
                  case FNC_WRIT:
                  case FNC_WALL:
                  case FNC_READ:
                  case FNC_WBLK:
                  case FNC_MOVE:
                       break;
                  }
                  break;

             case DTC_BLOCK:                          /* In block */
                  uptr->DELAY --;
                  sim_activate(uptr,DT_WRDTIM);
                  blk = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
                  word = (uptr->DSTATE >> DTC_V_WORD) & DTC_M_WORD;
                  off = ((blk << 7) + word) << 1;
                  /* Check if at end of block */
                  if (word == 0) {
                      uptr->DSTATE &= ~((DTC_M_WORD << DTC_V_WORD) | 7);
                      uptr->DSTATE |= DTC_FCHK;           /* Move to Checksum */
                  } else {
                      uptr->DSTATE &= ~(DTC_M_WORD << DTC_V_WORD);
                      uptr->DSTATE |= (word - 1) << DTC_V_WORD;
                  }
                  uptr->u6-=2;
                  if ((dtc_dtsb & DTB_DLY) || (dtc_dtsb & DTB_ACT) == 0)
                      break;
                  switch (DTC_GETFNC(uptr->CMD)) {
                  case FNC_MOVE:
                  case FNC_SRCH:
                  case FNC_WBLK:
                       break;
                  case FNC_WMRK:
                       dtc_dtsb |= DTB_ILL;
                       break;
                  case FNC_RALL:
                  case FNC_READ:
                       data = ((uint64)fbuf[off]) << 18;
                       data |= ((uint64)fbuf[off+1]);
                       if (dct_write(dtc_dct, &data, 6) == 0) {
                           dtc_dtsb &= ~DTB_ACT;
                           dtc_dtsb |= DTB_INCBLK|DTB_DONE;
                       }
                       break;

                  case FNC_WRIT:
                  case FNC_WALL:
                       if (dct_read(dtc_dct, &data, 6) == 0) {
                           dtc_dtsb &= ~DTB_ACT;
                           dtc_dtsb |= DTB_INCBLK|DTB_DONE;
                       }
                       fbuf[off] = (data >> 18) & RMASK;
                       fbuf[off+1] = data & RMASK;
                       uptr->hwmark = uptr->capac;
                       break;
                  }
                  sim_debug(DEBUG_DETAIL, &dtc_dev,
                             "DTC %o rev data word %o:%o %012llo %d %06o %06o\n",
                                u, blk, word, data, off, fbuf[off], fbuf[off+1]);
                  break;

             case DTC_RCHK:                           /* In reverse checksum */
                  sim_activate(uptr,DT_WRDTIM*2);
                  uptr->DELAY -= 2;
                  sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o rev reverse check %06o %06o\n",
                              u, uptr->CMD, dtc_dtsb);
                  word = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
                  uptr->DSTATE = DTC_BLOCK|(word << DTC_V_BLK)|(DTC_M_WORD << DTC_V_WORD) |
                                (uptr->DSTATE & DTC_MOTMASK);
                  if ((dtc_dtsb & DTB_ACT) != 0 && dct_is_connect(dtc_dct) == 0)
                      dtc_dtsb |= DTB_DONE;
                  if ((dtc_dtsb & DTB_DLY) != 0) {
                      if (uptr->DELAY < 0) {
                          dtc_dtsb &= ~DTB_DLY;
                          dtc_dtsb |= DTB_TIME;
                          if (uptr->CMD & DTC_TIME)
                              set_interrupt(DTC_DEVNUM, dtc_dtsa);
                      }
                      break;
                  }
                  switch (DTC_GETFNC(uptr->CMD)) {
                  case FNC_WRIT:
                  case FNC_WALL:
                  case FNC_SRCH:
                  case FNC_RALL:
                  case FNC_MOVE:
                  case FNC_READ:
                  case FNC_WBLK:
                       if (dtc_dtsb & DTB_REQ) {
                           dtc_dtsb |= DTB_ACT;
                           dtc_dtsb &= ~(DTB_REQ|DTB_NULL);
                       }
                       break;
                  case FNC_WMRK:
                       dtc_dtsb |= DTB_ILL;
                       break;
                  }
                  break;

             case DTC_RBLK:                           /* In reverse block number */
                  sim_activate(uptr,DT_WRDTIM*2);
                  uptr->DELAY -= 2;
                  word = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
                  data = (uint64)word;
                  uptr->DSTATE = DTC_RCHK|(word << DTC_V_BLK)|(DTC_M_WORD << DTC_V_WORD) |
                           (uptr->DSTATE & DTC_MOTMASK);
                  sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o rev reverse block %04o\n", u, word);
                  dtc_dtsb &= ~DTB_EOT;
                  if ((dtc_dtsb & DTB_DLY) != 0) {
                      if (uptr->DELAY < 0) {
                          dtc_dtsb &= ~DTB_DLY;
                          dtc_dtsb |= DTB_TIME;
                          if (uptr->CMD & DTC_TIME)
                              set_interrupt(DTC_DEVNUM, dtc_dtsa);
                      }
                      break;
                  }
                  switch (DTC_GETFNC(uptr->CMD)) {
                  case FNC_MOVE:
                  case FNC_READ:
                  case FNC_WMRK:
                  case FNC_WRIT:
                       break;
                  case FNC_RALL:
                       if (dtc_dtsb & DTB_ACT && dct_write(dtc_dct, &data, 6) == 0)
                           dtc_dtsb |= DTB_DONE;
                       break;
                  case FNC_SRCH:
                       if (dtc_dtsb & DTB_ACT) {
                           (void)dct_write(dtc_dct, &data, 6);
                           dtc_dtsb |= DTB_DONE;
                       }
                       break;
                  case FNC_WALL:
                  case FNC_WBLK:
                       if (dtc_dtsb & DTB_ACT) {
                           (void)dct_read(dtc_dct, &data, 6);
                           dtc_dtsb |= DTB_DONE;
                       }
                       break;
                  }
                  if (dtc_dtsb & DTB_REQ) {
                      sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o activate\n", u);
                      dtc_dtsb &= ~(DTB_REQ|DTB_NULL);
                      dtc_dtsb |= DTB_ACT;
                  }
                  break;

             case DTC_REND:                           /* In final endzone */
                  sim_activate(uptr, DT_WRDTIM*10);
                  uptr->DELAY -= 10;
                  word = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
                  word--;
                  uptr->DSTATE = DTC_RBLK|(word << DTC_V_BLK) | (uptr->DSTATE & DTC_MOTMASK);
                  dtc_dtsb &= ~DTB_EOT;
                  break;
             }
         } else {
         /* Moving in forward direction */
             switch (uptr->DSTATE & 7) {
             case DTC_FEND:                           /* Tape in endzone */
                  sim_activate(uptr, DT_WRDTIM*10);
                  uptr->DELAY -= 10;
                  sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o forward end\n", u);
                  /* Move to first block */
                  uptr->DSTATE = DTC_FBLK | (uptr->DSTATE & DTC_MOTMASK);
                  uptr->u6 = 0;
                  break;

             case DTC_FBLK:                           /* In forward block number */
                  uptr->DELAY -= 2;
                  sim_activate(uptr,DT_WRDTIM*2);
                  dtc_dtsb &= ~DTB_EOT;
                  word = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
                  uptr->DSTATE = DTC_FCHK|(word << DTC_V_BLK) | (uptr->DSTATE & DTC_MOTMASK);
                  sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o forward block %04o %06o\n",
                             u, word, dtc_dtsb);
                  data = (uint64)word;
                  if ((dtc_dtsb & DTB_DLY) != 0) {
                      if (uptr->DELAY < 0) {
                          dtc_dtsb &= ~DTB_DLY;
                          dtc_dtsb |= DTB_TIME;
                          if (uptr->CMD & DTC_TIME)
                              set_interrupt(DTC_DEVNUM, dtc_dtsa);
                      }
                      break;
                  }
                  if ((dtc_dtsb & DTB_ACT) != 0 && dct_is_connect(dtc_dct) == 0)
                      dtc_dtsb |= DTB_DONE;
                  switch (DTC_GETFNC(uptr->CMD)) {
                  case FNC_SRCH:
                       if (dtc_dtsb & DTB_ACT) {
                           (void)dct_write(dtc_dct, &data, 6);
                           dtc_dtsb |= DTB_DONE;
                       }
                       break;
                  case FNC_RALL:
                       if ((dtc_dtsb & DTB_ACT) != 0 && dct_write(dtc_dct, &data, 6) == 0)
                           dtc_dtsb |= DTB_DONE;
                       break;
                  case FNC_READ:
                  case FNC_WRIT:
                       if (dtc_dtsb & DTB_REQ) {
                           dtc_dtsb &= ~(DTB_REQ|DTB_NULL);
                           dtc_dtsb |= DTB_ACT;
                       }
                       break;
                  case FNC_MOVE:
                       break;
                  case FNC_WALL:
                  case FNC_WBLK:
                       (void)dct_read(dtc_dct, &data, 6);
                       break;
                  case FNC_WMRK:
                       dtc_dtsb |= DTB_ILL;
                       break;
                  }
                  break;

             case DTC_FCHK:                           /* In forward checksum */
                  uptr->DELAY -= 2;
                  sim_activate(uptr,DT_WRDTIM*2);
                  sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o forward check %06o\n", u, dtc_dtsb);
                  uptr->DSTATE &= ~7;
                  uptr->DSTATE |= DTC_BLOCK;              /* Move to datablock */
                  if ((dtc_dtsb & DTB_ACT) != 0 && dct_is_connect(dtc_dct) == 0)
                      dtc_dtsb |= DTB_DONE;
                  if ((dtc_dtsb & DTB_DLY) != 0) {
                      if (uptr->DELAY < 0) {
                          dtc_dtsb &= ~DTB_DLY;
                          dtc_dtsb |= DTB_TIME;
                          if (uptr->CMD & DTC_TIME)
                              set_interrupt(DTC_DEVNUM, dtc_dtsa);
                      }
                      break;
                  }
                  switch (DTC_GETFNC(uptr->CMD)) {
                  case FNC_RALL:
                       if (dtc_dtsb & DTB_ACT) {
                           blk = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
                           if (blk < 075)
                               data = 0721200220107LL;
                           else if (blk > 075)
                               data = 0721200233107LL;
                           else
                               data = 0577777777777LL;
                           if (dct_write(dtc_dct, &data, 6) == 0)
                              dtc_dtsb |= DTB_DONE;
                       }
                       break;
                  case FNC_WMRK:
                       dtc_dtsb |= DTB_ILL;
                       break;
                  case FNC_SRCH:
                  case FNC_WRIT:
                  case FNC_WALL:
                  case FNC_READ:
                  case FNC_WBLK:
                       if (dtc_dtsb & DTB_REQ) {
                           dtc_dtsb &= ~(DTB_REQ|DTB_NULL);
                           dtc_dtsb |= DTB_ACT;
                       }
                       break;
                  case FNC_MOVE:
                       break;
                  }
                  break;

             case DTC_BLOCK:                          /* In block */
                  uptr->DELAY --;
                  sim_activate(uptr,DT_WRDTIM);
                  blk = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
                  word = (uptr->DSTATE >> DTC_V_WORD) & DTC_M_WORD;
                  off = ((blk << 7) + word) << 1;
                  /* Check if at end of block */
                  if (word == DTC_M_WORD) {
                      uptr->DSTATE &= ~7;
                      uptr->DSTATE |= DTC_RCHK;           /* Move to checksum */
                  } else {
                      uptr->DSTATE &= ~(DTC_M_WORD << DTC_V_WORD);
                      uptr->DSTATE |= (word + 1) << DTC_V_WORD;
                  }
                  if (dtc_dtsb & DTB_DLY)
                      break;
                  if ((dtc_dtsb & DTB_ACT) == 0)
                      break;
                  switch (DTC_GETFNC(uptr->CMD)) {
                  case FNC_MOVE:
                  case FNC_SRCH:
                  case FNC_WALL:
                  case FNC_WBLK:
                       break;
                  case FNC_RALL:
                  case FNC_READ:
                       data = ((uint64)fbuf[off]) << 18;
                       data |= (uint64)fbuf[off+1];
                       if (dct_write(dtc_dct, &data, 6) == 0)
                          dtc_dtsb |= DTB_DONE;
                       break;
                  case FNC_WRIT:
                       if (dct_read(dtc_dct, &data, 6) == 0)
                          dtc_dtsb |= DTB_DONE;
                       fbuf[off] = (data >> 18) & RMASK;
                       fbuf[off+1] = data & RMASK;
                       uptr->hwmark = uptr->capac;
                       break;
                  case FNC_WMRK:
                       dtc_dtsb |= DTB_ILL;
                       break;
                  }
                  sim_debug(DEBUG_DETAIL, &dtc_dev,
                            "DTC %o data word %o:%o %012llo %d %06o %06o\n",
                             u, blk, word, data, off, fbuf[off], fbuf[off+1]);
                  break;

             case DTC_RCHK:                           /* In reverse checksum */
                  uptr->DELAY -=2;
                  sim_activate(uptr,DT_WRDTIM*2);
                  sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o reverse check\n", u);
                  uptr->DSTATE &= ~(DTC_M_WORD << DTC_V_WORD) | 7;
                  uptr->DSTATE |= DTC_RBLK;               /* Move to end of block */
                  if ((dtc_dtsb & DTB_ACT) != 0 && dct_is_connect(dtc_dct) == 0)
                      dtc_dtsb |= DTB_DONE;
                  if ((dtc_dtsb & DTB_DLY) != 0) {
                      if (uptr->DELAY < 0) {
                          dtc_dtsb &= ~DTB_DLY;
                          dtc_dtsb |= DTB_TIME;
                          if (uptr->CMD & DTC_TIME)
                              set_interrupt(DTC_DEVNUM, dtc_dtsa);
                      }
                      break;
                  }
                  switch (DTC_GETFNC(uptr->CMD)) {
                  case FNC_RALL:
                       if (dtc_dtsb & DTB_ACT) {
                           blk = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
                           if (blk < 073)
                               data = 0721200220107LL;
                           else
                               data = 0721200233107LL;
                           if (dct_write(dtc_dct, &data, 6) == 0)
                               dtc_dtsb |= DTB_DONE;
                       }
                       break;
                  case FNC_WMRK:
                       dtc_dtsb |= DTB_ILL;
                       break;
                  case FNC_SRCH:
                  case FNC_WRIT:
                  case FNC_WALL:
                  case FNC_READ:
                  case FNC_WBLK:
                  case FNC_MOVE:
                       break;
                  }
                  break;

             case DTC_RBLK:                           /* In reverse block number */
                  uptr->DELAY -=2;
                  sim_activate(uptr,DT_WRDTIM*2);
                  word = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
                  word++;
                  if (word > 01101) {
                       uptr->DSTATE = DTC_REND|(word << DTC_V_BLK)|(DTC_M_WORD << DTC_V_WORD) |
                              (uptr->DSTATE & DTC_MOTMASK);
                  } else {
                       uptr->DSTATE = DTC_FBLK|(word << DTC_V_BLK)|(uptr->DSTATE & DTC_MOTMASK);
                  }
                  sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o reverse block %o\n", u, word);
                  if ((dtc_dtsb & DTB_DLY) != 0) {
                      if (uptr->DELAY < 0) {
                          dtc_dtsb &= ~DTB_DLY;
                          dtc_dtsb |= DTB_TIME;
                          if (uptr->CMD & DTC_TIME)
                              set_interrupt(DTC_DEVNUM, dtc_dtsa);
                      }
                      break;
                  }
                  /* Check if DCC has disconnected */
                  if ((dtc_dtsb & DTB_ACT) != 0 && dct_is_connect(dtc_dct) == 0)
                      dtc_dtsb |= DTB_DONE;
                  /* Check if request pending */
                  if (dtc_dtsb & DTB_REQ) {
                      dtc_dtsb &= ~(DTB_REQ|DTB_NULL);
                      dtc_dtsb |= DTB_ACT;
                  }
                  switch (DTC_GETFNC(uptr->CMD)) {
                  case FNC_RALL:
                       if ((dtc_dtsb & DTB_ACT) != 0 && dct_write(dtc_dct, &data, 6) == 0)
                           dtc_dtsb |= DTB_DONE;
                       break;
                  case FNC_SRCH:
                  case FNC_MOVE:
                  case FNC_READ:
                  case FNC_WRIT:
                  case FNC_WALL:
                  case FNC_WBLK:
                       break;
                  case FNC_WMRK:
                       dtc_dtsb |= DTB_ILL;
                       set_interrupt(DTC_DEVNUM, dtc_dtsa);
                       break;
                  }
                  if (dtc_dtsb & DTB_REQ) {
                      dtc_dtsb &= ~(DTB_REQ|DTB_NULL);
                      dtc_dtsb |= DTB_ACT;
                  }
                  break;

             case DTC_REND:                           /* In final endzone */
                  /* Set stop */
                  uptr->CMD |= DTC_FNC_STOP;
                  sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o reverse end\n", u);
                  dtc_dtsb |= DTB_EOT;
                  if (dtc_dtsa & DTC_ETF)
                      set_interrupt(DTC_DEVNUM, dtc_dtsa);
                  if ((dtc_dtsb & DTB_DLY) != 0) {
                      dtc_dtsb &= ~DTB_DLY;
                      dtc_dtsb |= DTB_TIME;
                      if (uptr->CMD & DTC_TIME)
                          set_interrupt(DTC_DEVNUM, dtc_dtsa);
                  }
                  sim_activate(uptr, DT_WRDTIM*10);
                  break;
             }
         }

         if ((dtc_dtsb & DTB_DONE) != 0) {
             if ((uptr->CMD & DTC_JDONE) != 0) {
                 set_interrupt(DTC_DEVNUM, dtc_dtsa);
                 sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o post done\n", u);
             }

             dtc_dtsb &= ~(DTB_REQ|DTB_ACT);
             dtc_dtsb |= DTB_NULL;
             uptr->CMD &= 077077;
         }

         if ((dtc_dtsb & (DTB_ILL|DTB_PAR|DTB_EOT)) != 0) {
             set_interrupt(DTC_DEVNUM, dtc_dtsa);
             uptr->CMD = DTC_FNC_STOP;
         }

    /* Check if starting */
    } else if (uptr->CMD & DTC_START) {
        /* Start up delay */
        sim_activate(uptr, DT_WRDTIM*10);
        if ((dtc_dtsb & DTB_DLY) != 0) {
            uptr->DELAY = 0;
            dtc_dtsb |= DTB_TIME;
            dtc_dtsb &= ~DTB_DLY;
            if(uptr->CMD & DTC_TIME)
               set_interrupt(DTC_DEVNUM, dtc_dtsa);
        }

        uptr->DSTATE |=  DTC_MOT;
        if (uptr->CMD & DTC_RVDRV) {
            uptr->DSTATE |= DTC_REV;
        } else {
            uptr->DSTATE &= ~DTC_REV;
        }
        sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o start %06o\n", u, uptr->CMD);
        return SCPE_OK;
    } else if ((dtc_dtsb & DTB_DLY) != 0) {
        uptr->DELAY = 0;
        dtc_dtsb |= DTB_TIME;
        dtc_dtsb &= ~DTB_DLY;
        if(dtc_dtsa & DTC_TIME)
           set_interrupt(DTC_DEVNUM, dtc_dtsa);
        sim_debug(DEBUG_DETAIL, &dtc_dev, "DTC %o delay over %06o\n", u, dtc_dtsa);
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
dtc_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    uint32             *fbuf = (uint32 *) uptr->filebuf;    /* file buffer */
    uint64              word;
    int                 off;
    int                 wc, addr;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    off = 0;
    wc = fbuf[off++]+1;
    addr = fbuf[off++];
    while (wc != 0777777) {
        wc = (wc + 1) & RMASK;
        addr = (addr + 1) & RMASK;
        word = ((uint64)fbuf[off++]) << 18;
        word |= (uint64)fbuf[off++];
        if (addr < 020)
           FM[addr] = word;
        else
           M[addr] = word;
    }
    if (addr < 020)
       FM[addr] = word;
    else
       M[addr] = word;
    uptr->DSTATE = (1 << DTC_V_BLK) | DTC_BLOCK | DTC_MOT;
    sim_activate(uptr,30000);
    PC = word & RMASK;
    return SCPE_OK;
}

/* set DCT channel and unit. */
t_stat
dtc_set_dct (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 dct;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    dct = (int32) get_uint (cptr, 8, 20, &r);
    if (r != SCPE_OK)
        return r;
    dtc_dct = dct;
    return SCPE_OK;
}

t_stat
dtc_show_dct (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   if (uptr == NULL)
      return SCPE_IERR;

   fprintf (st, "DCT=%02o", dtc_dct);
   return SCPE_OK;
}

/* Reset routine */

t_stat
dtc_reset (DEVICE *dptr)
{
    int   i;

    dtc_dtsb = dtc_dtsa = 0;                                    /* clear status */
    for (i = 0; i < DTC_NUMDR; i++) {
       if ((dtc_unit[i].DSTATE & DTC_MOT) != 0)
           dtc_unit[i].CMD |= DTC_FNC_STOP;
    }
    clr_interrupt(DTC_DEVNUM);
    clr_interrupt(DTC_DEVNUM|4);
    return SCPE_OK;
}

/* Attach routine

   Determine 12b, 16b, or 18b/36b format
   Allocate buffer
   If 12b, read 12b format and convert to 18b in buffer
   If 16b, read 16b format and convert to 18b in buffer
   If 18b/36b, read data into buffer
*/

t_stat
dtc_attach (UNIT *uptr, CONST char *cptr)
{
    uint16 pdp8b[D8_NBSIZE];
    uint16 pdp11b[D18_BSIZE];
    uint32 ba, sz, k, *fbuf;
    int32 u = uptr - dtc_dev.units;
    t_stat r;
    
    r = attach_unit (uptr, cptr);                           /* attach */
    if (r != SCPE_OK)                                       /* error? */
        return r;
    if ((sim_switches & SIM_SW_REST) == 0) {                /* not from rest? */
        uptr->flags = uptr->flags & ~(UNIT_8FMT | UNIT_11FMT);      /* default 18b */
        if (sim_switches & SWMASK ('T'))                    /* att 12b? */
            uptr->flags = uptr->flags | UNIT_8FMT;
        else if (sim_switches & SWMASK ('S'))               /* att 16b? */
            uptr->flags = uptr->flags | UNIT_11FMT;
        else if (!(sim_switches & SWMASK ('A')) &&          /* autosize? */
            (sz = sim_fsize (uptr->fileref))) {
            if (sz == D8_FILSIZ)
                uptr->flags = uptr->flags | UNIT_8FMT;
            else if (sz == D11_FILSIZ)
                uptr->flags = uptr->flags | UNIT_11FMT;
            }
        }
    uptr->capac = DTU_CAPAC (uptr);                         /* set capacity */
    uptr->filebuf = calloc (uptr->capac, sizeof (uint32));
    if (uptr->filebuf == NULL) {                            /* can't alloc? */
        detach_unit (uptr);
        return SCPE_MEM;
        }
    fbuf = (uint32 *) uptr->filebuf;                        /* file buffer */
    printf ("%s%d: ", sim_dname (&dtc_dev), u);
    if (uptr->flags & UNIT_8FMT)
        printf ("12b format");
    else if (uptr->flags & UNIT_11FMT)
        printf ("16b format");
    else printf ("18b/36b format");
    printf (", buffering file in memory\n");
    if (uptr->flags & UNIT_8FMT) {                          /* 12b? */
        for (ba = 0; ba < uptr->capac; ) {                  /* loop thru file */
            k = fxread (pdp8b, sizeof (uint16), D8_NBSIZE, uptr->fileref);
            if (k == 0)
                break;
            for ( ; k < D8_NBSIZE; k++)
                pdp8b[k] = 0;
            for (k = 0; k < D8_NBSIZE; k = k + 3) {         /* loop thru blk */
                fbuf[ba] = ((uint32) (pdp8b[k] & 07777) << 6) |
                    ((uint32) (pdp8b[k + 1] >> 6) & 077);
                fbuf[ba + 1] = ((uint32) (pdp8b[k + 1] & 077) << 12) |
                    ((uint32) pdp8b[k + 2] & 07777);
                ba = ba + 2;                                /* end blk loop */
                }
            }                                               /* end file loop */
        uptr->hwmark = ba;
            }                                               /* end if */
    else if (uptr->flags & UNIT_11FMT) {                    /* 16b? */
        for (ba = 0; ba < uptr->capac; ) {                  /* loop thru file */
            k = fxread (pdp11b, sizeof (uint16), D18_BSIZE, uptr->fileref);
            if (k == 0)
                break;
            for ( ; k < D18_BSIZE; k++)
                pdp11b[k] = 0;
            for (k = 0; k < D18_BSIZE; k++)
                fbuf[ba++] = pdp11b[k];
                }
        uptr->hwmark = ba;
        }                                                   /* end elif */
    else uptr->hwmark = fxread (uptr->filebuf, sizeof (uint32),
        uptr->capac, uptr->fileref);
    uptr->flags = uptr->flags | UNIT_BUF;                   /* set buf flag */
    uptr->pos = DT_EZLIN;                                   /* beyond leader */
    return SCPE_OK;
}

/* Detach routine

   Cancel in progress operation
   If 12b, convert 18b buffer to 12b and write to file
   If 16b, convert 18b buffer to 16b and write to file
   If 18b/36b, write buffer to file
   Deallocate buffer
*/

t_stat
dtc_detach (UNIT* uptr)
{
    uint16 pdp8b[D8_NBSIZE];
    uint16 pdp11b[D18_BSIZE];
    uint32 ba, k, *fbuf;
    int32 u = uptr - dtc_dev.units;
    
    if (!(uptr->flags & UNIT_ATT))
        return SCPE_OK;
    if (sim_is_active (uptr)) {
        sim_cancel (uptr);
        uptr->CMD = uptr->pos = 0;
        }
    fbuf = (uint32 *) uptr->filebuf;                        /* file buffer */
    if (uptr->hwmark && ((uptr->flags & UNIT_RO) == 0)) {   /* any data? */
        printf ("%s%d: writing buffer to file\n", sim_dname (&dtc_dev), u);
        rewind (uptr->fileref);                             /* start of file */
        if (uptr->flags & UNIT_8FMT) {                      /* 12b? */
            for (ba = 0; ba < uptr->hwmark; ) {             /* loop thru file */
                for (k = 0; k < D8_NBSIZE; k = k + 3) {     /* loop blk */
                    pdp8b[k] = (fbuf[ba] >> 6) & 07777;
                    pdp8b[k + 1] = ((fbuf[ba] & 077) << 6) |
                        ((fbuf[ba + 1] >> 12) & 077);
                    pdp8b[k + 2] = fbuf[ba + 1] & 07777;
                    ba = ba + 2;
                                    }                                                /* end loop blk */
                fxwrite (pdp8b, sizeof (uint16), D8_NBSIZE, uptr->fileref);
                if (ferror (uptr->fileref))
                    break;
                }                                           /* end loop file */
            }                                               /* end if 12b */
        else if (uptr->flags & UNIT_11FMT) {                /* 16b? */
            for (ba = 0; ba < uptr->hwmark; ) {             /* loop thru file */
                for (k = 0; k < D18_BSIZE; k++)             /* loop blk */
                    pdp11b[k] = fbuf[ba++] & 0177777;
                fxwrite (pdp11b, sizeof (uint16), D18_BSIZE, uptr->fileref);
                if (ferror (uptr->fileref))
                    break;
                }                                           /* end loop file */
            }                                               /* end if 16b */
        else fxwrite (uptr->filebuf, sizeof (uint32),       /* write file */
            uptr->hwmark, uptr->fileref);
        if (ferror (uptr->fileref))
            perror ("I/O error");
        }                                                   /* end if hwmark */
    free (uptr->filebuf);                                   /* release buf */
    uptr->flags = uptr->flags & ~UNIT_BUF;                  /* clear buf flag */
    uptr->filebuf = NULL;                                   /* clear buf ptr */
    uptr->flags = uptr->flags & ~(UNIT_8FMT | UNIT_11FMT);  /* default fmt */
    uptr->capac = DT_CAPAC;                                 /* default size */
    return detach_unit (uptr);
}
#endif
