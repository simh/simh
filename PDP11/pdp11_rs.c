/* pdp11_rs.c - RS03/RS04 Massbus disk controller

   Copyright (c) 2013, Robert M Supnik

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   rs           RS03/RS04 fixed head disks

   23-Oct-13    RMS     Revised for new boot setup routine
*/

#if defined (VM_PDP10)
#error "RS03/RS04 not supported on the PDP-10!"

#elif defined (VM_PDP11)
#include "pdp11_defs.h"
#define DEV_RADIX       8

#elif defined (VM_VAX)
#error "RS03/RS04 not supported on the VAX!"
#endif

#include <math.h>

#define RS_NUMDR        8                               /* #drives */
#define RS03_NUMWD      64                              /* words/sector */
#define RS04_NUMWD      128                             /* words/sector */
#define RS_NUMSC        64                              /* sectors/track */
#define RS_NUMTK        64                              /* tracks/disk */
#define RS_MAXFR        (1 << 16)                       /* max transfer */
#define GET_POS(x)      ((int) fmod (sim_gtime() / ((double) (x)), \
                        ((double) (RS03_NUMWD * RS_NUMSC))))
#define RS03_ID         0
#define RS04_ID         2
#define RS03_SIZE       (RS_NUMTK * RS_NUMSC * RS03_NUMWD)
#define RS04_SIZE       (RS_NUMTK * RS_NUMSC * RS04_NUMWD)
#define RS_NUMWD(d)     ((d)? RS04_NUMWD: RS03_NUMWD)
#define RS_SIZE(d)      ((d)? RS04_SIZE: RS03_SIZE)

/* Flags in the unit flags word */

#define UNIT_V_DTYPE    (UNIT_V_UF + 0)                 /* disk type */
#define RS03_DTYPE       (0)
#define RS04_DTYPE	     (1)
#define UNIT_V_AUTO     (UNIT_V_UF + 1)                 /* autosize */
#define UNIT_V_WLK      (UNIT_V_UF + 2)                 /* write lock */
#define UNIT_DTYPE      (1 << UNIT_V_DTYPE)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & 1)

/* RSCS1 - control/status 1 - offset 0 */

#define RS_CS1_OF       0
#define CS1_GO          CSR_GO                          /* go */
#define CS1_V_FNC       1                               /* function pos */
#define CS1_M_FNC       037                             /* function mask */
#define CS1_N_FNC       (CS1_M_FNC + 1)
#define  FNC_NOP        000                             /* no operation */
#define  FNC_DCLR       004                             /* drive clear */
#define  FNC_SEARCH     014                             /* search */
#define  FNC_XFR        020                             /* divide line for xfr */
#define  FNC_WCHK       024                             /* write check */
#define  FNC_WRITE      030                             /* write */
#define  FNC_READ       034                             /* read */
#define CS1_RW          076
#define CS1_DVA         04000                           /* drive avail */
#define GET_FNC(x)      (((x) >> CS1_V_FNC) & CS1_M_FNC)

/* RSDS - drive status - offset 1 */

#define RS_DS_OF        1
#define DS_RDY          0000200                         /* drive ready */
#define DS_DPR          0000400                         /* drive present */
#define DS_LST          0002000                         /* last sector */
#define DS_WLK          0004000                         /* write locked */
#define DS_MOL          0010000                         /* medium online */
#define DS_PIP          0020000                         /* pos in progress */
#define DS_ERR          0040000                         /* error */
#define DS_ATA          0100000                         /* attention active */
#define DS_MBZ          0001177

/* RSER - error status - offset 2 */

#define RS_ER_OF       2
#define ER_ILF         0000001                         /* illegal func */
#define ER_ILR         0000002                         /* illegal register */
#define ER_RMR         0000004                         /* reg mod refused */
#define ER_PAR         0000010                         /* parity err */
#define ER_AOE         0001000                         /* addr ovflo err */
#define ER_IAE         0002000                         /* invalid addr err */
#define ER_WLE         0004000                         /* write lock err NI */
#define ER_DTE         0010000                         /* drive time err NI */
#define ER_OPI         0020000                         /* op incomplete */
#define ER_UNS         0040000                         /* drive unsafe */
#define ER_DCK         0100000                         /* data check NI */
#define ER_MBZ         0000760

/* RSMR - maintenace register - offset 3 */

#define RS_MR_OF        3

/* RSAS - attention summary - offset 4 */

#define RS_AS_OF        4
#define AS_U0           0000001                         /* unit 0 flag */

/* RSDA - sector/track - offset 5
   All 16b are RW, but only <14:12> are tested for "invalid" address */

#define RS_DA_OF        5
#define DA_V_SC         0                               /* sector pos */
#define DA_M_SC         077                             /* sector mask */
#define DA_V_TK         6                               /* track pos */
#define DA_M_TK         077                             /* track mask */
#define GET_SC(x)       (((x) >> DA_V_SC) & DA_M_SC)
#define GET_TK(x)       (((x) >> DA_V_TK) & DA_M_TK)
#define DA_INV          0070000                         /* inv addr */
#define DA_IGN          0100000                         /* ignored */

/* RSDT - drive type - offset 6 */

#define RS_DT_OF        6

/* RSLA - look ahead register - offset 7 */

#define RS_LA_OF        7

/* This controller supports many two disk drive types:

   type         #words/        #sectors/      #tracks/
                 sector         track          drive

   RS03         64              64              64              =256KW
   RS04         128             64              640             =512KW

   In theory, each drive can be a different type. The size field in
   each unit selects the drive capacity for each drive and thus the
   drive type.  
*/

uint16 rscs1[RS_NUMDR] = { 0 };                         /* control/status 1 */
uint16 rsda[RS_NUMDR] = { 0 };                          /* track/sector */
uint16 rsds[RS_NUMDR] = { 0 };                          /* drive status */
uint16 rser[RS_NUMDR] = { 0 };                          /* error status */
uint16 rsmr[RS_NUMDR] = { 0 };                          /* maint register */
uint8 rswlk[RS_NUMDR] = { 0 };                          /* wlk switches */
int32 rs_stopioe = 1;                                   /* stop on error */
int32 rs_wait = 10;                                     /* rotate time */
static const char *rs_fname[CS1_N_FNC] = {
    "NOP", "01", "02", "03", "DCLR", "05", "06", "07",
    "10", "11", "12", "13", "SCH", "15", "16", "17",
    "20", "21", "22", "23", "WRCHK", "25", "26", "27",
    "WRITE", "31", "32", "33", "READ", "35", "36", "37"
    };

t_stat rs_mbrd (int32 *data, int32 ofs, int32 drv);
t_stat rs_mbwr (int32 data, int32 ofs, int32 drv);
t_stat rs_svc (UNIT *uptr);
t_stat rs_reset (DEVICE *dptr);
t_stat rs_attach (UNIT *uptr, char *cptr);
t_stat rs_detach (UNIT *uptr);
t_stat rs_boot (int32 unitno, DEVICE *dptr);
void rs_set_er (uint16 flg, int32 drv);
void rs_clr_as (int32 mask);
void rs_update_ds (uint16 flg, int32 drv);
t_stat rs_go (int32 drv);
t_stat rs_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
int32 rs_abort (void);
t_stat rs_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *rs_description (DEVICE *dptr);

/* RS data structures

   rs_dev       RS device descriptor
   rs_unit      RS unit list
   rs_reg       RS register list
   rs_mod       RS modifier list
*/

DIB rs_dib = { MBA_RS, 0, &rs_mbrd, &rs_mbwr, 0, 0, 0, { &rs_abort } };

UNIT rs_unit[] = {
    { UDATA (&rs_svc, UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|UNIT_AUTO|
             UNIT_BUFABLE|UNIT_MUSTBUF|(RS04_DTYPE << UNIT_V_DTYPE), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|UNIT_AUTO|
             UNIT_BUFABLE|UNIT_MUSTBUF|(RS04_DTYPE << UNIT_V_DTYPE), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|UNIT_AUTO|
             UNIT_BUFABLE|UNIT_MUSTBUF|(RS04_DTYPE << UNIT_V_DTYPE), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|UNIT_AUTO|
             UNIT_BUFABLE|UNIT_MUSTBUF|(RS04_DTYPE << UNIT_V_DTYPE), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|UNIT_AUTO|
             UNIT_BUFABLE|UNIT_MUSTBUF|(RS04_DTYPE << UNIT_V_DTYPE), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|UNIT_AUTO|
             UNIT_BUFABLE|UNIT_MUSTBUF|(RS04_DTYPE << UNIT_V_DTYPE), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|UNIT_AUTO|
             UNIT_BUFABLE|UNIT_MUSTBUF|(RS04_DTYPE << UNIT_V_DTYPE), RS04_SIZE) },
    { UDATA (&rs_svc, UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|UNIT_AUTO|
             UNIT_BUFABLE|UNIT_MUSTBUF|(RS04_DTYPE << UNIT_V_DTYPE), RS04_SIZE) }
    };

REG rs_reg[] = {
    { BRDATAD (CS1, rscs1, DEV_RDX, 16, RS_NUMDR, "control/status 1") },
    { BRDATAD (DA, rsda, DEV_RDX, 16, RS_NUMDR, "track/sector") },
    { BRDATAD (DS, rsds, DEV_RDX, 16, RS_NUMDR, "drive status") },
    { BRDATAD (ER, rser, DEV_RDX, 16, RS_NUMDR, "error status") },
    { BRDATAD (MR, rsmr, DEV_RDX, 16, RS_NUMDR, "maint register") },
    { BRDATAD (WLKS, rswlk, DEV_RDX, 6, RS_NUMDR, "write lock switches") },
    { DRDATAD (TIME, rs_wait, 24, "rotate time"), REG_NZ + PV_LEFT },
    { URDATAD (CAPAC, rs_unit[0].capac, 10, T_ADDR_W, 0,
              RS_NUMDR, PV_LEFT | REG_HRO, "Capacity") },
    { FLDATAD (STOP_IOE, rs_stopioe, 0, "stop on I/O error") },
    { NULL }
    };

MTAB rs_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "MASSBUS", NULL, NULL, &mba_show_num, NULL, "Display Massbus Address" },
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL, NULL, NULL, "Write enable disk drive"  },
    { UNIT_WLK, UNIT_WLK, "write lockable", "LOCKED", NULL, NULL, NULL, "Write lock disk drive" },
    { (UNIT_DTYPE|UNIT_ATT), (RS03_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
      "RS03", NULL, NULL },
    { (UNIT_DTYPE|UNIT_ATT), (RS04_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
      "RS04", NULL, NULL },
    { (UNIT_AUTO|UNIT_DTYPE|UNIT_ATT), (RS03_DTYPE << UNIT_V_DTYPE),
      "RS03", NULL, NULL },
    { (UNIT_AUTO|UNIT_DTYPE|UNIT_ATT), (RS04_DTYPE << UNIT_V_DTYPE),
      "RS04", NULL, NULL },
    { (UNIT_AUTO+UNIT_ATT), UNIT_AUTO, "autosize", NULL, NULL },
    { UNIT_AUTO, UNIT_AUTO, NULL, "AUTOSIZE", NULL, NULL, NULL, "set type based on file size at ATTACH" },
    { (UNIT_AUTO|UNIT_DTYPE), (RS03_DTYPE << UNIT_V_DTYPE),
      NULL, "RS03", &rs_set_size, NULL, NULL, "Set drive type RS03" },
    { (UNIT_AUTO|UNIT_DTYPE), (RS04_DTYPE << UNIT_V_DTYPE),
      NULL, "RS04", &rs_set_size, NULL, NULL, "Set drive type RS04" }, 
    { 0 }
    };

DEVICE rs_dev = {
    "RS", rs_unit, rs_reg, rs_mod,
    RS_NUMDR, DEV_RADIX, 19, 1, DEV_RADIX, 16,
    NULL, NULL, &rs_reset,
    &rs_boot, &rs_attach, &rs_detach,
    &rs_dib, DEV_DISABLE|DEV_DIS|DEV_UBUS|DEV_QBUS|DEV_MBUS|DEV_DEBUG, 0,
    NULL, NULL, NULL, &rs_help, NULL, NULL,
    &rs_description
    };

/* Massbus register read */

t_stat rs_mbrd (int32 *data, int32 ofs, int32 drv)
{
uint32 val, dtype, i;
UNIT *uptr;

rs_update_ds (0, drv);                                  /* update ds */
uptr = rs_dev.units + drv;                              /* get unit */
if (uptr->flags & UNIT_DIS) {                           /* nx disk */
    *data = 0;
    return MBE_NXD;
    }
dtype = GET_DTYPE (uptr->flags);                        /* get drive type */
ofs = ofs & MBA_RMASK;                                  /* mask offset */

switch (ofs) {                                          /* decode offset */

    case RS_CS1_OF:                                     /* RSCS1 */
        val = (rscs1[drv] & CS1_RW) | CS1_DVA;          /* DVA always set */
        break;

    case RS_DA_OF:                                      /* RSDA */
        val = rsda[drv];
        break;

    case RS_DS_OF:                                      /* RSDS */
        val = rsds[drv] & ~DS_MBZ;
        break;

    case RS_ER_OF:                                      /* RSER */
        val = rser[drv] & ~ER_MBZ;
        break;

    case RS_AS_OF:                                      /* RSAS */
        val = 0;
        for (i = 0; i < RS_NUMDR; i++) {
            if (rsds[i] & DS_ATA)
                val |= (AS_U0 << i);
            }
        break;

    case RS_LA_OF:                                      /* RSLA */
        val = GET_POS (rs_wait);
        break;

    case RS_MR_OF:                                      /* RSMR */
        val = rsmr[drv];
        break;

    case RS_DT_OF:                                      /* RSDT */
        val = dtype? RS04_ID: RS03_ID;
        break;

   default:                                             /* all others */
        *data = 0;
        return MBE_NXR;
        }

*data = val;
return SCPE_OK;
}

/* Massbus register write */

t_stat rs_mbwr (int32 data, int32 ofs, int32 drv)
{
UNIT *uptr;

uptr = rs_dev.units + drv;                              /* get unit */
if (uptr->flags & UNIT_DIS)                             /* nx disk */
    return MBE_NXD;
if ((ofs != RS_AS_OF) && sim_is_active (uptr)) {        /* unit busy? */
    rs_set_er (ER_RMR, drv);                            /* won't write */
    rs_update_ds (0, drv);
    return SCPE_OK;
    }
ofs = ofs & MBA_RMASK;                                  /* mask offset */

switch (ofs) {                                          /* decode PA<5:1> */

    case RS_CS1_OF:                                     /* RSCS1 */
        rscs1[drv] = data & CS1_RW;
        if (data & CS1_GO)                              /* start op */
            return rs_go (drv);
        break;  

    case RS_DA_OF:                                      /* RSDA */
        rsda[drv] = (uint16)data;
        break;

    case RS_AS_OF:                                      /* RSAS */
        rs_clr_as (data);
        break;

    case RS_MR_OF:                                      /* RSMR */
        rsmr[drv] = (uint16)data;
        break;

    case RS_ER_OF:                                      /* RSER */
    case RS_DS_OF:                                      /* RSDS */
    case RS_LA_OF:                                      /* RSLA */
    case RS_DT_OF:                                      /* RSDT */
        break;                                          /* read only */

    default:                                            /* all others */
        return MBE_NXR;
        }                                               /* end switch */

rs_update_ds (0, drv);                                  /* update status */
return SCPE_OK;
}

/* Initiate operation - unit not busy, function set */

t_stat rs_go (int32 drv)
{
int32 fnc, t;
UNIT *uptr;

fnc = GET_FNC (rscs1[drv]);                             /* get function */
if (DEBUG_PRS (rs_dev))
    fprintf (sim_deb, ">>RS%d STRT: fnc=%s, ds=%o, da=%o, er=%o\n",
             drv, rs_fname[fnc], rsds[drv], rsda[drv], rser[drv]);
uptr = rs_dev.units + drv;                              /* get unit */
rs_clr_as (AS_U0 << drv);                               /* clear attention */
if ((fnc != FNC_DCLR) && (rsds[drv] & DS_ERR)) {        /* err & ~clear? */
    rs_set_er (ER_ILF, drv);                            /* not allowed */
    rs_update_ds (DS_ATA, drv);                         /* set attention */
    return MBE_GOE;
    }

switch (fnc) {                                          /* case on function */

    case FNC_DCLR:                                      /* drive clear */
        rser[drv] = 0;                                  /* clear errors */
    case FNC_NOP:                                       /* no operation */
        return SCPE_OK;

    case FNC_SEARCH:                                    /* search */
    case FNC_WRITE:                                     /* write */
    case FNC_WCHK:                                      /* write check */
    case FNC_READ:                                      /* read */
        if ((uptr->flags & UNIT_ATT) == 0) {            /* not attached? */
            rs_set_er (ER_UNS, drv);                    /* unsafe */
            break;
            }
        if (rsda[drv] & DA_INV) {                       /* bad address? */
            rs_set_er (ER_IAE, drv);
            break;
            }
        rsds[drv] = rsds[drv] & ~DS_RDY;                /* clr drive rdy */
        if (fnc == FNC_SEARCH)                          /* search? */
            rsds[drv] = rsds[drv] | DS_PIP;             /* set PIP */
        t = abs (rsda[drv] - GET_POS (rs_wait));        /* pos diff */
        if (t < 1)                                      /* min time */
            t = 1;
        sim_activate (uptr, rs_wait * t);               /* schedule */
        return SCPE_OK;

    default:                                            /* all others */
        rs_set_er (ER_ILF, drv);                        /* not supported */
        break;
        }

rs_update_ds (DS_ATA, drv);                             /* set attn, req int */
return MBE_GOE;
}

/* Abort opertion - there is a data transfer in progress */

int32 rs_abort (void)
{
return rs_reset (&rs_dev);
}

/* Service unit timeout

   Complete search or data transfer command
   Unit must exist - can't remove an active unit
   Drives are buffered in memory - no IO errors
*/

t_stat rs_svc (UNIT *uptr)
{
int32 i, fnc, dtype, drv;
int32 wc, abc, awc, mbc, da;
uint16 *fbuf = (uint16 *)uptr->filebuf;

dtype = GET_DTYPE (uptr->flags);                        /* get drive type */
drv = (int32) (uptr - rs_dev.units);                    /* get drv number */
da = rsda[drv] * RS_NUMWD (dtype);                      /* get disk addr */
fnc = GET_FNC (rscs1[drv]);                             /* get function */

if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
    rs_set_er (ER_UNS, drv);                            /* set drive error */
    if (fnc >= FNC_XFR)                                 /* xfr? set done */
        mba_set_don (rs_dib.ba);
    rs_update_ds (DS_ATA, drv);                         /* set attn */
    return (rs_stopioe? SCPE_UNATT: SCPE_OK);
    }
rsds[drv] = (rsds[drv] & ~DS_PIP) | DS_RDY;             /* change drive status */

switch (fnc) {                                          /* case on function */
 
    case FNC_SEARCH:                                    /* search */
        rs_update_ds (DS_ATA, drv);
        break;

    case FNC_WRITE:                                     /* write */
        if ((uptr->flags & UNIT_WLK) &&                 /* write locked? */
            (GET_TK (rsda[drv]) <= (int32) rswlk[drv])) {
            rs_set_er (ER_WLE, drv);                    /* set drive error */
            mba_set_exc (rs_dib.ba);                    /* set exception */
            rs_update_ds (DS_ATA, drv);                 /* set attn */
            return SCPE_OK;
            }
    case FNC_WCHK:                                      /* write check */
    case FNC_READ:                                      /* read */
        if (rsda[drv] & DA_INV) {                       /* bad addr? */
            rs_set_er (ER_IAE, drv);                   /* set error */
            mba_set_exc (rs_dib.ba);                    /* set exception */
            rs_update_ds (DS_ATA, drv);                 /* set attn */
            break;
            }
        fbuf = fbuf + da;                               /* ptr into buffer */
        mbc = mba_get_bc (rs_dib.ba);                   /* get byte count */
        wc = (mbc + 1) >> 1;                            /* convert to words */
        if ((da + wc) > RS_SIZE (dtype)) {              /* disk overrun? */
            rs_set_er (ER_AOE, drv);                    /* set err */
            wc = RS_SIZE (dtype) - da;                  /* trim xfer */
            mbc = wc << 1;                              /* trim mb count */
            }
        if (fnc == FNC_WRITE) {                         /* write? */
            abc = mba_rdbufW (rs_dib.ba, mbc, fbuf);    /* rd mem to buf */
            wc = (abc + 1) >> 1;                        /* actual # wds */
            awc = (wc + (RS_NUMWD (dtype) - 1)) & ~(RS_NUMWD (dtype) - 1);
            for (i = wc; i < awc; i++)                  /* fill buf */
                fbuf[i] = 0;
            if ((da + awc) > (int32) uptr->hwmark)      /* update hw mark*/
                uptr->hwmark = da + awc;
            }                                           /* end if wr */
        else if (fnc == FNC_READ)                       /* read  */
            mba_wrbufW (rs_dib.ba, mbc, fbuf);          /* wri buf to mem */
        else mba_chbufW (rs_dib.ba, mbc, fbuf);         /* check vs mem */

        da = da + wc + (RS_NUMWD (dtype) - 1);
        if (da >= RS_SIZE (dtype))
            rsds[drv] = rsds[drv] | DS_LST;
        rsda[drv] = (uint16)(da / RS_NUMWD (dtype));
        mba_set_don (rs_dib.ba);                        /* set done */
        rs_update_ds (0, drv);                          /* update ds */
        break;
        }                                               /* end case func */

if (DEBUG_PRS (rs_dev))
    fprintf (sim_deb, ">>RS%d DONE: fnc=%s, ds=%o, da=%o, er=%d\n",
             drv, rs_fname[fnc], rsds[drv], rsda[drv], rser[drv]);
return SCPE_OK;
}

/* Set drive error */

void rs_set_er (uint16 flag, int32 drv)
{
rser[drv] = rser[drv] | flag;
rsds[drv] = rsds[drv] | DS_ATA;
mba_upd_ata (rs_dib.ba, 1);
return;
}

/* Clear attention flags */

void rs_clr_as (int32 mask)
{
uint32 i, as;

for (i = as = 0; i < RS_NUMDR; i++) {
    if (mask & (AS_U0 << i))
        rsds[i] &= ~DS_ATA;
    if (rsds[i] & DS_ATA)
        as = 1;
    }
mba_upd_ata (rs_dib.ba, as);
return;
}

/* Drive status update */

void rs_update_ds (uint16 flag, int32 drv)
{
if (flag & DS_ATA)
    mba_upd_ata (rs_dib.ba, 1);
if (rs_unit[drv].flags & UNIT_DIS) {
    rsds[drv] = rser[drv] = 0;
    return;
    }
else rsds[drv] = (rsds[drv] | DS_DPR) & ~(DS_ERR | DS_WLK);
if (rs_unit[drv].flags & UNIT_ATT) {
    rsds[drv] = rsds[drv] | DS_MOL;
    if ((rs_unit[drv].flags & UNIT_WLK) &&
        (GET_TK (rsda[drv]) <= (int32) rswlk[drv]))
        rsds[drv] = rsds[drv] | DS_WLK;
    }
if (rser[drv])
    rsds[drv] = rsds[drv] | DS_ERR;
rsds[drv] = rsds[drv] | flag;
return;
}

/* Device reset */

t_stat rs_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

mba_set_enbdis (MBA_RS, rs_dev.flags & DEV_DIS);
for (i = 0; i < RS_NUMDR; i++) {
    uptr = rs_dev.units + i;
    sim_cancel (uptr);
    rscs1[i] = 0;
    rser[i] = 0;
    rsda[i] = 0;
    rsmr[i] = 0;
    rsds[i] = DS_RDY;
    rs_update_ds (0, i);                                /* upd drive status */
    }
return SCPE_OK;
}

/* Device attach */

t_stat rs_attach (UNIT *uptr, char *cptr)
{
int32 drv, p;
t_stat r;

uptr->capac = RS_SIZE (GET_DTYPE (uptr->flags));
r = attach_unit (uptr, cptr);                           /* attach unit */
if (r != SCPE_OK)                                       /* error? */
    return r;
drv = (int32) (uptr - rs_dev.units);                    /* get drv number */
rsds[drv] = DS_MOL | DS_RDY | DS_DPR;                   /* upd drv status */
rser[drv] = 0;
rs_update_ds (DS_ATA, drv);                             /* upd drive status */

if ((uptr->flags & UNIT_AUTO) == 0)                     /* autosize? */
    return SCPE_OK;
p = sim_fsize (uptr->fileref);                          /* get file size */
if (((p + 1) >> 1) <= RS03_SIZE) {
    uptr->flags &= ~UNIT_DTYPE;
    uptr->capac = RS03_SIZE;
    }
else {
    uptr->flags |= UNIT_DTYPE;
    uptr->capac = RS04_SIZE;
    }
return SCPE_OK;
}

/* Device detach */

t_stat rs_detach (UNIT *uptr)
{
int32 drv;

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
drv = (int32) (uptr - rs_dev.units);                    /* get drv number */
rsds[drv] = 0;
if (!sim_is_running)                                    /* from console? */
    rs_update_ds (DS_ATA, drv);                         /* request intr */
return detach_unit (uptr);
}

/* Set size command validation routine */

t_stat rs_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 dtype = GET_DTYPE (val);

if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = RS_SIZE (dtype);
return SCPE_OK;
}

/* Set bad block routine */

/* Boot routine */

#define BOOT_START      02000                           /* start */
#define BOOT_ENTRY      (BOOT_START + 002)              /* entry */
#define BOOT_UNIT       (BOOT_START + 010)              /* unit number */
#define BOOT_CSR        (BOOT_START + 014)              /* CSR */
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (uint16))

static const uint16 boot_rom[] = {
    0042123,                        /* "SD" */
    0012706, BOOT_START,            /* mov #boot_start, sp */
    0012700, 0000000,               /* mov #unit, r0 */
    0012701, 0172040,               /* mov #RSCS1, r1 */
    0012761, 0000040, 0000010,      /* mov #CS2_CLR, 10(r1) ; reset */
    0010061, 0000010,               /* mov r0, 10(r1)       ; set unit */
    0012761, 0177000, 0000002,      /* mov #-512., 2(r1)    ; set wc */
    0005061, 0000004,               /* clr 4(r1)            ; clr ba */
    0005061, 0000006,               /* clr 6(r1)            ; clr da */
    0012711, 0000071,               /* mov #READ+GO, (r1)   ; read  */
    0105711,                        /* tstb (r1)            ; wait */
    0100376,                        /* bpl .-2 */
    0005002,                        /* clr R2 */
    0005003,                        /* clr R3 */
    0012704, BOOT_START+020,        /* mov #start+020, r4 */
    0005005,                        /* clr R5 */
    0105011,                        /* clrb (r1) */
    0005007                         /* clr PC */
    };

t_stat rs_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
extern uint16 *M;

for (i = 0; i < BOOT_LEN; i++)
    M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & (RS_NUMDR - 1);
M[BOOT_CSR >> 1] = mba_get_csr (rs_dib.ba) & DMASK;
cpu_set_boot (BOOT_ENTRY);
return SCPE_OK;
}

t_stat rs_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprintf (st, "RS03/RS04 Massbus disk controller (RS)\n\n");
fprintf (st, "The RS controller implements the Massbus family fixed head disks.  RS\n");
fprintf (st, "options include the ability to set units write enabled or write locked,\n");
fprintf (st, "to set the drive type to RS03, RS04, or autosize:\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.\n");
fprintf (st, "The RS device supports the BOOT command.\n");
fprint_reg_help (st, dptr);
fprintf (st, "\nError handling is as follows:\n\n");
fprintf (st, "    error         STOP_IOE   processed as\n");
fprintf (st, "    not attached  1          report error and stop\n");
fprintf (st, "                  0          disk not ready\n\n");
fprintf (st, "RS data files are buffered in memory; therefore, end of file and OS I/O\n");
fprintf (st, "errors cannot occur.\n");
return SCPE_OK;
}

char *rs_description (DEVICE *dptr)
{
return "RS03/RS04 Massbus disk controller";
}
