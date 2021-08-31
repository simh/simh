/* kx10_tu.c: DEC Massbus TM03/TU10 tape controller

   Copyright (c) 2017-2020, Richard Cornwell

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
   FITNESS FOR A PARTICULAR PUTUOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "kx10_defs.h"
#include "sim_tape.h"

#ifndef NUM_DEVS_TU
#define NUM_DEVS_TU 0
#endif

#if (NUM_DEVS_TU > 0)

#define NUM_UNITS_TU    8
#define TU_NUMFR        (64*1024)

#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark = 0xFFFFFFFF

/* Flags in the unit flags word */

#define TU_UNIT         UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE

#define CMD             u3
/* u3  low */
/* TUC - 00 - control */

#define CS1_GO          1               /* go */
#define CS1_V_FNC       1               /* function pos */
#define CS1_M_FNC       037             /* function mask */
#define CS1_FNC         (CS1_M_FNC << CS1_V_FNC)
#define  FNC_NOP        000             /* no operation */
#define  FNC_UNLOAD     001             /* unload */
#define  FNC_REWIND     003             /* rewind */
#define  FNC_DCLR       004             /* drive clear */
#define  FNC_PRESET     010             /* read-in preset */
#define  FNC_ERASE      012             /* Erase */
#define  FNC_WTM        013             /* Write Tape Mark */
#define  FNC_SPACEF     014             /* Space record forward */
#define  FNC_SPACEB     015             /* Space record backward */
#define FNC_XFER        024             /* >=? data xfr */
#define  FNC_WCHK       024             /* write check */
#define  FNC_WCHKREV    027             /* write check reverse */
#define  FNC_WRITE      030             /* write */
#define  FNC_READ       034             /* read */
#define  FNC_READREV    037             /* read reverse */
#define CS1_DVA         0004000         /* drive avail NI */
#define GET_FNC(x)      (((x) >> CS1_V_FNC) & CS1_M_FNC)
#define CS_TM           001000          /* Tape mark sensed */
#define CS_MOTION       002000          /* Tape moving */
#define CS_PIP          004000          /* Tape Position command */
#define CS_ATA          010000          /* Tape signals attention */
#define CS_CHANGE       020000          /* Status changed */

#define STATUS          u5
/* u5  low */
/* TUDS - 01 - drive status */

#define DS_SLA          0000001         /* Drive has become ready */
#define DS_BOT          0000002         /* Beginning of tape */
#define DS_TM           0000004         /* Tape mark */
#define DS_IDB          0000010         /* Identification burst */
#define DS_SDWN         0000020         /* Tape stoped */
#define DS_PES          0000040         /* Phase Encoding */
#define DS_SSC          0000100         /* Status change */
#define DS_DRY          0000200         /* drive ready */
#define DS_DPR          0000400         /* drive present */
#define DS_PGM          0001000         /* programable NI */
#define DS_EOT          0002000         /* end of tape */
#define DS_WRL          0004000         /* write locked */
#define DS_MOL          0010000         /* medium online */
#define DS_PIP          0020000         /* pos in progress */
#define DS_ERR          0040000         /* error */
#define DS_ATA          0100000         /* attention active */

/* u5 high */
/* TUER1 - 02 - error status 1 */

#define ER1_ILF         0000001         /* illegal func */
#define ER1_ILR         0000002         /* illegal register */
#define ER1_RMR         0000004         /* reg mod refused */
#define ER1_CPAR        0000010         /* control parity err NI */
#define ER1_FMT         0000020         /* format err */
#define ER1_DPAR        0000040         /* data parity error */
#define ER1_INC         0000100         /* Incorrectable data */
#define ER1_PEF         0000200         /* format error */
#define ER1_NSG         0000400         /* Nonstandard gap NI */
#define ER1_FCE         0001000         /* Frame count error */
#define ER1_ITM         0002000         /* Illegal tape mark */
#define ER1_NEF         0004000         /* Non executable function */
#define ER1_DTE         0010000         /* drive time err NI */
#define ER1_OPI         0020000         /* op incomplete */
#define ER1_UNS         0040000         /* drive unsafe */
#define ER1_DCK         0100000         /* data check NI */

/* TUMR - 03 - maintenace register */

/* TUAS - 04 - attention summary */

#define AS_U0           0000001         /* unit 0 flag */

/* TUDC - 05 - frame count */

/* TUDT - 06 - drive type */

/* TULA - 07 - Check Character */

/* TUSN  - 10 - serial number */

/* TUTC  - 11 - Tape control register */
#define TC_SS     0000007                    /* Slave select mask */
#define TC_EVPAR  0000010                    /* Even parity */
#define TC_FMTSEL 0000360                    /* Format select */
#define TC_10CORE 000                        /* PDP 10 Core */
   /* 4 8 bit chars + 1 4 bit char */
#define TC_15CORE 001                        /* PDP 15 core */
   /* 3 6 bit chars per word */
#define TC_10NORM 003                        /* PDP 10 Compatible */
   /* 4 8 bit chars per word */
#define TC_11NORM 014                        /* PDP 11 Normal */
   /* 2 8 bit chars per word */
#define TC_11CORE 015                        /* PDP 11 Core */
   /* 4 4 bit chars per word */
#define TC_15NORM 016                        /* PDP 15 Normal */
   /* 2 8 bit chars per word */
#define TC_DENS   0003400                    /* Density (ignored) */
#define TC_800    0001400                    /* 800 BPI */
#define TC_1600   0002000                    /* 1600 BPI */
#define TC_EAODTE 0010000                    /* Enable abort */
#define TC_SAC    0020000                    /* Slave address change */
#define TC_FCS    0040000                    /* Frame count status */
#define TC_ACCL   0100000                    /* Acceleration */


/* TUER3 - 15 - error status 3 - more unsafe conditions - unimplemented */

#define CPOS          u4
#define DATAPTR       u6

uint8         tu_buf[NUM_DEVS_TU][TU_NUMFR];
uint16        tu_frame[NUM_DEVS_TU];
uint16        tu_tcr[NUM_DEVS_TU];
static uint64 tu_boot_buffer;

int           tu_write(DEVICE *dptr, struct rh_if *rhc, int reg, uint32 data);
int           tu_read(DEVICE *dptr, struct rh_if *rhc, int reg, uint32 *data);
void          tu_rst(DEVICE *dptr);
t_stat        tu_srv(UNIT *);
t_stat        tu_boot(int32, DEVICE *);
void          tu_ini(UNIT *, t_bool);
t_stat        tu_reset(DEVICE *);
t_stat        tu_attach(UNIT *, CONST char *);
t_stat        tu_detach(UNIT *);
t_stat        tu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                    const char *cptr);
const char    *tu_description (DEVICE *dptr);


UNIT                tu_unit[] = {
/* Controller 1 */
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
};

struct rh_if  tu_rh[NUM_DEVS_TU] = {
     { &tu_write, &tu_read}
};

DIB tu_dib[] = {
    {RH10_DEV, 1, &rh_devio, &rh_devirq, &tu_rh[0]}
};

MTAB                tu_mod[] = {
#if KL
    {MTAB_XTD|MTAB_VDV, TYPE_RH10, NULL, "RH10",  &rh_set_type, NULL,
              NULL, "Sets controller to RH10" },
    {MTAB_XTD|MTAB_VDV, TYPE_RH20, "RH20", "RH20", &rh_set_type, &rh_show_type,
              NULL, "Sets controller to RH20"},
#endif
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write ring in place" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "no Write ring in place" },
    {MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LENGTH", "LENGTH",
     &sim_tape_set_capac, &sim_tape_show_capac, NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "DENSITY", "DENSITY",
     &sim_tape_set_dens, &sim_tape_show_dens, NULL},
    {0}
};

REG                 tua_reg[] = {
    {ORDATA(IVECT, tu_rh[0].ivect, 18)},
    {FLDATA(IMODE, tu_rh[0].imode, 0)},
    {ORDATA(FRAME, tu_frame[0], 16)},
    {ORDATA(TCR, tu_tcr[0], 16)},
    {ORDATA(XFER, tu_rh[0].xfer_drive, 3), REG_HRO},
    {ORDATA(DRIVE, tu_rh[0].drive, 3), REG_HRO},
    {ORDATA(REG, tu_rh[0].reg, 6), REG_RO},
    {ORDATA(RAE, tu_rh[0].rae, 8), REG_RO},
    {ORDATA(ATTN, tu_rh[0].attn, 8), REG_RO},
    {ORDATA(STATUS, tu_rh[0].status, 18), REG_RO},
    {ORDATA(CIA, tu_rh[0].cia, 18)},
    {ORDATA(CCW, tu_rh[0].ccw, 18)},
    {ORDATA(WCR, tu_rh[0].wcr, 18)},
    {ORDATA(CDA, tu_rh[0].cda, 18)},
    {ORDATA(DEVNUM, tu_rh[0].devnum, 9), REG_HRO},
    {ORDATA(BUF, tu_rh[0].buf, 36), REG_HRO},
    {BRDATA(BUFF, tu_buf[0], 16, 8, TU_NUMFR), REG_HRO},
    {0}
};

DEVICE              tua_dev = {
    "TUA", tu_unit, tua_reg, tu_mod,
    NUM_UNITS_TU, 8, 18, 1, 8, 36,
    NULL, NULL, &tu_reset, &tu_boot, &tu_attach, &tu_detach,
    &tu_dib[0], DEV_DISABLE | DEV_DEBUG | DEV_TAPE, 0, dev_debug,
    NULL, NULL, &tu_help, NULL, NULL, &tu_description
};


DEVICE *tu_devs[] = {
    &tua_dev,
};

int
tu_write(DEVICE *dptr, struct rh_if *rhc, int reg, uint32 data) {
    int            ctlr = GET_CNTRL_RH(dptr->units[0].flags);
    int            unit = tu_tcr[ctlr] & 07;
    UNIT          *uptr = &dptr->units[unit];
    int            i;

    if (rhc->drive != 0 && reg != 04)   /* Only one unit at 0 */
       return 1;

    if (uptr->CMD & CS1_GO) {
       uptr->STATUS |= (ER1_RMR);
       return 0;
    }

    switch(reg) {
    case  000:  /* control */
        sim_debug(DEBUG_DETAIL, dptr, "%s%o %d Status=%06o\n",
                             dptr->name, unit, ctlr, uptr->STATUS);
        if ((data & 01) != 0 && (uptr->flags & UNIT_ATT) != 0) {
            uptr->CMD = data & 076;
            switch (GET_FNC(data)) {
            case FNC_NOP:
                break;

            case FNC_PRESET:                      /* read-in preset */
            case FNC_READ:                        /* read */
            case FNC_READREV:                     /* read w/ headers */
                tu_frame[ctlr] = 0;
                tu_tcr[ctlr] |= TC_FCS;
                 /* Fall through */

            case FNC_WRITE:                       /* write */
            case FNC_SPACEF:                      /* Space forward */
            case FNC_SPACEB:                      /* Space backward */
                 if ((tu_tcr[ctlr] & TC_FCS) == 0) {
                    uptr->STATUS |= ER1_NEF;
                    break;
                 }
                 /* Fall through */

            case FNC_ERASE:                       /* Erase gap */
            case FNC_WTM:                         /* Write tape mark */
            case FNC_WCHK:                        /* write check */
            case FNC_REWIND:                      /* rewind */
            case FNC_UNLOAD:                      /* unload */
            case FNC_WCHKREV:                     /* write w/ headers */
                uptr->CMD  |= CS_PIP|CS1_GO;
                rhc->attn = 0;
                for (i = 0; i < 8; i++) {
                    if (dptr->units[i].CMD & CS_ATA)
                       rhc->attn = 1;
                }
                CLR_BUF(uptr);
                uptr->DATAPTR = 0;
                sim_activate(uptr, 100);
                break;

            case FNC_DCLR:                        /* drive clear */
                uptr->CMD &= ~(CS_ATA|CS1_GO|CS_TM);
                uptr->STATUS = 0;
                rhc->status &= ~PI_ENABLE;
                rhc->attn = 0;
                for (i = 0; i < 8; i++) {
                    if (dptr->units[i].CMD & CS_ATA)
                       rhc->attn = 1;
                }
                break;
            default:
                uptr->STATUS |= (ER1_ILF);
                uptr->CMD |= CS_ATA;
                rhc->attn = 1;
            }
            sim_debug(DEBUG_DETAIL, dptr, "%s%o AStatus=%06o\n", dptr->name, unit,
                                      uptr->CMD);
        }
        return 0;
    case  001:  /* status */
        break;
    case  002:  /* error register 1 */
        uptr->STATUS &= ~0177777;
        uptr->STATUS |= data;
        break;
    case  003:  /* maintenance */
        break;
    case  004:  /* atten summary */
        rhc->attn = 0;
        if (data & 1) {
            for (i = 0; i < 8; i++)
                dptr->units[i].CMD &= ~CS_ATA;
        }
        break;
    case  005:  /* frame count */
        tu_frame[ctlr] = data & 0177777;
        tu_tcr[ctlr] |= TC_FCS;
        break;
    case  006:  /* drive type */
    case  007:  /* look ahead */
    case  011:  /* tape control register */
        tu_tcr[ctlr]  = data & 0177777 ;
        break;
    default:
        uptr->STATUS |= ER1_ILR;
        uptr->CMD |= CS_ATA;
        rhc->attn = 1;
        rhc->rae = 1;
    }
    return 0;
}

int
tu_read(DEVICE *dptr, struct rh_if *rhc, int reg, uint32 *data) {
    int            ctlr = GET_CNTRL_RH(dptr->units[0].flags);
    int            unit = tu_tcr[ctlr] & 07;
    UNIT          *uptr = &dptr->units[unit];
    uint32         temp = 0;
    int            i;

    if (rhc->drive != 0 && reg != 4)   /* Only one unit at 0 */
       return 1;

    switch(reg) {
    case  000:  /* control */
        temp = uptr->CMD & 076;
        temp |= CS1_DVA;
        if (uptr->CMD & CS1_GO)
           temp |= CS1_GO;
        break;
    case  001:  /* status */
        temp = DS_DPR;
        if (uptr->CMD & CS_ATA)
           temp |= DS_ATA;
        if (uptr->CMD & CS_CHANGE)
           temp |= DS_SSC;
        if ((uptr->STATUS & 0177777) != 0)
           temp |= DS_ERR;
        if ((uptr->flags & UNIT_ATT) != 0) {
           temp |= DS_MOL;
           if (uptr->CMD & CS_TM)
              temp |= DS_TM;
           if (uptr->flags & MTUF_WRP)
              temp |= DS_WRL;
           if ((uptr->CMD & (CS_MOTION|CS_PIP|CS1_GO)) == 0)
              temp |= DS_DRY;
           if (sim_tape_bot(uptr))
              temp |= DS_BOT;
           if (sim_tape_eot(uptr))
              temp |= DS_EOT;
           if ((uptr->CMD & CS_MOTION) == 0)
              temp |= DS_SDWN;
           if (uptr->CMD & CS_PIP)
              temp |= DS_PIP;
        }
        break;
    case  002:  /* error register 1 */
        temp = uptr->STATUS & 0177777;
        break;
    case  004:  /* atten summary */
        for (i = 0; i < 8; i++) {
            if (dptr->units[i].CMD & CS_ATA)
                temp |= 1;
        }
        break;
    case  005:  /* frame count */
        temp = tu_frame[ctlr];
        break;
    case  006:  /* drive type */
        if ((uptr->flags & UNIT_DIS) == 0)
            temp = 042054;
        break;
    case  011: /* tape control register */
        temp = tu_tcr[ctlr];
        break;
    case  010:  /* serial no */
        temp = 020 + (unit + 1);
        break;
    case  003:  /* maintenance */
    case  007:  /* look ahead */
        break;
    default:
        uptr->STATUS |= (ER1_ILR);
        uptr->CMD |= CS_ATA;
        rhc->attn = 1;
        rhc->rae = 1;
    }
    *data = temp;
    return 0;
}


/* Map simH errors into machine errors */
void tu_error(UNIT * uptr, t_stat r)
{
    int           ctlr = GET_CNTRL_RH(uptr->flags);
    DEVICE       *dptr = tu_devs[ctlr];
    struct rh_if *rhc = &tu_rh[ctlr];

    switch (r) {
    case MTSE_OK:            /* no error */
         break;

    case MTSE_TMK:           /* tape mark */
         uptr->CMD |= CS_TM;
         break;

    case MTSE_WRP:           /* write protected */
         uptr->STATUS |= (ER1_NEF);
         uptr->CMD |= CS_ATA;
         break;

    case MTSE_UNATT:         /* unattached */
    case MTSE_BOT:           /* beginning of tape */
    case MTSE_EOM:           /* end of medium */
         break;

    case MTSE_IOERR:         /* IO error */
    case MTSE_FMT:           /* invalid format */
         uptr->STATUS |= (ER1_PEF);
         uptr->CMD |= CS_ATA;
         break;

    case MTSE_RECE:          /* error in record */
         uptr->STATUS |= (ER1_DPAR);
         uptr->CMD |= CS_ATA;
         break;

    case MTSE_INVRL:         /* invalid rec lnt */
         uptr->STATUS |= (ER1_FCE);
         uptr->CMD |= CS_ATA;
         break;

    }
    if (uptr->CMD & CS_ATA)
        rh_setattn(rhc, 0);
    if (GET_FNC(uptr->CMD) >= FNC_XFER && uptr->CMD & (CS_ATA | CS_TM))
        rh_error(rhc);
    uptr->CMD &= ~(CS_MOTION|CS_PIP|CS1_GO);
    sim_debug(DEBUG_EXP, dptr, "Setting status %d\n", r);
}

/* Handle processing of tape requests. */
t_stat tu_srv(UNIT * uptr)
{
    int           ctlr = GET_CNTRL_RH(uptr->flags);
    int           unit;
    struct rh_if *rhc;
    DEVICE       *dptr;
    t_stat        r;
    t_mtrlnt      reclen;
    uint8         ch;
    int           cc;
    int           cc_max;

    /* Find dptr, and df10 */
    dptr = tu_devs[ctlr];
    rhc = &tu_rh[ctlr];
    unit = uptr - dptr->units;
    cc_max = (4 + ((tu_tcr[ctlr] & TC_FMTSEL) == 0));
    if ((uptr->flags & UNIT_ATT) == 0) {
        tu_error(uptr, MTSE_UNATT);      /* attached? */
        rh_setirq(rhc);
        return SCPE_OK;
    }
    switch (GET_FNC(uptr->CMD)) {
    case FNC_NOP:
    case FNC_DCLR:
         sim_debug(DEBUG_DETAIL, dptr, "%s%o nop\n", dptr->name, unit);
         tu_error(uptr, MTSE_OK);      /* Nop */
         rh_setirq(rhc);
         return SCPE_OK;

    case FNC_REWIND:
         sim_debug(DEBUG_DETAIL, dptr, "%s%o rewind\n", dptr->name, unit);
         if (uptr->CMD & CS1_GO) {
             sim_activate(uptr,40000);
             uptr->CMD |= CS_MOTION;
             uptr->CMD &= ~(CS1_GO);
         } else {
             uptr->CMD &= ~(CS_MOTION|CS_PIP);
             uptr->CMD |= CS_CHANGE|CS_ATA;
             rh_setattn(rhc, 0);
             (void)sim_tape_rewind(uptr);
         }
         return SCPE_OK;

    case FNC_UNLOAD:
         sim_debug(DEBUG_DETAIL, dptr, "%s%o unload\n", dptr->name, unit);
         uptr->CMD |= CS_CHANGE|CS_ATA;
         tu_error(uptr, sim_tape_detach(uptr));
         return SCPE_OK;

    case FNC_WCHKREV:
    case FNC_READREV:
         if (BUF_EMPTY(uptr)) {
             uptr->CMD &= ~CS_PIP;
             if ((r = sim_tape_rdrecr(uptr, &tu_buf[ctlr][0], &reclen,
                                 TU_NUMFR)) != MTSE_OK) {
                 sim_debug(DEBUG_DETAIL, dptr, "%s%o read error %d\n", dptr->name, unit, r);
                 if (r == MTSE_BOT)
                     uptr->STATUS |= ER1_NEF;
                 tu_error(uptr, r);
                 rh_finish_op(rhc, 0);
             } else {
                 sim_debug(DEBUG_DETAIL, dptr, "%s%o read %d\n", dptr->name, unit, reclen);
                 uptr->CMD |= CS_MOTION;
                 uptr->hwmark = reclen;
                 uptr->DATAPTR = uptr->hwmark-1;
                 uptr->CPOS = cc_max;
                 rhc->buf = 0;
                 sim_activate(uptr, 120);
             }
             return SCPE_OK;
         }
         if (uptr->DATAPTR >= 0) {
             tu_frame[ctlr]++;
             cc = (8 * (3 - uptr->CPOS)) + 4;
             ch = tu_buf[ctlr][uptr->DATAPTR];
             if (cc < 0)
                 rhc->buf |= (uint64)(ch & 0x0f);
             else
                 rhc->buf |= (uint64)(ch & 0xff) << cc;
             uptr->DATAPTR--;
             uptr->CPOS--;
             if (uptr->CPOS == 0) {
                 uptr->CPOS = cc_max;
                 if (GET_FNC(uptr->CMD) == FNC_READREV && rh_write(rhc) == 0) {
                    tu_error(uptr, MTSE_OK);
                    rh_finish_op(rhc, 0);
                    return SCPE_OK;
                 }
                 sim_debug(DEBUG_DATA, dptr, "%s%o readrev %012llo\n",
                           dptr->name, unit, rhc->buf);
                 rhc->buf = 0;
             }
         } else {
             if (uptr->CPOS != cc_max)
                 rh_write(rhc);
             (void)rh_blkend(rhc);
             tu_error(uptr, MTSE_OK);
             rh_finish_op(rhc, 0);
             return SCPE_OK;
         }
         break;

    case FNC_WCHK:
    case FNC_READ:
         if (BUF_EMPTY(uptr)) {
             uptr->CMD &= ~CS_PIP;
             uptr->CMD |= CS_MOTION;
             if ((r = sim_tape_rdrecf(uptr, &tu_buf[ctlr][0], &reclen,
                                 TU_NUMFR)) != MTSE_OK) {
                 sim_debug(DEBUG_DETAIL, dptr, "%s%o read error %d\n", dptr->name, unit, r);
                 if (r == MTSE_TMK)
                     uptr->STATUS |= ER1_FCE;
                 tu_error(uptr, r);
                 rh_finish_op(rhc, 0);
             } else {
                 sim_debug(DEBUG_DETAIL, dptr, "%s%o read %d %d\n", dptr->name,  unit, reclen, uptr->pos);
                 uptr->hwmark = reclen;
                 uptr->DATAPTR = 0;
                 uptr->CPOS = 0;
                 rhc->buf = 0;
                 sim_activate(uptr, 120);
             }
             return SCPE_OK;
         }
         if ((uint32)uptr->DATAPTR < uptr->hwmark) {
             tu_frame[ctlr]++;
             cc = (8 * (3 - uptr->CPOS)) + 4;
             ch = tu_buf[ctlr][uptr->DATAPTR];
             if (cc < 0)
                 rhc->buf |= (uint64)(ch & 0x0f);
             else
                 rhc->buf |= (uint64)(ch & 0xff) << cc;
             uptr->DATAPTR++;
             uptr->CPOS++;
             if (uptr->CPOS == cc_max) {
                 uptr->CPOS = 0;
                 if (GET_FNC(uptr->CMD) == FNC_READ && rh_write(rhc) == 0) {
                     tu_error(uptr, MTSE_OK);
                     if ((uint32)uptr->DATAPTR == uptr->hwmark)
                         (void)rh_blkend(rhc);
                     rh_finish_op(rhc, 0);
                     return SCPE_OK;
                 }
                 sim_debug(DEBUG_DATA, dptr, "%s%o read %012llo %d\n",
                           dptr->name, unit, rhc->buf, uptr->DATAPTR);
                 rhc->buf = 0;
             }
         } else {
             if (uptr->CPOS != 0) {
                 sim_debug(DEBUG_DATA, dptr, "%s%o readf %012llo\n",
                              dptr->name, unit, rhc->buf);
                 rh_write(rhc);
             }
             if (tu_frame[ctlr] != 0)
                 uptr->STATUS |= ER1_FCE;
             tu_error(uptr, MTSE_OK);
             (void)rh_blkend(rhc);
             rh_finish_op(rhc, 0);
             return SCPE_OK;
         }
         break;

    case FNC_WRITE:
         if (BUF_EMPTY(uptr)) {
             uptr->CMD &= ~CS_PIP;
             if (tu_frame[ctlr] == 0) {
                  uptr->STATUS |= ER1_NEF;
                  uptr->CMD |= CS_ATA;
                  rhc->attn = 1;
                  tu_error(uptr, MTSE_OK);
                  rh_finish_op(rhc, 0);
                  return SCPE_OK;
             }
             if ((uptr->flags & MTUF_WRP) != 0) {
                 tu_error(uptr, MTSE_WRP);
                 rh_finish_op(rhc, 0);
                 return SCPE_OK;
             }
             uptr->CMD |= CS_MOTION;
             sim_debug(DEBUG_EXP, dptr, "%s%o Init write\n", dptr->name, unit);
             uptr->hwmark = 0;
             uptr->CPOS = 0;
             uptr->DATAPTR = 0;
             rhc->buf = 0;
         }
         if (tu_frame[ctlr] != 0 && uptr->CPOS == 0 && rh_read(rhc) == 0)
             uptr->CPOS |= 010;

         if ((uptr->CMD & CS_MOTION) != 0) {
             if (uptr->CPOS == 0)
                  sim_debug(DEBUG_DATA, dptr, "%s%o write %012llo\n",
                             dptr->name, unit, rhc->buf);
             /* Write next char out */
             cc = (8 * (3 - (uptr->CPOS & 07))) + 4;
             if (cc < 0)
                  ch = rhc->buf & 0x0f;
             else
                  ch = (rhc->buf >> cc) & 0xff;
             tu_buf[ctlr][uptr->DATAPTR] = ch;
             uptr->DATAPTR++;
             uptr->hwmark = uptr->DATAPTR;
             uptr->CPOS = (uptr->CPOS & 010) | ((uptr->CPOS & 07) + 1);
             if ((uptr->CPOS & 7) == cc_max) {
                uptr->CPOS &= 010;
             }
             tu_frame[ctlr] = 0177777 & (tu_frame[ctlr] + 1);
             if (tu_frame[ctlr] == 0) {
                uptr->CPOS = 010;
                tu_tcr[ctlr] &= ~(TC_FCS);
             }
         }
         if (uptr->CPOS == 010) {
             /* Write out the block */
             reclen = uptr->hwmark;
             r = sim_tape_wrrecf(uptr, &tu_buf[ctlr][0], reclen);
             sim_debug(DEBUG_DETAIL, dptr, "%s%o Write %d %d\n",
                          dptr->name, unit, reclen, uptr->CPOS);
             uptr->DATAPTR = 0;
             uptr->hwmark = 0;
             (void)rh_blkend(rhc);
             tu_error(uptr, r); /* Record errors */
             rh_finish_op(rhc,0 );
             return SCPE_OK;
         }
         break;

    case FNC_WTM:
         uptr->CMD &= ~CS_PIP;
         uptr->CMD |= CS_ATA;
         if ((uptr->flags & MTUF_WRP) != 0) {
             tu_error(uptr, MTSE_WRP);
         } else {
             tu_error(uptr, sim_tape_wrtmk(uptr));
         }
         sim_debug(DEBUG_DETAIL, dptr, "%s%o WTM\n", dptr->name, unit);
         return SCPE_OK;

    case FNC_ERASE:
         uptr->CMD &= ~CS_PIP;
         uptr->CMD |= CS_ATA;
         if ((uptr->flags & MTUF_WRP) != 0) {
             tu_error(uptr, MTSE_WRP);
         } else {
             tu_error(uptr, sim_tape_wrgap(uptr, 35));
         }
         sim_debug(DEBUG_DETAIL, dptr, "%s%o ERG\n", dptr->name, unit);
         return SCPE_OK;

    case FNC_SPACEF:
    case FNC_SPACEB:
         sim_debug(DEBUG_DETAIL, dptr, "%s%o space %o\n", dptr->name, unit, GET_FNC(uptr->CMD));
         uptr->CMD &= ~CS_PIP;
         if (tu_frame[ctlr] == 0) {
              uptr->STATUS |= ER1_NEF;
              uptr->CMD |= CS_ATA;
              tu_error(uptr, MTSE_OK);
              return SCPE_OK;
         }
         uptr->CMD |= CS_MOTION;
         /* Always skip at least one record */
         if (GET_FNC(uptr->CMD) == FNC_SPACEF)
             r = sim_tape_sprecf(uptr, &reclen);
         else
             r = sim_tape_sprecr(uptr, &reclen);
         tu_frame[ctlr] = 0177777 & (tu_frame[ctlr] + 1);
         switch (r) {
         case MTSE_OK:            /* no error */
              break;

         case MTSE_BOT:           /* beginning of tape */
              uptr->STATUS |= ER1_NEF;
              /* Fall Through */

         case MTSE_TMK:           /* tape mark */
         case MTSE_EOM:           /* end of medium */
              if (tu_frame[ctlr] != 0)
                 uptr->STATUS |= ER1_FCE;
              else
                 tu_tcr[ctlr] &= ~(TC_FCS);
              uptr->CMD |= CS_ATA;
              /* Stop motion if we recieve any of these */
              tu_error(uptr, r);
              return SCPE_OK;
         }
         if (tu_frame[ctlr] == 0) {
            uptr->CMD |= CS_ATA;
            tu_error(uptr, MTSE_OK);
            return SCPE_OK;
         } else {
            tu_tcr[ctlr] &= ~(TC_FCS);
            sim_activate(uptr, reclen * 100);
         }
         return SCPE_OK;
    }
    sim_activate(uptr, 50);
    return SCPE_OK;
}




t_stat
tu_reset(DEVICE * dptr)
{
    int ctlr;
    for (ctlr = 0; ctlr < NUM_DEVS_TU; ctlr++) {
        tu_rh[ctlr].attn = 0;
        tu_rh[ctlr].rae = 0;
    }
    return SCPE_OK;
}

void tu_read_word(UNIT *uptr) {
     int i, cc, ch;

     tu_boot_buffer = 0;
     for(i = 0; i <= 4; i++) {
        cc = (8 * (3 - i)) + 4;
        ch = tu_buf[0][uptr->DATAPTR];
        if (cc < 0)
            tu_boot_buffer |=  (uint64)(ch & 0x0f);
        else
            tu_boot_buffer |= (uint64)(ch & 0xff) << cc;
        uptr->DATAPTR++;
     }
}

/* Boot from given device */
t_stat
tu_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    t_mtrlnt            reclen;
    t_stat              r;
    uint32              addr;
    int                 wc;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    r = sim_tape_rewind(uptr);
    if (r != SCPE_OK)
        return r;
    r = sim_tape_rdrecf(uptr, &tu_buf[0][0], &reclen, TU_NUMFR);
    if (r != SCPE_OK)
        return r;
    uptr->DATAPTR = 0;
    uptr->hwmark = reclen;

    tu_read_word(uptr);
    wc = (tu_boot_buffer >> 18) & RMASK;
    addr = tu_boot_buffer & RMASK;
    while (wc != 0) {
        wc = (wc + 1) & RMASK;
        addr = (addr + 1) & RMASK;
        if ((uint32)uptr->DATAPTR >= uptr->hwmark) {
            r = sim_tape_rdrecf(uptr, &tu_buf[0][0], &reclen, TU_NUMFR);
            if (r != SCPE_OK)
                return r;
            uptr->DATAPTR = 0;
            uptr->hwmark = reclen;
        }
        tu_read_word(uptr);
        if (addr < 020)
           FM[addr] = tu_boot_buffer;
        else
           M[addr] = tu_boot_buffer;
    }
    if (addr < 020)
        FM[addr] = tu_boot_buffer;
    else
        M[addr] = tu_boot_buffer;

    PC = tu_boot_buffer & RMASK;
    return SCPE_OK;
}


t_stat
tu_attach(UNIT * uptr, CONST char *file)
{   t_stat   r;
    int          ctlr = GET_CNTRL_RH(uptr->flags);
    struct rh_if *rhc = &tu_rh[ctlr];

    uptr->CMD = 0;
    uptr->STATUS = 0;
    r = sim_tape_attach_ex(uptr, file, 0, 0);
    if (r == SCPE_OK && (sim_switches & SIM_SW_REST) == 0) {
        uptr->CMD = CS_ATA|CS_CHANGE;
        rh_setattn(rhc, 0);
    }
    return r;
}

t_stat
tu_detach(UNIT * uptr)
{
    int          ctlr = GET_CNTRL_RH(uptr->flags);
    struct rh_if *rhc = &tu_rh[ctlr];

    /* Find df10 */
    uptr->STATUS = 0;
    uptr->CMD = CS_ATA|CS_CHANGE;
    rh_setattn(rhc, 0);
    return sim_tape_detach(uptr);
}



t_stat tu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "TU Tape Drives with TM03 formatter. (TU)\n\n");
fprintf (st, "The TU controller implements the Massbus tape formatter the TM03. TU\n");
fprintf (st, "options include the ability to set units write enabled or write locked\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.\n");
fprintf (st, "The TU device supports the BOOT command.\n");
sim_tape_attach_help (st, dptr, uptr, flag, cptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *tu_description (DEVICE *dptr)
{
    return "TU04/05/06/07 Massbus disk controller";
}


#endif
