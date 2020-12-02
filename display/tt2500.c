/* tt2500.c: TT2500 display interface.

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
   in this Software without prior written authorization from Lars Brinkhoff

*/

#include "display.h"
#include "tt2500.h"

#if defined(__cplusplus)
extern "C" {
#endif

int tt2500_init(void *dev, int debug)
{
  return display_init (DIS_TT2500, 1, dev);
}

static void tt2500_point (int x, int y, int i)
{
  if (i < 7)
    display_point (x, y, DISPLAY_INT_MAX*(7-i)/7, 0);
}

int tt2500_cycle(int us, int slowdown)
{
  return display_age (us, slowdown);
}

#define ABS(_X) ((_X) >= 0 ? (_X) : -(_X))
#define SIGN(_X) ((_X) >= 0 ? 1 : -1)

static void
xline (int x, int y, int x2, int dx, int dy, int i)
{
  int ix = SIGN(dx);
  int iy = SIGN(dy);
  int ay;

  dx = ABS(dx);
  dy = ABS(dy);

  ay = dy/2;
  for (;;) {
    tt2500_point (x, y, i);
    if (x == x2)
      break;
    if (ay > 0) {
      y += iy;
      ay -= dx;
    }
    ay += dy;
    x += ix;
  }
}
  
static void
yline (int x, int y, int y2, int dx, int dy, int i)
{
  int ix = SIGN(dx);
  int iy = SIGN(dy);
  int ax;

  dx = ABS(dx);
  dy = ABS(dy);

  ax = dx/2;
  for (;;) {
    tt2500_point (x, y, i);
    if (y == y2)
      break;
    if (ax > 0) {
      x += ix;
      ax -= dy;
    }
    ax += dx;
    y += iy;
  }
}

void
tt2500_line (int x1, int y1, int x2, int y2, int i)
{
  int dx = x2 - x1;
  int dy = y2 - y1;
  if (ABS (dx) > ABS(dy))
    xline (x1, y1, x2, dx, dy, i);
  else
    yline (x1, y1, y2, dx, dy, i);
}

#if defined(__cplusplus)
}
#endif
