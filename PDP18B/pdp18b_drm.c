/* pdp18b_drm.c: drum/fixed head disk simulator

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

   drm          (PDP-4,PDP-7) Type 24 serial drum

   03-Sep-13    RMS     Added explicit void * cast
   14-Jan-04    RMS     Revised IO device call interface
   26-Oct-03    RMS     Cleaned up buffer copy code
   05-Dec-02    RMS     Updated from Type 24 documentation
   22-Nov-02    RMS     Added PDP-4 support
   05-Feb-02    RMS     Added DIB, device number support
   03-Feb-02    RMS     Fixed bug in reset routine (Robert Alan Byer)
   06-Jan-02    RMS     Revised enable/disable support
   25-Nov-01    RMS     Revised interrupt structure
   10-Jun-01    RMS     Cleaned up IOT decoding to reflect hardware
   26-Apr-01    RMS     Added device enable/disable support
   14-Apr-99    RMS     Changed t_addr to unsigned
*/

#include "pdp18b_defs.h"
#include <math.h>

/* Constants */

#define DRM_NUMWDS      256                             /* words/sector */
#define DRM_NUMSC       2                               /* sectors/track */
#define DRM_NUMTR       256                             /* tracks/drum */
#define DRM_NUMDK       1                               /* drum/controller */
#define DRM_NUMWDT      (DRM_NUMWDS * DRM_NUMSC)        /* words/track */
#define DRM_SIZE        (DRM_NUMDK * DRM_NUMTR * DRM_NUMWDT) /* words/drum */
#define DRM_SMASK       ((DRM_NUMTR * DRM_NUMSC) - 1)   /* sector mask */

/* Parameters in the unit descriptor */

#define FUNC            u4                              /* function */
#define DRM_READ        000                             /* read */
#define DRM_WRITE       040                             /* write */

#define GET_POS(x)      ((int) fmod (sim_gtime() / ((double) (x)), \
                        ((double) DRM_NUMWDT)))

extern int32 M[];
extern int32 int_hwre[API_HLVL+1];
extern UNIT cpu_unit;

int32 drm_da = 0;                                       /* track address */
int32 drm_ma = 0;                                       /* memory address */
int32 drm_err = 0;                                      /* error flag */
int32 drm_wlk = 0;                                      /* write lock */
int32 drm_time = 10;                                    /* inter-word time */
int32 drm_stopioe = 1;                                  /* stop on error */

DEVICE drm_dev;
int32 drm60 (int32 dev, int32 pulse, int32 AC);
int32 drm61 (int32 dev, int32 pulse, int32 AC);
int32 drm62 (int32 dev, int32 pulse, int32 AC);
int32 drm_iors (void);
t_stat drm_svc (UNIT *uptr);
t_stat drm_reset (DEVICE *dptr);
t_stat drm_boot (int32 unitno, DEVICE *dptr);

/* DRM data structures

   drm_dev      DRM device descriptor
   drm_unit     DRM unit descriptor
   drm_reg      DRM register list
*/

DIB drm_dib = { DEV_DRM, 3 ,&drm_iors, { &drm60, &drm61, &drm62 } };

UNIT drm_unit = {
    UDATA (&drm_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF,
           DRM_SIZE)
    };

REG drm_reg[] = {
    { ORDATA (DA, drm_da, 9) },
    { ORDATA (MA, drm_ma, 16) },
    { FLDATA (INT, int_hwre[API_DRM], INT_V_DRM) },
    { FLDATA (DONE, int_hwre[API_DRM], INT_V_DRM) },
    { FLDATA (ERR, drm_err, 0) },
    { ORDATA (WLK, drm_wlk, 32) },
    { DRDATA (TIME, drm_time, 24), REG_NZ + PV_LEFT },
    { FLDATA (STOP_IOE, drm_stopioe, 0) },
    { ORDATA (DEVNO, drm_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB drm_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO", &set_devno, &show_devno },
    { 0 }
    };

DEVICE drm_dev = {
    "DRM", &drm_unit, drm_reg, drm_mod,
    1, 8, 20, 1, 8, 18,
    NULL, NULL, &drm_reset,
    &drm_boot, NULL, NULL,
    &drm_dib, DEV_DISABLE
    };

/* IOT routines */

int32 drm60 (int32 dev, int32 pulse, int32 AC)
{
if ((pulse & 027) == 06) {                              /* DRLR, DRLW */
    drm_ma = AC & 0177777;                              /* load mem addr */
    drm_unit.FUNC = pulse & DRM_WRITE;                  /* save function */
    }
return AC;
}

int32 drm61 (int32 dev, int32 pulse, int32 AC)
{
int32 t;

if (pulse & 001) {                                      /* DRSF */
    if (TST_INT (DRM))
        AC = AC | IOT_SKP;
    }
if (pulse & 002) {                                      /* DRCF */
    CLR_INT (DRM);                                      /* clear done */
    drm_err = 0;                                        /* clear error */
    }
if (pulse & 004) {                                      /* DRSS */
    drm_da = AC & DRM_SMASK;                            /* load sector # */
    t = ((drm_da % DRM_NUMSC) * DRM_NUMWDS) - GET_POS (drm_time);
    if (t <= 0)                                         /* wrap around? */
        t = t + DRM_NUMWDT;
    sim_activate (&drm_unit, t * drm_time);             /* schedule op */
    }
return AC;
}

int32 drm62 (int32 dev, int32 pulse, int32 AC)
{
int32 t;

if (pulse & 001) {                                      /* DRSN */
    if (drm_err == 0)
        AC = AC | IOT_SKP;
    }
if (pulse & 004) {                                      /* DRCS */
    CLR_INT (DRM);                                      /* clear done */
    drm_err = 0;                                        /* clear error */
    t = ((drm_da % DRM_NUMSC) * DRM_NUMWDS) - GET_POS (drm_time);
    if (t <= 0)                                         /* wrap around? */
        t = t + DRM_NUMWDT;
    sim_activate (&drm_unit, t * drm_time);             /* schedule op */
    }
return AC;
}

/* Unit service

   This code assumes the entire drum is buffered.
*/

t_stat drm_svc (UNIT *uptr)
{
int32 i;
uint32 da;
int32 *fbuf = (int32 *) uptr->filebuf;

if ((uptr->flags & UNIT_BUF) == 0) {                    /* not buf? abort */
    drm_err = 1;                                        /* set error */
    SET_INT (DRM);                                      /* set done */
    return IORETURN (drm_stopioe, SCPE_UNATT);
    }

da = drm_da * DRM_NUMWDS;                               /* compute dev addr */
for (i = 0; i < DRM_NUMWDS; i++, da++) {                /* do transfer */
    if (uptr->FUNC == DRM_READ) {                       /* read? */
        if (MEM_ADDR_OK (drm_ma))                       /* if !nxm */
            M[drm_ma] = fbuf[da];                       /* read word */
        }
    else {                                              /* write */
        if ((drm_wlk >> (drm_da >> 4)) & 1)
            drm_err = 1;
        else {                                          /* not locked */
            fbuf[da] = M[drm_ma];                       /* write word */
            if (da >= uptr->hwmark)
                uptr->hwmark = da + 1;
            }
        }
    drm_ma = (drm_ma + 1) & 0177777;                    /* incr mem addr */
    }
drm_da = (drm_da + 1) & DRM_SMASK;                      /* incr dev addr */
SET_INT (DRM);                                          /* set done */
return SCPE_OK;
}

/* Reset routine */

t_stat drm_reset (DEVICE *dptr)
{
drm_da = drm_ma = drm_err = 0;
CLR_INT (DRM);                                          /* clear done */
sim_cancel (&drm_unit);
return SCPE_OK;
}

/* IORS routine */

int32 drm_iors (void)
{
return (TST_INT (DRM)? IOS_DRM: 0);
}

/* Bootstrap routine */

#define BOOT_START 02000
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int))

static const int32 boot_rom[] = {
    0750000,                        /* CLA              ; dev, mem addr */
    0706006,                        /* DRLR             ; load ma */
    0706106,                        /* DRSS             ; load da, start */
    0706101,                        /* DRSF             ; wait for done */
    0602003,                        /* JMP .-1 */
    0600000                         /* JMP 0            ; enter boot */
	};

t_stat drm_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
extern int32 PC;

if (drm_dib.dev != DEV_DRM)                             /* non-std addr? */
    return STOP_NONSTD;
for (i = 0; i < BOOT_LEN; i++)
    M[BOOT_START + i] = boot_rom[i];
PC = BOOT_START;
return SCPE_OK;
}
