/* tt2500_crt.c: TT2500 CRT display

   Copyright (c) 2020, Lars Brinkhoff

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
   LARS BRINKHOFF BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Lars Brinkhoff shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Lars Brinkhoff.
*/

#include "tt2500_defs.h"
#include "sim_video.h"
#include "display/display.h"

/* Function declaration. */
static t_stat crt_svc (UNIT *uptr);
static t_stat crt_reset (DEVICE *dptr);

/* Debug */
#define DBG             0001

static UNIT crt_unit = {
  UDATA (&crt_svc, UNIT_IDLE, 0)
};

static DEBTAB crt_deb[] = {
  { "DBG", DBG },
  { "KEY", SIM_VID_DBG_KEY },
  { NULL, 0 }
};

#ifdef USE_DISPLAY
#define CRT_DIS  0
#else
#define CRT_DIS  DEV_DIS
#endif

DEVICE crt_dev = {
  "CRT", &crt_unit, NULL, NULL,
  1, 8, 16, 1, 8, 16,
  NULL, NULL, &crt_reset,
  NULL, NULL, NULL,
  NULL, DEV_DISABLE | DEV_DEBUG | CRT_DIS, 0, crt_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static t_stat
crt_svc(UNIT *uptr)
{
#ifdef USE_DISPLAY
  display_age (100, 0);
  if (!display_is_blank ())
    sim_activate_after (uptr, 100);
  if (dpy_quit) {
    dpy_quit = FALSE;
    return SCPE_STOP;
  }
#endif
  return SCPE_OK;
}

static t_stat
crt_reset (DEVICE *dptr)
{
#ifdef USE_DISPLAY
  if (dptr->flags & DEV_DIS || sim_switches & SWMASK('P')) {
    display_close (dptr);
    sim_cancel (&crt_unit);
  } else {
    display_reset ();
    display_init (DIS_TT2500, 1, dptr);
    vid_register_quit_callback (&dpy_quit_callback);
  }
#endif
  return SCPE_OK;
}

void
crt_line (uint16 x1, uint16 y1, uint16 x2, uint16 y2, uint16 i)
{
  sim_debug (DBG, &crt_dev, "Line %d,%d - %d,%d @ %d\n", x1, y1, x2, y2, i);
#ifdef USE_DISPLAY
  if ((crt_dev.flags & DEV_DIS) == 0 && !sim_is_active (&crt_unit))
    sim_activate_abs (&crt_unit, 0);
  if (crt_dev.flags & DEV_DIS)
    return;
  display_line (x1, y1, x2, y2, DISPLAY_INT_MAX*(7-i)/7);
#endif
}
