/* ka10_mtc.c: Type 516 Magnetic tape controller

   Copyright (c) 2013-2019, Richard Cornwell

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

#ifndef NUM_DEVS_MTC
#define NUM_DEVS_MTC 0
#endif

#if (NUM_DEVS_MTC > 0)

#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark = 0xFFFFFFFF

#define MTUF_7TRK       (1 << MTUF_V_UF)

#define BUFFSIZE        (32 * 1024)
#define UNIT_MT         UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE
#define LT              66      /* Time per char low density */
#define HT              16      /* Time per char high density */

/* MTC register */
#define FLAG_PIA        0000007       /* 0 */
#define DIS_EOR         0000010       /* 3 */
#define UNIT_NUM        0000160       /* 4 */
#define HOLD_SEL        0000200       /* 7 */
#define FUNCTION        0007400       /* 8 */
#define NOP             000     /* Nop */
#define NOP_1           010     /* Nop */
#define REWIND          001     /* Rewind */
#define UNLOAD          011     /* Unload */
#define WRITE           002     /* Write */
#define WRITE_1         012     /* Write */
#define WTM             003     /* Write End of File */
#define ERG             013     /* Write blank tape */
#define CMP             004     /* Compare */
#define CMP_1           014     /* Compare */
#define READ            005     /* Read  */
#define READ_BK         015     /* Read Backward */
#define SPC_FWD         006     /* Space forward */
#define SPC_EOF         016     /* Space to end of file */
#define SPC_REV         007     /* Space reverse */
#define SPC_REV_EOF     017     /* Space reverse to EOF. */
#define DENS_200        0000000
#define DENS_556        0010000
#define DENS_800        0020000
#define DENS_MSK        0030000       /* 6 */
#define ODD_PARITY      0040000       /*14 */
#define SLICE           0100000
#define WRCLK           0200000
#define FALS_EOR        0400000
#define CMD_FULL        0x8000000
#define CMD_MASK        0777760

/* MTS register */
#define TAPE_FREE       0000001
#define TAPE_RDY        0000002
#define EOR_FLAG        0000004   /* End of record */
#define PARITY_ERR      0000010
#define PARITY_ERRL     0000020
#define READ_CMP        0000040
#define MIS_CHR         0000100  /* Charaters missed on tape */
#define WRITE_LOCK      0000200
#define EOF_FLAG        0000400
#define LD_PT           0001000  /* Tape near load point */
#define END_PT          0002000  /* Tape near end point */
#define BOT_FLAG        0004000
#define EOT_FLAG        0010000
#define REW             0020000
#define TRF_CMD         0040000
#define CONT_MOT        0100000
#define MOT_STOP        0200000
#define ILL_OPR         0400000

/* CONO to MTS */
#define ENB_ICE         0000001  /* Control Ready */
#define ENB_JNU         0000002  /* Set monitor unit */
#define ENB_ERF         0000004  /* End of Record */
#define ENB_XNE         0040000  /* New command rdy */
#define ENB_LIE         0100000  /* Load point */

/* IRQ Masks in status */
#define IRQ_ICE         001000000 
#define IRQ_JNU         002000000
#define IRQ_ERF         004000000
#define IRQ_XNE         010000000
#define IRQ_LIE         020000000
#define IRQ_MASK        037000000

/* MTM register */
#define EOR_RD_DLY      0000001
#define EOR_WR_DLY      0000002
#define MIS_CHR_DLY     0000004
#define FR_CHR_INH      0000010
#define UNIT_BUF_FIN    0000160
#define MOT_DLY         0000200
#define FUNC_FIN        0007400
#define UNIT_SEL_NEW    0010000
#define CMD_HOLD        0020000
#define MOT_STOP_DLY    0040000
#define EOR_MOT_DLY     0100000
#define REC_IN_PROG     0200000
#define TRP_SPD_DLY     0400000


#define MTC_DEVCTL       0220
#define MTC_DEVSTA       0224
#define MTC_DEVSTM       0230
#define MTC_MOTION       000000001       /* Mag tape unit in motion */
#define MTC_BUSY         000000002       /* Mag tape unit is busy */
#define MTC_START        000000004       /* Start a command */

#define CNTRL           u3
#define STATUS          u4        /* Per drive status bits */
#define CPOS            u5        /* Character position */
#define BPOS            u6        /* Position in buffer */

t_stat         mtc_devio(uint32 dev, uint64 *data);
void           mtc_checkirq(UNIT * uptr);
t_stat         mtc_srv(UNIT *);
t_stat         mtc_boot(int32, DEVICE *);
t_stat         mtc_reset(DEVICE *);
t_stat         mtc_set_dct (UNIT *, int32, CONST char *, void *);
t_stat         mtc_show_dct (FILE *, UNIT *, int32, CONST void *);
t_stat         mtc_attach(UNIT *, CONST char *);
t_stat         mtc_detach(UNIT *);
t_stat         mtc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                  const char *cptr);
const char     *mtc_description (DEVICE *dptr);

uint16          mtc_pia;
uint8           mtc_sel_unit;
uint32          mtc_hold_cmd;
uint32          mtc_status;
int             mtc_dct;  /* DCT Channel and unit */

static uint8    parity_table[64] = {
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
uint8               mtc_buffer[BUFFSIZE];

#if !PDP6
#define D DEV_DIS
#else
#define D 0
#endif

UNIT                mtc_unit[] = {
/* Controller 1 */
    {UDATA(&mtc_srv,  UNIT_MT, 0)},  /* 0 */
    {UDATA(&mtc_srv,  UNIT_MT, 0)},  /* 1 */
    {UDATA(&mtc_srv,  UNIT_MT, 0)},  /* 2 */
    {UDATA(&mtc_srv,  UNIT_MT, 0)},  /* 3 */
    {UDATA(&mtc_srv,  UNIT_MT, 0)},  /* 4 */
    {UDATA(&mtc_srv,  UNIT_MT, 0)},  /* 5 */
    {UDATA(&mtc_srv,  UNIT_MT, 0)},  /* 6 */
    {UDATA(&mtc_srv,  UNIT_MT, 0)},  /* 7 */
};

DIB mtc_dib = {MTC_DEVCTL, 3, &mtc_devio, NULL};

MTAB                mtc_mod[] = {
    {MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL},
    {MTUF_7TRK, 0, "9T", "9T", NULL, NULL},
    {MTUF_7TRK, MTUF_7TRK, "7T", "7T", NULL, NULL},
    {MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LENGTH", "LENGTH",
     &sim_tape_set_capac, &sim_tape_show_capac, NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "DENSITY", "DENSITY",
     &sim_tape_set_dens, &sim_tape_show_dens, NULL},
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "DCT", "DCT",
     &mtc_set_dct, &mtc_show_dct, NULL},
    {0}
};


DEVICE              mtc_dev = {
    "MTC", mtc_unit, NULL, mtc_mod,
    8, 8, 15, 1, 8, 8,
    NULL, NULL, &mtc_reset, &mtc_boot, &mtc_attach, &mtc_detach,
    &mtc_dib, DEV_DISABLE | DEV_DEBUG | DEV_TAPE | D, 0, dev_debug,
    NULL, NULL, &mtc_help, NULL, NULL, &mtc_description
};

t_stat
mtc_devio(uint32 dev, uint64 *data) {
      uint64     res;
      DEVICE    *dptr = &mtc_dev;
      UNIT      *uptr;
      int        u;

      switch(dev & 0374) {
      case MTC_DEVCTL:
          switch(dev & 03) {
          case CONI:
              res = (uint64)((mtc_hold_cmd & CMD_MASK) | (mtc_pia & FLAG_PIA));
              *data = res;
              sim_debug(DEBUG_CONI, dptr, "MTC CONI %03o status %08o %o %o PC=%06o\n",
                          dev, mtc_status, mtc_sel_unit, mtc_pia, PC);
              break;

           case CONO:
              clr_interrupt(MTC_DEVCTL);
              mtc_pia = (uint16)(*data) & (FLAG_PIA);
              mtc_hold_cmd = (*data & CMD_MASK);
              sim_debug(DEBUG_CONO, dptr, "MTC CONO %03o start %o %o%012llo PC=%06o\n",
                            dev, mtc_sel_unit, mtc_pia, *data, PC);
              /* If nop done */
              if ((mtc_hold_cmd & FUNCTION) == 0)
                   break;
              u = (mtc_hold_cmd >> 4) & 07;
              uptr = &mtc_unit[u];
              if ((uptr->flags & UNIT_ATT) != 0) {
                  /* If unit is not busy, give it the command to run */
                  if ((uptr->CNTRL & (MTC_START|MTC_BUSY)) == 0) {
                     sim_debug(DEBUG_CONO, dptr, "MTC CONO %03o starting %o\n", dev, u);
                     mtc_sel_unit = u;
                     mtc_hold_cmd &= ~CMD_FULL;
                     uptr->CNTRL = (mtc_hold_cmd & ~UNIT_NUM) | MTC_START;
                     uptr->STATUS = 0;
                     mtc_status &= IRQ_MASK;   /* Clear all flags but IRQ flags */
                     mtc_status |= TRF_CMD;
                     sim_activate(uptr, 1000);
                  }
              }
              mtc_checkirq(uptr);
              break;

         case DATAI:
              break;

         case DATAO:
              break;
         }
         break;

     case MTC_DEVSTA:
          switch(dev & 03) {
          case CONI:
              uptr = &mtc_unit[mtc_sel_unit];
              res = mtc_status | (uint64)(uptr->STATUS);
              if ((uptr->flags & MTUF_WLK) != 0)
                  res |= WRITE_LOCK;
              if (sim_tape_bot(uptr))
                  res |= BOT_FLAG;
              if (sim_tape_eot(uptr))
                  res |= EOT_FLAG;
              if ((uptr->flags & UNIT_ATT) != 0 && (uptr->CNTRL & (MTC_START|MTC_BUSY)) == 0)
                  res |= TAPE_RDY;
              if ((uptr->flags & UNIT_ATT) == 0 || (uptr->CNTRL & (MTC_START|MTC_MOTION|MTC_BUSY)) == 0)
                  res |= TAPE_FREE;
              *data = res;
              sim_debug(DEBUG_CONI, dptr, "MTC CONI %03o status %012llo %o %08o PC=%06o\n",
                          dev, res, mtc_sel_unit, mtc_status, PC);
              break;

         case CONO:
              mtc_status &= 00777777;
              mtc_status |= (*data & 07) << 18;
              mtc_status |= (*data & (ENB_XNE|ENB_LIE)) << 7;
              if (*data & TAPE_RDY && (mtc_hold_cmd & FUNCTION) == 0) {
                 /* Switch to drive to check status */
                 mtc_sel_unit = (mtc_hold_cmd >> 4) & 07;
              } 
              sim_debug(DEBUG_CONO, dptr, "MTC CONO %03o status %012llo %o %08o PC=%06o\n",
                          dev, *data, mtc_sel_unit, mtc_status, PC);
              uptr = &mtc_unit[mtc_sel_unit];
              mtc_checkirq(uptr);
              break;

         case DATAI:
              break;

         case DATAO:
              break;
         }
         break;

     case MTC_DEVSTM:
         switch(dev & 03) {
         case CONI:
              uptr = &mtc_unit[mtc_sel_unit];
              res = (mtc_sel_unit << 4) | (uptr->CNTRL & FUNC_FIN);
              if (mtc_sel_unit != ((mtc_hold_cmd & UNIT_NUM) >> 4))
                  res |= UNIT_SEL_NEW;
              if (mtc_hold_cmd & CMD_FULL) 
                  res |= CMD_HOLD;
              sim_debug(DEBUG_CONI, dptr, "MTC CONI %03o status2 %012llo %o %08o PC=%06o\n",
                          dev, res, mtc_sel_unit, mtc_status, PC);
              break;

         case CONO:
              break;

         case DATAI:
              break;

         case DATAO:
              break;
         }
         break;
     }
     return SCPE_OK;
}

void
mtc_checkirq(UNIT * uptr)
{
    clr_interrupt(MTC_DEVCTL);
    if ((mtc_status & IRQ_XNE) != 0 && (mtc_status & TRF_CMD) != 0) {
        set_interrupt(MTC_DEVCTL, mtc_pia);
        return;
    }
    if ((mtc_status & IRQ_LIE) != 0 && sim_tape_bot(uptr)) {
        set_interrupt(MTC_DEVCTL, mtc_pia);
        return;
    }
    if ((mtc_status & (EOR_FLAG|IRQ_ERF)) == (EOR_FLAG|IRQ_ERF)) {
        set_interrupt(MTC_DEVCTL, mtc_pia);
        return;
    }
    if ((mtc_status & IRQ_ICE) != 0 &&
           (uptr->CNTRL & (MTC_START|MTC_MOTION|MTC_BUSY)) == 0) {
        set_interrupt(MTC_DEVCTL, mtc_pia);
        return;
    }
#if 0
    /* Need to verify if this is real interrupt or not */
    if ((mtc_status & IRQ_JNU) != 0 &&
        (mtc_hold_cmd & CMD_FULL) == 0 &&
        (uptr->CNTRL & (MTC_START|MTC_BUSY)) == 0) {
           sim_debug(DEBUG_DETAIL, &mtc_dev, "MTC%o jnu %o %08o\n", mtc_sel_unit, mtc_pia, mtc_status);
       set_interrupt(MTC_DEVCTL, mtc_pia);
       return;
    }
#endif
} 

/* Handle processing of tape requests. */
t_stat
mtc_srv(UNIT * uptr)
{
    DEVICE             *dptr = find_dev_from_unit(uptr);
    unsigned int        unit = (uptr - dptr->units) & 7;
    int                 cmd = (uptr->CNTRL & FUNCTION) >> 8;
    t_mtrlnt            reclen;
    t_stat              r = SCPE_ARG;   /* Force error if not set */
    uint8               ch;
    int                 cc;
    uint64              hold_reg;
    int                 cc_max;
    int                 i;

    if ((uptr->CNTRL & (MTC_START|MTC_BUSY)) == 0) {
        if (uptr->STATUS & (PARITY_ERR|PARITY_ERRL|READ_CMP|MIS_CHR|EOF_FLAG)) {
           mtc_hold_cmd &= ~CMD_FULL;
           sim_debug(DEBUG_DETAIL, dptr, "MTC%o stoping %o %08o\n", unit, mtc_pia, mtc_status);
        }

        /* If tape in motion, generate EOR and wait */
        if (uptr->CNTRL & MTC_MOTION) {
            sim_debug(DEBUG_DETAIL, dptr, "MTC%o EOR %08o\n", unit, uptr->STATUS);
            uptr->CNTRL &= ~MTC_MOTION;
            sim_activate(uptr, 500);
            mtc_checkirq(uptr);
            return SCPE_OK;
        }
        sim_debug(DEBUG_DETAIL, dptr, "MTC%o Done %08o %08o\n", unit, mtc_hold_cmd, mtc_status);
 
        /* Check if command pending */
        if ((mtc_hold_cmd & CMD_FULL) != 0) {
            unsigned int u = (mtc_hold_cmd >> 4) & 07;
            sim_debug(DEBUG_DETAIL, dptr, "MTC%o New command %o\n", unit, u);
            /* Is it for me? */
            if (u == unit) {
                mtc_hold_cmd &= ~CMD_FULL;
                uptr->CNTRL = (mtc_hold_cmd & ~UNIT_NUM) | MTC_START;
                uptr->STATUS = 0;
                cmd = (uptr->CNTRL & FUNCTION) >> 8;
                mtc_status |= TRF_CMD;
                sim_activate(uptr, 100);
                mtc_checkirq(uptr);
                return SCPE_OK;
            } else {
                uptr = &mtc_unit[u];
                /* See if other unit can be started */
                mtc_sel_unit = u;
                if ((uptr->CNTRL & (MTC_START|MTC_MOTION|MTC_BUSY)) == 0) {
                   sim_activate(uptr, 100);
                }
                return SCPE_OK;
            }
        } else {
            sim_debug(DEBUG_DETAIL, dptr, "MTC%o stoping %o %08o\n", unit, mtc_pia, mtc_status);
            mtc_checkirq(uptr);
            return SCPE_OK;
        }
    }

    if (uptr->flags & MTUF_7TRK) {
       cc_max = 6;
    } else {
       cc_max = 5;
    }

    if (uptr->CNTRL & MTC_START) 
       uptr->BPOS = 0;


    switch(cmd) {
    case NOP:
    case NOP_1:
         sim_debug(DEBUG_DETAIL, dptr, "MTC%o Idle\n", unit);
         uptr->CNTRL &= ~(MTC_BUSY|MTC_START);
         break;

    case REWIND:
         if (uptr->CNTRL & MTC_START) {
             UNIT          *nuptr = &mtc_unit[(mtc_hold_cmd >> 4) & 07];
             uptr->CNTRL &= ~MTC_START;
             uptr->CNTRL |= MTC_BUSY|MTC_MOTION;
             uptr->STATUS |= REW;
             if ((mtc_hold_cmd & CMD_FULL) && ((mtc_hold_cmd >> 4) & 07) != unit &&
                 (nuptr->CNTRL & (MTC_START|MTC_MOTION|MTC_BUSY)) == 0) {
                 mtc_hold_cmd &= ~CMD_FULL;
                 nuptr->CNTRL = (mtc_hold_cmd & ~UNIT_NUM) | MTC_START;
                 if (mtc_status & IRQ_XNE)
                     set_interrupt(MTC_DEVCTL, mtc_pia);
                 sim_activate(nuptr, 100);
             }
             sim_activate(uptr, 100000);
         } else {
             uptr->CNTRL &= ~(MTC_BUSY|FUNCTION);
             uptr->STATUS &= ~REW;
             sim_activate(uptr, 100);
             sim_debug(DEBUG_DETAIL, dptr, "MTC%o rewind\n", unit);
             sim_tape_rewind(uptr);
         }
         return SCPE_OK;

    case UNLOAD:
         if (uptr->CNTRL & MTC_START) {
             uptr->CNTRL &= ~MTC_START;
             uptr->CNTRL |= MTC_BUSY|MTC_MOTION;
             uptr->STATUS |= REW;
             sim_activate(uptr, 100000);
         } else {
             uptr->CNTRL &= ~(MTC_BUSY);
             uptr->STATUS &= ~REW;
             sim_activate(uptr, 100);
             sim_debug(DEBUG_DETAIL, dptr, "MTC%o unload\n", unit);
             sim_tape_detach(uptr);
         }
         return SCPE_OK;

    case READ_BK:
         if (uptr->CNTRL & MTC_START) {
             uptr->CNTRL &= ~MTC_START;
             if (sim_tape_bot(uptr)) {
                 sim_debug(DEBUG_DETAIL, dptr, "MTC%o read back at bot\n", unit);
                 uptr->STATUS |= ILL_OPR;
                 mtc_status |= EOR_FLAG;
                 break;
             }
             uptr->CNTRL |= MTC_MOTION;
             if ((r = sim_tape_rdrecr(uptr, &mtc_buffer[0], &reclen, BUFFSIZE)) != MTSE_OK) {
                 sim_debug(DEBUG_DETAIL, dptr, "MTC%o read back error %d\n", unit, r);
                 if (r == MTSE_TMK)
                     uptr->STATUS |= EOF_FLAG;
                 else
                     uptr->STATUS |= PARITY_ERRL;
                 mtc_status |= EOR_FLAG;
                 mtc_checkirq(uptr);
                 break;
             }
             uptr->CNTRL |= MTC_BUSY;
             sim_debug(DEBUG_DETAIL, dptr, "MTC%o read back %d\n", unit, reclen);
             uptr->hwmark = reclen;
             uptr->BPOS = reclen-1;
             break;
         }
         hold_reg = 0;
         for (i = cc_max - 1; i >= 0; i--) {
             ch = mtc_buffer[uptr->BPOS];
             if (uptr->flags & MTUF_7TRK) {
                 cc = 6 * (5 - i);
                 if ((((uptr->CNTRL & ODD_PARITY) ? 0x40 : 0) ^
                       parity_table[ch & 0x3f]) != 0) {
                       mtc_status |= PARITY_ERR;
                 }
                 hold_reg |= (uint64)(ch & 0x3f) << cc;
             } else {
                 cc = (8 * (3 - i)) + 4;
                 if (cc < 0)
                     hold_reg |=  (uint64)(ch & 0x0f);
                 else
                     hold_reg |= (uint64)(ch & 0xff) << cc;
             }
             if ((uint32)uptr->BPOS == 0)
                 break;
             uptr->BPOS--;
         }
         if (dct_write(mtc_dct, &hold_reg, cc_max - i) == 0) {
             uptr->CNTRL &= ~(MTC_BUSY);
         }
         break;

    case READ:
         if (uptr->CNTRL & MTC_START) {
             uptr->CNTRL &= ~MTC_START;
             uptr->CNTRL |= MTC_MOTION;
             if ((r = sim_tape_rdrecf(uptr, &mtc_buffer[0], &reclen, BUFFSIZE)) != MTSE_OK) {
                 sim_debug(DEBUG_DETAIL, dptr, "MTC%o read error %d\n", unit, r);
                 if (r == MTSE_TMK)
                     uptr->STATUS |= EOF_FLAG;
                 else if (r == MTSE_EOM)
                     uptr->STATUS |= ILL_OPR;
                 else
                     uptr->STATUS |= PARITY_ERRL;
                 mtc_status |= EOR_FLAG;
                 mtc_checkirq(uptr);
                 break;
             }
             uptr->CNTRL |= MTC_BUSY;
             sim_debug(DEBUG_DETAIL, dptr, "MTC%o read %d\n", unit, reclen);
             uptr->hwmark = reclen;
             uptr->BPOS = 0;
             break;
         }
         hold_reg = 0;
         for (i = 0; i < cc_max; i++) {
             if ((uint32)uptr->BPOS >= uptr->hwmark)
                 break;
             ch = mtc_buffer[uptr->BPOS];
             if (uptr->flags & MTUF_7TRK) {
                 cc = 6 * (5 - i);
                 if ((((uptr->CNTRL & ODD_PARITY) ? 0x40 : 0) ^
                       parity_table[ch & 0x3f]) != 0) {
                       mtc_status |= PARITY_ERR;
                 }
                 hold_reg |= (uint64)(ch & 0x3f) << cc;
             } else {
                 cc = (8 * (3 - i)) + 4;
                 if (cc < 0)
                     hold_reg |=  (uint64)(ch & 0x0f);
                 else
                     hold_reg |= (uint64)(ch & 0xff) << cc;
             }
             uptr->BPOS++;
         }
         sim_debug(DEBUG_DETAIL, dptr, "MTC%o read data %012llo\n", unit, hold_reg);
         if (dct_write(mtc_dct, &hold_reg, i) == 0 ||(uint32)uptr->BPOS >= uptr->hwmark) {
             uptr->CNTRL &= ~(MTC_BUSY);
             mtc_status |= EOR_FLAG;
             mtc_checkirq(uptr);
             sim_debug(DEBUG_DETAIL, dptr, "MTC%o read eor %d %08o\n", unit, uptr->BPOS, mtc_status);
         }
         break;

    case CMP:
    case CMP_1:
         if (uptr->CNTRL & MTC_START) {
             uptr->CNTRL &= ~MTC_START;
             uptr->CNTRL |= MTC_MOTION;
             if ((r = sim_tape_rdrecf(uptr, &mtc_buffer[0], &reclen,
                                  BUFFSIZE)) != MTSE_OK) {
                 sim_debug(DEBUG_DETAIL, dptr, "MTC%o read cmp error %d\n", unit, r);
                 if (r == MTSE_TMK)
                     uptr->STATUS |= EOF_FLAG;
                 else if (r == MTSE_EOM)
                     uptr->STATUS |= ILL_OPR;
                 else
                     uptr->STATUS |= PARITY_ERRL;
                 mtc_status |= EOR_FLAG;
                 mtc_checkirq(uptr);
                 break;
             }
             uptr->CNTRL |= MTC_BUSY;
             sim_debug(DEBUG_DETAIL, dptr, "MTC%o compare %d\n", unit, reclen);
             uptr->hwmark = reclen;
             uptr->BPOS = 0;
             break;
         }
         if (uptr->BPOS >= (int32)uptr->hwmark) {
             uptr->CNTRL &= ~(MTC_BUSY);
         } else if (dct_read(mtc_dct, &hold_reg, cc_max)) {
             for(i = 0; i < cc_max; i++) {
                 if (uptr->flags & MTUF_7TRK) {
                     ch = mtc_buffer[uptr->BPOS];
                     if ((((uptr->CNTRL & ODD_PARITY) ? 0x40 : 0) ^
                          parity_table[ch & 0x3f]) != (ch & 0x40)) {
                          mtc_status |= PARITY_ERR;
                     }
                     mtc_buffer[uptr->BPOS] &= 0x3f;
                     cc = 6 * (6 - i);
                     ch = (hold_reg >> cc) & 0x3f;
                 } else {
                     if ((uptr->CNTRL & ODD_PARITY) == 0)
                          mtc_status |= PARITY_ERR;
                     /* Write next char out */
                     cc = (8 * (3 - i)) + 4;
                     if (cc < 0)
                         ch = hold_reg & 0x0f;
                     else
                         ch = (hold_reg >> cc) & 0xff;
                 }
                 if (mtc_buffer[uptr->BPOS] != ch) {
                     uptr->STATUS |= READ_CMP;
                 }
                 uptr->BPOS++;
             }
         } else {
             uptr->CNTRL &= ~(MTC_BUSY);
             mtc_status |= EOR_FLAG;
             mtc_checkirq(uptr);
         }
         break;

    case WRITE:
    case WRITE_1:
         /* Writing and Type A, request first data word */
         if (uptr->CNTRL & MTC_START) {
             uptr->CNTRL &= ~MTC_START;
             if ((uptr->flags & MTUF_WLK) != 0) {
                 uptr->STATUS |= ILL_OPR;
                 break;
             }
             uptr->CNTRL |= MTC_MOTION|MTC_BUSY;
             sim_debug(DEBUG_EXP, dptr, "MTC%o Init write\n", unit);
             uptr->hwmark = 0;
             uptr->BPOS = 0;
             break;
         }
         /* Force error if we exceed buffer size */
         if (uptr->BPOS >= BUFFSIZE) {
             uptr->CNTRL &= ~(MTC_BUSY);
             mtc_status |= EOR_FLAG;
             mtc_checkirq(uptr);
             break;
         }
         if (dct_read(mtc_dct, &hold_reg, 0)) {
            sim_debug(DEBUG_DETAIL, dptr, "MTC%o Write data %012llo\n", unit, hold_reg);
            for(i = 0; i < cc_max; i++) {
                if (uptr->flags & MTUF_7TRK) {
                    cc = 6 * (6 - i);
                    ch = (hold_reg >> cc) & 0x3f;
                    ch |= ((uptr->CNTRL & ODD_PARITY) ? 0x40 : 0) ^
                              parity_table[ch & 0x3f];
                } else {
                    /* Write next char out */
                    cc = (8 * (3 - i)) + 4;
                    if (cc < 0)
                         ch = hold_reg & 0x0f;
                    else
                         ch = (hold_reg >> cc) & 0xff;
                }
                mtc_buffer[uptr->BPOS] = ch;
                uptr->BPOS++;
                uptr->hwmark = uptr->BPOS;
             }
         } else {
             /* Write out the block */
             reclen = uptr->hwmark;
             r = sim_tape_wrrecf(uptr, &mtc_buffer[0], reclen);
             sim_debug(DEBUG_DETAIL, dptr, "MTC%o Write %d %d\n", unit, reclen, r);
             if (r == MTSE_EOM)
                 uptr->STATUS |= ILL_OPR;
             else if (r != MTSE_OK)
                 uptr->STATUS |= PARITY_ERRL;
             mtc_status |= EOR_FLAG;
             uptr->CNTRL &= ~(MTC_BUSY);
             uptr->BPOS = 0;
             uptr->hwmark = 0;
         }
         break;

    case WTM:
         if (uptr->CNTRL & MTC_START) {
            sim_debug(DEBUG_DETAIL, dptr, "MTC%o WTM\n", unit);
            uptr->CNTRL &= ~MTC_START;
            if ((uptr->flags & MTUF_WLK) != 0) {
                uptr->STATUS |= ILL_OPR;
                mtc_status |= (EOR_FLAG);
                break;
            }
            uptr->CNTRL |= MTC_MOTION;
            r = sim_tape_wrtmk(uptr);
            if (r != MTSE_OK)
                uptr->STATUS |= PARITY_ERRL;
             mtc_status |= EOR_FLAG;
             mtc_checkirq(uptr);
         }
         break;

    case ERG:
         if (uptr->CNTRL & MTC_START) {
             sim_debug(DEBUG_DETAIL, dptr, "MTC%o ERG\n", unit);
             uptr->CNTRL &= ~MTC_START;
             if ((uptr->flags & MTUF_WLK) != 0) {
                 uptr->STATUS |= ILL_OPR;
                 mtc_status |= (EOR_FLAG);
                 break;
             }
             uptr->CNTRL |= MTC_MOTION;
             mtc_status |= EOR_FLAG;
             mtc_checkirq(uptr);
         }
         break;

    case SPC_REV_EOF:
    case SPC_EOF:
    case SPC_REV:
    case SPC_FWD:
         sim_debug(DEBUG_DETAIL, dptr, "MTC%o space %o\n", unit, cmd);
         if (uptr->CNTRL & MTC_START) {
             uptr->CNTRL &= ~MTC_START;
             if ((cmd & 7) == SPC_REV && sim_tape_bot(uptr)) {
                uptr->STATUS |= ILL_OPR;
                break;
             }
             uptr->CNTRL |= MTC_MOTION|MTC_BUSY;
         }
         /* Always skip at least one record */
         if ((cmd & 7) == SPC_FWD)
             r = sim_tape_sprecf(uptr, &reclen);
         else
             r = sim_tape_sprecr(uptr, &reclen);
         switch (r) {
         case MTSE_OK:            /* no error */
              if ((cmd & 010) != 0)
                  break;
              /* Fall through */
         case MTSE_TMK:           /* tape mark */
         case MTSE_BOT:           /* beginning of tape */
         case MTSE_EOM:           /* end of medium */
              /* Stop motion if we recieve any of these */
              uptr->CNTRL &= ~(MTC_BUSY);
              mtc_status |= EOR_FLAG;
              mtc_checkirq(uptr);
         }
         uptr->hwmark = 0;
         sim_activate(uptr, 420 * (reclen/6));
         return SCPE_OK;
    }
    sim_activate(uptr, 420);
    return SCPE_OK;
}

uint64
mtc_read_word(UNIT *uptr) {
     int i,  cc, ch;
     uint64  hold_reg = 0;

     for(i = 0; i <= 4; i++) {
        cc = (8 * (3 - i)) + 4;
        ch = mtc_buffer[uptr->BPOS];
        if (cc < 0)
            hold_reg |=  (uint64)(ch & 0x3f);
        else
            hold_reg |= (uint64)(ch & 0xff) << cc;
        uptr->BPOS++;
     }
     return hold_reg;
}

/* Boot from given device */
t_stat
mtc_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    t_mtrlnt            reclen;
    t_stat              r;
    uint64              hold_reg;
    int                 wc, addr;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    r = sim_tape_rewind(uptr);
    if (r != SCPE_OK)
        return r;
    uptr->CNTRL = 022200; /* Read 800 BPI, Core */
    r = sim_tape_rdrecf(uptr, &mtc_buffer[0], &reclen, BUFFSIZE);
    if (r != SCPE_OK)
        return r;
    uptr->BPOS = 0;
    uptr->hwmark = reclen;

    hold_reg = mtc_read_word(uptr);
    wc = (hold_reg >> 18) & RMASK;
    addr = hold_reg & RMASK;
    while (wc != 0) {
        wc = (wc + 1) & RMASK;
        addr = (addr + 1) & RMASK;
        if ((uint32)uptr->BPOS >= uptr->hwmark) {
            r = sim_tape_rdrecf(uptr, &mtc_buffer[0], &reclen, BUFFSIZE);
            if (r != SCPE_OK)
                return r;
            uptr->BPOS = 0;
            uptr->hwmark = reclen;
        }
        hold_reg = mtc_read_word(uptr);
        if (addr < 020)
           FM[addr] = hold_reg;
        else
           M[addr] = hold_reg;
    }
    if (addr < 020)
        FM[addr] = hold_reg;
    else
        M[addr] = hold_reg;

    PC = hold_reg & RMASK;
    return SCPE_OK;
}

t_stat
mtc_reset(DEVICE * dptr)
{
    int i;
    for (i = 0 ; i < 8; i++) {
        UNIT    *uptr = &mtc_unit[i];

        if (MT_DENS(uptr->dynflags) == MT_DENS_NONE)
                uptr->dynflags = MT_200_VALID | MT_556_VALID;
        uptr->CNTRL = 0;
        sim_cancel(uptr);
    }
    mtc_pia = 0;
    mtc_status = 0;
    mtc_sel_unit = TAPE_FREE|TAPE_RDY;
    return SCPE_OK;
}

/* set DCT channel and unit. */
t_stat
mtc_set_dct (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 dct;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    dct = (int32) get_uint (cptr, 8, 20, &r);
    if (r != SCPE_OK)
        return r;
    mtc_dct = dct;
    return SCPE_OK;
}

t_stat
mtc_show_dct (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   if (uptr == NULL)
      return SCPE_IERR;

   fprintf (st, "DCT=%02o", mtc_dct);
   return SCPE_OK;
}

t_stat
mtc_attach(UNIT * uptr, CONST char *file)
{
    uptr->CNTRL = 0;
    uptr->STATUS = 0;
    return sim_tape_attach_ex(uptr, file, 0, 0);
}

t_stat
mtc_detach(UNIT * uptr)
{
    uptr->CPOS = 0;
    return sim_tape_detach(uptr);
}

t_stat
mtc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Type 516 Magnetic Tape\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.  The\n");
fprintf (st, "bad block option can be used only when a unit is attached to a file.\n");
fprintf (st, "The DTC does support the BOOT command, however this did not work on real PDP6.\n");
sim_tape_attach_help (st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

const char *
mtc_description (DEVICE *dptr)
{
return "Type 516 magnetic tape controller" ;
}

#endif
