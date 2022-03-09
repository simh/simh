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
#define CS1_TRE         0040000         /* Set if errors */
#define CS1_SC          0100000         /* Set if TRE or ATTN */
#define GET_FNC(x)      (((x) >> CS1_V_FNC) & CS1_M_FNC)

#define TUDS            01
#define STATUS          u4              /* Attentions status for device */
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

#define TUER1           02
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

#define TUMR            03
/* TUMR - 03 - maintenace register */

#define TUAS            04
/* TUAS - 04 - attention summary */

#define AS_U0           0000001         /* unit 0 flag */

#define TUDC            05
/* TUDC - 05 - frame count */

#define TUDT            06
/* TUDT - 06 - drive type */

#define TULA            07
/* TULA - 07 - Check Character */

#define TUSN            010
/* TUSN  - 10 - serial number */

#define TUTC            011
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

#define CPOS          u5
#define DATAPTR       u6

uint8         tu_buf[NUM_DEVS_TU][TU_NUMFR];
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
#if KS
extern DEVICE *rh_boot_dev;
extern int     rh_boot_unit;
#endif


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

#if KS
struct rh_if   tu_rh[NUM_DEVS_TU] = {
     { &tu_write, &tu_read},
};

DIB tu_dib[NUM_DEVS_TU] = {
     {0772440, 037, 0224, 6, 3, &uba_rh_read, &uba_rh_write, 0, 0, &tu_rh[0]},
};
#else
struct rh_if  tu_rh[NUM_DEVS_TU] = {
     { &tu_write, &tu_read}
};

DIB tu_dib[] = {
    {RH10_DEV, 1, &rh_devio, &rh_devirq, &tu_rh[0]}
};
#endif

MTAB                tu_mod[] = {
#if KL
    {MTAB_XTD|MTAB_VDV, TYPE_RH10, NULL, "RH10",  &rh_set_type, NULL,
              NULL, "Sets controller to RH10" },
    {MTAB_XTD|MTAB_VDV, TYPE_RH20, "RH20", "RH20", &rh_set_type, &rh_show_type,
              NULL, "Sets controller to RH20"},
#endif
    {MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL},
    {MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LENGTH", "LENGTH",
     &sim_tape_set_capac, &sim_tape_show_capac, NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "DENSITY", "DENSITY",
     &sim_tape_set_dens, &sim_tape_show_dens, NULL},
#if KS
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "addr", "addr",  &uba_set_addr, uba_show_addr,
              NULL, "Sets address of RH11" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "vect", "vect",  &uba_set_vect, uba_show_vect,
              NULL, "Sets vect of RH11" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "br", "br",  &uba_set_br, uba_show_br,
              NULL, "Sets br of RH11" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "ctl", "ctl",  &uba_set_ctl, uba_show_ctl,
              NULL, "Sets uba of RH11" },
#endif
    {0}
};

REG                 tua_reg[] = {
#if !KS
    {ORDATA(IVECT, tu_rh[0].ivect, 18)},
    {FLDATA(IMODE, tu_rh[0].imode, 0)},
    {ORDATA(XFER, tu_rh[0].xfer_drive, 3), REG_HRO},
    {ORDATA(REG, tu_rh[0].reg, 6), REG_RO},
    {ORDATA(CIA, tu_rh[0].cia, 18)},
    {ORDATA(CCW, tu_rh[0].ccw, 18)},
    {ORDATA(DEVNUM, tu_rh[0].devnum, 9), REG_HRO},
#endif
    {ORDATA(FRAME, tu_rh[0].regs[TUDC], 16)},
    {ORDATA(TCR, tu_rh[0].regs[TUTC], 16)},
    {ORDATA(DRIVE, tu_rh[0].drive, 3), REG_HRO},
    {ORDATA(RAE, tu_rh[0].rae, 8), REG_RO},
    {ORDATA(ATTN, tu_rh[0].attn, 8), REG_RO},
    {ORDATA(STATUS, tu_rh[0].status, 18), REG_RO},
    {ORDATA(WCR, tu_rh[0].wcr, 18)},
    {ORDATA(CDA, tu_rh[0].cda, 18)},
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
    uint16        *regs = &rhc->regs[0];
    int            unit = regs[TUTC] & 07;
    UNIT          *uptr = &dptr->units[unit];
    int            i;

    if (rhc->drive != 0 && reg != 04)   /* Only one unit at 0 */
       return -1;

    if ((uptr->CMD & CS1_GO) != 0 || (uptr->STATUS & DS_PIP) != 0) {
       regs[TUER1] |= ER1_RMR;
       return 0;
    }

    switch(reg) {
    case  000:  /* control */
        sim_debug(DEBUG_DETAIL, dptr, "%s%o %d Status=%06o\n",
                             dptr->name, unit, ctlr, uptr->STATUS);
        uptr->CMD = data & 076;
        if ((data & 01) == 0) {
           sim_debug(DEBUG_DETAIL, &tua_dev, "TU%o no go %06o\n", unit, data);
           return 0;                           /* No, nop */
        }

        if ((uptr->flags & UNIT_ATT) == 0) {
            if (GET_FNC(data) == FNC_DCLR) {
                uptr->STATUS = 0;
                rhc->attn = 0;
                for (i = 0; i < NUM_UNITS_TU; i++) {
                    if (dptr->units[i].STATUS & DS_ATA)
                       rhc->attn = 1;
                }
            }
            sim_debug(DEBUG_DETAIL, &tua_dev, "TU%o unattached %06o\n", unit, data);
            return 0;                           /* No, nop */
        }

        switch (GET_FNC(data)) {
        case FNC_NOP:
            break;

        case FNC_PRESET:                      /* read-in preset */
        case FNC_READ:                        /* read */
        case FNC_READREV:                     /* read w/ headers */
            regs[TUDC] = 0;
            regs[TUTC] |= TC_FCS;
             /* Fall through */

        case FNC_WRITE:                       /* write */
        case FNC_SPACEF:                      /* Space forward */
        case FNC_SPACEB:                      /* Space backward */
             if ((regs[TUTC] & TC_FCS) == 0) {
                regs[TUER1] |= ER1_NEF;
                break;
             }
             /* Fall through */

        case FNC_ERASE:                       /* Erase gap */
        case FNC_WTM:                         /* Write tape mark */
        case FNC_WCHK:                        /* write check */
        case FNC_REWIND:                      /* rewind */
        case FNC_UNLOAD:                      /* unload */
        case FNC_WCHKREV:                     /* write w/ headers */
            uptr->CMD |= CS1_GO;
            uptr->STATUS = DS_PIP;
            regs[TUTC] |= TC_ACCL;
            regs[TUER1] = 0;
            rhc->attn = 0;
            for (i = 0; i < NUM_UNITS_TU; i++) {
                if (dptr->units[i].STATUS & DS_ATA)
                   rhc->attn = 1;
            }
            CLR_BUF(uptr);
            uptr->DATAPTR = 0;
            sim_activate(uptr, 100);
            break;

        case FNC_DCLR:                        /* drive clear */
            uptr->CMD &= ~(CS1_GO);
            uptr->STATUS = 0;
            regs[TUER1] = 0;
            regs[TUTC] &= ~TC_FCS;
            rhc->status &= ~PI_ENABLE;
            rhc->attn = 0;
            for (i = 0; i < NUM_UNITS_TU; i++) {
                if (dptr->units[i].STATUS & DS_ATA)
                   rhc->attn = 1;
            }
            break;
        default:
            regs[TUER1] |= ER1_ILF;
            uptr->STATUS = DS_ATA;
            rhc->attn = 1;
            rh_setattn(rhc, 0);
        }
        sim_debug(DEBUG_DETAIL, dptr, "%s%o AStatus=%06o\n", dptr->name, unit,
                                  uptr->CMD);
        return 0;
    case  001:  /* status */
        break;
    case  002:  /* error register 1 */
        regs[TUER1] = data;
        break;
    case  003:  /* maintenance */
        regs[TUMR] = data;
        fprintf(stderr, "TU MR=%06o\r\n", data);
        break;
    case  004:  /* atten summary */
        rhc->attn = 0;
        if (data & 1) {
            for (i = 0; i < NUM_UNITS_TU; i++)
                dptr->units[i].STATUS &= ~DS_ATA;
        }
        break;
    case  005:  /* frame count */
        regs[TUDC] = data;
        regs[TUTC] |= TC_FCS;
        break;
    case  006:  /* drive type */
    case  007:  /* look ahead */
    case  010:  /* look ahead */
        break;
    case  011:  /* tape control register */
        regs[TUTC] = data;
        break;
    default:
#if KS
        return 1;
#else
        regs[TUER1] |= ER1_ILR;
        uptr->STATUS = DS_ATA;
        rhc->attn = 1;
        rhc->rae = 1;
#endif
    }
    return 0;
}

int
tu_read(DEVICE *dptr, struct rh_if *rhc, int reg, uint32 *data) {
    int            ctlr = GET_CNTRL_RH(dptr->units[0].flags);
    uint16        *regs = &rhc->regs[0];
    int            unit = regs[TUTC] & 07;
    UNIT          *uptr = &dptr->units[unit];
    uint32         temp = 0;
    int            i;

    if (rhc->drive != 0 && reg != 4)   /* Only one unit at 0 */
       return -1;

    switch(reg) {
    case  000:  /* control */
        temp = uptr->CMD & 077;
        temp |= CS1_DVA;
        break;
    case  001:  /* status */
        temp = DS_DPR | uptr->STATUS;
        if (regs[TUER1] != 0)
           temp |= DS_ERR;
        if ((uptr->flags & UNIT_ATT) != 0) {
           temp |= DS_MOL;
           if ((regs[TUTC] & TC_DENS) == TC_1600)
              temp |= DS_PES;
           if (uptr->flags & MTUF_WLK)
              temp |= DS_WRL;
           if ((uptr->CMD & (CS1_GO)) == 0 && (uptr->STATUS & (DS_PIP)) == 0)
              temp |= DS_DRY;
           if (sim_tape_bot(uptr))
              temp |= DS_BOT;
           if (sim_tape_eot(uptr))
              temp |= DS_EOT;
           if ((uptr->CMD & CS1_GO) == 0)
              temp |= DS_SDWN;
        }
        break;
    case  002:  /* error register 1 */
        temp = regs[TUER1];
        break;
    case  004:  /* atten summary */
        for (i = 0; i < NUM_UNITS_TU; i++) {
            if (dptr->units[i].STATUS & DS_ATA)
                temp |= 1;
        }
        break;
    case  005:  /* frame count */
        temp = regs[TUDC];
        break;
    case  006:  /* drive type */
        if ((uptr->flags & UNIT_DIS) == 0)
            temp = 0142054;
        break;
    case  010:  /* serial no */
        temp = 020 + (unit + 1);
        break;
    case  011: /* tape control register */
        temp = regs[TUTC];
        break;
    case  003:  /* maintenance */
        temp = regs[TUMR];
        break;
    case  007:  /* look ahead */
        break;
    default:
#if KS
        return 1;
#else
        regs[TUER1] |= ER1_ILR;
        uptr->STATUS |= DS_ATA;
        rhc->attn = 1;
        rhc->rae = 1;
#endif
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
    uint16        *regs = &rhc->regs[0];

    switch (r) {
    case MTSE_OK:            /* no error */
         break;

    case MTSE_TMK:           /* tape mark */
         uptr->STATUS |= DS_TM;
         break;

    case MTSE_WRP:           /* write protected */
         regs[TUER1] |= ER1_NEF;
         uptr->STATUS |= DS_ATA;
         break;

    case MTSE_UNATT:         /* unattached */
    case MTSE_BOT:           /* beginning of tape */
    case MTSE_EOM:           /* end of medium */
         break;

    case MTSE_IOERR:         /* IO error */
    case MTSE_FMT:           /* invalid format */
         regs[TUER1] |= ER1_PEF;
         uptr->STATUS |= DS_ATA;
         break;

    case MTSE_RECE:          /* error in record */
         regs[TUER1] |= ER1_DPAR;
         uptr->STATUS |= DS_ATA;
         break;

    case MTSE_INVRL:         /* invalid rec lnt */
         regs[TUER1] |= ER1_FCE;
         uptr->STATUS |= DS_ATA;
         break;

    }
    if (uptr->STATUS & DS_ATA)
        rh_setattn(rhc, 0);
    if (GET_FNC(uptr->CMD) >= FNC_XFER && uptr->STATUS & (DS_ATA | DS_TM))
        rh_error(rhc);
    uptr->CMD &= ~(CS1_GO);
    uptr->STATUS &= ~(DS_PIP);
    sim_debug(DEBUG_EXP, dptr, "Setting status %d\n", r);
}

/* Handle processing of tape requests. */
t_stat tu_srv(UNIT * uptr)
{
    int           ctlr = GET_CNTRL_RH(uptr->flags);
    int           unit;
    struct rh_if *rhc;
    uint16        *regs;
    DEVICE       *dptr;
    t_stat        r;
    t_mtrlnt      reclen;
    uint8         ch;
    int           cc;
    int           cc_max;

    /* Find dptr, and df10 */
    dptr = tu_devs[ctlr];
    rhc = &tu_rh[ctlr];
    regs = &rhc->regs[0];
    unit = uptr - dptr->units;
    cc_max = (4 + ((regs[TUTC] & TC_FMTSEL) == 0));
    regs[TUTC] &= ~TC_ACCL;
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
         if (uptr->STATUS & CS1_GO) {
             sim_activate(uptr,40000);
             uptr->CMD &= ~(CS1_GO);
         } else {
             sim_debug(DEBUG_DETAIL, dptr, "%s%o rewind done\n", dptr->name, unit);
             uptr->STATUS |= DS_SSC|DS_ATA;
             tu_error(uptr, sim_tape_rewind(uptr));
         }
         return SCPE_OK;

    case FNC_UNLOAD:
         sim_debug(DEBUG_DETAIL, dptr, "%s%o unload\n", dptr->name, unit);
         uptr->STATUS |= DS_SSC|DS_ATA;
         tu_error(uptr, sim_tape_detach(uptr));
         return SCPE_OK;

    case FNC_WCHKREV:
    case FNC_READREV:
         if (BUF_EMPTY(uptr)) {
             uptr->STATUS &= ~DS_PIP;
             if ((r = sim_tape_rdrecr(uptr, &tu_buf[ctlr][0], &reclen,
                                 TU_NUMFR)) != MTSE_OK) {
                 sim_debug(DEBUG_DETAIL, dptr, "%s%o read error %d\n", dptr->name, unit, r);
                 if (r == MTSE_BOT)
                     regs[TUER1] |= ER1_NEF;
                 tu_error(uptr, r);
                 rh_finish_op(rhc, 0);
             } else {
                 sim_debug(DEBUG_DETAIL, dptr, "%s%o read %d\n", dptr->name, unit, reclen);
                 uptr->hwmark = reclen;
                 uptr->DATAPTR = uptr->hwmark-1;
                 uptr->CPOS = cc_max;
                 rhc->buf = 0;
                 sim_activate(uptr, 120);
             }
             return SCPE_OK;
         }
         if (uptr->DATAPTR >= 0) {
             regs[TUDC]++;
             if (regs[TUDC] == 0)
                regs[TUTC] &= ~TC_FCS;
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
             uptr->STATUS &= ~DS_PIP;
             if ((r = sim_tape_rdrecf(uptr, &tu_buf[ctlr][0], &reclen,
                                 TU_NUMFR)) != MTSE_OK) {
                 sim_debug(DEBUG_DETAIL, dptr, "%s%o read error %d\n", dptr->name, unit, r);
                 if (r == MTSE_TMK)
                     regs[TUER1] |= ER1_FCE;
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
             regs[TUDC]++;
             if (regs[TUDC] == 0)
                regs[TUTC] &= ~TC_FCS;
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
             if (regs[TUDC] != 0)
                 regs[TUER1] |= ER1_FCE;
             tu_error(uptr, MTSE_OK);
             (void)rh_blkend(rhc);
             rh_finish_op(rhc, 0);
             return SCPE_OK;
         }
         break;

    case FNC_WRITE:
         if (BUF_EMPTY(uptr)) {
             uptr->STATUS &= ~DS_PIP;
             if (regs[TUDC] == 0) {
                  regs[TUER1] |= ER1_NEF;
                  uptr->STATUS |= DS_ATA;
                  rhc->attn = 1;
                  tu_error(uptr, MTSE_OK);
                  rh_finish_op(rhc, 0);
                  return SCPE_OK;
             }
             if ((uptr->flags & MTUF_WLK) != 0) {
                 tu_error(uptr, MTSE_WRP);
                 rh_finish_op(rhc, 0);
                 return SCPE_OK;
             }
             sim_debug(DEBUG_EXP, dptr, "%s%o Init write\n", dptr->name, unit);
             uptr->hwmark = 0;
             uptr->CPOS = 0;
             uptr->DATAPTR = 0;
             rhc->buf = 0;
         }
         if (regs[TUDC] != 0 && uptr->CPOS == 0 && rh_read(rhc) == 0)
             uptr->CPOS |= 010;

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
         regs[TUDC]++;
         if (regs[TUDC] == 0) {
            uptr->CPOS = 010;
            regs[TUTC] &= ~(TC_FCS);
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
         uptr->STATUS &= ~DS_PIP;
         uptr->STATUS |= DS_ATA;
         if ((uptr->flags & MTUF_WLK) != 0) {
             tu_error(uptr, MTSE_WRP);
         } else {
             tu_error(uptr, sim_tape_wrtmk(uptr));
         }
         sim_debug(DEBUG_DETAIL, dptr, "%s%o WTM\n", dptr->name, unit);
         return SCPE_OK;

    case FNC_ERASE:
         uptr->STATUS &= ~DS_PIP;
         uptr->STATUS |= DS_ATA;
         sim_tape_sprecf(uptr, &reclen);
         if ((uptr->flags & MTUF_WLK) != 0) {
             tu_error(uptr, MTSE_WRP);
         } else {
             tu_error(uptr, sim_tape_wrgap(uptr, 35));
         }
         sim_debug(DEBUG_DETAIL, dptr, "%s%o ERG\n", dptr->name, unit);
         return SCPE_OK;

    case FNC_SPACEF:
    case FNC_SPACEB:
         sim_debug(DEBUG_DETAIL, dptr, "%s%o space %06o %o\n", dptr->name, unit, regs[TUDC], GET_FNC(uptr->CMD));
         uptr->STATUS &= ~DS_PIP;
         /* Always skip at least one record */
         if (GET_FNC(uptr->CMD) == FNC_SPACEF)
             r = sim_tape_sprecf(uptr, &reclen);
         else
             r = sim_tape_sprecr(uptr, &reclen);
         regs[TUDC]++;
         if (regs[TUDC] == 0)
            regs[TUTC] &= ~TC_FCS;
         switch (r) {
         case MTSE_OK:            /* no error */
              break;

         case MTSE_BOT:           /* beginning of tape */
              regs[TUER1] |= ER1_NEF;
              /* Fall Through */

         case MTSE_TMK:           /* tape mark */
         case MTSE_EOM:           /* end of medium */
              if (regs[TUDC] != 0)
                 regs[TUER1] |= ER1_FCE;
              uptr->STATUS |= DS_ATA;
              /* Stop motion if we recieve any of these */
              tu_error(uptr, r);
         sim_debug(DEBUG_DETAIL, dptr, "%s%o space finish %06o %o\n", dptr->name, unit, regs[TUDC], GET_FNC(uptr->CMD));
              return SCPE_OK;
         }
         if (regs[TUDC] == 0) {
            uptr->STATUS |= DS_ATA;
            tu_error(uptr, MTSE_OK);
         sim_debug(DEBUG_DETAIL, dptr, "%s%o space finish1 %06o %o\n", dptr->name, unit, regs[TUDC], GET_FNC(uptr->CMD));
            return SCPE_OK;
         } else {
            sim_activate(uptr, reclen * 100);
         sim_debug(DEBUG_DETAIL, dptr, "%s%o space cont %06o %o\n", dptr->name, unit, regs[TUDC], GET_FNC(uptr->CMD));
         }
         return SCPE_OK;
    }
    sim_activate(uptr, 50);
    return SCPE_OK;
}




t_stat
tu_reset(DEVICE * dptr)
{
    struct rh_if *rhc = &tu_rh[0];
    uint16        *regs = &rhc->regs[0];

    rh_reset(dptr, &tu_rh[0]);
    regs[TUER1] = 0;
    regs[TUTC] = TC_1600;
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
    UNIT           *uptr = &dptr->units[unit_num];
    int             ctlr = GET_CNTRL_RH(uptr->flags);
    struct rh_if   *rhc = &tu_rh[ctlr];
    uint16         *regs = &rhc->regs[0];
    t_mtrlnt        reclen;
    t_stat          r;
    uint32          addr;
    int             wc;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    r = sim_tape_rewind(uptr);
    if (r != SCPE_OK)
        return r;
    uptr->CMD = 0;
#if KS
    /* Skip first file, which is micro code */
    while (r == MTSE_OK)
        r = sim_tape_rdrecf(uptr, &tu_buf[0][0], &reclen, TU_NUMFR);

    if (r != MTSE_TMK)
        return r;
    /* Next read in the boot block */
    r = sim_tape_rdrecf(uptr, &tu_buf[0][0], &reclen, TU_NUMFR);
    if (r != MTSE_OK)
        return r;
    uptr->DATAPTR = 0;
    uptr->hwmark = reclen;
    wc = reclen;

    addr = 01000;
    while (uptr->DATAPTR < wc) {
        tu_read_word(uptr);
        M[addr] = tu_boot_buffer;
        addr ++;
    }
    regs[TUTC] |= unit_num;
    M[036] = rhc->dib->uba_addr | (rhc->dib->uba_ctl << 18);
    M[037] = 0;
    M[040] = regs[TUTC];
    PC = 01000;
    rh_boot_dev = dptr;
    rh_boot_unit = unit_num;
#else
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
    regs[TUTC] |= unit_num;
#endif
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
        uptr->STATUS = DS_ATA|DS_SSC;
        rh_setattn(rhc, 0);
    }
    return r;
}

t_stat
tu_detach(UNIT * uptr)
{
    int          ctlr = GET_CNTRL_RH(uptr->flags);
    struct rh_if *rhc = &tu_rh[ctlr];

    uptr->STATUS = DS_ATA|DS_SSC;
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
#if KS
fprintf (st, "The RH11 is a unibus device, various parameters can be changed on these devices\n");
fprintf (st, "\n The address of the device can be set with: \n");
fprintf (st, "      sim> SET TUA ADDR=octal   default address= 772440\n");
fprintf (st, "\n The interrupt vector can be set with: \n");
fprintf (st, "      sim> SET TUA VECT=octal   default 224\n");
fprintf (st, "\n The interrupt level can be set with: \n");
fprintf (st, "      sim> SET TUA BR=#     # should be between 4 and 7.\n");
fprintf (st, "\n The unibus addaptor that the DZ is on can be set with:\n");
fprintf (st, "      sim> SET TUA CTL=#    # can be either 1 or 3\n");
#endif
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *tu_description (DEVICE *dptr)
{
    return "TU04/05/06/07 Massbus disk controller";
}


#endif
