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

#ifndef SIM_VIDEO_H_
#define SIM_VIDEO_H_     0

#include "sim_defs.h"

#ifdef  __cplusplus
extern "C" {
#endif

#define SIM_KEYPRESS_DOWN      0                        /* key states */
#define SIM_KEYPRESS_UP        1
#define SIM_KEYPRESS_REPEAT    2


#define SIM_KEY_F1             0                        /* key syms */
#define SIM_KEY_F2             1
#define SIM_KEY_F3             2
#define SIM_KEY_F4             3
#define SIM_KEY_F5             4
#define SIM_KEY_F6             5
#define SIM_KEY_F7             6
#define SIM_KEY_F8             7
#define SIM_KEY_F9             8
#define SIM_KEY_F10            9
#define SIM_KEY_F11            10
#define SIM_KEY_F12            11

#define SIM_KEY_0              12
#define SIM_KEY_1              13
#define SIM_KEY_2              14
#define SIM_KEY_3              15
#define SIM_KEY_4              16
#define SIM_KEY_5              17
#define SIM_KEY_6              18
#define SIM_KEY_7              19
#define SIM_KEY_8              20
#define SIM_KEY_9              21

#define SIM_KEY_A              22
#define SIM_KEY_B              23
#define SIM_KEY_C              24
#define SIM_KEY_D              25
#define SIM_KEY_E              26
#define SIM_KEY_F              27
#define SIM_KEY_G              28
#define SIM_KEY_H              29
#define SIM_KEY_I              30
#define SIM_KEY_J              31
#define SIM_KEY_K              32
#define SIM_KEY_L              33
#define SIM_KEY_M              34
#define SIM_KEY_N              35
#define SIM_KEY_O              36
#define SIM_KEY_P              37
#define SIM_KEY_Q              38
#define SIM_KEY_R              39
#define SIM_KEY_S              40
#define SIM_KEY_T              41
#define SIM_KEY_U              42
#define SIM_KEY_V              43
#define SIM_KEY_W              44
#define SIM_KEY_X              45
#define SIM_KEY_Y              46
#define SIM_KEY_Z              47

#define SIM_KEY_BACKQUOTE      48
#define SIM_KEY_MINUS          49
#define SIM_KEY_EQUALS         50
#define SIM_KEY_LEFT_BRACKET   51
#define SIM_KEY_RIGHT_BRACKET  52
#define SIM_KEY_SEMICOLON      53
#define SIM_KEY_SINGLE_QUOTE   54
#define SIM_KEY_BACKSLASH      55
#define SIM_KEY_LEFT_BACKSLASH 56
#define SIM_KEY_COMMA          57
#define SIM_KEY_PERIOD         58
#define SIM_KEY_SLASH          59

#define SIM_KEY_PRINT          60
#define SIM_KEY_SCRL_LOCK      61
#define SIM_KEY_PAUSE          62

#define SIM_KEY_ESC            63
#define SIM_KEY_BACKSPACE      64
#define SIM_KEY_TAB            65
#define SIM_KEY_ENTER          66
#define SIM_KEY_SPACE          67
#define SIM_KEY_INSERT         68
#define SIM_KEY_DELETE         69
#define SIM_KEY_HOME           70
#define SIM_KEY_END            71
#define SIM_KEY_PAGE_UP        72
#define SIM_KEY_PAGE_DOWN      73
#define SIM_KEY_UP             74
#define SIM_KEY_DOWN           75
#define SIM_KEY_LEFT           76
#define SIM_KEY_RIGHT          77

#define SIM_KEY_CAPS_LOCK      78
#define SIM_KEY_NUM_LOCK       79

#define SIM_KEY_ALT_L          80
#define SIM_KEY_ALT_R          81
#define SIM_KEY_CTRL_L         82
#define SIM_KEY_CTRL_R         83
#define SIM_KEY_SHIFT_L        84
#define SIM_KEY_SHIFT_R        85
#define SIM_KEY_WIN_L          86
#define SIM_KEY_WIN_R          87
#define SIM_KEY_MENU           88

#define SIM_KEY_KP_ADD         89
#define SIM_KEY_KP_SUBTRACT    90
#define SIM_KEY_KP_END         91
#define SIM_KEY_KP_DOWN        92
#define SIM_KEY_KP_PAGE_DOWN   93
#define SIM_KEY_KP_LEFT        94
#define SIM_KEY_KP_RIGHT       95
#define SIM_KEY_KP_HOME        96
#define SIM_KEY_KP_UP          97
#define SIM_KEY_KP_PAGE_UP     98
#define SIM_KEY_KP_INSERT      99
#define SIM_KEY_KP_DELETE      100
#define SIM_KEY_KP_5           101
#define SIM_KEY_KP_ENTER       102
#define SIM_KEY_KP_MULTIPLY    103
#define SIM_KEY_KP_DIVIDE      104

#define SIM_KEY_UNKNOWN        200

typedef struct VID_DISPLAY VID_DISPLAY;

struct mouse_event {
    int32 x_rel;                                          /* X axis relative motion */
    int32 y_rel;                                          /* Y axis relative motion */
    int32 x_pos;                                          /* X axis position */
    int32 y_pos;                                          /* Y axis position */
    t_bool b1_state;                                      /* state of button 1 */
    t_bool b2_state;                                      /* state of button 2 */
    t_bool b3_state;                                      /* state of button 3 */
    DEVICE *dev;                                          /* which device */
    VID_DISPLAY *vptr;                                    /* which display */
    };

struct key_event {
    uint32 key;                                           /* key sym */
    uint32 state;                                         /* key state change */
    DEVICE *dev;                                          /* which device */
    VID_DISPLAY *vptr;                                    /* which display */
    };

typedef struct mouse_event SIM_MOUSE_EVENT;
typedef struct key_event SIM_KEY_EVENT;

t_stat vid_open (DEVICE *dptr, const char *title, uint32 width, uint32 height, int flags);
#define SIM_VID_INPUTCAPTURED       1                       /* Mouse and Keyboard input captured (calling */
                                                            /* code responsible for cursor display in video) */
typedef void (*VID_QUIT_CALLBACK)(void);
t_stat vid_register_quit_callback (VID_QUIT_CALLBACK callback);
typedef void (*VID_GAMEPAD_CALLBACK)(int, int, int);
t_stat vid_register_gamepad_motion_callback (VID_GAMEPAD_CALLBACK);
t_stat vid_register_gamepad_button_callback (VID_GAMEPAD_CALLBACK);
t_stat vid_close (void);
t_stat vid_poll_kb (SIM_KEY_EVENT *ev);
t_stat vid_poll_mouse (SIM_MOUSE_EVENT *ev);
uint32 vid_map_rgb (uint8 r, uint8 g, uint8 b);
void vid_draw (int32 x, int32 y, int32 w, int32 h, uint32 *buf);
void vid_beep (void);
void vid_refresh (void);
const char *vid_version (void);
const char *vid_key_name (int32 key);
t_stat vid_set_cursor (t_bool visible, uint32 width, uint32 height, uint8 *data, uint8 *mask, uint32 hot_x, uint32 hot_y);
t_stat vid_set_release_key (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat vid_show_release_key (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat vid_show_video (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat vid_show (FILE* st, DEVICE *dptr,  UNIT* uptr, int32 val, CONST char* desc);
t_stat vid_screenshot (const char *filename);
t_bool vid_is_fullscreen (void);
t_stat vid_set_fullscreen (t_bool flag);

extern int vid_active;
void vid_set_cursor_position (int32 x, int32 y);        /* cursor position (set by calling code) */

t_stat vid_open_window (VID_DISPLAY **vptr, DEVICE *dptr, const char *title, uint32 width, uint32 height, int flags);
t_stat vid_close_window (VID_DISPLAY *vptr);
t_stat vid_close_all (void);
uint32 vid_map_rgb_window (VID_DISPLAY *vptr, uint8 r, uint8 g, uint8 b);
void vid_draw_window (VID_DISPLAY *vptr, int32 x, int32 y, int32 w, int32 h, uint32 *buf);
void vid_refresh_window (VID_DISPLAY *vptr);
t_stat vid_set_cursor_window (VID_DISPLAY *vptr, t_bool visible, uint32 width, uint32 height, uint8 *data, uint8 *mask, uint32 hot_x, uint32 hot_y);
t_bool vid_is_fullscreen_window (VID_DISPLAY *vptr);
t_stat vid_set_fullscreen_window (VID_DISPLAY *vptr, t_bool flag);
void vid_set_cursor_position_window (VID_DISPLAY *vptr, int32 x, int32 y);        /* cursor position (set by calling code) */

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
extern int (*vid_display_kb_event_process)(SIM_KEY_EVENT *kev);

#define SIM_VID_DBG_MOUSE   0x10000000
#define SIM_VID_DBG_CURSOR  0x20000000
#define SIM_VID_DBG_KEY     0x40000000
#define SIM_VID_DBG_VIDEO   0x80000000

#ifdef  __cplusplus
}
#endif

#if defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)
#include <SDL.h>
#endif /* HAVE_LIBSDL */

#endif
