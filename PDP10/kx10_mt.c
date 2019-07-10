/* ka10_mt.c: TM10A/B Magnetic tape controller

   Copyright (c) 2013-2017, Richard Cornwell

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

#include "kx10_defs.h"
#include "sim_tape.h"

#ifndef NUM_DEVS_MT
#define NUM_DEVS_MT 0
#endif

#if (NUM_DEVS_MT > 0)

#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark = 0xFFFFFFFF

#define MTDF_TYPEB      (1 << DEV_V_UF)
#define MTUF_7TRK       (1 << MTUF_V_UF)

#define BUFFSIZE        (32 * 1024)
#define UNIT_MT         UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE
#define LT              66      /* Time per char low density */
#define HT              16      /* Time per char high density */

#define NOP_CLR         000     /* Nop clear, interrupt */
#define NOP_IDLE        010     /* Nop interrupt when idle */
#define REWIND          001     /* Rewind */
#define UNLOAD          011     /* Unload */
#define READ            002     /* Read  */
#define READ_NOEOR      012     /* Read no end of record. */
#define CMP             003     /* Compare */
#define CMP_NOEOR       013     /* Compare no end of record. */
#define WRITE           004     /* Write */
#define WRITE_LONG      014     /* Write with long record gap. */
#define WTM             005     /* Write End of File */
#define ERG             015     /* Write blank tape */
#define SPC_FWD         006     /* Space forward */
#define SPC_EOF         016     /* Space to end of file */
#define SPC_REV         007     /* Space reverse */
#define SPC_REV_EOF     017     /* Space reverse to EOF. */

#define DATA_REQUEST    00000000001
#define NEXT_UNIT       00000000002
#define SEVEN_CHAN      00000000004
#define WRITE_LOCK      00000000010
#define CHAN_ERR        00000000020
#define IDLE_UNIT       00000000040
#define JOB_DONE        00000000100
#define BAD_TAPE        00000000200
#define DATA_LATE       00000000400
#define RLC_ERR         00000001000
#define READ_CMP        00000002000
#define EOT_FLAG        00000004000
#define EOF_FLAG        00000010000
#define PARITY_ERR      00000020000
#define ILL_OPR         00000040000
#define BOT_FLAG        00000100000
#define REW_FLAG        00000200000
#define TRAN_HUNG       00000400000
#define CHAR_COUNT      00007000000
#define WT_CW_DONE      00010000000
#define DATA_PARITY     00020000000
#define NXM_ERR         00040000000
#define CW_PAR_ERR      00100000000
#define B22_FLAG        00400000000

#define DATA_PIA        000000007       /* 0 */
#define FLAG_PIA        000000070       /* 3 */
#define DENS_200        000000000
#define DENS_556        000000100
#define DENS_800        000000200
#define DENS_MSK        000000300       /* 6 */
#define NEXT_UNIT_ENAB  000000400       /* 8 */
#define FUNCTION        000017000       /* 9 */
#define CORE_DUMP       000020000       /*13 */
#define ODD_PARITY      000040000       /*14 */
#define UNIT_NUM        000700000       /*15 */
#define NEXT_UNIT_NUM   007000000       /*18 */

#define MT_DEVNUM       0340
#define MT_MOTION       000000001       /* Mag tape unit in motion */
#define MT_BUSY         000000002       /* Mag tape unit is busy */
#define MT_BUFFUL       000000004       /* Buffer full of data */
#define MT_BRFUL        000000010       /* BR register full */
#define MT_STOP         000000020       /* DF10 End of Channel */
#define MT_LASTWD       000000040       /* Last data word of record */

#define CNTRL           u3
#define CPOS            u5        /* Character position */
#define BPOS            u6        /* Position in buffer */

t_stat         mt_devio(uint32 dev, uint64 *data);
t_stat         mt_srv(UNIT *);
t_stat         mt_boot(int32, DEVICE *);
t_stat         mt_set_mta (UNIT *uptr, int32 val, CONST char *cptr, void *desc) ;
t_stat         mt_show_mta (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
#if MPX_DEV
t_stat         mt_set_mpx (UNIT *uptr, int32 val, CONST char *cptr, void *desc) ;
t_stat         mt_show_mpx (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
#endif
t_stat         mt_reset(DEVICE *);
t_stat         mt_attach(UNIT *, CONST char *);
t_stat         mt_detach(UNIT *);
t_stat         mt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                  const char *cptr);
const char     *mt_description (DEVICE *dptr);

struct df10     mt_df10;
uint16          mt_pia;
uint8           mt_sel_unit;
uint8           mt_next_unit;
uint8           wr_eor;
uint64          mt_status;
uint64          mt_hold_reg;
int             mt_mpx_lvl = 0;
int             hri_mode; /* Read in mode for TM10B */

static uint8          parity_table[64] = {
    /* 0    1    2    3    4    5    6    7 */
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000
};


/* One buffer per channel */
uint8               mt_buffer[BUFFSIZE];

UNIT                mt_unit[] = {
/* Controller 1 */
    {UDATA(&mt_srv,  UNIT_MT, 0)},  /* 0 */
    {UDATA(&mt_srv,  UNIT_MT, 0)},  /* 1 */
    {UDATA(&mt_srv,  UNIT_MT, 0)},  /* 2 */
    {UDATA(&mt_srv,  UNIT_MT, 0)},  /* 3 */
    {UDATA(&mt_srv,  UNIT_MT, 0)},  /* 4 */
    {UDATA(&mt_srv,  UNIT_MT, 0)},  /* 5 */
    {UDATA(&mt_srv,  UNIT_MT, 0)},  /* 6 */
    {UDATA(&mt_srv,  UNIT_MT, 0)},  /* 7 */
};

DIB mt_dib = {MT_DEVNUM, 2, &mt_devio, NULL};

MTAB                mt_mod[] = {
    {MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL},
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, MTDF_TYPEB, "TYPE", "TYPE", &mt_set_mta, &mt_show_mta},
    {MTUF_7TRK, 0, "9T", "9T", NULL, NULL},
    {MTUF_7TRK, MTUF_7TRK, "7T", "7T", NULL, NULL},
    {MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LENGTH", "LENGTH",
     &sim_tape_set_capac, &sim_tape_show_capac, NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "DENSITY", "DENSITY",
     &sim_tape_set_dens, &sim_tape_show_dens, NULL},
#if MPX_DEV
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "MPX", "MPX",
     &mt_set_mpx, &mt_show_mpx, NULL},
#endif
    {0}
};

REG                 mt_reg[] = {
    {BRDATA(BUFF, &mt_buffer[0], 16, 64, BUFFSIZE), REG_HRO},
    {ORDATA(PIA, mt_pia, 3)},
    {ORDATA(UNIT, mt_sel_unit, 3)},
    {ORDATA(NUNIT, mt_next_unit, 3)},
    {FLDATA(READIN, hri_mode, 0), REG_HRO},
    {FLDATA(WREOR, wr_eor, 0), REG_HRO},
    {ORDATA(STATUS, mt_status, 18), REG_HRO},
    {ORDATA(HOLD, mt_hold_reg, 36), REG_HRO},
    {ORDATA(MPX, mt_mpx_lvl, 3)},
    {ORDATA(DSTATUS, mt_df10.status, 18), REG_RO},
    {ORDATA(CIA, mt_df10.cia, 18)},
    {ORDATA(CCW, mt_df10.ccw, 18)},
    {ORDATA(WCR, mt_df10.wcr, 18)},
    {ORDATA(CDA, mt_df10.cda, 18)},
    {ORDATA(DEVNUM, mt_df10.devnum, 9), REG_HRO},
    {ORDATA(BUF, mt_df10.buf, 36), REG_HRO},
    {ORDATA(NXM, mt_df10.nxmerr, 8), REG_HRO},
    {ORDATA(COMP, mt_df10.ccw_comp, 8), REG_HRO},
    {0}
};

DEVICE              mt_dev = {
    "MTA", mt_unit, mt_reg, mt_mod,
    8, 8, 15, 1, 8, 8,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    &mt_dib, DEV_DISABLE | DEV_DEBUG | DEV_TAPE, 0, dev_debug,
    NULL, NULL, &mt_help, NULL, NULL, &mt_description
};

t_stat mt_devio(uint32 dev, uint64 *data) {
      uint64     res;
      DEVICE    *dptr = &mt_dev;
      UNIT      *uptr = &mt_unit[mt_sel_unit];

      switch(dev & 07) {
      case CONI:
          res = (uint64)(mt_pia & (NEXT_UNIT_ENAB|FLAG_PIA|DATA_PIA));
          res |= (uint64)(uptr->CNTRL & 077300);
          res |= ((uint64)mt_sel_unit) << 15;
          res |= ((uint64)mt_next_unit) << 18;
          res |= ((uint64)wr_eor) << 21;
          if (dptr->flags & MTDF_TYPEB)
             res |= 7LL;  /* Force DATA PIA to 7 on type B */
          *data = res;
          sim_debug(DEBUG_CONI, dptr, "MT CONI %03o status %06o %o %o PC=%06o\n",
                      dev, (uint32)res, mt_sel_unit, mt_pia, PC);
          break;

       case CONO:
          clr_interrupt(MT_DEVNUM);
          clr_interrupt(MT_DEVNUM+4);
          mt_next_unit = (*data >> 15) & 07;
          mt_pia = (uint16)(*data) & (NEXT_UNIT_ENAB|FLAG_PIA|DATA_PIA);
          mt_status &= ~(DATA_REQUEST|CHAN_ERR|JOB_DONE|DATA_LATE| \
                      BAD_TAPE|RLC_ERR|READ_CMP|EOF_FLAG|EOT_FLAG|BOT_FLAG| \
                      PARITY_ERR|ILL_OPR|REW_FLAG|TRAN_HUNG| \
                      WT_CW_DONE|DATA_PARITY|NXM_ERR|CW_PAR_ERR|IDLE_UNIT| \
                      SEVEN_CHAN|NEXT_UNIT);
          /* Check if we can switch to new unit */
          if (mt_next_unit != mt_sel_unit) {
               sim_cancel(uptr);
               mt_sel_unit = mt_next_unit;
               uptr = &mt_unit[mt_sel_unit];
          }
          if (mt_pia & NEXT_UNIT_ENAB) {
              set_interrupt(dev, mt_pia >> 3);
          }
          uptr->CNTRL = (int32)(*data & 077300);
          mt_df10.buf = 0;
          sim_debug(DEBUG_CONO, dptr,
                  "MT CONO %03o start %o %o %o %012llo %012llo PC=%06o\n",
                      dev, uptr->CNTRL, mt_sel_unit, mt_pia, *data, mt_status, PC);
          if ((uptr->flags & UNIT_ATT) != 0) {
              /* Check if Write */
              int  cmd = (uptr->CNTRL & FUNCTION) >> 9;
              uptr->CNTRL &= ~(MT_BRFUL|MT_BUFFUL|MT_STOP);
              switch(cmd & 07) {
              case NOP_CLR:
                     uptr->CNTRL &= ~MT_BUSY;
                     wr_eor = 0;
                     mt_status |= NEXT_UNIT;
                     if (cmd & 010) {
                         mt_status |= JOB_DONE;
                         set_interrupt(MT_DEVNUM+4, mt_pia >> 3);
                     } else {
                         clr_interrupt(MT_DEVNUM+4);
                     }
                     clr_interrupt(MT_DEVNUM);
                     sim_debug(DEBUG_EXP, dptr, "Setting status %012llo\n", mt_status);
                     return SCPE_OK;

              case REWIND:
                     mt_status |= REW_FLAG;
                     break;

              case WRITE:
                     if ((uptr->flags & MTUF_WLK) != 0) {
                        mt_status |= IDLE_UNIT|ILL_OPR|EOF_FLAG;
                        break;
                     }
                     /* Fall through */

              case WTM:
              case READ:
              case CMP:
                     CLR_BUF(uptr);
                     uptr->CPOS = 0;
                     break;

              case SPC_REV:
                     if (sim_tape_bot(uptr)) {
                         mt_status |= JOB_DONE|ILL_OPR;
                         set_interrupt(MT_DEVNUM+4, mt_pia >> 3);
                         return SCPE_OK;
                     }
                     /* Fall through */

              case SPC_FWD:
                     if ((dptr->flags & MTDF_TYPEB) == 0 && (cmd & 010) == 0) {
                         mt_status |= DATA_REQUEST;
                         set_interrupt_mpx(MT_DEVNUM, mt_pia, mt_mpx_lvl);
                     }
              }
              mt_status |= IDLE_UNIT;
              uptr->CNTRL |= MT_BUSY;
              sim_activate(uptr, 1000);
          } else {
              sim_activate(uptr, 9999999);
              sim_debug(DEBUG_CONO, dptr, "MT CONO %03o hung PC=%06o\n", dev, PC);
          }
          break;

     case DATAI:
          /* Xfer data */
          clr_interrupt(MT_DEVNUM);
          *data = mt_hold_reg;
          uptr->CNTRL &= ~MT_BUFFUL;
          mt_status &= ~DATA_REQUEST;
          if (uptr->CNTRL & MT_BRFUL) {
             mt_hold_reg = mt_df10.buf;
             mt_df10.buf = 0;
             uptr->CNTRL &= ~MT_BRFUL;
             uptr->CNTRL |= MT_BUFFUL;
             if ((dptr->flags & MTDF_TYPEB) == 0) {
                 mt_status |= DATA_REQUEST;
                 set_interrupt_mpx(MT_DEVNUM, mt_pia, mt_mpx_lvl);
             }
          }
          sim_debug(DEBUG_DATA, dptr, "MT %03o >%012llo\n", dev, *data);
          break;

     case DATAO:
          /* Xfer data */
          mt_hold_reg = *data;
          mt_status &= ~DATA_REQUEST;
          clr_interrupt(MT_DEVNUM);
          uptr->CNTRL |= MT_BUFFUL;
          sim_debug(DEBUG_DATA, dptr, "MT %03o <%012llo, %012llo\n",
                    dev, mt_hold_reg, mt_df10.buf);
          break;

     case CONI|04:
          res = mt_status;
          if ((uptr->CNTRL & MT_BUSY) == 0)
              res |= NEXT_UNIT;
          if ((uptr->CNTRL & (06000|MT_STOP)) == 02000 && (mt_status & JOB_DONE) != 0)
              res |= RLC_ERR;
          if ((uptr->flags & MTUF_7TRK) != 0)
              res |= SEVEN_CHAN;
          if ((uptr->flags & UNIT_ATT) != 0 && (uptr->CNTRL & MT_MOTION) == 0)
              res |= IDLE_UNIT;
          if ((uptr->flags & MTUF_WLK) != 0)
              res |= WRITE_LOCK;
          if (sim_tape_bot(uptr))
              res |= BOT_FLAG;
          if (sim_tape_eot(uptr))
              res |= EOT_FLAG;
          if ((dptr->flags & MTDF_TYPEB) == 0)
              res |= WT_CW_DONE|DATA_PARITY|NXM_ERR|CW_PAR_ERR;
#if KI_22BIT
          if (dptr->flags & MTDF_TYPEB)
              res |= B22_FLAG;
#endif
          *data = res;
          sim_debug(DEBUG_CONI, dptr, "MT CONI %03o status2 %012llo %o %012llo PC=%06o\n",
                      dev, res, mt_sel_unit, mt_status, PC);
          break;

     case CONO|04:
          if (*data & 1) {
              uptr->CNTRL |= MT_STOP;
              hri_mode = 0;
              sim_debug(DEBUG_DETAIL, dptr, "MT stop %03o %012llo\n", dev, mt_status);
          }
          if (*data & 2) {
              mt_hold_reg ^= mt_df10.buf;
          }
          if (dptr->flags & MTDF_TYPEB) {
              if (*data & 04)
                  df10_writecw(&mt_df10);
              if (*data & 010)
                  mt_status &= ~(WT_CW_DONE);
          }
          sim_debug(DEBUG_CONO, dptr, "MT CONO %03o control %o %o %012llo %012llo\n",
                      dev, uptr->CNTRL, mt_sel_unit, mt_hold_reg, mt_df10.buf);
          break;

     case DATAI|04:
          *data = 0;
          break;

     case DATAO|04:
          /* Set Initial CCW */
          if (dptr->flags & MTDF_TYPEB)
              df10_setup(&mt_df10, (uint32) *data);
          else
              mt_df10.buf ^= mt_hold_reg;
          sim_debug(DEBUG_DATAIO, dptr, "MT DATAO %03o %012llo\n", dev, *data);
          break;
     }
     return SCPE_OK;
}

/* Wrapper to handle reading of hold register or via DF10 */
void mt_df10_read(DEVICE *dptr, UNIT *uptr) {
     if (dptr->flags & MTDF_TYPEB) {
         if (!df10_read(&mt_df10)) {
             uptr->CNTRL |= MT_STOP;
         }
         sim_debug(DEBUG_DATA, dptr, "MT  <%012llo %o\n", mt_df10.buf, uptr->CPOS);
     } else {
        if (uptr->CNTRL & MT_BUFFUL) {
            mt_df10.buf = mt_hold_reg;
            if ((uptr->CNTRL & MT_STOP) == 0) {
                mt_status |= DATA_REQUEST;
                set_interrupt_mpx(MT_DEVNUM, mt_pia, mt_mpx_lvl);
            }
        } else {
            if ((uptr->CNTRL & MT_STOP) == 0) {
                mt_status |= DATA_LATE;
                uptr->CNTRL |= MT_STOP;
            }
            return;
        }
     }
     uptr->CNTRL &= ~MT_BUFFUL;
     uptr->CNTRL |= MT_BRFUL;
     uptr->CPOS = 0;
}

/* Wrapper to handle writing of hold register or via DF10 */
void mt_df10_write(DEVICE *dptr, UNIT *uptr) {
     if (dptr->flags & MTDF_TYPEB) {
        if (hri_mode) {
            mt_hold_reg = mt_df10.buf;
            mt_status |= DATA_REQUEST;
        } else if (!df10_write(&mt_df10)) {
            uptr->CNTRL |= MT_STOP;
            return;
        }
        sim_debug(DEBUG_DATA, dptr, "MT  >%012llo %o\n", mt_df10.buf, uptr->CPOS);
        uptr->CNTRL &= ~(MT_BUFFUL|MT_BRFUL);
     } else {
        if ((uptr->CNTRL & MT_BUFFUL) == 0) {
            mt_hold_reg = mt_df10.buf;
            mt_status |= DATA_REQUEST;
            uptr->CNTRL &= ~(MT_BRFUL);
            uptr->CNTRL |= MT_BUFFUL;
            set_interrupt_mpx(MT_DEVNUM, mt_pia, mt_mpx_lvl);
        } else {
            uptr->CNTRL |= MT_BRFUL;
        }
     }
     mt_df10.buf = 0;
     uptr->CPOS = 0;
}


/* Map simH errors into machine errors */
t_stat mt_error(UNIT * uptr, t_stat r, DEVICE * dptr)
{
       switch (r) {
       case MTSE_OK:            /* no error */
            break;

       case MTSE_TMK:           /* tape mark */
            mt_status |= EOF_FLAG;
            break;

       case MTSE_WRP:           /* write protected */
            mt_status |= WRITE_LOCK;
            break;

       case MTSE_UNATT:         /* unattached */
            mt_status |= TRAN_HUNG;
            break;

       case MTSE_IOERR:         /* IO error */
       case MTSE_FMT:           /* invalid format */
            mt_status |= ILL_OPR;
            break;

       case MTSE_RECE:          /* error in record */
            mt_status |= BAD_TAPE;
            break;

       case MTSE_BOT:           /* beginning of tape */
            mt_status |= BOT_FLAG;
            break;

       case MTSE_INVRL:         /* invalid rec lnt */
            break;

       case MTSE_EOM:           /* end of medium */
            mt_status |= EOT_FLAG;
            break;
       }
        if (mt_next_unit != mt_sel_unit) {
            mt_sel_unit = mt_next_unit;
            mt_status |= NEXT_UNIT;
            if (mt_pia & NEXT_UNIT_ENAB)
                set_interrupt(MT_DEVNUM+4, mt_pia >> 3);
       }
       mt_status |= JOB_DONE;
       uptr->CNTRL &= ~MT_BUSY;
       sim_debug(DEBUG_EXP, dptr, "Setting status %d %012llo\n", r, mt_status);
       set_interrupt(MT_DEVNUM+4, mt_pia >> 3);
       return SCPE_OK;
}

/* Handle processing of tape requests. */
t_stat mt_srv(UNIT * uptr)
{
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 unit = (uptr - dptr->units) & 7;
    int                 cmd = (uptr->CNTRL & FUNCTION) >> 9;
    t_mtrlnt            reclen;
    t_stat              r = SCPE_ARG;   /* Force error if not set */
    uint8               ch;
    int                 cc;
    int                 cc_max;

    if ((uptr->flags & UNIT_ATT) == 0) {
       uptr->CNTRL &= ~MT_MOTION;
       return mt_error(uptr, MTSE_UNATT, dptr);      /* attached? */
    }

    if ((cmd & 6) != 0 && (uptr->CNTRL & DENS_MSK) != DENS_800) {
       uptr->CNTRL &= ~MT_MOTION;
       return mt_error(uptr, MTSE_FMT, dptr);    /* wrong density? */
    }

    if (uptr->flags & MTUF_7TRK) {
       cc_max = 6;
    } else {
       cc_max = (4 + ((uptr->CNTRL & CORE_DUMP) != 0));
    }

    switch(cmd) {
    case NOP_IDLE:
        sim_debug(DEBUG_DETAIL, dptr, "MT%o Idle\n", unit);
        uptr->CNTRL &= ~MT_MOTION;
        return mt_error(uptr, MTSE_OK, dptr);

    case NOP_CLR:
        sim_debug(DEBUG_DETAIL, dptr, "MT%o nop\n", unit);
        return mt_error(uptr, MTSE_OK, dptr);      /* Nop */

    case REWIND:
        mt_status &= ~IDLE_UNIT;
        sim_debug(DEBUG_DETAIL, dptr, "MT%o rewind\n", unit);
        uptr->CNTRL &= ~MT_MOTION;
        mt_status |= BOT_FLAG;
        return mt_error(uptr, sim_tape_rewind(uptr), dptr);

    case UNLOAD:
        mt_status &= ~IDLE_UNIT;
        sim_debug(DEBUG_DETAIL, dptr, "MT%o unload\n", unit);
        uptr->CNTRL &= ~MT_MOTION;
        return mt_error(uptr, sim_tape_detach(uptr), dptr);

    case READ:
    case READ_NOEOR:
        if (uptr->CNTRL & MT_STOP) {
            if ((uptr->CNTRL & MT_LASTWD) == 0)
               mt_status |= RLC_ERR;
            if (dptr->flags & MTDF_TYPEB)
                df10_writecw(&mt_df10);
            return mt_error(uptr, MTSE_OK, dptr);
        }
        if (BUF_EMPTY(uptr)) {
            uptr->CNTRL |= MT_MOTION;
            mt_status &= ~(IDLE_UNIT|BOT_FLAG|EOF_FLAG|EOT_FLAG|PARITY_ERR|CHAR_COUNT);
            if ((r = sim_tape_rdrecf(uptr, &mt_buffer[0], &reclen,
                                 BUFFSIZE)) != MTSE_OK) {
                 sim_debug(DEBUG_DETAIL, dptr, "MT%o read error %d\n", unit, r);
                 uptr->CNTRL &= ~MT_MOTION;
                 if (dptr->flags & MTDF_TYPEB && r == MTSE_TMK) {
                     df10_write(&mt_df10);
                     df10_writecw(&mt_df10);
                 }
                 return mt_error(uptr, r, dptr);
            }
            sim_debug(DEBUG_DETAIL, dptr, "MT%o read %d\n", unit, reclen);
            uptr->hwmark = reclen;
            uptr->BPOS = 0;
        }
        if (uptr->CNTRL & MT_BRFUL) {
            mt_status |= DATA_LATE;
            sim_debug(DEBUG_EXP, dptr, "data late\n");
            break;
        }
        if ((uint32)uptr->BPOS < uptr->hwmark) {
            if (uptr->flags & MTUF_7TRK) {
                cc = 6 * (5 - uptr->CPOS);
                ch = mt_buffer[uptr->BPOS];
                if ((((uptr->CNTRL & ODD_PARITY) ? 0x40 : 0) ^
                      parity_table[ch & 0x3f]) != 0) {
                      mt_status |= PARITY_ERR;
                }
                mt_df10.buf |= (uint64)(ch & 0x3f) << cc;
            } else {
                if ((uptr->CNTRL & ODD_PARITY) == 0)
                      mt_status |= PARITY_ERR;
                cc = (8 * (3 - uptr->CPOS)) + 4;
                ch = mt_buffer[uptr->BPOS];
                if (cc < 0)
                    mt_df10.buf |=  (uint64)(ch & 0x3f);
                else
                    mt_df10.buf |= (uint64)(ch & 0xff) << cc;
            }
            uptr->BPOS++;
            uptr->CPOS++;
            if ((uint32)(uptr->BPOS + cc_max) >= uptr->hwmark)
                uptr->CNTRL |= MT_LASTWD;
            mt_status &= ~CHAR_COUNT;
            mt_status |= (uint64)(uptr->CPOS) << 18;
            if (uptr->CPOS == cc_max)
                mt_df10_write(dptr, uptr);
          } else {
                if ((cmd & 010) == 0) {
                    if (dptr->flags & MTDF_TYPEB)
                        df10_writecw(&mt_df10);
                    uptr->CNTRL &= ~(MT_MOTION|MT_BUSY);
                    return mt_error(uptr, MTSE_OK, dptr);
                } else {
                    CLR_BUF(uptr);
                }
          }
          break;

    case CMP:
    case CMP_NOEOR:
         if (uptr->CNTRL & MT_STOP) {
             if (dptr->flags & MTDF_TYPEB)
                 df10_writecw(&mt_df10);
             return mt_error(uptr, MTSE_OK, dptr);
         }
         if (BUF_EMPTY(uptr)) {
             uptr->CNTRL |= MT_MOTION;
             mt_status &= ~(IDLE_UNIT|BOT_FLAG|EOF_FLAG|EOT_FLAG|PARITY_ERR|CHAR_COUNT);
             if ((r = sim_tape_rdrecf(uptr, &mt_buffer[0], &reclen,
                                  BUFFSIZE)) != MTSE_OK) {
                  sim_debug(DEBUG_DETAIL, dptr, "MT%o read error %d\n", unit, r);
                  uptr->CNTRL &= ~MT_MOTION;
                  if (dptr->flags & MTDF_TYPEB && r == MTSE_TMK)
                      mt_df10_read(dptr, uptr);
                  return mt_error(uptr, r, dptr);
             }
             sim_debug(DEBUG_DETAIL, dptr, "MT%o compare %d\n", unit, reclen);
             uptr->hwmark = reclen;
             uptr->BPOS = 0;
             if ((dptr->flags & MTDF_TYPEB) == 0) {
                 mt_status |= DATA_REQUEST;
                 set_interrupt_mpx(MT_DEVNUM, mt_pia, mt_mpx_lvl);
             }
             break;
         }
         if (uptr->BPOS >= (int32)uptr->hwmark) {
            if (cmd == CMP_NOEOR) {
               CLR_BUF(uptr);
               uptr->CNTRL &= ~MT_LASTWD;
            } else {
                if (dptr->flags & MTDF_TYPEB)
                    df10_writecw(&mt_df10);
                uptr->CNTRL &= ~(MT_MOTION|MT_BUSY);
                return mt_error(uptr, MTSE_INVRL, dptr);
            }
         } else if ((uptr->CNTRL & MT_BRFUL) == 0) {
            /* Write out first character. */
            mt_df10_read(dptr, uptr);
         }
         if ((uptr->CNTRL & MT_BRFUL) != 0) {
            if (uptr->flags & MTUF_7TRK) {
                ch = mt_buffer[uptr->BPOS];
                if ((((uptr->CNTRL & ODD_PARITY) ? 0x40 : 0) ^
                      parity_table[ch & 0x3f]) != (ch & 0x40)) {
                      mt_status |= PARITY_ERR;
                }
                mt_buffer[uptr->BPOS] &= 0x3f;
                cc = 6 * (5 - uptr->CPOS);
                ch = (mt_df10.buf >> cc) & 0x3f;
            } else {
                if ((uptr->CNTRL & ODD_PARITY) == 0)
                      mt_status |= PARITY_ERR;
                /* Write next char out */
                cc = (8 * (3 - uptr->CPOS)) + 4;
                if (cc < 0)
                     ch = mt_df10.buf & 0x3f;
                else
                     ch = (mt_df10.buf >> cc) & 0xff;
            }
            if (mt_buffer[uptr->BPOS] != ch) {
                mt_status |= READ_CMP;
                if ((dptr->flags & MTDF_TYPEB) == 0) {
                     uptr->BPOS = uptr->hwmark;
                     mt_status &= ~CHAR_COUNT;
                     mt_status |= (uint64)(uptr->CPOS+1) << 18;
                     uptr->CNTRL &= ~(MT_MOTION|MT_BUSY);
                     if (dptr->flags & MTDF_TYPEB)
                         df10_writecw(&mt_df10);
                     return mt_error(uptr, MTSE_OK, dptr);
                }
            }
            uptr->BPOS++;
            uptr->CPOS++;
            if (uptr->BPOS == uptr->hwmark)
                uptr->CNTRL |= MT_LASTWD;
            if (uptr->CPOS == cc_max) {
               uptr->CPOS = 0;
               uptr->CNTRL &= ~MT_BRFUL;
            }
            mt_status &= ~CHAR_COUNT;
            mt_status |= (uint64)(uptr->CPOS+1) << 18;
         }
         break;

    case WRITE:
    case WRITE_LONG:
         /* Writing and Type A, request first data word */
         if (BUF_EMPTY(uptr)) {
             uptr->CNTRL |= MT_MOTION;
             mt_status &= ~(IDLE_UNIT|BOT_FLAG|EOF_FLAG|EOT_FLAG|PARITY_ERR|CHAR_COUNT);
             sim_debug(DEBUG_EXP, dptr, "MT%o Init write\n", unit);
             uptr->hwmark = 0;
             uptr->CPOS = 0;
             uptr->BPOS = 0;
             mt_status |= (uint64)(1) << 18;
             if ((dptr->flags & MTDF_TYPEB) == 0) {
                 mt_status |= DATA_REQUEST;
                 set_interrupt_mpx(MT_DEVNUM, mt_pia, mt_mpx_lvl);
             }
             break;
         }
         /* Force error if we exceed buffer size */
         if (uptr->BPOS >= BUFFSIZE)
             return mt_error(uptr, MTSE_RECE, dptr);
         if ((uptr->CNTRL & MT_BRFUL) == 0)
              mt_df10_read(dptr, uptr);
         if ((uptr->CNTRL & MT_BRFUL) != 0) {
            if (uptr->flags & MTUF_7TRK) {
                cc = 6 * (5 - uptr->CPOS);
                ch = (mt_df10.buf >> cc) & 0x3f;
                ch |= ((uptr->CNTRL & ODD_PARITY) ? 0x40 : 0) ^
                          parity_table[ch & 0x3f];
            } else {
                /* Write next char out */
                cc = (8 * (3 - uptr->CPOS)) + 4;
                if (cc < 0)
                     ch = mt_df10.buf & 0x3f;
                else
                     ch = (mt_df10.buf >> cc) & 0xff;
            }
            mt_buffer[uptr->BPOS] = ch;
            uptr->BPOS++;
            uptr->hwmark = uptr->BPOS;
            uptr->CPOS++;
            if (uptr->CPOS == cc_max) {
               uptr->CPOS = 0;
               uptr->CNTRL &= ~MT_BRFUL;
            }
            mt_status &= ~CHAR_COUNT;
            mt_status |= (uint64)(uptr->CPOS+1) << 18;
         }
         if ((uptr->CNTRL & (MT_STOP|MT_BRFUL|MT_BUFFUL)) == MT_STOP) {
                /* Write out the block */
                wr_eor = 1;
                reclen = uptr->hwmark;
                mt_status &= ~(BOT_FLAG|EOF_FLAG|EOT_FLAG|CHAR_COUNT);
                r = sim_tape_wrrecf(uptr, &mt_buffer[0], reclen);
                sim_debug(DEBUG_DETAIL, dptr, "MT%o Write %d\n", unit, reclen);
                uptr->BPOS = 0;
                uptr->hwmark = 0;
                uptr->CNTRL &= ~MT_MOTION;
                if (dptr->flags & MTDF_TYPEB)
                    df10_writecw(&mt_df10);
                return mt_error(uptr, r, dptr); /* Record errors */
         }
         break;

    case WTM:
        if ((uptr->flags & MTUF_WLK) != 0)
            return mt_error(uptr, MTSE_WRP, dptr);
        if (uptr->CPOS == 0) {
            mt_status &= ~(IDLE_UNIT|BOT_FLAG|EOT_FLAG);
            sim_debug(DEBUG_DETAIL, dptr, "MT%o WTM\n", unit);
            r = sim_tape_wrtmk(uptr);
            if (r != MTSE_OK)
                return mt_error(uptr, r, dptr);
            uptr->CPOS++;
            wr_eor = 1;
         } else {
            wr_eor = 0;
            mt_status |= EOF_FLAG;
            uptr->CNTRL &= ~MT_MOTION;
            return mt_error(uptr, MTSE_OK, dptr);
         }
         break;

    case ERG:
        if ((uptr->flags & MTUF_WLK) != 0)
            return mt_error(uptr, MTSE_WRP, dptr);
        uptr->CNTRL &= ~MT_MOTION;
        mt_status &= ~(IDLE_UNIT|BOT_FLAG|EOT_FLAG);
        sim_debug(DEBUG_DETAIL, dptr, "MT%o ERG\n", unit);
        return mt_error(uptr, sim_tape_wrgap(uptr, 35), dptr);

    case SPC_REV_EOF:
    case SPC_EOF:
    case SPC_REV:
    case SPC_FWD:
        sim_debug(DEBUG_DETAIL, dptr, "MT%o space %o\n", unit, cmd);
        uptr->CNTRL |= MT_MOTION;
        mt_status &= ~(IDLE_UNIT|BOT_FLAG|EOT_FLAG);
        /* Always skip at least one record */
        if ((cmd & 7) == SPC_FWD)
            r = sim_tape_sprecf(uptr, &reclen);
        else
            r = sim_tape_sprecr(uptr, &reclen);
        switch (r) {
        case MTSE_OK:            /* no error */
             break;
        case MTSE_TMK:           /* tape mark */
        case MTSE_BOT:           /* beginning of tape */
        case MTSE_EOM:           /* end of medium */
             /* Stop motion if we recieve any of these */
             uptr->CNTRL &= ~MT_MOTION;
             mt_status &= ~DATA_REQUEST;
             clr_interrupt(MT_DEVNUM);
             return mt_error(uptr, r, dptr);
        }
        /* Clear tape mark, command, idle since we will need to change dir */
        if ((cmd & 010) == 0) {
            mt_df10_read(dptr, uptr);
            if ((uptr->CNTRL & MT_BRFUL) == 0) {
                mt_status &= ~DATA_LATE;
                uptr->CNTRL &= ~MT_MOTION;
                if (dptr->flags & MTDF_TYPEB)
                    df10_writecw(&mt_df10);
                return mt_error(uptr, MTSE_OK, dptr);
            }
            uptr->CNTRL &= ~MT_BRFUL;
        }
        uptr->hwmark = 0;
        sim_activate(uptr, 5000);
        return SCPE_OK;
    }
    sim_activate(uptr, 420);
    return SCPE_OK;
}

void mt_read_word(UNIT *uptr) {
     int i, cc, ch;

     mt_df10.buf = 0;
     for(i = 0; i <= 4; i++) {
        cc = (8 * (3 - i)) + 4;
        ch = mt_buffer[uptr->BPOS];
        if (cc < 0)
            mt_df10.buf |=  (uint64)(ch & 0x3f);
        else
            mt_df10.buf |= (uint64)(ch & 0xff) << cc;
        uptr->BPOS++;
     }
}

/* Boot from given device */
t_stat
mt_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    t_mtrlnt            reclen;
    t_stat              r;
    int                 wc, addr;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    r = sim_tape_rewind(uptr);
    if (r != SCPE_OK)
        return r;
    uptr->CNTRL = 022200; /* Read 800 BPI, Core */
    r = sim_tape_rdrecf(uptr, &mt_buffer[0], &reclen, BUFFSIZE);
    if (r != SCPE_OK)
        return r;
    uptr->BPOS = 0;
    uptr->hwmark = reclen;

    mt_read_word(uptr);
    wc = (mt_df10.buf >> 18) & RMASK;
    addr = mt_df10.buf & RMASK;
    while (wc != 0) {
        wc = (wc + 1) & RMASK;
        addr = (addr + 1) & RMASK;
        if ((uint32)uptr->BPOS >= uptr->hwmark) {
            r = sim_tape_rdrecf(uptr, &mt_buffer[0], &reclen, BUFFSIZE);
            if (r != SCPE_OK)
                return r;
            uptr->BPOS = 0;
            uptr->hwmark = reclen;
        }
        mt_read_word(uptr);
        if (addr < 020)
           FM[addr] = mt_df10.buf;
        else
           M[addr] = mt_df10.buf;
    }
    if (addr < 020)
        FM[addr] = mt_df10.buf;
    else
        M[addr] = mt_df10.buf;

    PC = mt_df10.buf & RMASK;
    /* If not at end of record and TMA continue record read */
    if ((uint32)uptr->BPOS < uptr->hwmark) {
        uptr->CNTRL |= MT_MOTION|MT_BUSY;
        uptr->CNTRL &= ~(MT_BRFUL|MT_BUFFUL);
        mt_hold_reg = mt_df10.buf = 0;
        if ((dptr->flags & MTDF_TYPEB) != 0) {
            mt_df10.cia = 020;
            mt_df10.cda = addr;
        }
        hri_mode = 1;
        sim_activate(uptr, 300);
    }
    return SCPE_OK;
}

t_stat mt_set_mta (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE *dptr;
    dptr = find_dev_from_unit (uptr);
    if (dptr == NULL)
       return SCPE_IERR;
    dptr->flags &= ~MTDF_TYPEB;
    if (*cptr == 'B')
        dptr->flags |= val;
    else if (*cptr != 'A')
        return SCPE_ARG;
    return SCPE_OK;
}

t_stat mt_show_mta (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   DEVICE *dptr;

   if (uptr == NULL)
      return SCPE_IERR;

   dptr = find_dev_from_unit(uptr);
   if (dptr == NULL)
      return SCPE_IERR;
   if (dptr->flags & val) {
      fprintf (st, "TM10B");
   } else {
      fprintf (st, "TM10A");
   }
   return SCPE_OK;
}

#if MPX_DEV
/* set MPX level number */
t_stat mt_set_mpx (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 mpx;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    mpx = (int32) get_uint (cptr, 8, 8, &r);
    if (r != SCPE_OK)
        return r;
    mt_mpx_lvl = mpx;
    return SCPE_OK;
}

t_stat mt_show_mpx (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   if (uptr == NULL)
      return SCPE_IERR;

   fprintf (st, "MPX=%o", mt_mpx_lvl);
   return SCPE_OK;
}
#endif

t_stat
mt_reset(DEVICE * dptr)
{
    int i;
    for (i = 0 ; i < 8; i++) {
        UNIT    *uptr = &mt_unit[i];

        if (MT_DENS(uptr->dynflags) == MT_DENS_NONE)
                uptr->dynflags = MT_200_VALID | MT_556_VALID;
        uptr->CNTRL = 0;
        sim_cancel(uptr);
    }
    mt_df10.devnum = mt_dib.dev_num;
    mt_df10.nxmerr = 24;
    mt_df10.ccw_comp = 25;
    mt_pia = 0;
    mt_status = 0;
    mt_sel_unit = 0;
    mt_next_unit = 0;
    mt_hold_reg = 0;
    return SCPE_OK;
}

t_stat
mt_attach(UNIT * uptr, CONST char *file)
{
    return sim_tape_attach_ex(uptr, file, 0, 0);
}

t_stat
mt_detach(UNIT * uptr)
{
    uptr->CPOS = 0;
    return sim_tape_detach(uptr);
}

t_stat mt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "MT10 Magnetic Tape\n\n");
fprintf (st, "The MT10 tape controller can be set to either type A or B\n");
fprintf (st, "The A model lacks a DF10, so all I/O must be polled mode. To set the\n");
fprintf (st, "tape controller to a B model with DF10 do:\n\n");
fprintf (st, "    sim> SET %s TYPE=B \n", dptr->name);
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.  The\n");
fprintf (st, "bad block option can be used only when a unit is attached to a file.\n");
fprintf (st, "The MT10 does support the BOOT command.\n");
sim_tape_attach_help (st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

const char *mt_description (DEVICE *dptr)
{
return "MT10 magnetic tape controller" ;
}

#endif
