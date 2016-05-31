/*
 * $Id: win32.c,v 1.39 2005/01/14 18:58:03 phil Exp $
 * Win32 support for XY display simulator
 * Phil Budne <phil@ultimate.com>
 * September 2003
 * Revised by Douglas A. Gwyn, 05 Feb. 2004
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

/* use a thread to handle windows messages; */
#define THREADS

/*
 * BUGS:
 * Does not allow you to close display window;
 * would need to tear down both system, and system independent data.
 *
 * now tries to handle PAINT message, as yet untested!!
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "ws.h"
#include "display.h"

#ifndef PIX_SIZE
#define PIX_SIZE 1
#endif

#define APP_CLASS "XYAppClass"
#define APP_MENU "XYAppMenu"            /* ?? */

/*
 * light pen location
 * see ws.h for full description
 */
int ws_lp_x = -1;
int ws_lp_y = -1;

static HWND static_wh;
static HINSTANCE static_inst;
static int xpixels, ypixels;
static const char *window_name;
static HBRUSH white_brush;
static HBRUSH black_brush;
#ifdef SWITCH_CURSORS
static HCURSOR cross, arrow;
#endif

static __inline int
map_key(int k)
{
    switch (k) {
    case 186: return ';';               /* VK_OEM_1? */
    case 222: return '\'';              /* VK_OEM_7? */
    }
    return k;
}

static void
keydown(int k)
{
    display_keydown(map_key(k));
}

static void
keyup(int k)
{
    display_keyup(map_key(k));
}

/*
 * here on any button click, or if mouse dragged while a button down
 */
static void
mousepos(DWORD lp)
{
    int x, y;

    x = LOWORD(lp);
    y = HIWORD(lp);

    /* convert to display coordinates */
#if PIX_SIZE > 1
    x /= PIX_SIZE;
    y /= PIX_SIZE;
#endif
    y = ypixels - 1 - y;

    /* if window has been stretched, can get out of range bits!! */
    if (x >= 0 && x < xpixels && y >= 0 && y < ypixels) {
        /* checked by display_add_point() */
        ws_lp_x = x;
        ws_lp_y = y;
    }
}

/* thoingggg!! "message for you sir!!!" */
static LRESULT CALLBACK
patsy(HWND wh, UINT msg, WPARAM wp, LPARAM lp) /* "WndProc" */
{
    /* printf("msg %d\n", msg); */
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_MOUSEMOVE:
        if (wp & (MK_LBUTTON|MK_MBUTTON|MK_RBUTTON)) {
#ifdef SWITCH_CURSORS
            if (ws_lp_x == -1 && !display_tablet)
                SetCursor(cross);
#endif
            mousepos(lp);
        }
#ifdef SWITCH_CURSORS
        else if (ws_lp_x != -1 && !display_tablet)
            SetCursor(arrow);
#endif
        break;                          /* return?? */

    case WM_LBUTTONDOWN:
        display_lp_sw = 1;
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
#ifdef SWITCH_CURSORS
        if (!display_tablet)
            SetCursor(cross);
#endif
        mousepos(lp);
        break;                          /* return?? */

    case WM_LBUTTONUP:
        display_lp_sw = 0;
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
#ifdef SWITCH_CURSORS
        if (!display_tablet)
            SetCursor(arrow);
#endif
        ws_lp_x = ws_lp_y = -1;
        break;                          /* return?? */

    case WM_KEYDOWN:
        keydown(wp);
        break;

    case WM_KEYUP:
        keyup(wp);
        break;

    case WM_PAINT:
        display_repaint();
        break;                          /* return?? */
    }
    return DefWindowProc(wh, msg, wp, lp);
}

int
ws_poll(int *valp, int maxus)
{
#ifdef THREADS
    /* msgthread handles window events; just delay simulator */
    if (maxus > 0)
        Sleep((maxus+999)/1000);
#else
    MSG msg;
    DWORD start;
    int maxms = (maxus + 999) / 1000;

    for (start = GetTickCount(); GetTickCount() - start < maxms; Sleep(1)) {
        /* empty message queue without blocking */
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
#endif
    return 1;
}

/* called from non-threaded main program */
int
ws_loop(void (*func)(void *), void *arg)
{
    int val;
    while (ws_poll(&val, 0))
        (*func)(arg);
    return val;
}

/* worker for display init */
static void
ws_init2(void) {
    WNDCLASS wc;
    int h, w;

#ifdef SWITCH_CURSORS
    if (!display_tablet) {
        arrow = LoadCursor(NULL, IDC_ARROW);
        cross = LoadCursor(NULL, IDC_CROSS);
    }
#endif

    black_brush = GetStockObject(BLACK_BRUSH);
    white_brush = GetStockObject(WHITE_BRUSH);

    wc.lpszClassName    = APP_CLASS;
    wc.lpfnWndProc      = patsy;
    wc.style            = CS_OWNDC | CS_VREDRAW | CS_HREDRAW;
                        /* also CS_NOCLOSE? CS_SAVEBITS? */

    wc.hInstance        = static_inst = GetModuleHandleA(0);
    wc.hIcon            = LoadIcon(NULL, IDI_APPLICATION);
#ifdef SWITCH_CURSORS
    wc.hCursor          = NULL;
#else
    wc.hCursor          = display_tablet ? NULL : LoadCursor(NULL, IDC_CROSS);
#endif
    wc.hbrBackground    = black_brush;
    wc.lpszMenuName     = APP_MENU;
    wc.cbClsExtra       = 0;
    wc.cbWndExtra       = 0; 
    /* WNDCLASSEX/RegisterClassEx include hIconSm (small icon) */
    RegisterClass(&wc);

    /* 
     * WS_OVERLAPPEDWINDOW=>
     *  WS_OVERLAPPED, WS_CAPTION, WS_SYSMENU, WS_THICKFRAME,
     *  WS_MINIMIZEBOX, WS_MAXIMIZEBOX
     *
     * WS_CHILD (no menu bar), WS_POPUP (mutually exclusive)
     */

    /* empirical crocks to get entire screen; */
    w = (xpixels*PIX_SIZE)+6;
    h = (ypixels*PIX_SIZE)+32;
    /* XXX -- above values work with XP; Phil had +10,+30 */

    static_wh = CreateWindow(APP_CLASS,         /* registered class name */
                             window_name,       /* window name */
                             WS_OVERLAPPED,     /* style */
                             CW_USEDEFAULT, CW_USEDEFAULT, /* X,Y */
                             w, h,
                             NULL,              /* HWND hWndParent */
                             NULL,              /* HMENU hMenu */
                             static_inst,       /* application instance */
                             NULL);             /* lpParam */

    ShowWindow(static_wh, SW_SHOW);
    UpdateWindow(static_wh);
}

#ifdef THREADS
static volatile int init_done;
static DWORD msgthread_id;

static DWORD WINAPI
msgthread(LPVOID arg)
{
    MSG msg;

    ws_init2();

    /* XXX use a mutex? */
    init_done = 1;

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}

static void
ws_thread_init(void)
{
    HANDLE th = CreateThread(NULL,              /* sec. attr. */
                             0,                 /* stack size */
                             msgthread,
                             NULL,              /* param */
                             0,                 /* flags */
                             &msgthread_id);
    CloseHandle(th);

    /* XXX use a mutex; don't wait forever!! */
    while (!init_done)
            Sleep(200);
}
#endif /* THREADS */

/* called from display layer on first display op */
int
ws_init(const char *name, int xp, int yp, int colors, void *dptr)
{
    xpixels = xp;
    ypixels = yp;
    window_name = name;

#ifdef THREADS
    ws_thread_init();
#else
    ws_init2();
#endif
    return 1;                           /* XXX return errors!! */
}

void ws_shutdown (void)
{
}

void *
ws_color_rgb(int r, int g, int b)
{
    /* XXX check for failure??? try GetNearestColor??? */
    return CreateSolidBrush(RGB(r/256, g/256, b/256));
}

void *
ws_color_black(void)
{
    return black_brush;
}

void *
ws_color_white(void)
{
    return white_brush;
}

void
ws_display_point(int x, int y, void *color)
{
    HDC dc;
    RECT r;
    HBRUSH brush = color;

    if (x > xpixels || y > ypixels)
        return;

    y = ypixels - 1 - y;                /* invert y, top left origin */

    /* top left corner */
    r.left = x*PIX_SIZE;
    r.top = y*PIX_SIZE;

    /* bottom right corner, non-inclusive */
    r.right = (x+1)*PIX_SIZE;
    r.bottom = (y+1)*PIX_SIZE;

    if (brush == NULL)
        brush = black_brush;

    dc = GetDC(static_wh);
    FillRect(dc, &r, brush);
    ReleaseDC(static_wh, dc);
}
  
void
ws_sync(void) {
    /* noop */
}

void
ws_beep(void) {
#if 0
    /* play SystemDefault sound; does not work over terminal service */
    MessageBeep(MB_OK);
#else
    /* works over terminal service? Plays default sound/beep on Win9x/ME */
    Beep(440, 500);                     /* Hz, ms. */
#endif
}

unsigned long
os_elapsed(void)
{
    static int new;
    unsigned long ret;
    static DWORD t[2];

    /*
     * only returns milliseconds, but Sleep()
     * only takes milliseconds.
     *
     * wraps after 49.7 days of uptime.
     * DWORD is an unsigned long, so this should be OK
     */
    t[new] = GetTickCount();
    if (t[!new] == 0)
        ret = ~0L;                      /* +INF */
    else
        ret = (t[new] - t[!new]) * 1000;
    new = !new;                         /* Ecclesiastes III */
    return ret;
}
