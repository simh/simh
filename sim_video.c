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

   11-Jun-2013  MB      First version
*/

#include "sim_video.h"

#if HAVE_LIBSDL
#include <SDL.h>
#include <SDL_thread.h>

extern int32 sim_is_running;

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
    t_bool b1_state;
    t_bool b2_state;
    t_bool b3_state;
    } MOUSE_EVENT_QUEUE;

int vid_thread (void* arg);

t_bool vid_active;
t_bool vid_key_state[SDLK_LAST];
t_bool vid_mouse_captured;
int32 vid_width;
int32 vid_height;
SDL_Surface *vid_image;                                 /* video buffer */
SDL_Surface *vid_window;                                /* window handle */
SDL_Thread *vid_thread_id;                              /* event thread handle */
SDL_Color vid_palette[256];
KEY_EVENT_QUEUE vid_key_events;                         /* keyboard events */
MOUSE_EVENT_QUEUE vid_mouse_events;                     /* mouse events */

t_stat vid_open (uint32 width, uint32 height)
{
if (!vid_active) {
    vid_active = TRUE;
    vid_width = width;
    vid_height = height;
    vid_mouse_captured = FALSE;

    vid_key_events.head = 0;
    vid_key_events.tail = 0;
    vid_key_events.count = 0;
    vid_key_events.sem = SDL_CreateSemaphore (1);
    vid_mouse_events.head = 0;
    vid_mouse_events.tail = 0;
    vid_mouse_events.count = 0;
    vid_mouse_events.sem = SDL_CreateSemaphore (1);

    vid_thread_id = SDL_CreateThread (vid_thread, NULL);
    if (vid_thread_id == NULL) {
        vid_active = FALSE;
        return SCPE_OPENERR;
        }
    
    vid_image = SDL_CreateRGBSurface (SDL_SWSURFACE, width, height, 8, 0, 0, 0, 0);

    vid_palette[0].r = 0;
    vid_palette[0].g = 0;
    vid_palette[0].b = 0;
    vid_palette[1].r = 255;
    vid_palette[1].g = 255;
    vid_palette[1].b = 255;
    SDL_SetColors (vid_image, vid_palette, 0, 2);

    memset (&vid_key_state, 0, sizeof(vid_key_state));
    }
return SCPE_OK;
}

t_stat vid_close (void)
{
SDL_Event user_event;

if (vid_active) {
    vid_active = FALSE;
    user_event.type = SDL_USEREVENT;
    user_event.user.code = EVENT_CLOSE;
    user_event.user.data1 = NULL;
    user_event.user.data2 = NULL;

    SDL_PushEvent (&user_event);
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

void vid_draw (int32 x, int32 y, int32 w, int32 h, uint8 *buf)
{
int32 i, j;
uint8* pix;

pix = (uint8 *)vid_image->pixels;
pix = pix + (y * vid_width) + x;

for (i = y; i < (y + h); i++) {
    for (j = x; j < (x + w); j++) {
        *pix++ = *buf++;
        }
    pix = pix + vid_width;
    }
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
if (SDL_SemWait (vid_mouse_events.sem) == 0) {
    if (vid_mouse_events.count < MAX_EVENTS) {
        ev.x_rel = event->xrel;
        ev.y_rel = (-event->yrel);
        ev.b1_state = (event->state & SDL_BUTTON(SDL_BUTTON_LEFT)) ? TRUE : FALSE;
        ev.b2_state = (event->state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ? TRUE : FALSE;
        ev.b3_state = (event->state & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? TRUE : FALSE;
        vid_mouse_events.b1_state = ev.b1_state;
        vid_mouse_events.b2_state = ev.b2_state;
        vid_mouse_events.b3_state = ev.b3_state;
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
if (SDL_SemWait (vid_mouse_events.sem) == 0) {
    if (vid_mouse_events.count < MAX_EVENTS) {
        state = (event->state == SDL_PRESSED) ? TRUE : FALSE;
        ev.x_rel = 0;
        ev.y_rel = 0;
        switch (event->button) {
            case SDL_BUTTON_LEFT:
                vid_mouse_events.b1_state = state;
                break;
            case SDL_BUTTON_MIDDLE:
                vid_mouse_events.b2_state = state;
                break;
            case SDL_BUTTON_RIGHT:
                vid_mouse_events.b3_state = state;
                break;
                }
        ev.b1_state = vid_mouse_events.b1_state;
        ev.b2_state = vid_mouse_events.b2_state;
        ev.b3_state = vid_mouse_events.b3_state;
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

SDL_BlitSurface (vid_image, NULL, vid_window, &vid_dst);
SDL_UpdateRects (vid_window, 1, &vid_dst);
}

int vid_thread (void* arg)
{
int vid_bpp = 8;
int vid_flags = 0;
SDL_Event event;

if (SDL_Init (SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE) < 0) {
    return SCPE_OPENERR;
    }

vid_window = SDL_SetVideoMode (vid_width, vid_height, vid_bpp, vid_flags);

if (vid_window == NULL) {
    return SCPE_OPENERR;
    }

if (SDL_EnableKeyRepeat (SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL) < 0) {
    return SCPE_OPENERR;
    }

SDL_WM_SetCaption (&sim_name[0], &sim_name[0]);

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
                break;
            }
        }
    }
SDL_Quit ();
return 0;
}

const char *vid_version(void)
{
static char SDLVersion[80];
const SDL_version *ver = SDL_Linked_Version();

sprintf(SDLVersion, "SDL Version %d.%d.%d", ver->major, ver->minor, ver->patch);
return (const char *)SDLVersion;
}

#else

/* Non-implemented versions */

t_stat vid_open (uint32 width, uint32 height)
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

void vid_draw (int32 x, int32 y, int32 w, int32 h, uint8 *buf)
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

#endif
