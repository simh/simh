/* id_dp.c: Interdata 2.5MB/10MB cartridge disk simulator

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

   dp           M46-421 2.5MB/10MB cartridge disk

   18-Mar-05    RMS     Added attached test to detach routine
   25-Jan-04    RMS     Revised for device debug support
   25-Apr-03    RMS     Revised for extended file support
   16-Feb-03    RMS     Fixed read to test transfer ok before selch operation
*/

#include "id_defs.h"
#include <math.h>

#define DP_NUMBY        256                             /* bytes/sector */
#define DP_NUMSC        24                              /* sectors/track */

#define UNIT_V_DTYPE    (UNIT_V_UF + 0)                 /* disk type */
#define UNIT_M_DTYPE    0x1
#define UNIT_V_AUTO     (UNIT_V_UF + 2)                 /* autosize */
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)

#define CYL             u3                              /* current cylinder */
#define STD             u4                              /* drive status */

/* Controller status */

#define STC_OVR         0x80                            /* overrun */
#define STC_ACF         0x40                            /* addr cmp fail */
#define STC_DEF         0x20                            /* def track NI */
#define STC_CYO         0x10                            /* cylinder ovflo */
#define STC_IDL         0x02                            /* ctrl idle */
#define STC_DTE         0x01                            /* xfer error */
#define SETC_EX         (STC_OVR|STC_ACF|STC_DEF|STC_CYO)
#define STC_MASK        (STC_OVR|STC_ACF|STC_DEF|STC_CYO|STA_BSY|STC_IDL|STC_DTE)

/* Controller command */

#define CMC_MASK        0xF                             
#define CMC_CLR         0x8                             /* reset */
#define CMC_RD          0x1                             /* read */
#define CMC_WR          0x2                             /* write */
#define CMC_RCHK        0x3                             /* read check */
#define CMC_RFMT        0x5                             /* read fmt NI */
#define CMC_WFMT        0x6                             /* write fmt NI */

/* Drive status, ^ = dynamic, * = in unit status */

#define STD_WRP         0x80                            /* ^write prot */
#define STD_WCK         0x40                            /* write check NI */
#define STD_ILA         0x20                            /* *illegal addr */
#define STD_ILK         0x10                            /* ^addr interlock */
#define STD_MOV         0x08                            /* *heads in motion */
#define STD_INC         0x02                            /* seek incomplete NI */
#define STD_NRDY        0x01                            /* ^not ready */
#define STD_UST         (STD_ILA | STD_MOV)             /* set from unit */
#define SETD_EX         (STD_WCK | STD_ILA | STD_ILK)   /* set examine */

/* Drive command */

#define CMD_SK          0x02                            /* seek */
#define CMD_RST         0x01                            /* restore */

/* Head/sector register */

#define HS_SMASK        0x1F                            /* sector mask */
#define HS_V_SRF        5                               /* surface */
#define HS_HMASK        0x20                            /* head mask */
#define HS_MASK         (HS_HMASK | HS_SMASK)
#define GET_SEC(x)      ((x) & HS_SMASK)
#define GET_SRF(x)      (((x) & HS_HMASK) >> HS_V_SRF) 

#define GET_SA(p,cy,sf,sc,t) (((((((p)*drv_tab[t].cyl)+(cy))*drv_tab[t].surf)+(sf))* \
                        DP_NUMSC)+(sc))
#define GET_ROTATE(x)   ((int) fmod (sim_gtime() / ((double) (x)), \
                        ((double) DP_NUMSC)))

/* This controller supports two different disk drive types:

   type         #sectors/       #surfaces/      #cylinders/
                 surface         cylinder        drive

   2315         24              2               203
   5440         24              4               408

   In theory, each drive can be a different type.  The size field in
   each unit selects the drive capacity for each drive and thus the
   drive type.  DISKS MUST BE DECLARED IN ASCENDING SIZE AND MUST HAVE
   THE SAME SECTORS/TRACK.
*/

#define TYPE_2315       0
#define CYL_2315        203
#define SURF_2315       2
#define SIZE_2315       (DP_NUMSC * SURF_2315 * CYL_2315 * DP_NUMBY)

#define TYPE_5440       1
#define CYL_5440        408
#define SURF_5440       2
#define SIZE_5440       (2 * DP_NUMSC * SURF_5440 * CYL_5440 * DP_NUMBY)

struct drvtyp {
    int32       cyl;                                    /* cylinders */
    uint32      surf;                                   /* surfaces */
    uint32      size;                                   /* #blocks */
    };

static struct drvtyp drv_tab[] = {
    { CYL_2315, SURF_2315, SIZE_2315 },
    { CYL_5440, SURF_5440, SIZE_5440 },
    { 0 }
    };

extern uint32 int_req[INTSZ], int_enb[INTSZ];

uint8 dpxb[DP_NUMBY];                                   /* xfer buffer */
uint32 dp_bptr = 0;                                     /* buffer ptr */
uint32 dp_db = 0;                                       /* ctrl buffer */
uint32 dp_cyl = 0;                                      /* drive buffer */
uint32 dp_sta = 0;                                      /* ctrl status */
uint32 dp_cmd = 0;                                      /* ctrl command */
uint32 dp_plat = 0;                                     /* platter */
uint32 dp_hdsc = 0;                                     /* head/sector */
uint32 dp_svun = 0;                                     /* most recent unit */
uint32 dp_1st = 0;                                      /* first byte */
uint32 dpd_arm[DP_NUMDR] = { 0 };                       /* drives armed */
int32 dp_stime = 100;                                   /* seek latency */
int32 dp_rtime = 100;                                   /* rotate latency */
int32 dp_wtime = 1;                                     /* word time */
uint8 dp_tplte[(2 * DP_NUMDR) + 2];                     /* fix/rmv + ctrl + end */

uint32 dp (uint32 dev, uint32 op, uint32 dat);
void dp_ini (t_bool dtpl);
t_stat dp_svc (UNIT *uptr);
t_stat dp_reset (DEVICE *dptr);
t_stat dp_attach (UNIT *uptr, CONST char *cptr);
t_stat dp_detach (UNIT *uptr);
t_stat dp_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dp_rds (UNIT *uptr);
t_stat dp_wds (UNIT *uptr);
t_bool dp_dter (UNIT *uptr, uint32 first);
void dp_done (uint32 flg);

extern t_stat id_dboot (int32 u, DEVICE *dptr);

/* DP data structures

   dp_dev       DP device descriptor
   dp_unit      DP unit list
   dp_reg       DP register list
   dp_mod       DP modifier list
*/

DIB dp_dib = { d_DPC, 0, v_DPC, dp_tplte, &dp, &dp_ini };

UNIT dp_unit[] = {
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+(TYPE_5440 << UNIT_V_DTYPE), SIZE_5440) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+(TYPE_5440 << UNIT_V_DTYPE), SIZE_5440) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+(TYPE_5440 << UNIT_V_DTYPE), SIZE_5440) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+(TYPE_5440 << UNIT_V_DTYPE), SIZE_5440) }
    };

REG dp_reg[] = {
    { HRDATA (CMD, dp_cmd, 3) },
    { HRDATA (STA, dp_sta, 8) },
    { HRDATA (BUF, dp_db, 8) },
    { HRDATA (PLAT, dp_plat, 1) },
    { HRDATA (HDSC, dp_hdsc, 6) },
    { HRDATA (CYL, dp_cyl, 9) },
    { HRDATA (SVUN, dp_svun, 8), REG_HIDDEN },
    { BRDATA (DBUF, dpxb, 16, 8, DP_NUMBY) },
    { HRDATA (DBPTR, dp_bptr, 9), REG_RO },
    { FLDATA (FIRST, dp_1st, 0) },
    { GRDATA (IREQ, int_req[l_DPC], 16, DP_NUMDR + 1, i_DPC) },
    { GRDATA (IENB, int_enb[l_DPC], 16, DP_NUMDR + 1, i_DPC) },
    { BRDATA (IARM, dpd_arm, 16, 1, DP_NUMDR) },
    { DRDATA (RTIME, dp_rtime, 24), PV_LEFT | REG_NZ },
    { DRDATA (STIME, dp_stime, 24), PV_LEFT | REG_NZ },
    { DRDATA (WTIME, dp_wtime, 24), PV_LEFT | REG_NZ },
    { URDATA (UCYL, dp_unit[0].CYL, 16, 9, 0,
              DP_NUMDR, REG_RO) },
    { URDATA (UST, dp_unit[0].STD, 16, 8, 0,
              DP_NUMDR, REG_RO) },
    { URDATA (CAPAC, dp_unit[0].capac, 10, T_ADDR_W, 0,
              DP_NUMDR, PV_LEFT | REG_HRO) },
    { HRDATA (DEVNO, dp_dib.dno, 8), REG_HRO },
    { HRDATA (SELCH, dp_dib.sch, 2), REG_HRO },
    { NULL }
    };

MTAB dp_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write enable drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "Write lock drive" },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_2315 << UNIT_V_DTYPE) + UNIT_ATT,
      "2315", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_5440 << UNIT_V_DTYPE) + UNIT_ATT,
      "5440", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_2315 << UNIT_V_DTYPE),
      "2315", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_5440 << UNIT_V_DTYPE),
      "5440", NULL, NULL },
    { (UNIT_AUTO+UNIT_ATT), UNIT_AUTO, "autosize", NULL, NULL },
    { UNIT_AUTO, UNIT_AUTO, NULL, "AUTOSIZE", NULL },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_2315 << UNIT_V_DTYPE),
      NULL, "2315", &dp_set_size }, 
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_5440 << UNIT_V_DTYPE),
      NULL, "5440", &dp_set_size },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "SELCH", "SELCH",
      &set_sch, &show_sch, NULL },
    { 0 }
    };

DEVICE dp_dev = {
    "DP", dp_unit, dp_reg, dp_mod,
    DP_NUMDR, 16, 24, 1, 16, 8,
    NULL, NULL, &dp_reset,
    &id_dboot, &dp_attach, &dp_detach,
    &dp_dib, DEV_DISABLE | DEV_DEBUG
    };

/* Controller: IO routine */

uint32 dpc (uint32 dev, uint32 op, uint32 dat)
{
uint32 f, t, u;
UNIT *uptr;
static uint8 good_cmd[8] = { 0, 1, 1, 1, 0, 0, 0, 0 };

switch (op) {                                           /* case IO op */

    case IO_ADR:                                        /* select */
        sch_adr (dp_dib.sch, dev);                      /* inform sel ch */
        return BY;                                      /* byte only */

    case IO_RD:                                         /* read data */
        if (dp_sta & STC_IDL)                           /* if idle */
            return GET_ROTATE (dp_rtime);               /* return sector */
        else dp_sta = dp_sta | STA_BSY;                 /* xfr? set busy */
        return dp_db;                                   /* return data */

    case IO_WD:                                         /* write data */
        if (DEBUG_PRS (dp_dev)) fprintf (sim_deb,
            ">>DPC WD = %02X, STA = %02X\n", dat, dp_sta);
        if (dp_sta & STC_IDL)                           /* idle? hdsc */
            dp_hdsc = dat & HS_MASK;
        else {                                          /* data xfer */
            dp_sta = dp_sta | STA_BSY;                  /* set busy */
            dp_db = dat & 0xFF;                         /* store data */
            }
        break;

    case IO_SS:                                         /* status */
        t = dp_sta & STC_MASK;                          /* get status */
        if (t & SETC_EX)                                /* test for EX */
            t = t | STA_EX;
        return t;

    case IO_OC:                                         /* command */
        if (DEBUG_PRS (dp_dev)) fprintf (sim_deb,
            ">>DPC OC = %02X, STA = %02X\n", dat, dp_sta);
        f = dat & CMC_MASK;                             /* get cmd */
        if (f & CMC_CLR) {                              /* clear? */
            dp_reset (&dp_dev);                         /* reset world */
            break;
            }
        u = (dp_svun - dp_dib.dno - o_DP0) / o_DP0;     /* get unit */
        uptr = dp_dev.units + u;                        /* ignore if busy */
        if (!(dp_sta & STC_IDL) || sim_is_active (uptr))
            break;
        dp_cmd = f;                                     /* save cmd */
        if (dp_cmd == CMC_WR)                           /* write: bsy=0 else */
            dp_sta = 0;
        else dp_sta = STA_BSY;                          /* bsy=1,idl,err=0 */
        dp_1st = 1;                                     /* xfr not started */
        dp_bptr = 0;                                    /* buffer empty */
        if (dp_svun & o_DPF)                            /* upper platter? */
            dp_plat = 1;
        else dp_plat = 0;                               /* no, lower */
        if (good_cmd[f])                                /* legal? sched */
            sim_activate (uptr, dp_rtime);
        break;
        }

return 0;
}

/* Drives: IO routine */

uint32 dp (uint32 dev, uint32 op, uint32 dat)
{
int32 diff;
uint32 t, u;
UNIT *uptr;

if (dev == dp_dib.dno)                                  /* controller? */
    return dpc (dev, op, dat);
u = (dev - dp_dib.dno - o_DP0) / o_DP0;                 /* get unit num */
uptr = dp_dev.units + u;                                /* get unit ptr */
switch (op) {                                           /* case IO op */

    case IO_ADR:                                        /* select */
        if (dp_sta & STC_IDL)                           /* idle? save unit */
            dp_svun = dev;
        return BY;                                      /* byte only */

    case IO_WD:                                         /* write data */
        if (DEBUG_PRS (dp_dev))
            fprintf (sim_deb, ">>DP%d WD = %02X, STA = %02X\n",
                     u, dat, dp_sta);
        if (GET_DTYPE (uptr->flags) == TYPE_2315)       /* 2.5MB drive? */
            dp_cyl = dat & 0xFF;                        /* cyl is 8b */
        else dp_cyl = ((dp_cyl << 8) | dat) & DMASK16;  /* insert byte */
        break;

    case IO_SS:                                         /* status */
        if (uptr->flags & UNIT_ATT) t =                 /* onl? */
            ((uptr->flags & UNIT_WPRT)? STD_WRP: 0) |
            ((dp_sta & STC_IDL)? 0: STD_ILK) |
            (uptr->STD & STD_UST);
        else t = STD_MOV | STD_NRDY;                    /* off = X'09' */
        if (t & SETD_EX)                                /* test for ex */
            t = t | STA_EX;
        return t;
 
   case IO_OC:                                          /* command */
        if (DEBUG_PRS (dp_dev))
            fprintf (sim_deb, ">>DP%d OC = %02X, STA = %02X\n",
                     u, dat, dp_sta);
        dpd_arm[u] = int_chg (v_DPC + u + 1, dat, dpd_arm[u]);
        if (dat & CMD_SK)                               /* seek? get cyl */
            t = dp_cyl;
        else if (dat & CMD_RST)                         /* rest? cyl 0 */
            t = 0; 
        else break;                                     /* no action */
        diff = t - uptr->CYL;
        if (diff < 0)                                   /* ABS cyl diff */
            diff = -diff;
        else if (diff == 0)                             /* must be nz */
            diff = 1;
        uptr->STD = STD_MOV;                            /* stat = moving */
        uptr->CYL = t;                                  /* put on cyl */
        sim_activate (uptr, diff * dp_stime);           /* schedule */
        break;
        }

return 0;
}

/* Unit service

   If seek done, on cylinder;
   if read check, signal completion;
   else, do read or write
*/

t_stat dp_svc (UNIT *uptr)
{
uint32 u = uptr - dp_dev.units;                         /* get unit number */
int32 cyl = uptr->CYL;                                  /* get cylinder */
uint32 dtype = GET_DTYPE (uptr->flags);                 /* get drive type */
t_stat r;

if (uptr->STD & STD_MOV) {                              /* seek? */
    uptr->STD = 0;                                      /* clr seek in prog */
    if ((uptr->flags & UNIT_ATT) == 0)                  /* offl? hangs */
        return SCPE_OK;
    if (cyl >= drv_tab[dtype].cyl) {                    /* bad cylinder? */
        uptr->STD = STD_ILA;                            /* error */
        uptr->CYL = drv_tab[dtype].cyl - 1;             /* put at edge */
        }
    if (dpd_arm[u])                                     /* req intr */
        SET_INT (v_DPC + u + 1);
    return SCPE_OK;
    }

switch (dp_cmd & 0x7) {                                 /* case on func */

    case CMC_RCHK:                                      /* read check */
        dp_dter (uptr, 1);                              /* check xfr err */
        break;

    case CMC_RD:                                        /* read */
        if (sch_actv (dp_dib.sch, dp_dib.dno)) {        /* sch transfer? */
            if (dp_dter (uptr, dp_1st))                 /* check xfr err */
                return SCPE_OK;
            if ((r = dp_rds (uptr)))                    /* read sec, err? */
                return r;
            dp_1st = 0;
            sch_wrmem (dp_dib.sch, dpxb, DP_NUMBY);     /* write to memory */
            if (sch_actv (dp_dib.sch, dp_dib.dno)) {    /* more to do? */       
                sim_activate (uptr, dp_rtime);          /* reschedule */
                return SCPE_OK;
                }
            break;                                      /* no, set done */
            } 
        dp_sta = dp_sta | STC_DTE;                      /* can't work */
        break;

    case CMC_WR:                                        /* write */
        if (sch_actv (dp_dib.sch, dp_dib.dno)) {        /* sch transfer? */
            if (dp_dter (uptr, dp_1st))                 /* check xfr err */
                return SCPE_OK;
            dp_bptr = sch_rdmem (dp_dib.sch, dpxb, DP_NUMBY); /* read from mem */
            dp_db = dpxb[dp_bptr - 1];                  /* last byte */
            if ((r = dp_wds (uptr)))                    /* write sec, err? */
                return r;
            dp_1st = 0;
            if (sch_actv (dp_dib.sch, dp_dib.dno)) {    /* more to do? */       
                sim_activate (uptr, dp_rtime);          /* reschedule */
                return SCPE_OK;
                }
            break;                                      /* no, set done */
            }
        dp_sta = dp_sta | STC_DTE;                      /* can't work */
        break;
        }                                               /* end case func */

dp_done (0);                                            /* done */
return SCPE_OK;
}

/* Read data sector */

t_stat dp_rds (UNIT *uptr)
{
uint32 i;

i = fxread (dpxb, sizeof (uint8), DP_NUMBY, uptr->fileref);
for ( ; i < DP_NUMBY; i++)                              /* fill with 0's */
    dpxb[i] = 0;
if (ferror (uptr->fileref)) {                           /* error? */
    sim_perror ("DP I/O error");
    clearerr (uptr->fileref);
    dp_done (STC_DTE);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Write data sector */

t_stat dp_wds (UNIT *uptr)
{
for ( ; dp_bptr < DP_NUMBY; dp_bptr++)
    dpxb[dp_bptr] = dp_db;                              /* fill with last */
fxwrite (dpxb, sizeof (uint8), DP_NUMBY, uptr->fileref);
if (ferror (uptr->fileref)) {                           /* error? */
    sim_perror ("DP I/O error");
    clearerr (uptr->fileref);
    dp_done (STC_DTE);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Data transfer error test routine */

t_bool dp_dter (UNIT *uptr, uint32 first)
{
uint32 hd, sc, sa;
uint32 dtype = GET_DTYPE (uptr->flags);                 /* get drive type */

if (((uptr->flags & UNIT_ATT) == 0) ||                  /* not attached? */
    ((uptr->flags & UNIT_WPRT) && (dp_cmd == CMC_WR))) {
    dp_done (STC_DTE);                                  /* error, done */
    return TRUE;
    }
hd = GET_SRF (dp_hdsc);                                 /* get head */
sc = GET_SEC (dp_hdsc);                                 /* get sector */
if (dp_cyl != (uint32) uptr->CYL) {                     /* wrong cylinder? */
    if (dp_cyl == 0)
        uptr->CYL = 0;
    else {
        dp_done (STC_ACF);                              /* error, done */
        return TRUE;
        } 
    }
if (sc >= DP_NUMSC) {                                   /* bad sector? */
    dp_done (STC_OVR);                                  /* error, done */
    return TRUE;
    }
if (!first && (sc == 0) && (hd == 0)) {                 /* cyl overflow? */
    dp_done (STC_CYO);                                  /* error, done */
    return TRUE;
    }
sa = GET_SA (dp_plat, uptr->CYL, hd, sc, dtype);        /* curr disk addr */
fseek (uptr->fileref, sa * DP_NUMBY, SEEK_SET);
if ((sc + 1) < DP_NUMSC)                                /* end of track? */
    dp_hdsc = dp_hdsc + 1;
else dp_hdsc = (dp_hdsc ^ HS_HMASK) & HS_HMASK;         /* sec 0, nxt srf */
return FALSE;
}

/* Data transfer done routine */

void dp_done (uint32 flg)
{
dp_sta = (dp_sta | STC_IDL | flg) & ~STA_BSY;           /* set flag, idle */
SET_INT (v_DPC);                                        /* unmaskable intr */
if (flg)                                                /* if err, stop ch */
    sch_stop (dp_dib.sch);
return;
}

/* Reset routine */

t_stat dp_reset (DEVICE *dptr)
{
uint32 u;
UNIT *uptr;

dp_cmd = 0;                                             /* clear cmd */
dp_sta = STA_BSY | STC_IDL;                             /* idle, busy */
dp_1st = 0;                                             /* clear flag */
dp_svun = dp_db = 0;                                    /* clear unit, buf */
dp_plat = 0;
dp_hdsc = 0;                                            /* clear addr */
CLR_INT (v_DPC);                                        /* clear ctrl int */
SET_ENB (v_DPC);                                        /* always enabled */
for (u = 0; u < DP_NUMDR; u++) {                        /* loop thru units */
    uptr = dp_dev.units + u;
    uptr->CYL = uptr->STD = 0;
    CLR_INT (v_DPC + u + 1);                            /* clear intr */
    CLR_ENB (v_DPC + u + 1);                            /* clear enable */
    dpd_arm[u] = 0;                                     /* clear arm */
    sim_cancel (uptr);                                  /* cancel activity */
    }
return SCPE_OK;
}

/* Attach routine (with optional autosizing) */

t_stat dp_attach (UNIT *uptr, CONST char *cptr)
{
uint32 i, p;
t_stat r;

uptr->capac = drv_tab[GET_DTYPE (uptr->flags)].size;
r = attach_unit (uptr, cptr);                           /* attach unit */
if (r != SCPE_OK)                                       /* error? */
    return r;
uptr->CYL = 0;
if ((uptr->flags & UNIT_AUTO) == 0)                 /* autosize? */
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

/* Detach routine (generates an interrupt) */

t_stat dp_detach (UNIT *uptr)
{
uint32 u = uptr - dp_dev.units;

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
if (dpd_arm[u])                                         /* if arm, intr */
    SET_INT (v_DPC + u + 1);
return detach_unit (uptr);
}

/* Set size command validation routine */

t_stat dp_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = drv_tab[GET_DTYPE (val)].size;
return SCPE_OK;
}

/* Create device number (T) or interrupt (F) template */

void dp_ini (t_bool dtpl)
{
int32 u, j, dev;

dp_tplte[0] = 0;                                        /* controller */
for (u = 0, j = 1; u < DP_NUMDR; u++) {                 /* loop thru units */
    dev = (u + 1) * o_DP0;                              /* drive dev # */
    dp_tplte[j++] = dev;
    if (dtpl && (GET_DTYPE (dp_unit[u].flags) == TYPE_5440)) 
        dp_tplte[j++] = dev + o_DPF;                    /* if fixed */
    }
dp_tplte[j] = TPL_END;                                  /* end marker */
return;
}
