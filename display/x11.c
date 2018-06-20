/*
 * $Id: x11.c,v 1.32 2005/01/14 18:58:03 phil Exp $
 * X11 support for XY display simulator
 * Phil Budne <phil@ultimate.com>
 * September 2003
 *
 * Changes from Douglas A. Gwyn, Jan 8, 2004
 *
 * started from PDP-8/E simulator (vc8e.c & kc8e.c);
 *      This PDP8 Emulator was written by Douglas W. Jones at the
 *      University of Iowa.  It is distributed as freeware, of
 *      uncertain function and uncertain utility.
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

#ifndef USE_XKB
#define USE_XKB 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ws.h"
#include "display.h"

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Core.h>
#include <X11/Shell.h>
#include <X11/cursorfont.h>
#ifdef USE_XKB
#include <X11/XKBlib.h>
#endif

#include <sys/types.h>
#include <sys/time.h>

#ifndef PIX_SIZE
#define PIX_SIZE 1
#endif

/*
 * light pen location
 * see ws.h for full description
 */
int ws_lp_x = -1;
int ws_lp_y = -1;

static XtAppContext app_context; /* the topmost context for everything */
static Display* dpy;            /* its display */
static int      scr;            /* its screen */
static Colormap cmap;           /* its colormap */
static Widget crtshell;         /* the X window shell */
static Widget crt;              /* the X window in which output will plot */
static int xpixels, ypixels;
#ifdef FULL_SCREEN
/* occupy entire screen for vintage computer fan Sellam Ismail */
static int xoffset, yoffset;
#endif

static GC whiteGC;              /* gc with white foreground */
static GC blackGC;              /* gc with black foreground */
static int buttons = 0;         /* tracks state of all buttons */

static int os_pollfd(int, int); /* forward */

/* here on any mouse button down, AND movement when any button down */
static void
handle_button_press(w, d, e, b)
    Widget w;
    XtPointer d;
    XEvent *e;
    Boolean *b;
{
    int x, y;

    x = e->xbutton.x;
    y = e->xbutton.y;
#ifdef FULL_SCREEN
    /* untested! */
    x -= xoffset;
    y -= yoffset;
#endif
#if PIX_SIZE > 1
    x *= PIX_SIZE;
    y *= PIX_SIZE;
#endif

    if (!display_tablet)
        /* crosshair cursor to indicate tip of active pen */
        XDefineCursor(dpy, XtWindow(crt),
                        (Cursor) XCreateFontCursor(dpy, XC_crosshair));

    y = ypixels - y - 1;
    /*printf("lightpen at %d,%d\n", x, y); fflush(stdout);*/
    ws_lp_x = x;
    ws_lp_y = y;

    if (e->type == ButtonPress) {
        buttons |= e->xbutton.button;

        if (e->xbutton.button == 1) {
            display_lp_sw = 1;
            /*printf("tip switch activated\n"); fflush(stdout);*/
        }
    }

    if (b)
        *b = TRUE;
}

static void
handle_button_release(w, d, e, b)
    Widget w;
    XtPointer d;
    XEvent *e;
    Boolean *b;
{
    if ((buttons &= ~e->xbutton.button) == 0) { /* all buttons released */
        if (!display_tablet)
            /* pencil cursor (close to a pen!) to indicate inactive pen posn */
            XDefineCursor(dpy, XtWindow(crt),
                                (Cursor) XCreateFontCursor(dpy, XC_pencil));

        /* XXX change cursor back?? */
        ws_lp_x = ws_lp_y = -1;
    }

    if (e->xbutton.button == 1) {
        display_lp_sw = 0;
        /*printf("tip switch deactivated\n"); fflush(stdout);*/
    }

    if (b)
        *b = TRUE;
}

/*
 * map keyboard XEvent to 8 bit char or -1
 */
static int
mapkey(e)
    XEvent *e;
{
    int shift = (ShiftMask & e->xkey.state) != 0;
    KeySym key;

#ifdef USE_XKB
    /*
     * X Keyboard Extension
     * described in
     * https://www.x.org/releases/X11R7.7/doc/libX11/XKB/xkblib.html#Xkb_Keyboard_Extension_Support_for_Keyboards
     * copyright 1995, 1996
     *
     * use XkbLibraryVersion and/or XkbQueryExtension
     * to determine if available???
     */
    key = XkbKeycodeToKeysym(dpy, e->xkey.keycode, 0, shift);
#elif 1
    /*
     * documented in
     * Xlib: C Language X Interface (X Version 11, Release 4)
     * copyright 1989
     */
    int keysyms_per_keycode_return;
    KeySym *keysyms = XGetKeyboardMapping(dpy,
                                          e->xkey.keycode,
                                          1,
                                          &keysyms_per_keycode_return);
    key = keysyms[0];
    XFree(keysyms);
#else  /* just in case... */
    /* XKeycodeToKeysym deprecated */
    key = XKeycodeToKeysym(dpy, e->xkey.keycode, shift);
#endif

    if ((key & 0xff00) == 0)
        return key & 0xff;

    switch (key) {
    case XK_Return: return '\r';
    }
    /* printf("ignoring keycode %#x\r\n", key); /**/
    return -1;
}

static void
handle_key_press(w, d, e, b)
    Widget w;
    XtPointer d;
    XEvent *e;
    Boolean *b;
{
    int k = mapkey(e);
    if (k >= 0)
        display_keydown(k);

    if (b)
        *b = TRUE;
}

static void
handle_key_release(w, d, e, b)
    Widget w;
    XtPointer d;
    XEvent *e;
    Boolean *b;
{
    int k = mapkey(e);
    if (k >= 0)
        display_keyup(k);

    if (b)
        *b = TRUE;
}

static void
handle_exposure(w, d, e, b)
    Widget w;
    XtPointer d;
    XEvent *e;
    Boolean *b;
{
    display_repaint();

    if (b)
        *b = TRUE;
}

int
ws_init(const char *crtname,    /* crt type name */
    int xp, int yp,             /* screen size in pixels */
    int colors,                 /* colors to support (not used) */
    void *dptr)
{
    Arg arg[25];
    XGCValues gcvalues;
    unsigned int n;
    int argc;
    char *argv[1];
    int height, width;
#ifdef USE_XKB
    Bool supported;
#endif
    xpixels = xp;               /* save screen size */
    ypixels = yp;

    if (getenv ("DISPLAY") == NULL) {
        return 0;
    }

    XtToolkitInitialize();
    app_context = XtCreateApplicationContext();
    argc = 0;
    argv[0] = NULL;
    dpy = XtOpenDisplay( app_context, NULL, NULL, crtname, NULL, 0,
                        &argc, argv);

#ifdef USE_XKB
    /*
     * suppress synthetic key up events from autorepeat
     * (will still see repeated down events)
     * see keymap function for XKb history
     */
    supported = False;
    (void) XkbSetDetectableAutoRepeat(dpy, True, &supported);
#endif

    scr = DefaultScreen(dpy);

    crtshell = XtAppCreateShell( crtname, /* app name */
                                crtname, /* app class */
                                applicationShellWidgetClass, /* wclass */
                                dpy, /* display */
                                NULL, /* arglist */
                                0);     /* nargs */

    cmap = DefaultColormap(dpy, scr);

    /*
     * Create a drawing area
     */

    n = 0;
#ifdef FULL_SCREEN
    /* center raster in full-screen black window */
    width = DisplayWidth(dpy,scr);
    height = DisplayHeight(dpy,scr);

    xoffset = (width - xpixels*PIX_SIZE)/2;
    yoffset = (height - ypixels*PIX_SIZE)/2;
#else
    width = xpixels*PIX_SIZE;
    height = ypixels*PIX_SIZE;
#endif
    XtSetArg(arg[n], XtNwidth, width);                          n++;
    XtSetArg(arg[n], XtNheight, height);                        n++;
    XtSetArg(arg[n], XtNbackground, BlackPixel( dpy, scr ));    n++;

    crt = XtCreateWidget( crtname, widgetClass, crtshell, arg, n);
    XtManageChild(crt);
    XtPopup(crtshell, XtGrabNonexclusive);
    XtSetKeyboardFocus(crtshell, crt);  /* experimental? */

    /*
     * Create black and white Graphics Contexts
     */

    gcvalues.foreground = BlackPixel( dpy, scr );
    gcvalues.background = BlackPixel( dpy, scr );
    blackGC = XCreateGC(dpy, XtWindow(crt),
                        GCForeground | GCBackground, &gcvalues);

    gcvalues.foreground = WhitePixel( dpy, scr );
    whiteGC = XCreateGC(dpy, XtWindow(crt),
                        GCForeground | GCBackground, &gcvalues);

    if (!display_tablet) {
        /* pencil cursor */
        XDefineCursor(dpy, XtWindow(crt),
                      (Cursor) XCreateFontCursor(dpy, XC_pencil));
    }

    /*
     * Setup to handle events
     */

    XtAddEventHandler(crt, ButtonPressMask|ButtonMotionMask, FALSE,
                      handle_button_press, NULL);
    XtAddEventHandler(crt, ButtonReleaseMask, FALSE,
                      handle_button_release, NULL);
    XtAddEventHandler(crt, KeyPressMask, FALSE,
                      handle_key_press, NULL);
    XtAddEventHandler(crt, KeyReleaseMask, FALSE,
                      handle_key_release, NULL);
    XtAddEventHandler(crt, ExposureMask, FALSE,
                      handle_exposure, NULL);
    return 1;
} /* ws_init */

void ws_shutdown (void)
{
}

void *
ws_color_black(void)
{
    return blackGC;
}

void *
ws_color_white(void)
{
    return whiteGC;
}

void *
ws_color_rgb(int r, int g, int b)
{
    XColor color;

    color.red = r;
    color.green = g;
    color.blue = b;
    /* ignores flags */

    if (XAllocColor(dpy, cmap, &color)) {
        XGCValues gcvalues;
        memset(&gcvalues, 0, sizeof(gcvalues));
        gcvalues.foreground = gcvalues.background = color.pixel;
        return XCreateGC(dpy, XtWindow(crt),
                         GCForeground | GCBackground,
                         &gcvalues);
    }
    /* allocation failed */
    return NULL;
}

/* put a point on the screen */
void
ws_display_point(int x, int y, void *color)
{
    GC gc = (GC) color;

    if (x > xpixels || y > ypixels)
        return;

    y = ypixels - y - 1;                /* X11 coordinate system */

#ifdef FULL_SCREEN
    x += xoffset;
    y += yoffset;
#endif
    if (gc == NULL)
        gc = blackGC;                   /* default to off */
#if PIX_SIZE == 1
    XDrawPoint(dpy, XtWindow(crt), gc, x, y);
#else
    XFillRectangle(dpy, XtWindow(crt), gc,
                   x*PIX_SIZE, y*PIX_SIZE, PIX_SIZE, PIX_SIZE);
#endif
}

void
ws_sync(void)
{
    XFlush(dpy);
}

/*
 * elapsed wall clock time since last call
 * +INF on first call
 */

struct elapsed_state {
    struct timeval tvs[2];
    int new;
};

static unsigned long
elapsed(struct elapsed_state *ep)
{
    unsigned long val;

    gettimeofday(&ep->tvs[ep->new], NULL);
    if (ep->tvs[!ep->new].tv_sec == 0)
        val = ~0L;
    else
        val = ((ep->tvs[ep->new].tv_sec - ep->tvs[!ep->new].tv_sec) * 1000000 +
               (ep->tvs[ep->new].tv_usec - ep->tvs[!ep->new].tv_usec));
    ep->new = !ep->new;
    return val;
}

/* called periodically */
int
ws_poll(int *valp, int maxusec)
{
    static struct elapsed_state es;     /* static to avoid clearing! */

#ifdef WS_POLL_DEBUG
    printf("ws_poll %d\n", maxusec);
    fflush(stdout);
#endif
    elapsed(&es);                       /* start clock */
    do {
        unsigned long e;

        /* tried checking return, but lost on TCP connections? */
        os_pollfd(ConnectionNumber(dpy), maxusec);

        while (XtAppPending(app_context)) {
            XEvent event;

            /* XXX check for connection loss; set *valp? return 0 */
            XtAppNextEvent(app_context, &event );
            XtDispatchEvent( &event );
        }
        e = elapsed(&es);
#ifdef WS_POLL_DEBUG
        printf(" maxusec %d e %d\r\n", maxusec, e);
        fflush(stdout);
#endif
        maxusec -= e;
    } while (maxusec > 10000);  /* 10ms */
    return 1;
}

/* utility: can be called from main program
 * which is willing to cede control
 */
int
ws_loop(void (*func)(void *), void *arg)
{
    int val;

    /* XXX use XtAppAddWorkProc & XtAppMainLoop? */
    while (ws_poll(&val,0))
        (*func)(arg);
    return val;
}

void
ws_beep(void)
{
    XBell(dpy, 0);                      /* ring at base volume */
    XFlush(dpy);
}

/****************
 * could move these to unix.c, if VMS versions needed
 * (or just (GASP!) ifdef)
 */

/* public version, used by delay code */
unsigned long
os_elapsed(void)
{
    static struct elapsed_state es;
    return elapsed(&es);
}

/*
 * select/DisplayNumber works on VMS 7.0+?
 * could move to "unix.c"
 * (I have some nasty VMS code that's supposed to to the job
 * for older systems)
 */

/*
 * sleep for maxus microseconds, returning TRUE sooner if fd is readable
 * used by X11 driver
 */
static int
os_pollfd(int fd, int maxus)
{

    /* use trusty old select (most portable) */
    fd_set rfds;
    struct timeval tv;

    if (maxus >= 1000000) {             /* not bloody likely, avoid divide */
        tv.tv_sec = maxus / 1000000;
        tv.tv_usec = maxus % 1000000;
    }
    else {
        tv.tv_sec = 0;
        tv.tv_usec = maxus;
    }
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    return select(fd+1, &rfds, NULL, NULL, &tv) > 0;
}
