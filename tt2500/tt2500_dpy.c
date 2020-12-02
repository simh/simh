/* imlac_dpy.c: TT2500 display.

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

#include <string.h>
#include "tt2500_defs.h"

/* Debug */
#define DBG_REG         0001
#define DBG_VEC         0002
#define DBG_TXT         0004
#define DBG_60HZ        0010
#define DBG_2KHZ        0020

/* DSR */
#define DSR_VEC   0160000
#define DSR_TXT   0006000

static t_stat dpy_60hz_svc (UNIT *uptr);
static t_stat dpy_2khz_svc (UNIT *uptr);
static t_stat dpy_reset (DEVICE *dptr);
static void dpy_text_line (void);

static uint8 black[4096], green[4096];
uint8 FONT[4096];
uint8 LINE[73];
static uint16 YCOR;
static uint16 XCOR;
static uint16 SCROLL;
uint16 DSR = 0;
static uint16 ROW = 0;
static uint16 COL = 0;
int dpy_quit = FALSE;

/* DSR
160000 Vector beam: 0=on, 7=off.
 10000 TV on.
  6000 Color mode.
       0000
       2000 Dark background.
       4000
       6000

tv-off	   tv-off-const      1 001 (unused)
           tv-green-const    2 010 (unused)

tv-blank   green-blank-const 4 100 (flash)
tv-active  tv-dark-const     5 101 (normal) "green chars on field ebony"
           dark-blank-const  7 111 (unused)
*/

/* Function declaration. */
static uint16 dpy_read (uint16);
static void dpy_write (uint16, uint16);

static UNIT dpy_unit = {
  UDATA (dpy_2khz_svc, UNIT_IDLE, 0)
};

static BITFIELD dsr_bits[] = {
  BITNCF(10),
  BITF(TXT,2),
  BITF(ON,1),
  BITF(VEC,3),
  ENDBITS
};

static REG dpy_reg[] = {
  { ORDATAD (YCOR, YCOR, 9, "Y coordinate") },
  { ORDATAD (XCOR, XCOR, 9, "X coordinate") },
  { ORDATAD (SCROLL, SCROLL, 16, "Scroll") },
  { ORDATADF (DSR, DSR, 16, "Status register", dsr_bits ) },
  { NULL }
};

static DEBTAB dpy_deb[] = {
  { "REG", DBG_REG },
  { "VEC", DBG_VEC },
  { "TXT", DBG_TXT },
  { "60HZ", DBG_60HZ },
  { "2KHZ", DBG_2KHZ },
  { NULL, 0 }
};

static TTDEV dpy_ttdev = {
  { REG_YCOR, REG_XCOR, REG_SCROLL, REG_DSR },
  dpy_read,
  dpy_write,
};

DEVICE dpy_dev = {
  "DPY", &dpy_unit, dpy_reg, NULL,
  1, 8, 16, 1, 8, 16,
  NULL, NULL, dpy_reset,
  NULL, NULL, NULL, &dpy_ttdev, DEV_DEBUG, 0, dpy_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

/* To ensure the two clocks are always in sync, dpy_60hz_svc is called
   from dpy_2khz_svc. */

static t_stat dpy_60hz_svc (UNIT *uptr)
{
  sim_debug (DBG_60HZ, &dpy_dev, "60 Hz interrupt\n");
  flag_on (INT_60HZ);
  tv_refresh ();
  return SCPE_OK;
}

static t_stat dpy_2khz_svc (UNIT *uptr)
{
  static int n = 0;
  t_stat r;

  /* 30 text lines, plus one extra for the page refresh interrupt, per
     60 Hz page comes to 538 microseconds. */
  r = sim_activate_after (uptr, 538);
  if (r != SCPE_OK)
    return r;

  if (++n == 31) {
    n = 0;
    return dpy_60hz_svc (&dpy_unit);
  }

  sim_debug (DBG_2KHZ, &dpy_dev, "2 kHz interrupt\n");
  dpy_text_line ();
  flag_on (INT_2KHZ);
  return SCPE_OK;
}

static uint16 dpy_read (uint16 reg)
{
  uint16 data = 0;
  switch (reg) {
  case REG_YCOR:
    data = YCOR;
    sim_debug (DBG_REG, &dpy_dev, "%06o <= YCOR\n", data);
    break;
  case REG_XCOR:
    data = XCOR;
    sim_debug (DBG_REG, &dpy_dev, "%06o <= XCOR\n", data);
    break;
  case REG_SCROLL:
    data = SCROLL;
    sim_debug (DBG_REG, &dpy_dev, "%06o <= SCROLL\n", data);
    break;
  case REG_DSR:
    data = DSR;
    sim_debug (DBG_REG, &dpy_dev, "DSR <= %06o\n", data);
    break;
  }
  return data;
}

static void dpy_write (uint16 reg, uint16 data)
{
  switch (reg) {
  case REG_YCOR:
    sim_debug (DBG_REG, &dpy_dev, "YCOR <= %06o\n", data);
    YCOR = data;
    break;
  case REG_XCOR:
    sim_debug (DBG_REG, &dpy_dev, "XCOR <= %06o\n", data);
    XCOR = data;
    break;
  case REG_SCROLL:
    sim_debug (DBG_REG, &dpy_dev, "SCROLL <= %06o\n", data);
    SCROLL = data;
    flag_off (INT_60HZ);
    COL = 0;
    ROW = 29;
    break;
  case REG_DSR:
    sim_debug (DBG_REG, &dpy_dev, "DSR <= %06o\n", data);
    DSR = data;
    break;
  }
}

static t_stat dpy_reset (DEVICE *dptr)
{
  memset (black, 0, sizeof black);
  memset (green, 0377, sizeof green);
  sim_activate_abs (&dpy_unit, 0);
  return SCPE_OK;
}

void dpy_magic (uint16 xr, uint16 *r2, uint16 *r3, uint16 r4, uint16 r5)
{
  uint32 x = *r2, y = *r3;
  uint16 x0, y0, x1, y1, dx, dy;

  sim_debug (DBG_VEC, &dpy_dev, "MAGIC %06o\n", xr);
  sim_debug (DBG_VEC, &dpy_dev, "X,YCOR = %06o, %06o\n", XCOR, YCOR);
  sim_debug (DBG_VEC, &dpy_dev, "X,YPOS = %06o, %06o\n", *r2, *r3);
  sim_debug (DBG_VEC, &dpy_dev, "SIN,COS = %06o, %06o\n", r4, r5);

  x0 = x1 = XCOR;
  y0 = y1 = YCOR;
  dx = (r4 & 0100000) ? -1 : 1;
  dy = (r5 & 0100000) ? -1 : 1;
    
  flag_on (STAR_WRAP);
  while (xr & 04000) {
    sim_interval--;
    x = cpu_alu (0, ALU_ADD, x, r4);
    if (V)
      x1 = (XCOR += dx);

    sim_interval--;
    y = cpu_alu (0, ALU_ADD, y, r5);
    if (V)
      y1 = (YCOR += dy);

    if ((XCOR & 01000) != 0 || (YCOR & 01000) != 0) {
      x1 -= dx;
      y1 -= dy;
      flag_off (STAR_WRAP);
      break;
    }

    xr++;
  }
  crt_line (x0, y0, x1, y1, DSR >> 13);

  *r2 = x;
  *r3 = y;
}

void dpy_chartv (uint16 data)
{
  sim_debug (DBG_TXT, &dpy_dev, "CHARTV %03o (%06o)\n", data & 0377, data);
  flag_off (INT_2KHZ);
  memmove (LINE, LINE + 1, 72);
  LINE[72] = (uint8)data;
}

static void dpy_text_line (void)
{
  uint8 *font;

  if ((DSR & 016000) == 010000)
    font = green;
  else if (DSR & 010000)
    font = FONT;
  else
    font = black;

  tv_line (ROW, LINE, font);
  if (dpy_dev.dctrl)
    tv_refresh ();

  ROW++;
  if (ROW == 30)
    ROW = 0;
}

void dpy_quit_callback (void)
{
  dpy_quit = TRUE;
}
