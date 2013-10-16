/*
 * $Id: type340.c,v 1.6 2005/01/14 18:58:00 phil Exp $
 * Simulator Independent DEC Type 340 Graphic Display Processor Simulation
 * Phil Budne <phil@ultimate.com>
 * September 20, 2003
 * from vt11.c
 *
 * Information from DECUS 7-13
 * http://www.spies.com/~aek/pdf/dec/pdp7/7-13_340displayProgMan.pdf
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
 * Except as contained in this notice, the name of the author shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization
 * from the authors.
 */

#include "xy.h"                 /* XY plot interface */

/*
 * The Type 340 was used on the PDP-{4,6,7,9,10}
 * and used 18-bit words, with bits numbered 0 thru 17
 * (most significant to least)
 */

#define BITMASK(N) (1<<(17-(N)))

/* mask for a field */
#define FIELDMASK(START,END) ((1<<((END)-(START)+1))-1)

/* extract a field */
#define GETFIELD(W,START,END) (((W)>>(17-(END)))&FIELDMASK(START,END))

/* extract a 1-bit field */
#define TESTBIT(W,B) (((W) & BITMASK(B)) != 0)

#ifdef DEBUG_TY340
#define DEBUGF(X) printf X
#else
#define DEBUGF(X)
#endif

typedef long ty340word;

static ty340word DAC;                   /* Display Address Counter */
static unsigned char shift;             /* 1 bit */
static enum mode mode;                  /* 3 bits */
static int scale;                       /* 2 bits */

enum mode { PARAM=0, POINT, SLAVE, CHAR, VECTOR, VCONT, INCR, SUBR };

enum jump_type { DJP=2, DJS=3, DDS=1 };
static ty340word ASR;                   /* Address Save Register */
static unsigned char save_ff;           /* "save" flip-flop */

static unsigned char intensity;         /* 3 bits */
static unsigned char lp_ena;            /* 1 bit */

/* kept signed for raster violation checking */
static short xpos, ypos;                /* 10 bits, signed */       
static unsigned char sequence;          /* 2 bits */

/* XXX make defines public for 340_cycle return */
#define STOPPED 01
#define LPHIT 02
#define VEDGE 04
#define HEDGE 010
static unsigned char status = STOPPED;

/*
 * callbacks into PDP-6/10 simulator
 */
extern ty340word ty340_fetch(ty340word);
extern void ty340_store(ty340word, ty340word);
extern void ty340_stop_int(void);
extern void ty340_lp_int(void);

void
ty340_set_dac(ty340word addr)
{
    DAC = addr;
    mode = 0;
    DEBUGF(("set DAC %06\r\n", DAC));
    status = 0;                         /* XXX just clear stopped? */
    /* XXX clear other stuff? save_ff? */
}

void
ty340_reset(void)
{
    /* XXX call display layer? destroy window? */
    xpos = ypos = 0;
    status = STOPPED;
}

static int
point(int x, int y, int seq)
{
    int i;

    /* XXX apply scale? */

    i = DISPLAY_INT_MAX-7+intensity;
    if (i <= 0)
        i = 1;

    if (x < 0 || x > 1023) {
        status |= VEDGE;
        return 0;
    }
    if (y < 0 || y > 1023) {
        status |= HEDGE;
        return 0;
    }

    if (display_point(x, y, i, 0)) {
        if (lp_ena) {
            /* XXX save location? */
            status |= LPHIT;
            sequence = seq;
        }
    }
}

/*
 * two-step algorithm, developed by Xiaolin Wu
 * from http://graphics.lcs.mit.edu/~mcmillan/comp136/Lecture6/Lines.html
 */

/*
 * The two-step algorithm takes the interesting approach of treating
 * line drawing as a automaton, or finite state machine. If one looks
 * at the possible configurations for the next two pixels of a line,
 * it is easy to see that only a finite set of possibilities exist.
 * The two-step algorithm shown here also exploits the symmetry of
 * line-drawing by simultaneously drawn from both ends towards the
 * midpoint.
 */

static void
lineTwoStep(int x0, int y0, int x1, int y1)
{
    int dy = y1 - y0;
    int dx = x1 - x0;
    int stepx, stepy;

    if (dy < 0) { dy = -dy;  stepy = -1; } else { stepy = 1; }
    if (dx < 0) { dx = -dx;  stepx = -1; } else { stepx = 1; }

    lpoint(x0,y0);
    if (dx == 0 && dy == 0)             /* following algorithm won't work */
        return;                         /* just the one dot */
    lpoint(x1, y1);
    if (dx > dy) {
        int length = (dx - 1) >> 2;
        int extras = (dx - 1) & 3;
        int incr2 = (dy << 2) - (dx << 1);
        if (incr2 < 0) {
            int c = dy << 1;
            int incr1 = c << 1;
            int d =  incr1 - dx;
            int i;

            for (i = 0; i < length; i++) {
                x0 += stepx;
                x1 -= stepx;
                if (d < 0) {                            /* Pattern: */
                    lpoint(x0, y0);
                    lpoint(x0 += stepx, y0);            /*  x o o   */
                    lpoint(x1, y1);
                    lpoint(x1 -= stepx, y1);
                    d += incr1;
                }
                else {
                    if (d < c) {                          /* Pattern: */
                        lpoint(x0, y0);                   /*      o   */
                        lpoint(x0 += stepx, y0 += stepy); /*  x o     */
                        lpoint(x1, y1);
                        lpoint(x1 -= stepx, y1 -= stepy);
                    } else {
                        lpoint(x0, y0 += stepy);        /* Pattern: */
                        lpoint(x0 += stepx, y0);        /*    o o   */
                        lpoint(x1, y1 -= stepy);        /*  x       */
                        lpoint(x1 -= stepx, y1);
                    }
                    d += incr2;
                }
            }
            if (extras > 0) {
                if (d < 0) {
                    lpoint(x0 += stepx, y0);
                    if (extras > 1) lpoint(x0 += stepx, y0);
                    if (extras > 2) lpoint(x1 -= stepx, y1);
                } else
                    if (d < c) {
                        lpoint(x0 += stepx, y0);
                        if (extras > 1) lpoint(x0 += stepx, y0 += stepy);
                        if (extras > 2) lpoint(x1 -= stepx, y1);
                    } else {
                        lpoint(x0 += stepx, y0 += stepy);
                        if (extras > 1) lpoint(x0 += stepx, y0);
                        if (extras > 2) lpoint(x1 -= stepx, y1 -= stepy);
                    }
            }
        } else {
            int c = (dy - dx) << 1;
            int incr1 = c << 1;
            int d =  incr1 + dx;
            int i;
            for (i = 0; i < length; i++) {
                x0 += stepx;
                x1 -= stepx;
                if (d > 0) {
                    lpoint(x0, y0 += stepy);            /* Pattern: */
                    lpoint(x0 += stepx, y0 += stepy);   /*      o   */
                    lpoint(x1, y1 -= stepy);            /*    o     */
                    lpoint(x1 -= stepx, y1 -= stepy);   /*  x       */
                    d += incr1;
                } else {
                    if (d < c) {
                        lpoint(x0, y0);                   /* Pattern: */
                        lpoint(x0 += stepx, y0 += stepy); /*      o   */
                        lpoint(x1, y1);                   /*  x o     */
                        lpoint(x1 -= stepx, y1 -= stepy);
                    } else {
                        lpoint(x0, y0 += stepy);        /* Pattern: */
                        lpoint(x0 += stepx, y0);        /*    o o   */
                        lpoint(x1, y1 -= stepy);        /*  x       */
                        lpoint(x1 -= stepx, y1);
                    }
                    d += incr2;
                }
            }
            if (extras > 0) {
                if (d > 0) {
                    lpoint(x0 += stepx, y0 += stepy);
                    if (extras > 1) lpoint(x0 += stepx, y0 += stepy);
                    if (extras > 2) lpoint(x1 -= stepx, y1 -= stepy);
                } else if (d < c) {
                    lpoint(x0 += stepx, y0);
                    if (extras > 1) lpoint(x0 += stepx, y0 += stepy);
                    if (extras > 2) lpoint(x1 -= stepx, y1);
                } else {
                    lpoint(x0 += stepx, y0 += stepy);
                    if (extras > 1) lpoint(x0 += stepx, y0);
                    if (extras > 2) {
                        if (d > c)
                            lpoint(x1 -= stepx, y1 -= stepy);
                        else
                            lpoint(x1 -= stepx, y1);
                    }
                }
            }
        }
    } else {
        int length = (dy - 1) >> 2;
        int extras = (dy - 1) & 3;
        int incr2 = (dx << 2) - (dy << 1);
        if (incr2 < 0) {
            int c = dx << 1;
            int incr1 = c << 1;
            int d =  incr1 - dy;
            int i;
            for (i = 0; i < length; i++) {
                y0 += stepy;
                y1 -= stepy;
                if (d < 0) {
                    lpoint(x0, y0);
                    lpoint(x0, y0 += stepy);
                    lpoint(x1, y1);
                    lpoint(x1, y1 -= stepy);
                    d += incr1;
                } else {
                    if (d < c) {
                        lpoint(x0, y0);
                        lpoint(x0 += stepx, y0 += stepy);
                        lpoint(x1, y1);
                        lpoint(x1 -= stepx, y1 -= stepy);
                    } else {
                        lpoint(x0 += stepx, y0);
                        lpoint(x0, y0 += stepy);
                        lpoint(x1 -= stepx, y1);
                        lpoint(x1, y1 -= stepy);
                    }
                    d += incr2;
                }
            }
            if (extras > 0) {
                if (d < 0) {
                    lpoint(x0, y0 += stepy);
                    if (extras > 1) lpoint(x0, y0 += stepy);
                    if (extras > 2) lpoint(x1, y1 -= stepy);
                } else
                    if (d < c) {
                        lpoint(x0, y0 += stepy);
                        if (extras > 1) lpoint(x0 += stepx, y0 += stepy);
                        if (extras > 2) lpoint(x1, y1 -= stepy);
                    } else {
                        lpoint(x0 += stepx, y0 += stepy);
                        if (extras > 1) lpoint(x0, y0 += stepy);
                        if (extras > 2) lpoint(x1 -= stepx, y1 -= stepy);
                    }
            }
        } else {
            int c = (dx - dy) << 1;
            int incr1 = c << 1;
            int d =  incr1 + dy;
            int i;
            for (i = 0; i < length; i++) {
                y0 += stepy;
                y1 -= stepy;
                if (d > 0) {
                    lpoint(x0 += stepx, y0);
                    lpoint(x0 += stepx, y0 += stepy);
                    lpoint(x1 -= stepy, y1);
                    lpoint(x1 -= stepx, y1 -= stepy);
                    d += incr1;
                } else {
                    if (d < c) {
                        lpoint(x0, y0);
                        lpoint(x0 += stepx, y0 += stepy);
                        lpoint(x1, y1);
                        lpoint(x1 -= stepx, y1 -= stepy);
                    } else {
                        lpoint(x0 += stepx, y0);
                        lpoint(x0, y0 += stepy);
                        lpoint(x1 -= stepx, y1);
                        lpoint(x1, y1 -= stepy);
                    }
                    d += incr2;
                }
            }
            if (extras > 0) {
                if (d > 0) {
                    lpoint(x0 += stepx, y0 += stepy);
                    if (extras > 1) lpoint(x0 += stepx, y0 += stepy);
                    if (extras > 2) lpoint(x1 -= stepx, y1 -= stepy);
                } else if (d < c) {
                    lpoint(x0, y0 += stepy);
                    if (extras > 1) lpoint(x0 += stepx, y0 += stepy);
                    if (extras > 2) lpoint(x1, y1 -= stepy);
                } else {
                    lpoint(x0 += stepx, y0 += stepy);
                    if (extras > 1) lpoint(x0, y0 += stepy);
                    if (extras > 2) {
                        if (d > c)
                            lpoint(x1 -= stepx, y1 -= stepy);
                        else
                            lpoint(x1, y1 -= stepy);
                    }
                }
            }
        }
    }
} /* lineTwoStep */

static int
vector(int i, int sx, int dx, int sy, int dy)
{
    int x0, y0, x1, y1;

    x0 = xpos;
    y0 = ypos;

    if (sx) {
        x1 = x0 - dx;
        if (x1 < 0)                     /* XXX TEMP? */
            x1 = 0;
    }
    else {
        x1 = x0 + dx;
        if (x1 > 1023)                  /* XXX TEMP? */
            x1 = 1023;
    }

    if (sy) {
        y1 = y0 - dy;
        if (y1 < 0)                     /* XXX TEMP? */
            y1 = 0;
    }
    else {
        y1 = y0 + dy;                   /* XXX TEMP? */
        if (y1 > 1023)
            y1 = 1023;
    }

    DEBUGF(("vector i%d (%d,%d) to (%d,%d)\r\n", i, x0, y0, x1, y1));
    if (i)
        lineTwoStep(x0, y0, x1, y1);

    xpos = x1;
    ypos = y1;
    return 0;
}

/* return true on raster violation */
int
ipoint(int i, int n, unsigned char byte)
{
    if (byte & 010) {                   /* left/right */
        if (byte & 04) {
            if (xpos == 0) {
                status |= VEDGE;
                return 1;
            }
            xpos--;
        }
        else {
            if (xpos == 1023) {
                status |= VEDGE;
                return 1;
            }
            xpos++;
        }
    }
    if (byte & 02) {                    /* up/down */
        if (byte & 04) {
            if (ypos == 0) {
                status |= HEDGE;
                return 1;
            }
            ypos--;
        }
        else {
            if (ypos == 1023) {
                status |= HEDGE;
                return 1;
            }
            ypos++;
        }
    }
    if (i)
        point(xpos, ypos, n);

    return 0;
}

/* 
 * 342 character generator - first 64 characters (from manual)
 */
static const unsigned char chars[64][5] = {
    { 0070, 0124, 0154, 0124, 0070 },   /* 00 */
    { 0174, 0240, 0240, 0240, 0174 },   /* 01 A */
    { 0376, 0222, 0222, 0222, 0154 },   /* 02 B */
    { 0174, 0202, 0202, 0202, 0104 },   /* 03 C */
    { 0376, 0202, 0202, 0202, 0174 },   /* 04 D */
    { 0376, 0222, 0222, 0222, 0222 },   /* 05 E */
    { 0376, 0220, 0220, 0220, 0220 },   /* 06 F */
    { 0174, 0202, 0222, 0222, 0134 },   /* 07 G */
    { 0376, 0020, 0020, 0020, 0376 },   /* 10 H */
    { 0000, 0202, 0376, 0202, 0000 },   /* 11 I */
    { 0004, 0002, 0002, 0002, 0374 },   /* 12 J */
    { 0376, 0020, 0050, 0104, 0202 },   /* 13 K */
    { 0376, 0002, 0002, 0002, 0002 },   /* 14 K */
    { 0374, 0100, 0040, 0100, 0374 },   /* 15 M */
    { 0376, 0100, 0040, 0020, 0376 },   /* 16 N */
    { 0174, 0202, 0202, 0202, 0174 },   /* 17 O */
    { 0376, 0220, 0220, 0220, 0140 },   /* 20 P */
    { 0174, 0202, 0212, 0206, 0176 },   /* 21 Q */
    { 0376, 0220, 0230, 0224, 0142 },   /* 22 R */
    { 0144, 0222, 0222, 0222, 0114 },   /* 23 S */
    { 0200, 0200, 0376, 0200, 0200 },   /* 24 T */
    { 0374, 0002, 0002, 0002, 0374 },   /* 25 U */
    { 0370, 0004, 0002, 0004, 0370 },   /* 26 V */
    { 0376, 0004, 0010, 0004, 0376 },   /* 27 W */
    { 0202, 0104, 0070, 0104, 0202 },   /* 30 X */
    { 0200, 0100, 0076, 0100, 0200 },   /* 31 Y */
    { 0226, 0232, 0222, 0262, 0322 },   /* 32 Z */
    { 0000, 0000, 0000, 0000, 0000 },   /* 33 LF */
    { 0000, 0000, 0000, 0000, 0000 },   /* 34 CR */
    { 0000, 0000, 0000, 0000, 0000 },   /* 35 HORIZ */
    { 0000, 0000, 0000, 0000, 0000 },   /* 36 VERT */
    { 0000, 0000, 0000, 0000, 0000 },   /* 37 ESC */
    { 0000, 0000, 0000, 0000, 0000 },   /* 40 space */
    { 0000, 0000, 0372, 0000, 0000 },   /* 41 ! */
    { 0000, 0340, 0000, 0340, 0000 },   /* 42 " */
    { 0050, 0376, 0050, 0376, 0050 },   /* 43 # */
    { 0144, 0222, 0376, 0222, 0114 },   /* 44 $ */
    { 0306, 0310, 0220, 0246, 0306 },   /* 45 % */
    { 0154, 0222, 0156, 0004, 0012 },   /* 46 & */
    { 0000, 0000, 0300, 0340, 0000 },   /* 47 ' */
    { 0070, 0104, 0202, 0000, 0000 },   /* 50 ( */
    { 0000, 0000, 0202, 0104, 0070 },   /* 51 ) */
    { 0124, 0070, 0174, 0070, 0124 },   /* 52 * */
    { 0020, 0020, 0174, 0020, 0020 },   /* 53 + */
    { 0000, 0014, 0016, 0000, 0000 },   /* 54 , */
    { 0020, 0020, 0020, 0020, 0020 },   /* 55 - */
    { 0000, 0006, 0006, 0000, 0000 },   /* 56 . */
    { 0004, 0010, 0020, 0040, 0100 },   /* 57 / */
    { 0174, 0212, 0222, 0242, 0174 },   /* 60 0 */
    { 0000, 0102, 0376, 0002, 0000 },   /* 61 1 */
    { 0116, 0222, 0222, 0222, 0142 },   /* 62 2 */
    { 0104, 0202, 0222, 0222, 0154 },   /* 63 3 */
    { 0020, 0060, 0120, 0376, 0020 },   /* 64 4 */
    { 0344, 0222, 0222, 0222, 0214 },   /* 65 5 */
    { 0174, 0222, 0222, 0222, 0114 },   /* 66 6 */
    { 0306, 0210, 0220, 0240, 0300 },   /* 67 7 */
    { 0154, 0222, 0222, 0222, 0154 },   /* 70 8 */
    { 0144, 0222, 0222, 0222, 0174 },   /* 71 9 */
    { 0000, 0066, 0066, 0000, 0000 },   /* 72 : */
    { 0000, 0154, 0156, 0000, 0000 },   /* 73 ; */
    { 0020, 0050, 0104, 0202, 0000 },   /* 74 < */
    { 0050, 0050, 0050, 0050, 0050 },   /* 75 = */
    { 0000, 0202, 0104, 0050, 0020 },   /* 76 > */
    { 0100, 0200, 0236, 0220, 0140 }    /* 77 ? */
};

/*
 * type 342 Character/Symbol generator for type 340 display
 * return true if ESCaped
 */
static int
character(int n, char c)
{
    int x, y;

    switch (c) {
    case 033:                           /* LF */
        if (ypos < 12) {
            status |= HEDGE;
            ypos = 0;
        }
        else
            ypos -= 12;                 /* XXX scale? */

        return 0;
    case 034:                           /* CR */
        xpos = 0;
        return 0;
    case 035:                           /* shift in */
        shift = 1;
        return 0;
    case 036:                           /* shift out */
        shift = 0;
        return 0;
    case 037:                           /* escape */
        sequence = n;
        return 1;
    }
    /* XXX plot character from character set selected by "shift"
     * (offset index by 64?)
     */
    for (x = 0; x < 5; x++) {
        for (y = 0; y < 7; y++) {
            if (chars[c][x] & (1<<y)) {
                /* XXX check for raster violation? */
                point(xpos+x, ypos+y, n); /* XXX scale? */
            }
        }
    }
    xpos += 7;                          /* XXX scale? */
    if (xpos > 1023) {
        xpos = 1023;
        status |= VEDGE;
    }
    return 0;
}

int
ty340_cycle(int us, int slowdown)
{
    ty340word inst, addr;
    int i, escape, stopped;

    if (status & STOPPED)
        return 0;                       /* XXX age display? */

    inst = ty340_fetch(DAC);
    DEBUGF(("%06o: %06o\r\n", DAC, inst));
    DAC++;

    escape = 0;
    switch (mode) {
    case PARAM:
        mode = GETFIELD(inst, 2, 4);

        if (TESTBIT(inst, 5)) {         /* load l.p. enable */
            lp_ena = TESTBIT(inst,6);
            DEBUGF(("lp_ena %d\r\n", lp_ena));
        }

        if (TESTBIT(inst, 7)) {
            status |= STOPPED;
            if (TESTBIT(inst, 8))
                ty340_stop_int();       /* set stop_int_end? */
        }

        if (TESTBIT(inst, 11))
            scale = GETFIELD(inst, 12, 13);

        if (TESTBIT(inst, 14))
            intensity = GETFIELD(inst, 15, 17);

        break;

    case POINT:
        mode = GETFIELD(inst, 2, 4);

        if (TESTBIT(inst, 5))           /* load l.p. enable */
            lp_ena = TESTBIT(inst,6);

        if (TESTBIT(inst, 1))
            ypos = GETFIELD(inst, 8, 17);
        else
            xpos = GETFIELD(inst, 8, 17);

        if (TESTBIT(inst, 7))
            point(xpos, ypos, 0);
        break;

    case SLAVE:
        mode = GETFIELD(inst, 2, 4);
        break;

    case CHAR:
        escape = (character(0, GETFIELD(inst, 0, 5)) ||
                  character(1, GETFIELD(inst, 6, 11)) ||
                  character(2, GETFIELD(inst, 12, 17)));
        break;

    case VECTOR:
        escape = TESTBIT(inst, 0);
        if (vector(TESTBIT(inst, 1),
                   TESTBIT(inst, 2), GETFIELD(inst, 3, 9),
                   TESTBIT(inst, 10), GETFIELD(inst, 11, 17))) {
            /* XXX interrupt? */
        }
        break;
    case VCONT:
        escape = TESTBIT(inst, 0);
        if (vector(TESTBIT(inst, 1),
                   TESTBIT(inst, 2), GETFIELD(inst, 3, 9),
                   TESTBIT(inst, 10), GETFIELD(inst, 11, 17))) {
            /* XXX set escape? */
            mode = PARAM;               /* raster violation */
        }
        break;

    case INCR:
        escape = TESTBIT(inst, 0);      /* escape bit */
        i = TESTBIT(inst, 1);

        if (ipoint(i, 0, GETFIELD(inst, 2, 5)) ||
            ipoint(i, 1, GETFIELD(inst, 6, 9)) ||
            ipoint(i, 2, GETFIELD(inst, 10, 13)) ||
            ipoint(i, 3, GETFIELD(inst, 14, 17)))
            /* XXX set escape? */
            mode = PARAM;               /* raster violation */
        break;

    case SUBR:
        /* type 347 Display Subroutine Option? */

        mode = GETFIELD(inst, 2, 4);
        /* XXX take high bits of current DAC? */
        addr = GETFIELD(inst, 5, 17);

        switch (GETFIELD(inst, 0, 1)) {
        case DJS:                       /* display jump and save */
            ASR = DAC;
            save_ff = 1;                /* set "save" flip-flop */
            /* FALL */
        case DJP:                       /* display jump */
            DAC = addr;
            break;
        case DDS:                       /* display deposit save register */
            ty340_deposit(addr, (DJP<<16) | ASR);
            save_ff = 0;                /* ?? */
            break;
        default:
            /* XXX ??? */
            break;
        }
        break;
    }

    if (escape) {
        mode = PARAM;
        if (save_ff) {
            /* return from subroutine */
            DAC = ASR;
            save_ff = 0;
        }
    }
    return status;
} /* ty340_cycle */
