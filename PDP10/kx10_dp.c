/* kx10_dp.c: Dec Data Products Disk Drive.

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
#include "kx10_disk.h"

#ifndef NUM_DEVS_DP
#define NUM_DEVS_DP 0
#endif

#if (NUM_DEVS_DP > 0)

#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark = 0xFFFFFFFF

#define RP_NUMWD        128     /* 36bit words/sec */
#define DP_DEVNUM       0250    /* First device number */
#define NUM_UNITS_DP    8

/* Flags in the unit flags word */

#define DEV_WHDR        (1 << DEV_V_UF)                /* Enable write headers */
#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* disk type */
#define UNIT_M_DTYPE    3
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)

/* Parameters in the unit descriptor */

#define CUR_CYL         u3              /* current cylinder */
#define DATAPTR         u4              /* data pointer */
#define UFLAGS          u5              /* Function */
#define STATUS          u6              /* Drive status */
#define SECT_CNT        up7             /* Sector counter */
#define CONTROL         007
#define CMD_MASK        070
#define SEEK_DONE       0100            /* Seek finished */
#define SEEK_STATE      0200            /* Seek in progress */
#define DONE            0400            /* Done bit */


/* CONI/CONO Flags */
#define SUF_ERR         0000000000100LL
#define SEC_ERR         0000000000200LL
#define ILL_CMD         0000000000400LL
#define ILL_WR          0000000001000LL
#define NOT_RDY         0000000002000LL      /* Clear CXR */
#define PRT_ERR         0000000004000LL      /* 14-17 Clear CCPE, DSPE, DISK WDPE, CDPE */
#define NXM_ERR         0000000010000LL
#define SLW_CHN         0000000020000LL
#define SRC_ERR         0000000040000LL
#define PWR_FAIL_10     0000000100000LL
#define END_CYL         0000000200000LL      /* No effect */
#define SRC_DONE        0000000400000LL      /* No effect */
#define DSK_PRTY        0000001000000LL      /* No effect */
#define CHN_PRTY        0000002000000LL      /* No effect */
#define SEC_PRTY        0000004000000LL      /* No effect */
#define CCW_PRTY        0000010000000LL      /* No effect */
#define B22_FLAG        0000020000000LL

#define CLRMSK          0000000177710LL
#define CLRMSK2         0000176000000LL

/* DATAO */
#define DWPE_STOP       0000000001000LL
#define SPARE           0000000002000LL
#define DSPE_STOP       0000000004000LL
#define SECTOR          0000000170000LL
#define CYL256          0000000200000LL
#define SURFACE         0000017400000LL
#define CYL             0007760000000LL
#define DRIVE           0070000000000LL
#define OP              0700000000000LL

#define RD      0
#define WR      1
#define RV      2
#define WH      3
#define SK      4
#define CL      5
#define NO      6
#define RC      7

/* DATAI Flags */
#define ATTN            0000000000776LL
#define DEFECT          0000000001000LL
#define SEL_RP03        0000000002000LL
#define SEL_CYL256      0000000004000LL
#define SEL_SPARE       0000000010000LL
#define SEL_SEC         0000000760000LL
#define WR_HD_LK        0000001000000LL
#define RD_ONLY         0000002000000LL
#define NO_DRIVE        0000004000000LL
#define FILE_UNSAFE     0000010000000LL
#define DRV_ONLINE      0000020000000LL
#define ON_CYL          0000040000000LL
#define SEEK_INC        0000100000000LL
#define SEL_CYL         0077600000000LL
#define SEL_DRIVE       0700000000000LL

#define RP01_DTYPE      0
#define RP01_SECT       5
#define RP01_SURF       10
#define RP01_CYL        203
#define RP01_DEV        0
#define RP01_SIZE       (RP01_SECT * RP01_SURF * RP01_CYL * RP_NUMWD)

#define RP02_DTYPE      1
#define RP02_SECT       10
#define RP02_SURF       20
#define RP02_CYL        203
#define RP02_DEV        0
#define RP02_SIZE       (RP02_SECT * RP02_SURF * RP02_CYL * RP_NUMWD)

#define RP03_DTYPE      2
#define RP03_SECT       10
#define RP03_SURF       20
#define RP03_CYL        406
#define RP03_DEV        1
#define RP03_SIZE       (RP03_SECT * RP03_SURF * RP03_CYL * RP_NUMWD)

struct drvtyp {
    int32       sect;                                   /* sectors */
    int32       surf;                                   /* surfaces */
    int32       cyl;                                    /* cylinders */
    int32       size;                                   /* #blocks */
    int32       devtype;                                /* device type */
    };

struct drvtyp dp_drv_tab[] = {
    { RP01_SECT, RP01_SURF, RP01_CYL, RP01_SIZE, RP01_DTYPE},
    { RP02_SECT, RP02_SURF, RP02_CYL, RP02_SIZE, RP02_DTYPE},
    { RP03_SECT, RP03_SURF, RP03_CYL, RP03_SIZE, RP03_DTYPE},
    { 0 }
    };


struct df10   dp_df10[NUM_DEVS_DP];
uint32        dp_cur_unit[NUM_DEVS_DP];
uint64        dp_buf[NUM_DEVS_DP][RP_NUMWD];
int           readin_flag = 0;

t_stat        dp_devio(uint32 dev, uint64 *data);
t_stat        dp_svc(UNIT *);
t_stat        dp_boot(int32, DEVICE *);
void          dp_ini(UNIT *, t_bool);
t_stat        dp_reset(DEVICE *);
t_stat        dp_attach(UNIT *, CONST char *);
t_stat        dp_detach(UNIT *);
t_stat        dp_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat        dp_set_hdr(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat        dp_show_hdr(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat        dp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                 const char *cptr);
const char    *dp_description (DEVICE *dptr);

UNIT                dp_unit[] = {
/* Controller 1 */
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },

#if (NUM_DEVS_DP > 1)
/* Controller 2 */
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
#if (NUM_DEVS_DP > 2)
/* Controller 3 */
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
#if (NUM_DEVS_DP > 3)
/* Controller 4 */
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
#endif
#endif
#endif
};

DIB dp_dib[] = {
    {DP_DEVNUM+000, 1, &dp_devio, NULL},
    {DP_DEVNUM+004, 1, &dp_devio, NULL},
    {DP_DEVNUM+010, 1, &dp_devio, NULL},
    {DP_DEVNUM+014, 1, &dp_devio, NULL}};


MTAB                dp_mod[] = {
    {UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL},
    {MTAB_XTD|MTAB_VDV, 0, NULL, "NOHEADERS",
            &dp_set_hdr, &dp_show_hdr, NULL, "Disable header writing"},
    {MTAB_XTD|MTAB_VDV, DEV_WHDR, "write header", "HEADERS",
            &dp_set_hdr, &dp_show_hdr, NULL, "Enable header writing"},
    {UNIT_DTYPE, (RP03_DTYPE << UNIT_V_DTYPE), "RP03", "RP03", &dp_set_type },
    {UNIT_DTYPE, (RP02_DTYPE << UNIT_V_DTYPE), "RP02", "RP02", &dp_set_type },
    {UNIT_DTYPE, (RP01_DTYPE << UNIT_V_DTYPE), "RP01", "RP01", &dp_set_type },
    {MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT", NULL, &disk_show_fmt }, 
    {0},
};

REG                 dpa_reg[] = {
    {BRDATA(BUFF, dp_buf[0], 16, 64, RP_NUMWD), REG_HRO},
    {HRDATA(UNIT, dp_cur_unit[0], 32), REG_HRO},
    {FLDATA(READIN, readin_flag, 0), REG_HRO},
    {ORDATA(STATUS, dp_df10[0].status, 18), REG_RO},
    {ORDATA(CIA, dp_df10[0].cia, 18)},
    {ORDATA(CCW, dp_df10[0].ccw, 18)},
    {ORDATA(WCR, dp_df10[0].wcr, 18)},
    {ORDATA(CDA, dp_df10[0].cda, 18)},
    {ORDATA(DEVNUM, dp_df10[0].devnum, 9), REG_HRO},
    {ORDATA(BUF, dp_df10[0].buf, 36), REG_HRO},
    {ORDATA(NXM, dp_df10[0].nxmerr, 8), REG_HRO},
    {ORDATA(COMP, dp_df10[0].ccw_comp, 8), REG_HRO},
    {0}
};

DEVICE              dpa_dev = {
    "DPA", dp_unit, dpa_reg, dp_mod,
    NUM_UNITS_DP, 8, 18, 1, 8, 36,
    NULL, NULL, &dp_reset, &dp_boot, &dp_attach, &dp_detach,
    &dp_dib[0], DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dp_help, NULL, NULL, &dp_description
};

#if (NUM_DEVS_DP > 1)
REG                 dpb_reg[] = {
    {BRDATA(BUFF, dp_buf[1], 16, 64, RP_NUMWD), REG_HRO},
    {HRDATA(DF10, dp_cur_unit[1], 32), REG_HRO},
    {ORDATA(STATUS, dp_df10[1].status, 18), REG_RO},
    {ORDATA(CIA, dp_df10[1].cia, 18)},
    {ORDATA(CCW, dp_df10[1].ccw, 18)},
    {ORDATA(WCR, dp_df10[1].wcr, 18)},
    {ORDATA(CDA, dp_df10[1].cda, 18)},
    {ORDATA(DEVNUM, dp_df10[1].devnum, 9), REG_HRO},
    {ORDATA(BUF, dp_df10[1].buf, 36), REG_HRO},
    {ORDATA(NXM, dp_df10[1].nxmerr, 8), REG_HRO},
    {ORDATA(COMP, dp_df10[1].ccw_comp, 8), REG_HRO},
    {0}
};

DEVICE              dpb_dev = {
    "DPB", &dp_unit[010], dpb_reg, dp_mod,
    NUM_UNITS_DP, 8, 18, 1, 8, 36,
    NULL, NULL, &dp_reset, &dp_boot, &dp_attach, &dp_detach,
    &dp_dib[1], DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dp_help, NULL, NULL, &dp_description
};

#if (NUM_DEVS_DP > 2)
REG                 dpc_reg[] = {
    {BRDATA(BUFF, dp_buf[2], 16, 64, RP_NUMWD), REG_HRO},
    {HRDATA(DF10, dp_cur_unit[2], 32), REG_HRO},
    {ORDATA(STATUS, dp_df10[2].status, 18), REG_RO},
    {ORDATA(CIA, dp_df10[2].cia, 18)},
    {ORDATA(CCW, dp_df10[2].ccw, 18)},
    {ORDATA(WCR, dp_df10[2].wcr, 18)},
    {ORDATA(CDA, dp_df10[2].cda, 18)},
    {ORDATA(DEVNUM, dp_df10[2].devnum, 9), REG_HRO},
    {ORDATA(BUF, dp_df10[2].buf, 36), REG_HRO},
    {ORDATA(NXM, dp_df10[2].nxmerr, 8), REG_HRO},
    {ORDATA(COMP, dp_df10[2].ccw_comp, 8), REG_HRO},
    {0}
};

DEVICE              dpc_dev = {
    "DPC", &dp_unit[020], dpc_reg, dp_mod,
    NUM_UNITS_DP, 8, 18, 1, 8, 36,
    NULL, NULL, &dp_reset, &dp_boot, &dp_attach, &dp_detach,
    &dp_dib[2], DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dp_help, NULL, NULL, &dp_description
};

#if (NUM_DEVS_DP > 3)
REG                 dpd_reg[] = {
    {BRDATA(BUFF, &dp_buf[3][0], 16, 64, RP_NUMWD), REG_HRO},
    {BRDATA(DF10, dp_cur_unit[3], 32), REG_HRO},
    {ORDATA(STATUS, dp_df10[3].status, 18), REG_RO},
    {ORDATA(CIA, dp_df10[3].cia, 18)},
    {ORDATA(CCW, dp_df10[3].ccw, 18)},
    {ORDATA(WCR, dp_df10[3].wcr, 18)},
    {ORDATA(CDA, dp_df10[3].cda, 18)},
    {ORDATA(DEVNUM, dp_df10[3].devnum, 9), REG_HRO},
    {ORDATA(BUF, dp_df10[3].buf, 36), REG_HRO},
    {ORDATA(NXM, dp_df10[3].nxmerr, 8), REG_HRO},
    {ORDATA(COMP, dp_df10[3].ccw_comp, 8), REG_HRO},
    {0}
};

DEVICE              dpd_dev = {
    "DPD", &dp_unit[030], dpd_reg, dp_mod,
    NUM_UNITS_DP, 8, 18, 1, 8, 36,
    NULL, NULL, &dp_reset, &dp_boot, &dp_attach, &dp_detach,
    &dp_dib[3], DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dp_help, NULL, NULL, &dp_description
};

#endif
#endif
#endif

DEVICE *dp_devs[] = {
    &dpa_dev,
#if (NUM_DEVS_DP > 1)
    &dpb_dev,
#if (NUM_DEVS_DP > 2)
    &dpc_dev,
#if (NUM_DEVS_DP > 3)
    &dpd_dev,
#endif
#endif
#endif
};


t_stat dp_devio(uint32 dev, uint64 *data) {
     uint64         res;
     int            ctlr = (dev - DP_DEVNUM) >> 2;
     struct df10   *df10;
     DEVICE        *dptr;
     UNIT          *uptr;
     int            unit;
     int            cyl;
     int            tmp;
     static int     sect_count;

     if (ctlr < 0 || ctlr >= NUM_DEVS_DP)
        return SCPE_OK;

     df10 = &dp_df10[ctlr];
     dptr = dp_devs[ctlr];
     unit = dp_cur_unit[ctlr];
     uptr = &dp_unit[(ctlr * NUM_UNITS_DP) + unit];
     switch(dev & 3) {
     case CONI:
        *data = (uint64)(df10->status | uptr->STATUS);
#if KI_22BIT
        *data |= B22_FLAG;
#endif
        sim_debug(DEBUG_CONI, dptr, "DP %03o CONI %012llo %d PC=%o\n", dev,
                           *data, ctlr, PC);
        break;

     case CONO:
         clr_interrupt(dev);
         df10->status &= ~07LL;
         df10->status |= *data & 07LL;
         if (*data & BUSY) {
             /* Stop controller */
             sim_cancel(uptr);
             df10_finish_op(df10, 0);
             uptr->STATUS &= ~(BUSY);
         }
         /* Clear flags */
         uptr->STATUS &= ~(*data & CLRMSK);
         if (*data & PRT_ERR)
            uptr->STATUS &= ~(CLRMSK2);
         if (*data & CCW_COMP) {
            df10_writecw(df10);
            df10->status &= ~CCW_COMP;
         }
         if (*data & PI_ENABLE) {
             uptr->UFLAGS &= ~DONE;
             /* Check if any drives still reporting seek done */
             tmp = 1;
             uptr = &dp_unit[ctlr * NUM_UNITS_DP];
             for(unit = 0; unit < NUM_UNITS_DP; unit++) {
                if (uptr->UFLAGS & SEEK_DONE) {
                     tmp = 0;
                     break;
                }
                uptr++;
             }
             if (tmp)
                 df10->status &= ~PI_ENABLE;
             else
                 df10_setirq(df10);
         }
         sim_debug(DEBUG_CONO, dptr, "DP %03o CONO %06o %d PC=%o %06o\n", dev,
                 (uint32)*data, ctlr, PC, df10->status);
         break;

     case DATAI:
         res = (uint64)(unit) << 33;
         if ((dptr->flags & DEV_WHDR) == 0)
             res |= WR_HD_LK;                  /* Can't write headers. */
         if (dp_drv_tab[GET_DTYPE(uptr->flags)].devtype == RP03_DTYPE)
             res |= SEL_RP03;
         if (uptr->flags & UNIT_DIS) {
             res |= NO_DRIVE;
         } else if (uptr->flags & UNIT_ATT) {
             res |= DRV_ONLINE;
             cyl = uptr->CUR_CYL;
             res |= ((uint64)(cyl & 0377)) << 25;
             if (cyl & 0400)
                res |= SEL_CYL256;
             if (sect_count > 20)
                sect_count = 0;
             res |= SEL_SEC & (uint64)(sect_count << 13);
             sect_count++;
             if ((uptr->UFLAGS & SEEK_STATE) == 0)
                res |= ON_CYL;
             if (uptr->flags & UNIT_WPRT)
                res |= RD_ONLY|WR_HD_LK;
         }
         uptr = &dp_unit[ctlr * NUM_UNITS_DP];
         for(unit = 0; unit < NUM_UNITS_DP; unit++) {
            if (uptr->UFLAGS & SEEK_DONE)
                res |= 0400>>unit;
            uptr++;
         }
         sim_debug(DEBUG_DATAIO, dptr, "DP %03o DATI %012llo %d  PC=%o F=%o %o\n",
                 dev, res, ctlr, PC, uptr->UFLAGS, sect_count);
         *data = res;
         break;

     case DATAO:
         sim_debug(DEBUG_DATAIO, dptr, "DP %03o DATO %012llo, %d PC=%o\n",
                 dev, *data, ctlr, PC);
         if (df10->status & BUSY) {
            uptr->STATUS |= ILL_CMD;
            return SCPE_OK;
         }
         clr_interrupt(dev);
         df10->status &= ~(PI_ENABLE|CCW_COMP);
         unit = (*data >> 30) & 07;
         dp_cur_unit[ctlr] = unit;
         uptr = &dp_unit[(ctlr * NUM_UNITS_DP) + unit];
         if ((uptr->STATUS & NOT_RDY) == 0)  {
              uptr->STATUS &= ~(SUF_ERR|SEC_ERR|SRC_ERR|NXM_ERR|ILL_WR|
                          NO_DRIVE|NOT_RDY|ILL_CMD|END_CYL|SRC_DONE);
         }
         cyl = ((*data >> 22) & 0377);
         if (*data & CYL256)
            cyl += 0400;
         tmp = (*data >> 33) & 07;
         switch(tmp) {
         case WH:
             if ((dptr->flags & DEV_WHDR) == 0) {
                uptr->UFLAGS |= DONE;
                uptr->STATUS |= ILL_WR;
                df10_setirq(df10);
                return SCPE_OK;
             }
             *data &= ~SECTOR;    /* Clear sector */
             /* Fall through */

         case WR:
             if (uptr->flags & UNIT_WPRT) {
                uptr->UFLAGS |= DONE;
                uptr->STATUS |= ILL_WR;
                df10_setirq(df10);
                return SCPE_OK;
             }
             /* Fall through */

         case RD:
         case RV:
             if (uptr->flags & UNIT_DIS) {
                uptr->UFLAGS |= DONE;
                uptr->STATUS |= NO_DRIVE;
                df10_setirq(df10);
                return SCPE_OK;
             }
             if ((uptr->flags & UNIT_ATT) == 0) {
                uptr->UFLAGS |= DONE;
                uptr->STATUS |= NOT_RDY;
                df10_setirq(df10);
                return SCPE_OK;
             }
             uptr->UFLAGS = ((*data & (SURFACE|SECTOR)) >> 3) | (cyl << 20)
                              | (tmp << 3) | ctlr;
             uptr->DATAPTR = 0;      /* Set no data */
             CLR_BUF(uptr);
             df10_setup(df10, (uint32)*data);
             uptr->STATUS |= BUSY;
             break;

         case RC:
             cyl = 0;
             /* Fall through */

         case SK:
             uptr->STATUS |= NOT_RDY;
             if ((uptr->flags & UNIT_ATT) == 0)
                return SCPE_OK;
             uptr->UFLAGS = (cyl << 20) | (tmp<<3) | ctlr | SEEK_STATE;
             break;

         case CL:
             uptr->UFLAGS &= ~DONE;
             uptr = &dp_unit[ctlr * NUM_UNITS_DP];
             for(unit = 0; unit < NUM_UNITS_DP; unit++) {
                if (*data & (0400 >> unit))
                   uptr->UFLAGS &= ~(SEEK_DONE);
                uptr++;
             }
             /* Fall through */

         case NO:
             tmp = 0;
             uptr = &dp_unit[ctlr * NUM_UNITS_DP];
             for(unit = 0; unit < NUM_UNITS_DP; unit++) {
                if (uptr->UFLAGS & SEEK_DONE) {
                     tmp = 1;
                     break;
                }
                uptr++;
             }
             if (tmp) {
                 df10_setirq(df10);
             }
             return SCPE_OK;
         }
         sim_activate(uptr, 150);
    }
    return SCPE_OK;
}


t_stat dp_svc (UNIT *uptr)
{
   int         dtype = GET_DTYPE(uptr->flags);
   int         ctlr  = uptr->UFLAGS & 03;
   int         cmd   = (uptr->UFLAGS & 070) >> 3;
   int         sect  = (uptr->UFLAGS >> 9);
   int         surf  = (sect >> 5) & 037;
   int         cyl   = (uptr->UFLAGS >> 20) & 0777;
   DEVICE      *dptr = dp_devs[ctlr];
   struct df10 *df10 = &dp_df10[ctlr];
   int diff, diffs;
   int         r;
   sect &= 017;

   switch(cmd) {
   case WR:
   case RV:
   case RD:
           /* Cylinder, Surface, Sector all ok */
           if (BUF_EMPTY(uptr)) {
                 sim_debug(DEBUG_DETAIL, dptr,
                       "DP %d cmd=%o cyl=%d (%o) sect=%d surf=%d %d\n",
                        ctlr, uptr->UFLAGS, cyl, cyl, sect, surf,uptr->CUR_CYL);
                uptr->STATUS |= SRC_DONE;
                if (uptr->STATUS & END_CYL) {
                     if (cmd == WR) {
                          if(df10_read(df10))
                              df10_read(df10);
                     }
                     uptr->UFLAGS |= DONE;
                     uptr->STATUS &= ~BUSY;
                     df10_finish_op(df10, 0);
                     return SCPE_OK;
                }
                if (sect >= dp_drv_tab[dtype].sect) {
                     uptr->STATUS &= ~BUSY;
                     uptr->STATUS |= SEC_ERR;
                }
                if (surf >= dp_drv_tab[dtype].surf) {
                     uptr->STATUS &= ~BUSY;
                     uptr->STATUS |= SUF_ERR;
                }
                if (cyl != uptr->CUR_CYL) {
                     uptr->STATUS &= ~BUSY;
                     uptr->STATUS |= SRC_ERR;
                }
                if ((uptr->STATUS & BUSY) == 0) {
                     uptr->UFLAGS |= DONE;
                     df10_finish_op(df10, 0);
                     return SCPE_OK;
                }
                if (cmd != WR) {
                    /* Read the block */
                    int da = ((cyl * dp_drv_tab[dtype].surf + surf)
                                   * dp_drv_tab[dtype].sect + sect);
                    (void)disk_read(uptr, &dp_buf[ctlr][0], da, RP_NUMWD);
                    uptr->hwmark = RP_NUMWD;
                    uptr->DATAPTR = 0;
                    sect = sect + 1;
                    if (sect >= dp_drv_tab[dtype].sect) {
                        sect = 0;
                        surf = surf + 1;
                        if (surf >= dp_drv_tab[dtype].surf) {
                           uptr->STATUS |= END_CYL;
                        } else {
                           uptr->UFLAGS &= ~(01757000);
                           uptr->UFLAGS |= (surf << 14);
                        }
                    } else {
                        uptr->UFLAGS &= ~(017000);
                        uptr->UFLAGS |= (sect << 9);
                    }
                } else {
                    uptr->DATAPTR = 0;
                    uptr->hwmark = 0;
                }
                sim_activate(uptr, 50);
                return SCPE_OK;
           }
           switch(cmd) {
           case WR:
               r = df10_read(df10);
               if (r)
                   uptr->hwmark = uptr->DATAPTR;
               dp_buf[ctlr][uptr->DATAPTR] = df10->buf;
               break;
           case RV:
           case RD:
               df10->buf = dp_buf[ctlr][uptr->DATAPTR];
               r = df10_write(df10);
               break;
           }
           sim_debug(DEBUG_DATA, dptr, "Xfer %d %08o %012llo %08o\n",
                          uptr->DATAPTR, df10->cda, df10->buf, df10->wcr);
           uptr->DATAPTR++;
           if (uptr->DATAPTR >= RP_NUMWD || r == 0 ) {
               if (cmd == WR) {
                    int da = ((cyl * dp_drv_tab[dtype].surf + surf)
                                   * dp_drv_tab[dtype].sect + sect);
                    /* write block the block */
                    for (; uptr->DATAPTR < RP_NUMWD; uptr->DATAPTR++)
                        dp_buf[ctlr][uptr->DATAPTR] = 0;
                    (void)disk_write(uptr, &dp_buf[ctlr][0], da, RP_NUMWD);
                    uptr->STATUS |= SRC_DONE;
                    sect = sect + 1;
                    if (sect >= dp_drv_tab[dtype].sect) {
                        sect = 0;
                        surf = surf + 1;
                        if (surf >= dp_drv_tab[dtype].surf) {
                           uptr->STATUS |= END_CYL;
                        } else {
                           uptr->UFLAGS &= ~(01757 << 9);
                           uptr->UFLAGS |= (surf << 14);
                        }
                    } else {
                        uptr->UFLAGS &= ~(017 << 9);
                        uptr->UFLAGS |= (sect << 9);
                    }
                }
                uptr->DATAPTR = 0;
                CLR_BUF(uptr);
           }
           if (r)
               sim_activate(uptr, 40);
           else {
               uptr->STATUS &= ~(SRC_DONE|END_CYL|BUSY);
               uptr->UFLAGS |= DONE;
               return SCPE_OK;
           }
           break;
   case WH:
           /* Cylinder, Surface, Sector all ok */
           if (BUF_EMPTY(uptr)) {
                if (uptr->DATAPTR == 0)
                    sim_debug(DEBUG_DETAIL, dptr,
                       "DP %d cmd=%o cyl=%d (%o) sect=%d surf=%d %d\n",
                        ctlr, uptr->UFLAGS, cyl, cyl, sect, surf,uptr->CUR_CYL);
                uptr->STATUS |= SRC_DONE;
                if (uptr->STATUS & END_CYL) {
                     if(df10_read(df10))
                        df10_read(df10);
                     uptr->UFLAGS |= DONE;
                     uptr->STATUS &= ~BUSY;
                     df10_finish_op(df10, 0);
                     return SCPE_OK;
                }
                if (sect >= dp_drv_tab[dtype].sect) {
                     uptr->STATUS &= ~BUSY;
                     uptr->STATUS |= SEC_ERR;
                }
                if (surf >= dp_drv_tab[dtype].surf) {
                     uptr->STATUS &= ~BUSY;
                     uptr->STATUS |= SUF_ERR;
                }
                if (cyl != uptr->CUR_CYL) {
                     uptr->STATUS &= ~BUSY;
                     uptr->STATUS |= SRC_ERR;
                }
                if ((uptr->STATUS & BUSY) == 0) {
                     uptr->UFLAGS |= DONE;
                     df10_finish_op(df10, 0);
                     return SCPE_OK;
                }
                r = df10_read(df10);
                uptr->DATAPTR++;
                sim_debug(DEBUG_DATA, dptr, "Xfer h%d %012llo\n",
                          uptr->DATAPTR, df10->buf);
                if (uptr->DATAPTR == 36) {
                    uptr->DATAPTR = 0;
                    uptr->hwmark = 0;
                }
           } else {
                r = df10_read(df10);
                if (r)
                    uptr->hwmark = uptr->DATAPTR;
                dp_buf[ctlr][uptr->DATAPTR] = (df10->buf << 1) & FMASK;
                sim_debug(DEBUG_DATA, dptr, "Xfer %d %012llo\n",
                               uptr->DATAPTR, df10->buf);
                uptr->DATAPTR++;
                if (uptr->DATAPTR >= RP_NUMWD || r == 0 ) {
                     int da = ((cyl * dp_drv_tab[dtype].surf + surf)
                                    * dp_drv_tab[dtype].sect + sect);
                     /* write block the block */
                     for (; uptr->DATAPTR < RP_NUMWD; uptr->DATAPTR++)
                         dp_buf[ctlr][uptr->DATAPTR] = 0;
                     (void)disk_write(uptr, &dp_buf[ctlr][0], da, RP_NUMWD);
                     uptr->STATUS |= SRC_DONE;
                     sect = sect + 1;
                     if (sect >= dp_drv_tab[dtype].sect) {
                         uptr->STATUS |= END_CYL;
                     } else {
                         uptr->UFLAGS &= ~(017 << 9);
                         uptr->UFLAGS |= (sect << 9);
                     }
                     uptr->DATAPTR = 0;
                     CLR_BUF(uptr);
                }
           }
           if (r)
               sim_activate(uptr, 25);
           else {
               uptr->STATUS &= ~(SRC_DONE|END_CYL|BUSY);
               uptr->UFLAGS |= DONE;
               return SCPE_OK;
           }
           break;

    case CL:
    case NO:
           return SCPE_OK;
    case RC:
    case SK:
           if(uptr->UFLAGS & SEEK_STATE) {
               diff = cyl - uptr->CUR_CYL;
               diffs = (diff < 0) ? -1 : 1;
               sim_debug(DEBUG_DETAIL, dptr, "DP Seek %d %d %d %d\n",
                          ctlr, cyl, uptr->CUR_CYL, diff);
               if (diff == 0) {
                   uptr->UFLAGS |= SEEK_DONE;
                   uptr->UFLAGS &= ~SEEK_STATE;
                   uptr->STATUS &= ~(NOT_RDY);
                   if ((df10->status & BUSY) == 0)
                        df10_setirq(df10);
               } else if (diff < 10 && diff > -10) {
                   uptr->CUR_CYL += diffs;
                   if (uptr->CUR_CYL < 0) {
                       uptr->UFLAGS |= SEEK_DONE;
                       uptr->UFLAGS &= ~SEEK_STATE;
                       uptr->STATUS &= ~(NOT_RDY);
                       uptr->CUR_CYL = 0;
                       if ((df10->status & BUSY) == 0)
                           df10_setirq(df10);
                   } else if (uptr->CUR_CYL > dp_drv_tab[dtype].cyl) {
                       uptr->UFLAGS |= SEEK_DONE;
                       uptr->UFLAGS &= ~SEEK_STATE;
                       uptr->STATUS &= ~(NOT_RDY);
                       uptr->CUR_CYL = dp_drv_tab[dtype].cyl;
                       if ((df10->status & BUSY) == 0)
                           df10_setirq(df10);
                   } else
                       sim_activate(uptr, 500);
               } else if (diff > 100 || diff < -100) {
                   uptr->CUR_CYL += diffs * 100;
                   sim_activate(uptr, 4000);
               } else {
                   uptr->CUR_CYL += diffs * 10;
                   sim_activate(uptr, 1000);
               }
           }
    }
    return SCPE_OK;
}


t_stat
dp_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int         i;
    if (uptr == NULL) return SCPE_IERR;
    for (i = 0; dp_drv_tab[i].sect != 0; i++) {
        if (GET_DTYPE(val) == dp_drv_tab[i].devtype) {
            uptr->flags &= ~(UNIT_DTYPE);
            uptr->flags |= val;
            uptr->capac = dp_drv_tab[i].size;
            return SCPE_OK;
        }
    }
    return SCPE_IERR;
}

t_stat
dp_set_hdr(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE *dptr;
    dptr = find_dev_from_unit (uptr);
    if (dptr == NULL)
       return SCPE_IERR;
    dptr->flags &= ~DEV_WHDR;
    dptr->flags |= val;
    return SCPE_OK;
}

t_stat dp_show_hdr (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   DEVICE *dptr;

   if (uptr == NULL)
      return SCPE_IERR;

   dptr = find_dev_from_unit(uptr);
   if (dptr == NULL)
      return SCPE_IERR;
   fprintf (st, "%s", (dptr->flags & DEV_WHDR) ? "HEADERS" : "NOHEADERS");
   return SCPE_OK;
}


t_stat
dp_reset(DEVICE * dptr)
{
    int unit;
    int ctlr;
    UNIT *uptr = dptr->units;
    for(unit = 0; unit < NUM_UNITS_DP; unit++) {
         uptr->UFLAGS  = 0;
         uptr->STATUS  = 0;
         uptr->CUR_CYL = 0;
         uptr++;
    }
    for (ctlr = 0; ctlr < NUM_DEVS_DP; ctlr++) {
        dp_df10[ctlr].status = 0;
        dp_df10[ctlr].devnum = dp_dib[ctlr].dev_num;
        dp_df10[ctlr].nxmerr = 12;
        dp_df10[ctlr].ccw_comp = 5;
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
dp_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    uint32              addr;
    uint32              ptr;
    int                 sect;
    int                 wc;

    addr = (MEMSIZE - 512) & RMASK;
    for (sect = 4; sect <= 7; sect++) {
        (void)disk_read(uptr, &dp_buf[0][0], sect, RP_NUMWD);
        ptr = 0;
        for(wc = RP_NUMWD; wc > 0; wc--)
            M[addr++] = dp_buf[0][ptr++];
    }
    PC = (MEMSIZE - 512) & RMASK;
    return SCPE_OK;
}

/* Device attach */

t_stat dp_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    DEVICE *dptr;
    DIB *dib;
    int ctlr;

    r = disk_attach (uptr, cptr);
    if (r != SCPE_OK || (sim_switches & SIM_SW_REST) != 0)
        return r;
    uptr->capac = dp_drv_tab[GET_DTYPE (uptr->flags)].size;
    dptr = find_dev_from_unit(uptr);
    if (dptr == 0)
        return SCPE_OK;
    dib = (DIB *) dptr->ctxt;
    ctlr = dib->dev_num & 014;
    uptr->CUR_CYL = 0;
    uptr->UFLAGS = (NO << 3) | SEEK_DONE | (ctlr >> 2);
    dp_df10[ctlr].status |= PI_ENABLE;
    set_interrupt(DP_DEVNUM + (ctlr), dp_df10[ctlr].status);
    return SCPE_OK;
}

/* Device detach */

t_stat dp_detach (UNIT *uptr)
{
    if (!(uptr->flags & UNIT_ATT))                          /* attached? */
        return SCPE_OK;
    if (sim_is_active (uptr))                              /* unit active? */
        sim_cancel (uptr);                                  /* cancel operation */
    return disk_detach (uptr);
}

t_stat dp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "RP10 RP01/2/3  Disk Pack Drives (DP)\n\n");
fprintf (st, "The DP controller implements the RP10 disk drives.  RP\n");
fprintf (st, "options include the ability to set units write enabled or write locked, to\n");
fprintf (st, "set the drive type to one of three disk types.\n");
disk_attach_help(st, dptr, uptr, flag, cptr);
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.\n");
fprintf (st, "The RP device supports the BOOT command.\n");
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *dp_description (DEVICE *dptr)
{
return "RP10 disk controller";
}

#endif
