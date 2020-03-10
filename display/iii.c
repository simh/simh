/* iii.c: Triple III display interface.

   Copyright (c) 2020, Richard Cornwell (based on help from Lars Brinkhoff)

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell

*/

#include "display.h"
#include "iii.h"

#if defined(__cplusplus)
extern "C" {
#endif

int iii_init(void *dev, int debug)
{
  return display_init(DIS_III, 1, dev);
}

void iii_point (int x, int y, int l)
{
  display_point(x + 512, y + 512, l, 0);
}

int iii_cycle(int us, int slowdown)
{
  return display_age(us, slowdown);
}

/* Draw a line between two points */
void
iii_draw_line(int x1, int y1, int x2, int y2, int l)
{
    int                 dx, ax;
    int                 dy;
    int                 i, j;
    int                 pu, pd, ws, et;
    int                 ipc, fpc;

    /* Origin us to 0 */
    x1 += 512;
    y1 += 512;
    x2 += 512;
    y2 += 512;

    /* Always draw top to bottom */
    if (y1 > y2) {
       int temp;
       temp = y1;
       y1 = y2;
       y2 = temp;
       temp = x1;
       x1 = x2;
       x2 = temp;
    }

    /* Figure out distances */
    dx = x2 - x1;
    dy = y2 - y1;
    /* Figure out whether we're going left or right */
    if (dx < 0) {
        ax = -1;
        dx = -dx;
    } else {
        ax = 1;
    }

    /* Vertical line */
    if (dx == 0) {
        for (i = 1; i < dy; i++) {
            display_point(x1, y1, l, 0);
            y1++;
        }
        return;
    }
    /* Horizontal line */
    if (dy == 0) {
        for (i = 1; i < dx; i++) {
            display_point(x1, y1, l, 0);
	    x1+=ax;
        }
        return;
    } 
    /* Diagnonal line */
    if (dx == dy) {
        for (i = 1; i < dx; i++) {
            display_point(x1, y1, l, 0);
            x1 += ax;
            y1 ++;
        }
        return;
    }
    /* Determine whether the line is X or Y major */
    if (dx >= dy) {
        /* X major line */
        ws = dx / dy;
        /* Adjust for each y by 1 step */
        pu = (dx % dy) * 2;
        /* Overrun error */
        pd = dy * 2;
        et = (dx % dy) - (dy * 2);

        ipc = (ws / 2) + 1;
        fpc = ipc;
        if ((pu == 0) && (ws & 1) == 0)
           ipc--;
        if ((ws & 1) != 0)
           et += dy;
        /* Draw run in x direction */
        for (j = 0; j < ipc; j++) {
             display_point(x1, y1, l, 0);
             x1 += ax;
        }
        y1++;
        /* Draw rest */
        for (i = 0; i< (dy-1); i++) {
            int rl = ws;
            if ((et += pu) > 0) {
                rl++;
                et -= pd;
            }
            for (j = 0; j < rl; j++) {
                 display_point(x1, y1, l, 0);
                 x1 += ax;
            }
            y1++;
        }
        for (j = 0; j < fpc; j++) {
             display_point(x1, y1, l, 0);
             x1 += ax;
        }
    } else {
        ws = dy / dx;
        pu = (dy % dx) * 2;
        pd = dx * 2;
        et = (dy % dx) - (dx * 2);
        ipc = (ws / 2) + 1;
        fpc = ipc;
        if ((pu == 0) && ((ws & 1) == 0))
           ipc--;
        if ((ws & 1) != 0)
           et += dx;
        
        /* Draw run in y direction */
        for (j = 0; j < ipc; j++) {
             display_point(x1, y1, l, 0);
             y1 ++;
        }
        x1 += ax;
        /* Draw rest */
        for (i = 0; i< (dx-1); i++) {
            int rl = ws;
            if ((et += pu) > 0) {
                rl++;
                et -= pd;
            }
            for (j = 0; j < rl; j++) {
                 display_point(x1, y1, l, 0);
                 y1 ++;
            }
            x1 += ax;
        }
        for (j = 0; j < fpc; j++) {
             display_point(x1, y1, l, 0);
             y1 ++;
        }
    }
}

#if defined(__cplusplus)
}
#endif
