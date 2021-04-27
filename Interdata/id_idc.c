/* id_idc.c: Interdata MSM/IDC disk controller simulator

   Copyright (c) 2001-2008, Robert M. Supnik

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

   idc          MSM/IDC disk controller

   03-Apr-06    RMS     Fixed WD/WH handling (Davis Johnson)
   30-Mar-06    RMS     Fixed bug, nop command should be ignored (Davis Johnson)
   25-Apr-03    RMS     Revised for extended file support
   16-Feb-03    RMS     Fixed read to test transfer ok before selch operation

   Note: define flag ID_IDC to enable the extra functions of the intelligent
   disk controller
*/

#include "id_defs.h"

#define IDC_NUMBY       256                             /* bytes/sector */
#define IDC_NUMSC       64                              /* sectors/track */

#define UNIT_V_DTYPE    (UNIT_V_UF + 0)                 /* disk type */
#define UNIT_M_DTYPE    0x7
#define UNIT_V_AUTO     (UNIT_V_UF + 4)                 /* autosize */
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)

#define CYL             u3                              /* current cylinder */
#define HD              u4                              /* current head */
#define STD             buf                             /* drive status */
#define FNC             wait                            /* function */

#define IDC_DRVMASK     ((1 << ID_NUMDR) - 1)           /* drive bit mask */
#define IDC_DIRMASK     (IDC_DRVMASK << (i_IDC + 1))    /* drive irq mask */

/* Controller status */

#define STC_WRP         0x80                            /* write protected */
#define STC_ACF         0x40                            /* addr cmp fail */
#define STC_DEF         0x20                            /* def track NI */
#define STC_CYO         0x10                            /* cylinder ovflo */
#define STC_IDL         0x02                            /* ctrl idle */
#define STC_DTE         0x01                            /* xfer error */
#define SETC_EX         (STC_WRP|STC_ACF|STC_DEF|STC_CYO)
#define STC_MASK        (STC_WRP|STC_ACF|STC_DEF|STC_CYO|STA_BSY|STC_IDL|STC_DTE)

/* Controller command */

#define CMC_MASK        0x3F                            
#define CMC_CLR         0x08                            /* reset */
#define CMC_RD          0x01                            /* read */
#define CMC_WR          0x02                            /* write */
#define CMC_RCHK        0x03                            /* read check */
#define CMC_FCHK        0x04                            /* format check NI */
#define CMC_RFMT        0x05                            /* read fmt NI */
#define CMC_WFMT        0x06                            /* write fmt NI */
#define CMC_WFTK        0x07                            /* write fmt track NI */

/* IDC only functions */

#define CMC_RRAM        0x10                            /* read RAM */
#define CMC_WRAM        0x11                            /* write RAM */
#define CMC_EXP0        0x12                            /* read page 0 NI */
#define CMC_RUNC        0x21                            /* read uncorr */
#define CMC_STST        0x30                            /* self test */
#define CMC_WLNG        0x32                            /* write long NI */
#define CMC_LAMP        0x37                            /* lamp test */

#define CMC_DRV         0x100                           /* drive func */
#define CMC_DRV1        0x200                           /* drive func, part 2 */

/* Drive status, ^ = dynamic, * = in unit status */

#define STD_WRP         0x80                            /* ^write prot */
/*                      0x40                          *//* unused */
#define STD_ACH         0x20                            /* alt chan busy NI */
#define STD_UNS         0x10                            /* *unsafe */
#define STD_NRDY        0x08                            /* ^not ready */
#define STD_SKI         0x02                            /* *seek incomplete */
#define STD_OFFL        0x01                            /* ^offline */
#define STD_UST         (STD_UNS | STD_SKI)             /* set from unit */
#define SETD_EX         (STD_WRP | STD_UNS)             /* set examine */

/* Drive command */

#define CMDF_SHD        0x20                            /* set head */
#define CMDF_SCY        0x10                            /* set cylinder */
#define CMD_SK          0x02                            /* seek */
#define CMD_RST         0x01                            /* restore */

#define CMDX_MASK       0x30                            /* ext cmd bits */
#define CMDX_RLS        0x80                            /* release */
#define CMDX_CLF        0x40                            /* clear fault */
#define CMDX_SVP        0x08                            /* servo + */
#define CMDX_SVM        0x04                            /* servo - */
#define CMDX_DSP        0x02                            /* strobe + */
#define CMDX_DSM        0x01                            /* strobe - */

/* Geometry masks */

#define CY_MASK         0xFFF                           /* cylinder */
#define HD_MASK         0x1F                            /* head mask */
#define SC_MASK         0x3F                            /* sector mask */
#define HCYL_V_HD       10                              /* head/cyl word */
#define HCYL_V_CYL      0

#define GET_SA(cy,sf,sc,t) \
                        (((((cy)*drv_tab[t].surf)+(sf))*IDC_NUMSC)+(sc))

/* The MSM (IDC) controller supports (two) six different disk drive types:

   type         #sectors/       #surfaces/      #cylinders/
                 surface         cylinder        drive

   MCCDD16      64              1               823     IDC
   MCCDD48      64              3               823     IDC
   MCCDD80      64              5               823     IDC
   MSM80        64              5               823     MSM
   MSM300       64              19              823     MSM
   MSM330F      64              16              1024    IDC

   In theory, each drive can be a different type.  The size field in
   each unit selects the drive capacity for each drive and thus the
   drive type.  DISKS MUST BE DECLARED IN ASCENDING SIZE AND MUST HAVE
   THE SAME SECTORS/TRACK.
*/

#define TYPE_MCCDD16    0
#define SURF_MCCDD16    1
#define CYL_MCCDD16     823
#define SIZE_MCCDD16    (IDC_NUMSC * SURF_MCCDD16 * CYL_MCCDD16 * IDC_NUMBY)

#define TYPE_MCCDD48    1
#define SURF_MCCDD48    3
#define CYL_MCCDD48     823
#define SIZE_MCCDD48    (IDC_NUMSC * SURF_MCCDD48 * CYL_MCCDD48 * IDC_NUMBY)

#define TYPE_MCCDD80    2
#define SURF_MCCDD80    5
#define CYL_MCCDD80     823
#define SIZE_MCCDD80    (IDC_NUMSC * SURF_MCCDD80 * CYL_MCCDD80 * IDC_NUMBY)

#define TYPE_MSM80      3
#define SURF_MSM80      5
#define CYL_MSM80       823
#define SIZE_MSM80      (IDC_NUMSC * SURF_MSM80 * CYL_MSM80 * IDC_NUMBY)

#define TYPE_MSM300     4
#define SURF_MSM300     19
#define CYL_MSM300      823
#define SIZE_MSM300     (IDC_NUMSC * SURF_MSM300 * CYL_MSM300 * IDC_NUMBY)

#define TYPE_MSM330F    5
#define SURF_MSM330F    16
#define CYL_MSM330F     1024
#define SIZE_MSM330F    (IDC_NUMSC * SURF_MSM330F * CYL_MSM330F * IDC_NUMBY)


struct drvtyp {
    uint32      surf;                                   /* surfaces */
    uint32      cyl;                                    /* cylinders */
    uint32      size;                                   /* #blocks */
    uint32      msmf;                                   /* MSM drive */
    };

static struct drvtyp drv_tab[] = {
    { SURF_MCCDD16, CYL_MCCDD16, SIZE_MCCDD16, 0 },
    { SURF_MCCDD48, CYL_MCCDD48, SIZE_MCCDD48, 0 },
    { SURF_MCCDD80, CYL_MCCDD80, SIZE_MCCDD80, 0 },
    { SURF_MSM80, CYL_MSM80, SIZE_MSM80, 1 },
    { SURF_MSM300, CYL_MSM300, SIZE_MSM300, 1 },
    { SURF_MSM330F, CYL_MSM330F, SIZE_MSM330F, 0 },
    { 0 }
    };

extern uint32 int_req[INTSZ], int_enb[INTSZ];

uint8 idcxb[IDC_NUMBY * 3];                             /* xfer buffer */
uint32 idc_bptr = 0;                                    /* buffer ptr */
uint32 idc_wdptr = 0;                                   /* ctrl write data ptr */
uint32 idc_db = 0;                                      /* ctrl buffer */
uint32 idc_sta = 0;                                     /* ctrl status */
uint32 idc_sec = 0;                                     /* sector */
uint32 idc_hcyl = 0;                                    /* head/cyl */
uint32 idc_svun = 0;                                    /* most recent unit */
uint32 idc_1st = 0;                                     /* first byte */
uint32 idc_arm = 0;                                     /* ctrl armed */
uint32 idd_db = 0;                                      /* drive buffer */
uint32 idd_wdptr = 0;                                   /* drive write data ptr */
uint32 idd_arm[ID_NUMDR] = { 0 };                       /* drives armed */
uint16 idd_dcy[ID_NUMDR] = { 0 };                       /* desired cyl */
uint32 idd_sirq = 0;                                    /* drive saved irq */
int32 idc_stime = 100;                                  /* seek latency */
int32 idc_rtime = 100;                                  /* rotate latency */
int32 idc_ctime = 5;                                    /* command latency */
uint8 idc_tplte[] = { 0, 1, 2, 3, 4, TPL_END };         /* ctrl + drive */

uint32 id (uint32 dev, uint32 op, uint32 dat);
t_stat idc_svc (UNIT *uptr);
t_stat idc_reset (DEVICE *dptr);
t_stat idc_attach (UNIT *uptr, CONST char *cptr);
t_stat idc_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
void idc_wd_byte (uint32 dat);
t_stat idc_rds (UNIT *uptr);
t_stat idc_wds (UNIT *uptr);
t_bool idc_dter (UNIT *uptr, uint32 first);
void idc_done (uint32 flg);

extern t_stat id_dboot (int32 u, DEVICE *dptr);

/* DP data structures

   idc_dev      DP device descriptor
   idc_unit     DP unit list
   idc_reg      DP register list
   idc_mod      DP modifier list
*/

DIB idc_dib = { d_IDC, 0, v_IDC, idc_tplte, &id, NULL };

UNIT idc_unit[] = {
    { UDATA (&idc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+(TYPE_MSM80 << UNIT_V_DTYPE), SIZE_MSM80) },
    { UDATA (&idc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+(TYPE_MSM80 << UNIT_V_DTYPE), SIZE_MSM80) },
    { UDATA (&idc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+(TYPE_MSM80 << UNIT_V_DTYPE), SIZE_MSM80) },
    { UDATA (&idc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+(TYPE_MSM80 << UNIT_V_DTYPE), SIZE_MSM80) }
    };

REG idc_reg[] = {
    { HRDATA (STA, idc_sta, 8) },
    { HRDATA (BUF, idc_db, 8) },
    { HRDATA (SEC, idc_sec, 8) },
    { HRDATA (HCYL, idc_hcyl, 16) },
    { HRDATA (BUF, idd_db, 8) },
    { HRDATA (SVUN, idc_svun, 2), REG_HIDDEN },
    { BRDATA (DBUF, idcxb, 16, 8, IDC_NUMBY * 3) },
    { HRDATA (DBPTR, idc_bptr, 10), REG_RO },
    { FLDATA (FIRST, idc_1st, 0) },
    { HRDATA (CWDPTR, idc_wdptr, 2) },
    { HRDATA (DWDPTR, idc_wdptr, 1) },
    { GRDATA (IREQ, int_req[l_IDC], 16, ID_NUMDR + 1, i_IDC) },
    { GRDATA (IENB, int_enb[l_IDC], 16, ID_NUMDR + 1, i_IDC) },
    { GRDATA (SIREQ, idd_sirq, 16, ID_NUMDR, i_IDC + 1), REG_RO },
    { FLDATA (ICARM, idc_arm, 0) },
    { BRDATA (IDARM, idd_arm, 16, 1, ID_NUMDR) },
    { DRDATA (RTIME, idc_rtime, 24), PV_LEFT | REG_NZ },
    { DRDATA (STIME, idc_stime, 24), PV_LEFT | REG_NZ },
    { DRDATA (CTIME, idc_ctime, 24), PV_LEFT | REG_NZ },
    { BRDATA (CYL, idd_dcy, 16, 16, ID_NUMDR) },
    { URDATA (UCYL, idc_unit[0].CYL, 16, 12, 0,
              ID_NUMDR, REG_RO) },
    { URDATA (UHD, idc_unit[0].HD, 16, 5, 0,
              ID_NUMDR, REG_RO) },
    { URDATA (UFNC, idc_unit[0].FNC, 16, 10, 0,
              ID_NUMDR, REG_HRO) },
    { URDATA (UST, idc_unit[0].STD, 16, 8, 0,
              ID_NUMDR, REG_RO) },
    { URDATA (CAPAC, idc_unit[0].capac, 10, T_ADDR_W, 0,
              ID_NUMDR, PV_LEFT | REG_HRO) },
    { HRDATA (DEVNO, idc_dib.dno, 8), REG_HRO },
    { HRDATA (SELCH, idc_dib.sch, 2), REG_HRO },
    { BRDATA (TPLTE, idc_tplte, 16, 8, ID_NUMDR + 1), REG_HRO },
    { NULL }
    };

MTAB idc_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write enable drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "Write lock drive" },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_MCCDD16 << UNIT_V_DTYPE) + UNIT_ATT,
      "MCCDD16", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_MCCDD48 << UNIT_V_DTYPE) + UNIT_ATT,
      "MCCDD48", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_MCCDD80 << UNIT_V_DTYPE) + UNIT_ATT,
      "MCCDD80", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_MSM330F << UNIT_V_DTYPE) + UNIT_ATT,
      "MSM330F", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_MCCDD16 << UNIT_V_DTYPE),
      "MCCDD16", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_MCCDD48 << UNIT_V_DTYPE),
      "MCCDD48", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_MCCDD80 << UNIT_V_DTYPE),
      "MCCDD80", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_MSM330F << UNIT_V_DTYPE),
      "MSM330F", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_MCCDD16 << UNIT_V_DTYPE),
      NULL, "MCCDD16", &idc_set_size }, 
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_MCCDD48 << UNIT_V_DTYPE),
      NULL, "MCCDD48", &idc_set_size }, 
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_MCCDD80 << UNIT_V_DTYPE),
      NULL, "MCCDD80", &idc_set_size }, 
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_MSM330F << UNIT_V_DTYPE),
      NULL, "MSM330F", &idc_set_size },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_MSM80 << UNIT_V_DTYPE) + UNIT_ATT,
      "MSM80", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_MSM300 << UNIT_V_DTYPE) + UNIT_ATT,
      "MSM300", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_MSM80 << UNIT_V_DTYPE),
      "MSM80", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_MSM300 << UNIT_V_DTYPE),
      "MSM300", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_MSM80 << UNIT_V_DTYPE),
      NULL, "MSM80", &idc_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_MSM300 << UNIT_V_DTYPE),
      NULL, "MSM300", &idc_set_size },
    { (UNIT_AUTO+UNIT_ATT), UNIT_AUTO, "autosize", NULL, NULL },
    { UNIT_AUTO, UNIT_AUTO, NULL, "AUTOSIZE", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "SELCH", "SELCH",
      &set_sch, &show_sch, NULL },
    { 0 }
    };

DEVICE idc_dev = {
    "DM", idc_unit, idc_reg, idc_mod,
    ID_NUMDR, 16, 29, 1, 16, 8,
    NULL, NULL, &idc_reset,
    &id_dboot, &idc_attach, NULL,
    &idc_dib, DEV_DISABLE
    };

/* Controller: IO routine */

uint32 idc (uint32 dev, uint32 op, uint32 dat)
{
uint32 f, t;
UNIT *uptr;

switch (op) {                                           /* case IO op */

    case IO_ADR:                                        /* select */
        sch_adr (idc_dib.sch, dev);                     /* inform sel ch */
        return HW;                                      /* halfwords */

    case IO_RD:                                         /* read data */
    case IO_RH:                                         /* read halfword */
        return 0;                                       /* return data */

    case IO_WD:                                         /* write data */
        idc_wd_byte (dat);                              /* one byte only */
        break;

    case IO_WH:                                         /* write halfword */
        idc_wd_byte (dat >> 8);                         /* high byte */
        idc_wd_byte (dat);                              /* low byte */
        break;

    case IO_SS:                                         /* status */
        t = idc_sta & STC_MASK;                         /* get status */
        if (t & SETC_EX)                                /* test for EX */
            t = t | STA_EX;
        return t;

    case IO_OC:                                         /* command */
        idc_arm = int_chg (v_IDC, dat, idc_arm);        /* upd int ctrl */
        idc_wdptr = 0;                                  /* init ptr */
        f = dat & CMC_MASK;                             /* get cmd */
        uptr = idc_dev.units + idc_svun;                /* get unit */
        if (f & CMC_CLR) {                              /* clear? */
            idc_reset (&idc_dev);                       /* reset world */
            break;
            }
        if ((f == 0) ||                                 /* if nop, */
            (f == CMC_EXP0) ||                          /* expg, */
            !(idc_sta & STC_IDL) ||                     /* !idle, */
            sim_is_active (uptr)) break;                /* unit busy, ignore */
        idc_sta = STA_BSY;                              /* bsy=1,idl,err=0 */
        idc_1st = 1;                                    /* xfr not started */
        idc_bptr = 0;                                   /* buffer empty */
        uptr->FNC = f;                                  /* save cmd */
        sim_activate (uptr, idc_rtime);                 /* schedule */
        idd_sirq = int_req[l_IDC] & IDC_DIRMASK;        /* save drv ints */
        int_req[l_IDC] = int_req[l_IDC] & ~IDC_DIRMASK; /* clr drv ints */
        break;
        }

return 0;
}

/* Process WD/WH data */

void idc_wd_byte (uint32 dat)
{
dat = dat & 0xFF;
switch (idc_wdptr) {

    case 0:                                             /* byte 0 = sector */
        idc_sec = dat;
        idc_wdptr++;
        break;

    case 1:                                             /* byte 1 = high hd/cyl */
        idc_hcyl = (idc_hcyl & 0xFF) | (dat << 8);
        idc_wdptr++;
        break;

    case 2:                                             /* byte 2 = low hd/cyl */
        idc_hcyl = (idc_hcyl & 0xFF00) | dat;
        idc_wdptr = 0;
        break;
        }

return;
}

/* Drives: IO routine */

uint32 id (uint32 dev, uint32 op, uint32 dat)
{
uint32 t, u, f;
UNIT *uptr;

if (dev == idc_dib.dno)                                 /* controller? */
    return idc (dev, op, dat);
u = (dev - idc_dib.dno - o_ID0) / o_ID0;                /* get unit num */
uptr = idc_dev.units + u;                               /* get unit ptr */
switch (op) {                                           /* case IO op */

    case IO_ADR:                                        /* select */
        if (idc_sta & STC_IDL)                          /* idle? save unit */
            idc_svun = u;
        return BY;                                      /* byte only */

    case IO_RD:                                         /* read data */
    case IO_RH:
        return 0;

    case IO_WD:                                         /* write data */
        if (idd_wdptr & 1)                              /* low byte? */
            idd_db = (idd_db & 0xFF00) | dat;
        else idd_db = (idd_db & 0xFF) | (dat << 8);     /* no, high */
        idd_wdptr = idd_wdptr ^ 1;                      /* other byte */
        break;

    case IO_SS:                                         /* status */
        if (uptr->flags & UNIT_ATT) t =
            ((uptr->flags & UNIT_WPRT)? STD_WRP: 0) |
            (sim_is_active (uptr)? STD_NRDY: 0) |
            (uptr->STD & STD_UST);
        else t = STD_NRDY | STD_OFFL;                   /* off = X'09' */
        if (t & SETD_EX)                                /* test for ex */
            t = t | STA_EX;
        return t;

    case IO_OC:                                         /* command */
        idd_arm[u] = int_chg (v_IDC + u + 1, dat, idd_arm[u]);
        idd_wdptr = 0;                                  /* init ptr */
        if (idd_arm[u] == 0)                            /* disarmed? */
            idd_sirq &= ~(1 << (v_IDC + u + 1));        /* clr saved req */
        f = dat & CMC_MASK;                             /* get cmd */
        if ((f == 0) ||                                 /* if nop, */
            (f == CMDX_MASK) ||                         /* 0x30, */
            !(idc_sta & STC_IDL) ||                     /* !idle, */
            sim_is_active (uptr))                       /* unit busy, ignore */
            break;
        uptr->FNC = f | CMC_DRV;                        /* save cmd */
        idc_sta = idc_sta & ~STC_IDL;                   /* clr idle */
        sim_activate (uptr, idc_ctime);                 /* schedule */
        break;
        }

return 0;
}

/* Unit service

   If drive command, process; if an interrupt is needed (positioning
   command), schedule second part

   If data transfer command, process; must use selector channel
*/

t_stat idc_svc (UNIT *uptr)
{
int32 diff;
uint32 f, u = uptr - idc_dev.units;                     /* get unit number */
uint32 dtype = GET_DTYPE (uptr->flags);                 /* get drive type */
t_stat r;

if (uptr->FNC & CMC_DRV) {                              /* drive cmd? */
    f = uptr->FNC & CMC_MASK;                           /* get cmd */
    if (uptr->FNC & CMC_DRV1) {                         /* part 2? */
        if (idd_arm[u]) {                               /* drv int armed? */
            if (idc_sta & STC_IDL)                      /* ctrl idle? */
                SET_INT (v_IDC + u + 1);                /* req intr */
            else idd_sirq |= (1 << (v_IDC + u + 1));    /* def intr */
            }
        if ((uptr->flags & UNIT_ATT) == 0)
            return SCPE_OK;
        if (((f & CMDX_MASK) == 0) &&                   /* seek? */
            (f & (CMD_SK | CMD_RST))) {
            if (idd_dcy[u] >= drv_tab[dtype].cyl)       /* bad cylinder? */
                uptr->STD = uptr->STD | STD_SKI;        /* error */
            uptr->CYL = idd_dcy[u];                     /* put on cyl */
            }
        }                                               /* end if p2 */
    else {                                              /* part 1 */
        idc_sta = idc_sta | STC_IDL;                    /* set idle */
        uptr->FNC = uptr->FNC | CMC_DRV1;               /* set part 2 */
        if (f >= CMDX_MASK) {                           /* extended? */
            if (f & CMDX_CLF)                           /* clr fault? */
                uptr->STD = uptr->STD & ~STD_UNS;       /* clr unsafe */
            if (f & (CMDX_RLS | CMDX_SVP | CMDX_SVM))   /* intr expected? */
                sim_activate (uptr, idc_ctime);
            }
        else if (f >= CMDF_SCY) {                       /* tag? */
            if (f & CMDF_SHD)
                uptr->HD = idd_db & HD_MASK;
            else if (f & CMDF_SCY) {
                if (idd_db >= drv_tab[dtype].cyl)       /* bad cylinder? */
                    uptr->STD = uptr->STD | STD_SKI;    /* set seek inc */
                idd_dcy[u] = idd_db & CY_MASK;
                }
            }
        else if (f & (CMD_SK | CMD_RST)) {              /* seek? */
            if (f == CMD_RST)                           /* restore? */
                idd_dcy[u] = 0;
            if (idd_dcy[u] >= drv_tab[dtype].cyl) {     /* bad cylinder? */
                uptr->STD = uptr->STD | STD_SKI;        /* set seek inc */
                idd_dcy[u] = uptr->CYL;                 /* no motion */
                sim_activate (uptr, 0);                 /* finish asap */
                }
            else {                                      /* cylinder ok */
                uptr->STD = uptr->STD & ~STD_SKI;       /* clr seek inc */
                diff = idd_dcy[u] - uptr->CYL;
                if (diff < 0)                           /* ABS cyl diff */
                    diff = -diff;
                else if (diff == 0)                     /* must be nz */
                    diff = 1;
                sim_activate (uptr, diff * idc_stime);
                }
            }
        }                                               /* end else p1 */
    return SCPE_OK;                                     /* end if drv */
    }

switch (uptr->FNC & CMC_MASK) {                         /* case on func */

    case CMC_RCHK:                                      /* read check */
        idc_dter (uptr, 1);                             /* check xfr err */
        break;

#if defined (ID_IDC)
    case CMC_RUNC:                                      /* read uncorr */
#endif
    case CMC_RD:                                        /* read */
        if (sch_actv (idc_dib.sch, idc_dib.dno)) {      /* sch transfer? */
            if (idc_dter (uptr, idc_1st))               /* dte? done */
                return SCPE_OK;
            if ((r = idc_rds (uptr)))                   /* read sec, err? */
                return r;
            idc_1st = 0;
            sch_wrmem (idc_dib.sch, idcxb, IDC_NUMBY);  /* write mem */
            if (sch_actv (idc_dib.sch, idc_dib.dno)) {  /* more to do? */       
                sim_activate (uptr, idc_rtime);         /* reschedule */
                return SCPE_OK;
                }
            break;                                      /* no, set done */
            }
        idc_sta = idc_sta | STC_DTE;                    /* cant work */
        break;

    case CMC_WR:                                        /* write */
        if (sch_actv (idc_dib.sch, idc_dib.dno)) {      /* sch transfer? */
            if (idc_dter (uptr, idc_1st))               /* dte? done */
                return SCPE_OK;
            idc_bptr = sch_rdmem (idc_dib.sch, idcxb, IDC_NUMBY); /* read mem */
            idc_db = idcxb[idc_bptr - 1];               /* last byte */
            if ((r = idc_wds (uptr)))                   /* write sec, err? */
                return r;
            idc_1st = 0;
            if (sch_actv (idc_dib.sch, idc_dib.dno)) {  /* more to do? */       
                sim_activate (uptr, idc_rtime);         /* reschedule */
                return SCPE_OK;
                }
            break;                                      /* no, set done */
            }
        idc_sta = idc_sta | STC_DTE;                    /* cant work */
        break;

    case CMC_FCHK: case CMC_RFMT: case CMC_WFMT: case CMC_WFTK:
        idc_dter (uptr, 1);
        idc_sta = idc_sta | STC_WRP;
        break;

#if defined (ID_IDC)
    case CMC_RRAM:                                      /* read RAM */
        if (sch_actv (idc_dib.sch, idc_dib.dno)) {      /* sch transfer? */
            sch_wrmem (idc_dib.sch, idcxb, IDC_NUMBY * 3);
            if (sch_actv (idc_dib.sch, idc_dib.dno)) {  /* more to do? */       
                sim_activate (uptr, idc_rtime);         /* reschedule */
                return SCPE_OK;
                }
            break;                                      /* no, set done */
            }
        idc_sta = idc_sta | STC_DTE;                    /* cant work */
        break;

    case CMC_WRAM:                                      /* write RAM */
        if (sch_actv (idc_dib.sch, idc_dib.dno)) {      /* sch transfer? */
            sch_rdmem (idc_dib.sch, idcxb, IDC_NUMBY * 3); /* read from mem */
            if (sch_actv (idc_dib.sch, idc_dib.dno)) {  /* more to do? */       
                sim_activate (uptr, idc_rtime);         /* reschedule */
                return SCPE_OK;
                }
            break;                                      /* no, set done */
            }
        idc_sta = idc_sta | STC_DTE;                    /* cant work */
        break;

    case CMC_STST: case CMC_LAMP:                       /* tests */
        break;
#endif

default:
    idc_sta = idc_sta | STC_DTE;
    break;
    }

idc_done (0);                                           /* done */
return SCPE_OK;
}

/* Read data sector */

t_stat idc_rds (UNIT *uptr)
{
uint32 i;

i = fxread (idcxb, sizeof (uint8), IDC_NUMBY, uptr->fileref);
if (ferror (uptr->fileref)) {                           /* error? */
    sim_perror ("IDC I/O error");
    clearerr (uptr->fileref);
    idc_done (STC_DTE);
    return SCPE_IOERR;
    }
for ( ; i < IDC_NUMBY; i++)                             /* fill with 0's */
    idcxb[i] = 0;
return SCPE_OK;
}

/* Write data sector */

t_bool idc_wds (UNIT *uptr)
{
for ( ; idc_bptr < IDC_NUMBY; idc_bptr++)
    idcxb[idc_bptr] = idc_db;                           /* fill with last */
fxwrite (idcxb, sizeof (uint8), IDC_NUMBY, uptr->fileref);
if (ferror (uptr->fileref)) {                           /* error? */
    sim_perror ("IDC I/O error");
    clearerr (uptr->fileref);
    idc_done (STC_DTE);
    return SCPE_IOERR;
    }
return FALSE;
}

/* Data transfer error test routine */

t_bool idc_dter (UNIT *uptr, uint32 first)
{
uint32 cy;
uint32 hd, sc, sa;
uint32 dtype = GET_DTYPE (uptr->flags);                 /* get drive type */

if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
    idc_done (STC_DTE);                                 /* error, done */
    return TRUE;
    }
if ((uptr->flags & UNIT_WPRT) && (uptr->FNC == CMC_WR)) {
    idc_done (STC_WRP);                                 /* error, done */
    return TRUE;
    }
cy = uptr->CYL;                                         /* get cylinder */
hd = uptr->HD;                                          /* get head */
sc = idc_sec & SC_MASK;                                 /* get sector */
if (cy >= drv_tab[dtype].cyl) {                         /* bad cylinder? */
    uptr->STD = uptr->STD | STD_SKI;                    /* error */
    idc_done (STC_DTE);                                 /* error, done */
    return TRUE;
    }
if (hd >= drv_tab[dtype].surf) {                        /* bad head? */
    if (first) {                                        /* 1st xfer? */
        uptr->STD = uptr->STD | STD_UNS;                /* drive unsafe */
        idc_done (STC_ACF);
        }
    else idc_done (STC_CYO);                            /* no, cyl ovf */
    return TRUE;
    }
sa = GET_SA (cy, hd, sc, dtype);                        /* curr disk addr */
fseek (uptr->fileref, sa * IDC_NUMBY, SEEK_SET);        /* seek to pos */
idc_sec = (idc_sec + 1) & SC_MASK;                      /* incr disk addr */
if (idc_sec == 0)
    uptr->HD = uptr->HD + 1;
return FALSE;
}

/* Data transfer done routine */

void idc_done (uint32 flg)
{
idc_sta = (idc_sta | STC_IDL | flg) & ~STA_BSY;         /* set flag, idle */
if (idc_arm)                                            /* if armed,  intr */
    SET_INT (v_IDC);
int_req[l_IDC] = int_req[l_IDC] | idd_sirq;             /* restore drv ints */
idd_sirq = 0;                                           /* clear saved */
if (flg)                                                /* if err, stop sch */
    sch_stop (idc_dib.sch);
return;
}

/* Reset routine */

t_stat idc_reset (DEVICE *dptr)
{
uint32 u;
UNIT *uptr;

idc_sta = STC_IDL | STA_BSY;                            /* idle, busy */
idc_wdptr = 0;
idd_wdptr = 0;
idc_1st = 0;                                            /* clear flag */
idc_svun = idc_db = 0;                                  /* clear unit, buf */
idc_sec = 0;                                            /* clear addr */
idc_hcyl = 0;
CLR_INT (v_IDC);                                        /* clear ctrl int */
CLR_ENB (v_IDC);                                        /* clear ctrl enb */
idc_arm = 0;                                            /* clear ctrl arm */
idd_sirq = 0;
for (u = 0; u < ID_NUMDR; u++) {                        /* loop thru units */
    uptr = idc_dev.units + u;
    uptr->CYL = uptr->STD = 0;
    uptr->HD = uptr->FNC = 0;
    idd_dcy[u] = 0;
    CLR_INT (v_IDC + u + 1);                            /* clear intr */
    CLR_ENB (v_IDC + u + 1);                            /* clear enable */
    idd_arm[u] = 0;                                     /* clear arm */
    sim_cancel (uptr);                                  /* cancel activity */
    }
return SCPE_OK;
}

/* Attach routine (with optional autosizing) */

t_stat idc_attach (UNIT *uptr, CONST char *cptr)
{
uint32 i, p;
t_stat r;

uptr->capac = drv_tab[GET_DTYPE (uptr->flags)].size;
r = attach_unit (uptr, cptr);                           /* attach unit */
if (r != SCPE_OK)                                       /* error? */
    return r;
uptr->CYL = 0;
if ((uptr->flags & UNIT_AUTO) == 0)                     /* autosize? */
    return SCPE_OK;
if ((p = ftell (uptr->fileref)) == 0)
    return SCPE_OK;
for (i = 0; drv_tab[i].surf != 0; i++) {
    if (p <= drv_tab[i].size) {
        uptr->flags = (uptr->flags & ~UNIT_DTYPE) | (i << UNIT_V_DTYPE);
        uptr->capac = drv_tab[i].size;
        return SCPE_OK;
        }
    }
return SCPE_OK;
}

/* Set size command validation routine */

t_stat idc_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = drv_tab[GET_DTYPE (val)].size;
return SCPE_OK;
}
