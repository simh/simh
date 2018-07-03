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

#include "sim_video.h"
#include "scp.h"

t_bool vid_active = FALSE;
int32 vid_cursor_x;
int32 vid_cursor_y;
t_bool vid_mouse_b1 = FALSE;
t_bool vid_mouse_b2 = FALSE;
t_bool vid_mouse_b3 = FALSE;
static VID_QUIT_CALLBACK vid_quit_callback = NULL;

t_stat vid_register_quit_callback (VID_QUIT_CALLBACK callback)
{
vid_quit_callback = callback;
return SCPE_OK;
}

t_stat vid_show (FILE* st, DEVICE *dptr,  UNIT* uptr, int32 val, CONST char* desc)
{
return vid_show_video (st, uptr, val, desc);
}

#if defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)

char vid_release_key[64] = "Ctrl-Right-Shift";

#include <SDL.h>
#include <SDL_thread.h>

static const char *key_names[] = 
    {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", 
     "0",   "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9",    
     "A",   "B",  "C",  "D",  "E",  "F",  "G",  "H",  "I",  "J", 
     "K",   "L",  "M",  "N",  "O",  "P",  "Q",  "R",  "S",  "T", 
     "U",   "V",  "W",  "X",  "Y",  "Z", 
     "BACKQUOTE",   "MINUS",   "EQUALS", "LEFT_BRACKET", "RIGHT_BRACKET", 
     "SEMICOLON", "SINGLE_QUOTE", "BACKSLASH", "LEFT_BACKSLASH", "COMMA", 
     "PERIOD", "SLASH", "PRINT", "SCRL_LOCK", "PAUSE", "ESC", "BACKSPACE", 
     "TAB", "ENTER", "SPACE", "INSERT", "DELETE", "HOME", "END", "PAGE_UP", 
     "PAGE_DOWN", "UP", "DOWN", "LEFT", "RIGHT", "CAPS_LOCK", "NUM_LOCK", 
     "ALT_L", "ALT_R", "CTRL_L", "CTRL_R", "SHIFT_L", "SHIFT_R", 
     "WIN_L", "WIN_R", "MENU", "KP_ADD", "KP_SUBTRACT", "KP_END", "KP_DOWN", 
     "KP_PAGE_DOWN", "KP_LEFT", "KP_RIGHT", "KP_HOME", "KP_UP", "KP_PAGE_UP", 
     "KP_INSERT", "KP_DELETE", "KP_5", "KP_ENTER", "KP_MULTIPLY", "KP_DIVIDE"
     };

const char *vid_key_name (int32 key)
{
static char tmp_key_name[40];

    if (key < sizeof(key_names)/sizeof(key_names[0]))
        sprintf (tmp_key_name, "SIM_KEY_%s", key_names[key]);
    else
        sprintf (tmp_key_name, "UNKNOWN KEY: %d", key);
    return tmp_key_name;
}

#if defined(HAVE_LIBPNG)
/* From: https://github.com/driedfruit/SDL_SavePNG */

/*
 * Save an SDL_Surface as a PNG file.
 *
 * Returns 0 success or -1 on failure, the error message is then retrievable
 * via SDL_GetError().
 */
#define SDL_SavePNG(surface, file) \
        SDL_SavePNG_RW(surface, SDL_RWFromFile(file, "wb"), 1)

/*
 * SDL_SavePNG -- libpng-based SDL_Surface writer.
 *
 * This code is free software, available under zlib/libpng license.
 * http://www.libpng.org/pub/png/src/libpng-LICENSE.txt
 */
#include <SDL.h>
#include <png.h>

#define SUCCESS 0
#define ERROR -1

#define USE_ROW_POINTERS

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define rmask 0xFF000000
#define gmask 0x00FF0000
#define bmask 0x0000FF00
#define amask 0x000000FF
#else
#define rmask 0x000000FF
#define gmask 0x0000FF00
#define bmask 0x00FF0000
#define amask 0xFF000000
#endif

/* libpng callbacks */ 
static void png_error_SDL(png_structp ctx, png_const_charp str)
{
    SDL_SetError("libpng: %s\n", str);
}
static void png_write_SDL(png_structp png_ptr, png_bytep data, png_size_t length)
{
    SDL_RWops *rw = (SDL_RWops*)png_get_io_ptr(png_ptr);
    SDL_RWwrite(rw, data, sizeof(png_byte), length);
}

static SDL_Surface *SDL_PNGFormatAlpha(SDL_Surface *src) 
{
    SDL_Surface *surf;
    SDL_Rect rect = { 0 };

    /* NO-OP for images < 32bpp and 32bpp images that already have Alpha channel */ 
    if (src->format->BitsPerPixel <= 24 || src->format->Amask) {
        src->refcount++;
        return src;
    }

    /* Convert 32bpp alpha-less image to 24bpp alpha-less image */
    rect.w = src->w;
    rect.h = src->h;
    surf = SDL_CreateRGBSurface(src->flags, src->w, src->h, 24,
        src->format->Rmask, src->format->Gmask, src->format->Bmask, 0);
    SDL_LowerBlit(src, &rect, surf, &rect);

    return surf;
}

static int SDL_SavePNG_RW(SDL_Surface *surface, SDL_RWops *dst, int freedst) 
{
    png_structp png_ptr;
    png_infop info_ptr;
    png_colorp pal_ptr;
    SDL_Palette *pal;
    int i, colortype;
#ifdef USE_ROW_POINTERS
    png_bytep *row_pointers;
#endif
    /* Initialize and do basic error checking */
    if (!dst)
    {
        SDL_SetError("Argument 2 to SDL_SavePNG_RW can't be NULL, expecting SDL_RWops*\n");
        return (ERROR);
    }
    if (!surface)
    {
        SDL_SetError("Argument 1 to SDL_SavePNG_RW can't be NULL, expecting SDL_Surface*\n");
        if (freedst) SDL_RWclose(dst);
        return (ERROR);
    }
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, png_error_SDL, NULL); /* err_ptr, err_fn, warn_fn */
    if (!png_ptr) 
    {
        SDL_SetError("Unable to png_create_write_struct on %s\n", PNG_LIBPNG_VER_STRING);
        if (freedst) SDL_RWclose(dst);
        return (ERROR);
    }
    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        SDL_SetError("Unable to png_create_info_struct\n");
        png_destroy_write_struct(&png_ptr, NULL);
        if (freedst) SDL_RWclose(dst);
        return (ERROR);
    }
    if (setjmp(png_jmpbuf(png_ptr)))    /* All other errors, see also "png_error_SDL" */
    {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        if (freedst) SDL_RWclose(dst);
        return (ERROR);
    }

    /* Setup our RWops writer */
    png_set_write_fn(png_ptr, dst, png_write_SDL, NULL); /* w_ptr, write_fn, flush_fn */

    /* Prepare chunks */
    colortype = PNG_COLOR_MASK_COLOR;
    if (surface->format->BytesPerPixel > 0
    &&  surface->format->BytesPerPixel <= 8
    && (pal = surface->format->palette))
    {
        colortype |= PNG_COLOR_MASK_PALETTE;
        pal_ptr = (png_colorp)malloc(pal->ncolors * sizeof(png_color));
        for (i = 0; i < pal->ncolors; i++) {
            pal_ptr[i].red   = pal->colors[i].r;
            pal_ptr[i].green = pal->colors[i].g;
            pal_ptr[i].blue  = pal->colors[i].b;
        }
        png_set_PLTE(png_ptr, info_ptr, pal_ptr, pal->ncolors);
        free(pal_ptr);
    }
    else if (surface->format->BytesPerPixel > 3 || surface->format->Amask)
        colortype |= PNG_COLOR_MASK_ALPHA;

    png_set_IHDR(png_ptr, info_ptr, surface->w, surface->h, 8, colortype,
        PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

//    png_set_packing(png_ptr);

    /* Allow BGR surfaces */
    if (surface->format->Rmask == bmask
    && surface->format->Gmask == gmask
    && surface->format->Bmask == rmask)
        png_set_bgr(png_ptr);

    /* Write everything */
    png_write_info(png_ptr, info_ptr);
#ifdef USE_ROW_POINTERS
    row_pointers = (png_bytep*) malloc(sizeof(png_bytep)*surface->h);
    for (i = 0; i < surface->h; i++)
        row_pointers[i] = (png_bytep)(Uint8*)surface->pixels + i * surface->pitch;
    png_write_image(png_ptr, row_pointers);
    free(row_pointers);
#else
    for (i = 0; i < surface->h; i++)
        png_write_row(png_ptr, (png_bytep)(Uint8*)surface->pixels + i * surface->pitch);
#endif
    png_write_end(png_ptr, info_ptr);

    /* Done */
    png_destroy_write_struct(&png_ptr, &info_ptr);
    if (freedst) SDL_RWclose(dst);
    return (SUCCESS);
}
#endif /* defined(HAVE_LIBPNG) */

/* 
    Some platforms (OS X), require that ALL input event processing be 
    performed by the main thread of the process.

    To satisfy this requirement, we leverage the SDL_MAIN functionality 
    which does:
    
             #defines main SDL_main
     
     and we define the main() entry point here.  Locally, we run the 
     application's SDL_main in a separate thread, and while that thread
     is running, the main thread performs event handling and dispatch.
     
 */

#define EVENT_REDRAW     1                              /* redraw event for SDL */
#define EVENT_CLOSE      2                              /* close event for SDL */
#define EVENT_CURSOR     3                              /* new cursor for SDL */
#define EVENT_WARP       4                              /* warp mouse position for SDL */
#define EVENT_DRAW       5                              /* draw/blit region for SDL */
#define EVENT_SHOW       6                              /* show SDL capabilities */
#define EVENT_OPEN       7                              /* vid_open request */
#define EVENT_EXIT       8                              /* program exit */
#define EVENT_SCREENSHOT 9                              /* produce screenshot of video window */
#define EVENT_BEEP      10                              /* audio beep */
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
int vid_video_events (void);
void vid_show_video_event (void);
void vid_screenshot_event (void);
void vid_beep_event (void);

/* 
   libSDL and libSDL2 have significantly different APIs.  
   The consequence is that this code has significant #ifdef sections.

   The current structure is to implement the API differences in each 
   routine that has a difference.  This allows the decision and flow 
   logic to exist once and thus to allow logic changes to be implemented 
   in one place.

 */


t_bool vid_mouse_captured;
int32 vid_flags;                                        /* Open Flags */
int32 vid_width;
int32 vid_height;
t_bool vid_ready;
char vid_title[128];
static void vid_beep_setup (int duration_ms, int tone_frequency);
static void vid_beep_cleanup (void);
#if SDL_MAJOR_VERSION == 1

/*
   Some platforms that use X11 display technology have libSDL 
   environments which need to call XInitThreads when libSDL is used
   in multi-threaded programs.  This routine attempts to locate
   the X11 shareable library and if it is found loads it and calls
   the XInitThreads routine to meet this requirement.
 */
#ifdef HAVE_DLOPEN
#include <dlfcn.h>
#endif

static void _XInitThreads (void)
{
#ifdef HAVE_DLOPEN
static void *hLib = NULL;                   /* handle to Library */
#define __STR_QUOTE(tok) #tok
#define __STR(tok) __STR_QUOTE(tok)
static const char* lib_name = "libX11." __STR(HAVE_DLOPEN);
typedef int (*_func)();
_func _func_ptr = NULL;

if (!hLib)
    hLib = dlopen(lib_name, RTLD_NOW);
if (hLib)
    _func_ptr = (_func)((size_t)dlsym(hLib, "XInitThreads"));
if (_func_ptr)
    _func_ptr();
#endif
}

t_bool vid_key_state[SDLK_LAST];
SDL_Surface *vid_image;                                 /* video buffer */
SDL_Surface *vid_window;                                /* window handle */
#else
t_bool vid_key_state[SDL_NUM_SCANCODES];
SDL_Texture *vid_texture;                               /* video buffer in GPU */
SDL_Renderer *vid_renderer;
SDL_Window *vid_window;                                 /* window handle */
uint32 vid_windowID;
#endif
SDL_Thread *vid_thread_handle = NULL;                   /* event thread handle */
SDL_Cursor *vid_cursor = NULL;                          /* current cursor */
t_bool vid_cursor_visible = FALSE;                      /* cursor visibility state */
uint32 vid_mono_palette[2];                             /* Monochrome Color Map */
SDL_Color vid_colors[256];
KEY_EVENT_QUEUE vid_key_events;                         /* keyboard events */
MOUSE_EVENT_QUEUE vid_mouse_events;                     /* mouse events */
DEVICE *vid_dev;

#if defined (SDL_MAIN_AVAILABLE)
#if defined (main)
#undef main
#endif

static int main_argc;
static char **main_argv;
static SDL_Thread *vid_main_thread_handle;

int main_thread (void *arg)
{
SDL_Event user_event;
int stat;

stat = SDL_main (main_argc, main_argv);
user_event.type = SDL_USEREVENT;
user_event.user.code = EVENT_EXIT;
user_event.user.data1 = NULL;
user_event.user.data2 = NULL;
while (SDL_PushEvent (&user_event) < 0)
    sim_os_ms_sleep (10);
return stat;
}

int main (int argc, char *argv[])
{
SDL_Event event;
int status;

main_argc = argc;
main_argv = argv;

#if SDL_MAJOR_VERSION == 1
_XInitThreads();
status = SDL_Init (SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE);

vid_main_thread_handle = SDL_CreateThread (main_thread , NULL);
#else
SDL_SetHint (SDL_HINT_RENDER_DRIVER, "software");

status = SDL_Init (SDL_INIT_VIDEO);

vid_main_thread_handle = SDL_CreateThread (main_thread , "simh-main", NULL);
#endif

if (status) {
    fprintf (stderr, "SDL Video subsystem can't initialize\n");
    exit (1);
    }

sim_os_set_thread_priority (PRIORITY_ABOVE_NORMAL);

vid_beep_setup (400, 660);

while (1) {
    int status = SDL_WaitEvent (&event);
    if (status == 1) {
        if (event.type == SDL_USEREVENT) {
            if (event.user.code == EVENT_EXIT)
                break;
            if (event.user.code == EVENT_OPEN)
                vid_video_events ();
            else {
                if (event.user.code == EVENT_SHOW)
                    vid_show_video_event ();
                else {
                    if (event.user.code == EVENT_SCREENSHOT)
                        vid_screenshot_event ();
                    else {
                        if (event.user.code == EVENT_BEEP)
                            vid_beep_event ();
                        else {
                            sim_printf ("main(): Unexpected User event: %d\n", event.user.code);
                            break;
                            }
                        }
                    }
                }
            }
        else {
//          sim_printf ("main(): Ignoring unexpected event: %d\n", event.type);
            }
        }
    else {
        if (status < 0)
            sim_printf ("main() - ` error: %s\n", SDL_GetError());
        }
    }
SDL_WaitThread (vid_main_thread_handle, &status);
vid_beep_cleanup ();
SDL_Quit ();
return status;
}

static t_stat vid_create_window ()
{
int wait_count = 0;
SDL_Event user_event;

vid_ready = FALSE;
user_event.type = SDL_USEREVENT;
user_event.user.code = EVENT_OPEN;
user_event.user.data1 = NULL;
user_event.user.data2 = NULL;
SDL_PushEvent (&user_event);

while ((!vid_ready) && (++wait_count < 20))
    sim_os_ms_sleep (100);
if (!vid_ready) {
    vid_close ();
    return SCPE_OPENERR;
    }
return SCPE_OK;
}
#else
static int vid_create_window ()
{
int wait_count = 0;

#if SDL_MAJOR_VERSION == 1
vid_thread_handle = SDL_CreateThread (vid_thread, NULL);
#else
vid_thread_handle = SDL_CreateThread (vid_thread, "vid-thread", NULL);
#endif
if (vid_thread_handle == NULL) {
    vid_close ();
    return SCPE_OPENERR;
    }
while ((!vid_ready) && (++wait_count < 20))
    sim_os_ms_sleep (100);
if (!vid_ready) {
    vid_close ();
    return SCPE_OPENERR;
    }
return SCPE_OK;
}
#endif

t_stat vid_open (DEVICE *dptr, const char *title, uint32 width, uint32 height, int flags)
{
if (!vid_active) {
    int wait_count = 0;
    t_stat stat;

    if ((strlen(sim_name) + 7 + (dptr ? strlen (dptr->name) : 0) + (title ? strlen (title) : 0)) < sizeof (vid_title))
        sprintf (vid_title, "%s%s%s%s%s", sim_name, dptr ? " - " : "", dptr ? dptr->name : "", title ? " - " : "", title ? title : "");
    else
        sprintf (vid_title, "%s", sim_name);
    vid_flags = flags;
    vid_active = TRUE;
    vid_width = width;
    vid_height = height;
    vid_mouse_captured = FALSE;
    vid_cursor_visible = (vid_flags & SIM_VID_INPUTCAPTURED);

    vid_key_events.head = 0;
    vid_key_events.tail = 0;
    vid_key_events.count = 0;
    vid_key_events.sem = SDL_CreateSemaphore (1);
    vid_mouse_events.head = 0;
    vid_mouse_events.tail = 0;
    vid_mouse_events.count = 0;
    vid_mouse_events.sem = SDL_CreateSemaphore (1);

    vid_dev = dptr;

    stat = vid_create_window ();
    if (stat != SCPE_OK)
        return stat;

    sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_open() - Success\n");
    }
return SCPE_OK;
}

t_stat vid_close (void)
{
if (vid_active) {
    SDL_Event user_event;
    int status;

    vid_active = FALSE;
    if (vid_ready) {
        sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_close()\n");
        user_event.type = SDL_USEREVENT;
        user_event.user.code = EVENT_CLOSE;
        user_event.user.data1 = NULL;
        user_event.user.data2 = NULL;

        while (SDL_PushEvent (&user_event) < 0)
            sim_os_ms_sleep (10);
        vid_dev = NULL;
        }
    if (vid_thread_handle) {
        SDL_WaitThread (vid_thread_handle, &status);
        vid_thread_handle = NULL;
        }
    while (vid_ready)
        sim_os_ms_sleep (10);
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
t_stat stat = SCPE_EOF;
SIM_MOUSE_EVENT *nev;

if (SDL_SemTryWait (vid_mouse_events.sem) == 0) {
    if (vid_mouse_events.count > 0) {
        stat = SCPE_OK;
        *ev = vid_mouse_events.events[vid_mouse_events.head++];
        vid_mouse_events.count--;
        if (vid_mouse_events.head == MAX_EVENTS)
            vid_mouse_events.head = 0;
        nev = &vid_mouse_events.events[vid_mouse_events.head];
        if ((vid_mouse_events.count > 0) &&
            (0 == (ev->x_rel + nev->x_rel)) &&
            (0 == (ev->y_rel + nev->y_rel)) &&
            (ev->b1_state == nev->b1_state) &&
            (ev->b2_state == nev->b2_state) &&
            (ev->b3_state == nev->b3_state)) {
            if ((++vid_mouse_events.head) == MAX_EVENTS)
                vid_mouse_events.head = 0;
            vid_mouse_events.count--;
            stat = SCPE_EOF;
            sim_debug (SIM_VID_DBG_MOUSE, vid_dev, "vid_poll_mouse: ignoring bouncing events\n");
            }
        }
    if (SDL_SemPost (vid_mouse_events.sem))
        sim_printf ("%s: vid_poll_mouse(): SDL_SemPost error: %s\n", sim_dname(vid_dev), SDL_GetError());
    }
return stat;
}

void vid_draw (int32 x, int32 y, int32 w, int32 h, uint32 *buf)
{
#if SDL_MAJOR_VERSION == 1
int32 i;
uint32* pixels;

sim_debug (SIM_VID_DBG_VIDEO, vid_dev, "vid_draw(%d, %d, %d, %d)\n", x, y, w, h);

pixels = (uint32 *)vid_image->pixels;

for (i = 0; i < h; i++)
    memcpy (pixels + ((i + y) * vid_width) + x, buf + w*i, w*sizeof(*pixels));
#else
SDL_Event user_event;
SDL_Rect *vid_dst;
uint32 *vid_data;

sim_debug (SIM_VID_DBG_VIDEO, vid_dev, "vid_draw(%d, %d, %d, %d)\n", x, y, w, h);

vid_dst = (SDL_Rect *)malloc (sizeof(*vid_dst));
if (!vid_dst) {
    sim_printf ("%s: vid_draw() memory allocation error\n", vid_dev ? sim_dname(vid_dev) : "Video Device");
    return;
    }
vid_dst->x = x;
vid_dst->y = y;
vid_dst->w = w;
vid_dst->h = h;
vid_data = (uint32 *)malloc (w*h*sizeof(*buf));
if (!vid_data) {
    sim_printf ("%s: vid_draw() memory allocation error\n", vid_dev ? sim_dname(vid_dev) : "Video Device");
    free (vid_dst);
    return;
    }
memcpy (vid_data, buf, w*h*sizeof(*buf));
user_event.type = SDL_USEREVENT;
user_event.user.code = EVENT_DRAW;
user_event.user.data1 = (void *)vid_dst;
user_event.user.data2 = (void *)vid_data;
if (SDL_PushEvent (&user_event) < 0) {
    sim_printf ("%s: vid_draw() SDL_PushEvent error: %s\n", vid_dev ? sim_dname(vid_dev) : "Video Device", SDL_GetError());
    free (vid_dst);
    free (vid_data);
    }
#endif
}

t_stat vid_set_cursor (t_bool visible, uint32 width, uint32 height, uint8 *data, uint8 *mask, uint32 hot_x, uint32 hot_y)
{
SDL_Cursor *cursor = SDL_CreateCursor (data, mask, width, height, hot_x, hot_y);
SDL_Event user_event;

sim_debug (SIM_VID_DBG_CURSOR, vid_dev, "vid_set_cursor(%s, %d, %d) Setting New Cursor\n", visible ? "visible" : "invisible", width, height);
if (sim_deb) {
    uint32 i, j;

    for (i=0; i<height; i++) {
        sim_debug (SIM_VID_DBG_CURSOR, vid_dev, "Cursor:  ");
        for (j=0; j<width; j++) {
            int byte = (j + i*width) >> 3;
            int bit = 7 - ((j + i*width) & 0x7);
            static char mode[] = "TWIB";

            sim_debug (SIM_VID_DBG_CURSOR, vid_dev, "%c", mode[(((data[byte]>>bit)&1)<<1)|(mask[byte]>>bit)&1]);
            }
        sim_debug (SIM_VID_DBG_CURSOR, vid_dev, "\n");
        }
    }

user_event.type = SDL_USEREVENT;
user_event.user.code = EVENT_CURSOR;
user_event.user.data1 = cursor;
user_event.user.data2 = (void *)((size_t)visible);

if (SDL_PushEvent (&user_event) < 0) {
    sim_printf ("%s: vid_set_cursor() SDL_PushEvent error: %s\n", vid_dev ? sim_dname(vid_dev) : "Video Device", SDL_GetError());
    SDL_FreeCursor (cursor);
    }

return SCPE_OK;
}

void vid_set_cursor_position (int32 x, int32 y)
{
int32 x_delta = vid_cursor_x - x;
int32 y_delta = vid_cursor_y - y;

if (vid_flags & SIM_VID_INPUTCAPTURED)
    return;

if ((x_delta) || (y_delta)) {
    sim_debug (SIM_VID_DBG_CURSOR, vid_dev, "vid_set_cursor_position(%d, %d) - Cursor position changed\n", x, y);
    /* Any queued mouse motion events need to have their relative 
       positions adjusted since they were queued based on different info. */
    if (SDL_SemWait (vid_mouse_events.sem) == 0) {
        int32 i;
        SIM_MOUSE_EVENT *ev;

        for (i=0; i<vid_mouse_events.count; i++) {
            ev = &vid_mouse_events.events[(vid_mouse_events.head + i)%MAX_EVENTS];
            sim_debug (SIM_VID_DBG_CURSOR, vid_dev, "Pending Mouse Motion Event Adjusted from: (%d, %d) to (%d, %d)\n", ev->x_rel, ev->y_rel, ev->x_rel + x_delta, ev->y_rel + y_delta);
            ev->x_rel += x_delta;
            ev->y_rel += y_delta;
            }
        if (SDL_SemPost (vid_mouse_events.sem))
            sim_printf ("%s: vid_set_cursor_position(): SDL_SemPost error: %s\n", vid_dev ? sim_dname(vid_dev) : "Video Device", SDL_GetError());
        }
    else {
        sim_printf ("%s: vid_set_cursor_position(): SDL_SemWait error: %s\n", vid_dev ? sim_dname(vid_dev) : "Video Device", SDL_GetError());
        }
    vid_cursor_x = x;
    vid_cursor_y = y;
    if (vid_cursor_visible) {
        SDL_Event user_event;

        user_event.type = SDL_USEREVENT;
        user_event.user.code = EVENT_WARP;
        user_event.user.data1 = NULL;
        user_event.user.data2 = NULL;

        if (SDL_PushEvent (&user_event) < 0)
            sim_printf ("%s: vid_set_cursor_position() SDL_PushEvent error: %s\n", sim_dname(vid_dev), SDL_GetError());
        sim_debug (SIM_VID_DBG_CURSOR, vid_dev, "vid_set_cursor_position() - Warp Queued\n");
        }
    else {
        sim_debug (SIM_VID_DBG_CURSOR, vid_dev, "vid_set_cursor_position() - Warp Skipped\n");
        }
    }
}

void vid_refresh (void)
{
SDL_Event user_event;

sim_debug (SIM_VID_DBG_VIDEO, vid_dev, "vid_refresh() - Queueing Refresh Event\n");

user_event.type = SDL_USEREVENT;
user_event.user.code = EVENT_REDRAW;
user_event.user.data1 = NULL;
user_event.user.data2 = NULL;

if (SDL_PushEvent (&user_event) < 0)
    sim_printf ("%s: vid_refresh() SDL_PushEvent error: %s\n", sim_dname(vid_dev), SDL_GetError());
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
#if SDL_MAJOR_VERSION == 1
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
#else
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
#endif
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
#if SDL_MAJOR_VERSION != 1
    case SDLK_NUMLOCKCLEAR:
        return SIM_KEY_NUM_LOCK;
#endif
    case SDLK_CAPSLOCK:
        return SIM_KEY_CAPS_LOCK;
#if SDL_MAJOR_VERSION == 1
    case SDLK_SCROLLOCK:
        return SIM_KEY_SCRL_LOCK;
#else
    case SDLK_SCROLLLOCK:
        return SIM_KEY_SCRL_LOCK;
#endif
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
#if SDL_MAJOR_VERSION == 1
    case SDLK_RMETA:
        return SIM_KEY_ALT_R;

    case SDLK_LMETA:
        return SIM_KEY_WIN_L;
#else
    case SDLK_LGUI:
        return SIM_KEY_WIN_L;

    case SDLK_RGUI:
        return SIM_KEY_WIN_R;
#endif
#if SDL_MAJOR_VERSION == 1
    case SDLK_PRINT:
        return SIM_KEY_PRINT;
#else
    case SDLK_PRINTSCREEN:
        return SIM_KEY_PRINT;
#endif
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
#if SDL_MAJOR_VERSION == 1
        KeyStates = SDL_GetKeyState(&numkeys);
    if ((vid_flags & SIM_VID_INPUTCAPTURED) && 
        (event->state == SDL_PRESSED) && 
        KeyStates[SDLK_RSHIFT] && 
        (KeyStates[SDLK_LCTRL] || KeyStates[SDLK_RCTRL])) {
#else
        KeyStates = SDL_GetKeyboardState(&numkeys);
    if ((vid_flags & SIM_VID_INPUTCAPTURED) && 
        (event->state == SDL_PRESSED) && 
        KeyStates[SDL_SCANCODE_RSHIFT] && 
        (KeyStates[SDL_SCANCODE_LCTRL] || KeyStates[SDL_SCANCODE_RCTRL])) {
#endif
        sim_debug (SIM_VID_DBG_KEY, vid_dev, "vid_key() - Cursor Release\n");
#if SDL_MAJOR_VERSION == 1
        if (SDL_WM_GrabInput (SDL_GRAB_OFF) < 0)        /* relese cursor */
            sim_printf ("%s: vid_key(): SDL_WM_GrabInput error: %s\n", sim_dname(vid_dev), SDL_GetError());
        if (SDL_ShowCursor (SDL_ENABLE) < 0)            /* show cursor */
            sim_printf ("%s: vid_key(): SDL_ShowCursor error: %s\n", sim_dname(vid_dev), SDL_GetError());
#else
        if (SDL_SetRelativeMouseMode(SDL_FALSE) < 0)    /* release cursor, show cursor */
            sim_printf ("%s: vid_key(): SDL_SetRelativeMouseMode error: %s\n", sim_dname(vid_dev), SDL_GetError());
#endif
        vid_mouse_captured = FALSE;
        return;
        }
    }
if (!sim_is_running)
    return;
if (SDL_SemWait (vid_key_events.sem) == 0) {
    if (vid_key_events.count < MAX_EVENTS) {
        ev.key = vid_map_key (event->keysym.sym);
        sim_debug (SIM_VID_DBG_KEY, vid_dev, "Keyboard Event: State: %s, Keysym(scancode,sym): (%d,%d) - %s\n", (event->state == SDL_PRESSED) ? "PRESSED" : "RELEASED", event->keysym.scancode, event->keysym.sym, vid_key_name(ev.key));
        if (event->state == SDL_PRESSED) {
#if SDL_MAJOR_VERSION == 1
            if (!vid_key_state[event->keysym.sym]) {    /* Key was not down before */
                vid_key_state[event->keysym.sym] = TRUE;
#else
            if (!vid_key_state[event->keysym.scancode]) {/* Key was not down before */
                vid_key_state[event->keysym.scancode] = TRUE;
#endif
                ev.state = SIM_KEYPRESS_DOWN;
                }
            else
                ev.state = SIM_KEYPRESS_REPEAT;
            }
        else {
#if SDL_MAJOR_VERSION == 1
            vid_key_state[event->keysym.sym] = FALSE;
#else
            vid_key_state[event->keysym.scancode] = FALSE;
#endif
            ev.state = SIM_KEYPRESS_UP;
            }
        vid_key_events.events[vid_key_events.tail++] = ev;
        vid_key_events.count++;
        if (vid_key_events.tail == MAX_EVENTS)
            vid_key_events.tail = 0;
        }
    else {
        sim_debug (SIM_VID_DBG_KEY, vid_dev, "Keyboard Event DISCARDED: State: %s, Keysym: Scancode: %d, Keysym: %d\n", (event->state == SDL_PRESSED) ? "PRESSED" : "RELEASED", event->keysym.scancode, event->keysym.sym);
        }
    if (SDL_SemPost (vid_key_events.sem))
        sim_printf ("%s: vid_key(): SDL_SemPost error: %s\n", sim_dname(vid_dev), SDL_GetError());
    }
}

void vid_mouse_move (SDL_MouseMotionEvent *event)
{
SDL_Event dummy_event;
SDL_MouseMotionEvent *dev = (SDL_MouseMotionEvent *)&dummy_event;
SIM_MOUSE_EVENT ev;

if ((!vid_mouse_captured) && (vid_flags & SIM_VID_INPUTCAPTURED))
    return;

if (!sim_is_running)
    return;
if (!vid_cursor_visible)
    return;
sim_debug (SIM_VID_DBG_MOUSE, vid_dev, "Mouse Move Event: pos:(%d,%d) rel:(%d,%d) buttons:(%d,%d,%d)\n", 
           event->x, event->y, event->xrel, event->yrel, (event->state & SDL_BUTTON(SDL_BUTTON_LEFT)) ? 1 : 0, (event->state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ? 1 : 0, (event->state & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 1 : 0);
#if SDL_MAJOR_VERSION == 1
while (SDL_PeepEvents (&dummy_event, 1, SDL_GETEVENT, SDL_MOUSEMOTIONMASK)) {
#else
while (SDL_PeepEvents (&dummy_event, 1, SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION)) {
#endif
    /* Coalesce motion activity to avoid thrashing */
    event->xrel += dev->xrel;
    event->yrel += dev->yrel;
    event->x = dev->x;
    event->y = dev->y;
    event->state = dev->state;
    sim_debug (SIM_VID_DBG_MOUSE, vid_dev, "Mouse Move Event: Additional Event Coalesced:pos:(%d,%d) rel:(%d,%d) buttons:(%d,%d,%d)\n", 
        dev->x, dev->y, dev->xrel, dev->yrel, (dev->state & SDL_BUTTON(SDL_BUTTON_LEFT)) ? 1 : 0, (dev->state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ? 1 : 0, (dev->state & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 1 : 0);
    };
if (SDL_SemWait (vid_mouse_events.sem) == 0) {
    if (!vid_mouse_captured) {
        event->xrel = (event->x - vid_cursor_x);
        event->yrel = (event->y - vid_cursor_y);
        }
    vid_mouse_b1 = (event->state & SDL_BUTTON(SDL_BUTTON_LEFT)) ? TRUE : FALSE;
    vid_mouse_b2 = (event->state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ? TRUE : FALSE;
    vid_mouse_b3 = (event->state & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? TRUE : FALSE;
    sim_debug (SIM_VID_DBG_MOUSE, vid_dev, "Mouse Move Event: pos:(%d,%d) rel:(%d,%d) buttons:(%d,%d,%d) - Count: %d vid_cursor:(%d,%d)\n", 
                                            event->x, event->y, event->xrel, event->yrel, (event->state & SDL_BUTTON(SDL_BUTTON_LEFT)) ? 1 : 0, (event->state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ? 1 : 0, (event->state & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 1 : 0, vid_mouse_events.count, vid_cursor_x, vid_cursor_y);
    if (vid_mouse_events.count < MAX_EVENTS) {
        SIM_MOUSE_EVENT *tail = &vid_mouse_events.events[(vid_mouse_events.tail+MAX_EVENTS-1)%MAX_EVENTS];

        ev.x_rel = event->xrel;
        ev.y_rel = event->yrel;
        ev.b1_state = vid_mouse_b1;
        ev.b2_state = vid_mouse_b2;
        ev.b3_state = vid_mouse_b3;
        ev.x_pos = event->x;
        ev.y_pos = event->y;
        if ((vid_mouse_events.count > 0) &&             /* Is there a tail event? */
            (ev.b1_state == tail->b1_state) &&          /* With the same button state? */
            (ev.b2_state == tail->b2_state) && 
            (ev.b3_state == tail->b3_state)) {          /* Merge the motion */
            tail->x_rel += ev.x_rel;
            tail->y_rel += ev.y_rel;
            tail->x_pos = ev.x_pos;
            tail->y_pos = ev.y_pos;
            sim_debug (SIM_VID_DBG_MOUSE, vid_dev, "Mouse Move Event: Coalesced into pending event: (%d,%d)\n", 
                tail->x_rel, tail->y_rel);
            }
        else {                                          /* Add a new event */
            vid_mouse_events.events[vid_mouse_events.tail++] = ev;
            vid_mouse_events.count++;
            if (vid_mouse_events.tail == MAX_EVENTS)
                vid_mouse_events.tail = 0;
            }
        }
    else {
        sim_debug (SIM_VID_DBG_MOUSE, vid_dev, "Mouse Move Event Discarded: Count: %d\n", vid_mouse_events.count);
        }
    if (SDL_SemPost (vid_mouse_events.sem))
        sim_printf ("%s: vid_mouse_move(): SDL_SemPost error: %s\n", sim_dname(vid_dev), SDL_GetError());
    }
}

void vid_mouse_button (SDL_MouseButtonEvent *event)
{
SDL_Event dummy_event;
SIM_MOUSE_EVENT ev;
t_bool state;

if ((!vid_mouse_captured) && (vid_flags & SIM_VID_INPUTCAPTURED)) {
    if ((event->state == SDL_PRESSED) &&
        (event->button == SDL_BUTTON_LEFT)) {               /* left click and cursor not captured? */
        sim_debug (SIM_VID_DBG_KEY, vid_dev, "vid_mouse_button() - Cursor Captured\n");
#if SDL_MAJOR_VERSION == 1
        SDL_WM_GrabInput (SDL_GRAB_ON);                     /* lock cursor to window */
        SDL_ShowCursor (SDL_DISABLE);                       /* hide cursor */
        SDL_WarpMouse (vid_width/2, vid_height/2);          /* back to center */
        SDL_PumpEvents ();
        while (SDL_PeepEvents (&dummy_event, 1, SDL_GETEVENT, SDL_MOUSEMOTIONMASK)) {};
#else
        if (SDL_SetRelativeMouseMode (SDL_TRUE) < 0)        /* lock cursor to window, hide cursor */
            sim_printf ("%s: vid_mouse_button(): SDL_SetRelativeMouseMode error: %s\n", sim_dname(vid_dev), SDL_GetError());
        SDL_WarpMouseInWindow (NULL, vid_width/2, vid_height/2);/* back to center */
        SDL_PumpEvents ();
        while (SDL_PeepEvents (&dummy_event, 1, SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION)) {};
#endif
        vid_mouse_captured = TRUE;
        }
    return;
    }
if (!sim_is_running)
    return;
state = (event->state == SDL_PRESSED) ? TRUE : FALSE;
if (SDL_SemWait (vid_mouse_events.sem) == 0) {
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
    sim_debug (SIM_VID_DBG_MOUSE, vid_dev, "Mouse Button Event: State: %d, Button: %d, (%d,%d)\n", event->state, event->button, event->x, event->y);
    if (vid_mouse_events.count < MAX_EVENTS) {
        ev.x_rel = 0;
        ev.y_rel = 0;
        ev.x_pos = event->x;
        ev.y_pos = event->y;
        ev.b1_state = vid_mouse_b1;
        ev.b2_state = vid_mouse_b2;
        ev.b3_state = vid_mouse_b3;
        vid_mouse_events.events[vid_mouse_events.tail++] = ev;
        vid_mouse_events.count++;
        if (vid_mouse_events.tail == MAX_EVENTS)
            vid_mouse_events.tail = 0;
        }
    else {
        sim_debug (SIM_VID_DBG_MOUSE, vid_dev, "Mouse Button Event Discarded: Count: %d\n", vid_mouse_events.count);
        }
    if (SDL_SemPost (vid_mouse_events.sem))
        sim_printf ("%s: Mouse Button Event: SDL_SemPost error: %s\n", sim_dname(vid_dev), SDL_GetError());
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
if (sim_deb)
    fflush (sim_deb);
#if SDL_MAJOR_VERSION == 1
if (SDL_BlitSurface (vid_image, NULL, vid_window, &vid_dst) < 0)
    sim_printf ("%s: vid_update(): SDL_BlitSurface error: %s\n", sim_dname(vid_dev), SDL_GetError());
SDL_UpdateRects (vid_window, 1, &vid_dst);
#else
if (SDL_RenderClear (vid_renderer))
    sim_printf ("%s: Video Update Event: SDL_RenderClear error: %s\n", sim_dname(vid_dev), SDL_GetError());
if (SDL_RenderCopy (vid_renderer, vid_texture, NULL, NULL))
    sim_printf ("%s: Video Update Event: SDL_RenderCopy error: %s\n", sim_dname(vid_dev), SDL_GetError());
SDL_RenderPresent (vid_renderer);
#endif
}

void vid_update_cursor (SDL_Cursor *cursor, t_bool visible)
{
if (!cursor)
    return;
sim_debug (SIM_VID_DBG_VIDEO, vid_dev, "Cursor Update Event: Previously %s, Now %s, New Cursor object at: %p, Old Cursor object at: %p\n", 
                            SDL_ShowCursor(-1) ? "visible" : "invisible", visible ? "visible" : "invisible", cursor, vid_cursor);
SDL_SetCursor (cursor);
#if SDL_MAJOR_VERSION == 1
if (visible)
    SDL_WarpMouse (vid_cursor_x, vid_cursor_y);/* sync position */
#else
if ((vid_window == SDL_GetMouseFocus ()) && visible)
    SDL_WarpMouseInWindow (NULL, vid_cursor_x, vid_cursor_y);/* sync position */
#endif
if ((vid_cursor != cursor) && (vid_cursor))
    SDL_FreeCursor (vid_cursor);
vid_cursor = cursor;
SDL_ShowCursor (visible);
vid_cursor_visible = visible;
}

void vid_warp_position (void)
{
sim_debug (SIM_VID_DBG_VIDEO, vid_dev, "Mouse Warp Event: Warp to: (%d,%d)\n", vid_cursor_x, vid_cursor_y);

SDL_PumpEvents ();
#if SDL_MAJOR_VERSION == 1
SDL_WarpMouse (vid_cursor_x, vid_cursor_y);
#else
SDL_WarpMouseInWindow (NULL, vid_cursor_x, vid_cursor_y);
#endif
SDL_PumpEvents ();
}

void vid_draw_region (SDL_UserEvent *event)
{
SDL_Rect *vid_dst = (SDL_Rect *)event->data1;
uint32 *buf = (uint32 *)event->data2;

sim_debug (SIM_VID_DBG_VIDEO, vid_dev, "Draw Region Event: (%d,%d,%d,%d)\n", vid_dst->x, vid_dst->x, vid_dst->w, vid_dst->h);

#if SDL_MAJOR_VERSION == 1
if (1) {
    int32 i;
    uint32* pixels;

    pixels = (uint32 *)vid_image->pixels;

    for (i = 0; i < vid_dst->h; i++)
        memcpy (pixels + ((i + vid_dst->y) * vid_width) + vid_dst->x, buf + vid_dst->w*i, vid_dst->w*sizeof(*pixels));
    }
#else
if (SDL_UpdateTexture(vid_texture, vid_dst, buf, vid_dst->w*sizeof(*buf)))
    sim_printf ("%s: vid_draw() - SDL_UpdateTexture error: %s\n", sim_dname(vid_dev), SDL_GetError());
#endif

free (vid_dst);
free (buf);
event->data1 = NULL;
}

int vid_video_events (void)
{
SDL_Event event;
#if SDL_MAJOR_VERSION == 1
static const char *eventtypes[] = {
    "NOEVENT",              /**< Unused (do not remove) */
    "ACTIVEEVENT",          /**< Application loses/gains visibility */
    "KEYDOWN",              /**< Keys pressed */
    "KEYUP",                /**< Keys released */
    "MOUSEMOTION",          /**< Mouse moved */
    "MOUSEBUTTONDOWN",      /**< Mouse button pressed */
    "MOUSEBUTTONUP",        /**< Mouse button released */
    "JOYAXISMOTION",        /**< Joystick axis motion */
    "JOYBALLMOTION",        /**< Joystick trackball motion */
    "JOYHATMOTION",         /**< Joystick hat position change */
    "JOYBUTTONDOWN",        /**< Joystick button pressed */
    "JOYBUTTONUP",          /**< Joystick button released */
    "QUIT",                 /**< User-requested quit */
    "SYSWMEVENT",           /**< System specific event */
    "EVENT_RESERVEDA",      /**< Reserved for future use.. */
    "EVENT_RESERVEDB",      /**< Reserved for future use.. */
    "VIDEORESIZE",          /**< User resized video mode */
    "VIDEOEXPOSE",          /**< Screen needs to be redrawn */
    "EVENT_RESERVED2",      /**< Reserved for future use.. */
    "EVENT_RESERVED3",      /**< Reserved for future use.. */
    "EVENT_RESERVED4",      /**< Reserved for future use.. */
    "EVENT_RESERVED5",      /**< Reserved for future use.. */
    "EVENT_RESERVED6",      /**< Reserved for future use.. */
    "EVENT_RESERVED7",      /**< Reserved for future use.. */
    "USEREVENT",            /** Events SDL_USEREVENT(24) through SDL_MAXEVENTS-1(31) are for your use */
    "",
    "",
    "",
    "",
    "",
    "",
    ""
    };
#else
static const char *eventtypes[SDL_LASTEVENT];
#endif
static const char *windoweventtypes[256];
static t_bool initialized = FALSE;

if (!initialized) {
    initialized = TRUE;
#if SDL_MAJOR_VERSION != 1
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

    windoweventtypes[SDL_WINDOWEVENT_NONE] = "NONE";                /**< Never used */
    windoweventtypes[SDL_WINDOWEVENT_SHOWN] = "SHOWN";              /**< Window has been shown */
    windoweventtypes[SDL_WINDOWEVENT_HIDDEN] = "HIDDEN";            /**< Window has been hidden */
    windoweventtypes[SDL_WINDOWEVENT_EXPOSED] = "EXPOSED";          /**< Window has been exposed and should be
                                                                         redrawn */
    windoweventtypes[SDL_WINDOWEVENT_MOVED] = "MOVED";              /**< Window has been moved to data1, data2
                                     */
    windoweventtypes[SDL_WINDOWEVENT_RESIZED] = "RESIZED";          /**< Window has been resized to data1xdata2 */
    windoweventtypes[SDL_WINDOWEVENT_SIZE_CHANGED] = "SIZE_CHANGED";/**< The window size has changed, either as a result of an API call or through the system or user changing the window size. */
    windoweventtypes[SDL_WINDOWEVENT_MINIMIZED] = "MINIMIZED";      /**< Window has been minimized */
    windoweventtypes[SDL_WINDOWEVENT_MAXIMIZED] = "MAXIMIZED";      /**< Window has been maximized */
    windoweventtypes[SDL_WINDOWEVENT_RESTORED] = "RESTORED";        /**< Window has been restored to normal size
                                                                         and position */
    windoweventtypes[SDL_WINDOWEVENT_ENTER] = "ENTER";              /**< Window has gained mouse focus */
    windoweventtypes[SDL_WINDOWEVENT_LEAVE] = "LEAVE";              /**< Window has lost mouse focus */
    windoweventtypes[SDL_WINDOWEVENT_FOCUS_GAINED] = "FOCUS_GAINED";/**< Window has gained keyboard focus */
    windoweventtypes[SDL_WINDOWEVENT_FOCUS_LOST] = "FOCUS_LOST";    /**< Window has lost keyboard focus */
    windoweventtypes[SDL_WINDOWEVENT_CLOSE] = "CLOSE";              /**< The window manager requests that the
                                                                         window be closed */

    /* Keyboard events */
    eventtypes[SDL_KEYDOWN] = "KEYDOWN";                            /**< Key pressed */
    eventtypes[SDL_KEYUP] = "KEYUP";                                /**< Key released */
    eventtypes[SDL_TEXTEDITING] = "TEXTEDITING";                    /**< Keyboard text editing (composition) */
    eventtypes[SDL_TEXTINPUT] = "TEXTINPUT";                        /**< Keyboard text input */

    /* Mouse events */
    eventtypes[SDL_MOUSEMOTION] = "MOUSEMOTION";                    /**< Mouse moved */
    eventtypes[SDL_MOUSEBUTTONDOWN] = "MOUSEBUTTONDOWN";            /**< Mouse button pressed */
    eventtypes[SDL_MOUSEBUTTONUP] = "MOUSEBUTTONUP";                /**< Mouse button released */
    eventtypes[SDL_MOUSEWHEEL] = "MOUSEWHEEL";                      /**< Mouse wheel motion */

    /* Joystick events */
    eventtypes[SDL_JOYAXISMOTION] = "JOYAXISMOTION";                /**< Joystick axis motion */
    eventtypes[SDL_JOYBALLMOTION] = "JOYBALLMOTION";                /**< Joystick trackball motion */
    eventtypes[SDL_JOYHATMOTION] = "JOYHATMOTION";                  /**< Joystick hat position change */
    eventtypes[SDL_JOYBUTTONDOWN] = "JOYBUTTONDOWN";                /**< Joystick button pressed */
    eventtypes[SDL_JOYBUTTONUP] = "JOYBUTTONUP";                    /**< Joystick button released */
    eventtypes[SDL_JOYDEVICEADDED] = "JOYDEVICEADDED";              /**< A new joystick has been inserted into the system */
    eventtypes[SDL_JOYDEVICEREMOVED] = "JOYDEVICEREMOVED";          /**< An opened joystick has been removed */

    /* Game controller events */
    eventtypes[SDL_CONTROLLERAXISMOTION] = "CONTROLLERAXISMOTION";          /**< Game controller axis motion */
    eventtypes[SDL_CONTROLLERBUTTONDOWN] = "CONTROLLERBUTTONDOWN";          /**< Game controller button pressed */
    eventtypes[SDL_CONTROLLERBUTTONUP] = "CONTROLLERBUTTONUP";              /**< Game controller button released */
    eventtypes[SDL_CONTROLLERDEVICEADDED] = "CONTROLLERDEVICEADDED";        /**< A new Game controller has been inserted into the system */
    eventtypes[SDL_CONTROLLERDEVICEREMOVED] = "CONTROLLERDEVICEREMOVED";    /**< An opened Game controller has been removed */
    eventtypes[SDL_CONTROLLERDEVICEREMAPPED] = "CONTROLLERDEVICEREMAPPED";  /**< The controller mapping was updated */

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

#if (SDL_MINOR_VERSION > 0) || (SDL_PATCHLEVEL >= 3)
    /* Render events */
    eventtypes[SDL_RENDER_TARGETS_RESET] = "RENDER_TARGETS_RESET"; /**< The render targets have been reset */
#endif

#if (SDL_MINOR_VERSION > 0) || (SDL_PATCHLEVEL >= 4)
    /* Render events */
    eventtypes[SDL_RENDER_DEVICE_RESET] = "RENDER_DEVICE_RESET"; /**< The render device has been reset */
#endif

    /** Events ::SDL_USEREVENT through ::SDL_LASTEVENT are for your use,
     *  and should be allocated with SDL_RegisterEvents()
     */
    eventtypes[SDL_USEREVENT] = "USEREVENT";
#endif  /* SDL_MAJOR_VERSION != 1 */
    }

sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE, vid_dev, "vid_thread() - Starting\n");

vid_mono_palette[0] = sim_end ? 0xFF000000 : 0x000000FF;        /* Black */
vid_mono_palette[1] = 0xFFFFFFFF;                               /* White */

memset (&vid_key_state, 0, sizeof(vid_key_state));

#if SDL_MAJOR_VERSION == 1
vid_window = SDL_SetVideoMode (vid_width, vid_height, 8, 0);

SDL_EnableKeyRepeat (SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

if (sim_end)
    vid_image = SDL_CreateRGBSurface (SDL_SWSURFACE, vid_width, vid_height, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
else
    vid_image = SDL_CreateRGBSurface (SDL_SWSURFACE, vid_width, vid_height, 32, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff);

#else
SDL_CreateWindowAndRenderer (vid_width, vid_height, SDL_WINDOW_SHOWN, &vid_window, &vid_renderer);

if ((vid_window == NULL) || (vid_renderer == NULL)) {
    sim_printf ("%s: Error Creating Video Window: %s\n", sim_dname(vid_dev), SDL_GetError());
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
    sim_printf ("%s: Error configuring Video environment: %s\n", sim_dname(vid_dev), SDL_GetError());
    SDL_DestroyRenderer(vid_renderer);
    vid_renderer = NULL;
    SDL_DestroyWindow(vid_window);
    vid_window = NULL;
    SDL_Quit ();
    return 0;
    }

SDL_StopTextInput ();

vid_windowID = SDL_GetWindowID (vid_window);

#endif

if (vid_flags & SIM_VID_INPUTCAPTURED) {
    char title[150];

    memset (title, 0, sizeof(title));
    strncpy (title, vid_title, sizeof(title)-1);
    strncat (title, "                                             ReleaseKey=", sizeof(title)-(1+strlen(title)));
    strncat (title, vid_release_key, sizeof(title)-(1+strlen(title)));
#if SDL_MAJOR_VERSION == 1
    SDL_WM_SetCaption (title, title);
#else
    SDL_SetWindowTitle (vid_window, title);
#endif
    }
else
#if SDL_MAJOR_VERSION == 1
    SDL_WM_SetCaption (vid_title, sim_name);
#else
    SDL_SetWindowTitle (vid_window, vid_title);
#endif

vid_ready = TRUE;

sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE|SIM_VID_DBG_CURSOR, vid_dev, "vid_thread() - Started\n");

while (vid_active) {
    int status = SDL_WaitEvent (&event);
    if (status == 1) {
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
#if SDL_MAJOR_VERSION != 1
            case SDL_WINDOWEVENT:
                if (event.window.windowID == vid_windowID) {
                    sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE|SIM_VID_DBG_CURSOR, vid_dev, "vid_thread() - Window Event: %d - %s\n", event.window.event, windoweventtypes[event.window.event]);
                    switch (event.window.event) {
                        case SDL_WINDOWEVENT_ENTER:
                             if (vid_flags & SIM_VID_INPUTCAPTURED)
                                 SDL_WarpMouseInWindow (NULL, vid_width/2, vid_height/2);   /* center position */
                            break;
                        case SDL_WINDOWEVENT_EXPOSED:
                            vid_update ();
                            break;
                        }
                    }
                break;
#endif
            case SDL_USEREVENT:
                /* There are 6 user events generated */
                /* EVENT_REDRAW to update the display */
                /* EVENT_DRAW   to update a region in the display texture */
                /* EVENT_SHOW   to display the current SDL video capabilities */
                /* EVENT_CURSOR to change the current cursor */
                /* EVENT_WARP   to warp the cursor position */
                /* EVENT_CLOSE  to wake up this thread and let */
                /*              it notice vid_active has changed */
                while (vid_active && event.user.code) {
                    if (event.user.code == EVENT_REDRAW) {
                        vid_update ();
                        event.user.code = 0;    /* Mark as done */
#if SDL_MAJOR_VERSION == 1
if (0)                        while (SDL_PeepEvents (&event, 1, SDL_GETEVENT, SDL_EVENTMASK(SDL_USEREVENT))) {
#else
if (0)                        while (SDL_PeepEvents (&event, 1, SDL_GETEVENT, SDL_USEREVENT, SDL_USEREVENT)) {
#endif
                            if (event.user.code == EVENT_REDRAW) {
                                /* Only do a single video update between waiting for events */
                                sim_debug (SIM_VID_DBG_VIDEO, vid_dev, "vid_thread() - Ignored extra REDRAW Event\n");
                                event.user.code = 0;    /* Mark as done */
                                continue;
                                }
                            break;
                            }
                        }
                    if (event.user.code == EVENT_CURSOR) {
                        vid_update_cursor ((SDL_Cursor *)(event.user.data1), (t_bool)((size_t)event.user.data2));
                        event.user.data1 = NULL;
                        event.user.code = 0;    /* Mark as done */
                        }
                    if (event.user.code == EVENT_WARP) {
                        vid_warp_position ();
                        event.user.code = 0;    /* Mark as done */
                        }
                    if (event.user.code == EVENT_CLOSE) {
                        event.user.code = 0;    /* Mark as done */
                        }
                    if (event.user.code == EVENT_DRAW) {
                        vid_draw_region ((SDL_UserEvent*)&event);
                        event.user.code = 0;    /* Mark as done */
                        }
                    if (event.user.code == EVENT_SHOW) {
                        vid_show_video_event ();
                        event.user.code = 0;    /* Mark as done */
                        }
                    if (event.user.code == EVENT_SCREENSHOT) {
                        vid_screenshot_event ();
                        event.user.code = 0;    /* Mark as done */
                        }
                    if (event.user.code == EVENT_BEEP) {
                        vid_beep_event ();
                        event.user.code = 0;    /* Mark as done */
                        }
                    if (event.user.code != 0) {
                        sim_printf ("vid_thread(): Unexpected user event code: %d\n", event.user.code);
                        }
                    }
                break;
            case SDL_QUIT:
                sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE|SIM_VID_DBG_CURSOR, vid_dev, "vid_thread() - QUIT Event - %s\n", vid_quit_callback ? "Signaled" : "Ignored");
                if (vid_quit_callback)
                    vid_quit_callback ();
                break;

            default:
                sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE|SIM_VID_DBG_CURSOR, vid_dev, "vid_thread() - Ignored Event: Type: %s(%d)\n", eventtypes[event.type], event.type);
                break;
            }
        }
    else {
        if (status < 0)
            sim_printf ("%s: vid_thread() - SDL_WaitEvent error: %s\n", sim_dname(vid_dev), SDL_GetError());
        }
    }
vid_ready = FALSE;
if (vid_cursor) {
    SDL_FreeCursor (vid_cursor);
    vid_cursor = NULL;
    }
#if SDL_MAJOR_VERSION != 1
SDL_DestroyTexture(vid_texture);
vid_texture = NULL;
SDL_DestroyRenderer(vid_renderer);
vid_renderer = NULL;
SDL_DestroyWindow(vid_window);
#endif /* SDL_MAJOR_VERSION != 1 */
vid_window = NULL;
sim_debug (SIM_VID_DBG_VIDEO|SIM_VID_DBG_KEY|SIM_VID_DBG_MOUSE|SIM_VID_DBG_CURSOR, vid_dev, "vid_thread() - Exiting\n");
return 0;
}

int vid_thread (void *arg)
{
int stat;

#if SDL_MAJOR_VERSION == 1
_XInitThreads();
stat = SDL_Init (SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE);
#else
SDL_SetHint (SDL_HINT_RENDER_DRIVER, "software");

stat = SDL_Init (SDL_INIT_VIDEO);
#endif
if (stat) {
    sim_printf ("SDL Video subsystem can't initialize\n");
    return 0;
    }
vid_beep_setup (400, 660);
vid_video_events ();
vid_beep_cleanup ();
SDL_Quit ();
return 0;
}

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

t_stat vid_set_release_key (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
return SCPE_NOFNC;
}

t_stat vid_show_release_key (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
if (vid_flags & SIM_VID_INPUTCAPTURED)
    fprintf (st, "ReleaseKey=%s", vid_release_key);
return SCPE_OK;
}

static t_stat _vid_show_video (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
int i;

fprintf (st, "Video support using SDL: %s\n", vid_version());
#if defined (SDL_MAIN_AVAILABLE)
fprintf (st, "  SDL Events being processed on the main process thread\n");
#endif
if (!vid_active) {
#if !defined (SDL_MAIN_AVAILABLE)
    int stat = SDL_Init(SDL_INIT_VIDEO);

    if (stat)
        return sim_messagef (SCPE_OPENERR, "SDL_Init() failed.  Video subsystem is unavailable.\n");
#endif
    }
else {
    fprintf (st, "  Currently Active Video Window: (%d by %d pixels)\n", vid_width, vid_height);
    fprintf (st, "  ");
    vid_show_release_key (st, uptr, val, desc);
    fprintf (st, "\n");
#if SDL_MAJOR_VERSION != 1
    fprintf (st, "  SDL Video Driver: %s\n", SDL_GetCurrentVideoDriver());
#endif
    }
#if SDL_MAJOR_VERSION == 1
if (1) {
    char driver_name[64];
    const SDL_VideoInfo *info = SDL_GetVideoInfo();

    fprintf (st, "  Video Driver:                                     %s\n", SDL_VideoDriverName(driver_name, sizeof(driver_name)));
    fprintf (st, "  hardware surfaces available:                      %s\n", info->hw_available ? "Yes" : "No");
    fprintf (st, "  window manager available:                         %s\n", info->wm_available ? "Yes" : "No");
    fprintf (st, "  hardware to hardware blits accelerated:           %s\n", info->blit_hw ? "Yes" : "No");
    fprintf (st, "  hardware to hardware colorkey blits accelerated:  %s\n", info->blit_hw_CC ? "Yes" : "No");
    fprintf (st, "  hardware to hardware alpha blits accelerated:     %s\n", info->blit_hw_A ? "Yes" : "No");
    fprintf (st, "  software to hardware blits accelerated:           %s\n", info->blit_sw ? "Yes" : "No");
    fprintf (st, "  software to hardware colorkey blits accelerated:  %s\n", info->blit_sw_CC ? "Yes" : "No");
    fprintf (st, "  software to hardware alpha blits accelerated:     %s\n", info->blit_sw_A ? "Yes" : "No");
    fprintf (st, "  color fills accelerated:                          %s\n", info->blit_fill ? "Yes" : "No");
    fprintf (st, "  Video Memory:                                     %dKb\n", info->video_mem);
    }
#else
for (i = 0; i < SDL_GetNumVideoDisplays(); ++i) {
    SDL_DisplayMode display;

    if (SDL_GetCurrentDisplayMode(i, &display)) {
        fprintf (st, "Could not get display mode for video display #%d: %s", i, SDL_GetError());
        }
    else {
        fprintf (st, "  Display %s(#%d): current display mode is %dx%dpx @ %dhz. \n", SDL_GetDisplayName(i), i, display.w, display.h, display.refresh_rate);
        }
    }
fprintf (st, "  Available SDL Renderers:\n");
for (i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
    SDL_RendererInfo info;

    if (SDL_GetRenderDriverInfo (i, &info)) {
        fprintf (st, "Could not get render driver info for driver #%d: %s", i, SDL_GetError());
        }
    else {
        uint32 j, k;
        static struct {uint32 format; const char *name;} PixelFormats[] = {
            {SDL_PIXELFORMAT_INDEX1LSB,     "Index1LSB"},
            {SDL_PIXELFORMAT_INDEX1MSB,     "Index1MSB"},
            {SDL_PIXELFORMAT_INDEX4LSB,     "Index4LSB"},
            {SDL_PIXELFORMAT_INDEX4MSB,     "Index4MSB"},
            {SDL_PIXELFORMAT_INDEX8,        "Index8"},
            {SDL_PIXELFORMAT_RGB332,        "RGB332"},
            {SDL_PIXELFORMAT_RGB444,        "RGB444"},
            {SDL_PIXELFORMAT_RGB555,        "RGB555"},
            {SDL_PIXELFORMAT_BGR555,        "BGR555"},
            {SDL_PIXELFORMAT_ARGB4444,      "ARGB4444"},
            {SDL_PIXELFORMAT_RGBA4444,      "RGBA4444"},
            {SDL_PIXELFORMAT_ABGR4444,      "ABGR4444"},
            {SDL_PIXELFORMAT_BGRA4444,      "BGRA4444"},
            {SDL_PIXELFORMAT_ARGB1555,      "ARGB1555"},
            {SDL_PIXELFORMAT_RGBA5551,      "RGBA5551"},
            {SDL_PIXELFORMAT_ABGR1555,      "ABGR1555"},
            {SDL_PIXELFORMAT_BGRA5551,      "BGRA5551"},
            {SDL_PIXELFORMAT_RGB565,        "RGB565"},
            {SDL_PIXELFORMAT_BGR565,        "BGR565"},
            {SDL_PIXELFORMAT_RGB24,         "RGB24"},
            {SDL_PIXELFORMAT_BGR24,         "BGR24"},
            {SDL_PIXELFORMAT_RGB888,        "RGB888"},
            {SDL_PIXELFORMAT_RGBX8888,      "RGBX8888"},
            {SDL_PIXELFORMAT_BGR888,        "BGR888"},
            {SDL_PIXELFORMAT_BGRX8888,      "BGRX8888"},
            {SDL_PIXELFORMAT_ARGB8888,      "ARGB8888"},
            {SDL_PIXELFORMAT_RGBA8888,      "RGBA8888"},
            {SDL_PIXELFORMAT_ABGR8888,      "ABGR8888"},
            {SDL_PIXELFORMAT_BGRA8888,      "BGRA8888"},
            {SDL_PIXELFORMAT_ARGB2101010,   "ARGB2101010"},
            {SDL_PIXELFORMAT_YV12,          "YV12"},
            {SDL_PIXELFORMAT_IYUV,          "IYUV"},
            {SDL_PIXELFORMAT_YUY2,          "YUY2"},
            {SDL_PIXELFORMAT_UYVY,          "UYVY"},
            {SDL_PIXELFORMAT_YVYU,          "YVYU"},
            {SDL_PIXELFORMAT_UNKNOWN,       "Unknown"}};

        fprintf (st, "     Render #%d - %s\n", i, info.name);
        fprintf (st, "        Flags: 0x%X - ", info.flags);
        if (info.flags & SDL_RENDERER_SOFTWARE)
            fprintf (st, "Software|");
        if (info.flags & SDL_RENDERER_ACCELERATED)
            fprintf (st, "Accelerated|");
        if (info.flags & SDL_RENDERER_PRESENTVSYNC)
            fprintf (st, "PresentVSync|");
        if (info.flags & SDL_RENDERER_TARGETTEXTURE)
            fprintf (st, "TargetTexture|");
        fprintf (st, "\n");
        if ((info.max_texture_height != 0) || (info.max_texture_width != 0))
            fprintf (st, "        Max Texture: %d by %d\n", info.max_texture_height, info.max_texture_width);
        fprintf (st, "        Pixel Formats:\n");
        for (j=0; j<info.num_texture_formats; j++) {
            for (k=0; 1; k++) {
                if (PixelFormats[k].format == info.texture_formats[j]) {
                    fprintf (st, "            %s\n", PixelFormats[k].name);
                    break;
                    }
                if (PixelFormats[k].format == SDL_PIXELFORMAT_UNKNOWN) {
                    fprintf (st, "            %s - 0x%X\n", PixelFormats[k].name, info.texture_formats[j]);
                    break;
                    }
                }
            }
        }
    }
if (vid_active) {
    SDL_RendererInfo info;

    SDL_GetRendererInfo (vid_renderer, &info);
    fprintf (st, "  Currently Active Renderer: %s\n", info.name);
    }
if (1) {
    static const char *hints[] = {
#if defined (SDL_HINT_FRAMEBUFFER_ACCELERATION)
                SDL_HINT_FRAMEBUFFER_ACCELERATION   ,
#endif
#if defined (SDL_HINT_RENDER_DRIVER)
                SDL_HINT_RENDER_DRIVER              ,
#endif
#if defined (SDL_HINT_RENDER_OPENGL_SHADERS)
                SDL_HINT_RENDER_OPENGL_SHADERS      ,
#endif
#if defined (SDL_HINT_RENDER_DIRECT3D_THREADSAFE)
                SDL_HINT_RENDER_DIRECT3D_THREADSAFE ,
#endif
#if defined (SDL_HINT_RENDER_DIRECT3D11_DEBUG)
                SDL_HINT_RENDER_DIRECT3D11_DEBUG    ,
#endif
#if defined (SDL_HINT_RENDER_SCALE_QUALITY)
                SDL_HINT_RENDER_SCALE_QUALITY       ,
#endif
#if defined (SDL_HINT_RENDER_VSYNC)
                SDL_HINT_RENDER_VSYNC               ,
#endif
#if defined (SDL_HINT_VIDEO_ALLOW_SCREENSAVER)
                SDL_HINT_VIDEO_ALLOW_SCREENSAVER    ,
#endif
#if defined (SDL_HINT_VIDEO_X11_XVIDMODE)
                SDL_HINT_VIDEO_X11_XVIDMODE         ,
#endif
#if defined (SDL_HINT_VIDEO_X11_XINERAMA)
                SDL_HINT_VIDEO_X11_XINERAMA         ,
#endif
#if defined (SDL_HINT_VIDEO_X11_XRANDR)
                SDL_HINT_VIDEO_X11_XRANDR           ,
#endif
#if defined (SDL_HINT_GRAB_KEYBOARD)
                SDL_HINT_GRAB_KEYBOARD              ,
#endif
#if defined (SDL_HINT_MOUSE_RELATIVE_MODE_WARP)
                SDL_HINT_MOUSE_RELATIVE_MODE_WARP    ,
#endif
#if defined (SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS)
                SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS   ,
#endif
#if defined (SDL_HINT_IDLE_TIMER_DISABLED)
                SDL_HINT_IDLE_TIMER_DISABLED ,
#endif
#if defined (SDL_HINT_ORIENTATIONS)
                SDL_HINT_ORIENTATIONS ,
#endif
#if defined (SDL_HINT_ACCELEROMETER_AS_JOYSTICK)
                SDL_HINT_ACCELEROMETER_AS_JOYSTICK ,
#endif
#if defined (SDL_HINT_XINPUT_ENABLED)
                SDL_HINT_XINPUT_ENABLED ,
#endif
#if defined (SDL_HINT_GAMECONTROLLERCONFIG)
                SDL_HINT_GAMECONTROLLERCONFIG ,
#endif
#if defined (SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS)
                SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS ,
#endif
#if defined (SDL_HINT_ALLOW_TOPMOST)
                SDL_HINT_ALLOW_TOPMOST ,
#endif
#if defined (SDL_HINT_TIMER_RESOLUTION)
                SDL_HINT_TIMER_RESOLUTION ,
#endif
#if defined (SDL_HINT_VIDEO_HIGHDPI_DISABLED)
                SDL_HINT_VIDEO_HIGHDPI_DISABLED ,
#endif
#if defined (SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK)
                SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK ,
#endif
#if defined (SDL_HINT_VIDEO_WIN_D3DCOMPILER)
                SDL_HINT_VIDEO_WIN_D3DCOMPILER              ,
#endif
#if defined (SDL_HINT_VIDEO_WINDOW_SHARE_PIXEL_FORMAT)
                SDL_HINT_VIDEO_WINDOW_SHARE_PIXEL_FORMAT    ,
#endif
#if defined (SDL_HINT_WINRT_PRIVACY_POLICY_URL)
                SDL_HINT_WINRT_PRIVACY_POLICY_URL ,
#endif
#if defined (SDL_HINT_WINRT_PRIVACY_POLICY_LABEL)
                SDL_HINT_WINRT_PRIVACY_POLICY_LABEL ,
#endif
#if defined (SDL_HINT_WINRT_HANDLE_BACK_BUTTON)
                SDL_HINT_WINRT_HANDLE_BACK_BUTTON ,
#endif
#if defined (SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES)
                SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES,
#endif
                NULL};
    fprintf (st, "  Currently Active SDL Hints:\n");
    for (i=0; hints[i]; i++) {
        if (SDL_GetHint (hints[i]))
            fprintf (st, "      %s = %s\n", hints[i], SDL_GetHint (hints[i]));
        }
    }
#endif /* SDL_MAJOR_VERSION != 1 */
#if !defined (SDL_MAIN_AVAILABLE)
if (!vid_active)
    SDL_Quit();
#endif
return SCPE_OK;
}

static t_stat _show_stat;
static FILE *_show_st;
static UNIT *_show_uptr;
static int32 _show_val;
static CONST void *_show_desc;

void vid_show_video_event (void)
{
_show_stat = _vid_show_video (_show_st, _show_uptr, _show_val, _show_desc);
}

t_stat vid_show_video (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
SDL_Event user_event;

_show_stat = -1;
_show_st = st;
_show_uptr = uptr;
_show_val = val;
_show_desc = desc;

user_event.type = SDL_USEREVENT;
user_event.user.code = EVENT_SHOW;
user_event.user.data1 = NULL;
user_event.user.data2 = NULL;
#if defined (SDL_MAIN_AVAILABLE)
while (SDL_PushEvent (&user_event) < 0)
    sim_os_ms_sleep (10);
#else
vid_show_video_event ();
#endif
while (_show_stat == -1)
    SDL_Delay (20);
return _show_stat;
}

static t_stat _vid_screenshot (const char *filename)
{
int stat;
char *fullname = NULL;

if (!vid_active) {
    sim_printf ("No video display is active\n");
    return SCPE_UDIS | SCPE_NOMESSAGE;
    }
fullname = (char *)malloc (strlen(filename) + 5);
if (!filename)
    return SCPE_MEM;
#if SDL_MAJOR_VERSION == 1
#if defined(HAVE_LIBPNG)
if (!match_ext (filename, "bmp")) {
    sprintf (fullname, "%s%s", filename, match_ext (filename, "png") ? "" : ".png");
    stat = SDL_SavePNG(vid_image, fullname);
    }
else {
    sprintf (fullname, "%s", filename);
    stat = SDL_SaveBMP(vid_image, fullname);
    }
#else
sprintf (fullname, "%s%s", filename, match_ext (filename, "bmp") ? "" : ".bmp");
stat = SDL_SaveBMP(vid_image, fullname);
#endif /* defined(HAVE_LIBPNG) */
#else /* SDL_MAJOR_VERSION != 1 */
if (1) {
    SDL_Surface *sshot = sim_end ? SDL_CreateRGBSurface(0, vid_width, vid_height, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000) :
                                   SDL_CreateRGBSurface(0, vid_width, vid_height, 32, 0x0000ff00, 0x000ff000, 0xff000000, 0x000000ff) ;
    SDL_RenderReadPixels(vid_renderer, NULL, SDL_PIXELFORMAT_ARGB8888, sshot->pixels, sshot->pitch);
#if defined(HAVE_LIBPNG)
    if (!match_ext (filename, "bmp")) {
        sprintf (fullname, "%s%s", filename, match_ext (filename, "png") ? "" : ".png");
        stat = SDL_SavePNG(sshot, fullname);
        }
    else {
        sprintf (fullname, "%s", filename);
        stat = SDL_SaveBMP(sshot, fullname);
        }
#else
    sprintf (fullname, "%s%s", filename, match_ext (filename, "bmp") ? "" : ".bmp");
    stat = SDL_SaveBMP(sshot, fullname);
#endif /* defined(HAVE_LIBPNG) */
    SDL_FreeSurface(sshot);
    }
#endif
if (stat) {
    sim_printf ("Error saving screenshot to %s: %s\n", fullname, SDL_GetError());
    free (fullname);
    return SCPE_IOERR | SCPE_NOMESSAGE;
    }
else {
    if (!sim_quiet)
        sim_printf ("Screenshot saved to %s\n", fullname);
    free (fullname);
    return SCPE_OK;
    }
}

static t_stat _screenshot_stat;
static const char *_screenshot_filename;

void vid_screenshot_event (void)
{
_screenshot_stat = _vid_screenshot (_screenshot_filename);
}

t_stat vid_screenshot (const char *filename)
{
SDL_Event user_event;

_screenshot_stat = -1;
_screenshot_filename = filename;

user_event.type = SDL_USEREVENT;
user_event.user.code = EVENT_SCREENSHOT;
user_event.user.data1 = NULL;
user_event.user.data2 = NULL;
#if defined (SDL_MAIN_AVAILABLE)
while (SDL_PushEvent (&user_event) < 0)
    sim_os_ms_sleep (10);
#else
vid_screenshot_event ();
#endif
while (_screenshot_stat == -1)
    SDL_Delay (20);
return _screenshot_stat;
}

#include <SDL_audio.h>
#include <math.h>

const int AMPLITUDE = 20000;
const int SAMPLE_FREQUENCY = 11025;
static int16 *vid_beep_data;
static int vid_beep_offset;
static int vid_beep_duration;
static int vid_beep_samples;

static void vid_audio_callback(void *ctx, Uint8 *stream, int length)
{
int16 *data = (int16 *)stream;
int i, sum, remnant = ((vid_beep_samples - vid_beep_offset) * sizeof (*vid_beep_data));

if (length > remnant) {
    memset (stream + remnant, 0, length - remnant);
    length = remnant;
    if (remnant == 0) {
        SDL_PauseAudio(1);
        return;
        }
    }
memcpy (stream, &vid_beep_data[vid_beep_offset], length);
for (i=sum=0; i<length; i++)
    sum += stream[i];
vid_beep_offset += length / sizeof(*vid_beep_data);
}

static void vid_beep_setup (int duration_ms, int tone_frequency)
{
if (!vid_beep_data) {
    int i;
    SDL_AudioSpec desiredSpec;

    memset (&desiredSpec, 0, sizeof(desiredSpec));
    desiredSpec.freq = SAMPLE_FREQUENCY;
    desiredSpec.format = AUDIO_S16SYS;
    desiredSpec.channels = 1;
    desiredSpec.samples = 2048;
    desiredSpec.callback = vid_audio_callback;

    SDL_OpenAudio(&desiredSpec, NULL);

    vid_beep_samples = (int)((SAMPLE_FREQUENCY * duration_ms) / 1000.0);
    vid_beep_duration = duration_ms;
    vid_beep_data = (int16 *)malloc (sizeof(*vid_beep_data) * vid_beep_samples);
    for (i=0; i<vid_beep_samples; i++)
        vid_beep_data[i] = (int16)(AMPLITUDE * sin(((double)(i * M_PI * tone_frequency)) / SAMPLE_FREQUENCY));
    }
}

static void vid_beep_cleanup (void)
{
SDL_CloseAudio();
free (vid_beep_data);
vid_beep_data = NULL;
}

void vid_beep_event (void)
{
vid_beep_offset = 0;                /* reset to beginning of sample set */
SDL_PauseAudio (0);                 /* Play sound */
}

void vid_beep (void)
{
SDL_Event user_event;

user_event.type = SDL_USEREVENT;
user_event.user.code = EVENT_BEEP;
user_event.user.data1 = NULL;
user_event.user.data2 = NULL;
#if defined (SDL_MAIN_AVAILABLE)
while (SDL_PushEvent (&user_event) < 0)
    sim_os_ms_sleep (10);
#else
vid_beep_event ();
#endif
SDL_Delay (vid_beep_duration + 100);/* Wait for sound to finnish */
}

#else /* !(defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)) */
/* Non-implemented versions */

uint32 vid_mono_palette[2];                             /* Monochrome Color Map */

t_stat vid_open (DEVICE *dptr, const char *title, uint32 width, uint32 height, int flags)
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

t_stat vid_set_cursor (t_bool visible, uint32 width, uint32 height, uint8 *data, uint8 *mask, uint32 hot_x, uint32 hot_y)
{
return SCPE_NOFNC;
}

void vid_set_cursor_position (int32 x, int32 y)
{
return;
}

void vid_refresh (void)
{
return;
}

void vid_beep (void)
{
return;
}

const char *vid_version (void)
{
return "No Video Support";
}

t_stat vid_set_release_key (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
return SCPE_NOFNC;
}

t_stat vid_show_release_key (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
fprintf (st, "no release key");
return SCPE_OK;
}

t_stat vid_show_video (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
fprintf (st, "video support unavailable");
return SCPE_OK;
}

t_stat vid_screenshot (const char *filename)
{
sim_printf ("video support unavailable\n");
return SCPE_NOFNC|SCPE_NOMESSAGE;
}
#endif /* defined(USE_SIM_VIDEO) */
