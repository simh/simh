/* pdp8_df.c: DF32 fixed head disk simulator

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

   df           DF32 fixed head disk

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
   04-Oct-02    RMS     Added DIBs, device number support
   28-Nov-01    RMS     Added RL8A support
   25-Apr-01    RMS     Added device enable/disable support

   The DF32 is a head-per-track disk.  It uses the three cycle data break
   facility.  To minimize overhead, the entire DF32 is buffered in memory.

   Two timing parameters are provided:

   df_time      Interword timing, must be non-zero
   df_burst     Burst mode, if 0, DMA occurs cycle by cycle; otherwise,
                DMA occurs in a burst
*/

#include "pdp8_defs.h"
#include <math.h>

#define UNIT_V_AUTO     (UNIT_V_UF + 0)                 /* autosize */
#define UNIT_V_PLAT     (UNIT_V_UF + 1)                 /* #platters - 1 */
#define UNIT_M_PLAT     03
#define UNIT_PLAT       (UNIT_M_PLAT << UNIT_V_PLAT)
#define UNIT_GETP(x)    ((((x) >> UNIT_V_PLAT) & UNIT_M_PLAT) + 1)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define UNIT_PLAT       (UNIT_M_PLAT << UNIT_V_PLAT)

/* Constants */

#define DF_NUMWD        2048                            /* words/track */
#define DF_NUMTR        16                              /* tracks/disk */
#define DF_DKSIZE       (DF_NUMTR * DF_NUMWD)           /* words/disk */
#define DF_NUMDK        4                               /* disks/controller */
#define DF_WC           07750                           /* word count */
#define DF_MA           07751                           /* mem address */
#define DF_WMASK        (DF_NUMWD - 1)                  /* word mask */

/* Parameters in the unit descriptor */

#define FUNC            u4                              /* function */
#define DF_READ         2                               /* read */
#define DF_WRITE        4                               /* write */

/* Status register */

#define DFS_PCA         04000                           /* photocell status */
#define DFS_DEX         03700                           /* disk addr extension */
#define DFS_MEX         00070                           /* mem addr extension */
#define DFS_DRL         00004                           /* data late error */
#define DFS_WLS         00002                           /* write lock error */
#define DFS_NXD         00002                           /* non-existent disk */
#define DFS_PER         00001                           /* parity error */
#define DFS_ERR         (DFS_DRL | DFS_WLS | DFS_PER)
#define DFS_V_DEX       6
#define DFS_V_MEX       3

#define GET_MEX(x)      (((x) & DFS_MEX) << (12 - DFS_V_MEX))
#define GET_DEX(x)      (((x) & DFS_DEX) << (12 - DFS_V_DEX))
#define GET_POS(x)      ((int) fmod (sim_gtime() / ((double) (x)), \
                        ((double) DF_NUMWD)))
#define UPDATE_PCELL    if (GET_POS (df_time) < 6) df_sta = df_sta | DFS_PCA; \
                        else df_sta = df_sta & ~DFS_PCA

extern uint16 M[];
extern int32 int_req, stop_inst;
extern UNIT cpu_unit;

int32 df_sta = 0;                                       /* status register */
int32 df_da = 0;                                        /* disk address */
int32 df_done = 0;                                      /* done flag */
int32 df_wlk = 0;                                       /* write lock */
int32 df_time = 10;                                     /* inter-word time */
int32 df_burst = 1;                                     /* burst mode flag */
int32 df_stopioe = 1;                                   /* stop on error */

DEVICE df_dev;
int32 df60 (int32 IR, int32 AC);
int32 df61 (int32 IR, int32 AC);
int32 df62 (int32 IR, int32 AC);
t_stat df_svc (UNIT *uptr);
t_stat pcell_svc (UNIT *uptr);
t_stat df_reset (DEVICE *dptr);
t_stat df_boot (int32 unitno, DEVICE *dptr);
t_stat df_attach (UNIT *uptr, char *cptr);
t_stat df_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);

/* DF32 data structures

   df_dev       RF device descriptor
   df_unit      RF unit descriptor
   pcell_unit   photocell timing unit (orphan)
   df_reg       RF register list
*/

DIB df_dib = { DEV_DF, 3, { &df60, &df61, &df62 } };

UNIT df_unit = {
    UDATA (&df_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF,
		   DF_DKSIZE)
    };

REG df_reg[] = {
    { ORDATA (STA, df_sta, 12) },
    { ORDATA (DA, df_da, 12) },
    { ORDATA (WC, M[DF_WC], 12), REG_FIT },
    { ORDATA (MA, M[DF_MA], 12), REG_FIT },
    { FLDATA (DONE, df_done, 0) },
    { FLDATA (INT, int_req, INT_V_DF) },
    { ORDATA (WLS, df_wlk, 8) },
    { DRDATA (TIME, df_time, 24), REG_NZ + PV_LEFT },
    { FLDATA (BURST, df_burst, 0) },
    { FLDATA (STOP_IOE, df_stopioe, 0) },
    { DRDATA (CAPAC, df_unit.capac, 18), REG_HRO },
    { ORDATA (DEVNUM, df_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB df_mod[] = {
    { UNIT_PLAT, (0 << UNIT_V_PLAT), NULL, "1P", &df_set_size },
    { UNIT_PLAT, (1 << UNIT_V_PLAT), NULL, "2P", &df_set_size },
    { UNIT_PLAT, (2 << UNIT_V_PLAT), NULL, "3P", &df_set_size },
    { UNIT_PLAT, (3 << UNIT_V_PLAT), NULL, "4P", &df_set_size },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { 0 }
    };

DEVICE df_dev = {
    "DF", &df_unit, df_reg, df_mod,
    1, 8, 17, 1, 8, 12,
    NULL, NULL, &df_reset,
    &df_boot, &df_attach, NULL,
    &df_dib, DEV_DISABLE
    };

/* IOT routines */

int32 df60 (int32 IR, int32 AC)
{
int32 t;
int32 pulse = IR & 07;

UPDATE_PCELL;                                           /* update photocell */
if (pulse & 1) {                                        /* DCMA */
    df_da = 0;                                          /* clear disk addr */
    df_done = 0;                                        /* clear done */
    df_sta = df_sta & ~DFS_ERR;                         /* clear errors */
    int_req = int_req & ~INT_DF;                        /* clear int req */
    }
if (pulse & 6) {                                        /* DMAR, DMAW */
    df_da = df_da | AC;                                 /* disk addr |= AC */
    df_unit.FUNC = pulse & ~1;                          /* save function */
    t = (df_da & DF_WMASK) - GET_POS (df_time);         /* delta to new loc */
    if (t < 0)                                          /* wrap around? */
        t = t + DF_NUMWD;
    sim_activate (&df_unit, t * df_time);               /* schedule op */
    AC = 0;                                             /* clear AC */
    }
return AC;
}

/* Based on the hardware implementation.  DEAL and DEAC work as follows:

   6615         pulse 1 = clear df_sta<dex,mex>
                pulse 4 = df_sta = df_sta | AC<dex,mex>
                          AC = AC | old_df_sta
   6616         pulse 2 = clear AC, skip if address confirmed
                pulse 4 = df_sta = df_sta | AC<dex,mex> = 0 (nop)
                          AC = AC | old_df_sta
*/

int32 df61 (int32 IR, int32 AC)
{
int32 old_df_sta = df_sta;
int32 pulse = IR & 07;

UPDATE_PCELL;                                           /* update photocell */
if (pulse & 1)                                          /* DCEA */
    df_sta = df_sta & ~(DFS_DEX | DFS_MEX);             /* clear dex, mex */
if (pulse & 2)                                          /* DSAC */
    AC = ((df_da & DF_WMASK) == GET_POS (df_time))? IOT_SKP: 0;
if (pulse & 4) {
    df_sta = df_sta | (AC & (DFS_DEX | DFS_MEX));       /* DEAL */
    AC = AC | old_df_sta;                               /* DEAC */
    }
return AC;
}

int32 df62 (int32 IR, int32 AC)
{
int32 pulse = IR & 07;

UPDATE_PCELL;                                           /* update photocell */
if (pulse & 1) {                                        /* DFSE */
    if ((df_sta & DFS_ERR) == 0)
        AC = AC | IOT_SKP;
    }
if (pulse & 2) {                                        /* DFSC */
    if (pulse & 4)                                      /* for DMAC */
        AC = AC & ~07777;
    else if (df_done)
        AC = AC | IOT_SKP;
    }
if (pulse & 4)                                          /* DMAC */
    AC = AC | df_da;
return AC;
}

/* Unit service

   Note that for reads and writes, memory addresses wrap around in the
   current field.  This code assumes the entire disk is buffered.
*/

t_stat df_svc (UNIT *uptr)
{
int32 pa, t, mex;
uint32 da;
int16 *fbuf = (int16 *) uptr->filebuf;

UPDATE_PCELL;                                           /* update photocell */
if ((uptr->flags & UNIT_BUF) == 0) {                    /* not buf? abort */
    df_done = 1;
    int_req = int_req | INT_DF;                         /* update int req */
    return IORETURN (df_stopioe, SCPE_UNATT);
    }

mex = GET_MEX (df_sta);
da = GET_DEX (df_sta) | df_da;                          /* form disk addr */
do {
    if (da >= uptr->capac) {                            /* nx disk addr? */
        df_sta = df_sta | DFS_NXD;
        break;
		}
    M[DF_WC] = (M[DF_WC] + 1) & 07777;                  /* incr word count */
    M[DF_MA] = (M[DF_MA] + 1) & 07777;                  /* incr mem addr */
    pa = mex | M[DF_MA];                                /* add extension */
    if (uptr->FUNC == DF_READ) {                        /* read? */
        if (MEM_ADDR_OK (pa)) M[pa] = fbuf[da];         /* if !nxm, read wd */
        }
    else {                                              /* write */
        t = (da >> 14) & 07;                            /* check wr lock */
        if ((df_wlk >> t) & 1)                          /* locked? set err */
            df_sta = df_sta | DFS_WLS;
        else {                                          /* not locked */
            fbuf[da] = M[pa];							/* write word */
            if (da >= uptr->hwmark) uptr->hwmark = da + 1;
            }
        }
    da = (da + 1) & 0377777;                            /* incr disk addr */
    } while ((M[DF_WC] != 0) && (df_burst != 0));       /* brk if wc, no brst */

if ((M[DF_WC] != 0) && ((df_sta & DFS_ERR) == 0))       /* more to do? */
    sim_activate (&df_unit, df_time);                   /* sched next */
else {
    if (uptr->FUNC != DF_READ)
        da = (da - 1) & 0377777;
    df_done = 1;                                        /* done */
    int_req = int_req | INT_DF;                         /* update int req */
    }
df_sta = (df_sta & ~DFS_DEX) | ((da >> (12 - DFS_V_DEX)) & DFS_DEX);
df_da = da & 07777;                                     /* separate disk addr */
return SCPE_OK;
}

/* Reset routine */

t_stat df_reset (DEVICE *dptr)
{
df_sta = df_da = 0;
df_done = 1;
int_req = int_req & ~INT_DF;                            /* clear interrupt */
sim_cancel (&df_unit);
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

t_stat df_boot (int32 unitno, DEVICE *dptr)
{
size_t i;

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

t_stat df_attach (UNIT *uptr, char *cptr)
{
uint32 p, sz;
uint32 ds_bytes = DF_DKSIZE * sizeof (int16);

if ((uptr->flags & UNIT_AUTO) && (sz = sim_fsize_name (cptr))) {
    p = (sz + ds_bytes - 1) / ds_bytes;
    if (p >= DF_NUMDK)
        p = DF_NUMDK - 1;
    uptr->flags = (uptr->flags & ~UNIT_PLAT) |
         (p << UNIT_V_PLAT);
    }
uptr->capac = UNIT_GETP (uptr->flags) * DF_DKSIZE;
return attach_unit (uptr, cptr);
}

/* Change disk size */

t_stat df_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (val < 0)
    return SCPE_IERR;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = UNIT_GETP (val) * DF_DKSIZE;
uptr->flags = uptr->flags & ~UNIT_AUTO;
return SCPE_OK;
}
