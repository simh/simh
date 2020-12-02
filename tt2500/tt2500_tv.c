/* tt2500_tv.c: TT2500 TV text display

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
#include "display/tt2500.h"
#include "display/display.h"

/* Function declaration. */
static t_stat tv_svc (UNIT *uptr);
static t_stat tv_reset (DEVICE *dptr);

static uint32 surface[8 * 16 * 72];
static uint32 palette[2];
static VID_DISPLAY *vptr = NULL;

/* Debug */
#define DBG             0001

static UNIT tv_unit = {
  UDATA (&tv_svc, UNIT_IDLE, 0)
};

static DEBTAB tv_deb[] = {
  { "DBG", DBG },
  { "KEY", SIM_VID_DBG_KEY },
  { NULL, 0 }
};

#if defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)
#define TV_DIS  0
#else
#define TV_DIS  DEV_DIS
#endif

DEVICE tv_dev = {
  "TV", &tv_unit, NULL, NULL,
  1, 8, 16, 1, 8, 16,
  NULL, NULL, &tv_reset,
  NULL, NULL, NULL,
  NULL, DEV_DISABLE | DEV_DEBUG | TV_DIS, 0, tv_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static t_stat
tv_svc(UNIT *uptr)
{
  SIM_KEY_EVENT ev;

  sim_activate_after (uptr, 10000);

  if (dpy_quit) {
    dpy_quit = FALSE;
    return SCPE_STOP;
  }

  if (vid_poll_kb (&ev) == SCPE_OK) {
#ifdef USE_DISPLAY
    if (vid_display_kb_event_process != NULL)
      vid_display_kb_event_process (&ev);
#endif
  }
  return SCPE_OK;
}

static t_stat
tv_reset (DEVICE *dptr)
{
  t_stat r;
  if (dptr->flags & DEV_DIS || sim_switches & SWMASK('P')) {
    sim_cancel (&tv_unit);
    if (vptr != NULL)
      vid_close_window (vptr);
    vptr = NULL;
  } else if (vptr == NULL) {
    r = vid_open_window (&vptr, dptr, "Text display", 576, 480, 0);
    if (r != SCPE_OK)
      return r;
    sim_activate_abs (&tv_unit, 0);
    vid_register_quit_callback (&dpy_quit_callback);
    palette[0] = vid_map_rgb_window (vptr, 0x00, 0x00, 0x00);
    palette[1] = vid_map_rgb_window (vptr, 0x00, 0xFF, 0x30);
  }
  return SCPE_OK;
}

static void tv_character (int row, int col, uint8 c, uint8 *font)
{
  uint16 i, j, pixels, address;

  address = 16 * c;
  for (i = 0; i < 16; i++) {
    pixels = font[address + i];
    for (j = 0; j < 8; j++) {
      surface[8 * (72 * i + col) + j] = palette[(pixels >> 7) & 1];
      pixels <<= 1;
    }
  }
}

void tv_line (int row, uint8 *line, uint8 *font)
{
  int col;

  line[72] = 0;
  sim_debug (DBG, &tv_dev, "Text row %d: %s\n", row, (char *)line);

  if (tv_dev.flags & DEV_DIS)
    return;

  for (col = 0; col < 72; col++)
    tv_character (row, col, *line++, font);
  vid_draw_window (vptr, 0, 16 * row, 8 * 72, 16, surface);
}

void tv_refresh (void)
{
  if (tv_dev.flags & DEV_DIS)
    return;
  sim_debug (DBG, &tv_dev, "Refresh screen.\n");
  vid_refresh_window (vptr);
}
