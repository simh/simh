/*
 * $Id: display.h,v 1.13 2004/01/24 08:34:33 phil Exp $
 * interface to O/S independent layer of XY display simulator
 * Phil Budne <phil@ultimate.com>
 * September 2003
 *
 * Changes from Douglas A. Gwyn, Jan 12, 2004
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
 * Except as contained in this notice, the names of the authors shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization
 * from the authors.
 */

/*
 * known display types
 */
enum display_type {
    /*
     * Give TX-0 the rightful spot as the progenitor
     * of the PDP-1, and thus all DEC machines.
     */
    DIS_TX0 = 0,
    DIS_IMLAC = 1,
    DIS_VR14 = 14,
    DIS_VR17 = 17,
    DIS_VR20 = 20,
    DIS_TYPE30 = 30,
    DIS_VR48 = 48,
    DIS_III = 111,
    DIS_TYPE340 = 340,
    DIS_NG = 999,
    DIS_TT2500 = 2500,
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

/*
 * close display
 */
extern void display_close(void *dptr);

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
 * Return true if the display is blank.
 */
extern int display_is_blank(void);

/*
 * display intensity levels.
 * always at least 8 (for VT11/VS60) -- may be mapped internally
 */
#define DISPLAY_INT_MAX 7
#define DISPLAY_INT_MIN 0               /* lowest "on" level */

/*
 * plot a point; arguments are x, y, intensity, color (0/1)
 * returns true if light pen active (mouse button down)
 * at (or very near) this location.
 *
 * Display initialized on first call.
 */
extern int display_point(int,int,int,int);

/*
 * plot a line; arguments are start and end x, y, intensity
 *
 * Display initialized on first call.
 */
extern void display_line(int,int,int,int,int);

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
 * bits high as long as key down
 *
 * asdf kl;'
 * bits just where PDP-1 spacewar expects them!
 * key mappings same as MIT Media Lab Java PDP-1 simulator
 *
 * PDP-6/10 SPCWAR adds hyperspace button, two more players.
 * All additional bits above PDP-1 18-bit word.
 */
extern unsigned long spacewar_switches;

/*
 * spacewar_switches fruit salad:
 *
 * the low 18 bits spacewar_swiches (a 32-bit int)
 * are the switches for PDP-1 spacewar (four at either end)
 * where the game expects them.
 *
 * Additional bits for the PDP-6/10 game are in the upper 14 bits:
 * existing (top player) hyperspace buttons in the top two bits,
 * and bottom players in two 6-bit bytes
 *
 * Too much mess to have in three places (display.c key up/down and
 * the PDP-10 interface code), so I'm using a common idiom from the PDP-10
 * world: define a macro that expands to multiple macro invocations
 * and redefine the inner macro as needed before expanding the outer macro.
 *
 * I have little/no expectation that this is actually playable, but
 * you could rig up an AVR (or other USB MCU) with switch boxes to
 * look like a USB HID keyboard.  For full historical accuracy, please
 * use wooden controllers!
 *
 * Phil Budne Feb 2018
 */

/* SWSW (SpaceWar SWitch) macro args:
 * LC: lower case key
 * UC: upper case key
 * BIT: bit in spacewar_switches (1 means switch on)
 *      yes, 32-bit int bits expressed in octal
 *      (which is just like decimal, if you're missing two fingers)
 * POS: user in PDP-6/10 parlance Upper/Lower Left/Right
 * FUNC: function name in PDP-6/10 parlance (FIRE means beam or torpedo)
 * comment is meaning in PDP-1 parlance
 *
 * entries in order of PDP-1 function bit order
 */
#define SPACEWAR_SWITCHES \
    SWSW('f', 'F',           01, UL, FIRE) /* torpedos */       \
    SWSW('d', 'D',           02, UL, THRUST) /* engines */      \
    SWSW('a', 'A',           04, UL, CW) /* rotate R */         \
    SWSW('s', 'S',          010, UL, CCW) /* rotate L */        \
    SWSW('g', 'G', 010000000000, UL, HYPER) /* PDP-6/10 hyperspace */ \
    \
    SWSW('\'', '"',      040000, UR, FIRE) /* torpedos */       \
    SWSW(';', ':',      0100000, UR, THRUST) /* engines */      \
    SWSW('k', 'K',      0200000, UR, CW) /* rotate R */         \
    SWSW('l', 'L',      0400000, UR, CCW) /* rotate L */        \
    SWSW('\r','\n',020000000000, UR, HYPER) /* PDP-6/10 hyperspace */ \
    \
    SWSW('v', 'V',     01000000, LL, FIRE) /* torpedos */       \
    SWSW('c', 'C',     02000000, LL, THRUST) /* engines */      \
    SWSW('z', 'Z',     04000000, LL, CW) /* rotate R */         \
    SWSW('x', 'X',    010000000, LL, CCW) /* rotate L */        \
    SWSW('b', 'B',    020000000, LL, HYPER) /* hyperspace */    \
    \
    SWSW('.', '>',   0100000000, LR, FIRE) /* torpedos */       \
    SWSW(',', '<',   0200000000, LR, THRUST) /* engines */      \
    SWSW('n', 'N',   0400000000, LR, CW) /* rotate R */         \
    SWSW('m', 'M',  01000000000, LR, CCW) /* rotate L */        \
    SWSW('/', '?',  02000000000, LR, HYPER) /* hyperspace */

/*
 * The last character typed in the display window.
 */
extern unsigned char display_last_char;

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
 * simulator will set up to 36 simulated switches.
 */
extern void cpu_get_switches(unsigned long *p1, unsigned long *p2);
extern void cpu_set_switches(unsigned long, unsigned long);
