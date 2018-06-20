/*
 * wrap, or clip?
 * skip vector points when scaling?
 * are scaled character sizes right??
 */

/*
 * $Id: type340.c,v 1.6 2005/01/14 18:58:00 phil Exp $
 * Simulator Independent DEC Type 340 Graphic Display Processor Simulation
 * Phil Budne <phil@ultimate.com>
 * September 20, 2003
 * from vt11.c
 *
 * First displayed PDP-6/10 SPCWAR Feb 2018!
 *
 * The Type 340 was used on the PDP-{4,6,7,9,10}
 * and used 18-bit words, with bits numbered 0 thru 17
 * (most significant to least)
 *
 * This file simulates ONLY the 340 proper
 * and not CPU specific interfacing details
 *
 * see:
 * http://www.bitsavers.org/pdf/dec/graphics/H-340_Type_340_Precision_Incremental_CRT_System_Nov64.pdf
 *
 * Initial information from DECUS 7-13:
 * http://www.bitsavers.org/pdf/dec/graphics/7-13_340_Display_Programming_Manual.pdf
 * pre-bitsavers location(!!):
 * http://www.spies.com/~aek/pdf/dec/pdp7/7-13_340displayProgMan.pdf
 *
 * NOTE!!! The 340 is an async processor, with multiple control signals
 * running in parallel.  No attempt has been made to simulate this.
 * And while it might be fun to try to implement it as a bit vector
 * of signals, and run code triggered by those signals in the next
 * service interval, BUT unless/until this is proven necessary, I'm
 * resisting that impulse (pun not intended).
 */

/*
 * Copyright (c) 2003-2018, Philip L. Budne
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

#include "display.h"                 /* XY plot interface */
#include "type340.h"                 /* interface definitions */
#include "type340cmd.h"              /* 340 command definitions */

/*
 * sub-options, under "#if"
 * (make runtime selectable????!)
 * TYPE342      character generator
 * TYPE343      slave display control
 * TYPE347      subroutine facility
 */

#ifndef TYPE342
#define TYPE342 1                       /* default to character generation */
#endif

#define BITMASK(N) (1<<(17-(N)))

/* mask for a field */
#define FIELDMASK(START,END) ((1<<((END)-(START)+1))-1)

/* extract a field */
#define GETFIELD(W,START,END) (((W)>>(17-(END)))&FIELDMASK(START,END))

/* extract a 1-bit field */
#define TESTBIT(W,B) (((W) & BITMASK(B)) != 0)

#ifdef DEBUG_TY340
#include <stdio.h>
#define DEBUGF(X) printf X
#else
#define DEBUGF(X)
#endif
enum mode { PARAM=0, POINT, SLAVE, CHAR, VECTOR, VCONT, INCR, SUBR };

#define TY340_UNITS 1

enum jump_type { DJP=2, DJS=3, DDS=1 }; /* type 347 */

/* put all the state in a struct "just in case" */
static struct type340 {
/*  ty340word DAC;              /* Display Address Counter */
    ty340word status;           /* see ST340_XXX in type340.h */
    signed short xpos, ypos;    /* 10 bits, signed (for OOB checks) */
    char initialized;           /* 0 before display_init */
    /* only using (evil) bitfield syntax to limit enum size */
    enum mode mode : 8;         /* 3 bits */
    unsigned char lp_ena;       /* 1 bit */
    unsigned char scale;        /* multiplier: 1,2,4,8 */
    unsigned char intensity;    /* 3 bits */
#if TYPE342
    unsigned char shift;        /* 1 bit */
#endif
#if TYPE347
    ty340word ASR;              /* Address Save Register */
    unsigned char SAVE_FF;      /* "save" flip-flop */
#endif
} u340[TY340_UNITS];

#if TY340_UNITS == 1
#define UNIT(N) u340
#else
#define UNIT(N) (u340+(N))
#endif

#if 0
/* NOT USED WITH PDP-6 Type 344 Interface!! */
void
ty340_set_dac(ty340word addr)
{
    struct type340 *u = UNIT(0);
    u->DAC = addr;
    DEBUGF(("set DAC %06o\r\n", u->DAC));

    /* XXX only when reset? */
    u->mode = 0;
    u->status = 0;               /* XXX just clear stopped? */
    ty340_rfd();                 /* ready for data */
}
#endif

ty340word
ty340_reset(void)
{
    struct type340 *u = UNIT(0);
#ifndef TY340_NODISPLAY
    if (!u->initialized) {
        display_init(DIS_TYPE340, 1, u); /* XXX check return? */
        u->initialized = 1;
    }
#endif
    u->xpos = u->ypos = 0;
    u->mode = 0;
    u->status = 0;
    u->scale = 1;
#if TYPE342
    u->shift = 0;
#endif
#if TYPE347
    u->SAVE_FF = 0;
#endif
    ty340_rfd();                /* ready for data */
    return u->status;
}

static int
point(int x, int y, int seq)
{
    struct type340 *u = UNIT(0);
    int i;

#ifdef TYPE340_POINT
    DEBUGF(("type340 point %d %d %d\r\n", x, y, seq));
#endif

    i = DISPLAY_INT_MAX-7+u->intensity;
    if (i <= 0)
        i = 1;

    if (x < 0 || x > 1023) {
        /* XXX clip? wrap?? */
        u->status |= ST340_VEDGE;
        return 0;
    }
    if (y < 0 || y > 1023) {
        /* XXX clip? wrap?? */
        u->status |= ST340_HEDGE;
        return 0;
    }

#ifndef TY340_NODISPLAY
    if (display_point(x, y, i, 0)) {
        /*
         * in real life: type340 pauses
         * until CPU reads coordinates
         */
        u->status |= ST340_LPHIT;
        if (u->lp_ena)
            ty340_lp_int(x, y);
    }
#endif
    return 1;
}

void
lpoint(int x, int y)
{
#ifdef TYPE340_LPOINT
    DEBUGF(("type340 lpoint %d %d\r\n", x, y));
#endif
    point(x, y, 0);
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
                    lpoint(x1 -= stepx, y1);
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

/* here in VECTOR & VCONT modes */
int
vector(int i, int sy, int dy, int sx, int dx)
{
    struct type340 *u = UNIT(0);
    int x0, y0, x1, y1;
    int flags = 0;

    DEBUGF(("v i%d y%c%d x%c%d\r\n", i,
            (sy ? '-' : '+'), dy,
            (sx ? '-' : '+'), dx));
    x0 = u->xpos;
    y0 = u->ypos;

    if (sx) {
        x1 = x0 - dx * u->scale;
        if (x1 < 0) {
            x1 = 0;
            flags = ST340_HEDGE;
        }
    }
    else {
        x1 = x0 + dx * u->scale;
        if (x1 > 1023) {
            x1 = 1023;
            flags = ST340_HEDGE;
        }
    }

    if (sy) {
        y1 = y0 - dy * u->scale;
        if (y1 < 0) {
            y1 = 0;
            flags |= ST340_VEDGE;
        }
    }
    else {
        y1 = y0 + dy * u->scale;
        if (y1 > 1023) {
            y1 = 1023;
            flags |= ST340_VEDGE;
        }
    }

    DEBUGF(("vector i%d (%d,%d) to (%d,%d)\r\n", i, x0, y0, x1, y1));
    if (i)                              /* XXX need OLD value??? */
        lineTwoStep(x0, y0, x1, y1);

    u->xpos = x1;
    u->ypos = y1;
    u->status |= flags;                 /* ?? */
    return flags;
}

/*
 * incremental vector
 * return true on raster violation
 *
 * i is intensify
 * n is subvector number
 * byte is 4 bits
 */
int
ipoint(int i, int n, unsigned char byte)
{
    struct type340 *u = UNIT(0);
    DEBUGF(("type340 ipoint i%d n%d %#o\r\n", i, n, byte));
    if (byte & 010) {                   /* left/right */
        if (byte & 04) {                /* left */
            u->xpos -= u->scale;
            if (u->xpos < 0) {
                u->xpos = 0;            /* XXX wrap? */
                u->status |= ST340_VEDGE; /* save flags & continue?? */
                return 1;               /* escape */
            }
        }
        else {                          /* right */
            u->xpos += u->scale;
            if (u->xpos > 1023) {
                u->xpos = 1023;         /* XXX wrap? */
                u->status |= ST340_VEDGE;
                return 1;
            }
        }
    }
    if (byte & 02) {                    /* up/down */
        if (byte & 01) {                /* down */
            u->ypos -= u->scale;
            if (u->ypos < 0) {
                u->ypos = 0;            /* XXX wrap? */
                u->status |= ST340_HEDGE;
                return 1;
            }
        }
        else {
            u->ypos += u->scale;
            if (u->ypos > 1023) {
                u->ypos = 1023;         /* XXX wrap? */
                u->status |= ST340_HEDGE;
                return 1;
            }
        }
    }
    if (i)
        point(u->xpos, u->ypos, n);

    return 0;                           /* no escape */
}

#if TYPE342
/* 
 * 342 character generator - first 64 characters
 * 7-13_340_Display_Programming_Manual.pdf p.24
 * each char contains a vertical stripe of the matrix.
 * highest bit is top, lowest bit is unused (what was I drinking? -PLB)
 * first char is leftmost
 */
static const unsigned char chars[128][5] = {
    { 0070, 0124, 0154, 0124, 0070 },   /* 00 blob */
    { 0176, 0240, 0240, 0240, 0176 },   /* 01 A */
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
    { 0376, 0002, 0002, 0002, 0002 },   /* 14 L */
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
    { 0100, 0200, 0236, 0220, 0140 },   /* 77 ? */
/*
 * NOT YET COMPLETE!!!
 * original letterforms not available, using
 * https://fontstruct.com/fontstructions/show/357807/5x7_monospaced_pixel_font
 * PLB: I wonder if VT52 was 5x7????
 */
    { 0070, 0124, 0154, 0124, 0070 },   /* 00 blob */
    { 0034, 0042, 0042, 0024, 0076 },   /* 01 a */
    { 0376, 0024, 0042, 0042, 0034 },   /* 02 b */
    { 0034, 0042, 0042, 0042, 0024 },   /* 03 c */
    { 0034, 0042, 0042, 0024, 0376 },   /* 04 d */
    { 0034, 0052, 0052, 0052, 0032 },   /* 05 e */
    { 0010, 0176, 0210, 0200, 0100 },   /* 06 f */
    { 0020, 0052, 0052, 0052, 0074 },   /* 07 g */
    { 0376, 0020, 0040, 0040, 0036 },   /* 10 h */
    { 0000, 0022, 0136, 0002, 0000 },   /* 11 i */
    { 0000, 0004, 0042, 0274, 0000 },   /* 12 j */
    { 0376, 0010, 0024, 0042, 0000 },   /* 13 k */
    { 0374, 0002, 0002, 0000, 0000 },   /* 14 l */
    { 0076, 0040, 0036, 0040, 0036 },   /* 15 m */
    { 0076, 0020, 0040, 0040, 0036 },   /* 16 n */
    { 0034, 0042, 0042, 0042, 0034 },   /* 17 o */
    { 0076, 0050, 0050, 0050, 0020 },   /* 20 p */
    { 0020, 0050, 0050, 0050, 0076 },   /* 21 q */
    { 0076, 0020, 0040, 0040, 0000 },   /* 22 r */
    { 0022, 0052, 0052, 0052, 0044 },   /* 23 s */
    { 0020, 0174, 0022, 0004, 0000 },   /* 24 t */
    { 0074, 0002, 0002, 0004, 0076 },   /* 25 u */
    { 0060, 0014, 0002, 0014, 0060 },   /* 26 v */
    { 0070, 0006, 0030, 0006, 0070 },   /* 27 w */
    { 0042, 0024, 0010, 0024, 0042 },   /* 30 x */
    { 0060, 0012, 0012, 0012, 0074 },   /* 31 y */
    { 0042, 0046, 0052, 0062, 0042 },   /* 32 z */
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
int
character(int n, unsigned char c)
{
    struct type340 *u = UNIT(0);
    int x, y;
    unsigned char s = u->scale;

    switch (c) {
    case CHRLF:                         /* LF */
        u->ypos -= 12*s;
        if (u->ypos < 0) {
            u->status |= ST340_HEDGE;
            u->ypos = 0;
        }
        return 0;
    case CHRCR:                         /* CR */
        u->xpos = 0;
        return 0;
    case CHRUC:                         /* "SHIFT IN (HORIZ)" */
        u->shift = 0;                   /* upper case in SPCWAR 163 */
        return 0;
    case CHRLC:                         /* "SHIFT OUT (VERT)" */
        u->shift = 0100;                /* lower case in SPCWAR 163 */
        return 0;
    case CHRESC:                        /* escape */
        return 1;
    }
    /* plot character from character set selected by "shift" */
    for (x = 0; x < 5; x++) {           /* column: 0 to 4, left to right */
        for (y = 0; y < 7; y++) {       /* row: 0 to 6, bottom to top */
            if (chars[c|u->shift][x] & (2<<y)) {
                /* XXX check for raster violation? */
                point(u->xpos+x*s, u->ypos+y*s, n);
            }
        }
    }
    u->xpos += 7*s;
    if (u->xpos > 1023) {
        u->xpos = 1023;
        u->status |= ST340_VEDGE;
    }
    return 0;
}
#endif

/*
 * execute one type340 instruction
 * returns status word
 * (could return number of microseconds)
 */
ty340word
ty340_instruction(ty340word inst)
{
    struct type340 *u = UNIT(0);
    int i, escape;
#ifdef TYPE347
    ty340word addr;
#endif

    /* cleared by RFD */
    u->status &= ~(ST340_HEDGE|ST340_VEDGE);

    DEBUGF(("type340 mode %#o status %#o\r\n", u->mode, u->status));
    if (u->status & ST340_STOPPED)      /* XXX LPINT as well??? */
        return u->status;

    escape = 0;
    switch (u->mode) {
    case PARAM:
        if (inst & 0600600) { /* curious to see if MIT hacked theirs */
            DEBUGF(("type340 reserved param bits set %#o\r\n", inst));
        }

        /* READ TO MODE: */
        u->mode = GETFIELD(inst, 2, 4);
        if (TESTBIT(inst, 5)) {         /* load l.p. enable */
            u->lp_ena = TESTBIT(inst,6);
            DEBUGF(("type340 lp_ena %d\r\n", u->lp_ena));
        }

        /* PM PULSE: */
        if (TESTBIT(inst, 14)) {
            /* STORE INTENSITY: */
            u->intensity = GETFIELD(inst, 15, 17);
            DEBUGF(("type340 set intensity %d\r\n", u->intensity));
        }
        if (TESTBIT(inst, 11)) {
            /* STORE SCALE: */
            u->scale = 1<<GETFIELD(inst, 12, 13); /* save as multiplier */
            DEBUGF(("type340 set scale %d\r\n", u->scale));
        }
        if (TESTBIT(inst, 7)) {
            u->status |= ST340_STOPPED;
            DEBUGF(("type340 stop\r\n"));
            if (TESTBIT(inst, 8)) {
                DEBUGF(("type340 stop int\r\n"));
                u->status |= ST340_STOP_INT;
            }
        }
        break;

    case POINT:
        u->mode = GETFIELD(inst, 2, 4);

        if (TESTBIT(inst, 5)) {         /* load l.p. enable */
            u->lp_ena = TESTBIT(inst,6);
            DEBUGF(("type340 lp_ena %d\r\n", u->lp_ena));
        }

        if (TESTBIT(inst, 1)) {
            u->ypos = GETFIELD(inst, 8, 17);
            DEBUGF(("type340 set u->ypos %d\r\n", u->ypos));
        }
        else {
            u->xpos = GETFIELD(inst, 8, 17);
            DEBUGF(("type340 set xpos %d\r\n", u->xpos));
        }
        if (TESTBIT(inst, 7)) {         /* intensify */
            DEBUGF(("type340 point (%d,%d)\r\n", u->ypos, u->xpos));
            point(u->xpos, u->ypos, 0);
        }
        break;

    case SLAVE:
        DEBUGF(("type340 slave %06o\r\n", inst));
        u->mode = GETFIELD(inst, 2, 4);
#if TYPE343
        /* control multiple windows???? */
#else
        /* ..set the mode register and halt without requesting a new data word */
        u->status |= ST340_STOPPED;
#endif
        break;

    case CHAR:
        DEBUGF(("type340 char %06o\r\n", inst));
#if TYPE342
        escape = (character(0, GETFIELD(inst, 0, 5)) ||
                  character(1, GETFIELD(inst, 6, 11)) ||
                  character(2, GETFIELD(inst, 12, 17)));
#else
        /* what other missing options do: */
        u->status |= ST340_STOPPED;
#endif
        break;

    case VECTOR:
        DEBUGF(("type340 vector %06o\r\n", inst));
        escape = TESTBIT(inst, 0);
        if (vector(TESTBIT(inst, 1),
                   TESTBIT(inst, 2), GETFIELD(inst, 3, 9),
                   TESTBIT(inst, 10), GETFIELD(inst, 11, 17))) {
            /* XXX interrupt? */
            escape = 1;
        }
        break;
    case VCONT:
        DEBUGF(("type340 vcont %06o\r\n", inst));
        while (!vector(TESTBIT(inst, 1),
                       TESTBIT(inst, 2), GETFIELD(inst, 3, 9),
                       TESTBIT(inst, 10), GETFIELD(inst, 11, 17)))
            ;
        escape = 1;                     /* XXX always???? */
        /* NOTE: NO INTERRUPT!! Clear conditions???? */
        break;

    case INCR:
        DEBUGF(("type340 incr %06o\r\n", inst));
        i = TESTBIT(inst, 1);           /* intensify bit */
        if (ipoint(i, 0, GETFIELD(inst, 2, 5)) ||
            ipoint(i, 1, GETFIELD(inst, 6, 9)) ||
            ipoint(i, 2, GETFIELD(inst, 10, 13)) ||
            ipoint(i, 3, GETFIELD(inst, 14, 17)))
            escape = 1;
        else if (TESTBIT(inst, 0))      /* escape bit */
            escape = 1;
        break;

    case SUBR:
        DEBUGF(("type340 subr %06o\r\n", inst));
#if TYPE347
        /* type 347 Display Subroutine Option? */

        u->mode = GETFIELD(inst, 2, 4);
        addr = GETFIELD(inst, 5, 17);

        switch (GETFIELD(inst, 0, 1)) {
        case DJS:                       /* display jump and save */
            u->ASR = u->DAC;
            u->SAVE_FF = 1;             /* set "save" flip-flop */
            /* FALL */
        case DJP:                       /* display jump */
            u->DAC = addr;
            break;
        case DDS:                       /* display deposit save register */
            ty340_store(addr, (DJP<<16) | u->ASR);
            /* clear SAVE_FF? */
            break;
        default:
            /* XXX ??? */
            break;
        }
#else  /* no 347 */
        /* "halts without generating request for data or interrupt" */
        u->status |= ST340_STOPPED;
#endif
        break;
    }

    if (escape) {
        u->mode = PARAM;
#if TYPE347
        if (u->SAVE_FF) {
            /* return from subroutine */
            u->DAC = u->ASR;
            u->SAVE_FF = 0;
        }
#endif
    }
    if (!(u->status & ST340_STOPPED))   /* XXX LPINT as well??? */
        ty340_rfd();                    /* ready for data */
    return u->status;
} /* ty340_instruction */

ty340word
ty340_status(void)
{
    struct type340 *u = UNIT(0);
    return u->status;
}
