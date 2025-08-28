#ifdef USE_DISPLAY
/* pdp8_dpy.c: Type 34 point-plotting display

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
   THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.
*/

#include "pdp8_defs.h"
#include "display/display.h"
#include "sim_video.h"

/* Run a Type 34 cycle every this many PDP-8 "cycle" times. */
#define DPY_DELAY 1

/* Memory cycle time. */
#define MEMORY_CYCLE 1

#define CYCLE_US (MEMORY_CYCLE*(DPY_DELAY*2+1))

t_stat dpy_svc(UNIT *uptr);
int32  dpy_iot (int32 IR, int32 AC);
t_stat dpy_reset(DEVICE *dptr);
t_stat dpy_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *dpy_description (DEVICE *dptr);

DIB dpy_dib = { DEV_DPY, 2, { &dpy_iot, &dpy_iot } };

UNIT dpy_unit = {
  UDATA (&dpy_svc, UNIT_IDLE, 0), 0
};

static t_bool dpy_quit = FALSE;
static uint16 dpy_x, dpy_y;

static void dpy_quit_callback (void)
{
  dpy_quit = TRUE;
}

DEVICE dpy_dev = {
    "DPY", &dpy_unit, NULL, NULL,
    1, 8, 16, 1, 8, 16,
    NULL, NULL, &dpy_reset,
    NULL, NULL, NULL,
    &dpy_dib, DEV_DIS | DEV_DISABLE, 0,
    NULL, NULL, NULL, NULL, NULL, NULL, 
    &dpy_description
};

t_stat
dpy_svc(UNIT *uptr)
{
#ifdef USE_DISPLAY
  display_age (100, 0);
  sim_activate_after (uptr, 100);
  if (dpy_quit) {
    dpy_quit = FALSE;
    return SCPE_STOP;
  }
#endif
  return SCPE_OK;
}

/* Type 34: IOT routine */

int32 dpy_iot (int32 IR, int32 AC)
{
  switch (IR & 071) {
  case 051:                                         /* DCX */
    dpy_x = 0;
    break;
  case 061:                                         /* DCY */
    dpy_y = 0;
    break;
  }

  switch (IR & 072) {
  case 052:                                         /* DXL */
    dpy_x |= AC & 01777;
    break;
  case 062:                                         /* DYL */
    dpy_y |= AC & 01777;
    break;
  }

  switch (IR & 004) {
  case 004:                                         /* DIX, DIY */
#ifdef USE_DISPLAY
    if (dpy_dev.flags & DEV_DIS)
      break;
    display_point (dpy_x, dpy_y, DISPLAY_INT_MAX, 0);
#endif
    break;
  }

  return AC;
}

t_stat
dpy_reset(DEVICE *dptr)
{
#ifdef USE_DISPLAY
  if (dptr->flags & DEV_DIS || (sim_switches & SWMASK('P')) != 0) {
    display_close (dptr);
    sim_cancel (&dpy_unit);
  } else {
    display_reset ();
    display_init (DIS_TYPE30, 1, dptr);
    vid_register_quit_callback (&dpy_quit_callback);
    sim_activate_abs (&dpy_unit, 0);
  }
#endif
  return SCPE_OK;
}

const char *dpy_description (DEVICE *dptr)
{
  return "Type 34 vector display controller";
}

void
cpu_get_switches(unsigned long *p1, unsigned long *p2)
{
}

void
cpu_set_switches(unsigned long w1, unsigned long w2)
{
}
    
#else  /* USE_DISPLAY not defined */
char pdp8_dpy_unused;   /* sometimes empty object modules cause problems */
#endif /* USE_DISPLAY not defined */
