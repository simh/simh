/*
 * simh sim_video support for XY display simulator
 * Mark Pizzolato <mark@infocomm.com>
 * January 2016
 * Based on win32.c module by Phil Budne
 */

/*
 * Copyright (c) 2016, Mark Pizzolato
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
 * BUGS:
 * Does not allow you to close display window;
 * would need to tear down both system, and system independent data.
 *
 */

#include "sim_video.h"
#include <stdio.h>
#include <stdlib.h>
#include "ws.h"
#include "display.h"

#ifndef PIX_SIZE
#define PIX_SIZE 1
#endif

/*
 * light pen location
 * see ws.h for full description
 */
int ws_lp_x = -1;
int ws_lp_y = -1;

/* A device simulator can optionally set the vid_display_kb_event_process
 * routine pointer to the address of a routine.
 * Simulator code which uses the display library which processes window 
 * keyboard data with code in display/sim_ws.c can use this routine to
 * explicitly get access to keyboard events that arrive in the display 
 * window.  This routine should return 0 if it has handled the event that
 * was passed, and non zero if it didn't handle it.  If the routine address
 * is not set or a non zero return value occurs, then the keyboard event
 * will be processed by the display library which may then be handled as
 * console character input if the device console code is implemented to 
 * accept this.
 */
int (*vid_display_kb_event_process)(SIM_KEY_EVENT *kev) = NULL;

static int xpixels, ypixels;
static int pix_size = PIX_SIZE;
static const char *window_name;
static uint32 *colors = NULL;
static uint32 ncolors = 0, size_colors = 0;
static uint32 *surface = NULL;
static uint32 ws_palette[2];                            /* Monochrome palette */
typedef struct cursor {
    Uint8 *data;
    Uint8 *mask;
    int width;
    int height;
    int hot_x;
    int hot_y;
    } CURSOR;

static CURSOR *arrow_cursor;
static CURSOR *cross_cursor;


static  int
map_key(int k)
{
    switch (k) {
        case SIM_KEY_0:                   return '0';
        case SIM_KEY_1:                   return '1';
        case SIM_KEY_2:                   return '2';
        case SIM_KEY_3:                   return '3';
        case SIM_KEY_4:                   return '4';
        case SIM_KEY_5:                   return '5';
        case SIM_KEY_6:                   return '6';
        case SIM_KEY_7:                   return '7';
        case SIM_KEY_8:                   return '8';
        case SIM_KEY_9:                   return '9';
        case SIM_KEY_A:                   return 'a';
        case SIM_KEY_B:                   return 'b';
        case SIM_KEY_C:                   return 'c';
        case SIM_KEY_D:                   return 'd';
        case SIM_KEY_E:                   return 'e';
        case SIM_KEY_F:                   return 'f';
        case SIM_KEY_G:                   return 'g';
        case SIM_KEY_H:                   return 'h';
        case SIM_KEY_I:                   return 'i';
        case SIM_KEY_J:                   return 'j';
        case SIM_KEY_K:                   return 'k';
        case SIM_KEY_L:                   return 'l';
        case SIM_KEY_M:                   return 'm';
        case SIM_KEY_N:                   return 'n';
        case SIM_KEY_O:                   return 'o';
        case SIM_KEY_P:                   return 'p';
        case SIM_KEY_Q:                   return 'q';
        case SIM_KEY_R:                   return 'r';
        case SIM_KEY_S:                   return 's';
        case SIM_KEY_T:                   return 't';
        case SIM_KEY_U:                   return 'u';
        case SIM_KEY_V:                   return 'v';
        case SIM_KEY_W:                   return 'w';
        case SIM_KEY_X:                   return 'x';
        case SIM_KEY_Y:                   return 'y';
        case SIM_KEY_Z:                   return 'z';
        case SIM_KEY_BACKQUOTE:           return '`';
        case SIM_KEY_MINUS:               return '-';
        case SIM_KEY_EQUALS:              return '=';
        case SIM_KEY_LEFT_BRACKET:        return '[';
        case SIM_KEY_RIGHT_BRACKET:       return ']';
        case SIM_KEY_SEMICOLON:           return ';';
        case SIM_KEY_SINGLE_QUOTE:        return '\'';
        case SIM_KEY_BACKSLASH:           return '\\';
        case SIM_KEY_LEFT_BACKSLASH:      return '\\';
        case SIM_KEY_COMMA:               return ',';
        case SIM_KEY_PERIOD:              return '.';
        case SIM_KEY_SLASH:               return '/';
        case SIM_KEY_BACKSPACE:           return '\b';
        case SIM_KEY_TAB:                 return '\t';
        case SIM_KEY_ENTER:               return '\r';
        case SIM_KEY_SPACE:               return ' ';
        }
    return k;
}


static void
key_to_ascii (SIM_KEY_EVENT *kev)
{
    static t_bool k_ctrl, k_shift, k_alt, k_win;

#define MODKEY(L, R, mod)   \
    case L: case R: mod = (kev->state != SIM_KEYPRESS_UP); break;
#define MODIFIER_KEYS       \
    MODKEY(SIM_KEY_ALT_L,    SIM_KEY_ALT_R,      k_alt)     \
    MODKEY(SIM_KEY_CTRL_L,   SIM_KEY_CTRL_R,     k_ctrl)    \
    MODKEY(SIM_KEY_SHIFT_L,  SIM_KEY_SHIFT_R,    k_shift)   \
    MODKEY(SIM_KEY_WIN_L,    SIM_KEY_WIN_R,      k_win)
#define SPCLKEY(K, LC, UC)  \
    case K:                                                 \
        if (kev->state != SIM_KEYPRESS_UP)                  \
            display_last_char = (unsigned char)(k_shift ? UC : LC);\
        break;
#define SPECIAL_CHAR_KEYS   \
    SPCLKEY(SIM_KEY_BACKQUOTE,      '`',  '~')              \
    SPCLKEY(SIM_KEY_MINUS,          '-',  '_')              \
    SPCLKEY(SIM_KEY_EQUALS,         '=',  '+')              \
    SPCLKEY(SIM_KEY_LEFT_BRACKET,   '[',  '{')              \
    SPCLKEY(SIM_KEY_RIGHT_BRACKET,  ']',  '}')              \
    SPCLKEY(SIM_KEY_SEMICOLON,      ';',  ':')              \
    SPCLKEY(SIM_KEY_SINGLE_QUOTE,   '\'', '"')              \
    SPCLKEY(SIM_KEY_LEFT_BACKSLASH, '\\', '|')              \
    SPCLKEY(SIM_KEY_COMMA,          ',',  '<')              \
    SPCLKEY(SIM_KEY_PERIOD,         '.',  '>')              \
    SPCLKEY(SIM_KEY_SLASH,          '/',  '?')              \
    SPCLKEY(SIM_KEY_ESC,            '\033', '\033')         \
    SPCLKEY(SIM_KEY_BACKSPACE,      '\177', '\177')       \
    SPCLKEY(SIM_KEY_TAB,            '\t', '\t')             \
    SPCLKEY(SIM_KEY_ENTER,          '\r', '\r')             \
    SPCLKEY(SIM_KEY_SPACE,          ' ', ' ')

    switch (kev->key) {
        MODIFIER_KEYS
        SPECIAL_CHAR_KEYS
        case SIM_KEY_0: case SIM_KEY_1: case SIM_KEY_2: case SIM_KEY_3: case SIM_KEY_4:
        case SIM_KEY_5: case SIM_KEY_6: case SIM_KEY_7: case SIM_KEY_8: case SIM_KEY_9:
            if (kev->state != SIM_KEYPRESS_UP)
                display_last_char = (unsigned char)('0' + (kev->key - SIM_KEY_0)); 
            break;
        case SIM_KEY_A: case SIM_KEY_B: case SIM_KEY_C: case SIM_KEY_D: case SIM_KEY_E:
        case SIM_KEY_F: case SIM_KEY_G: case SIM_KEY_H: case SIM_KEY_I: case SIM_KEY_J:
        case SIM_KEY_K: case SIM_KEY_L: case SIM_KEY_M: case SIM_KEY_N: case SIM_KEY_O:
        case SIM_KEY_P: case SIM_KEY_Q: case SIM_KEY_R: case SIM_KEY_S: case SIM_KEY_T:
        case SIM_KEY_U: case SIM_KEY_V: case SIM_KEY_W: case SIM_KEY_X: case SIM_KEY_Y:
        case SIM_KEY_Z: 
            if (kev->state != SIM_KEYPRESS_UP)
                display_last_char = (unsigned char)((kev->key - SIM_KEY_A) + 
                                        (k_ctrl ? 1 : (k_shift ? 'A' : 'a')));
            break;
        }
}

int
ws_poll(int *valp, int maxus)
{
    SIM_MOUSE_EVENT mev;
    SIM_KEY_EVENT kev;

    if (maxus > 1000)
        sim_os_ms_sleep (maxus/1000);

    if (SCPE_OK == vid_poll_mouse (&mev)) {
        unsigned char old_lp_sw = display_lp_sw;
        
        if ((display_lp_sw = mev.b1_state)) {
            ws_lp_x = mev.x_pos;
            ws_lp_y = (ypixels - 1) - mev.y_pos; /* range 0 - (ypixels-1) */
            /* convert to display coordinates */
            ws_lp_x /= pix_size;
            ws_lp_y /= pix_size;
            if (!old_lp_sw && !display_tablet)
                vid_set_cursor (1, cross_cursor->width, cross_cursor->height, cross_cursor->data, cross_cursor->mask, cross_cursor->hot_x, cross_cursor->hot_y);
            }
        else {
            ws_lp_x = ws_lp_y = -1;
            if (old_lp_sw && !display_tablet)
                vid_set_cursor (1, arrow_cursor->width, arrow_cursor->height, arrow_cursor->data, arrow_cursor->mask, arrow_cursor->hot_x, arrow_cursor->hot_y);
            }
        vid_set_cursor_position (mev.x_pos, mev.y_pos);
        }
    if (SCPE_OK == vid_poll_kb (&kev)) {
        if ((vid_display_kb_event_process == NULL) || 
            (vid_display_kb_event_process (&kev) != 0)) {
            switch (kev.state) {
                case SIM_KEYPRESS_DOWN:
                case SIM_KEYPRESS_REPEAT:
                    display_keydown(map_key(kev.key));
                    break;
                case SIM_KEYPRESS_UP:
                    display_keyup(map_key(kev.key));
                    break;
                }
            key_to_ascii (&kev);
            }
        }
    return 1;
}

/* XPM */
static const char *arrow[] = {
  /* width height num_colors chars_per_pixel */
  "    16    16        3            1",
  /* colors */
  "X c #000000",    /* black */
  ". c #ffffff",    /* white */
  "  c None",
  /* pixels */
  "X               ",
  "XX              ",
  "X.X             ",
  "X..X            ",
  "X...X           ",
  "X....X          ",
  "X.....X         ",
  "X......X        ",
  "X.......X       ",
  "X........X      ",
  "X.....XXXXX     ",
  "X..X..X         ",
  "X.X X..X        ",
  "XX   X..X       ",
  "X     X..X      ",
  "       XX       ",
};

/* XPM */
static const char *cross[] = {
  /* width height num_colors chars_per_pixel hot_x hot_y*/
  "    16    16        3            1          7     7",
  /* colors */
  "X c #000000",    /* black */
  ". c #ffffff",    /* white */
  "  c None",
  /* pixels */
  "      XXXX      ",
  "      X..X      ",
  "      X..X      ",
  "      X..X      ",
  "      X..X      ",
  "      X..X      ",
  "XXXXXXX..XXXXXXX",
  "X..............X",
  "X..............X",
  "XXXXXXX..XXXXXXX",
  "      X..X      ",
  "      X..X      ",
  "      X..X      ",
  "      X..X      ",
  "      X..X      ",
  "      XXXX      ",
  "7,7"
};

static CURSOR *ws_create_cursor(const char *image[])
{
int byte, bit, row, col;
Uint8 *data = NULL;
Uint8 *mask = NULL;
char black, white, transparent;
CURSOR *result = NULL;
int width, height, colors, cpp;
int hot_x = 0, hot_y = 0;

if (4 > sscanf(image[0], "%d %d %d %d %d %d", 
               &width, &height, &colors, &cpp, &hot_x, &hot_y))
    return result;
if ((cpp != 1) || (0 != width%8) || (colors != 3))
    return result;
black = image[1][0];
white = image[2][0];
transparent = image[3][0];
data = (Uint8 *)calloc (1, (width / 8) * height);
mask = (Uint8 *)calloc (1, (width / 8) * height);
if (!data || !mask) {
    free (data);
    free (mask);
    return result;
    }
bit = 7;
byte = 0;
for (row=0; row<height; ++row) {
    for (col=0; col<width; ++col) {
        if (image[colors+1+row][col] == black) {
            data[byte] |= (1 << bit);
            mask[byte] |= (1 << bit);
            }
        else
            if (image[colors+1+row][col] == white) {
                mask[byte] |= (1 << bit);
                }
            else
                if (image[colors+1+row][col] != transparent) {
                    free (data);
                    free (mask);
                    return result;
                    }
        --bit;
        if (bit < 0) {
            ++byte;
            bit = 7;
            }
        }
    }
result = (CURSOR *)calloc (1, sizeof(*result));
if (result) {
    result->data = data;
    result->mask = mask;
    result->width = width;
    result->height = height;
    result->hot_x = hot_x;
    result->hot_y = hot_y;
    }
else {
    free (data);
    free (mask);
    }
return result;
}

static void ws_free_cursor (CURSOR *cursor)
{
if (!cursor)
    return;
free (cursor->data);
free (cursor->mask);
free (cursor);
}

/* called from display layer on first display op */
int
ws_init(const char *name, int xp, int yp, int colors, void *dptr)
{
    int i;
    int ret;
    
    arrow_cursor = ws_create_cursor (arrow);
    cross_cursor = ws_create_cursor (cross);
    xpixels = xp;
    ypixels = yp;
    window_name = name;
    surface = (uint32 *)realloc (surface, xpixels*ypixels*sizeof(*surface));
    ret = (0 == vid_open ((DEVICE *)dptr, name, xp*pix_size, yp*pix_size, 0));
    if (ret)
        vid_set_cursor (1, arrow_cursor->width, arrow_cursor->height, arrow_cursor->data, arrow_cursor->mask, arrow_cursor->hot_x, arrow_cursor->hot_y);
    ws_palette[0] = vid_map_rgb (0x00, 0x00, 0x00);     /* black */
    ws_palette[1] = vid_map_rgb (0xFF, 0xFF, 0xFF);     /* white */
    for (i=0; i<xpixels*ypixels; i++)
        surface[i] = ws_palette[0];
    return ret;
}

void
ws_shutdown(void)
{
ws_free_cursor(arrow_cursor);
ws_free_cursor(cross_cursor);
vid_close();
}

void *
ws_color_rgb(int r, int g, int b)
{
    uint32 color, i;
    
    color = vid_map_rgb ((r >> 8) & 0xFF, (g >> 8) & 0xFF, (b >> 8) & 0xFF);
    for (i=0; i<ncolors; i++) {
        if (colors[i] == color)
            return &colors[i];
        }
    if (ncolors == size_colors) {
        colors = (uint32 *)realloc (colors, (ncolors + 1000) * sizeof (*colors));
        size_colors += 1000;
        if (size_colors == 1000) {
            colors[0] = ws_palette[0];
            colors[1] = ws_palette[1];
            ncolors = 2;
            }
        }
    colors[ncolors] = color;
    ++ncolors;
    return (void *)&colors[ncolors-1];
}

void *
ws_color_black(void)
{
    return (void *)&ws_palette[0];
}

void *
ws_color_white(void)
{
    return (void *)&ws_palette[1];
}

void
ws_display_point(int x, int y, void *color)
{
    uint32 *brush = (uint32 *)color;

    if (x > xpixels || y > ypixels)
        return;

    y = ypixels - 1 - y;                /* invert y, top left origin */

    if (brush == NULL)
        brush = (uint32 *)ws_color_black ();
    if (pix_size > 1) {
        int i, j;
        
        for (i=0; i<pix_size; i++)
            for (j=0; j<pix_size; j++)
                surface[(y + i)*xpixels + x + j] = *brush;
        }
    else
        surface[y*xpixels + x] = *brush;
}
  
void
ws_sync(void) {
    vid_draw (0, 0, xpixels, ypixels, surface);
    vid_refresh ();
}

void
ws_beep(void) {
vid_beep ();
}

unsigned long
os_elapsed(void)
{
static int tnew;
unsigned long ret;
static uint32 t[2];

t[tnew] = sim_os_msec();
if (t[!tnew] == 0)
    ret = ~0L;                      /* +INF */
else
    ret = (t[tnew] - t[!tnew]) * 1000;/* usecs */
tnew = !tnew;                       /* Ecclesiastes III */
return ret;
}
