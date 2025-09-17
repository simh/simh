/* linc_crt.c: LINC CRT display

   Copyright (c) 2025, Lars Brinkhoff

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

#include "linc_defs.h"
#include "sim_video.h"
#include "display/display.h"

/* Function declaration. */
static t_stat crt_svc (UNIT *uptr);
static t_stat crt_reset (DEVICE *dptr);

static int crt_quit = FALSE;

/* Debug */
#define DBG             0001

static UNIT crt_unit = {
  UDATA (&crt_svc, UNIT_IDLE, 0)
};

static DEBTAB crt_deb[] = {
  { "DBG",  DBG },
  { "VVID", SIM_VID_DBG_VIDEO },
  { NULL, 0 }
};

#ifdef USE_DISPLAY
#define CRT_DIS  0
#else
#define CRT_DIS  DEV_DIS
#endif

DEVICE crt_dev = {
  "CRT", &crt_unit, NULL, NULL,
  1, 8, 12, 1, 8, 12,
  NULL, NULL, &crt_reset,
  NULL, NULL, NULL,
  NULL, DEV_DISABLE | DEV_DEBUG | DEV_DISPLAY, 0, crt_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static t_stat
crt_svc(UNIT *uptr)
{
#ifdef USE_DISPLAY
  display_age (100, 0);
  sim_activate_after (uptr, 100);
  if (crt_quit) {
    crt_quit = FALSE;
    return SCPE_STOP;
  }
#endif
  return SCPE_OK;
}

static void crt_quit_callback (void)
{
  crt_quit = TRUE;
}

static t_stat
crt_reset (DEVICE *dptr)
{
#ifdef USE_DISPLAY
  if ((dptr->flags & DEV_DIS) != 0 || (sim_switches & SWMASK('P')) != 0) {
    display_close (dptr);
    sim_cancel (&crt_unit);
  } else {
    display_reset ();
    display_init (DIS_LINC, 1, dptr);
    vid_register_quit_callback (&crt_quit_callback);
    sim_activate_abs (&crt_unit, 0);
  }
#endif
  return SCPE_OK;
}

void
crt_point (uint16 x, uint16 y)
{
  sim_debug(DBG, &crt_dev, "Point %o,%o\n", x, y);
#ifdef USE_DISPLAY
  if (crt_dev.flags & DEV_DIS)
    return;
  display_point(x, y, DISPLAY_INT_MAX, 0);
#endif
}

/* Hook called when CRT goes idle. */
void
crt_idle (void)
{
}

/* Display high voltage sync. */
void
crt_hvc (void)
{
}
