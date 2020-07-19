/* pdp8_rf.c: RF08 fixed head disk simulator

   Copyright (c) 1993-2013, Robert M Supnik

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

   rf           RF08 fixed head disk

   17-Sep-13    RMS     Changed to use central set_bootpc routine
   03-Sep-13    RMS     Added explicit void * cast
   15-May-06    RMS     Fixed bug in autosize attach (Dave Gesswein)
   07-Jan-06    RMS     Fixed unaligned register access bug (Doug Carman)
   04-Jan-04    RMS     Changed sim_fsize calling sequence
   26-Oct-03    RMS     Cleaned up buffer copy code
   26-Jul-03    RMS     Fixed bug in set size routine
   14-Mar-03    RMS     Fixed variable platter interaction with save/restore
   03-Mar-03    RMS     Fixed autosizing
   02-Feb-03    RMS     Added variable platter and autosizing support
   04-Oct-02    RMS     Added DIB, device number support
   28-Nov-01    RMS     Added RL8A support
   25-Apr-01    RMS     Added device enable/disable support
   19-Mar-01    RMS     Added disk monitor bootstrap, fixed IOT decoding
   15-Feb-01    RMS     Fixed 3 cycle data break sequence
   14-Apr-99    RMS     Changed t_addr to unsigned
   30-Mar-98    RMS     Fixed bug in RF bootstrap

   The RF08 is a head-per-track disk.  It uses the three cycle data break
   facility.  To minimize overhead, the entire RF08 is buffered in memory.

   Two timing parameters are provided:

   rf_time      Interword timing, must be non-zero
   rf_burst     Burst mode, if 0, DMA occurs cycle by cycle; otherwise,
                DMA occurs in a burst
*/

#include "pdp8_defs.h"
#include <math.h>

#define UNIT_V_AUTO     (UNIT_V_UF + 0)                 /* autosize */
#define UNIT_V_PLAT     (UNIT_V_UF + 1)                 /* #platters - 1 */
#define UNIT_M_PLAT     03
#define UNIT_GETP(x)    ((((x) >> UNIT_V_PLAT) & UNIT_M_PLAT) + 1)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define UNIT_PLAT       (UNIT_M_PLAT << UNIT_V_PLAT)

/* Constants */

#define RF_NUMWD        2048                            /* words/track */
#define RF_NUMTR        128                             /* tracks/disk */
#define RF_DKSIZE       (RF_NUMTR * RF_NUMWD)           /* words/disk */
#define RF_NUMDK        4                               /* disks/controller */
#define RF_WC           07750                           /* word count */
#define RF_MA           07751                           /* mem address */
#define RF_WMASK        (RF_NUMWD - 1)                  /* word mask */

/* Parameters in the unit descriptor */

#define FUNC            u4                              /* function */
#define RF_READ         2                               /* read */
#define RF_WRITE        4                               /* write */

/* Status register */

#define RFS_PCA         04000                           /* photocell status */
#define RFS_DRE         02000                           /* data req enable */
#define RFS_WLS         01000                           /* write lock status */
#define RFS_EIE         00400                           /* error int enable */
#define RFS_PIE         00200                           /* photocell int enb */
#define RFS_CIE         00100                           /* done int enable */
#define RFS_MEX         00070                           /* memory extension */
#define RFS_DRL         00004                           /* data late error */
#define RFS_NXD         00002                           /* non-existent disk */
#define RFS_PER         00001                           /* parity error */
#define RFS_ERR         (RFS_WLS + RFS_DRL + RFS_NXD + RFS_PER)
#define RFS_V_MEX       3

#define GET_MEX(x)      (((x) & RFS_MEX) << (12 - RFS_V_MEX))
#define GET_POS(x)      ((int) fmod (sim_gtime() / ((double) (x)), \
                        ((double) RF_NUMWD)))
#define UPDATE_PCELL    if (GET_POS(rf_time) < 6) rf_sta = rf_sta | RFS_PCA; \
                        else rf_sta = rf_sta & ~RFS_PCA
#define RF_INT_UPDATE   if ((rf_done && (rf_sta & RFS_CIE)) || \
                            ((rf_sta & RFS_ERR) && (rf_sta & RFS_EIE)) || \
                            ((rf_sta & RFS_PCA) && (rf_sta & RFS_PIE))) \
                            int_req = int_req | INT_RF; \
                        else int_req = int_req & ~INT_RF

extern uint16 M[];
extern int32 int_req, stop_inst;
extern UNIT cpu_unit;

int32 rf_sta = 0;                                       /* status register */
int32 rf_da = 0;                                        /* disk address */
int32 rf_done = 0;                                      /* done flag */
int32 rf_wlk = 0;                                       /* write lock */
int32 rf_time = 10;                                     /* inter-word time */
int32 rf_burst = 1;                                     /* burst mode flag */
int32 rf_stopioe = 1;                                   /* stop on error */

int32 rf60 (int32 IR, int32 AC);
int32 rf61 (int32 IR, int32 AC);
int32 rf62 (int32 IR, int32 AC);
int32 rf64 (int32 IR, int32 AC);
t_stat rf_svc (UNIT *uptr);
t_stat pcell_svc (UNIT *uptr);
t_stat rf_reset (DEVICE *dptr);
t_stat rf_boot (int32 unitno, DEVICE *dptr);
t_stat rf_attach (UNIT *uptr, CONST char *cptr);
t_stat rf_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
const char *rf_description (DEVICE *dptr);

/* RF08 data structures

   rf_dev       RF device descriptor
   rf_unit      RF unit descriptor
   pcell_unit   photocell timing unit
   rf_reg       RF register list
*/

DIB rf_dib = { DEV_RF, 5, { &rf60, &rf61, &rf62, NULL, &rf64 } };

UNIT rf_units[] = {
    { UDATA (&rf_svc, UNIT_FIX+UNIT_ATTABLE+
             UNIT_BUFABLE+UNIT_MUSTBUF, RF_DKSIZE) },
    { UDATA (&pcell_svc, UNIT_DIS, 0) }
    };
#define rf_unit    rf_units[0]
#define pcell_unit rf_units[1]

REG rf_reg[] = {
    { ORDATAD (STA, rf_sta, 12, "status") },
    { ORDATAD (DA, rf_da, 20, "low order disk address") },
    { ORDATAD (WC, M[RF_WC], 12, "word count (in memory)"), REG_FIT },
    { ORDATAD (MA, M[RF_MA], 12, "memory address (in memory)"), REG_FIT },
    { FLDATAD (DONE, rf_done, 0, "device done flag") },
    { FLDATAD (INT, int_req, INT_V_RF, "interrupt pending flag") },
    { ORDATAD (WLK, rf_wlk, 32, "write lock switches") },
    { DRDATAD (TIME, rf_time, 24, "rotational delay, per word"), REG_NZ + PV_LEFT },
    { FLDATAD (BURST, rf_burst, 0, "burst flag") },
    { FLDATAD (STOP_IOE, rf_stopioe, 0, "stop on I/O error") },
    { DRDATA (CAPAC, rf_unit.capac, 21), REG_HRO },
    { ORDATA (DEVNUM, rf_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB rf_mod[] = {
    { UNIT_PLAT, (0 << UNIT_V_PLAT), NULL, "1P", &rf_set_size },
    { UNIT_PLAT, (1 << UNIT_V_PLAT), NULL, "2P", &rf_set_size },
    { UNIT_PLAT, (2 << UNIT_V_PLAT), NULL, "3P", &rf_set_size },
    { UNIT_PLAT, (3 << UNIT_V_PLAT), NULL, "4P", &rf_set_size },
    { UNIT_AUTO, UNIT_AUTO, "autosize", "AUTOSIZE", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { 0 }
    };

DEVICE rf_dev = {
    "RF", rf_units, rf_reg, rf_mod,
    2, 8, 20, 1, 8, 12,
    NULL, NULL, &rf_reset,
    &rf_boot, &rf_attach, NULL,
    &rf_dib, DEV_DISABLE | DEV_DIS, 0,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &rf_description
    };

/* IOT routines */

int32 rf60 (int32 IR, int32 AC)
{
int32 t;
int32 pulse = IR & 07;

UPDATE_PCELL;                                           /* update photocell */
if (pulse & 1) {                                        /* DCMA */
    rf_da = rf_da & ~07777;                             /* clear DAR<8:19> */
    rf_done = 0;                                        /* clear done */
    rf_sta = rf_sta & ~RFS_ERR;                         /* clear errors */
    RF_INT_UPDATE;                                      /* update int req */
    }
if (pulse & 6) {                                        /* DMAR, DMAW */
    rf_da = rf_da | AC;                                 /* DAR<8:19> |= AC */
    rf_unit.FUNC = pulse & ~1;                          /* save function */
    t = (rf_da & RF_WMASK) - GET_POS (rf_time);         /* delta to new loc */
    if (t < 0)                                          /* wrap around? */
        t = t + RF_NUMWD;
    sim_activate (&rf_unit, t * rf_time);               /* schedule op */
    AC = 0;                                             /* clear AC */
    }
return AC;
}

int32 rf61 (int32 IR, int32 AC)
{
int32 pulse = IR & 07;

UPDATE_PCELL;                                           /* update photocell */
switch (pulse) {                                        /* decode IR<9:11> */

    case 1:                                             /* DCIM */
        rf_sta = rf_sta & 07007;                        /* clear STA<3:8> */
        int_req = int_req & ~INT_RF;                    /* clear int req */
        sim_cancel (&pcell_unit);                       /* cancel photocell */
        return AC;

    case 2:                                             /* DSAC */
        return ((rf_da & RF_WMASK) == GET_POS (rf_time))? IOT_SKP: 0;

    case 5:                                             /* DIML */
        rf_sta = (rf_sta & 07007) | (AC & 0770);        /* STA<3:8> <- AC */
        if (rf_sta & RFS_PIE)                           /* photocell int? */
            sim_activate (&pcell_unit, (RF_NUMWD - GET_POS (rf_time)) *
                rf_time);
        else sim_cancel (&pcell_unit);
        RF_INT_UPDATE;                                  /* update int req */
        return 0;                                       /* clear AC */

    case 6:                                             /* DIMA */
        return rf_sta;                                  /* AC <- STA<0:11> */
        }

return AC;
}

int32 rf62 (int32 IR, int32 AC)
{
int32 pulse = IR & 07;

UPDATE_PCELL;                                           /* update photocell */
if (pulse & 1) {                                        /* DFSE */
    if (rf_sta & RFS_ERR)
        AC = AC | IOT_SKP;
    }
if (pulse & 2) {                                        /* DFSC */
    if (pulse & 4)                                      /* for DMAC */
        AC = AC & ~07777;
    else if (rf_done)
        AC = AC | IOT_SKP;
    }
if (pulse & 4)                                          /* DMAC */
    AC = AC | (rf_da & 07777);
return AC;
}

int32 rf64 (int32 IR, int32 AC)
{
int32 pulse = IR & 07;

UPDATE_PCELL;                                           /* update photocell */
switch (pulse) {                                        /* decode IR<9:11> */

    case 1:                                             /* DCXA */
        rf_da = rf_da & 07777;                          /* clear DAR<0:7> */
        break;

    case 3:                                             /* DXAL */
        rf_da = rf_da & 07777;                          /* clear DAR<0:7> */
    case 2:                                             /* DXAL w/o clear */
        rf_da = rf_da | ((AC & 0377) << 12);            /* DAR<0:7> |= AC */
        AC = 0;                                         /* clear AC */
        break;

    case 5:                                             /* DXAC */
        AC = 0;                                         /* clear AC */
    case 4:                                             /* DXAC w/o clear */
        AC = AC | ((rf_da >> 12) & 0377);               /* AC |= DAR<0:7> */
        break;

    default:
        AC = (stop_inst << IOT_V_REASON) + AC;
        break;
        }                                               /* end switch */

if ((uint32) rf_da >= rf_unit.capac)
    rf_sta = rf_sta | RFS_NXD;
else rf_sta = rf_sta & ~RFS_NXD;
RF_INT_UPDATE;
return AC;
}

/* Unit service

   Note that for reads and writes, memory addresses wrap around in the
   current field.  This code assumes the entire disk is buffered.
*/

t_stat rf_svc (UNIT *uptr)
{
int32 pa, t, mex;
int16 *fbuf = (int16 *) uptr->filebuf;

UPDATE_PCELL;                                           /* update photocell */
if ((uptr->flags & UNIT_BUF) == 0) {                    /* not buf? abort */
    rf_sta = rf_sta | RFS_NXD;
    rf_done = 1;
    RF_INT_UPDATE;                                      /* update int req */
    return IORETURN (rf_stopioe, SCPE_UNATT);
    }

mex = GET_MEX (rf_sta);
do {
    if ((uint32) rf_da >= rf_unit.capac) {              /* disk overflow? */
        rf_sta = rf_sta | RFS_NXD;
        break;
        }
    M[RF_WC] = (M[RF_WC] + 1) & 07777;                  /* incr word count */
    M[RF_MA] = (M[RF_MA] + 1) & 07777;                  /* incr mem addr */
    pa = mex | M[RF_MA];                                /* add extension */
    if (uptr->FUNC == RF_READ) {                        /* read? */
        if (MEM_ADDR_OK (pa))                           /* if !nxm */
            M[pa] = fbuf[rf_da];                        /* read word */
        }
    else {                                              /* write */
        t = ((rf_da >> 15) & 030) | ((rf_da >> 14) & 07);
        if ((rf_wlk >> t) & 1)                          /* write locked? */
            rf_sta = rf_sta | RFS_WLS;
        else {                                          /* not locked */
            fbuf[rf_da] = M[pa];                        /* write word */
            if (((uint32) rf_da) >= uptr->hwmark)
                uptr->hwmark = rf_da + 1;
            }
        }
    rf_da = (rf_da + 1) & 03777777;                     /* incr disk addr */
    } while ((M[RF_WC] != 0) && (rf_burst != 0));       /* brk if wc, no brst */

if ((M[RF_WC] != 0) && ((rf_sta & RFS_ERR) == 0))       /* more to do? */
    sim_activate (&rf_unit, rf_time);                   /* sched next */
else {
    rf_done = 1;                                        /* done */
    RF_INT_UPDATE;                                      /* update int req */
    }
return SCPE_OK;
}

/* Photocell unit service */

t_stat pcell_svc (UNIT *uptr)
{
rf_sta = rf_sta | RFS_PCA;                              /* set photocell */
if (rf_sta & RFS_PIE) {                                 /* int enable? */
    sim_activate (&pcell_unit, RF_NUMWD * rf_time);
    int_req = int_req | INT_RF;
    }
return SCPE_OK;
}

/* Reset routine */

t_stat rf_reset (DEVICE *dptr)
{
rf_sta = rf_da = 0;
rf_done = 1;
int_req = int_req & ~INT_RF;                            /* clear interrupt */
sim_cancel (&rf_unit);
sim_cancel (&pcell_unit);
return SCPE_OK;
}

/* Bootstrap routine */

#define OS8_START       07750
#define OS8_LEN         (sizeof (os8_rom) / sizeof (int16))
#define DM4_START       00200
#define DM4_LEN         (sizeof (dm4_rom) / sizeof (int16))

static const uint16 os8_rom[] = {
    07600,                      /* 7750, CLA CLL        ; also word count */
    06603,                      /* 7751, DMAR           ; also address */
    06622,                      /* 7752, DFSC           ; done? */
    05352,                      /* 7753, JMP .-1        ; no */
    05752                       /* 7754, JMP @.-2       ; enter boot */
    };

static const uint16 dm4_rom[] = {
    00200, 07600,               /* 0200, CLA CLL */
    00201, 06603,               /* 0201, DMAR           ; read */
    00202, 06622,               /* 0202, DFSC           ; done? */
    00203, 05202,               /* 0203, JMP .-1        ; no */
    00204, 05600,               /* 0204, JMP @.-4       ; enter boot */
    07750, 07576,               /* 7750, 7576           ; word count */
    07751, 07576                /* 7751, 7576           ; address */
    };

t_stat rf_boot (int32 unitno, DEVICE *dptr)
{
size_t i;

if (rf_dib.dev != DEV_RF)                               /* only std devno */
    return STOP_NOTSTD;
if (sim_switches & SWMASK ('D')) {
    for (i = 0; i < DM4_LEN; i = i + 2)
        M[dm4_rom[i]] = dm4_rom[i + 1];
    cpu_set_bootpc (DM4_START);
    }
else {
    for (i = 0; i < OS8_LEN; i++)
        M[OS8_START + i] = os8_rom[i];
    cpu_set_bootpc (OS8_START);
    }
return SCPE_OK;
}

/* Attach routine */

t_stat rf_attach (UNIT *uptr, CONST char *cptr)
{
uint32 sz, p;
uint32 ds_bytes = RF_DKSIZE * sizeof (int16);

if ((uptr->flags & UNIT_AUTO) && (sz = sim_fsize_name (cptr))) {
    p = (sz + ds_bytes - 1) / ds_bytes;
    if (p >= RF_NUMDK)
        p = RF_NUMDK - 1;
    uptr->flags = (uptr->flags & ~UNIT_PLAT) |
        (p << UNIT_V_PLAT);
    }
uptr->capac = UNIT_GETP (uptr->flags) * RF_DKSIZE;
return attach_unit (uptr, cptr);
}

/* Change disk size */

t_stat rf_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (val < 0)
    return SCPE_IERR;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = UNIT_GETP (val) * RF_DKSIZE;
uptr->flags = uptr->flags & ~UNIT_AUTO;
return SCPE_OK;
}

const char *rf_description (DEVICE *dptr)
{
return "RF08 fixed head disk";
}
