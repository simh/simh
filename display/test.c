/*
 * $Id: test.c,v 1.22 2004/01/25 17:20:50 phil Exp - revised by DAG $
 * XY Display simulator test program (PDP-1 Munching Squares)
 * Phil Budne <phil@ultimate.com>
 * September 2003
 *
 * Updates from Douglas A. Gwyn, 12 Jan. 2004
 *
 * With thanks to Daniel Smith for his web page:
 * http://world.std.com/~dpbsmith/munch.html
 */

/*
 * Copyright (c) 2003-2004, Philip L. Budne
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the names of the authors shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization
 * from the authors.
 */

#ifndef TEST_DIS
#define TEST_DIS DIS_TYPE30
#endif

#ifndef TEST_RES
#define TEST_RES RES_HALF
#endif

#include <stdio.h>
#include <stdlib.h>

#ifndef EXIT_FAILURE
/* SunOS4 <stdlib.h> doesn't define this */
#define EXIT_FAILURE 1
#endif

#include "display.h"

static unsigned long test_switches = 0;

/* called from display code: */
unsigned long
cpu_get_switches(void) {
    return test_switches;
}

/* called from display code: */
void
cpu_set_switches(bits)
    unsigned long bits;
{
    printf("switches: %06lo\n", bits);
    test_switches = bits;
}

void
munch(void) {
    static long us = 0;
    static long io = 0, v = 0;
    long ac;
    int x, y;
    
    ac = test_switches;
    ac += v;                            /* add v */
    if (ac & ~0777777) {
      ac++;
      ac &= 0777777;
    }
    v = ac;                             /* dac v */

    ac <<= 9;                           /* rcl 9s */
    io <<= 9;
    io |= ac>>18;
    ac &= 0777777;
    ac |= io>>18;
    io &= 0777777;

    ac ^= v;                            /* xor v */

    /* convert +/-512 one's complement to 0..1022, origin in lower left */
    y = (io >> 8) & 01777;              /* hi 10 */
    if (y & 01000)
        y ^= 01000;
    else
        y += 511;

    x = (ac >> 8) & 01777;              /* hi 10 */
    if (x & 01000)                      /* negative */
        x ^= 01000;
    else
        x += 511;

    if (display_point(x, y, DISPLAY_INT_MAX, 0))
        printf("light pen hit at (%d,%d)\n", x, y);

/*#define US 100000                     /* 100ms (10/sec) */
/*#define US 50000                      /* 50ms (20/sec) */
/*#define US 20000                      /* 20ms (50/sec) */
/*#define US 10000                      /* 10ms (100/sec) */
#define US 0
    us += 50;                           /* 10 5us PDP-1 memory cycles */
    if (us >= US) {
      display_age(us, 1);
      us = 0;
    }
    display_sync();                     /* XXX push down */
}

#ifdef T2
/* display all window system level intensities;
 * must be compiled with -DINTENSITIES=<n> -DT2
 */
void
t2(void) {
    int x, y;

    display_init(TEST_DIS, TEST_RES, NULL);
    for (x = INTENSITIES-1; x >= 0; x--) {
        for (y = 0; y < 20; y++) {
            ws_display_point(x*4, y, x, 0);
            ws_display_point(x*4+1, y, x, 0);
            ws_display_point(x*4+2, y, x, 0);
            ws_display_point(x*4+3, y, x, 0);
        }
        display_sync();
    }
    fflush(stdout);
    for (;;)
        /* wait */ ;
}
#endif

#ifdef T3
/* display all "user" level intensities;
 * must be compiled with -DINTENSITIES=<n> -DT3
 *
 * skip every other virtual point on both axes
 * default scaling maps adjacent pixels and
 * causes re-intensification!
 */
void
t3(void) {
    int x, y;

    display_init(TEST_DIS, TEST_RES, NULL);
    for (x = DISPLAY_INT_MAX; x >= 0; x--) {
        for (y = 0; y < 20; y++) {
            display_point(x*2, y*2, x, 0);
        }
        display_sync();
    }
    fflush(stdout);
    for (;;)
        /* wait */ ;
}
#endif

int
main(void) {
    if (!display_init(TEST_DIS, TEST_RES, NULL))
        exit(EXIT_FAILURE);

    cpu_set_switches(04000UL);          /* classic starting value */
    for (;;) {
#ifdef T2
      t2();
#endif
#ifdef T3
      t3();
#endif
      munch();
    }
    /*NOTREACHED*/
}
