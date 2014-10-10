/* sim_video.c: Bitmap video output

   Copyright (c) 2011-2013, Matt Burke

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   08-Nov-2013  MB      Added globals for current mouse status
   11-Jun-2013  MB      First version
*/

#if defined(USE_SIM_VIDEO)

#include "sim_video.h"

t_bool vid_active = FALSE;
int32 vid_mouse_xrel = 0;
int32 vid_mouse_yrel = 0;
t_bool vid_mouse_b1 = FALSE;
t_bool vid_mouse_b2 = FALSE;
t_bool vid_mouse_b3 = FALSE;

#if HAVE_LIBSDL
#include <SDL.h>
#include <SDL_thread.h>

#define EVENT_REDRAW    1                               /* redraw event for SDL */
#define EVENT_CLOSE     2                               /* close event for SDL */
#define MAX_EVENTS      20                              /* max events in queue */

typedef struct {
    SIM_KEY_EVENT events[MAX_EVENTS];
    SDL_sem *sem;
    int32 head;
    int32 tail;
    int32 count;
    } KEY_EVENT_QUEUE;

typedef struct {
    SIM_MOUSE_EVENT events[MAX_EVENTS];
    SDL_sem *sem;
    int32 head;
    int32 tail;
    int32 count;
    } MOUSE_EVENT_QUEUE;

int vid_thread (void* arg);

/* 
   Currently there are two separate video implementations which exist
   due to the fact that libSDL and libSDL2 provide vastly different APIs.

   libSDL2 is the distinctly better choice going forward, and, given the 
   background thread event digestion, is the only one which will work on 
   OSX.

   We will abandon libSDL (Version 1) completely when the libSDL2 developer
   components are readily packaged for most Linux distributions.
 */

#if SDL_MAJOR_VERSION == 1
t_bool vid_key_state[SDLK_LAST];
t_bool vid_mouse_captured;
int32 vid_width;
int32 vid_height;
SDL_Surface *vid_image;                                 /* video buffer */
SDL_Surface *vid_window;                                /* window handle */
SDL_Thread *vid_thread_handle;                          /* event thread handle */
uint32 vid_mono_palette[2];
KEY_EVENT_QUEUE vid_key_events;                         /* keyboard events */
MOUSE_EVENT_QUEUE vid_mouse_events;                     /* mouse events */
DEVICE *vid_dev;

t_stat vid_open (DEVICE *dptr, uint32 width, uint32 height)
{
if (!vid_active) {
    vid_active = TRUE;
    vid_width = width;
    vid_height = height;
    vid_mouse_captured = FALSE;
    vid_mouse_xrel = 0;
    vid_mouse_yrel = 0;

    vid_key_events.head = 0;
    vid_key_events.tail = 0;
    vid_key_events.count = 0;
    vid_key_events.sem = SDL_CreateSemaphore (1);
    vid_mouse_events.head = 0;
    vid_mouse_events.tail = 0;
    vid_mouse_events.count = 0;
    vid_mouse_events.sem = SDL_CreateSemaphore (1);

    vid_dev = dptr;

    vid_thread_handle = SDL_CreateThread (vid_thread, NULL);
    if (vid_thread_handle == NULL) {
        vid_close ();
        return SCPE_OPENERR;
        }
    
    sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_open() - Success\n");
    }
return SCPE_OK;
}

t_stat vid_close (void)
{
SDL_Event user_event;
int status;

if (vid_active) {
    vid_active = FALSE;
    if (vid_thread_handle) {
        sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_close()\n");
        user_event.type = SDL_USEREVENT;
        user_event.user.code = EVENT_CLOSE;
        user_event.user.data1 = NULL;
        user_event.user.data2 = NULL;

        SDL_PushEvent (&user_event);
        SDL_WaitThread (vid_thread_handle, &status);
        vid_thread_handle = NULL;
        vid_dev = NULL;
        }
    if (vid_mouse_events.sem) {
        SDL_DestroySemaphore(vid_mouse_events.sem);
        vid_mouse_events.sem = NULL;
        }
    if (vid_key_events.sem) {
        SDL_DestroySemaphore(vid_key_events.sem);
        vid_key_events.sem = NULL;
        }
    }
return SCPE_OK;
}

t_stat vid_poll_kb (SIM_KEY_EVENT *ev)
{
if (SDL_SemTryWait (vid_key_events.sem) == 0) {         /* get lock */
    if (vid_key_events.count > 0) {                     /* events in queue? */
        *ev = vid_key_events.events[vid_key_events.head++];
        vid_key_events.count--;
        if (vid_key_events.head == MAX_EVENTS)
            vid_key_events.head = 0;
        SDL_SemPost (vid_key_events.sem);
        return SCPE_OK;
        }
    SDL_SemPost (vid_key_events.sem);
    }
return SCPE_EOF;
}

t_stat vid_poll_mouse (SIM_MOUSE_EVENT *ev)
{
if (SDL_SemTryWait (vid_mouse_events.sem) == 0) {
    if (vid_mouse_events.count > 0) {
        *ev = vid_mouse_events.events[vid_mouse_events.head++];
        vid_mouse_events.count--;
        if (vid_mouse_events.head == MAX_EVENTS)
            vid_mouse_events.head = 0;
        SDL_SemPost (vid_mouse_events.sem);
        return SCPE_OK;
        }
    SDL_SemPost (vid_mouse_events.sem);
    }
return SCPE_EOF;
}

void vid_draw (int32 x, int32 y, int32 w, int32 h, uint32 *buf)
{
int32 i;
uint32* pixels;

pixels = (uint32 *)vid_image->pixels;

for (i = y; i < (y + h); i++)
    memcpy (pixels + (i * vid_width) + x, buf, (size_t)w*sizeof(*pixels));
}

void vid_refresh (void)
{
SDL_Event user_event;

user_event.type = SDL_USEREVENT;
user_event.user.code = EVENT_REDRAW;
user_event.user.data1 = NULL;
user_event.user.data2 = NULL;

SDL_PushEvent (&user_event);
}

int vid_map_key (int key)
{
switch (key) {

    case SDLK_BACKSPACE:
        return SIM_KEY_BACKSPACE;

    case SDLK_TAB:
        return SIM_KEY_TAB;

    case SDLK_RETURN:
        return SIM_KEY_ENTER;

    case SDLK_PAUSE:
        return SIM_KEY_PAUSE;

    case SDLK_ESCAPE:
        return SIM_KEY_ESC;

    case SDLK_SPACE:
        return SIM_KEY_SPACE;

    case SDLK_QUOTE:
        return SIM_KEY_SINGLE_QUOTE;

    case SDLK_COMMA:
        return SIM_KEY_COMMA;

    case SDLK_MINUS:
        return SIM_KEY_MINUS;

    case SDLK_PERIOD:
        return SIM_KEY_PERIOD;

    case SDLK_SLASH:
        return SIM_KEY_SLASH;

    case SDLK_0:
        return SIM_KEY_0;

    case SDLK_1:
        return SIM_KEY_1;

    case SDLK_2:
        return SIM_KEY_2;

    case SDLK_3:
        return SIM_KEY_3;

    case SDLK_4:
        return SIM_KEY_4;

    case SDLK_5:
        return SIM_KEY_5;

    case SDLK_6:
        return SIM_KEY_6;

    case SDLK_7:
        return SIM_KEY_7;

    case SDLK_8:
        return SIM_KEY_8;

    case SDLK_9:
        return SIM_KEY_9;

    case SDLK_SEMICOLON:
        return SIM_KEY_SEMICOLON;

    case SDLK_EQUALS:
        return SIM_KEY_EQUALS;

    case SDLK_LEFTBRACKET:
        return SIM_KEY_LEFT_BRACKET;

    case SDLK_BACKSLASH:
        return SIM_KEY_BACKSLASH;

    case SDLK_RIGHTBRACKET:
        return SIM_KEY_RIGHT_BRACKET;

    case SDLK_BACKQUOTE:
        return SIM_KEY_BACKQUOTE;

    case SDLK_a:
        return SIM_KEY_A;

    case SDLK_b:
        return SIM_KEY_B;

    case SDLK_c:
        return SIM_KEY_C;

    case SDLK_d:
        return SIM_KEY_D;

    case SDLK_e:
        return SIM_KEY_E;

    case SDLK_f:
        return SIM_KEY_F;

    case SDLK_g:
        return SIM_KEY_G;

    case SDLK_h:
        return SIM_KEY_H;

    case SDLK_i:
        return SIM_KEY_I;

    case SDLK_j:
        return SIM_KEY_J;

    case SDLK_k:
        return SIM_KEY_K;

    case SDLK_l:
        return SIM_KEY_L;

    case SDLK_m:
        return SIM_KEY_M;

    case SDLK_n:
        return SIM_KEY_N;

    case SDLK_o:
        return SIM_KEY_O;

    case SDLK_p:
        return SIM_KEY_P;

    case SDLK_q:
        return SIM_KEY_Q;

    case SDLK_r:
        return SIM_KEY_R;

    case SDLK_s:
        return SIM_KEY_S;

    case SDLK_t:
        return SIM_KEY_T;

    case SDLK_u:
        return SIM_KEY_U;

    case SDLK_v:
        return SIM_KEY_V;

    case SDLK_w:
        return SIM_KEY_W;

    case SDLK_x:
        return SIM_KEY_X;

    case SDLK_y:
        return SIM_KEY_Y;

    case SDLK_z:
        return SIM_KEY_Z;

    case SDLK_DELETE:
        return SIM_KEY_DELETE;

    case SDLK_KP0:
        return SIM_KEY_KP_INSERT;

    case SDLK_KP1:
        return SIM_KEY_KP_END;

    case SDLK_KP2:
        return SIM_KEY_KP_DOWN;

    case SDLK_KP3:
        return SIM_KEY_KP_PAGE_DOWN;

    case SDLK_KP4:
        return SIM_KEY_KP_LEFT;

    case SDLK_KP5:
        return SIM_KEY_KP_5;

    case SDLK_KP6:
        return SIM_KEY_KP_RIGHT;

    case SDLK_KP7:
        return SIM_KEY_KP_HOME;

    case SDLK_KP8:
        return SIM_KEY_KP_UP;

    case SDLK_KP9:
        return SIM_KEY_KP_PAGE_UP;

    case SDLK_KP_PERIOD:
        return SIM_KEY_KP_DELETE;

    case SDLK_KP_DIVIDE:
        return SIM_KEY_KP_DIVIDE;

    case SDLK_KP_MULTIPLY:
        return SIM_KEY_KP_MULTIPLY;

    case SDLK_KP_MINUS:
        return SIM_KEY_KP_SUBTRACT;

    case SDLK_KP_PLUS:
        return SIM_KEY_KP_ADD;

    case SDLK_KP_ENTER:
        return SIM_KEY_KP_ENTER;

    case SDLK_UP:
        return SIM_KEY_UP;

    case SDLK_DOWN:
        return SIM_KEY_DOWN;

    case SDLK_RIGHT:
        return SIM_KEY_RIGHT;

    case SDLK_LEFT:
        return SIM_KEY_LEFT;

    case SDLK_INSERT:
        return SIM_KEY_INSERT;

    case SDLK_HOME:
        return SIM_KEY_HOME;

    case SDLK_END:
        return SIM_KEY_END;

    case SDLK_PAGEUP:
        return SIM_KEY_PAGE_UP;

    case SDLK_PAGEDOWN:
        return SIM_KEY_PAGE_DOWN;

    case SDLK_F1:
        return SIM_KEY_F1;

    case SDLK_F2:
        return SIM_KEY_F2;

    case SDLK_F3:
        return SIM_KEY_F3;

    case SDLK_F4:
        return SIM_KEY_F4;

    case SDLK_F5:
        return SIM_KEY_F5;

    case SDLK_F6:
        return SIM_KEY_F6;

    case SDLK_F7:
        return SIM_KEY_F7;

    case SDLK_F8:
        return SIM_KEY_F8;

    case SDLK_F9:
        return SIM_KEY_F9;

    case SDLK_F10:
        return SIM_KEY_F10;

    case SDLK_F11:
        return SIM_KEY_F11;

    case SDLK_F12:
        return SIM_KEY_F12;

    case SDLK_NUMLOCK:
        return SIM_KEY_NUM_LOCK;

    case SDLK_CAPSLOCK:
        return SIM_KEY_CAPS_LOCK;

    case SDLK_SCROLLOCK:
        return SIM_KEY_SCRL_LOCK;

    case SDLK_RSHIFT:
        return SIM_KEY_SHIFT_R;

    case SDLK_LSHIFT:
        return SIM_KEY_SHIFT_L;

    case SDLK_RCTRL:
        return SIM_KEY_CTRL_R;

    case SDLK_LCTRL:
        return SIM_KEY_CTRL_L;

    case SDLK_RALT:
        return SIM_KEY_ALT_R;

    case SDLK_LALT:
        return SIM_KEY_ALT_L;

    case SDLK_RMETA:
        return SIM_KEY_ALT_R;

    case SDLK_LMETA:
        return SIM_KEY_WIN_L;

    case SDLK_LSUPER:
        return SIM_KEY_WIN_L;

    case SDLK_RSUPER:
        return SIM_KEY_WIN_R;

    case SDLK_PRINT:
        return SIM_KEY_PRINT;

    case SDLK_BREAK:
        return SIM_KEY_PAUSE;

    case SDLK_MENU:
        return SIM_KEY_MENU;

    default:
        return SIM_KEY_UNKNOWN;
        }
}

void vid_key (SDL_KeyboardEvent *event)
{
SIM_KEY_EVENT ev;

if (vid_mouse_captured) {
    static Uint8 *KeyStates = NULL;
    static int numkeys;

    if (!KeyStates)
        KeyStates = SDL_GetKeyState(&numkeys);
    if ((event->state == SDL_PRESSED) && KeyStates[SDLK_RSHIFT] && (KeyStates[SDLK_LCTRL] || KeyStates[SDLK_RCTRL])) {
        sim_debug (SIM_VID_DBG_KEY, vid_dev, "vid_key() - Cursor Release\n");
        SDL_WM_GrabInput (SDL_GRAB_OFF);                /* relese cursor */
        SDL_ShowCursor (SDL_ENABLE);                    /* show cursor */
        vid_mouse_captured = FALSE;
        return;
        }
    }
if (!sim_is_running)
    return;
if (SDL_SemWait (vid_key_events.sem) == 0) {
    if (vid_key_events.count < MAX_EVENTS) {
        sim_debug (SIM_VID_DBG_KEY, vid_dev, "Keyboard Event: State: %d, Keysym: %d\n", event->state, event->keysym);
        if (event->state == SDL_PRESSED) {
            if (!vid_key_state[event->keysym.sym]) {    /* Key was not down before */
                vid_key_state[event->keysym.sym] = TRUE;
                ev.key = vid_map_key (event->keysym.sym);
                ev.state = SIM_KEYPRESS_DOWN;
                }
            else {
                ev.key = vid_map_key (event->keysym.sym);
                ev.state = SIM_KEYPRESS_REPEAT;
                }
            }
        else {
            vid_key_state[event->keysym.sym] = FALSE;
            ev.key = vid_map_key (event->keysym.sym);
            ev.state = SIM_KEYPRESS_UP;
            }
        vid_key_events.events[vid_key_events.tail++] = ev;
        vid_key_events.count++;
        if (vid_key_events.tail == MAX_EVENTS)
            vid_key_events.tail = 0;
        }
    else {
        sim_debug (SIM_VID_DBG_KEY, vid_dev, "Keyboard Event DISCARDED: State: %d, Keysym: %d\n", event->state, event->keysym);
        }
    SDL_SemPost (vid_key_events.sem);
    }
}

void vid_mouse_move (SDL_MouseMotionEvent *event)
{
SDL_Event dummy_event;
int32 cx;
int32 cy;
SIM_MOUSE_EVENT ev;

if (!vid_mouse_captured)
    return;

if ((event->x == 0) ||
    (event->y == 0) ||
    (event->x == (vid_width - 1)) ||
    (event->y == (vid_height - 1))) {                   /* reached edge of window? */
    cx = vid_width / 2;
    cy = vid_height / 2;
    SDL_WarpMouse (cx, cy);                             /* back to centre */
    SDL_PumpEvents ();
    while (SDL_PeepEvents (&dummy_event, 1, SDL_GETEVENT, SDL_MOUSEMOTIONMASK)) {};
    }
if (!sim_is_running)
    return;
vid_mouse_xrel += event->xrel;                          /* update cumulative x rel */
vid_mouse_yrel -= event->yrel;                          /* update cumulative y rel */
vid_mouse_b1 = (event->state & SDL_BUTTON(SDL_BUTTON_LEFT)) ? TRUE : FALSE;
vid_mouse_b2 = (event->state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ? TRUE : FALSE;
vid_mouse_b3 = (event->state & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? TRUE : FALSE;
if (SDL_SemWait (vid_mouse_events.sem) == 0) {
    sim_debug (SIM_VID_DBG_MOUSE, vid_dev, "Mouse Move Event: (%d,%d)\n", event->xrel, event->yrel);
    if (vid_mouse_events.count < MAX_EVENTS) {
        ev.x_rel = event->xrel;
        ev.y_rel = (-event->yrel);
        ev.b1_state = vid_mouse_b1;
        ev.b2_state = vid_mouse_b2;
        ev.b3_state = vid_mouse_b3;
        vid_mouse_events.events[vid_mouse_events.tail++] = ev;
        vid_mouse_events.count++;
        if (vid_mouse_events.tail == MAX_EVENTS)
            vid_mouse_events.tail = 0;
        }
    SDL_SemPost (vid_mouse_events.sem);
    }
}

void vid_mouse_button (SDL_MouseButtonEvent *event)
{
SDL_Event dummy_event;
int32 cx;
int32 cy;
SIM_MOUSE_EVENT ev;
t_bool state;

if (!vid_mouse_captured) {
    if ((event->state == SDL_PRESSED) &&
        (event->button == SDL_BUTTON_LEFT)) {               /* left click and cursor not captured? */
        sim_debug (SIM_VID_DBG_KEY, vid_dev, "vid_mouse_button() - Cursor Captured\n");
        SDL_WM_GrabInput (SDL_GRAB_ON);                     /* lock cursor to window */
        SDL_ShowCursor (SDL_DISABLE);                       /* hide cursor */
        cx = vid_width / 2;
        cy = vid_height / 2;
        SDL_WarpMouse (cx, cy);                             /* move cursor to centre of window */
        SDL_PumpEvents ();
        while (SDL_PeepEvents (&dummy_event, 1, SDL_GETEVENT, SDL_MOUSEMOTIONMASK)) {};
        vid_mouse_captured = TRUE;
        }
    return;
    }
if (!sim_is_running)
    return;
state = (event->state == SDL_PRESSED) ? TRUE : FALSE;
switch (event->button) {
    case SDL_BUTTON_LEFT:
        vid_mouse_b1 = state;
        break;
    case SDL_BUTTON_MIDDLE:
        vid_mouse_b2 = state;
        break;
    case SDL_BUTTON_RIGHT:
        vid_mouse_b3 = state;
        break;
        }
if (SDL_SemWait (vid_mouse_events.sem) == 0) {
    sim_debug (SIM_VID_DBG_MOUSE, vid_dev, "Mouse Button Event: State: %d, Button: %d, (%d,%d)\n", event->state, event->button, event->x, event->y);
    if (vid_mouse_events.count < MAX_EVENTS) {
        ev.x_rel = 0;
        ev.y_rel = 0;
        ev.b1_state = vid_mouse_b1;
        ev.b2_state = vid_mouse_b2;
        ev.b3_state = vid_mouse_b3;
        vid_mouse_events.events[vid_mouse_events.tail++] = ev;
        vid_mouse_events.count++;
        if (vid_mouse_events.tail == MAX_EVENTS)
            vid_mouse_events.tail = 0;
        }
    SDL_SemPost (vid_mouse_events.sem);
    }
}

void vid_update (void)
{
SDL_Rect vid_dst;

vid_dst.x = 0;
vid_dst.y = 0;
vid_dst.w = vid_width;
vid_dst.h = vid_height;

sim_debug (SIM_VID_DBG_VIDEO, vid_dev, "Video Update Event: \n");
SDL_BlitSurface (vid_image, NULL, vid_window, &vid_dst);
SDL_UpdateRects (vid_window, 1, &vid_dst);
}

int vid_thread (void* arg)
{
SDL_Event event;
static char *eventtypes[] = {
    "NOEVENT",			/**< Unused (do not remove) */
    "ACTIVEEVENT",			/**< Application loses/gains visibility */
    "KEYDOWN",			/**< Keys pressed */
    "KEYUP",			/**< Keys released */
    "MOUSEMOTION",			/**< Mouse moved */
    "MOUSEBUTTONDOWN",		/**< Mouse button pressed */
    "MOUSEBUTTONUP",		/**< Mouse button released */
    "JOYAXISMOTION",		/**< Joystick axis motion */
    "JOYBALLMOTION",		/**< Joystick trackball motion */
    "JOYHATMOTION",		/**< Joystick hat position change */
    "JOYBUTTONDOWN",		/**< Joystick button pressed */
    "JOYBUTTONUP",			/**< Joystick button released */
    "QUIT",			/**< User-requested quit */
    "SYSWMEVENT",			/**< System specific event */
    "EVENT_RESERVEDA",		/**< Reserved for future use.. */
    "EVENT_RESERVEDB",		/**< Reserved for future use.. */
    "VIDEORESIZE",			/**< User resized video mode */
    "VIDEOEXPOSE",			/**< Screen needs to be redrawn */
    "EVENT_RESERVED2",		/**< Reserved for future use.. */
    "EVENT_RESERVED3",		/**< Reserved for future use.. */
    "EVENT_RESERVED4",		/**< Reserved for future use.. */
    "EVENT_RESERVED5",		/**< Reserved for future use.. */
    "EVENT_RESERVED6",		/**< Reserved for future use.. */
    "EVENT_RESERVED7",		/**< Reserved for future use.. */
    "USEREVENT",            /** Events SDL_USEREVENT(24) through SDL_MAXEVENTS-1(31) are for your use */
    "",
    "",
    "",
    "",
    "",
    "",
    ""
    };

sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_thread() - Starting\n");

SDL_Init (SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE);

vid_window = SDL_SetVideoMode (vid_width, vid_height, 8, 0);

SDL_EnableKeyRepeat (SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

if (sim_end)
    vid_image = SDL_CreateRGBSurface (SDL_SWSURFACE, vid_width, vid_height, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
else
    vid_image = SDL_CreateRGBSurface (SDL_SWSURFACE, vid_width, vid_height, 32, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff);

vid_mono_palette[0] = sim_end ? 0xFF000000 : 0x000000FF;
vid_mono_palette[1] = 0xFFFFFFFF;


SDL_WM_SetCaption (&sim_name[0], &sim_name[0]);

memset (&vid_key_state, 0, sizeof(vid_key_state));

sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_thread() - Started\n");

while (vid_active) {
    if (SDL_WaitEvent (&event)) {
        switch (event.type) {

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                vid_key ((SDL_KeyboardEvent*)&event);
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                vid_mouse_button ((SDL_MouseButtonEvent*)&event);
                break;

            case SDL_MOUSEMOTION:
                vid_mouse_move ((SDL_MouseMotionEvent*)&event);
                break;

            case SDL_USEREVENT:
                if (event.user.code == EVENT_REDRAW)
                    vid_update ();
                break;

            default:
                sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_thread() - Ignored Event: Type: %s(%d)\n", eventtypes[event.type], event.type);
                break;
            }
        }
    }
SDL_Quit ();
sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_thread() - Exiting\n");
return 0;
}
#else                       /* libSDL2 implementation */
t_bool vid_key_state[SDL_NUM_SCANCODES];
t_bool vid_mouse_captured;
int32 vid_width;
int32 vid_height;
SDL_Texture *vid_texture;                               /* video buffer in GPU */
SDL_Window *vid_window;                                 /* window handle */
SDL_Renderer *vid_renderer;
SDL_Thread *vid_thread_handle;                          /* event thread handle */
uint32 vid_mono_palette[2];                             /* Monochrome Color Map */
SDL_Color vid_colors[256];
KEY_EVENT_QUEUE vid_key_events;                         /* keyboard events */
MOUSE_EVENT_QUEUE vid_mouse_events;                     /* mouse events */
DEVICE *vid_dev;

t_stat vid_open (DEVICE *dptr, uint32 width, uint32 height)
{
if (!vid_active) {
    int wait_count = 0;

    vid_active = TRUE;
    vid_width = width;
    vid_height = height;
    vid_mouse_captured = FALSE;
    vid_mouse_xrel = 0;
    vid_mouse_yrel = 0;

    vid_key_events.head = 0;
    vid_key_events.tail = 0;
    vid_key_events.count = 0;
    vid_key_events.sem = SDL_CreateSemaphore (1);
    vid_mouse_events.head = 0;
    vid_mouse_events.tail = 0;
    vid_mouse_events.count = 0;
    vid_mouse_events.sem = SDL_CreateSemaphore (1);

    vid_dev = dptr;

    vid_thread_handle = SDL_CreateThread (vid_thread, "vid-thread", NULL);
    if (vid_thread_handle == NULL) {
        vid_close ();
        return SCPE_OPENERR;
        }
    while ((!vid_texture) && (++wait_count < 20))
        sim_os_ms_sleep (100);
    if (!vid_texture) {
        vid_close ();
        return SCPE_OPENERR;
        }

    sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_open() - Success\n");
    }
return SCPE_OK;
}

t_stat vid_close (void)
{
SDL_Event user_event;
int status;

if (vid_active) {
    vid_active = FALSE;
    if (vid_thread_handle) {
        sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_close()\n");
        user_event.type = SDL_USEREVENT;
        user_event.user.code = EVENT_CLOSE;
        user_event.user.data1 = NULL;
        user_event.user.data2 = NULL;

        SDL_PushEvent (&user_event);
        SDL_WaitThread (vid_thread_handle, &status);
        vid_thread_handle = NULL;
        vid_dev = NULL;
        }
    if (vid_mouse_events.sem) {
        SDL_DestroySemaphore(vid_mouse_events.sem);
        vid_mouse_events.sem = NULL;
        }
    if (vid_key_events.sem) {
        SDL_DestroySemaphore(vid_key_events.sem);
        vid_key_events.sem = NULL;
        }
    }
return SCPE_OK;
}

t_stat vid_poll_kb (SIM_KEY_EVENT *ev)
{
if (SDL_SemTryWait (vid_key_events.sem) == 0) {         /* get lock */
    if (vid_key_events.count > 0) {                     /* events in queue? */
        *ev = vid_key_events.events[vid_key_events.head++];
        vid_key_events.count--;
        if (vid_key_events.head == MAX_EVENTS)
            vid_key_events.head = 0;
        SDL_SemPost (vid_key_events.sem);
        return SCPE_OK;
        }
    SDL_SemPost (vid_key_events.sem);
    }
return SCPE_EOF;
}

t_stat vid_poll_mouse (SIM_MOUSE_EVENT *ev)
{
if (SDL_SemTryWait (vid_mouse_events.sem) == 0) {
    if (vid_mouse_events.count > 0) {
        *ev = vid_mouse_events.events[vid_mouse_events.head++];
        vid_mouse_events.count--;
        if (vid_mouse_events.head == MAX_EVENTS)
            vid_mouse_events.head = 0;
        SDL_SemPost (vid_mouse_events.sem);
        return SCPE_OK;
        }
    SDL_SemPost (vid_mouse_events.sem);
    }
return SCPE_EOF;
}

void vid_draw (int32 x, int32 y, int32 w, int32 h, uint32 *buf)
{
int32 i;
uint32 *pixels;
int pitch;

SDL_LockTexture(vid_texture, NULL, (void *)&pixels, &pitch);

for (i = y; i < (y + h); i++)
    memcpy (pixels + (i * vid_width) + x, buf, (size_t)w*sizeof(*pixels));
SDL_UnlockTexture(vid_texture);
}

void vid_refresh (void)
{
SDL_Event user_event;

user_event.type = SDL_USEREVENT;
user_event.user.code = EVENT_REDRAW;
user_event.user.data1 = NULL;
user_event.user.data2 = NULL;

SDL_PushEvent (&user_event);
}

int vid_map_key (int key)
{
switch (key) {

    case SDLK_BACKSPACE:
        return SIM_KEY_BACKSPACE;

    case SDLK_TAB:
        return SIM_KEY_TAB;

    case SDLK_RETURN:
        return SIM_KEY_ENTER;

    case SDLK_ESCAPE:
        return SIM_KEY_ESC;

    case SDLK_SPACE:
        return SIM_KEY_SPACE;

    case SDLK_QUOTE:
        return SIM_KEY_SINGLE_QUOTE;

    case SDLK_COMMA:
        return SIM_KEY_COMMA;

    case SDLK_MINUS:
        return SIM_KEY_MINUS;

    case SDLK_PERIOD:
        return SIM_KEY_PERIOD;

    case SDLK_SLASH:
        return SIM_KEY_SLASH;

    case SDLK_0:
        return SIM_KEY_0;

    case SDLK_1:
        return SIM_KEY_1;

    case SDLK_2:
        return SIM_KEY_2;

    case SDLK_3:
        return SIM_KEY_3;

    case SDLK_4:
        return SIM_KEY_4;

    case SDLK_5:
        return SIM_KEY_5;

    case SDLK_6:
        return SIM_KEY_6;

    case SDLK_7:
        return SIM_KEY_7;

    case SDLK_8:
        return SIM_KEY_8;

    case SDLK_9:
        return SIM_KEY_9;

    case SDLK_SEMICOLON:
        return SIM_KEY_SEMICOLON;

    case SDLK_EQUALS:
        return SIM_KEY_EQUALS;

    case SDLK_LEFTBRACKET:
        return SIM_KEY_LEFT_BRACKET;

    case SDLK_BACKSLASH:
        return SIM_KEY_BACKSLASH;

    case SDLK_RIGHTBRACKET:
        return SIM_KEY_RIGHT_BRACKET;

    case SDLK_BACKQUOTE:
        return SIM_KEY_BACKQUOTE;

    case SDLK_a:
        return SIM_KEY_A;

    case SDLK_b:
        return SIM_KEY_B;

    case SDLK_c:
        return SIM_KEY_C;

    case SDLK_d:
        return SIM_KEY_D;

    case SDLK_e:
        return SIM_KEY_E;

    case SDLK_f:
        return SIM_KEY_F;

    case SDLK_g:
        return SIM_KEY_G;

    case SDLK_h:
        return SIM_KEY_H;

    case SDLK_i:
        return SIM_KEY_I;

    case SDLK_j:
        return SIM_KEY_J;

    case SDLK_k:
        return SIM_KEY_K;

    case SDLK_l:
        return SIM_KEY_L;

    case SDLK_m:
        return SIM_KEY_M;

    case SDLK_n:
        return SIM_KEY_N;

    case SDLK_o:
        return SIM_KEY_O;

    case SDLK_p:
        return SIM_KEY_P;

    case SDLK_q:
        return SIM_KEY_Q;

    case SDLK_r:
        return SIM_KEY_R;

    case SDLK_s:
        return SIM_KEY_S;

    case SDLK_t:
        return SIM_KEY_T;

    case SDLK_u:
        return SIM_KEY_U;

    case SDLK_v:
        return SIM_KEY_V;

    case SDLK_w:
        return SIM_KEY_W;

    case SDLK_x:
        return SIM_KEY_X;

    case SDLK_y:
        return SIM_KEY_Y;

    case SDLK_z:
        return SIM_KEY_Z;

    case SDLK_DELETE:
        return SIM_KEY_DELETE;

    case SDLK_KP_0:
        return SIM_KEY_KP_INSERT;

    case SDLK_KP_1:
        return SIM_KEY_KP_END;

    case SDLK_KP_2:
        return SIM_KEY_KP_DOWN;

    case SDLK_KP_3:
        return SIM_KEY_KP_PAGE_DOWN;

    case SDLK_KP_4:
        return SIM_KEY_KP_LEFT;

    case SDLK_KP_5:
        return SIM_KEY_KP_5;

    case SDLK_KP_6:
        return SIM_KEY_KP_RIGHT;

    case SDLK_KP_7:
        return SIM_KEY_KP_HOME;

    case SDLK_KP_8:
        return SIM_KEY_KP_UP;

    case SDLK_KP_9:
        return SIM_KEY_KP_PAGE_UP;

    case SDLK_KP_PERIOD:
        return SIM_KEY_KP_DELETE;

    case SDLK_KP_DIVIDE:
        return SIM_KEY_KP_DIVIDE;

    case SDLK_KP_MULTIPLY:
        return SIM_KEY_KP_MULTIPLY;

    case SDLK_KP_MINUS:
        return SIM_KEY_KP_SUBTRACT;

    case SDLK_KP_PLUS:
        return SIM_KEY_KP_ADD;

    case SDLK_KP_ENTER:
        return SIM_KEY_KP_ENTER;

    case SDLK_UP:
        return SIM_KEY_UP;

    case SDLK_DOWN:
        return SIM_KEY_DOWN;

    case SDLK_RIGHT:
        return SIM_KEY_RIGHT;

    case SDLK_LEFT:
        return SIM_KEY_LEFT;

    case SDLK_INSERT:
        return SIM_KEY_INSERT;

    case SDLK_HOME:
        return SIM_KEY_HOME;

    case SDLK_END:
        return SIM_KEY_END;

    case SDLK_PAGEUP:
        return SIM_KEY_PAGE_UP;

    case SDLK_PAGEDOWN:
        return SIM_KEY_PAGE_DOWN;

    case SDLK_F1:
        return SIM_KEY_F1;

    case SDLK_F2:
        return SIM_KEY_F2;

    case SDLK_F3:
        return SIM_KEY_F3;

    case SDLK_F4:
        return SIM_KEY_F4;

    case SDLK_F5:
        return SIM_KEY_F5;

    case SDLK_F6:
        return SIM_KEY_F6;

    case SDLK_F7:
        return SIM_KEY_F7;

    case SDLK_F8:
        return SIM_KEY_F8;

    case SDLK_F9:
        return SIM_KEY_F9;

    case SDLK_F10:
        return SIM_KEY_F10;

    case SDLK_F11:
        return SIM_KEY_F11;

    case SDLK_F12:
        return SIM_KEY_F12;

    case SDLK_NUMLOCKCLEAR:
        return SIM_KEY_NUM_LOCK;

    case SDLK_CAPSLOCK:
        return SIM_KEY_CAPS_LOCK;

    case SDLK_SCROLLLOCK:
        return SIM_KEY_SCRL_LOCK;

    case SDLK_RSHIFT:
        return SIM_KEY_SHIFT_R;

    case SDLK_LSHIFT:
        return SIM_KEY_SHIFT_L;

    case SDLK_RCTRL:
        return SIM_KEY_CTRL_R;

    case SDLK_LCTRL:
        return SIM_KEY_CTRL_L;

    case SDLK_RALT:
        return SIM_KEY_ALT_R;

    case SDLK_LALT:
        return SIM_KEY_ALT_L;

    case SDLK_LGUI:
        return SIM_KEY_WIN_L;

    case SDLK_RGUI:
        return SIM_KEY_WIN_R;

    case SDLK_PRINTSCREEN:
        return SIM_KEY_PRINT;

    case SDLK_PAUSE:
        return SIM_KEY_PAUSE;

    case SDLK_MENU:
        return SIM_KEY_MENU;

    default:
        return SIM_KEY_UNKNOWN;
        }
}

void vid_key (SDL_KeyboardEvent *event)
{
SIM_KEY_EVENT ev;

if (vid_mouse_captured) {
    static const Uint8 *KeyStates = NULL;
    static int numkeys;

    if (!KeyStates)
        KeyStates = SDL_GetKeyboardState(&numkeys);
    if ((event->state == SDL_PRESSED) && KeyStates[SDL_SCANCODE_RSHIFT] && (KeyStates[SDL_SCANCODE_LCTRL] || KeyStates[SDL_SCANCODE_RCTRL])) {
        sim_debug (SIM_VID_DBG_KEY, vid_dev, "vid_key() - Cursor Release\n");
        SDL_SetRelativeMouseMode(SDL_FALSE);            /* release cursor, show cursor */
        vid_mouse_captured = FALSE;
        return;
        }
    }
if (!sim_is_running)
    return;
if (SDL_SemWait (vid_key_events.sem) == 0) {
    sim_debug (SIM_VID_DBG_KEY, vid_dev, "Keyboard Event: State: %d, Keysym(scancode,sym): (%d,%d)\n", event->state, event->keysym.scancode, event->keysym.sym);
    if (vid_key_events.count < MAX_EVENTS) {
        if (event->state == SDL_PRESSED) {
            if (!vid_key_state[event->keysym.scancode]) {    /* Key was not down before */
                vid_key_state[event->keysym.scancode] = TRUE;
                ev.key = vid_map_key (event->keysym.sym);
                ev.state = SIM_KEYPRESS_DOWN;
                }
            else {
                ev.key = vid_map_key (event->keysym.sym);
                ev.state = SIM_KEYPRESS_REPEAT;
                }
            }
        else {
            vid_key_state[event->keysym.scancode] = FALSE;
            ev.key = vid_map_key (event->keysym.sym);
            ev.state = SIM_KEYPRESS_UP;
            }
        vid_key_events.events[vid_key_events.tail++] = ev;
        vid_key_events.count++;
        if (vid_key_events.tail == MAX_EVENTS)
            vid_key_events.tail = 0;
        }
    SDL_SemPost (vid_key_events.sem);
    }
}

void vid_mouse_move (SDL_MouseMotionEvent *event)
{
SDL_Event dummy_event;
int32 cx;
int32 cy;
SIM_MOUSE_EVENT ev;

if (!vid_mouse_captured)
    return;

if ((event->x == 0) ||
    (event->y == 0) ||
    (event->x == (vid_width - 1)) ||
    (event->y == (vid_height - 1))) {                   /* reached edge of window? */
    cx = vid_width / 2;
    cy = vid_height / 2;
    SDL_WarpMouseInWindow (NULL, cx, cy);               /* back to centre */
    SDL_PumpEvents ();
    while (SDL_PeepEvents (&dummy_event, 1, SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION)) {};
    }
if (!sim_is_running)
    return;
vid_mouse_xrel += event->xrel;                          /* update cumulative x rel */
vid_mouse_yrel -= event->yrel;                          /* update cumulative y rel */
vid_mouse_b1 = (event->state & SDL_BUTTON(SDL_BUTTON_LEFT)) ? TRUE : FALSE;
vid_mouse_b2 = (event->state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ? TRUE : FALSE;
vid_mouse_b3 = (event->state & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? TRUE : FALSE;
if (SDL_SemWait (vid_mouse_events.sem) == 0) {
    sim_debug (SIM_VID_DBG_MOUSE, vid_dev, "Mouse Move Event: (%d,%d)\n", event->xrel, event->yrel);
    if (vid_mouse_events.count < MAX_EVENTS) {
        ev.x_rel = event->xrel;
        ev.y_rel = (-event->yrel);
        ev.b1_state = vid_mouse_b1;
        ev.b2_state = vid_mouse_b2;
        ev.b3_state = vid_mouse_b3;
        vid_mouse_events.events[vid_mouse_events.tail++] = ev;
        vid_mouse_events.count++;
        if (vid_mouse_events.tail == MAX_EVENTS)
            vid_mouse_events.tail = 0;
        }
    SDL_SemPost (vid_mouse_events.sem);
    }
}

void vid_mouse_button (SDL_MouseButtonEvent *event)
{
SDL_Event dummy_event;
int32 cx;
int32 cy;
SIM_MOUSE_EVENT ev;
t_bool state;

if (!vid_mouse_captured) {
    if ((event->state == SDL_PRESSED) &&
        (event->button == SDL_BUTTON_LEFT)) {               /* left click and cursor not captured? */
        sim_debug (SIM_VID_DBG_KEY, vid_dev, "vid_mouse_button() - Cursor Captured\n");
        SDL_SetRelativeMouseMode(SDL_TRUE);                 /* lock cursor to window, hide cursor */
        cx = vid_width / 2;
        cy = vid_height / 2;
        SDL_WarpMouseInWindow (NULL, cx, cy);               /* back to centre */
        SDL_PumpEvents ();
        while (SDL_PeepEvents (&dummy_event, 1, SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION)) {};
        vid_mouse_captured = TRUE;
        }
    return;
    }
if (!sim_is_running)
    return;
state = (event->state == SDL_PRESSED) ? TRUE : FALSE;
switch (event->button) {
    case SDL_BUTTON_LEFT:
        vid_mouse_b1 = state;
        break;
    case SDL_BUTTON_MIDDLE:
        vid_mouse_b2 = state;
        break;
    case SDL_BUTTON_RIGHT:
        vid_mouse_b3 = state;
        break;
        }
if (SDL_SemWait (vid_mouse_events.sem) == 0) {
    sim_debug (SIM_VID_DBG_MOUSE, vid_dev, "Mouse Button Event: State: %d, Button: %d, (%d,%d)\n", event->state, event->button, event->x, event->y);
    if (vid_mouse_events.count < MAX_EVENTS) {
        ev.x_rel = 0;
        ev.y_rel = 0;
        ev.b1_state = vid_mouse_b1;
        ev.b2_state = vid_mouse_b2;
        ev.b3_state = vid_mouse_b3;
        vid_mouse_events.events[vid_mouse_events.tail++] = ev;
        vid_mouse_events.count++;
        if (vid_mouse_events.tail == MAX_EVENTS)
            vid_mouse_events.tail = 0;
        }
    SDL_SemPost (vid_mouse_events.sem);
    }
}

void vid_update (void)
{
SDL_Rect vid_dst;

vid_dst.x = 0;
vid_dst.y = 0;
vid_dst.w = vid_width;
vid_dst.h = vid_height;

sim_debug (SIM_VID_DBG_VIDEO, vid_dev, "Video Update Event: \n");
SDL_RenderClear(vid_renderer);
SDL_RenderCopy(vid_renderer, vid_texture, NULL, NULL);
SDL_RenderPresent(vid_renderer);
}

int vid_thread (void* arg)
{
SDL_Event event;
static char *eventtypes[SDL_LASTEVENT];
static t_bool initialized = FALSE;

if (!initialized) {
    initialized = TRUE;
    eventtypes[SDL_QUIT] = "QUIT";          /**< User-requested quit */

    /* These application events have special meaning on iOS, see README-ios.txt for details */
    eventtypes[SDL_APP_TERMINATING] = "APP_TERMINATING";   /**< The application is being terminated by the OS
                                     Called on iOS in applicationWillTerminate()
                                     Called on Android in onDestroy()
                                */
    eventtypes[SDL_APP_LOWMEMORY] = "APP_LOWMEMORY";          /**< The application is low on memory, free memory if possible.
                                     Called on iOS in applicationDidReceiveMemoryWarning()
                                     Called on Android in onLowMemory()
                                */
    eventtypes[SDL_APP_WILLENTERBACKGROUND] = "APP_WILLENTERBACKGROUND"; /**< The application is about to enter the background
                                     Called on iOS in applicationWillResignActive()
                                     Called on Android in onPause()
                                */
    eventtypes[SDL_APP_DIDENTERBACKGROUND] = "APP_DIDENTERBACKGROUND"; /**< The application did enter the background and may not get CPU for some time
                                     Called on iOS in applicationDidEnterBackground()
                                     Called on Android in onPause()
                                */
    eventtypes[SDL_APP_WILLENTERFOREGROUND] = "APP_WILLENTERFOREGROUND"; /**< The application is about to enter the foreground
                                     Called on iOS in applicationWillEnterForeground()
                                     Called on Android in onResume()
                                */
    eventtypes[SDL_APP_DIDENTERFOREGROUND] = "APP_DIDENTERFOREGROUND"; /**< The application is now interactive
                                     Called on iOS in applicationDidBecomeActive()
                                     Called on Android in onResume()
                                */

    /* Window events */
    eventtypes[SDL_WINDOWEVENT] = "WINDOWEVENT"; /**< Window state change */
    eventtypes[SDL_SYSWMEVENT] = "SYSWMEVENT";             /**< System specific event */

    /* Keyboard events */
    eventtypes[SDL_KEYDOWN] = "KEYDOWN"; /**< Key pressed */
    eventtypes[SDL_KEYUP] = "KEYUP";                  /**< Key released */
    eventtypes[SDL_TEXTEDITING] = "TEXTEDITING";            /**< Keyboard text editing (composition) */
    eventtypes[SDL_TEXTINPUT] = "TEXTINPUT";              /**< Keyboard text input */

    /* Mouse events */
    eventtypes[SDL_MOUSEMOTION] = "MOUSEMOTION"; /**< Mouse moved */
    eventtypes[SDL_MOUSEBUTTONDOWN] = "MOUSEBUTTONDOWN";        /**< Mouse button pressed */
    eventtypes[SDL_MOUSEBUTTONUP] = "MOUSEBUTTONUP";          /**< Mouse button released */
    eventtypes[SDL_MOUSEWHEEL] = "MOUSEWHEEL";             /**< Mouse wheel motion */

    /* Joystick events */
    eventtypes[SDL_JOYAXISMOTION] = "JOYAXISMOTION"; /**< Joystick axis motion */
    eventtypes[SDL_JOYBALLMOTION] = "JOYBALLMOTION";          /**< Joystick trackball motion */
    eventtypes[SDL_JOYHATMOTION] = "JOYHATMOTION";           /**< Joystick hat position change */
    eventtypes[SDL_JOYBUTTONDOWN] = "JOYBUTTONDOWN";          /**< Joystick button pressed */
    eventtypes[SDL_JOYBUTTONUP] = "JOYBUTTONUP";            /**< Joystick button released */
    eventtypes[SDL_JOYDEVICEADDED] = "JOYDEVICEADDED";         /**< A new joystick has been inserted into the system */
    eventtypes[SDL_JOYDEVICEREMOVED] = "JOYDEVICEREMOVED";       /**< An opened joystick has been removed */

    /* Game controller events */
    eventtypes[SDL_CONTROLLERAXISMOTION] = "CONTROLLERAXISMOTION"; /**< Game controller axis motion */
    eventtypes[SDL_CONTROLLERBUTTONDOWN] = "CONTROLLERBUTTONDOWN";          /**< Game controller button pressed */
    eventtypes[SDL_CONTROLLERBUTTONUP] = "CONTROLLERBUTTONUP";            /**< Game controller button released */
    eventtypes[SDL_CONTROLLERDEVICEADDED] = "CONTROLLERDEVICEADDED";         /**< A new Game controller has been inserted into the system */
    eventtypes[SDL_CONTROLLERDEVICEREMOVED] = "CONTROLLERDEVICEREMOVED";       /**< An opened Game controller has been removed */
    eventtypes[SDL_CONTROLLERDEVICEREMAPPED] = "CONTROLLERDEVICEREMAPPED";      /**< The controller mapping was updated */

    /* Touch events */
    eventtypes[SDL_FINGERDOWN] = "FINGERDOWN";
    eventtypes[SDL_FINGERUP] = "FINGERUP";
    eventtypes[SDL_FINGERMOTION] = "FINGERMOTION";

    /* Gesture events */
    eventtypes[SDL_DOLLARGESTURE] = "DOLLARGESTURE";
    eventtypes[SDL_DOLLARRECORD] = "DOLLARRECORD";
    eventtypes[SDL_MULTIGESTURE] = "MULTIGESTURE";

    /* Clipboard events */
    eventtypes[SDL_CLIPBOARDUPDATE] = "CLIPBOARDUPDATE"; /**< The clipboard changed */

    /* Drag and drop events */
    eventtypes[SDL_DROPFILE] = "DROPFILE"; /**< The system requests a file open */

    /** Events ::SDL_USEREVENT through ::SDL_LASTEVENT are for your use,
     *  and should be allocated with SDL_RegisterEvents()
     */
    eventtypes[SDL_USEREVENT] = "USEREVENT";

    }

sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_thread() - Starting\n");

vid_mono_palette[0] = sim_end ? 0xFF000000 : 0x000000FF;
vid_mono_palette[1] = 0xFFFFFFFF;

memset (&vid_key_state, 0, sizeof(vid_key_state));

SDL_Init (SDL_INIT_VIDEO);

SDL_CreateWindowAndRenderer (vid_width, vid_height, SDL_WINDOW_SHOWN, &vid_window, &vid_renderer);

if ((vid_window == NULL) || (vid_renderer == NULL)) {
    sim_printf ("%s: Error Creating Video Window: %s\b", sim_dname(vid_dev), SDL_GetError());
    SDL_Quit ();
    return 0;
    }

SDL_SetRenderDrawColor (vid_renderer, 0, 0, 0, 255);
SDL_RenderClear (vid_renderer);
SDL_RenderPresent (vid_renderer);

vid_texture = SDL_CreateTexture (vid_renderer,
                                 SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 vid_width, vid_height);
if (!vid_texture) {
    sim_printf ("%s: Error configuring Video environment: %s\b", sim_dname(vid_dev), SDL_GetError());
    SDL_DestroyRenderer(vid_renderer);
    vid_renderer = NULL;
    SDL_DestroyWindow(vid_window);
    vid_window = NULL;
    SDL_Quit ();
    return 0;
    }

SDL_StopTextInput ();

sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_thread() - Started\n");

while (vid_active) {
    if (SDL_WaitEvent (&event)) {
        switch (event.type) {

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                vid_key ((SDL_KeyboardEvent*)&event);
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                vid_mouse_button ((SDL_MouseButtonEvent*)&event);
                break;

            case SDL_MOUSEMOTION:
                vid_mouse_move ((SDL_MouseMotionEvent*)&event);
                break;

            case SDL_USEREVENT:
                /* There are 2 user events generated */
                /* EVENT_REDRAW to update the display */
                /* EVENT_CLOSE  to wake up this thread and let */
                /*              it notice vid_active has changed */
                if (event.user.code == EVENT_REDRAW)
                    vid_update ();
                break;

            default:
                sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_thread() - Ignored Event: Type: %s(%d)\n", eventtypes[event.type], event.type);
                break;
            }
        }
    }
SDL_DestroyTexture(vid_texture);
vid_texture = NULL;
SDL_DestroyRenderer(vid_renderer);
vid_renderer = NULL;
SDL_DestroyWindow(vid_window);
vid_window = NULL;
SDL_Quit ();
sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_thread() - Exiting\n");
return 0;
}
#endif      /* libSDL2 implementation */

const char *vid_version(void)
{
static char SDLVersion[80];
SDL_version compiled, running;

#if SDL_MAJOR_VERSION == 1
const SDL_version *ver = SDL_Linked_Version();
running.major = ver->major;
running.minor = ver->minor;
running.patch = ver->patch;
#else
SDL_GetVersion(&running);
#endif
SDL_VERSION(&compiled);

if ((compiled.major == running.major) &&
    (compiled.minor == running.minor) &&
    (compiled.patch == running.patch))
    sprintf(SDLVersion, "SDL Version %d.%d.%d", 
                        compiled.major, compiled.minor, compiled.patch);
else
    sprintf(SDLVersion, "SDL Version (Compiled: %d.%d.%d, Runtime: %d.%d.%d)", 
                        compiled.major, compiled.minor, compiled.patch,
                        running.major, running.minor, running.patch);
return (const char *)SDLVersion;
}

t_stat vid_set_release_key (FILE* st, UNIT* uptr, int32 val, void* desc)
{
return SCPE_NOFNC;
}

t_stat vid_show_release_key (FILE* st, UNIT* uptr, int32 val, void* desc)
{
fprintf (st, "ReleaseKey=Ctrl-Right-Shift");
return SCPE_OK;
}

#else

/* Non-implemented versions */

uint32 vid_mono_palette[2];                             /* Monochrome Color Map */

t_stat vid_open (DEVICE *dptr, uint32 width, uint32 height)
{
return SCPE_NOFNC;
}

t_stat vid_close (void)
{
return SCPE_OK;
}

t_stat vid_poll_kb (SIM_KEY_EVENT *ev)
{
return SCPE_EOF;
}

t_stat vid_poll_mouse (SIM_MOUSE_EVENT *ev)
{
return SCPE_EOF;
}

void vid_draw (int32 x, int32 y, int32 w, int32 h, uint32 *buf)
{
return;
}

void vid_refresh (void)
{
return;
}

const char *vid_version (void)
{
return "No Video Support";
}

t_stat vid_set_release_key (FILE* st, UNIT* uptr, int32 val, void* desc)
{
return SCPE_NOFNC;
}

t_stat vid_show_release_key (FILE* st, UNIT* uptr, int32 val, void* desc)
{
fprintf (st, "no release key");
return SCPE_OK;
}


#endif

#endif /* USE_SIM_VIDEO */
