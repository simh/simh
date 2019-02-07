/*
 * $Id: display.c,v 1.56 2004/02/03 21:44:34 phil Exp - revised by DAG $
 * Simulator and host O/S independent XY display simulator
 * Phil Budne <phil@ultimate.com>
 * September 2003
 *
 * with changes by Douglas A. Gwyn, 05 Feb. 2004
 *
 * started from PDP-8/E simulator vc8e.c;
 *  This PDP8 Emulator was written by Douglas W. Jones at the
 *  University of Iowa.  It is distributed as freeware, of
 *  uncertain function and uncertain utility.
 */

/*
 * Copyright (c) 2003-2018 Philip L. Budne
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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>         /* for USHRT_MAX */
#include "ws.h"
#include "display.h"

/*
 * The user may select (at compile time) how big a window is used to
 * emulate the display.  Using smaller windows saves memory and screen space.
 *
 * Type 30 has 1024x1024 addressing, but only 512x512 visible points.
 * VR14 has only 1024x768 visible points; VR17 has 1024x1024 visible points.
 * VT11 supports 4096x4096 addressing, clipping to the lowest 1024x1024 region.
 * VR48 has 1024x1024 visible points in the main display area and 128x1024
 * visible points in a menu area on the right-hand side (1152x1024 total).
 * VT48 supports 8192x8192 (signed) main-area addressing, clipping to a
 * 1024x1024 window which can be located anywhere within that region.
 * (XXX -- That is what the VT11/VT48 manuals say; however, evidence suggests
 * that the VT11 may actually support 8192x8192 (signed) addressing too.)
 */

/* Define the default display type (if display_init() not called) */
#ifndef DISPLAY_TYPE
#define DISPLAY_TYPE DIS_TYPE30
#endif /* DISPLAY_TYPE not defined */

/* select a default resolution if display_init() not called */
/* XXX keep in struct display? */
#ifndef PIX_SCALE
#define PIX_SCALE RES_HALF
#endif /* PIX_SCALE not defined */

/* select a default light-pen hit radius if display_init() not called */
#ifndef PEN_RADIUS
#define PEN_RADIUS 4
#endif /* PEN_RADIUS not defined */

/*
 * note: displays can have up to two different colors (eg VR20)
 * each color can be made up of any number of phosphors
 * with different colors and decay characteristics (eg Type 30)
 */

#define ELEMENTS(X) (sizeof(X)/sizeof(X[0]))

struct phosphor {
    double red, green, blue;
    double level;           /* decay level (0.5 for half life) */
    double t_level;         /* seconds to decay to level */
};

struct color {
    struct phosphor *phosphors;
    int nphosphors;
    int half_life;          /* for refresh calc */
};

struct display {
    enum display_type type;
    const char *name;
    struct color *color0, *color1;
    short xpoints, ypoints;
};

/*
 * original phosphor constants from Raphael Nabet's XMame 0.72.1 PDP-1 sim.
 *
 * http://bitsavers.trailing-edge.com/components/rca/hb-3/1963_HB-3_CRT_Storage_Tube_and_Monoscope_Section.pdf
 * pdf p374 says 16ADP7 used P7 phosphor.
 * pdf pp28-32 describe P7 phosphor (spectra, buildup, persistence)
 *
 * https://www.youtube.com/watch?v=hZumwXS4fJo
 * "3RP7A CRT - P7 Phosphor Persistence" shows colors/persistence
 */
static struct phosphor p7[] = {
    {0.11, 0.11, 1.0,  0.5, 0.05},  /* fast blue */
    {1.0,  1.0,  0.11, 0.5, 0.20}   /* slow yellow/green */
};
static struct color color_p7 = { p7, ELEMENTS(p7), 125000 };

/* green phosphor for VR14, VR17, VR20 */
static struct phosphor p29[] = {{0.0260, 1.0, 0.00121, 0.5, 0.025}};
struct color color_p29 = { p29, ELEMENTS(p29), 25000 };

/* green phosphor for Tek 611 */
static struct phosphor p31[] = {{0.0, 1.0, 0.77, 0.5, .1}};
struct color color_p31 = { p31, ELEMENTS(p31), 25000 };

static struct phosphor p40[] = {
    /* P40 blue-white spot with yellow-green decay (.045s to 10%?) */
    {0.4, 0.2, 0.924, 0.5, 0.0135},
    {0.5, 0.7, 0.076, 0.5, 0.065}
};
static struct color color_p40 = { p40, ELEMENTS(p40), 20000 };

/* "red" -- until real VR20 phosphor type/number/constants known */
static struct phosphor pred[] = { {1.0, 0.37, 0.37, 0.5, 0.10} };
static struct color color_red = { pred, ELEMENTS(pred), 100000 };

static struct display displays[] = {
   /*
     * TX-0
     *
     * Unknown manufacturer
     * 
     * 12" tube, 
     * maximum dot size ???
     * 50us point plot time (20,000 points/sec)
     * P7 Phosphor??? Two phosphor layers:
     * fast blue (.05s half life), and slow green (.2s half life)
     */
    { DIS_TX0, "MIT TX-0", &color_p7, NULL, 512, 512 },

    
    /*
     * Type 30
     * PDP-1/4/5/8/9/10 "Precision CRT" display system
     *
     * Raytheon 16ADP7A CRT?
     * web searches for 16ADP7 finds useful information!!
     * 16" tube, 14 3/8" square raster
     * maximum dot size .015"
     * 50us point plot time (20,000 points/sec)
     * P7 Phosphor??? Two phosphor layers:
     * fast blue (.05s half life), and slow green (.2s half life)
     * 360 lb
     * 7A at 115+-10V 60Hz
     */
    { DIS_TYPE30, "Type 30", &color_p7, NULL, 1024, 1024 },

    /*
     * VR14
     * used w/ GT40/44, AX08, VC8E
     *
     * Viewable area 6.75" x 9"
     * 12" diagonal
     * brightness >= 30 fL
     * dot size .02" (20 mils)
     * settle time:
     *  full screen 18us to +/-1 spot diameter
     *  .1" change 1us to +/-.5 spot diameter
     * weight 75lb
     */
    { DIS_VR14, "VR14", &color_p29, NULL, 1024, 768 },

    /*
     * VR17
     * used w/ GT40/44, AX08, VC8E
     *
     * Viewable area 9.25" x 9.25"
     * 17" diagonal
     * dot size .02" (20 mils)
     * brightness >= 25 fL
     * phosphor: P39 doped for IR light pen use
     * light pen: Type 375
     * weight 85lb
     */
    { DIS_VR17, "VR17", &color_p29, NULL, 1024, 1024 },

    /*
     * VR20
     * on VC8E
     * Two colors!!
     */
    { DIS_VR20, "VR20", &color_p29, &color_red, 1024, 1024 },

    /*
     * VR48
     * (on VT48 in VS60)
     * from Douglas A. Gwyn 23 Nov. 2003
     *
     * Viewable area 12" x 12", plus 1.5" x 12" menu area on right-hand side
     * 21" diagonal
     * dot size <= .01" (10 mils)
     * brightness >= 31 fL
     * phosphor: P40 (blue-white fluorescence with yellow-green phosphorescence)
     * light pen: Type 377A (with tip switch)
     * driving circuitry separate
     * (normally under table on which CRT is mounted)
     */
    { DIS_VR48, "VR48", &color_p40, NULL, 1024+VR48_GUTTER+128, 1024 },

    /*
     * Type 340 Display system
     * on PDP-1/4/6/7/9/10
     *
     * Raytheon 16ADP7A CRT, same as Type 30
     * 1024x1024
     * 9 3/8" raster (.01" dot pitch)
     * 0,0 at lower left
     * 8 intensity levels
     */
    { DIS_TYPE340, "Type 340", &color_p7, NULL, 1024, 1024 },

    /*
     * NG display
     * on PDP-11/45
     *
     * Tektronix 611
     * 512x512, out of 800x600
     * 0,0 at middle
     */
    { DIS_NG, "NG Display", &color_p31, NULL, 512, 512 }
};

/*
 * Unit time (in microseconds) used to store display point time to
 * live at current aging level.  If this is too small, delay values
 * cannot fit in an unsigned short.  If it is too large all pixels
 * will age at once.  Perhaps a suitable value should be calculated at
 * run time?  When display_init() calculates refresh_interval it
 * sanity checks for both cases.
 */
#define DELAY_UNIT 250

/* levels to display in first half-life; determines refresh rate */
#ifndef LEVELS_PER_HALFLIFE
#define LEVELS_PER_HALFLIFE 4
#endif

/* after 5 half lives (.5**5) remaining intensity is 3% of original */
#ifndef HALF_LIVES_TO_DISPLAY
#define HALF_LIVES_TO_DISPLAY 5
#endif

/*
 * refresh_rate is number of times per (simulated) second a pixel is
 * aged to next lowest intensity level.
 *
 * refresh_rate = ((1e6*LEVELS_PER_HALFLIFE)/PHOSPHOR_HALF_LIFE)
 * refresh_interval = 1e6/DELAY_UNIT/refresh_rate
 *          = PHOSPHOR_HALF_LIFE/LEVELS_PER_HALF_LIFE
 * intensities = (HALF_LIVES_TO_DISPLAY*PHOSPHOR_HALF_LIFE)/refresh_interval
 *         = HALF_LIVES_TO_DISPLAY*LEVELS_PER_HALFLIFE
 *
 * See also comments on display_age()
 *
 * Try to keep LEVELS_PER_HALFLIFE*HALF_LIVES_TO_DISPLAY*NLEVELS <= 192
 * to run on 8-bit (256 color) displays!
 */

/*
 * number of aging periods to display a point for
 */
#define NTTL (HALF_LIVES_TO_DISPLAY*LEVELS_PER_HALFLIFE)

/*
 * maximum (initial) TTL for a point.
 * TTL's are stored 1-based
 * (a stored TTL of zero means the point is off)
 */
#define MAXTTL NTTL

/*
 * number of drawing intensity levels
 */
#define NLEVELS (DISPLAY_INT_MAX-DISPLAY_INT_MIN+1)

#define MAXLEVEL (NLEVELS-1)

/*
 * Display Device Implementation
 */

/*
 * Each point on the display is represented by a "struct point".  When
 * a point isn't dark (intensity > 0), it is linked into a circular,
 * doubly linked delta queue (a priority queue where "delay"
 * represents the time difference from the previous entry (if any) in
 * the queue.
 *
 * All points are aged refresh_rate times/second, each time moved to the
 * next (logarithmically) lower intensity level.  When display_age() is
 * called, only the entries which have expired are processed.  Calling
 * display_age() often allows spreading out the workload.
 *
 * An alternative would be to have intensity levels represent linear
 * decreases in intensity, and have the decay time at each level change.
 * Inverting the decay function for a multi-component phosphor may be
 * tricky, and the two different colors would need different time tables.
 * Furthermore, it would require finding the correct location in the
 * queue when adding a point (currently only need to add points at end)
 */

/*
 * 12 bytes/entry on 32-bit system when REFRESH_RATE > 15
 * (requires 3MB for 512x512 display).
 */

typedef unsigned short delay_t;
#define DELAY_T_MAX USHRT_MAX

struct point {
    struct point *next;         /* next entry in queue */
    struct point *prev;         /* prev entry in queue */
    delay_t delay;              /* delta T in DELAY_UNITs */
    unsigned char ttl;          /* zero means off, not linked in */
    unsigned char level : 7;    /* intensity level */
    unsigned char color : 1;    /* for VR20 (two colors) */
};

static struct point *points;    /* allocated array of points */
static struct point _head;
#define head (&_head)

/*
 * time span of all entries in queue
 * should never exceed refresh_interval
 * (should be possible to make this a delay_t)
 */
static long queue_interval;

/* convert X,Y to a "struct point *" */
#define P(X,Y) (points + (X) + ((Y)*(size_t)xpixels))

/* convert "struct point *" to X and Y */
#define X(P) (((P) - points) % xpixels)
#define Y(P) (((P) - points) / xpixels)

static int initialized = 0;

/*
 * global set by O/S display level to indicate "light pen tip switch activated"
 * (This is used only by the VS60 emulation, also by vttest to change patterns)
 */
unsigned char display_lp_sw = 0;

/*
 * global set by DR11-C simulation when DR device enabled; deactivates
 * light pen and instead reports mouse coordinates as Talos digitizer
 * data via DR11-C
 */
unsigned char display_tablet = 0;

/*
 * can be changed with display_lp_radius()
 */
static long scaled_pen_radius_squared;

/* run-time -- set by display_init() */
static int xpoints, ypoints;
static int xpixels, ypixels;
static int refresh_rate;
static int refresh_interval;
static int ncolors;
static enum display_type display_type;
static int scale;

/*
 * relative brightness for each display level
 * (all but last must be less than 1.0)
 */
static float level_scale[NLEVELS];

/*
 * table of pointer to window system "colors"
 * for painting each age level, intensity level and beam color
 */
void *colors[2][NLEVELS][NTTL];

void
display_lp_radius(int r)
{
    r /= scale;
    scaled_pen_radius_squared = r * r;
}

/*
 * from display_age and display_point
 * since all points age at the same rate,
 * only adds points at end of list.
 */
static void
queue_point(struct point *p)
{
    int d;

    d = refresh_interval - queue_interval;
    queue_interval += d;
    /* queue_interval should now be == refresh_interval */

#ifdef PARANOIA
    if (p->ttl == 0 || p->ttl > MAXTTL)
    printf("queuing %d,%d level %d!\n", X(p), Y(p), p->level);
    if (d > DELAY_T_MAX)
    printf("queuing %d,%d delay %d!\n", X(p), Y(p), d);
    if (queue_interval > DELAY_T_MAX)
    printf("queue_interval (%d) > DELAY_T_MAX (%d)\n",
           (int)queue_interval, DELAY_T_MAX);
#endif /* PARANOIA defined */

    p->next = head;
    p->prev = head->prev;

    head->prev->next = p;
    head->prev = p;

    p->delay = d;
}

/*
 * here to to dynamically adjust interval for examination
 * of elapsed vs. simulated time, and fritter away
 * any extra wall-clock time without eating CPU
 */

/*
 * more parameters!
 */

/*
 * upper bound for elapsed time between elapsed time checks.
 * if more than MAXELAPSED microseconds elapsed while simulating
 * delay_check simulated microseconds, decrease delay_check.
 */
#define MAXELAPSED 100000       /* 10Hz */

/*
 * lower bound for elapsed time between elapsed time checks.
 * if fewer than MINELAPSED microseconds elapsed while simulating
 * delay_check simulated microseconds, increase delay_check.
 */
#define MINELAPSED 50000        /* 20Hz */

/*
 * upper bound for delay (sleep/poll).
 * If difference between elapsed time and simulated time is
 * larger than MAXDELAY microseconds, decrease delay_check.
 *
 * since delay is elapsed time - simulated time, MAXDELAY
 * should be <= MAXELAPSED
 */
#ifndef MAXDELAY
#define MAXDELAY 100000         /* 100ms */
#endif /* MAXDELAY not defined */

/*
 * lower bound for delay (sleep/poll).
 * If difference between elapsed time and simulated time is
 * smaller than MINDELAY microseconds, increase delay_check.
 *
 * since delay is elapsed time - simulated time, MINDELAY
 * should be <= MINELAPSED
 */
#ifndef MINDELAY
#define MINDELAY 50000          /* 50ms */
#endif /* MINDELAY not defined */

/*
 * Initial amount of simulated time to elapse before polling.
 * Value is very low to ensure polling occurs on slow systems.
 * Fast systems should ramp up quickly.
 */
#ifndef INITIAL_DELAY_CHECK
#define INITIAL_DELAY_CHECK 1000    /* 1ms */
#endif /* INITIAL_DELAY_CHECK */

/*
 * gain factor (2**-GAINSHIFT) for adjustment of adjustment
 * of delay_check
 */
#ifndef GAINSHIFT
#define GAINSHIFT 3         /* gain=0.125 (12.5%) */
#endif /* GAINSHIFT not defined */

static void
display_delay(int t, int slowdown)
{
    /* how often (in simulated us) to poll/check for delay */
    static unsigned long delay_check = INITIAL_DELAY_CHECK;

    /* accumulated simulated time */
    static unsigned long sim_time = 0;
    unsigned long elapsed;
    long delay;

    sim_time += t;
    if (sim_time < delay_check)
        return;

    elapsed = os_elapsed();     /* read and reset elapsed timer */
    if (elapsed == ~0L) {       /* first time thru? */
        slowdown = 0;           /* no adjustments */
        elapsed = sim_time;
        }

    /*
     * get delta between elapsed (real) time, and simulated time.
     * if simulated time running faster, we need to slow things down (delay)
     */
    if (slowdown)
        delay = sim_time - elapsed;
    else
        delay = 0;              /* just poll */

#ifdef DEBUG_DELAY2
    printf("sim %d elapsed %d delay %d\r\n", sim_time, elapsed, delay);
#endif

    /*
     * Try to keep the elapsed (real world) time between checks for
     * delay (and poll for window system events) bounded between
     * MAXELAPSED and MINELAPSED.  Also tries to keep
     * delay/poll time bounded between MAXDELAY and MINDELAY -- large
     * delays make the simulation spastic, while very small ones are
     * inefficient (too many system calls) and tend to be inaccurate
     * (operating systems have a certain granularity for time
     * measurement, and when you try to sleep/poll for very short
     * amounts of time, the noise will dominate).
     *
     * delay_check period may be adjusted often, and oscillate.  There
     * is no single "right value", the important things are to keep
     * the delay time and max poll intervals bounded, and responsive
     * to system load.
     */
    if (elapsed > MAXELAPSED || delay > MAXDELAY) {
        /* too much elapsed time passed, or delay too large; shrink interval */
        if (delay_check > 1) {
            delay_check -= delay_check>>GAINSHIFT;
#ifdef DEBUG_DELAY
            printf("reduced period to %d\r\n", delay_check);
#endif /* DEBUG_DELAY defined */
            }
        }
    else 
        if ((elapsed < MINELAPSED) || (slowdown && (delay < MINDELAY))) {
            /* too little elapsed time passed, or delta very small */
            int gain = delay_check>>GAINSHIFT;

            if (gain == 0)
                gain = 1;           /* make sure some change made! */
            delay_check += gain;
#ifdef DEBUG_DELAY
            printf("increased period to %d\r\n", delay_check);
#endif /* DEBUG_DELAY defined */
            }
    if (delay < 0)
        delay = 0;
    /* else if delay < MINDELAY, clamp at MINDELAY??? */

    /* poll for window system events and/or delay */
    ws_poll(NULL, delay);

    sim_time = 0;                   /* reset simulated time clock */

    /*
     * delay (poll/sleep) time included in next "elapsed" period
     * (clock not reset after a delay)
     */
} /* display_delay */

/*
 * here periodically from simulator to age pixels.
 *
 * calling often with small values will age a few pixels at a time,
 * and assist with graceful aging of display, and pixel aging.
 *
 * values should be smaller than refresh_interval!
 *
 * returns true if anything on screen changed.
 */

int
display_age(int t,          /* simulated us since last call */
        int slowdown)       /* slowdown to simulated speed */
{
    struct point *p;
    static int elapsed = 0;
    static int refresh_elapsed = 0; /* in units of DELAY_UNIT bounded by refresh_interval */
    int changed;

    if (!initialized && !display_init(DISPLAY_TYPE, PIX_SCALE, NULL))
        return 0;

    if (slowdown)
        display_delay(t, slowdown);

    changed = 0;

    elapsed += t;
    if (elapsed < DELAY_UNIT)
        return 0;

    t = elapsed / DELAY_UNIT;
    elapsed %= DELAY_UNIT;

    ++refresh_elapsed;
    if (refresh_elapsed >= refresh_interval) {
        display_sync ();
        refresh_elapsed = 0;
        }

    while ((p = head->next) != head) {
        int x, y;

        /* look at oldest entry */
        if (p->delay > t) {         /* further than our reach? */
            p->delay -= t;          /* update head */
            queue_interval -= t;    /* update span */
            break;                  /* quit */
            }

        x = X(p);
        y = Y(p);
#ifdef PARANOIA
        if (p->ttl == 0)
            printf("BUG: age %d,%d ttl zero\n", x, y);
#endif /* PARANOIA defined */

        /* dequeue point */
        p->prev->next = p->next;
        p->next->prev = p->prev;

        t -= p->delay;              /* lessen our reach */
        queue_interval -= p->delay; /* update queue span */

        ws_display_point(x, y, colors[p->color][p->level][--p->ttl]);
        changed = 1;

        /* queue it back up, unless we just turned it off! */
        if (p->ttl > 0)
            queue_point(p);
        }
    return changed;
} /* display_age */

/* here from window system */
void
display_repaint(void) {
    struct point *p;
    int x, y;
    /*
     * bottom to top, left to right.
     */
    for (p = points, y = 0; y < ypixels; y++)
        for (x = 0; x < xpixels; p++, x++)
            if (p->ttl)
                ws_display_point(x, y, colors[p->color][p->level][p->ttl-1]);
    ws_sync();
}

/* (0,0) is lower left */
static int
intensify(int x,            /* 0..xpixels */
      int y,                /* 0..ypixels */
      int level,            /* 0..MAXLEVEL */
      int color)            /* for VR20! 0 or 1 */
{
    struct point *p;
    int bleed;

    if (x < 0 || x >= xpixels || y < 0 || y >= ypixels)
        return 0;           /* limit to display */

    p = P(x,y);
    if (p->ttl) {           /* currently lit? */
#ifdef LOUD
        printf("%d,%d old level %d ttl %d new %d\r\n",
               x, y, p->level, p->ttl, level);
#endif /* LOUD defined */

        /* unlink from delta queue */
        p->prev->next = p->next;

        if (p->next == head)
            queue_interval -= p->delay;
        else
            p->next->delay += p->delay;
        p->next->prev = p->prev;
        }

    bleed = 0;              /* no bleeding for now */

    /* EXP: doesn't work... yet */
    /* if "recently" drawn, same or brighter, same color, make even brighter */
    if (p->ttl >= MAXTTL*2/3 && 
        level >= p->level && 
        p->color == color &&
        level < MAXLEVEL)
        level++;

    /*
     * this allows a dim beam to suck light out of
     * a recently drawn bright spot!!
     */
    if (p->ttl != MAXTTL || p->level != level || p->color != color) {
        p->ttl = MAXTTL;
        p->level = level;
        p->color = color;   /* save color even if monochrome */
        ws_display_point(x, y, colors[p->color][p->level][p->ttl-1]);
        }

    queue_point(p);         /* put at end of list */
    return bleed;
}

int
display_point(int x,        /* 0..xpixels (unscaled) */
          int y,            /* 0..ypixels (unscaled) */
          int level,        /* DISPLAY_INT_xxx */
          int color)        /* for VR20! 0 or 1 */
{
    long lx, ly;

    if (!initialized && !display_init(DISPLAY_TYPE, PIX_SCALE, NULL))
        return 0;

    /* scale x and y to the displayed number of pixels */
    /* handle common cases quickly */
    if (scale > 1) {
        if (scale == 2) {
            x >>= 1;
            y >>= 1;
            }
        else {
            x /= scale;
            y /= scale;
            }
        }

#if DISPLAY_INT_MIN > 0
    level -= DISPLAY_INT_MIN;       /* make zero based */
#endif
    intensify(x, y, level, color);
    /* no bleeding for now (used to recurse for neighbor points) */

    if (ws_lp_x == -1 || ws_lp_y == -1)
        return 0;

    lx = x - ws_lp_x;
    ly = y - ws_lp_y;
    return lx*lx + ly*ly <= scaled_pen_radius_squared;
} /* display_point */

/*
 * calculate decay color table for a phosphor mixture
 * must be called AFTER refresh_rate initialized!
 */
static void
phosphor_init(struct phosphor *phosphors, int nphosphors, int color)
{
    int ttl;

    /* for each display ttl level; newest to oldest */
    for (ttl = NTTL-1; ttl > 0; ttl--) {
        struct phosphor *pp;
        double rr, rg, rb;  /* real values */

        /* fractional seconds */
        double t = ((double)(NTTL-1-ttl))/refresh_rate;

        int ilevel;         /* intensity levels */
        int p;

        /* sum over all phosphors in mixture */
        rr = rg = rb = 0.0;
        for (pp = phosphors, p = 0; p < nphosphors; pp++, p++) {
            double decay = pow(pp->level, t/pp->t_level);

            rr += decay * pp->red;
            rg += decay * pp->green;
            rb += decay * pp->blue;
            }

        /* scale for brightness for each intensity level */
        for (ilevel = MAXLEVEL; ilevel >= 0; ilevel--) {
             int r, g, b;
             void *cp;

             /*
              * convert to 16-bit integer; clamp at 16 bits.
              * this allows the sum of brightness factors across phosphors
              * for each of R G and B to be greater than 1.0
              */

             r = (int)(rr * level_scale[ilevel] * 0xffff);
             if (r > 0xffff) r = 0xffff;

             g = (int)(rg * level_scale[ilevel] * 0xffff);
             if (g > 0xffff) g = 0xffff;

             b = (int)(rb * level_scale[ilevel] * 0xffff);
             if (b > 0xffff) b = 0xffff;

             cp = ws_color_rgb(r, g, b);
             if (!cp) {                     /* allocation failed? */
             if (ttl == MAXTTL-1) {         /* brand new */
                 if (ilevel == MAXLEVEL)    /* highest intensity? */
                     cp = ws_color_white(); /* use white */
                 else
                     cp = colors[color][ilevel+1][ttl]; /* use next lvl */
             } /* brand new */
             else if (r + g + b >= 0xffff*3/3) /* light-ish? */
                 cp = colors[color][ilevel][ttl+1]; /* use previous TTL */
                 else
                     cp = ws_color_black();
             }
             colors[color][ilevel][ttl] = cp;
        } /* for each intensity level */
    } /* for each TTL */
} /* phosphor_init */

static struct display *
find_type(enum display_type type)
{
    int i;
    struct display *dp;

    for (i = 0, dp = displays; i < ELEMENTS(displays); i++, dp++)
        if (dp->type == type)
            return dp;
    return NULL;
}

int
display_init(enum display_type type, int sf, void *dptr)
{
    static int init_failed = 0;
    struct display *dp;
    int half_life;
    int i;

    if (initialized) {
        /* cannot change type once started */
        /* XXX say something???? */
        return type == display_type;
        }

    if (init_failed)
        return 0;               /* avoid thrashing */

    init_failed = 1;            /* assume the worst */
    dp = find_type(type);
    if (!dp) {
        fprintf(stderr, "Unknown display type %d\r\n", (int)type);
        goto failed;
        }

    /* Initialize display list */
    head->next = head->prev = head;

    display_type = type;
    scale = sf;

    xpoints = dp->xpoints;
    ypoints = dp->ypoints;

    /* increase scale factor if won't fit on desktop? */
    xpixels = xpoints / scale;
    ypixels = ypoints / scale;

    /* set default pen radius now that scale is set */
    display_lp_radius(PEN_RADIUS);

    ncolors = 1;
    /*
     * use function to calculate from looking at avg (max?)
     * of phosphor half lives???
     */
#define COLOR_HALF_LIFE(C) ((C)->half_life)

    half_life = COLOR_HALF_LIFE(dp->color0);
    if (dp->color1) {
        if (dp->color1->half_life > half_life)
            half_life = COLOR_HALF_LIFE(dp->color1);
        ncolors++;
        }

    /* before phosphor_init; */
    refresh_rate = (1000000*LEVELS_PER_HALFLIFE)/half_life;
    refresh_interval = 1000000/DELAY_UNIT/refresh_rate;

    /*
     * sanity check refresh_interval
     * calculating/selecting DELAY_UNIT at runtime might avoid this!
     */

    /* must be non-zero; interval of 1 means all pixels will age at once! */
    if (refresh_interval < 1) {
        /* decrease DELAY_UNIT? */
        fprintf(stderr, "NOTE! refresh_interval too small: %d\r\n",
                        refresh_interval);

        /* dunno if this is a good idea, but might be better than dying */
        refresh_interval = 1;
        }

    /* point lifetime in DELAY_UNITs will not fit in p->delay field! */
    if (refresh_interval > DELAY_T_MAX) {
        /* increase DELAY_UNIT? */
        fprintf(stderr, "bad refresh_interval %d > DELAY_T_MAX %d\r\n",
            refresh_interval, DELAY_T_MAX);
        goto failed;
        }

    /*
     * before phosphor_init;
     * set up relative brightness of display intensity levels
     * (could differ for different hardware)
     *
     * linear for now.  boost factor insures low intensities are visible
     */
#define BOOST 5
    for (i = 0; i < NLEVELS; i++)
        level_scale[i] = ((float)i+1+BOOST)/(NLEVELS+BOOST);

    points = (struct point *)calloc((size_t)xpixels,
                    ypixels * sizeof(struct point));
    if (!points)
        goto failed;

    if (!ws_init(dp->name, xpixels, ypixels, ncolors, dptr))
        goto failed;

    phosphor_init(dp->color0->phosphors, dp->color0->nphosphors, 0);

    if (dp->color1)
        phosphor_init(dp->color1->phosphors, dp->color1->nphosphors, 1);

    initialized = 1;
    init_failed = 0;            /* hey, we made it! */
    return 1;

 failed:
    fprintf(stderr, "Display initialization failed\r\n");
    return 0;
}

void
display_reset(void)
{
    /* XXX tear down window? just clear it? */
}

void
display_sync(void)
{
    ws_poll (NULL, 0);
    ws_sync ();
}

void
display_beep(void)
{
    ws_beep();
}

int
display_xpoints(void)
{
    return xpoints;
}

int
display_ypoints(void)
{
    return ypoints;
}

int
display_scale(void)
{
    return scale;
}

/*
 * handle keyboard events
 *
 * data switches: bit toggled on key up, all cleared on space
 * enough for PDP-1/4/7/9/15 (for munching squares!):
 * 123 456 789 qwe rty uio
 *
 * second set of 18 for PDP-6/10, IBM7xxx (shifted versions of above):
 * !@# $%^ &*( QWE RTY UIO
 *
 */
unsigned long spacewar_switches = 0;

unsigned char display_last_char;

/* here from window system */
void
display_keydown(int k)
{
    switch (k) {
/* handle spacewar switches: see display.h for copious commentary */
#define SWSW(LC,UC,BIT,POS36,FUNC36) \
    case LC: case UC: spacewar_switches |= BIT; return;
    SPACEWAR_SWITCHES
#undef SWSW
    default: return;
    }
}

/* here from window system */
void
display_keyup(int k)
{
    unsigned long test_switches, test_switches2;

    cpu_get_switches(&test_switches, &test_switches2);
    switch (k) {
/* handle spacewar switches: see display.h for copious commentary */
#define SWSW(LC,UC,BIT,POS36,NAME36) \
    case LC: case UC: spacewar_switches &= ~BIT; return;

    SPACEWAR_SWITCHES
#undef SWSW

    case '1': test_switches ^= 1<<17; break;
    case '2': test_switches ^= 1<<16; break;
    case '3': test_switches ^= 1<<15; break;

    case '4': test_switches ^= 1<<14; break;
    case '5': test_switches ^= 1<<13; break;
    case '6': test_switches ^= 1<<12; break;

    case '7': test_switches ^= 1<<11; break;
    case '8': test_switches ^= 1<<10; break;
    case '9': test_switches ^= 1<<9; break;

    case 'q': test_switches ^= 1<<8; break;
    case 'w': test_switches ^= 1<<7; break;
    case 'e': test_switches ^= 1<<6; break;

    case 'r': test_switches ^= 1<<5; break;
    case 't': test_switches ^= 1<<4; break;
    case 'y': test_switches ^= 1<<3; break;

    case 'u': test_switches ^= 1<<2; break;
    case 'i': test_switches ^= 1<<1; break;
    case 'o': test_switches ^= 1; break;

    /* second set of 18 switches */
    case '!': test_switches2 ^= 1<<17; break;
    case '@': test_switches2 ^= 1<<16; break;
    case '#': test_switches2 ^= 1<<15; break;

    case '$': test_switches2 ^= 1<<14; break;
    case '%': test_switches2 ^= 1<<13; break;
    case '^': test_switches2 ^= 1<<12; break;

    case '&': test_switches2 ^= 1<<11; break;
    case '*': test_switches2 ^= 1<<10; break;
    case '(': test_switches2 ^= 1<<9; break;

    case 'Q': test_switches2 ^= 1<<8; break;
    case 'W': test_switches2 ^= 1<<7; break;
    case 'E': test_switches2 ^= 1<<6; break;

    case 'R': test_switches2 ^= 1<<5; break;
    case 'T': test_switches2 ^= 1<<4; break;
    case 'Y': test_switches2 ^= 1<<3; break;

    case 'U': test_switches2 ^= 1<<2; break;
    case 'I': test_switches2 ^= 1<<1; break;
    case 'O': test_switches2 ^= 1; break;

    case ' ': test_switches = test_switches2 = 0; break;
    default: return;
    }
    cpu_set_switches(test_switches, test_switches2);
}
