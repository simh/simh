/* vax_lk.h: DEC Keyboard (LK201)

   Copyright (c) 2017, Matt Burke

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

   lk           LK201 keyboard
*/

#ifndef _VAX_LK_H_
#define _VAX_LK_H_

#include "vax_defs.h"
#include "sim_video.h"

/* standard key codes */

#define LK_UNKNOWN      0x00

#define LK_TR_0         0xEF
#define LK_TR_1         0xC0
#define LK_TR_2         0xC5
#define LK_TR_3         0xCB
#define LK_TR_4         0xD0
#define LK_TR_5         0xD6
#define LK_TR_6         0xDB
#define LK_TR_7         0xE0
#define LK_TR_8         0xE5
#define LK_TR_9         0xEA
#define LK_A            0xC2
#define LK_B            0xD9
#define LK_C            0xCE
#define LK_D            0xCD
#define LK_E            0xCC
#define LK_F            0xD2
#define LK_G            0xD8
#define LK_H            0xDD
#define LK_I            0xE6
#define LK_J            0xE2
#define LK_K            0xE7
#define LK_L            0xEC
#define LK_M            0xE3
#define LK_N            0xDE
#define LK_O            0xEB
#define LK_P            0xF0
#define LK_Q            0xC1
#define LK_R            0xD1
#define LK_S            0xC7
#define LK_T            0xD7
#define LK_U            0xE1
#define LK_V            0xD3
#define LK_W            0xC6
#define LK_X            0xC8
#define LK_Y            0xDC
#define LK_Z            0xC3
#define LK_SPACE        0xD4
#define LK_SEMICOLON    0xF2
#define LK_PLUS         0xF5
#define LK_COMMA        0xE8
#define LK_UBAR         0xF9
#define LK_PERIOD       0xED
#define LK_QMARK        0xF3
#define LK_QUOTE        0xFB
#define LK_LBRACE       0xFA
#define LK_RBRACE       0xF6
#define LK_VBAR         0xF7
#define LK_TILDE        0xBF
#define LK_KP_0         0x92
#define LK_KP_1         0x96
#define LK_KP_2         0x97
#define LK_KP_3         0x98
#define LK_KP_4         0x99
#define LK_KP_5         0x9A
#define LK_KP_6         0x9B
#define LK_KP_7         0x9D
#define LK_KP_8         0x9E
#define LK_KP_9         0x9F
#define LK_KP_PF1       0xA1
#define LK_KP_PF2       0xA2
#define LK_KP_PF3       0xA3
#define LK_KP_PF4       0xA4
#define LK_KP_HYPHEN    0xA0
#define LK_KP_COMMA     0x9C
#define LK_KP_PERIOD    0x94
#define LK_KP_ENTER     0x95
#define LK_DELETE       0xBC
#define LK_TAB          0xBE
#define LK_RETURN       0xBD
#define LK_META         0xB1
#define LK_LOCK         0xB0
#define LK_SHIFT        0xAE
#define LK_CTRL         0xAF
#define LK_LEFT         0xA7
#define LK_RIGHT        0xA8
#define LK_UP           0xAA
#define LK_DOWN         0xA9
#define LK_REMOVE       0x8C
#define LK_NEXT_SCREEN  0x8F
#define LK_PREV_SCREEN  0x8E
#define LK_INSERT_HERE  0x8B
#define LK_FIND         0x8A
#define LK_SELECT       0x8D
#define LK_F1           0x56
#define LK_F2           0x57
#define LK_F3           0x58
#define LK_F4           0x59
#define LK_F5           0x5A
#define LK_F6           0x64
#define LK_F7           0x65
#define LK_F8           0x66
#define LK_F9           0x67
#define LK_F10          0x68
#define LK_F11          0x71
#define LK_F12          0x72

/* special codes */

#define LK_ALLUP        0xB3                            /* all up */
#define LK_METRONOME    0xB4                            /* metronome code */
#define LK_OUTERR       0xB5                            /* output error */
#define LK_INERR        0xB6                            /* input error */
#define LK_LOCKACK      0xB7                            /* kbd locked ack */
#define LK_TESTACK      0xB8                            /* test mode ack */
#define LK_PREDOWN      0xB9                            /* prefix to keys down */
#define LK_MODEACK      0xBA                            /* mode change ack */

/* interface functions */

t_stat lk_wr (uint8 c);
t_stat lk_rd (uint8 *c);
void lk_event (SIM_KEY_EVENT *ev);

#endif
