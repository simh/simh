/* imlac_crt.c: Imlac CRT display

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

#include "imlac_defs.h"
#include "sim_video.h"
#include "display/imlac.h"
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
  { "DBG", DBG },
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
  imlac_cycle (100, 0);
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
  if (dptr->flags & DEV_DIS || (sim_switches & SWMASK('P')) != 0) {
    display_close (dptr);
    sim_cancel (&crt_unit);
  } else {
    display_reset ();
    imlac_init (dptr, 1);
    sim_activate_abs (&crt_unit, 0);
    vid_register_quit_callback (&crt_quit_callback);
  }
#endif
  return SCPE_OK;
}

void
crt_point (uint16 x, uint16 y)
{
  sim_debug (DBG, &crt_dev, "Point %d,%d\n", x, y);
#ifdef USE_DISPLAY
  if (crt_dev.flags & DEV_DIS)
    return;
  imlac_point ((x & 03777) >> 1, (y & 03777) >> 1);
#endif
}

void
crt_line (uint16 x1, uint16 y1, uint16 x2, uint16 y2)
{
  sim_debug (DBG, &crt_dev, "Line %d,%d - %d,%d\n", x1, y1, x2, y2);
#ifdef USE_DISPLAY
  if (crt_dev.flags & DEV_DIS)
    return;
  imlac_line ((x1 & 03777) >> 1, (y1 & 03777) >> 1,
              (x2 & 03777) >> 1, (y2 & 03777) >> 1);
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
