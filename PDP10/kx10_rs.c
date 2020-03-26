/* kx10_rs.c: DEC Massbus RS04

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
#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark = 0xFFFFFFFF

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

/* Parameters in the unit descriptor */

#define CMD             u3
/* u3  low */
/* RSC - 00 - control */

#define CS1_GO          1               /* go */
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


uint64        rs_buf[NUM_DEVS_RS][RS_NUMWD];
int           rs_write(DEVICE *dptr, struct rh_if *rhc, int reg, uint32 data);
int           rs_read(DEVICE *dptr, struct rh_if *rhc, int reg, uint32 *data);
void          rs_rst(DEVICE *dptr);
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
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL_RH(0), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL_RH(0), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL_RH(0), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL_RH(0), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL_RH(0), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL_RH(0), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL_RH(0), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RS04_DTYPE)+CNTRL_RH(0), RS04_SIZE) },
};

struct rh_if   rs_rh[] = {
     { &rs_write, &rs_read, &rs_rst},
};

DIB rs_dib[] = {
    {RH10_DEV, 1, &rh_devio, &rh_devirq, &rs_rh[0]}
};

MTAB                rs_mod[] = {
#if KL
    {MTAB_XTD|MTAB_VDV, TYPE_RH10, NULL, "RH10",  &rh_set_type, NULL,
              NULL, "Sets controller to RH10" },
    {MTAB_XTD|MTAB_VDV, TYPE_RH20, "RH20", "RH20", &rh_set_type, &rh_show_type,
              NULL, "Sets controller to RH20"},
#endif
    {UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL},
    {UNIT_DTYPE, (RS03_DTYPE << UNIT_V_DTYPE), "RS03", "RS03", &rs_set_type },
    {UNIT_DTYPE, (RS04_DTYPE << UNIT_V_DTYPE), "RS04", "RS04", &rs_set_type },
    {0}
};

REG                 rsa_reg[] = {
    {ORDATA(IVECT, rs_rh[0].ivect, 18)},
    {FLDATA(IMODE, rs_rh[0].imode, 0)},
    {ORDATA(XFER, rs_rh[0].xfer_drive, 3), REG_HRO},
    {ORDATA(DRIVE, rs_rh[0].drive, 3), REG_HRO},
    {ORDATA(REG, rs_rh[0].reg, 6), REG_RO},
    {ORDATA(RAE, rs_rh[0].rae, 8), REG_RO},
    {ORDATA(ATTN, rs_rh[0].attn, 8), REG_RO},
    {ORDATA(STATUS, rs_rh[0].status, 18), REG_RO},
    {ORDATA(CIA, rs_rh[0].cia, 18)},
    {ORDATA(CCW, rs_rh[0].ccw, 18)},
    {ORDATA(WCR, rs_rh[0].wcr, 18)},
    {ORDATA(CDA, rs_rh[0].cda, 18)},
    {ORDATA(DEVNUM, rs_rh[0].devnum, 9), REG_HRO},
    {ORDATA(BUF, rs_rh[0].buf, 36), REG_HRO},
    {BRDATA(BUFF, rs_buf[0], 16, 64, RS_NUMWD), REG_HRO},
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


void
rs_rst(DEVICE *dptr)
{
   UNIT *uptr=dptr->units;
   int   drive;
   for(drive = 0; drive < NUM_UNITS_RS; drive++, uptr++) {
       uptr->CMD &= DS_MOL|DS_WRL|DS_DPR|DS_DRY|DS_VV|076;
       uptr->DA &= 003400177777;
   }
}

int
rs_write(DEVICE *dptr, struct rh_if *rhc, int reg, uint32 data) {
    int            i;
    int            unit = rhc->drive;
    UNIT          *uptr = &dptr->units[unit];
 
    if ((uptr->flags & UNIT_DIS) != 0)
        return 1;
    if ((uptr->CMD & CS1_GO) && reg != 04) {
        uptr->CMD |= (ER1_RMR << 16)|DS_ERR;
        return 0;
    }
    switch(reg) {
    case  000:  /* control */
        sim_debug(DEBUG_DETAIL, dptr, "%s%o Status=%06o\n", dptr->name, unit, uptr->CMD);
        /* Set if drive not writable */
        if (uptr->flags & UNIT_WLK)
           uptr->CMD |= DS_WRL;
        /* If drive not ready don't do anything */
        if ((uptr->CMD & DS_DRY) == 0) {
           uptr->CMD |= (ER1_RMR << 16)|DS_ERR;
           sim_debug(DEBUG_DETAIL, dptr, "%s%o busy\n", dptr->name, unit);
           return 0;
        }
        /* Check if GO bit set */
        if ((data & 1) == 0) {
           uptr->CMD &= ~076;
           uptr->CMD |= data & 076;
           sim_debug(DEBUG_DETAIL, dptr, "%s%o no go\n", dptr->name, unit);
           return 0;                           /* No, nop */
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
            uptr->CMD |= DS_PIP|CS1_GO;
            CLR_BUF(uptr);
            uptr->DATAPTR = 0;
            break;

        case FNC_PRESET:                      /* read-in preset */
            uptr->DA = 0;
            if ((uptr->flags & UNIT_ATT) != 0)
                uptr->CMD |= DS_VV;
            uptr->CMD |= DS_DRY;
            rh_setirq(rhc);
            break;

        case FNC_DCLR:                        /* drive clear */
            uptr->CMD |= DS_DRY;
            uptr->CMD &= ~(DS_ATA|CS1_GO);
            rhc->attn = 0;
            clr_interrupt(rhc->devnum);
            for (i = 0; i < 8; i++) {
                if (dptr->units[i].CMD & DS_ATA)
                   rhc->attn |= 1 << i;
            }
            break;
        default:
            uptr->CMD |= DS_DRY|DS_ERR|DS_ATA;
            uptr->CMD |= (ER1_ILF << 16);
        }
        if (uptr->CMD & CS1_GO)
            sim_activate(uptr, 100);
        sim_debug(DEBUG_DETAIL, dptr, "%s%o AStatus=%06o\n", dptr->name, unit, uptr->CMD);
        return 0;
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
        rhc->attn = 0;
        for (i = 0; i < 8; i++) {
            if (data & (1<<i))
                dptr->units[i].CMD &= ~DS_ATA;
            if (dptr->units[i].CMD & DS_ATA)
               rhc->attn |= 1 << i;
        }
        break;
    case  005:  /* sector/track */
        uptr->DA = data & 0177777;
        break;
    case  006:  /* drive type */
    case  007:  /* look ahead */
        break;
    default:
        uptr->CMD |= (ER1_ILR<<16)|DS_ERR;
        rhc->rae |= 1 << unit;
    }
    return 0;
}

int
rs_read(DEVICE *dptr, struct rh_if *rhc, int reg, uint32 *data) {
    int            unit = rhc->drive;
    UNIT          *uptr = &dptr->units[unit];
    uint32        temp = 0;
    int           i;

    if ((uptr->flags & UNIT_DIS) != 0)
        return 1;
    if ((uptr->flags & UNIT_ATT) == 0 && reg != 04) {       /* not attached? */
        *data = 0;
        return 0;
    }

    switch(reg) {
    case  000:  /* control */
        temp = uptr->CMD & 077;
        if (uptr->flags & UNIT_ATT)
           temp |= CS1_DVA;
        if ((uptr->CMD & CS1_GO) == 0)
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
            if (dptr->units[i].CMD & DS_ATA) {
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
        rhc->rae |= 1 << unit;
    }
    *data = temp;
    return 0;
}


t_stat rs_svc (UNIT *uptr)
{
    int           dtype = GET_DTYPE(uptr->flags);
    int           ctlr = GET_CNTRL_RH(uptr->flags);
    int           unit;
    DEVICE       *dptr;
    struct rh_if *rhc;
    int           da;
    int           sts;

    /* Find dptr, and df10 */
    dptr = rs_devs[ctlr];
    rhc = &rs_rh[ctlr];
    unit = uptr - dptr->units;
    if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
        uptr->CMD |= (ER1_UNS << 16) | DS_ATA|DS_ERR;       /* set drive error */
        rh_setirq(rhc);
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
        uptr->CMD |= DS_DRY|DS_ATA;
        uptr->CMD &= ~CS1_GO;
        rh_setattn(rhc, unit);
        sim_debug(DEBUG_DETAIL, dptr, "%s%o seekdone\n", dptr->name, unit);
        break;

    case FNC_SEARCH:                     /* search */
        if (GET_SC(uptr->DA) >= rs_drv_tab[dtype].sect ||
            GET_SF(uptr->DA) >= rs_drv_tab[dtype].surf)
            uptr->CMD |= (ER1_IAE << 16)|DS_ERR;
        uptr->CMD |= DS_DRY|DS_ATA;
        uptr->CMD &= ~CS1_GO;
        rh_setattn(rhc, unit);
        sim_debug(DEBUG_DETAIL, dptr, "%s%o searchdone\n", dptr->name, unit);
        break;

    case FNC_READ:                       /* read */
    case FNC_WCHK:                       /* write check */
        if (BUF_EMPTY(uptr)) {
            int wc;
            if (GET_SC(uptr->DA) >= rs_drv_tab[dtype].sect ||
                GET_SF(uptr->DA) >= rs_drv_tab[dtype].surf) {
                uptr->CMD |= (ER1_IAE << 16)|DS_ERR|DS_DRY|DS_ATA;
                uptr->CMD &= ~CS1_GO;
                sim_debug(DEBUG_DETAIL, dptr, "%s%o readx done\n", dptr->name, unit);
                rh_finish_op(rhc, 0);
                return SCPE_OK;
            }
            sim_debug(DEBUG_DETAIL, dptr, "%s%o read (%d,%d)\n", dptr->name, unit,
                   GET_SC(uptr->DA), GET_SF(uptr->DA));
            da = GET_DA(uptr->DA, dtype) * RS_NUMWD;
            (void)sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
            wc = sim_fread (&rs_buf[ctlr][0], sizeof(uint64), RS_NUMWD,
                                uptr->fileref);
            while (wc < RS_NUMWD)
                rs_buf[ctlr][wc++] = 0;
            uptr->hwmark = RS_NUMWD;
            uptr->DATAPTR = 0;
        }

        rhc->buf = rs_buf[ctlr][uptr->DATAPTR++];
        sim_debug(DEBUG_DATA, dptr, "%s%o read word %d %012llo %09o %06o\n",
                dptr->name, unit, uptr->DATAPTR, rhc->buf, rhc->cda, rhc->wcr);
        if (rh_write(rhc)) {
            if (uptr->DATAPTR == RS_NUMWD) {
                /* Increment to next sector. Set Last Sector */
                uptr->DATAPTR = 0;
                CLR_BUF(uptr);
                uptr->DA += 1 << DA_V_SC;
                if (GET_SC(uptr->DA) >= rs_drv_tab[dtype].sect) {
                    uptr->DA &= (DA_M_SF << DA_V_SF);
                    uptr->DA += 1 << DA_V_SF;
                    if (GET_SF(uptr->DA) >= rs_drv_tab[dtype].surf)
                        uptr->CMD |= DS_LST;
                }
                if (rh_blkend(rhc))
                   goto rd_end;
            }
            sim_activate(uptr, 10);
        } else {
rd_end:
            sim_debug(DEBUG_DETAIL, dptr, "%s%o read done\n", dptr->name, unit);
            uptr->CMD |= DS_DRY;
            uptr->CMD &= ~CS1_GO;
            if (uptr->DATAPTR == RS_NUMWD)
               (void)rh_blkend(rhc);
            rh_finish_op(rhc, 0);
            return SCPE_OK;
        }
        break;

    case FNC_WRITE:                      /* write */
        if (BUF_EMPTY(uptr)) {
            if (GET_SC(uptr->DA) >= rs_drv_tab[dtype].sect ||
                GET_SF(uptr->DA) >= rs_drv_tab[dtype].surf) {
                uptr->CMD |= (ER1_IAE << 16)|DS_ERR|DS_DRY|DS_ATA;
                uptr->CMD &= ~CS1_GO;
                sim_debug(DEBUG_DETAIL, dptr, "%s%o writex done\n", dptr->name, unit);
                rh_finish_op(rhc, 0);
                return SCPE_OK;
            }
            uptr->DATAPTR = 0;
            uptr->hwmark = 0;
        }
        sts = rh_read(rhc);
        rs_buf[ctlr][uptr->DATAPTR++] = rhc->buf;
        sim_debug(DEBUG_DATA, dptr, "%s%o write word %d %012llo %09o %06o\n",
                 dptr->name, unit, uptr->DATAPTR, rhc->buf, rhc->cda, rhc->wcr);
        if (sts == 0) {
            while (uptr->DATAPTR < RS_NUMWD)
                rs_buf[ctlr][uptr->DATAPTR++] = 0;
        }
        if (uptr->DATAPTR == RS_NUMWD) {
            sim_debug(DEBUG_DETAIL, dptr, "%s%o write (%d,%d)\n", dptr->name, unit,
                   GET_SC(uptr->DA), GET_SF(uptr->DA));
            da = GET_DA(uptr->DA, dtype) * RS_NUMWD;
            (void)sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
            (void)sim_fwrite (&rs_buf[ctlr][0], sizeof(uint64), RS_NUMWD,
                                uptr->fileref);
            uptr->DATAPTR = 0;
            CLR_BUF(uptr);
            if (sts) {
                uptr->DA += 1 << DA_V_SC;
                if (GET_SC(uptr->DA) >= rs_drv_tab[dtype].sect) {
                    uptr->DA &= (DA_M_SF << DA_V_SF);
                    uptr->DA += 1 << DA_V_SF;
                    if (GET_SF(uptr->DA) >= rs_drv_tab[dtype].surf)
                        uptr->CMD |= DS_LST;
                }
             }
             if (rh_blkend(rhc))
                  goto wr_end;
        }
        if (sts) {
            sim_activate(uptr, 10);
        } else {
wr_end:
            sim_debug(DEBUG_DETAIL, dptr, "%s%o write done\n", dptr->name, unit);
            uptr->CMD |= DS_DRY;
            uptr->CMD &= ~CS1_GO;
            rh_finish_op(rhc, 0);
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
        rs_rh[ctlr].status = 0;
        rs_rh[ctlr].attn = 0;
        rs_rh[ctlr].rae = 0;
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
rs_boot(int32 unit_num, DEVICE * rptr)
{
    UNIT         *uptr = &rptr->units[unit_num];
    int           ctlr = GET_CNTRL_RH(uptr->flags);
    struct rh_if *rhc;
    DEVICE       *dptr;
    uint32        addr;
    uint32        ptr = 0;
    uint64        word;
    int           wc;

    dptr = rs_devs[ctlr];
    rhc = &rs_rh[ctlr];
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
    rhc->reg = 040;
    rhc->drive = uptr - dptr->units;
    rhc->status |= CCW_COMP_1|PI_ENABLE;
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
    for (ctlr = 0; rh[ctlr].dev_num != 0; ctlr++) {
        if (rh[ctlr].dev == rstr)
            break;
    }
    if (uptr->flags & UNIT_WLK)
        uptr->CMD |= DS_WRL;
    if (sim_switches & SIM_SW_REST)
        return SCPE_OK;
    uptr->DA = 0;
    uptr->CMD &= ~DS_VV;
    uptr->CMD |= DS_DPR|DS_MOL|DS_DRY;
    rs_rh[ctlr].status |= PI_ENABLE;
    set_interrupt(dib->dev_num, rs_rh[ctlr].status);
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
