/* pdp1_drm.c: drum/fixed head disk simulator

   Copyright (c) 1993-2002, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   drm		Type 24 serial drum

   05-Dec-02	RMS	Cloned from pdp18b_drm.c
*/

#include "pdp1_defs.h"
#include <math.h>

/* Constants */

#define DRM_NUMWDS	256				/* words/sector */
#define DRM_NUMSC	2				/* sectors/track */
#define DRM_NUMTR	256				/* tracks/drum */
#define DRM_NUMDK	1				/* drum/controller */
#define DRM_NUMWDT	(DRM_NUMWDS * DRM_NUMSC)	/* words/track */
#define DRM_SIZE	(DRM_NUMDK * DRM_NUMTR * DRM_NUMWDT) /* words/drum */
#define DRM_SMASK	((DRM_NUMTR * DRM_NUMSC) - 1)	/* sector mask */

/* Parameters in the unit descriptor */

#define FUNC		u4				/* function */
#define DRM_READ	000				/* read */
#define DRM_WRITE	010				/* write */

#define GET_POS(x)	((int) fmod (sim_gtime() / ((double) (x)), \
			((double) DRM_NUMWDT)))

extern int32 M[];
extern int32 iosta, sbs;
extern int32 stop_inst;
extern UNIT cpu_unit;

int32 drm_da = 0;					/* track address */
int32 drm_ma = 0;					/* memory address */
int32 drm_err = 0;					/* error flag */
int32 drm_wlk = 0;					/* write lock */
int32 drm_time = 10;					/* inter-word time */
int32 drm_stopioe = 1;					/* stop on error */

t_stat drm_svc (UNIT *uptr);
t_stat drm_reset (DEVICE *dptr);

/* DRM data structures

   drm_dev	DRM device descriptor
   drm_unit	DRM unit descriptor
   drm_reg	DRM register list
*/

UNIT drm_unit =
	{ UDATA (&drm_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF,
		DRM_SIZE) };

REG drm_reg[] = {
	{ ORDATA (DA, drm_da, 9) },
	{ ORDATA (MA, drm_ma, 16) },
	{ FLDATA (DONE, iosta, IOS_V_DRM) },
	{ FLDATA (ERR, drm_err, 0) },
	{ ORDATA (WLK, drm_wlk, 32) },
	{ DRDATA (TIME, drm_time, 24), REG_NZ + PV_LEFT },
	{ FLDATA (STOP_IOE, drm_stopioe, 0) },
	{ NULL }  };

DEVICE drm_dev = {
	"DRM", &drm_unit, drm_reg, NULL,
	1, 8, 20, 1, 8, 18,
	NULL, NULL, &drm_reset,
	NULL, NULL, NULL,
	NULL, DEV_DISABLE };

/* IOT routines */

int32 drm (int32 IR, int32 dev, int32 IO)
{
int32 t;
int32 pulse = (IR >> 6) & 037;

if (drm_dev.flags & DEV_DIS)				/* disabled? */
	return (stop_inst << IOT_V_REASON) | IO;	/* stop if requested */
if ((pulse != 001) & (pulse != 011))			/* invalid pulse? */
	return (stop_inst << IOT_V_REASON) | IO;	/* stop if requested */
switch (dev) {						/* switch on device */
case 061:						/* DWR, DRD */
	drm_ma = IO & 0177777;				/* load mem addr */
	drm_unit.FUNC = pulse & DRM_WRITE;		/* save function */
	break;
case 062:						/* DBL, DCN */
	if (pulse & 010) drm_da = IO & DRM_SMASK;	/* load sector # */
	iosta = iosta & ~IOS_DRM;			/* clear flags */
	drm_err = 0;
	t = ((drm_da % DRM_NUMSC) * DRM_NUMWDS) - GET_POS (drm_time);
	if (t <= 0) t = t + DRM_NUMWDT;			/* wrap around? */
	break;
case 063:						/* DTD */
	if (iosta & IOS_DRM) return (IO | IOT_SKP);	/* skip if done */
case 064:						/* DSE, DSP */
	if ((drm_err == 0) || (pulse & 010))		/* no error, par test? */
	    return (IO | IOT_SKP);
	}
return IO;
}

/* Unit service

   This code assumes the entire drum is buffered.
*/

t_stat drm_svc (UNIT *uptr)
{
int32 i;
uint32 da;

if ((uptr->flags & UNIT_BUF) == 0) {			/* not buf? abort */
	drm_err = 1;					/* set error */
	iosta = iosta | IOS_DRM;			/* set done */
	sbs = sbs | SB_RQ;				/* req intr */
	return IORETURN (drm_stopioe, SCPE_UNATT);  }

da = drm_da * DRM_NUMWDS;				/* compute dev addr */
for (i = 0; i < DRM_NUMWDS; i++, da++) {		/* do transfer */
	if (uptr->FUNC == DRM_READ) {
	    if (MEM_ADDR_OK (drm_ma))			/* read, check nxm */
		M[drm_ma] = *(((int32 *) uptr->filebuf) + da);  }
	else {
	    if ((drm_wlk >> (drm_da >> 4)) & 1) drm_err = 1;
	    else {
	    	*(((int32 *) uptr->filebuf) + da) = M[drm_ma];
		if (da >= uptr->hwmark) uptr->hwmark = da + 1;  }  }
	drm_ma = (drm_ma + 1) & 0177777;  }		/* incr mem addr */
drm_da = (drm_da + 1) & DRM_SMASK;			/* incr dev addr */
iosta = iosta | IOS_DRM;				/* set done */
sbs = sbs | SB_RQ;					/* req intr */
return SCPE_OK;
}

/* Reset routine */

t_stat drm_reset (DEVICE *dptr)
{
drm_da = drm_ma = drm_err = 0;
iosta = iosta & ~IOS_DRM;
sim_cancel (&drm_unit);
return SCPE_OK;
}
