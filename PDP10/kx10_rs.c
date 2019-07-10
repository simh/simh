/* ka10_rs.c: Dec RH10 RS04

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
   FITNESS FOR A PARTICULAR PURSOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "kx10_defs.h"

#ifndef NUM_DEVS_RS
#define NUM_DEVS_RS 0
#endif

#if (NUM_DEVS_RS > 0)

#define RS_NUMWD        128     /* 36bit words/sec */
#define NUM_UNITS_RS    8

/* Flags in the unit flags word */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* disk type */
#define UNIT_M_DTYPE    7
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define DTYPE(x)        (((x) & UNIT_M_DTYPE) << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */
#define CNTRL_V_CTYPE   (UNIT_V_UF + 4)
#define CNTRL_M_CTYPE   7
#define GET_CNTRL(x)    (((x) >> CNTRL_V_CTYPE) & CNTRL_M_CTYPE)
#define CNTRL(x)        (((x) & CNTRL_M_CTYPE) << CNTRL_V_CTYPE)

/* Parameters in the unit descriptor */


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
/* RSC - 00 - control */

#define CS1_GO          CR_GO           /* go */
#define CS1_V_FNC       1               /* function pos */
#define CS1_M_FNC       037             /* function mask */
#define CS1_FNC         (CS1_M_FNC << CS1_V_FNC)
#define  FNC_NOP        000             /* no operation */
#define  FNC_DCLR       004             /* drive clear */
#define  FNC_PRESET     010             /* read-in preset */
#define  FNC_SEARCH     014             /* search */
#define FNC_XFER        024             /* >=? data xfr */
#define  FNC_WCHK       024             /* write check */
#define  FNC_WRITE      030             /* write */
#define  FNC_READ       034             /* read */
#define CS1_DVA         0004000         /* drive avail NI */
#define GET_FNC(x)      (((x) >> CS1_V_FNC) & CS1_M_FNC)

/* u3  low */
/* RSDS - 01 - drive status */

#define DS_VV           0000000         /* volume valid */
#define DS_DRY          0000200         /* drive ready */
#define DS_DPR          0000400         /* drive present */
#define DS_PGM          0001000         /* programable NI */
#define DS_LST          0002000         /* last sector */
#define DS_WRL          0004000         /* write locked */
#define DS_MOL          0010000         /* medium online */
#define DS_PIP          0020000         /* pos in progress */
#define DS_ERR          0040000         /* error */
#define DS_ATA          0100000         /* attention active */
#define DS_MBZ          0000076

/* u3 high */
/* RSER1 - 02 - error status 1 */

#define ER1_ILF         0000001         /* illegal func */
#define ER1_ILR         0000002         /* illegal register */
#define ER1_RMR         0000004         /* reg mod refused */
#define ER1_PAR         0000010         /* parity err */
#define ER1_FER         0000020         /* format err NI */
#define ER1_WCF         0000040         /* write clk fail NI */
#define ER1_ECH         0000100         /* ECC hard err NI */
#define ER1_HCE         0000200         /* hdr comp err NI */
#define ER1_HCR         0000400         /* hdr CRC err NI */
#define ER1_AOE         0001000         /* addr ovflo err */
#define ER1_IAE         0002000         /* invalid addr err */
#define ER1_WLE         0004000         /* write lock err */
#define ER1_DTE         0010000         /* drive time err NI */
#define ER1_OPI         0020000         /* op incomplete */
#define ER1_UNS         0040000         /* drive unsafe */
#define ER1_DCK         0100000         /* data check NI */

/* RSMR - 03 - maintenace register */

/* RSAS - 04 - attention summary */

#define AS_U0           0000001         /* unit 0 flag */

#define DA              u4
/* u4 high */
/* RSDC - 05 - desired sector */

#define DA_V_SC         0               /* sector pos */
#define DA_M_SC         077             /* sector mask */
#define DA_V_SF         6               /* track pos */
#define DA_M_SF         077             /* track mask */
#define DA_MBZ          0170000
#define GET_SC(x)       (((x) >> DA_V_SC) & DA_M_SC)
#define GET_SF(x)       (((x) >> DA_V_SF) & DA_M_SF)

/* RSDT - 06 - drive type */

/* RSLA - 07 - look ahead register */

#define LA_V_SC         6                               /* sector pos */

#define GET_DA(c,d)     (((GET_SF (c)) * rs_drv_tab[d].sect) + GET_SC (c))

#define DATAPTR         u6

/* This controller supports many different disk drive types.  These drives
   are operated in 576 bytes/sector (128 36b words/sector) mode, which gives
   them somewhat different geometry from the PDP-11 variants:

   type         #sectors/       #surfaces/
                 surface         cylinder

   RS03         32              64
   RS04         32              64

   In theory, each drive can be a different type.  The size field in
   each unit selects the drive capacity for each drive and thus the
   drive type.  DISKS MUST BE DECLARED IN ASCENDING SIZE.

*/

#define RS03_DTYPE      0
#define RS03_SECT       64
#define RS03_SURF       32
#define RS03_DEV        020002
#define RS03_SIZE       (RS03_SECT * RS03_SURF * RS_NUMWD)

#define RS04_DTYPE      1
#define RS04_SECT       64
#define RS04_SURF       32
#define RS04_DEV        020003
#define RS04_SIZE       (RS04_SECT * RS04_SURF * RS_NUMWD)


struct drvtyp {
    int32       sect;                                   /* sectors */
    int32       surf;                                   /* surfaces */
    int32       size;                                   /* #blocks */
    int32       devtype;                                /* device type */
    };

struct drvtyp rs_drv_tab[] = {
    { RS03_SECT, RS03_SURF, RS03_SIZE, RS03_DEV },
    { RS04_SECT, RS04_SURF, RS04_SIZE, RS04_DEV },
    { 0 }
    };


struct df10   rs_df10[NUM_DEVS_RS];
uint32        rs_xfer_drive[NUM_DEVS_RS];
uint64        rs_buf[NUM_DEVS_RS][RS_NUMWD];
int           rs_reg[NUM_DEVS_RS];
int           rs_ivect[NUM_DEVS_RS];
int           rs_imode[NUM_DEVS_RS];
int           rs_drive[NUM_DEVS_RS];
int           rs_rae[NUM_DEVS_RS];
int           rs_attn[NUM_DEVS_RS];
extern int    readin_flag;

t_stat        rs_devio(uint32 dev, uint64 *data);
int           rs_devirq(uint32 dev, int addr);
void          rs_write(int ctlr, int unit, int reg, uint32 data);
uint32        rs_read(int ctlr, int unit, int reg);
t_stat        rs_svc(UNIT *);
t_stat        rs_boot(int32, DEVICE *);
void          rs_ini(UNIT *, t_bool);
t_stat        rs_reset(DEVICE *);
t_stat        rs_attach(UNIT *, CONST char *);
t_stat        rs_detach(UNIT *);
t_stat        rs_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat        rs_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                    const char *cptr);
const char    *rs_description (DEVICE *dptr);


UNIT                rs_unit[] = {
/* Controller 1 */
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL(0), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL(0), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL(0), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL(0), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL(0), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL(0), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL(0), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL(0), RS04_SIZE) },
};

DIB rs_dib[] = {
    {RH10_DEV, 1, &rs_devio, &rs_devirq}
};

MTAB                rs_mod[] = {
    {UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL},
    {UNIT_DTYPE, (RS03_DTYPE << UNIT_V_DTYPE), "RS03", "RS03", &rs_set_type },
    {UNIT_DTYPE, (RS04_DTYPE << UNIT_V_DTYPE), "RS04", "RS04", &rs_set_type },
    {0}
};

REG                 rsa_reg[] = {
    {ORDATA(IVECT, rs_ivect[0], 18)},
    {FLDATA(IMODE, rs_imode[0], 0)},
    {ORDATA(XFER, rs_xfer_drive[0], 3), REG_HRO},
    {ORDATA(DRIVE, rs_drive[0], 3), REG_HRO},
    {ORDATA(REG, rs_reg[0], 6), REG_RO},
    {ORDATA(RAE, rs_rae[0], 8), REG_RO},
    {ORDATA(ATTN, rs_attn[0], 8), REG_RO},
    {FLDATA(READIN, readin_flag, 0), REG_HRO},
    {ORDATA(STATUS, rs_df10[0].status, 18), REG_RO},
    {ORDATA(CIA, rs_df10[0].cia, 18)},
    {ORDATA(CCW, rs_df10[0].ccw, 18)},
    {ORDATA(WCR, rs_df10[0].wcr, 18)},
    {ORDATA(CDA, rs_df10[0].cda, 18)},
    {ORDATA(DEVNUM, rs_df10[0].devnum, 9), REG_HRO},
    {ORDATA(BUF, rs_df10[0].buf, 36), REG_HRO},
    {ORDATA(NXM, rs_df10[0].nxmerr, 8), REG_HRO},
    {ORDATA(COMP, rs_df10[0].ccw_comp, 8), REG_HRO},
    {BRDATA(BUFF, &rs_buf[0][0], 16, 64, RS_NUMWD), REG_HRO},
    {0}
};  

DEVICE              rsa_dev = {
    "FSA", rs_unit, rsa_reg, rs_mod,
    NUM_UNITS_RS, 8, 18, 1, 8, 36,
    NULL, NULL, &rs_reset, &rs_boot, &rs_attach, &rs_detach,
    &rs_dib[0], DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &rs_help, NULL, NULL, &rs_description
};

DEVICE *rs_devs[] = {
    &rsa_dev,
};


t_stat rs_devio(uint32 dev, uint64 *data) {
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
     df10 = &rs_df10[ctlr];
     df10->devnum = dev;
     switch(dev & 3) {
     case CONI:
        *data = df10->status & ~(IADR_ATTN|IARD_RAE);
        if (rs_attn[ctlr] != 0 && (df10->status & IADR_ATTN))
           *data |= IADR_ATTN;
        if (rs_rae[ctlr] != 0 && (df10->status & IARD_RAE))
           *data |= IARD_RAE;
#if KI_22BIT
        *data |= B22_FLAG;
#endif
        sim_debug(DEBUG_CONI, dptr, "RS %03o CONI %06o PC=%o %o\n",
               dev, (uint32)*data, PC, rs_attn[ctlr]);
        return SCPE_OK;

     case CONO:
         clr_interrupt(dev);
         df10->status &= ~(07LL|IADR_ATTN|IARD_RAE);
         df10->status |= *data & (07LL|IADR_ATTN|IARD_RAE);
         /* Clear flags */
         if (*data & CONT_RESET) {
            UNIT *uptr=dptr->units;
            for(drive = 0; drive < NUM_UNITS_RS; drive++, uptr++) {
               uptr->CMD &= DS_MOL|DS_WRL|DS_DPR|DS_DRY|DS_VV|076;
               uptr->DA &= 003400177777;
            }
         }
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
         if ((df10->status & IADR_ATTN) != 0 && rs_attn[ctlr] != 0)
            set_interrupt(dev, df10->status);
            sim_debug(DEBUG_CONO, dptr, "RS %03o CONO %06o %d PC=%06o %06o\n",
                  dev, (uint32)*data, ctlr, PC, df10->status);
         return SCPE_OK;

     case DATAI:
        *data = 0;
        if (df10->status & BUSY && rs_reg[ctlr] != 04) {
            df10->status |= CC_CHAN_ACT;
            return SCPE_OK;
        }
        if (rs_reg[ctlr] == 040) {
              *data = (uint64)(rs_read(ctlr, rs_drive[ctlr], 0) & 077);
              *data |= ((uint64)(df10->cia)) << 6;
              *data |= ((uint64)(rs_xfer_drive[ctlr])) << 18;
        } else if (rs_reg[ctlr] == 044) {
              *data = (uint64)rs_ivect[ctlr];
              if (rs_imode[ctlr])
                *data |= IRQ_KI10;
              else
                *data |= IRQ_KA10;
        } else if (rs_reg[ctlr] == 054) {
                *data = (uint64)(rs_rae[ctlr]);
        } else if ((rs_reg[ctlr] & 040) == 0) {
               int parity;

               *data = (uint64)(rs_read(ctlr, rs_drive[ctlr], rs_reg[ctlr]) & 0177777);
               parity = (int)((*data >> 8) ^ *data);
               parity = (parity >> 4) ^ parity;
               parity = (parity >> 2) ^ parity;
               parity = ((parity >> 1) ^ parity) & 1;
               *data |= ((uint64)(parity ^ 1)) << 17;
               *data |= ((uint64)(rs_drive[ctlr])) << 18;
        }
        *data |= ((uint64)(rs_reg[ctlr])) << 30;
        sim_debug(DEBUG_DATAIO, dptr, "RS %03o DATI %012llo, %d %d PC=%06o\n",
                    dev, *data, ctlr, rs_drive[ctlr], PC);
        return SCPE_OK;

     case DATAO:
         sim_debug(DEBUG_DATAIO, dptr, "RS %03o DATO %012llo, %d PC=%06o %06o\n",
                    dev, *data, ctlr, PC, df10->status);
         rs_reg[ctlr] = ((int)(*data >> 30)) & 077;
         if (rs_reg[ctlr] < 040 && rs_reg[ctlr] != 04) {
            rs_drive[ctlr] = (int)(*data >> 18) & 07;
         }
         if (*data & LOAD_REG) {
             if (rs_reg[ctlr] == 040) {
                if ((*data & 1) == 0) {
                   return SCPE_OK;
                }

                if (df10->status & BUSY) {
                    df10->status |= CC_CHAN_ACT;
                    return SCPE_OK;
                }

                df10->status &= ~(1 << df10->ccw_comp);
                df10->status &= ~PI_ENABLE;
                if (((*data >> 1) & 077) < FNC_XFER) {
                   df10->status |= CXR_ILC;
                   df10_setirq(df10);
                   sim_debug(DEBUG_DATAIO, dptr,
                       "RS %03o command abort %012llo, %d[%d] PC=%06o %06o\n",
                       dev, *data, ctlr, rs_drive[ctlr], PC, df10->status);
                   return SCPE_OK;
                }

                /* Start command */
                df10_setup(df10, (uint32)(*data >> 6));
                rs_xfer_drive[ctlr] = (int)(*data >> 18) & 07;
                rs_write(ctlr, rs_drive[ctlr], 0, (uint32)(*data & 077));
                sim_debug(DEBUG_DATAIO, dptr,
                    "RS %03o command %012llo, %d[%d] PC=%06o %06o\n",
                    dev, *data, ctlr, rs_drive[ctlr], PC, df10->status);
             } else if (rs_reg[ctlr] == 044) {
                /* Set KI10 Irq vector */
                rs_ivect[ctlr] = (int)(*data & IRQ_VECT);
                rs_imode[ctlr] = (*data & IRQ_KI10) != 0;
             } else if (rs_reg[ctlr] == 050) {
                ;    /* Diagnostic access to mass bus. */
             } else if (rs_reg[ctlr] == 054) {
                /* clear flags */
                rs_rae[ctlr] &= ~(*data & 0377);
                if (rs_rae[ctlr] == 0)
                    clr_interrupt(dev);
             } else if ((rs_reg[ctlr] & 040) == 0) {
                rs_drive[ctlr] = (int)(*data >> 18) & 07;
                /* Check if access error */
                if (rs_rae[ctlr] & (1 << rs_drive[ctlr])) {
                    return SCPE_OK;
                }
                rs_drive[ctlr] = (int)(*data >> 18) & 07;
                rs_write(ctlr, rs_drive[ctlr], rs_reg[ctlr] & 037,
                        (int)(*data & 0777777));
             }
         }
         return SCPE_OK;
    }
    return SCPE_OK; /* Unreached */
}

/* Handle KI and KL style interrupt vectors */
int
rs_devirq(uint32 dev, int addr) {
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
        return (rs_imode[drive] ? rs_ivect[drive] : addr);
    }
    return  addr;
}

void
rs_write(int ctlr, int unit, int reg, uint32 data) {
    int            i;
    DEVICE        *dptr = rs_devs[ctlr];
    struct df10   *df10 = &rs_df10[ctlr];
    UNIT          *uptr = &dptr->units[unit];
 
    if ((uptr->CMD & CR_GO) && reg != 04) {
        uptr->CMD |= (ER1_RMR << 16)|DS_ERR;
        return;
    }
    switch(reg) {
    case  000:  /* control */
        sim_debug(DEBUG_DETAIL, dptr, "RSA%o %d Status=%06o\n", unit, ctlr, uptr->CMD);
        /* Set if drive not writable */
        if (uptr->flags & UNIT_WLK)
           uptr->CMD |= DS_WRL;
        /* If drive not ready don't do anything */
        if ((uptr->CMD & DS_DRY) == 0) {
           uptr->CMD |= (ER1_RMR << 16)|DS_ERR;
           sim_debug(DEBUG_DETAIL, dptr, "RSA%o %d busy\n", unit, ctlr);
           return;
        }
        /* Check if GO bit set */
        if ((data & 1) == 0) {
           uptr->CMD &= ~076;
           uptr->CMD |= data & 076;
           sim_debug(DEBUG_DETAIL, dptr, "RSA%o %d no go\n", unit, ctlr);
           return;                           /* No, nop */
        }
        uptr->CMD &= DS_ATA|DS_VV|DS_DPR|DS_MOL|DS_WRL;
        uptr->CMD |= data & 076;
        switch (GET_FNC(data)) {
        case FNC_NOP:
            uptr->CMD |= DS_DRY;
            break;

        case FNC_SEARCH:                      /* search */
        case FNC_WCHK:                        /* write check */
        case FNC_WRITE:                       /* write */
        case FNC_READ:                        /* read */
            uptr->CMD |= DS_PIP|CR_GO;
            uptr->DATAPTR = 0;
            break;

        case FNC_PRESET:                      /* read-in preset */
            uptr->DA = 0;
            if ((uptr->flags & UNIT_ATT) != 0)
                uptr->CMD |= DS_VV;
            uptr->CMD |= DS_DRY;
            df10_setirq(df10);
            break;

        case FNC_DCLR:                        /* drive clear */
            uptr->CMD |= DS_DRY;
            uptr->CMD &= ~(DS_ATA|CR_GO);
            rs_attn[ctlr] = 0;
            clr_interrupt(df10->devnum);
            for (i = 0; i < 8; i++) {
                if (rs_unit[(ctlr * 8) + i].CMD & DS_ATA)
                   rs_attn[ctlr] = 1;
            }
            if ((df10->status & IADR_ATTN) != 0 && rs_attn[ctlr] != 0)
                df10_setirq(df10);
            break;
        default:
            uptr->CMD |= DS_DRY|DS_ERR|DS_ATA;
            uptr->CMD |= (ER1_ILF << 16);
            if ((df10->status & IADR_ATTN) != 0 && rs_attn[ctlr] != 0)
                 df10_setirq(df10);
        }
        if (uptr->CMD & CR_GO)
            sim_activate(uptr, 100);
        sim_debug(DEBUG_DETAIL, dptr, "RSA%o AStatus=%06o\n", unit, uptr->CMD);
        return;
    case  001:  /* status */
        break;
    case  002:  /* error register 1 */
        uptr->CMD &= 0177777;
        uptr->CMD |= data << 16;
        if (data != 0)
           uptr->CMD |= DS_ERR;
        break;
    case  003:  /* maintenance */
        break;
    case  004:  /* atten summary */
        rs_attn[ctlr] = 0;
        for (i = 0; i < 8; i++) {
            if (data & (1<<i))
                rs_unit[(ctlr * 8) + i].CMD &= ~DS_ATA;
            if (rs_unit[(ctlr * 8) + i].CMD & DS_ATA)
               rs_attn[ctlr] = 1;
        }
        clr_interrupt(df10->devnum);
        if (((df10->status & IADR_ATTN) != 0 && rs_attn[ctlr] != 0) ||
             (df10->status & PI_ENABLE))
            df10_setirq(df10);
        break;
    case  005:  /* sector/track */
        uptr->DA = data & 0177777;
        break;
    case  006:  /* drive type */
    case  007:  /* look ahead */
        break;
    default:
        uptr->CMD |= (ER1_ILR<<16)|DS_ERR;
        rs_rae[ctlr] &= ~(1<<unit);
    }
}

uint32
rs_read(int ctlr, int unit, int reg) {
    DEVICE        *dptr = rs_devs[ctlr];
    struct df10   *df10 = &rs_df10[ctlr];
    UNIT          *uptr = &dptr->units[unit];
    uint32        temp = 0;
    int           i;

    if ((uptr->flags & UNIT_ATT) == 0 && reg != 04) {       /* not attached? */
        return 0;
    }

    switch(reg) {
    case  000:  /* control */
        temp = uptr->CMD & 077;
        if (uptr->flags & UNIT_ATT)
           temp |= CS1_DVA;
        if ((df10->status & BUSY) == 0 && (uptr->CMD & CR_GO) == 0)
           temp |= CS1_GO;
        break;
    case  001:  /* status */
        temp = uptr->CMD & 0177700;
        break;
    case  002:  /* error register 1 */
        temp = (uptr->CMD >> 16) & 0177777;
        break;
    case  004:  /* atten summary */
        for (i = 0; i < 8; i++) {
            if (rs_unit[(ctlr * 8) + i].CMD & DS_ATA) {
                temp |= 1 << i;
            }
        }
        break;
    case  005:  /* sector/track */
        temp = uptr->DA & 0177777;
        break;
    case  006:  /* drive type */
        temp = rs_drv_tab[GET_DTYPE(uptr->flags)].devtype;
        break;
    case  003:  /* maintenance */
    case  007:  /* look ahead */
        break;
    default:
        uptr->CMD |= (ER1_ILR<<16);
        rs_rae[ctlr] &= ~(1<<unit);
    }
    return temp;
}


t_stat rs_svc (UNIT *uptr)
{
    int          dtype = GET_DTYPE(uptr->flags);
    int          ctlr = GET_CNTRL(uptr->flags);
    int          unit;
    DEVICE      *dptr;
    struct df10 *df;
    int          da;
    t_stat       r;

    /* Find dptr, and df10 */
    dptr = rs_devs[ctlr];
    unit = uptr - dptr->units;
    df = &rs_df10[ctlr];
    if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
        uptr->CMD |= (ER1_UNS << 16) | DS_ATA|DS_ERR;       /* set drive error */
        df->status &= ~BUSY;
        df10_setirq(df);
        return (SCPE_OK);
    }

    /* Check if seeking */
    if (uptr->CMD & DS_PIP) {
       uptr->CMD &= ~DS_PIP;
       uptr->DATAPTR = 0;
    }

    switch (GET_FNC(uptr->CMD)) {
    case FNC_NOP:
    case FNC_DCLR:                       /* drive clear */
        break;
    case FNC_PRESET:                     /* read-in preset */
        rs_attn[ctlr] = 1;
        uptr->CMD |= DS_DRY|DS_ATA;
        uptr->CMD &= ~CR_GO;
        df->status &= ~BUSY;
        if (df->status & IADR_ATTN)
            df10_setirq(df);
        sim_debug(DEBUG_DETAIL, dptr, "RSA%o seekdone\n", unit);
        break;

    case FNC_SEARCH:                     /* search */
        if (GET_SC(uptr->DA) >= rs_drv_tab[dtype].sect ||
            GET_SF(uptr->DA) >= rs_drv_tab[dtype].surf)
            uptr->CMD |= (ER1_IAE << 16)|DS_ERR;
        rs_attn[ctlr] = 1;
        uptr->CMD |= DS_DRY|DS_ATA;
        uptr->CMD &= ~CR_GO;
        df->status &= ~BUSY;
        if ((df->status & (IADR_ATTN|BUSY)) == IADR_ATTN)
            df10_setirq(df);
        sim_debug(DEBUG_DETAIL, dptr, "RSA%o searchdone\n", unit);
        break;

    case FNC_READ:                       /* read */
    case FNC_WCHK:                       /* write check */
        if (uptr->DATAPTR == 0) {
            int wc;
            if (GET_SC(uptr->DA) >= rs_drv_tab[dtype].sect ||
                GET_SF(uptr->DA) >= rs_drv_tab[dtype].surf) {
                uptr->CMD |= (ER1_IAE << 16)|DS_ERR|DS_DRY|DS_ATA;
                df->status &= ~BUSY;
                uptr->CMD &= ~CR_GO;
                sim_debug(DEBUG_DETAIL, dptr, "RSA%o readx done\n", unit);
                df10_finish_op(df, 0);
                return SCPE_OK;
            }
            sim_debug(DEBUG_DETAIL, dptr, "RSA%o read (%d,%d)\n", unit,
                   GET_SC(uptr->DA), GET_SF(uptr->DA));
            da = GET_DA(uptr->DA, dtype) * RS_NUMWD;
            (void)sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
            wc = sim_fread (&rs_buf[ctlr][0], sizeof(uint64), RS_NUMWD,
                                uptr->fileref);
            while (wc < RS_NUMWD)
                rs_buf[ctlr][wc++] = 0;
            uptr->hwmark = RS_NUMWD;
        }

        df->buf = rs_buf[ctlr][uptr->DATAPTR++];
        sim_debug(DEBUG_DATA, dptr, "RSA%o read word %d %012llo %09o %06o\n",
                unit, uptr->DATAPTR, df->buf, df->cda, df->wcr);
        if (df10_write(df)) {
            if (uptr->DATAPTR == uptr->hwmark) {
                /* Increment to next sector. Set Last Sector */
                uptr->DATAPTR = 0;
                uptr->DA += 1 << DA_V_SC;
                if (GET_SC(uptr->DA) >= rs_drv_tab[dtype].sect) {
                    uptr->DA &= (DA_M_SF << DA_V_SF);
                    uptr->DA += 1 << DA_V_SF;
                    if (GET_SF(uptr->DA) >= rs_drv_tab[dtype].surf)
                        uptr->CMD |= DS_LST;
                }
            }
            sim_activate(uptr, 20);
        } else {
            sim_debug(DEBUG_DETAIL, dptr, "RSA%o read done\n", unit);
            uptr->CMD |= DS_DRY;
            uptr->CMD &= ~CR_GO;
            df10_finish_op(df, 0);
            return SCPE_OK;
        }
        break;

    case FNC_WRITE:                      /* write */
        if (uptr->DATAPTR == 0) {
            if (GET_SC(uptr->DA) >= rs_drv_tab[dtype].sect ||
                GET_SF(uptr->DA) >= rs_drv_tab[dtype].surf) {
                uptr->CMD |= (ER1_IAE << 16)|DS_ERR|DS_DRY|DS_ATA;
                uptr->CMD &= ~CR_GO;
                sim_debug(DEBUG_DETAIL, dptr, "RSA%o writex done\n", unit);
                df10_finish_op(df, 0);
                return SCPE_OK;
            }
        }
        r = df10_read(df);
        rs_buf[ctlr][uptr->DATAPTR++] = df->buf;
        sim_debug(DEBUG_DATA, dptr, "RSA%o write word %d %012llo %09o %06o\n",
                 unit, uptr->DATAPTR, df->buf, df->cda, df->wcr);
        if (r == 0 || uptr->DATAPTR == RS_NUMWD) {
            while (uptr->DATAPTR < RS_NUMWD)
                rs_buf[ctlr][uptr->DATAPTR++] = 0;
            sim_debug(DEBUG_DETAIL, dptr, "RSA%o write (%d,%d)\n", unit,
                   GET_SC(uptr->DA), GET_SF(uptr->DA));
            da = GET_DA(uptr->DA, dtype) * RS_NUMWD;
            (void)sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
            (void)sim_fwrite (&rs_buf[ctlr][0], sizeof(uint64), RS_NUMWD,
                                uptr->fileref);
            uptr->DATAPTR = 0;
            if (r) {
                uptr->DA += 1 << DA_V_SC;
                if (GET_SC(uptr->DA) >= rs_drv_tab[dtype].sect) {
                    uptr->DA &= (DA_M_SF << DA_V_SF);
                    uptr->DA += 1 << DA_V_SF;
                    if (GET_SF(uptr->DA) >= rs_drv_tab[dtype].surf)
                        uptr->CMD |= DS_LST;
                }
             }
        }
        if (r) {
            sim_activate(uptr, 20);
        } else {
            sim_debug(DEBUG_DETAIL, dptr, "RSA%o write done\n", unit);
            uptr->CMD |= DS_DRY;
            uptr->CMD &= ~CR_GO;
            df10_finish_op(df, 0);
            return SCPE_OK;
        }
        break;
    }
    return SCPE_OK;
}


t_stat
rs_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int         i;

    if (uptr == NULL) return SCPE_IERR;
    uptr->flags &= ~(UNIT_DTYPE);
    uptr->flags |= val;
    i = GET_DTYPE(val);
    uptr->capac = rs_drv_tab[i].size;
    return SCPE_OK;
}


t_stat
rs_reset(DEVICE * rstr)
{
    int ctlr;
    for (ctlr = 0; ctlr < NUM_DEVS_RS; ctlr++) {
        rs_df10[ctlr].devnum = rs_dib[ctlr].dev_num;
        rs_df10[ctlr].nxmerr = 19;
        rs_df10[ctlr].ccw_comp = 14;
        rs_df10[ctlr].status = 0;
        rs_attn[ctlr] = 0;
        rs_rae[ctlr] = 0;
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
rs_boot(int32 unit_num, DEVICE * rptr)
{
    UNIT         *uptr = &rptr->units[unit_num];
    int           ctlr = GET_CNTRL(uptr->flags);
    DEVICE       *dptr;
    struct df10  *df;
    uint32        addr;
    uint32        ptr = 0;
    uint64        word;
    int           wc;

    df = &rs_df10[ctlr];
    dptr = rs_devs[ctlr];
    (void)sim_fseek(uptr->fileref, 0, SEEK_SET);
    (void)sim_fread (&rs_buf[0][0], sizeof(uint64), RS_NUMWD, uptr->fileref);
    uptr->CMD |= DS_VV;
    addr = rs_buf[0][ptr] & RMASK;
    wc = (rs_buf[0][ptr++] >> 18) & RMASK;
    while (wc != 0) {
        wc = (wc + 1) & RMASK;
        addr = (addr + 1) & RMASK;
        word = rs_buf[0][ptr++];
        if (addr < 020)
           FM[addr] = word;
        else
           M[addr] = word;
    }
    addr = rs_buf[0][ptr] & RMASK;
    wc = (rs_buf[0][ptr++] >> 18) & RMASK;
    word = rs_buf[0][ptr++];
    rs_reg[ctlr] = 040;
    rs_drive[ctlr] = uptr - dptr->units;
    df->status |= CCW_COMP_1|PI_ENABLE;
    PC = word & RMASK;
    return SCPE_OK;

}


/* Device attach */

t_stat rs_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    DEVICE *rstr;
    DIB *dib;
    int ctlr;

    uptr->capac = rs_drv_tab[GET_DTYPE (uptr->flags)].size;
    r = attach_unit (uptr, cptr);
    if (r != SCPE_OK)
        return r;
    rstr = find_dev_from_unit(uptr);
    if (rstr == 0)
        return SCPE_OK;
    dib = (DIB *) rstr->ctxt;
    ctlr = dib->dev_num & 014;
    uptr->DA = 0;
    uptr->CMD &= ~DS_VV;
    uptr->CMD |= DS_DPR|DS_MOL|DS_DRY;
    if (uptr->flags & UNIT_WLK)
         uptr->CMD |= DS_WRL;
    rs_df10[ctlr].status |= PI_ENABLE;
    set_interrupt(dib->dev_num, rs_df10[ctlr].status);
    return SCPE_OK;
}

/* Device detach */

t_stat rs_detach (UNIT *uptr)
{
    if (!(uptr->flags & UNIT_ATT))                          /* attached? */
        return SCPE_OK;
    if (sim_is_active (uptr))                              /* unit active? */
        sim_cancel (uptr);                                  /* cancel operation */
    uptr->CMD &= ~(DS_VV|DS_WRL|DS_DPR|DS_DRY);
    return detach_unit (uptr);
}

t_stat rs_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "RS04 Disk Pack Drives (RS)\n\n");
fprintf (st, "The RS controller implements the Massbus family of fast disk drives.  RS\n");
fprintf (st, "options include the ability to set units write enabled or write locked, to\n");
fprintf (st, "set the drive type to one of six disk types or autosize, and to write a DEC\n");
fprintf (st, "standard 044 compliant bad block table on the last track.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.\n");
fprintf (st, "The RS device supports the BOOT command.\n");
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *rs_description (DEVICE *dptr)
{
    return "RS04 Massbus disk controller";
}


#endif
