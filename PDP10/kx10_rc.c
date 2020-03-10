/* kx10_rc.c: RC10 Disk Controller.

   Copyright (c) 2013-2020, Richard Cornwell

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

*/

#include "kx10_defs.h"

#ifndef NUM_DEVS_RC
#define NUM_DEVS_RC 0
#endif

#if (NUM_DEVS_RC > 0)

#define RC_DEVNUM       0170                    /* 0174 */
#define NUM_UNITS_RC    4

/* Flags in the unit flags word */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* disk type */
#define UNIT_M_DTYPE    1
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */

/* Parameters in the unit descriptor */

#define CUR_CYL         u3              /* current cylinder */
#define DATAPTR         u4              /* data pointer */
#define UFLAGS          u5              /* Function */


#define DISK_SEL        0600000000000LL
#define TRACK           0177600000000LL
#define SEGMENT         0000177000000LL
#define INIT_PAR        0000000770000LL   /* Read */
#define DPE_STOP        0000000004000LL
#define CPE_STOP        0000000002000LL
#define WRITE           0000000001000LL
#define EPAR            0000000000001LL
#define SEC_SEL         0000000001400LL   /* Read */
#define SECT_CNT        0000000000377LL   /* Read */

#define PI              0000007
#define WCW             0000040
#define SEC_SCTR        0600000

#define RST_MSK         0000000177710LL   /* CONO reset bits */
#define B22_FLAG        0040000000000LL   /* 22 bit controller. */
#define MAINT_SEG       0010000000000LL
#define PRTLT           0004000000000LL   /* Protected area less then bounds */
#define STS             0003777000000LL
#define SCRCHCMP        0000000400000LL   /* Tranfer in progress. */
#define S_ERROR         0000000200000LL   /* Segment not found */
#define DSK_DES_E       0000000100000LL   /* Duplicate disk */
#define TRK_SEL_E       0000000040000LL   /* Track not BCD number */
#define NOT_RDY         0000000020000LL   /* Drive not ready */
#define PSW_FAIL        0000000010000LL   /* Power supply fail */
#define DSK_PAR_E       0000000004000LL   /* Disk Parity Error */
#define CH_PAR_D        0000000002000LL   /* Channel Data Parity Error */
#define CH_PAR_C        0000000001000LL   /* Channel Control Parity Error */
#define NXM_ERR         0000000000400LL   /* Non existant memory */
#define ILL_WR          0000000000200LL   /* Write to protected area */
#define OVRRUN          0000000000100LL   /* Over run */

#define RD10_DTYPE      0
#define RD10_WDS        32
#define RD10_SEGS       80
#define RD10_CYL        200
#define RD10_SIZE       (RD10_SEGS * RD10_CYL * RD10_WDS)

#define RM10_DTYPE      1
#define RM10_WDS        64
#define RM10_SEGS       60
#define RM10_CYL        90
#define RM10_SIZE       (RM10_SEGS * RM10_CYL * RM10_WDS)

struct drvtyp {
    int32       wd_seg;                                 /* Number of words per segment */
    int32       seg;                                    /* segments */
    int32       cyl;                                    /* cylinders */
    int32       size;                                   /* #blocks */
    int32       devtype;                                /* device type */
    };

struct drvtyp rc_drv_tab[] = {
    { RD10_WDS, RD10_SEGS, RD10_CYL, RD10_SIZE, RD10_DTYPE},
    { RM10_WDS, RM10_SEGS, RM10_CYL, RM10_SIZE, RM10_DTYPE},
    { 0 }
    };

struct  df10    rc_df10[NUM_DEVS_RC];
uint64          rc_buf[NUM_DEVS_RC][RM10_WDS];
uint32          rc_ipr[NUM_DEVS_RC];

t_stat          rc_devio(uint32 dev, uint64 *data);
t_stat          rc_svc(UNIT *);
t_stat          rc_boot(int32, DEVICE *);
void            rc_ini(UNIT *, t_bool);
t_stat          rc_reset(DEVICE *);
t_stat          rc_attach(UNIT *, CONST char *);
t_stat          rc_detach(UNIT *);
t_stat          rc_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat          rc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                     const char *cptr);
const char      *rc_description (DEVICE *dptr);


UNIT                rc_unit[] = {
/* Controller 1 */
    { UDATA (&rc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RD10_DTYPE << UNIT_V_DTYPE), RD10_SIZE) },
    { UDATA (&rc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RD10_DTYPE << UNIT_V_DTYPE), RD10_SIZE) },
    { UDATA (&rc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RD10_DTYPE << UNIT_V_DTYPE), RD10_SIZE) },
    { UDATA (&rc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RD10_DTYPE << UNIT_V_DTYPE), RD10_SIZE) },

#if (NUM_DEVS_RC > 1)
/* Controller 2 */
    { UDATA (&rc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RD10_DTYPE << UNIT_V_DTYPE), RD10_SIZE) },
    { UDATA (&rc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RD10_DTYPE << UNIT_V_DTYPE), RD10_SIZE) },
    { UDATA (&rc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RD10_DTYPE << UNIT_V_DTYPE), RD10_SIZE) },
    { UDATA (&rc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RD10_DTYPE << UNIT_V_DTYPE), RD10_SIZE) },
#endif
};

DIB rc_dib[] = {
    {RC_DEVNUM+000, 1, &rc_devio, NULL},
    {RC_DEVNUM+004, 1, &rc_devio, NULL}
    };

MTAB                rc_mod[] = {
    {UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL},
    {UNIT_DTYPE, (RD10_DTYPE << UNIT_V_DTYPE), "RD10", "RD10", &rc_set_type },
    {UNIT_DTYPE, (RM10_DTYPE << UNIT_V_DTYPE), "RM10", "RM10", &rc_set_type },
    {0}
};

REG                 rca_reg[] = {
    {BRDATA(BUFF, rc_buf[0], 16, 64, RM10_WDS), REG_HRO},
    {ORDATA(IPR, rc_ipr[0], 2), REG_HRO},
    {ORDATA(STATUS, rc_df10[0].status, 18), REG_RO},
    {ORDATA(CIA, rc_df10[0].cia, 18)},
    {ORDATA(CCW, rc_df10[0].ccw, 18)},
    {ORDATA(WCR, rc_df10[0].wcr, 18)},
    {ORDATA(CDA, rc_df10[0].cda, 18)},
    {ORDATA(DEVNUM, rc_df10[0].devnum, 9), REG_HRO},
    {ORDATA(BUF, rc_df10[0].buf, 36), REG_HRO},
    {ORDATA(NXM, rc_df10[0].nxmerr, 8), REG_HRO},
    {ORDATA(COMP, rc_df10[0].ccw_comp, 8), REG_HRO},
    {0}
};

DEVICE              rca_dev = {
    "FHA", rc_unit, rca_reg, rc_mod,
    NUM_UNITS_RC, 8, 18, 1, 8, 36,
    NULL, NULL, &rc_reset, &rc_boot, &rc_attach, &rc_detach,
    &rc_dib[0], DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &rc_help, NULL, NULL, &rc_description
};

#if (NUM_DEVS_RC > 1)
REG                 rcb_reg[] = {
    {BRDATA(BUFF, &rc_buf[1][0], 16, 64, RM10_WDS), REG_HRO},
    {ORDATA(IPR, rc_ipr[1], 2), REG_HRO},
    {ORDATA(STATUS, rc_df10[1].status, 18), REG_RO},
    {ORDATA(CIA, rc_df10[1].cia, 18)},
    {ORDATA(CCW, rc_df10[1].ccw, 18)},
    {ORDATA(WCR, rc_df10[1].wcr, 18)},
    {ORDATA(CDA, rc_df10[1].cda, 18)},
    {ORDATA(DEVNUM, rc_df10[1].devnum, 9), REG_HRO},
    {ORDATA(BUF, rc_df10[1].buf, 36), REG_HRO},
    {ORDATA(NXM, rc_df10[1].nxmerr, 8), REG_HRO},
    {ORDATA(COMP, rc_df10[1].ccw_comp, 8), REG_HRO},
    {0}
};

DEVICE              rcb_dev = {
    "FHB", &rc_unit[010], rcb_reg, rc_mod,
    NUM_UNITS_RC, 8, 18, 1, 8, 36,
    NULL, NULL, &rc_reset, &rc_boot, &rc_attach, &rc_detach,
    &rc_dib[1], DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &rc_help, NULL, NULL, &rc_description
};

#endif

DEVICE *rc_devs[] = {
    &rca_dev,
#if (NUM_DEVS_RC > 1)
    &rcb_dev,
#endif
};


t_stat rc_devio(uint32 dev, uint64 *data) {
     int          ctlr = (dev - RC_DEVNUM) >> 2;
     struct df10 *df10;
     UNIT        *uptr;
     DEVICE      *dptr;
     int          unit;
     int          tmp;
     int          drv;
     int          cyl;
     int          dtype;

     if (ctlr < 0 || ctlr >= NUM_DEVS_RC)
        return SCPE_OK;

     df10 = &rc_df10[ctlr];
     dptr = rc_devs[ctlr];
     switch(dev & 3) {
     case CONI:
        *data = df10->status;
#if KI_22BIT
        *data |= B22_FLAG;
#endif
        *data |= PRTLT;
        sim_debug(DEBUG_CONI, dptr, "HK %03o CONI %06o PC=%o\n", dev,
                          (uint32)*data, PC);
        break;
     case CONO:
         if (*data & PI_ENABLE)
             df10->status &= ~(PI_ENABLE);
         clr_interrupt(dev);
         df10->status &= ~07;
         df10->status |= *data & 07;
         df10->status &= ~(RST_MSK & *data);
         if ((*data & BUSY) != 0) {
             unit = rc_ipr[ctlr] & 3;
             drv = unit + (ctlr * NUM_UNITS_RC);
             uptr = &rc_unit[drv];
             if ((df10->status & BUSY) != 0) {
                  sim_cancel(uptr);
                  df10_finish_op(df10, 0);
             } else {
                  df10->status &= ~BUSY;
                  df10_setirq(df10);
             }
         }
         rc_ipr[ctlr] &= ~SEC_SCTR;
         rc_ipr[ctlr] |= *data & SEC_SCTR;

         if ((df10->status & BUSY) != 0 && (*data & CCW_COMP) != 0) {
            df10_writecw(df10);
         } else
            df10->status &= ~CCW_COMP;
         sim_debug(DEBUG_CONO, dptr, "HK %03o CONO %06o PC=%o %06o\n", dev,
                   (uint32)*data, PC, df10->status);
         break;
     case DATAI:
         *data = rc_ipr[ctlr];
         unit = (rc_ipr[ctlr] & SEC_SCTR) >> 16;
         uptr = &rc_unit[(ctlr * NUM_UNITS_RC) + unit];
         *data |= (uptr->UFLAGS >> 3) & 0177;
         sim_debug(DEBUG_DATAIO, dptr, "HK %03o DATI %012llo PC=%o F=%o\n",
                  dev, *data, PC, uptr->UFLAGS);
         break;
     case DATAO:
         sim_debug(DEBUG_DATAIO, dptr, "HK %03o DATO %012llo, PC=%o\n",
                  dev, *data, PC);
         if (df10->status & BUSY) {
            return SCPE_OK;
         }
         df10->status &= ~(PI_ENABLE|S_ERROR);
         clr_interrupt(RC_DEVNUM + (ctlr * 4));
         rc_ipr[ctlr] &= ~(INIT_PAR|3);
         rc_ipr[ctlr] |= *data & INIT_PAR;
         unit = (*data >> 34) & 03;
         rc_ipr[ctlr] |= unit;
         drv = unit + (ctlr * NUM_UNITS_RC);
         uptr = &rc_unit[drv];
         if ((uptr->flags & UNIT_ATT) == 0) {
            df10->status &= ~BUSY;
            df10->status |= NOT_RDY;
            df10_setirq(df10);
            return SCPE_OK;
         }
         if ((uptr->flags & UNIT_WPRT) && *data & WRITE) {
            df10->status &= ~BUSY;
            df10->status |= ILL_WR;
            df10_setirq(df10);
            return SCPE_OK;
         }
         df10_setup(df10, (uint32)*data);
         tmp = (uint32)(*data >> 15) & ~07;
         cyl = (tmp >> 10) & 0777;
         if (((cyl & 017) > 9) || (((cyl >> 4) & 017) > 9)) {
              sim_debug(DEBUG_DETAIL, dptr, "HK %d non-bcd cyl %02x\n",
                        ctlr, cyl);
              df10_finish_op(df10, TRK_SEL_E);
              return SCPE_OK;
         }
         cyl = (((cyl >> 4) & 017) * 10) + (cyl & 017) +
                ((cyl & 0x100) ? 100 : 0);
         dtype = GET_DTYPE(uptr->flags);
         if (cyl >= rc_drv_tab[dtype].cyl) {
              sim_debug(DEBUG_DETAIL, dptr, "HK %d invalid cyl %d %d\n",
                       ctlr, cyl, rc_drv_tab[dtype].cyl);
              df10_finish_op(df10, TRK_SEL_E);
              return SCPE_OK;
         }
         cyl = (tmp >> 3) & 0177;
         if ((cyl & 017) > 9) {
              sim_debug(DEBUG_DETAIL, dptr, "HK %d non-bcd seg %02x\n",
                       ctlr, cyl);
              df10_finish_op(df10, TRK_SEL_E);
              return SCPE_OK;
         }
         uptr->UFLAGS =  tmp | ((*data & WRITE) != 0) | (ctlr << 1);
         uptr->DATAPTR = -1;    /* Set no data */
         if ((*data & WRITE) != 0)
            (void)df10_read(df10);
         sim_debug(DEBUG_DETAIL, dptr, "HK %d cyl %o\n", ctlr, uptr->UFLAGS);
         sim_activate(uptr, 100);
        break;
    }
    return SCPE_OK;
}


t_stat rc_svc (UNIT *uptr)
{
   int           dtype = GET_DTYPE(uptr->flags);
   int           ctlr  = (uptr->UFLAGS >> 1) & 03;
   int           seg   = (uptr->UFLAGS >> 3) & 0177;
   int           cyl   = (uptr->UFLAGS >> 10) & 0777;
   int           wr    = (uptr->UFLAGS & 1);
   int           seg_size = rc_drv_tab[dtype].wd_seg;
   struct df10  *df10  = &rc_df10[ctlr];
   int           tmp, wc;
   DEVICE       *dptr;
   t_stat        err, r;

   dptr = rc_devs[ctlr];
   /* Check if we need to seek */
   if (uptr->DATAPTR == -1) {
        cyl = (((cyl >> 4) & 017) * 10) + (cyl & 017) +
                ((cyl & 0x100) ? 100 : 0);
        if (cyl >= rc_drv_tab[dtype].cyl) {
              sim_debug(DEBUG_DETAIL, dptr, "HK %d invalid cyl %d %d %o\n",
                       ctlr, cyl, rc_drv_tab[dtype].cyl, uptr->UFLAGS);
              df10_finish_op(df10, TRK_SEL_E);
              return SCPE_OK;
        }
        /* Convert segment from BCD to binary */
        if ((seg & 017) > 10) {
              sim_debug(DEBUG_DETAIL, dptr, "HK %d non-bcd seg %02x %d %o\n",
                       ctlr, seg, rc_drv_tab[dtype].seg, uptr->UFLAGS);
              df10_finish_op(df10, S_ERROR);
              return SCPE_OK;
        }
        seg = (((seg >> 4) & 07) * 10) + (seg & 017);
        if (seg >= rc_drv_tab[dtype].seg) {
              sim_debug(DEBUG_DETAIL, dptr, "HK %d invalid sec %d %d %o\n",
                       ctlr, seg, rc_drv_tab[dtype].seg, uptr->UFLAGS);
              df10_finish_op(df10, S_ERROR);
              return SCPE_OK;
        }
        /* Check if reading */
        if (!wr) {
                 /* Read the block */
           int da;
           da = ((cyl * rc_drv_tab[dtype].seg) + seg) * seg_size;
           err = sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
           wc = sim_fread (&rc_buf[ctlr][0], sizeof(uint64),
                        seg_size, uptr->fileref);
           sim_debug(DEBUG_DETAIL, dptr, "HK %d Read %d %d %d %x\n",
                ctlr, da, cyl, seg, uptr->UFLAGS << 1 );
           for (; wc < seg_size; wc++)
                rc_buf[ctlr][wc] = 0;
         }
         uptr->DATAPTR = 0;
         df10->status |= SCRCHCMP;
    }
    if (wr) {
        rc_buf[ctlr][uptr->DATAPTR] = df10->buf;
        r = df10_read(df10);
    } else {
        df10->buf = rc_buf[ctlr][uptr->DATAPTR];
        r = df10_write(df10);
    }
    sim_debug(DEBUG_DATA, dptr, "Xfer %d %012llo %06o %06o\n", uptr->DATAPTR, df10->buf,
             df10->wcr, df10->cda);

    uptr->DATAPTR++;
    if (uptr->DATAPTR >= seg_size || r == 0 ) {
        /* Check if writing */
        df10->status &= ~SCRCHCMP;
        seg = (((seg >> 4) & 017) * 10) + (seg & 017);
        cyl = (((cyl >> 4) & 017) * 10) + (cyl & 017) +
                ((cyl & 0x100) ? 100 : 0);
        if (wr) {
             int da;

             while(uptr->DATAPTR < seg_size) {
                 rc_buf[ctlr][uptr->DATAPTR] = 0;
                 uptr->DATAPTR++;
             }
             da = ((cyl * rc_drv_tab[dtype].seg) + seg) * seg_size;
             sim_debug(DEBUG_DETAIL, dptr, "HK %d Write %d %d %d %x %d\n",
                  ctlr, da, cyl, seg, uptr->UFLAGS << 1, uptr->DATAPTR );
             err = sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
             wc = sim_fwrite(&rc_buf[ctlr][0],sizeof(uint64),
                        seg_size, uptr->fileref);
        }
        uptr->DATAPTR = -1;
        seg++;
        if (seg >= rc_drv_tab[dtype].seg) {
           seg = 0;
           cyl++;
           if (cyl >= rc_drv_tab[dtype].cyl)
              cyl = 0;
        }
        /* Convert seg back to bcd */
        tmp = seg % 10;
        seg /= 10;
        seg <<= 4;
        seg += tmp;
        wr = 0;
        if (cyl >= 100) {
            wr = 0x100;
            cyl -= 100;
        }
        tmp = (cyl % 10);
        cyl /= 10;
        cyl <<= 4;
        cyl += wr + tmp;
        uptr->UFLAGS = (uptr->UFLAGS & 7) + (seg << 3) + (cyl << 10);
    }
    if ((df10->status & PI_ENABLE) == 0) {
        sim_activate(uptr, 20);
    }
    return SCPE_OK;
}


t_stat
rc_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int         i;
    if (uptr == NULL) return SCPE_IERR;
    for (i = 0; rc_drv_tab[i].wd_seg != 0; i++) {
        if ((val >> UNIT_V_DTYPE) == rc_drv_tab[i].devtype) {
            uptr->capac = rc_drv_tab[i].size;
            return SCPE_OK;
        }
    }
    return SCPE_IERR;
}


t_stat
rc_reset(DEVICE * dptr)
{
    int unit;
    int ctlr;
    UNIT *uptr = dptr->units;
    for(unit = 0; unit < NUM_UNITS_RC; unit++) {
         uptr->UFLAGS  = 0;
         uptr->CUR_CYL = 0;
         uptr++;
    }
    for (ctlr = 0; ctlr < NUM_DEVS_RC; ctlr++) {
        rc_ipr[ctlr] = 0;
        rc_df10[ctlr].status = 0;
        rc_df10[ctlr].devnum = rc_dib[ctlr].dev_num;
        rc_df10[ctlr].nxmerr = 8;
        rc_df10[ctlr].ccw_comp = 5;
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
rc_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    int                 dtype = GET_DTYPE(uptr->flags);
    uint32              addr;
    int                 wc;
    int                 wps;
    int                 seg;
    int                 sect;
    uint32              ptr;

   addr = (MEMSIZE - 512) & RMASK;
   wps = rc_drv_tab[dtype].wd_seg;
   for (sect = 4; sect <= 7; sect++) {
       seg = (sect * 128) / wps;
       (void)sim_fseek(uptr->fileref, (seg * wps) * sizeof(uint64), SEEK_SET);
       (void)sim_fread (&rc_buf[0][0], sizeof(uint64), wps, uptr->fileref);
       ptr = 0;
       for(wc = wps; wc > 0; wc--) {
          M[addr++] = rc_buf[0][ptr++];
       }
    }
    PC = (MEMSIZE - 512) & RMASK;
    return SCPE_OK;
}

/* Device attach */

t_stat rc_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

uptr->capac = rc_drv_tab[GET_DTYPE (uptr->flags)].size;
r = attach_unit (uptr, cptr);
if (r != SCPE_OK || (sim_switches & SIM_SW_REST) != 0)
    return r;
uptr->CUR_CYL = 0;
uptr->UFLAGS = 0;
return SCPE_OK;
}

/* Device detach */

t_stat rc_detach (UNIT *uptr)
{
if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
if (sim_is_active (uptr))                              /* unit active? */
    sim_cancel (uptr);                                  /* cancel operation */
return detach_unit (uptr);
}

t_stat rc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "RD10/RM10  Disk Pack Drives (RC)\n\n");
fprintf (st, "The RC controller implements the RC-10 disk controller that talked\n");
fprintf (st, "to either RD10 mountable pack or RM10 drum drives.\n");
fprintf (st, "Options include the ability to set units write enabled or write locked, to\n");
fprintf (st, "set the drive type to one of two disk types\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.\n");
fprintf (st, "The RC device supports the BOOT command.\n");
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *rc_description (DEVICE *dptr)
{
return "RD10/RM10 disk controller";
}

#endif
