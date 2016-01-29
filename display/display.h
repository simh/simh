/*
 * $Id: display.h,v 1.13 2004/01/24 08:34:33 phil Exp $
 * interface to O/S independent layer of XY display simulator
 * Phil Budne <phil@ultimate.com>
 * September 2003
 *
 * Changes from Douglas A. Gwyn, Jan 12, 2004
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

/*
 * known display types
 */
enum display_type {
    DIS_VR14 = 14,
    DIS_VR17 = 17,
    DIS_VR20 = 20,
    DIS_TYPE30 = 30,
        DIS_TX0 = 33,
    DIS_VR48 = 48,
    DIS_TYPE340 = 340
};

/*
 * display scale factors
 */
#define RES_FULL    1
#define RES_HALF    2
#define RES_QUARTER 4
#define RES_EIGHTH  8

/*
 * must be called before first call to display_age()
 * (but called implicitly by display_point())
 */
extern int display_init(enum display_type, int scale, void *dptr);

/* return size of virtual display */
extern int display_xpoints(void);
extern int display_ypoints(void);

/* virtual points between display and menu sections */
#define VR48_GUTTER 8   /* just a guess */

/* conversion factor from virtual points and displayed pixels */
extern int display_scale(void);

/*
 * simulate passage of time; first argument is simulated microseconds elapsed,
 * second argument is flag to slow down simulated speed
 * see comments in display.c for why you should call it often!!
 * Under X11 polls for window events!!
 */
extern int display_age(int,int);

/*
 * display intensity levels.
 * always at least 8 (for VT11/VS60) -- may be mapped internally
 */
#define DISPLAY_INT_MAX 7
#define DISPLAY_INT_MIN 0               /* lowest "on" level */

/*
 * plot a point; argumen        ts are x, y, intensity, color (0/1)
 * returns true if light pen active (mouse button down)
 * at (or very near) this location.
 *
 * Display initialized on first call.
 */
extern int display_point(int,int,int,int);

/*
 * force window system to output bits to screen;
 * call after adding points, or aging the screen
 * collect any window system input (mouse or keyboard)
 */
extern void display_sync(void);

/*
 * currently a noop
 */
extern void display_reset(void);

/*
 * ring the bell
 */
extern void display_beep(void);

/*
 * Set light-pen radius; maximum radius in display coordinates
 * from a "lit" location that the light pen will see.
 */
extern void display_lp_radius(int);

/*
 * set by simulated spacewar switch box switches
 * 18 bits (only high 4 and low 4 used)
 */
extern unsigned long spacewar_switches;

/*
 * light pen "tip switch" activated (for VS60 emulation etc.)
 * should only be set from "driver" (window system layer)
 */
extern unsigned char display_lp_sw;

/*
 * deactivates light pen
 * (SIMH DR11-C simulation when initialized sets this and
 * then reports mouse coordinates as Talos digitizer data)
 */
extern unsigned char display_tablet;

/*
 * users of this library are expected to provide these calls.
 * simulator will set 18 simulated switches.
 */
extern unsigned long cpu_get_switches(void);    /* get current switch state */
extern void cpu_set_switches(unsigned long);    /* set switches */
