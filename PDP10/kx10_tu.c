/* ka10_tu.c: Dec RH10 TM03/TU10 tape controller

   Copyright (c) 2017, Richard Cornwell

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
#define CNTRL_V_CTYPE   (MTUF_V_UF)
#define CNTRL_M_CTYPE   7
#define GET_CNTRL(x)    (((x) >> CNTRL_V_CTYPE) & CNTRL_M_CTYPE)
#define CNTRL(x)        (((x) & CNTRL_M_CTYPE) << CNTRL_V_CTYPE)

/* CONI Flags */
#define IADR_ATTN       0000000000040LL   /* Interrupt on attention */
#define IARD_RAE        0000000000100LL   /* Interrupt on register access error */
#define DIB_CBOV        0000000000200LL   /* Control bus overrun */
#define CXR_PS_FAIL     0000000002000LL   /* Power supply fail (not implemented) */
#define CXR_ILC         0000000004000LL   /* Illegal function code */
#define CR_DRE          0000000010000LL   /* Or Data and Control Timeout */
#define DTC_OVER        0000000020000LL   /* DF10 did not supply word on time (not implemented) */
#define CCW_COMP_1      0000000040000LL   /* Control word written. */
#define CXR_CHAN_ER     0000000100000LL   /* Channel Error */
#define CXR_EXC         0000000200000LL   /* Error in drive transfer */
#define CXR_DBPE        0000000400000LL   /* Device Parity error (not implemented) */
#define CXR_NXM         0000001000000LL   /* Channel non-existent memory (not implemented) */
#define CXR_CWPE        0000002000000LL   /* Channel Control word parity error (not implemented) */
#define CXR_CDPE        0000004000000LL   /* Channel Data Parity Error (not implemented) */
#define CXR_SD_RAE      0000200000000LL   /* Register access error */
#define CXR_ILFC        0000400000000LL   /* Illegal CXR function code */
#define B22_FLAG        0004000000000LL   /* 22 bit channel */
#define CC_CHAN_PLS     0010000000000LL   /* Channel transfer pulse (not implemented) */
#define CC_CHAN_ACT     0020000000000LL   /* Channel in use */
#define CC_INH          0040000000000LL   /* Disconnect channel */
#define CB_FULL         0200000000000LL   /* Set when channel buffer is full (not implemented) */
#define AR_FULL         0400000000000LL   /* Set when AR is full (not implemented) */

/* CONO Flags */
#define ATTN_EN         0000000000040LL   /* enable attention interrupt. */
#define REA_EN          0000000000100LL   /* enable register error interrupt */
#define CBOV_CLR        0000000000200LL   /* Clear CBOV */
#define CONT_RESET      0000000002000LL   /* Clear All error bits */
#define ILC_CLR         0000000004000LL   /* Clear ILC and SD RAE */
#define DRE_CLR         0000000010000LL   /* Clear CR_CBTO and CR_DBTO */
#define OVER_CLR        0000000020000LL   /* Clear DTC overrun */
#define WRT_CW          0000000040000LL   /* Write control word */
#define CHN_CLR         0000000100000LL   /* Clear Channel Error */
#define DR_EXC_CLR      0000000200000LL   /* Clear DR_EXC */
#define DBPE_CLR        0000000400000LL   /* Clear CXR_DBPE */

/* DATAO/DATAI */
#define CR_REG          0770000000000LL   /* Register number */
#define LOAD_REG        0004000000000LL   /* Load register */
#define CR_MAINT_MODE   0000100000000LL   /* Maint mode... not implemented */
#define CR_DRIVE        0000007000000LL
#define CR_GEN_EVD      0000000400000LL   /* Enable Parity */
#define CR_DXES         0000000200000LL   /* Disable DXES errors  */
#define CR_INAD         0000000077600LL
#define CR_WTEVM        0000000000100LL   /* Verify Parity */
#define CR_FUNC         0000000000076LL
#define CR_GO           0000000000001LL

#define IRQ_VECT        0000000000177LL   /* Interupt vector */
#define IRQ_KI10        0000002000000LL
#define IRQ_KA10        0000001000000LL

#define CMD             u3
/* u3  low */
/* TUC - 00 - control */

#define CS1_GO          CR_GO           /* go */
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

/* TULA - 07 - look ahead register */

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

struct df10   tu_df10[NUM_DEVS_TU];
int           tu_xfer_drive[NUM_DEVS_TU];
uint8         tu_buf[NUM_DEVS_TU][TU_NUMFR];
int           tu_reg[NUM_DEVS_TU];
int           tu_ivect[NUM_DEVS_TU];
int           tu_imode[NUM_DEVS_TU];
int           tu_drive[NUM_DEVS_TU];
int           tu_rae[NUM_DEVS_TU];
int           tu_attn[NUM_DEVS_TU];
uint16        tu_frame[NUM_DEVS_TU];
uint16        tu_tcr[NUM_DEVS_TU];
extern int    readin_flag;
static uint64 tu_boot_buffer;

t_stat        tu_devio(uint32 dev, uint64 *data);
int           tu_devirq(uint32 dev, int addr);
void          tu_write(int ctlr, int unit, int reg, uint32 data);
uint32        tu_read(int ctlr, int unit, int reg);
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
    { UDATA (&tu_srv, TU_UNIT+CNTRL(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL(0), 0) },
};

DIB tu_dib[] = {
    {RH10_DEV, 1, &tu_devio, &tu_devirq}
};

MTAB                tu_mod[] = {
    {MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL},
    {MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LENGTH", "LENGTH",
     &sim_tape_set_capac, &sim_tape_show_capac, NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "DENSITY", "DENSITY",
     &sim_tape_set_dens, &sim_tape_show_dens, NULL},
    {0}
};

REG                 tua_reg[] = {
    {ORDATA(IVECT, tu_ivect[0], 18)},
    {FLDATA(IMODE, tu_imode[0], 0)},
    {ORDATA(FRAME, tu_frame[0], 16)},
    {ORDATA(TCR, tu_tcr[0], 16)},
    {ORDATA(XFER, tu_xfer_drive[0], 3), REG_HRO},
    {ORDATA(DRIVE, tu_drive[0], 3), REG_HRO},
    {ORDATA(REG, tu_reg[0], 6), REG_RO},
    {ORDATA(RAE, tu_rae[0], 8), REG_RO},
    {ORDATA(ATTN, tu_attn[0], 8), REG_RO},
    {FLDATA(READIN, readin_flag, 0), REG_HRO},
    {ORDATA(STATUS, tu_df10[0].status, 18), REG_RO},
    {ORDATA(CIA, tu_df10[0].cia, 18)},
    {ORDATA(CCW, tu_df10[0].ccw, 18)},
    {ORDATA(WCR, tu_df10[0].wcr, 18)},
    {ORDATA(CDA, tu_df10[0].cda, 18)},
    {ORDATA(DEVNUM, tu_df10[0].devnum, 9), REG_HRO},
    {ORDATA(BUF, tu_df10[0].buf, 36), REG_HRO},
    {ORDATA(NXM, tu_df10[0].nxmerr, 8), REG_HRO},
    {ORDATA(COMP, tu_df10[0].ccw_comp, 8), REG_HRO},
    {BRDATA(BUFF, &tu_buf[0][0], 16, 64, TU_NUMFR), REG_HRO},
    {0}
};  

DEVICE              tua_dev = {
    "TUA", tu_unit, NULL, tu_mod,
    NUM_UNITS_TU, 8, 18, 1, 8, 36,
    NULL, NULL, &tu_reset, &tu_boot, &tu_attach, &tu_detach,
    &tu_dib[0], DEV_DISABLE | DEV_DEBUG | DEV_TAPE, 0, dev_debug,
    NULL, NULL, &tu_help, NULL, NULL, &tu_description
};


DEVICE *tu_devs[] = {
    &tua_dev,
};


t_stat tu_devio(uint32 dev, uint64 *data) {
     int            ctlr = -1;
     DEVICE        *dptr = NULL;
     struct df10   *df10;
     int            drive;

     for (drive = 0; rh[drive].dev_num != 0; drive++) {
        if (rh[drive].dev_num == (dev & 0774)) {
            dptr = rh[drive].dev;
            break;
        }
     }

     if (dptr == NULL)
         return SCPE_OK;
     ctlr = GET_CNTRL(dptr->units[0].flags);
     df10 = &tu_df10[ctlr];
     df10->devnum = dev;
     switch(dev & 3) {
     case CONI:
        *data = df10->status & ~(IADR_ATTN|IARD_RAE);
        if (tu_attn[ctlr] != 0 && (df10->status & IADR_ATTN))
           *data |= IADR_ATTN;
        if (tu_rae[ctlr] != 0 && (df10->status & IARD_RAE))
           *data |= IARD_RAE;
#if KI_22BIT
        *data |= B22_FLAG;
#endif
        sim_debug(DEBUG_CONI, dptr, "TU %03o CONI %06o PC=%o %o\n",
               dev, (uint32)*data, PC, tu_attn[ctlr]);
        return SCPE_OK;

     case CONO:
         clr_interrupt(dev);
         df10->status &= ~07LL;
         df10->status |= *data & (07LL|IADR_ATTN|IARD_RAE);
         /* Clear flags */
         if (*data & (DBPE_CLR|DR_EXC_CLR|CHN_CLR))
            df10->status &= ~(*data & (DBPE_CLR|DR_EXC_CLR|CHN_CLR));
         if (*data & OVER_CLR)
            df10->status &= ~(DTC_OVER);
         if (*data & CBOV_CLR)
            df10->status &= ~(DIB_CBOV);
         if (*data & CXR_ILC)
            df10->status &= ~(CXR_ILFC|CXR_SD_RAE);
         if (*data & WRT_CW)
            df10_writecw(df10);
         if (*data & PI_ENABLE)
            df10->status &= ~PI_ENABLE;
         if (df10->status & PI_ENABLE)
            set_interrupt(dev, df10->status);
         if ((df10->status & IADR_ATTN) != 0 && tu_attn[ctlr] != 0)
            set_interrupt(dev, df10->status);
         sim_debug(DEBUG_CONO, dptr, "TU %03o CONO %06o %d PC=%06o %06o\n",
               dev, (uint32)*data, ctlr, PC, df10->status);
         return SCPE_OK;

     case DATAI:
        *data = 0;
        if (df10->status & BUSY && tu_reg[ctlr] != 04) {
            df10->status |= CC_CHAN_ACT;
            return SCPE_OK;
        }
        if (tu_reg[ctlr] == 040) {
              *data = (uint64)(tu_read(ctlr, tu_drive[ctlr], 0) & 077);
              *data |= ((uint64)(df10->cia)) << 6;
              *data |= ((uint64)(tu_xfer_drive[ctlr])) << 18;
        } else if (tu_reg[ctlr] == 044) {
              *data = (uint64)tu_ivect[ctlr];
              if (tu_imode[ctlr])
                *data |= IRQ_KI10;
              else
                *data |= IRQ_KA10;
        } else if (tu_reg[ctlr] == 054) {
                *data = (uint64)(tu_rae[ctlr]);
        } else if ((tu_reg[ctlr] & 040) == 0) {
              *data = (uint64)(tu_read(ctlr, tu_drive[ctlr], tu_reg[ctlr]) & 0177777);
              *data |= ((uint64)(tu_drive[ctlr])) << 18;
        }
        *data |= ((uint64)(tu_reg[ctlr])) << 30;
        sim_debug(DEBUG_DATAIO, dptr, "TU %03o DATI %012llo, %d %d PC=%06o\n",
                    dev, *data, ctlr, tu_drive[ctlr], PC);
        return SCPE_OK;

     case DATAO:
         sim_debug(DEBUG_DATAIO, dptr, "TU %03o DATO %012llo, %d PC=%06o %06o\n",
                    dev, *data, ctlr, PC, df10->status);
         tu_reg[ctlr] = ((int)(*data >> 30)) & 077;
         if (tu_reg[ctlr] < 040 && tu_reg[ctlr] != 04) {
            tu_drive[ctlr] = (int)(*data >> 18) & 07;
         }
         if (*data & LOAD_REG) {
             if (tu_reg[ctlr] == 040) {
                if ((*data & 1) == 0) {
                   return SCPE_OK;
                }
                if (df10->status & BUSY) {
                    df10->status |= CC_CHAN_ACT;
                    sim_debug(DEBUG_DATAIO, dptr,
                         "TU %03o command busy %012llo, %d[%d] PC=%06o %06o\n",
                         dev, *data, ctlr, tu_drive[ctlr], PC, df10->status);
                    return SCPE_OK;
                }

                df10->status &= ~(1 << df10->ccw_comp);
                df10->status &= ~PI_ENABLE;
                if (((*data >> 1) & 077) < FNC_XFER) {
                   df10->status |= CXR_ILC;
                   df10_setirq(df10);
                   sim_debug(DEBUG_DATAIO, dptr,
                       "TU %03o command abort %012llo, %d[%d] PC=%06o %06o\n",
                       dev, *data, ctlr, tu_drive[ctlr], PC, df10->status);
                   return SCPE_OK;
                }
                /* Check if access error */
                if (tu_rae[ctlr] & (1 << tu_drive[ctlr])) {
                    return SCPE_OK;
                }

                /* Start command */
                df10_setup(df10, (uint32)(*data >> 6));
                tu_xfer_drive[ctlr] = (int)(*data >> 18) & 07;
                tu_write(ctlr, tu_drive[ctlr], 0, (uint32)(*data & 077));
                sim_debug(DEBUG_DATAIO, dptr,
                    "TU %03o command %012llo, %d[%d] PC=%06o %06o\n",
                    dev, *data, ctlr, tu_drive[ctlr], PC, df10->status);
             } else if (tu_reg[ctlr] == 044) {
                /* Set KI10 Irq vector */
                tu_ivect[ctlr] = (int)(*data & IRQ_VECT);
                tu_imode[ctlr] = (*data & IRQ_KI10) != 0;
             } else if (tu_reg[ctlr] == 050) {
                ;    /* Diagnostic access to mass bus. */
             } else if (tu_reg[ctlr] == 054) {
                /* clear flags */
                tu_rae[ctlr] &= ~(*data & 0377);
                if (tu_rae[ctlr] == 0)
                    clr_interrupt(dev);
             } else if ((tu_reg[ctlr] & 040) == 0) {
                tu_drive[ctlr] = (int)(*data >> 18) & 07;
                /* Check if access error */
                if (tu_rae[ctlr] & (1 << tu_drive[ctlr])) {
                    return SCPE_OK;
                }
                tu_drive[ctlr] = (int)(*data >> 18) & 07;
                tu_write(ctlr, tu_drive[ctlr], tu_reg[ctlr] & 037,
                        (int)(*data & 0777777));
             }
         }
         return SCPE_OK;
    }
    return SCPE_OK; /* Unreached */
}

/* Handle KI and KL style interrupt vectors */
int
tu_devirq(uint32 dev, int addr) {
    DEVICE        *dptr = NULL;
    int            drive;

    for (drive = 0; rh[drive].dev_num != 0; drive++) {
       if (rh[drive].dev_num == (dev & 0774)) {
           dptr = rh[drive].dev;
           break;
       }
    }
    if (dptr != NULL) {
        drive = GET_CNTRL(dptr->units[0].flags);
        return (tu_imode[drive] ? tu_ivect[drive] : addr);
    }
    return  addr;
}

void
tu_write(int ctlr, int unit, int reg, uint32 data) {
    UNIT          *uptr = &tu_unit[(ctlr * 8) + (tu_tcr[ctlr] & 07)];
    DEVICE        *dptr = tu_devs[ctlr];
    struct df10   *df10 = &tu_df10[ctlr];
    int            i;

    if (uptr->CMD & CR_GO) {
       uptr->STATUS |= (ER1_RMR);
       return;
    }

    switch(reg) {
    case  000:  /* control */
        sim_debug(DEBUG_DETAIL, dptr, "TUA%o %d Status=%06o\n",
                             unit, ctlr, uptr->STATUS);
        df10->status &= ~(1 << df10->ccw_comp);
        if ((data & 01) != 0 && (uptr->flags & UNIT_ATT) != 0) {
            uptr->CMD = data & 076;
            switch (GET_FNC(data)) {
            case FNC_NOP:
                break;

            case FNC_PRESET:                      /* read-in preset */
            case FNC_READ:                        /* read */
            case FNC_READREV:                     /* read w/ headers */
                tu_frame[ctlr] = 0;
                 /* Fall through */

            case FNC_ERASE:                       /* Erase gap */
            case FNC_WRITE:                       /* write */
            case FNC_WTM:                         /* Write tape mark */
            case FNC_SPACEF:                      /* Space forward */
            case FNC_SPACEB:                      /* Space backward */
            case FNC_WCHK:                        /* write check */
            case FNC_REWIND:                      /* rewind */
            case FNC_UNLOAD:                      /* unload */
            case FNC_WCHKREV:                     /* write w/ headers */
                uptr->CMD  |= CS_PIP|CR_GO;
                uptr->CMD  &= ~CS_TM;
                CLR_BUF(uptr);
                uptr->DATAPTR = 0;
                df10->status &= ~PI_ENABLE;
                sim_activate(uptr, 100);
                break;

            case FNC_DCLR:                        /* drive clear */
                uptr->CMD &= ~(CS_ATA|CR_GO|CS_TM);
                uptr->STATUS = 0;
                tu_attn[ctlr] = 0;
                clr_interrupt(df10->devnum);
                for (i = 0; i < 8; i++) {
                    if (tu_unit[(ctlr * 8) + i].CMD & CS_ATA)
                       tu_attn[ctlr] = 1;
                }
                if ((df10->status & IADR_ATTN) != 0 && tu_attn[ctlr] != 0)
                    df10_setirq(df10);
                break;
            default:
                uptr->STATUS |= (ER1_ILF);
                uptr->CMD |= CS_ATA;
                tu_attn[ctlr] = 1;
                if ((df10->status & IADR_ATTN) != 0)
                    df10_setirq(df10);
            }
            sim_debug(DEBUG_DETAIL, dptr, "TUA%o AStatus=%06o\n", unit,
                                      uptr->CMD);
        }
        return;
    case  001:  /* status */
        break;
    case  002:  /* error register 1 */
        uptr->STATUS &= ~0177777;
        uptr->STATUS |= data;
        break;
    case  003:  /* maintenance */
        break;
    case  004:  /* atten summary */
        tu_attn[ctlr] = 0;
        for (i = 0; i < 8; i++) {
            if (data & (1<<i))
                tu_unit[(ctlr * 8) + i].CMD &= ~CS_ATA;
            if (tu_unit[(ctlr * 8) + i].CMD & CS_ATA)
               tu_attn[ctlr] = 1;
        }
        clr_interrupt(df10->devnum);
        if (((df10->status & IADR_ATTN) != 0 && tu_attn[ctlr] != 0) ||
             (df10->status & PI_ENABLE))
            df10_setirq(df10);
        break;
    case  005:  /* frame count */
        tu_frame[ctlr] = data & 0177777;
        break;
    case  006:  /* drive type */
    case  007:  /* look ahead */
    case  011:  /* tape control register */
        tu_tcr[ctlr]  = data & 0177777 ;
        break;
    default:
        uptr->STATUS |= ER1_ILR;
        uptr->CMD |= CS_ATA;
        tu_attn[ctlr] = 1;
        tu_rae[ctlr] |= (1<<unit);
        if ((df10->status & IADR_ATTN) != 0)
            df10_setirq(df10);
    }
}

uint32
tu_read(int ctlr, int unit, int reg) {
    UNIT          *uptr = &tu_unit[(ctlr * 8) + (tu_tcr[ctlr] & 07)];
    struct df10   *df10 = &tu_df10[ctlr];
    uint32        temp = 0;
    int           i;

    switch(reg) {
    case  000:  /* control */
        temp = uptr->CMD & 076;
        if (uptr->flags & UNIT_ATT)
           temp |= CS1_DVA;
        if (df10->status & BUSY || uptr->CMD & CR_GO)
           temp |= CS1_GO;
        break;
    case  001:  /* status */
        temp = DS_DPR;
        if (tu_attn[ctlr] != 0)
           temp |= DS_ATA;
        if (uptr->CMD & CS_CHANGE)
           temp |= DS_SSC;
        if ((uptr->STATUS & 0177777) != 0)
           temp |= DS_ERR|DS_ATA;
        if ((uptr->flags & UNIT_ATT) != 0) {
           temp |= DS_MOL;
           if (uptr->CMD & CS_TM)
              temp |= DS_TM;
           if (uptr->flags & MTUF_WLK)
              temp |= DS_WRL;
           if ((uptr->CMD & (CS_MOTION|CS_PIP|CR_GO)) == 0)
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
            if (tu_unit[(ctlr * 8) + i].CMD & CS_ATA) {
                temp |= 1 << i;
            }
        }
        break;
    case  005:  /* frame count */
        temp = tu_frame[ctlr];
        break;
    case  006:  /* drive type */
        temp = 040054;
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
        tu_attn[ctlr] = 1;
        tu_rae[ctlr] |= (1<<unit);
        if ((df10->status & IADR_ATTN) != 0)
            df10_setirq(df10);
    }
    return temp;
}


/* Map simH errors into machine errors */
void tu_error(UNIT * uptr, t_stat r)
{
    int          ctlr = GET_CNTRL(uptr->flags);
    DEVICE      *dptr = tu_devs[ctlr];

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
    if (uptr->CMD & CS_ATA) {
        tu_attn[ctlr] = 1;
    }
    uptr->CMD &= ~(CS_MOTION|CS_PIP|CR_GO);
    sim_debug(DEBUG_EXP, dptr, "Setting status %d\n", r);
}

/* Handle processing of tape requests. */
t_stat tu_srv(UNIT * uptr)
{
    int          ctlr = GET_CNTRL(uptr->flags);
    int          unit;
    DEVICE      *dptr;
    struct df10 *df;
    t_stat       r;
    t_mtrlnt     reclen;
    uint8        ch;
    int          cc;
    int          cc_max;

    /* Find dptr, and df10 */
    dptr = tu_devs[ctlr];
    unit = uptr - dptr->units;
    df = &tu_df10[ctlr];
    cc_max = (4 + ((tu_tcr[ctlr] & TC_FMTSEL) == 0));
    if ((uptr->flags & UNIT_ATT) == 0) {
        tu_error(uptr, MTSE_UNATT);      /* attached? */
        df10_setirq(df);
        return SCPE_OK;
    }
    switch (GET_FNC(uptr->CMD)) {
    case FNC_NOP:
    case FNC_DCLR:
          sim_debug(DEBUG_DETAIL, dptr, "TU%o nop\n", unit);
          tu_error(uptr, MTSE_OK);      /* Nop */
          df10_setirq(df);
          return SCPE_OK;

    case FNC_REWIND:
          sim_debug(DEBUG_DETAIL, dptr, "TU%o rewind\n", unit);
          if (uptr->CMD & CR_GO) {
              sim_activate(uptr,40000);
              uptr->CMD |= CS_MOTION;
              uptr->CMD &= ~(CR_GO);
          } else {
             uptr->CMD &= ~(CS_MOTION|CS_PIP);
             uptr->CMD |= CS_CHANGE|CS_ATA;
             tu_attn[ctlr] = 1;
             if ((df->status & IADR_ATTN) != 0)
                 df10_setirq(df);
             tu_error(uptr, sim_tape_rewind(uptr));
          }
          return SCPE_OK;

    case FNC_UNLOAD:
          sim_debug(DEBUG_DETAIL, dptr, "TU%o unload\n", unit);
          uptr->CMD &= ~(CR_GO);
          uptr->CMD |= CS_CHANGE|CS_ATA;
          tu_attn[ctlr] = 1;
          if ((df->status & IADR_ATTN) != 0)
               df10_setirq(df);
          tu_error(uptr, sim_tape_detach(uptr));
          return SCPE_OK;

    case FNC_WCHKREV:
    case FNC_READREV:
          if (BUF_EMPTY(uptr)) {
              uptr->CMD &= ~CS_PIP;
              if ((r = sim_tape_rdrecr(uptr, &tu_buf[ctlr][0], &reclen,
                                  TU_NUMFR)) != MTSE_OK) {
                  sim_debug(DEBUG_DETAIL, dptr, "TU%o read error %d\n", unit, r);
                  if (r == MTSE_BOT)
                      uptr->STATUS |= ER1_NEF;
                  tu_error(uptr, r);
                  df10_finish_op(df, 0);
              } else {
                  sim_debug(DEBUG_DETAIL, dptr, "TU%o read %d\n", unit, reclen);
                  uptr->CMD |= CS_MOTION;
                  uptr->hwmark = reclen;
                  uptr->DATAPTR = uptr->hwmark-1;
                  uptr->CPOS = cc_max;
                  df->buf = 0;
                  sim_activate(uptr, 100);
              }
              return SCPE_OK;
          }
          if (uptr->DATAPTR >= 0) {
              tu_frame[ctlr]++;
              cc = (8 * (3 - uptr->CPOS)) + 4;
              ch = tu_buf[ctlr][uptr->DATAPTR];
              if (cc < 0)
                  df->buf |= (uint64)(ch & 0x0f);
              else
                  df->buf |= (uint64)(ch & 0xff) << cc;
              uptr->DATAPTR--;
              uptr->CPOS--;
              if (uptr->CPOS == 0) {
                  uptr->CPOS = cc_max;
                  if (GET_FNC(uptr->CMD) == FNC_READREV &&
                      df10_write(df) == 0) {
                     tu_error(uptr, MTSE_OK);
                     return SCPE_OK;
                  }
                  sim_debug(DEBUG_DATA, dptr, "TU%o readrev %012llo\n", 
                            unit, df->buf);
                  df->buf = 0;
              }
          } else {
              if (uptr->CPOS != cc_max)
                 df10_write(df);
              tu_error(uptr, MTSE_OK);
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
                  sim_debug(DEBUG_DETAIL, dptr, "TU%o read error %d\n", unit, r);
                  tu_error(uptr, r);
                  df10_finish_op(df, 0);
              } else {
                  sim_debug(DEBUG_DETAIL, dptr, "TU%o read %d\n", unit, reclen);
                  uptr->hwmark = reclen;
                  uptr->DATAPTR = 0;
                  uptr->CPOS = 0;
                  df->buf = 0;
                  sim_activate(uptr, 100);
              }
              return SCPE_OK;
          }
          if ((uint32)uptr->DATAPTR < uptr->hwmark) {
              tu_frame[ctlr]++;
              cc = (8 * (3 - uptr->CPOS)) + 4;
              ch = tu_buf[ctlr][uptr->DATAPTR];
              if (cc < 0)
                  df->buf |= (uint64)(ch & 0x0f);
              else
                  df->buf |= (uint64)(ch & 0xff) << cc;
              uptr->DATAPTR++;
              uptr->CPOS++;
              if (uptr->CPOS == cc_max) {
                  uptr->CPOS = 0;
                  if (GET_FNC(uptr->CMD) == FNC_READ &&
                      df10_write(df) == 0) {
                      tu_error(uptr, MTSE_OK);
                      return SCPE_OK;
                  }
                  sim_debug(DEBUG_DATA, dptr, "TU%o read %012llo\n", 
                            unit, df->buf);
                  df->buf = 0;
              }
          } else {
            if (uptr->CPOS != 0) {
                sim_debug(DEBUG_DATA, dptr, "TU%o read %012llo\n",
                             unit, df->buf);
                df10_write(df);
            }
            tu_error(uptr, MTSE_OK);
            df10_finish_op(df, 0);
            return SCPE_OK;
          }
          break;

    case FNC_WRITE:
         if (BUF_EMPTY(uptr)) {
             uptr->CMD &= ~CS_PIP;
             if (tu_frame[ctlr] == 0) {
                  uptr->STATUS |= ER1_NEF;
                  uptr->CMD &= ~(CR_GO);
                  uptr->CMD |= CS_ATA;
                  tu_attn[ctlr] = 1;
                  tu_error(uptr, MTSE_OK);
                  df10_finish_op(df, 0);
                  return SCPE_OK;
             }
             if ((uptr->flags & MTUF_WLK) != 0) {
                 tu_error(uptr, MTSE_WRP);
                 df10_finish_op(df, 0);
                 return SCPE_OK;
             }
             uptr->CMD |= CS_MOTION;
             sim_debug(DEBUG_EXP, dptr, "TU%o Init write\n", unit);
             uptr->hwmark = 0;
             uptr->CPOS = 0;
             uptr->DATAPTR = 0;
             df->buf = 0;
         }
         if (tu_frame[ctlr] != 0 && uptr->CPOS == 0 && df10_read(df) == 0)
             uptr->CPOS |= 010;

         if ((uptr->CMD & CS_MOTION) != 0) {
             if (uptr->CPOS == 0)
                  sim_debug(DEBUG_DATA, dptr, "TU%o write %012llo\n",
                             unit, df->buf);
             /* Write next char out */
             cc = (8 * (3 - (uptr->CPOS & 07))) + 4;
             if (cc < 0)
                  ch = df->buf & 0x0f;
             else
                  ch = (df->buf >> cc) & 0xff;
             tu_buf[ctlr][uptr->DATAPTR] = ch;
             uptr->DATAPTR++;
             uptr->hwmark = uptr->DATAPTR;
             uptr->CPOS = (uptr->CPOS & 010) | ((uptr->CPOS & 07) + 1);
             if ((uptr->CPOS & 7) == cc_max) {
                uptr->CPOS &= 010;
             }
             tu_frame[ctlr] = 0177777 & (tu_frame[ctlr] + 1);
             if (tu_frame[ctlr] == 0)
                uptr->CPOS = 010;
         }
         if (uptr->CPOS == 010) {
                /* Write out the block */
                reclen = uptr->hwmark;
                r = sim_tape_wrrecf(uptr, &tu_buf[ctlr][0], reclen);
                sim_debug(DEBUG_DETAIL, dptr, "TU%o Write %d %d\n",
                             unit, reclen, uptr->CPOS);
                uptr->DATAPTR = 0;
                uptr->hwmark = 0;
                df10_finish_op(df,0 );
                tu_error(uptr, r); /* Record errors */
                return SCPE_OK;
         }
         break;

    case FNC_WTM:
        if ((uptr->flags & MTUF_WLK) != 0) {
            tu_error(uptr, MTSE_WRP);
        } else {
            tu_error(uptr, sim_tape_wrtmk(uptr));
        }
        uptr->CMD |= CS_ATA;
        tu_attn[ctlr] = 1;
        sim_debug(DEBUG_DETAIL, dptr, "TU%o WTM\n", unit);
        if ((df->status & IADR_ATTN) != 0)
             df10_setirq(df);
        return SCPE_OK;

    case FNC_ERASE:
        if ((uptr->flags & MTUF_WLK) != 0) {
            tu_error(uptr, MTSE_WRP);
        } else {
            tu_error(uptr, sim_tape_wrgap(uptr, 35));
        }
        uptr->CMD |= CS_ATA;
        tu_attn[ctlr] = 1;
        sim_debug(DEBUG_DETAIL, dptr, "TU%o ERG\n", unit);
        if ((df->status & IADR_ATTN) != 0)
             df10_setirq(df);
        return SCPE_OK;

    case FNC_SPACEF:
    case FNC_SPACEB:
        sim_debug(DEBUG_DETAIL, dptr, "TU%o space %o\n", unit, GET_FNC(uptr->CMD));
        if (tu_frame[ctlr] == 0) {
             uptr->STATUS |= ER1_NEF;
             uptr->CMD |= CS_ATA;
             tu_attn[ctlr] = 1;
             tu_error(uptr, MTSE_OK);
             if ((df->status & IADR_ATTN) != 0)
                  df10_setirq(df);
             return SCPE_OK;
        }
        uptr->CMD |= CS_MOTION;
        /* Always skip at least one record */
        if (GET_FNC(uptr->CMD) == FNC_SPACEF)
            r = sim_tape_sprecf(uptr, &reclen);
        else
            r = sim_tape_sprecr(uptr, &reclen);
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
             uptr->CMD &= ~(CR_GO);
             uptr->CMD |= CS_ATA;
             tu_attn[ctlr] = 1;
             /* Stop motion if we recieve any of these */
             tu_error(uptr, r);
             if ((df->status & IADR_ATTN) != 0)
                  df10_setirq(df);
             return SCPE_OK;
        }
        tu_frame[ctlr] = 0177777 & (tu_frame[ctlr] + 1);
        if (tu_frame[ctlr] == 0) {
           tu_error(uptr, MTSE_OK);
           if ((df->status & IADR_ATTN) != 0)
                df10_setirq(df);
           return SCPE_OK;
        } else
           sim_activate(uptr, 5000);
        return SCPE_OK;
    }
    sim_activate(uptr, 200);
    return SCPE_OK;
}




t_stat
tu_reset(DEVICE * dptr)
{
    int ctlr;
    for (ctlr = 0; ctlr < NUM_DEVS_TU; ctlr++) {
        tu_df10[ctlr].devnum = tu_dib[ctlr].dev_num;
        tu_df10[ctlr].nxmerr = 19;
        tu_df10[ctlr].ccw_comp = 14;
        tu_attn[ctlr] = 0;
        tu_rae[ctlr] = 0;
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
    int          ctlr = GET_CNTRL(uptr->flags);
    struct df10 *df;

    /* Find df10 */
    df = &tu_df10[ctlr];

    uptr->CMD = 0;
    uptr->STATUS = 0;
    r = sim_tape_attach_ex(uptr, file, 0, 0);
    if (r == SCPE_OK) {
        uptr->CMD = CS_ATA|CS_CHANGE;
        tu_attn[ctlr] = 1;
        if ((df->status & IADR_ATTN) != 0)
           df10_setirq(df);
    }
    return r;
}

t_stat
tu_detach(UNIT * uptr)
{
    int          ctlr = GET_CNTRL(uptr->flags);
    struct df10 *df;

    /* Find df10 */
    df = &tu_df10[ctlr];
    uptr->STATUS = 0;
    uptr->CMD = CS_ATA|CS_CHANGE;
    tu_attn[ctlr] = 1;
    if ((df->status & IADR_ATTN) != 0)
       df10_setirq(df);
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
