/*
 * $Id: carbon.c,v 1.2 2005/08/06 21:09:03 phil Exp $
 * Mac OS X Carbon support for XY display simulator
 * John Dundas <dundas@caltech.edu>
 * December 2004
 *
 * This is a simplistic driver under Mac OS Carbon for the XY display
 * simulator.
 *
 * A more interesting driver would use OpenGL directly.
 */

#include <Carbon/Carbon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ws.h"
#include "display.h"

#include <sys/types.h>
#include <sys/time.h>

#define ARRAYLEN(a)     (sizeof (a) / sizeof (a[0]))

#ifndef PIX_SIZE
#define PIX_SIZE 1
#endif

/*
 * light pen location
 * see ws.h for full description
 */
int ws_lp_x = -1;
int ws_lp_y = -1;

static RGBColor                 blckColor = { 0x0000, 0x0000, 0x0000 };
static RGBColor                 whteColor = { 0xFFFF, 0xFFFF, 0xFFFF };
static WindowRef                mainWind;
static RgnHandle                rgn;
static MouseTrackingRef         mouseRef;
static int                      xpixels, ypixels;
static int                      buttons = 0; /* tracks state of all buttons */
static EventTargetRef           EventDispatchTarget;

void MyEventWait (      EventTimeout    timeout )       /* double */
{
        EventRef        theEvent;

        if (ReceiveNextEvent (0, NULL, timeout, true, &theEvent) == noErr) {
                SendEventToEventTarget (theEvent, EventDispatchTarget);
                ReleaseEvent (theEvent);
        }
}

static pascal OSStatus doMouseEvent (   EventHandlerCallRef     handlerRef,
                                        EventRef                event,
                                        void                    *userData )
{
        OSStatus        err = eventNotHandledErr;
        Point           start;
        GrafPtr         prevPort;

        /* make sure the display is the current grafport */
        if (!QDSwapPort (GetWindowPort (mainWind), &prevPort))
                prevPort = NULL;
        switch (GetEventKind (event)) {
        case kEventMouseEntered:
                if (ActiveNonFloatingWindow () != mainWind)
                        break;
                SetThemeCursor (kThemeCrossCursor);
                break;
        case kEventMouseExited:
                if (ActiveNonFloatingWindow () != mainWind)
                        break;
                SetThemeCursor (kThemeArrowCursor);
                break;
        case kEventMouseDown:
                GetEventParameter (event, kEventParamMouseLocation,
                        typeQDPoint, NULL, sizeof (typeQDPoint), NULL, &start);
                GlobalToLocal (&start);
                ws_lp_x = start.h;
                ws_lp_y = ypixels - start.v;
                display_lp_sw = 1;
                break;
        case kEventMouseUp:
                display_lp_sw = 0;
                ws_lp_x = ws_lp_y = -1;
                break;
        default:
                break;
        }
        if (prevPort)
                SetPort (prevPort);
        return (err);
}

static pascal OSStatus updateWindow (   EventHandlerCallRef     handlerRef,
                                        EventRef                event,
                                        void                    *userData )
{
        OSStatus        err = eventNotHandledErr;

        switch (GetEventKind (event)) {
        case kEventWindowActivated:
                /* update menus */
                break;
        case kEventWindowClose:         /* Override window close */
                err = noErr;
                break;
        case kEventWindowDrawContent:
                display_repaint ();
                err = noErr;
        default:
                break;
        }
        return (err);
}

static pascal OSStatus doKbdEvent (     EventHandlerCallRef     handlerRef,
                                        EventRef                event,
                                        void                    *userData )
{
        UInt32  c, m;
        char    key;

        GetEventParameter (event, kEventParamKeyCode,
                typeUInt32, NULL, sizeof (typeUInt32), NULL, &c);
        GetEventParameter (event, kEventParamKeyMacCharCodes,
                typeChar,   NULL, sizeof (typeChar),   NULL, &key);
        GetEventParameter (event, kEventParamKeyModifiers,
                typeUInt32, NULL, sizeof (typeUInt32), NULL, &m);

        /* Keys with meta-modifiers are not allowed at this time. */
#define KEY_MODIFIERS   (cmdKey | optionKey | kEventKeyModifierFnMask)
        if (m & KEY_MODIFIERS)
                return (eventNotHandledErr);
        switch (GetEventKind (event)) {
        case kEventRawKeyRepeat:
        case kEventRawKeyDown:
                display_keydown (key);
                break;
        case kEventRawKeyUp:
                display_keyup (key);
                break;
        default:
                break;
        }
        return (noErr);
}

int ws_init (   const char *crtname,    /* crt type name */
                int     xp,             /* screen size in pixels */
                int     yp,
                int     colors,         /* colors to support (not used) */
                void    *dptr)
{
        WindowAttributes        windowAttrs;
        Rect                    r;
        CFStringRef             str;
        static MouseTrackingRegionID    mouseID = { 'AAPL', 0 };
        static const EventTypeSpec      moEvent[] = {
                { kEventClassMouse, kEventMouseEntered },
                { kEventClassMouse, kEventMouseExited },
                { kEventClassMouse, kEventMouseDown },
                { kEventClassMouse, kEventMouseUp },
        };
        static const EventTypeSpec      wuEvent[] = {
                { kEventClassWindow, kEventWindowDrawContent },
                { kEventClassWindow, kEventWindowClose },
                { kEventClassWindow, kEventWindowActivated},
        };
        static const EventTypeSpec      kdEvent[] = {
                { kEventClassKeyboard, kEventRawKeyDown }, 
                { kEventClassKeyboard, kEventRawKeyRepeat },
                { kEventClassKeyboard, kEventRawKeyUp},
        };


        xpixels = xp;           /* save screen size */
        ypixels = yp;
        r.top = 100; r.left = 100; r.bottom = r.top + yp; r.right = r.left + xp;

        /* should check this r against GetQDGlobalsScreenBits (&screen); */
        windowAttrs = kWindowCollapseBoxAttribute | kWindowStandardHandlerAttribute;
        if (CreateNewWindow (kDocumentWindowClass, windowAttrs, &r, &mainWind) != noErr)
                return (0);
        if (str = CFStringCreateWithCString (kCFAllocatorDefault, crtname,
                        kCFStringEncodingASCII)) {
                SetWindowTitleWithCFString (mainWind, str);
                CFRelease (str);
        }
        SetPortWindowPort (mainWind);
/*
 * Setup to handle events
 */
        EventDispatchTarget = GetEventDispatcherTarget ();
        InstallEventHandler (GetWindowEventTarget (mainWind),
                NewEventHandlerUPP (doMouseEvent), ARRAYLEN(moEvent),
                (EventTypeSpec *) &moEvent, NULL, NULL);
        InstallEventHandler (GetWindowEventTarget (mainWind),
                NewEventHandlerUPP (updateWindow), ARRAYLEN(wuEvent),
                (EventTypeSpec *) &wuEvent, NULL, NULL);
        InstallEventHandler (GetWindowEventTarget (mainWind),
                NewEventHandlerUPP (doKbdEvent), ARRAYLEN(kdEvent),
                (EventTypeSpec *) &kdEvent, NULL, NULL);
        /* create region to track cursor shape */
        r.top = 0; r.left = 0; r.bottom = yp; r.right = xp;
        rgn = NewRgn ();
        RectRgn (rgn, &r);
        CloseRgn (rgn);
        CreateMouseTrackingRegion (mainWind, rgn, NULL,
                kMouseTrackingOptionsLocalClip, mouseID, NULL, NULL, &mouseRef);

        ShowWindow (mainWind);
        RGBForeColor (&blckColor);
        PaintRect (&r);
        RGBBackColor (&blckColor);
        return (1);
}

void ws_shutdown (void)
{
}

void *ws_color_black (void)
{
    return (&blckColor);
}

void *ws_color_white (void)
{
    return (&whteColor);
}

void *ws_color_rgb (    int     r,
                        int     g,
                        int     b       )
{
        RGBColor        *color;

        if ((color = malloc (sizeof (RGBColor))) != NULL) {
                color->red = r;
                color->green = g;
                color->blue = b;
        }
        return (color);
}

/* put a point on the screen */
void ws_display_point ( int     x,
                        int     y,
                        void    *color  )
{
#if PIX_SIZE != 1
        Rect    r;
#endif

        if (x > xpixels || y > ypixels)
                return;

        y = ypixels - y /* - 1 */;

#if PIX_SIZE == 1
        SetCPixel (x, y, (color == NULL) ? &blckColor : color);
#else
        r.top = y * PIX_SIZE;
        r.left = x * PIX_SIZE;
        r.bottom = (y + 1) * PIX_SIZE;
        r.right = (x + 1) * PIX_SIZE;

        RGBForeColor ((color == NULL) ? &blckColor : color);
        PaintRect (&r);
#endif
}

void ws_sync (void)
{
        ;
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
int ws_poll (   int     *valp,
                int     maxusec )
{
        static struct elapsed_state     es;     /* static to avoid clearing! */

        elapsed(&es);                   /* start clock */
        do {
                unsigned long e;

                MyEventWait (maxusec * kEventDurationMicrosecond);
                e = elapsed(&es);
                maxusec -= e;
        } while (maxusec > 10000);      /* 10ms */
        return (1);
}

void ws_beep (void)
{
        SysBeep (3);
}

/* public version, used by delay code */
unsigned long os_elapsed (void)
{
        static struct elapsed_state     es;
        return (elapsed (&es));
}
