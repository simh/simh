/* kx10_dt.c: 18b DECtape simulator

   Copyright (c) 2017-2020 Richard Cornwell based on work by Bob Supnik

   Based on PDP18B/pdp18b_dt.c by:
   Copyright (c) 1993-2017, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell.

   dt           (PDP-4, PDP-7) Type 550/555 DECtape
                (PDP-9) TC02/TU55 DECtape
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
#ifndef NUM_DEVS_DT
#define NUM_DEVS_DT 0
#endif

#if (NUM_DEVS_DT > 0)
#define DT_DEVNUM       0320
#define DT_NUMDR        8                           /* #drives */
#define UNIT_V_8FMT     (UNIT_V_UF + 0)             /* 12b format */
#define UNIT_V_11FMT    (UNIT_V_UF + 1)             /* 16b format */
#define UNIT_8FMT       (1 << UNIT_V_8FMT)
#define UNIT_11FMT      (1 << UNIT_V_11FMT)

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

/* Status register A */
#define DTC_FLAG_PIA    07                  /* PI Channel */
#define DTC_DATA_PIA    070                 /* PI Channel */
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
#define DTC_V_UNIT      9                   /* unit select */
#define DTC_M_UNIT      07
#define DTC_DESEL       0010000             /* Deslect all units */
#define DTC_SEL         0020000             /* Select unit */
#define DTC_NODELAY     0040000             /* Don't delay */
#define DTC_RVDRV       0100000             /* Move unit reverse */
#define DTC_FWDRV       0200000             /* Move unit forward */
#define DTC_STSTOP      0400000             /* Stop unit */

#define CMD            u3
/* Flags in lower bits of u3 */
#define DTC_FNC_STOP    001                 /* Unit stopping */
#define DTC_FNC_START   002                 /* Start unit motion */
#define DTC_FNC_REV     004                 /* Unit to change direction */

/* CONO Unit +4 bit */
#define DTS_FUNC_STOP   0000001
#define DTS_STOP_ALL    0000002
#define DTS_BLK_MISS    0010000
#define DTS_END_ZONE    0020000
#define DTS_ILL_OP      0040000
#define DTS_JOB_DONE    0100000
#define DTS_DATA_MISS   0200000
#define DTS_PAR_ERR     0400000

#define DTC_GETFNC(x)   (((x) >> DTC_V_FNC) & DTC_M_FNC)
#define DTC_GETUNI(x)   (((x) >> DTC_V_UNIT) & DTC_M_UNIT)


/* Status register B */
#define DTB_PARENB      0400000000000LL      /* Parity Error Enable */
#define DTB_TIMENB      0200000000000LL      /* Data missed Enable */
#define DTB_JOBENB      0100000000000LL      /* Job done Enable */
#define DTB_ILLENB      0040000000000LL      /* Illegal Operation Enable */
#define DTB_ENDENB      0020000000000LL      /* End Zone Enable */
#define DTB_MISENB      0010000000000LL      /* Block Missed Enable */
#define DTB_DLY         0004000000000LL      /* Delay in progress */
#define DTB_ACT         0002000000000LL      /* Active */
#define DTB_SPD         0001000000000LL      /* Controller up to speed */
#define DTB_BLK         0000400000000LL      /* Block number */
#define DTB_REV         0000200000000LL      /* Reverse Check */
#define DTB_DAT         0000100000000LL      /* Data */
#define DTB_FIN         0000040000000LL      /* Final */
#define DTB_CHK         0000020000000LL      /* Checksum */
#define DTB_IDL         0000010000000LL      /* Idle */
#define DTB_BLKRD       0000004000000LL      /* Block Number Read */
#define DTB_STOP        0000001000000LL      /* Function Stop */
#define DTB_PAR         0000000400000LL      /* Parity Error */
#define DTB_MIS         0000000200000LL      /* Data Missed */
#define DTB_DONE        0000000100000LL      /* Job Done */
#define DTB_ILL         0000000040000LL      /* Illegal operation */
#define DTB_END         0000000020000LL      /* End Zone */
#define DTB_BLKMIS      0000000010000LL      /* Block Missed */
#define DTB_WRLK        0000000004000LL      /* Write Lock */
#define DTB_WRMK        0000000002000LL      /* Write Mark Switch */
#define DTB_INCBLK      0000000001000LL      /* Incomplete Block */
#define DTB_MRKERR      0000000000200LL      /* Mark Track Error */
#define DTB_SELERR      0000000000100LL      /* Select error */
#define DTB_FLGREQ      0000000000002LL      /* Flag Request */
#define DTB_DATREQ      0000000000001LL      /* Data Request */

#define DSTATE   u5              /* Dectape current state */
/* Current Dectape state in u5 */
#define DTC_FEND        0                    /* Tape in endzone */
#define DTC_FBLK        1                    /* In forward block number */
#define DTC_FCHK        2                    /* In forward checksum */
#define DTC_BLOCK       3                    /* In block */
#define DTC_RCHK        4                    /* In reverse checksum */
#define DTC_RBLK        5                    /* In reverse block number */
#define DTC_REND        7                    /* In final endzone */

#define DTC_MOTMASK     0170
#define DTC_MOT         0010                 /* Tape in motion */
#define DTC_REV         0020                 /* Tape in reverse */
#define DTC_STOP        0040                 /* Tape to stop */
#define DTC_ACCL        0100                 /* Tape accel or decl */

#define DTC_V_WORD      8                    /* Shift for word count */
#define DTC_M_WORD      0177                 /* 128 words per block */
#define DTC_V_BLK       16                   /* Shift for Block number */
#define DTC_M_BLK       01777                /* Block mask */

/* Logging */

#define LOG_MS          00200                           /* move, search */
#define LOG_RW          00400                           /* read, write */
#define LOG_RA          01000                           /* read all */
#define LOG_BL          02000                           /* block # lblk */

#define ABS(x)          (((x) < 0)? (-(x)): (x))

#define DT_WRDTIM       10000

#define WRITTEN       u6          /* Set when tape modified */

int32 dtsa = 0;                                         /* status A */
uint64 dtsb = 0;                                        /* status B */
uint64 dtdb = 0;                                        /* data buffer */
int dt_mpx_lvl;

t_stat         dt_devio(uint32 dev, uint64 *data);
t_stat         dt_svc (UNIT *uptr);
t_stat         dt_boot(int32 unit_num, DEVICE * dptr);
t_stat         dt_reset (DEVICE *dptr);
t_stat         dt_attach (UNIT *uptr, CONST char *cptr);
void           dt_flush (UNIT *uptr);
t_stat         dt_detach (UNIT *uptr);
#if MPX_DEV
t_stat         dt_set_mpx (UNIT *uptr, int32 val, CONST char *cptr, void *desc) ;
t_stat         dt_show_mpx (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
#endif


/* DT data structures

   dt_dev       DT device descriptor
   dt_unit      DT unit list
   dt_reg       DT register list
   dt_mod       DT modifier list
*/

DIB dt_dib = { DT_DEVNUM, 2, &dt_devio, NULL};

UNIT dt_unit[] = {
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) }
    };

REG dt_reg[] = {
    { ORDATA (DTSA, dtsa, 18) },
    { ORDATA (DTSB, dtsb, 18) },
    { ORDATA (DTDB, dtdb, 18) },
    { ORDATA (MPX, dt_mpx_lvl, 3) },
    { URDATA (POS, dt_unit[0].pos, 10, T_ADDR_W, 0,
              DT_NUMDR, PV_LEFT | REG_RO | REG_UNIT) },
    { NULL }
    };

MTAB dt_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write enable drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "Write lock drive" },
    { UNIT_8FMT + UNIT_11FMT, 0, "18b", NULL, NULL },
    { UNIT_8FMT + UNIT_11FMT, UNIT_8FMT, "12b", NULL, NULL },
    { UNIT_8FMT + UNIT_11FMT, UNIT_11FMT, "16b", NULL, NULL },
#if MPX_DEV
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "MPX", "MPX",
     &dt_set_mpx, &dt_show_mpx, NULL},
#endif
    { 0 }
    };

DEBTAB dt_deb[] = {
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

DEVICE dt_dev = {
    "DT", dt_unit, dt_reg, dt_mod,
    DT_NUMDR, 8, 24, 1, 8, 18,
    NULL, NULL, &dt_reset, &dt_boot, &dt_attach, &dt_detach,
    &dt_dib, DEV_DISABLE | DEV_DEBUG, 0,
    dt_deb, NULL, NULL
    };

/* IOT routines */
t_stat dt_devio(uint32 dev, uint64 *data) {
     int      i;

     switch(dev & 07) {
     case CONI:
          *data = (uint64)dtsa;
          sim_debug(DEBUG_CONI, &dt_dev, "DTA %03o CONI %06o PC=%o\n",
               dev, (uint32)*data, PC);
          break;

     case CONO:
          clr_interrupt(dev);
          clr_interrupt(dev|4);
          /* Copy over command and priority */
          dtsa &= ~0777;
          dtsa |= (*data & 0777);
          dtsb = 0;
          sim_debug(DEBUG_CONO, &dt_dev, "DTA %03o CONO %06o PC=%o\n",
               dev, (uint32)*data, PC);
          /* Check bits in command register */
          if (*data & DTC_DESEL) {
              /* Stop all drives and clear drive unit */
              dtsa &= 0770777;
              for (i = 0; i < DT_NUMDR; i++) {
                 dt_unit[i].CMD &= ~0700;
              }
              if ((*data & DTC_SEL) == 0)
                 break;
          }
          if (*data & DTC_SEL) {
              dtsa |= *data & 07000;
              i = DTC_GETUNI(dtsa);
              if ((dt_unit[i].flags & UNIT_ATT) == 0) {
                  dtsb |= DTB_ILL|DTB_SELERR;
                  dtsb &= ~DTB_IDL;
                  if (dtsb & DTB_ILLENB)
                     set_interrupt(DT_DEVNUM, dtsa);
                  return SCPE_OK;
              }
              if (i < DT_NUMDR && !sim_is_active(&dt_unit[i]))
                 sim_activate(&dt_unit[i], 1000);
              if (dt_unit[i].DSTATE & DTC_MOT) {
                  switch (dt_unit[i].DSTATE & 7) {
                  case DTC_FEND:                           /* Tape in endzone */
                  case DTC_REND:                           /* In final endzone */
                       dtsb |= DTB_END|DTB_IDL;
                       break;

                  case DTC_FBLK:                           /* In forward block number */
                  case DTC_RBLK:                           /* In reverse block number */
                       dtsb |= DTB_BLK|DTB_IDL;
                       break;

                  case DTC_RCHK:                           /* In reverse checksum */
                  case DTC_FCHK:                           /* In forward checksum */
                       dtsb |= DTB_CHK|DTB_IDL;
                       break;

                  case DTC_BLOCK:                          /* In block */
                       dtsb |= DTB_DAT;
                       break;
                  }
              } else {
                  dtsb |= DTB_IDL;
              }
          }
          if (*data & (DTC_FWDRV|DTC_RVDRV|DTC_STSTOP)) {
              i = DTC_GETUNI(dtsa);
#if DT_NUMDR < 8
              if (i >= DT_NUMDR)
                  break;
#endif
              if ((dt_unit[i].flags & UNIT_ATT) == 0) {
                  dtsb |= DTB_ILL;
                  dtsb &= ~DTB_IDL;
                  if (dtsb & DTB_ILLENB)
                     set_interrupt(DT_DEVNUM, dtsa);
                  return SCPE_OK;
              }
              if (*data & DTC_STSTOP) {
                   if ((dt_unit[i].DSTATE & (DTC_MOT)) != 0) {
                       dt_unit[i].CMD |= DTC_FNC_STOP;
                   }
                   dtsa &=~ (DTC_FWDRV|DTC_RVDRV);
              } else {
                   /* Start the unit if not already running */
                   dt_unit[i].CMD &= ~DTC_FNC_STOP;
                   if ((dt_unit[i].DSTATE & (DTC_MOT)) == 0) {
                       dt_unit[i].CMD |= DTC_FNC_START;
                       dtsb |= DTB_DLY;
                       if (!sim_is_active(&dt_unit[i]))
                          sim_activate(&dt_unit[i], 10000);
                   }
                   dtsa &=~ (DTC_FWDRV|DTC_RVDRV);
                   switch(*data & (DTC_FWDRV|DTC_RVDRV)) {
                   case DTC_FWDRV:
                          if (dt_unit[i].DSTATE & DTC_REV) {
                              dt_unit[i].CMD |= DTC_FNC_REV;
                              dtsa |= (DTC_RVDRV);
                          } else
                              dtsa |= (DTC_FWDRV);
                          break;
                   case DTC_RVDRV:
                          if ((dt_unit[i].DSTATE & DTC_REV) == 0) {
                              dt_unit[i].CMD |= DTC_FNC_REV;
                              dtsa |= (DTC_RVDRV);
                          } else
                              dtsa |= (DTC_FWDRV);
                          break;
                   case DTC_FWDRV|DTC_RVDRV:
                          dt_unit[i].CMD |= DTC_FNC_REV;
                          if ((dt_unit[i].DSTATE & DTC_REV) == 0)
                              dtsa |= (DTC_RVDRV);
                          else
                              dtsa |= (DTC_FWDRV);
                          break;
                   }
              }
          }
          break;

     case DATAI:
          *data = dtdb;
          dtsb &= ~DTB_DATREQ;
          clr_interrupt(dev|4);
          sim_debug(DEBUG_DATAIO, &dt_dev, "DTA %03o DATI %012llo PC=%06o\n",
                    dev, *data, PC);

          break;

     case DATAO:
          dtdb = *data;
          dtsb &= ~DTB_DATREQ;
          clr_interrupt(dev|4);
          sim_debug(DEBUG_DATAIO, &dt_dev, "DTA %03o DATO %012llo PC=%06o\n",
                    dev, *data, PC);
          break;

     case CONI|04:
          *data = dtsb;
          if (dtsb & 0770000) 
             *data |= DTB_FLGREQ;
          sim_debug(DEBUG_CONI, &dt_dev, "DTB %03o CONI %012llo PC=%o\n",
               dev, *data, PC);
          break;

     case CONO|04:
          dtsb = 0;
          clr_interrupt(dev);
          clr_interrupt(dev|4);
          if (*data & DTS_STOP_ALL) {
              /* Stop all other drives */
              for (i = 0; i < DT_NUMDR; i++) {
                 if (i != DTC_GETUNI(dtsa) &&
                    (dt_unit[i].DSTATE & DTC_MOT) != 0)
                     dt_unit[i].CMD |= DTC_FNC_STOP;
              }
          }
          dtsb = (uint64)((*data & (DTS_PAR_ERR|DTS_DATA_MISS|DTS_JOB_DONE| \
                     DTS_ILL_OP|DTS_END_ZONE|DTS_BLK_MISS)) << 18);
          if (*data & DTS_FUNC_STOP)
              dtsb |= DTB_STOP;

          sim_debug(DEBUG_CONO, &dt_dev, "DTB %03o CONO %06o PC=%o DTSB=%012llo\n",
               dev, (uint32)*data, PC, dtsb);
          break;

     case DATAI|4:
          sim_debug(DEBUG_DATAIO, &dt_dev, "DTB %03o DATI %012llo PC=%06o\n",
                    dev, *data, PC);
          break;
     case DATAO|4:
          sim_debug(DEBUG_DATAIO, &dt_dev, "DTB %03o DATO %012llo PC=%06o\n",
                    dev, *data, PC);
          break;

     }
     return SCPE_OK;
}

void dt_getword(uint64 *data, int req) {
    int dev = dt_dib.dev_num;
    clr_interrupt(dev|4);
    if (dtsb & DTB_DATREQ) {
        dtsb |= DTB_MIS;
        return;
    }
    *data = dtdb;
    if (req) {
        dtsb |= DTB_DATREQ;
        set_interrupt_mpx(dev|4, dtsa >> 3, dt_mpx_lvl);
    }
}

void dt_putword(uint64 *data) {
    int dev = dt_dib.dev_num;
    clr_interrupt(dev|4);
    if (dtsb & DTB_DATREQ) {
        dtsb |= DTB_MIS;
        return;
    }
    dtdb = *data;
    dtsb |= DTB_DATREQ;
    set_interrupt_mpx(dev|4, dtsa >> 3, dt_mpx_lvl);
}

/* Unit service

   Unit must be attached, detach cancels operation
*/

t_stat dt_svc (UNIT *uptr)
{
   int        word;
   uint64     data = 0;
   uint32     *fbuf = (uint32 *) uptr->filebuf;         /* file buffer */
   int        u = uptr-dt_unit;
   int        blk;
   int        off;
/*
 * Check if in motion or stopping.
 */
if (uptr->DSTATE & DTC_MOT) {
   /* Check if stoping */
   if (uptr->CMD & DTC_FNC_STOP) {
      /* Stop delay */
      sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o stopping\n", u);
      sim_activate(uptr, DT_WRDTIM*10);
      uptr->CMD &= ~DTC_FNC_STOP;
      uptr->DSTATE &= ~(DTC_MOT);
      blk = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
      uptr->DSTATE = (0100 << DTC_V_WORD) | DTC_BLOCK | (DTC_MOTMASK & uptr->DSTATE);
      if (uptr->DSTATE & DTC_REV) {
         if (blk <= 0) {
             blk = 0;
             uptr->DSTATE = DTC_FEND | (DTC_MOTMASK & uptr->DSTATE);
         } else {
            blk--;
         }
      } else {
         if (blk <= 01100)
            blk++;
      }
      uptr->DSTATE |= (blk << DTC_V_BLK);
      return SCPE_OK;
   }
   if (uptr->CMD & DTC_FNC_REV) {
       sim_activate(uptr, DT_WRDTIM*10);
       sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o reversing\n", u);
       uptr->CMD &= ~DTC_FNC_REV;
       uptr->DSTATE ^= DTC_REV;
       return SCPE_OK;
   }

   if (DTC_GETUNI(dtsa) == u)  {
       dtsb |= DTB_SPD;
       dtsb &= ~(DTB_DLY|DTB_IDL);
   }
  /* Moving in reverse direction */
  if (uptr->DSTATE & DTC_REV) {
      if (DTC_GETUNI(dtsa) == u)  {
          dtsb |= DTB_REV;
          dtsa &=~ DTC_FWDRV;
          dtsa |= DTC_RVDRV;
      }
      switch (uptr->DSTATE & 7) {
      case DTC_FEND:                           /* Tape in endzone */
           /* Set stop */
           sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o rev forward end\n", u);
           uptr->CMD |= DTC_FNC_STOP;
           uptr->u6 = 0;
           dtsb |= DTB_END;
           dtsb &= ~DTB_IDL;
           if (dtsb & DTB_ENDENB)
              set_interrupt(DT_DEVNUM, dtsa);
           sim_activate(uptr, DT_WRDTIM*10);
           break;

      case DTC_FBLK:                           /* In forward block number */
           sim_activate(uptr,DT_WRDTIM);
           word = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
           word--;
           if (word == 0)
               uptr->DSTATE = DTC_FEND | (DTC_MOTMASK & uptr->DSTATE);
           else
               uptr->DSTATE = DTC_RBLK|(word << DTC_V_BLK) | (DTC_MOTMASK & uptr->DSTATE);
           dtsb &= ~(DTB_CHK);
           dtsb |= DTB_IDL;
           if (dtsb & DTB_STOP)
               dtsa &= ~0700;          /* Clear command */
           sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o rev forward block\n", u);
           switch (DTC_GETFNC(uptr->CMD)) {
           case FNC_MOVE:
           case FNC_SRCH:
           case FNC_WBLK:
                if ((dtsb & DTB_STOP) == 0)
                    break;
                /* Fall through */
           case FNC_WALL:
           case FNC_RALL:
           case FNC_WRIT:
           case FNC_READ:
                uptr->CMD &= 077077;
                dtsb |= DTB_DONE;
                if (dtsb & DTB_JOBENB)
                   set_interrupt(DT_DEVNUM, dtsa);
                sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o rev stop\n", u);
                dtsb &= ~DTB_STOP;
                break;
           case FNC_WMRK:
                dtsb |= DTS_ILL_OP;
                if (dtsb & DTB_ILLENB)
                   set_interrupt(DT_DEVNUM, dtsa);
                break;
           }
           if (dtsb & (DTB_PAR|DTB_MIS|DTB_ILL|DTB_END|DTB_INCBLK|DTB_MRKERR)) {
                uptr->CMD |= DTC_FNC_STOP;
           }
           if (DTC_GETUNI(dtsa) == u)  {
               uptr->CMD &= 077077;
               uptr->CMD |= dtsa & 0700;          /* Copy command */
           }
           if (word <= 0) {
                uptr->DSTATE = DTC_FEND | (DTC_MOTMASK & uptr->DSTATE);
           }
           break;

      case DTC_FCHK:                           /* In forward checksum */
           sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o rev forward check\n", u);
           sim_activate(uptr,DT_WRDTIM*2);
           word = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
           uptr->DSTATE = DTC_FBLK|(word << DTC_V_BLK) | (DTC_MOTMASK & uptr->DSTATE);
           dtsb &= ~(DTB_DAT|DTB_FIN);
           dtsb |= DTB_CHK;
           break;

      case DTC_BLOCK:                          /* In block */
           sim_activate(uptr,DT_WRDTIM);
           dtsb |= DTB_DAT;
           blk = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
           word = (uptr->DSTATE >> DTC_V_WORD) & DTC_M_WORD;
           off = ((blk << 7) + word) << 1;
           /* Check if at end of block */
           if (word == 0) {
               uptr->DSTATE &= ~((DTC_M_WORD << DTC_V_WORD) | 7);
               uptr->DSTATE |= DTC_FCHK;           /* Move to Checksum */
               dtsb &= ~DTB_DAT;
               dtsb |= DTB_FIN;
           } else {
               uptr->DSTATE &= ~(DTC_M_WORD << DTC_V_WORD);
               uptr->DSTATE |= (word - 1) << DTC_V_WORD;
           }
           uptr->u6-=2;
           switch (DTC_GETFNC(uptr->CMD)) {
           case FNC_MOVE:
           case FNC_SRCH:
           case FNC_WBLK:
                break;
           case FNC_WMRK:
                dtsb |= DTS_ILL_OP;
                if (dtsb & DTB_ILLENB)
                   set_interrupt(DT_DEVNUM, dtsa);
                break;
           case FNC_RALL:
           case FNC_READ:
                data = ((uint64)fbuf[off]) << 18;
                data |= ((uint64)fbuf[off+1]);
                if ((dtsb & DTB_STOP) == 0)
                    dt_putword(&data);
                break;

           case FNC_WRIT:
           case FNC_WALL:
                if ((dtsb & DTB_STOP) == 0)
                    dt_getword(&data, (word != 0));
                else
                    data = dtdb;
                fbuf[off] = (data >> 18) & RMASK;
                fbuf[off+1] = data & RMASK;
                uptr->WRITTEN = 1;
                uptr->hwmark = uptr->capac;
                break;
           }
           if (word == 0) {
               dtsb &= ~DTB_DAT;
               dtsb |= DTB_FIN;
           }
           sim_debug(DEBUG_DETAIL, &dt_dev,
                      "DTA %o rev data word %o:%o %012llo %d %06o %06o\n",
                         u, blk, word, data, off, fbuf[off], fbuf[off+1]);
           break;

      case DTC_RCHK:                           /* In reverse checksum */
           sim_activate(uptr,DT_WRDTIM*2);
           sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o rev reverse check\n", u);
           word = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
           uptr->DSTATE = DTC_BLOCK|(word << DTC_V_BLK)|(DTC_M_WORD << DTC_V_WORD) | 
                                (DTC_MOTMASK & uptr->DSTATE);
           if (dtsb & DTB_STOP)
               dtsa &= ~0700;          /* Clear command */
           if (DTC_GETUNI(dtsa) == u)  {
               uptr->CMD &= 077077;
               uptr->CMD |= dtsa & 0700;        /* Copy command */
           }
           dtsb &= ~DTB_BLKRD;
           switch (DTC_GETFNC(uptr->CMD)) {
           case FNC_WRIT:
           case FNC_WALL:
               dtsb |= DTB_DATREQ;
               set_interrupt_mpx(DT_DEVNUM|4, dtsa >> 3, dt_mpx_lvl);
               break;
           case FNC_RALL:
           case FNC_MOVE:
           case FNC_READ:
           case FNC_WBLK:
               break;
           case FNC_SRCH:
               dtsb |= DTB_DONE;
               dtsb &= ~DTB_STOP;
               if (dtsb & DTB_JOBENB)
                  set_interrupt(DT_DEVNUM, dtsa);
               break;
           case FNC_WMRK:
               dtsb |= DTS_ILL_OP;
               if (dtsb & DTB_ILLENB)
                   set_interrupt(DT_DEVNUM, dtsa);
               break;
           }
           if (dtsb & (DTB_PAR|DTB_MIS|DTB_ILL|DTB_END|DTB_INCBLK|DTB_MRKERR)) {
                uptr->CMD |= DTC_FNC_STOP;
           }
           break;

      case DTC_RBLK:                           /* In reverse block number */
           sim_activate(uptr,DT_WRDTIM*2);
           word = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
           data = (uint64)word;
           uptr->DSTATE = DTC_RCHK|(word << DTC_V_BLK)|(DTC_M_WORD << DTC_V_WORD) |
                                 (DTC_MOTMASK & uptr->DSTATE);
           sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o rev reverse block %04o\n", u, word);
           dtsb &= ~DTB_END;
           dtsb |= DTB_BLKRD;
           if (DTC_GETUNI(dtsa) == u)  {
               uptr->CMD &= 077077;
               uptr->CMD |= dtsa & 0700;        /* Copy command */
           }
           switch (DTC_GETFNC(uptr->CMD)) {
           case FNC_MOVE:
           case FNC_READ:
           case FNC_WMRK:
           case FNC_WRIT:
                break;
           case FNC_RALL:
                dt_putword(&data);
                break;
           case FNC_SRCH:
                dt_putword(&data);
                break;
           case FNC_WALL:
           case FNC_WBLK:
                dt_getword(&data, 0);
                break;
           }
           break;

      case DTC_REND:                           /* In final endzone */
           sim_activate(uptr, DT_WRDTIM*10);
           word = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
           word--;
           uptr->DSTATE = DTC_RBLK|(word << DTC_V_BLK) | (DTC_MOTMASK & uptr->DSTATE);
           break;
      }
  } else {
      if (DTC_GETUNI(dtsa) == u)  {
          dtsb &= ~DTB_REV;
          dtsa &=~ DTC_RVDRV;
          dtsa |= DTC_FWDRV;
      }
  /* Moving in forward direction */
      switch (uptr->DSTATE & 7) {
      case DTC_FEND:                           /* Tape in endzone */
           sim_activate(uptr, DT_WRDTIM*10);
           sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o forward end\n", u);
           uptr->DSTATE = DTC_FBLK | (DTC_MOTMASK & uptr->DSTATE);  /* Move to first block */
           uptr->u6 = 0;
           dtsb &= ~DTB_IDL;
           break;

      case DTC_FBLK:                           /* In forward block number */
           sim_activate(uptr,DT_WRDTIM*2);
           dtsb &= ~DTB_END;
           dtsb |= DTB_BLKRD;
           word = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
           uptr->DSTATE = DTC_FCHK|(word << DTC_V_BLK) | (DTC_MOTMASK & uptr->DSTATE);
           sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o forward block %04o\n", u, word);
           data = (uint64)word;
           if (DTC_GETUNI(dtsa) == u)  {
               uptr->CMD &= 077077;
               uptr->CMD |= dtsa & 0700;        /* Copy command */
           }
           switch (DTC_GETFNC(uptr->CMD)) {
           case FNC_RALL:
           case FNC_SRCH:
               dt_putword(&data);
               break;
           case FNC_MOVE:
           case FNC_READ:
           case FNC_WRIT:
               break;
           case FNC_WALL:
           case FNC_WBLK:
               dt_getword(&data, 0);
               break;
           case FNC_WMRK:
               dtsb |= DTS_ILL_OP;
               if (dtsb & DTB_ILLENB)
                   set_interrupt(DT_DEVNUM, dtsa);
               break;
           }
           break;

      case DTC_FCHK:                           /* In forward checksum */
           sim_activate(uptr,DT_WRDTIM*2);
           sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o forward check\n", u);
           dtsb &= ~DTB_BLKRD;
           uptr->DSTATE &= ~7;
           uptr->DSTATE |= DTC_BLOCK;              /* Move to datablock */
           if (dtsb & DTB_STOP)
               dtsa &= ~0700;          /* Clear command */
           if (DTC_GETUNI(dtsa) == u)  {
               uptr->CMD &= 077077;
               uptr->CMD |= dtsa & 0700;          /* Copy command */
           }
           switch (DTC_GETFNC(uptr->CMD)) {
           case FNC_WRIT:
           case FNC_WALL:
               dtsb |= DTB_DATREQ;
               set_interrupt_mpx(DT_DEVNUM|4, dtsa >> 3, dt_mpx_lvl);
               break;
           case FNC_SRCH:
               dtsb |= DTB_DONE;
               dtsb &= ~DTB_STOP;
               if (dtsb & DTB_JOBENB)
                  set_interrupt(DT_DEVNUM, dtsa);
               break;
           case FNC_WMRK:
               dtsb |= DTS_ILL_OP;
               if (dtsb & DTB_ILLENB)
                   set_interrupt(DT_DEVNUM, dtsa);
               break;
           case FNC_RALL:
           case FNC_READ:
           case FNC_WBLK:
           case FNC_MOVE:
               break;
           }
           if (dtsb & (DTB_PAR|DTB_MIS|DTB_ILL|DTB_END|DTB_INCBLK|DTB_MRKERR)) {
                uptr->CMD |= DTC_FNC_STOP;
           }
           break;

      case DTC_BLOCK:                          /* In block */
           sim_activate(uptr,DT_WRDTIM);
           blk = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
           word = (uptr->DSTATE >> DTC_V_WORD) & DTC_M_WORD;
           off = ((blk << 7) + word) << 1;
           dtsb |= DTB_DAT;
           /* Check if at end of block */
           if (word == DTC_M_WORD) {
               uptr->DSTATE &= ~7;
               uptr->DSTATE |= DTC_RCHK;           /* Move to checksum */
               dtsb |= DTB_FIN;
           } else {
               uptr->DSTATE &= ~(DTC_M_WORD << DTC_V_WORD);
               uptr->DSTATE |= (word + 1) << DTC_V_WORD;
           }
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
                if ((dtsb & DTB_STOP) == 0)
                    dt_putword(&data);
                else
                    uptr->CMD &= 077077;
                break;
           case FNC_WRIT:
                if ((dtsb & DTB_STOP) == 0)
                    dt_getword(&data, (word != DTC_M_WORD));
                else {
                    uptr->CMD &= 077077;
                    data = dtdb;
                }
                fbuf[off] = (data >> 18) & RMASK;
                fbuf[off+1] = data & RMASK;
                uptr->WRITTEN = 1;
                uptr->hwmark = uptr->capac;
                break;
           case FNC_WMRK:
                dtsb |= DTS_ILL_OP;
                if (dtsb & DTB_ILLENB)
                   set_interrupt(DT_DEVNUM, dtsa);
                break;
           }
           if (word == DTC_M_WORD) {
               dtsb &= ~DTB_DAT;
               dtsb |= DTB_FIN;
           }
           sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o data word %o:%o %012llo %d %06o %06o\n",
                      u, blk, word, data, off, fbuf[off], fbuf[off+1]);
           break;

      case DTC_RCHK:                           /* In reverse checksum */
           sim_activate(uptr,DT_WRDTIM*2);
           sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o reverse check\n", u);
           uptr->DSTATE &= ~(DTC_M_WORD << DTC_V_WORD) | 7;
           uptr->DSTATE |= DTC_RBLK;               /* Move to end of block */
           dtsb &= ~(DTB_DAT|DTB_FIN);
           dtsb |= DTB_CHK;
           break;

      case DTC_RBLK:                           /* In reverse block number */
           sim_activate(uptr,DT_WRDTIM*2);
           dtsb &= ~(DTB_CHK);
           dtsb |= DTB_IDL;
           if (DTC_GETUNI(dtsa) == u)  {
               uptr->CMD &= 077077;
               uptr->CMD |= dtsa & 0700;          /* Copy command */
           }
           word = (uptr->DSTATE >> DTC_V_BLK) & DTC_M_BLK;
           word++;
           if (word > 01101) {
                uptr->DSTATE = DTC_REND|(word << DTC_V_BLK)|(DTC_M_WORD << DTC_V_WORD) |
                                  (DTC_MOTMASK & uptr->DSTATE);
           } else {
                uptr->DSTATE = DTC_FBLK|(word << DTC_V_BLK) | (DTC_MOTMASK & uptr->DSTATE);
           }
           if (dtsb & DTB_STOP)
               dtsa &= ~0700;          /* Clear command */
           sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o reverse block %o\n", u, word);
           switch (DTC_GETFNC(uptr->CMD)) {
           case FNC_MOVE:
           case FNC_WBLK:
           case FNC_SRCH:
                if ((dtsb & DTB_STOP) == 0)
                    break;
                /* Fall through */
           case FNC_WALL:
           case FNC_RALL:
           case FNC_WRIT:
           case FNC_READ:
           case FNC_WMRK:
                    uptr->CMD &= 077077;
                    dtsb |= DTB_DONE;
                    if (dtsb & DTB_JOBENB)
                       set_interrupt(DT_DEVNUM, dtsa);
                    sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o stop\n", u);
                    dtsb &= ~DTB_STOP;
                break;
           }
           if (dtsb & (DTB_PAR|DTB_MIS|DTB_ILL|DTB_END|DTB_INCBLK|DTB_MRKERR)) {
                uptr->CMD |= DTC_FNC_STOP;
           }
           break;

      case DTC_REND:                           /* In final endzone */
           /* Set stop */
           uptr->CMD |= DTC_FNC_STOP;
           sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o reverse end\n", u);
           dtsb &= ~DTB_IDL;
           dtsb |= DTB_END;
           if (dtsb & DTB_ENDENB)
              set_interrupt(DT_DEVNUM, dtsa);
           sim_activate(uptr, DT_WRDTIM*10);
           break;
      }
  }
/* Check if starting */
} else if (uptr->CMD & DTC_FNC_START) {
   /* Start up delay */
   sim_activate(uptr, DT_WRDTIM*10);
   uptr->CMD &= ~(0700 | DTC_FNC_START);
   if (DTC_GETUNI(dtsa) == u)
       uptr->CMD |= dtsa & 0700;          /* Copy command */
   uptr->DSTATE |=  DTC_MOT;
   if (uptr->CMD & DTC_FNC_REV) {
       uptr->CMD &= ~DTC_FNC_REV;
       uptr->DSTATE ^= DTC_REV;
   }
   sim_debug(DEBUG_DETAIL, &dt_dev, "DTA %o start %06o\n", u, uptr->CMD);
   return SCPE_OK;
}
return SCPE_OK;
}

/* Boot from given device */
t_stat
dt_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    uint32             *fbuf = (uint32 *) uptr->filebuf;    /* file buffer */
    uint64              word = 0;
    int                 off;
    int                 wc, addr;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    off = 0;
    wc = fbuf[off++];
    addr = fbuf[off++];
    while (wc != 0) {
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

#if MPX_DEV
/* set MPX level number */
t_stat dt_set_mpx (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 mpx;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    mpx = (int32) get_uint (cptr, 8, 8, &r);
    if (r != SCPE_OK)
        return r;
    dt_mpx_lvl = mpx;
    return SCPE_OK;
}

t_stat dt_show_mpx (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   if (uptr == NULL)
      return SCPE_IERR;

   fprintf (st, "MPX=%o", dt_mpx_lvl);
   return SCPE_OK;
}
#endif

/* Reset routine */

t_stat dt_reset (DEVICE *dptr)
{
    int   i;

    dtsb = dtsa = 0;                                    /* clear status */
    for (i = 0; i < DT_NUMDR; i++) {
       if ((dt_unit[i].DSTATE & DTC_MOT) != 0)
           dt_unit[i].CMD |= DTC_FNC_STOP;
    }
    clr_interrupt(DT_DEVNUM);
    clr_interrupt(DT_DEVNUM|4);
    return SCPE_OK;
}

/* Attach routine

   Determine 12b, 16b, or 18b/36b format
   Allocate buffer
   If 12b, read 12b format and convert to 18b in buffer
   If 16b, read 16b format and convert to 18b in buffer
   If 18b/36b, read data into buffer
*/

t_stat dt_attach (UNIT *uptr, CONST char *cptr)
{
    uint16 pdp8b[D8_NBSIZE];
    uint16 pdp11b[D18_BSIZE];
    uint32 ba, sz, k, *fbuf;
    int32 u = uptr - dt_dev.units;
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
    sim_printf ("%s%d: ", sim_dname (&dt_dev), u);
    if (uptr->flags & UNIT_8FMT)
        sim_printf ("12b format");
    else if (uptr->flags & UNIT_11FMT)
        sim_printf ("16b format");
    else sim_printf ("18b/36b format");
    sim_printf (", buffering file in memory\n");
    uptr->io_flush = dt_flush;
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
        }                                                   /* end file loop */
        uptr->hwmark = ba;
    } else if (uptr->flags & UNIT_11FMT) {                  /* 16b? */
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
    } else uptr->hwmark = fxread (uptr->filebuf, sizeof (uint32),
        uptr->capac, uptr->fileref);
    uptr->flags = uptr->flags | UNIT_BUF;                   /* set buf flag */
    uptr->pos = DT_EZLIN;                                   /* beyond leader */
    uptr->WRITTEN = 0;
    return SCPE_OK;
}

/* Flush tape image to disk

   Cancel in progress operation
   If 12b, convert 18b buffer to 12b and write to file
   If 16b, convert 18b buffer to 16b and write to file
   If 18b/36b, write buffer to file
   Deallocate buffer
*/

void dt_flush (UNIT* uptr)
{
    uint16 pdp8b[D8_NBSIZE];
    uint16 pdp11b[D18_BSIZE];
    uint32 ba, k, *fbuf;

    if (uptr->WRITTEN && uptr->hwmark && ((uptr->flags & UNIT_RO) == 0)) {   /* any data? */
        sim_printf ("%s: writing buffer to file: %s\n", sim_uname (uptr), uptr->filename);
        rewind (uptr->fileref);                             /* start of file */
        fbuf = (uint32 *) uptr->filebuf;                    /* file buffer */
        if (uptr->flags & UNIT_8FMT) {                      /* 12b? */
            for (ba = 0; ba < uptr->hwmark; ) {             /* loop thru file */
                for (k = 0; k < D8_NBSIZE; k = k + 3) {     /* loop blk */
                    pdp8b[k] = (fbuf[ba] >> 6) & 07777;
                    pdp8b[k + 1] = ((fbuf[ba] & 077) << 6) |
                        ((fbuf[ba + 1] >> 12) & 077);
                    pdp8b[k + 2] = fbuf[ba + 1] & 07777;
                    ba = ba + 2;
                }                                           /* end loop blk */
                fxwrite (pdp8b, sizeof (uint16), D8_NBSIZE, uptr->fileref);
                if (ferror (uptr->fileref))
                    break;
            }                                               /* end loop file */
        } else if (uptr->flags & UNIT_11FMT) {              /* 16b? */
            for (ba = 0; ba < uptr->hwmark; ) {             /* loop thru file */
                for (k = 0; k < D18_BSIZE; k++)             /* loop blk */
                    pdp11b[k] = fbuf[ba++] & 0177777;
                fxwrite (pdp11b, sizeof (uint16), D18_BSIZE, uptr->fileref);
                if (ferror (uptr->fileref))
                    break;
            }                                               /* end loop file */
        }                                                   /* end if 16b */
        else fxwrite (uptr->filebuf, sizeof (uint32),       /* write file */
            uptr->hwmark, uptr->fileref);
        if (ferror (uptr->fileref))
            sim_perror ("I/O error");
    }                                                        /* end if hwmark */
    uptr->WRITTEN = 0;
}

/* Detach routine

   Cancel in progress operation
   If 12b, convert 18b buffer to 12b and write to file
   If 16b, convert 18b buffer to 16b and write to file
   If 18b/36b, write buffer to file
   Deallocate buffer
*/

t_stat dt_detach (UNIT* uptr)
{
    int32 u = uptr - dt_dev.units;

    if (!(uptr->flags & UNIT_ATT))
        return SCPE_OK;
    if (sim_is_active (uptr)) {
        sim_cancel (uptr);
        uptr->CMD = uptr->pos = 0;
    }
    if (uptr->hwmark && ((uptr->flags & UNIT_RO) == 0))     /* any data? */
        dt_flush(uptr);                                     /* end if hwmark */
    free (uptr->filebuf);                                   /* release buf */
    uptr->flags = uptr->flags & ~UNIT_BUF;                  /* clear buf flag */
    uptr->filebuf = NULL;                                   /* clear buf ptr */
    uptr->flags = uptr->flags & ~(UNIT_8FMT | UNIT_11FMT);  /* default fmt */
    uptr->capac = DT_CAPAC;                                 /* default size */
    return detach_unit (uptr);
}
#endif
