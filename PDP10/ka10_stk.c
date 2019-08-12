/* ka10_stk.c: Stanford keyboard.

   Copyright (c) 2018, Lars Brinkhoff

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This is a device which interfaces with a Stanford keyboard.  It's
   specific to the MIT AI lab PDP-10.
*/

#include <time.h>
#include "kx10_defs.h"

#ifdef USE_DISPLAY
#if NUM_DEVS_STK > 0

#include "sim_video.h"
#include "display/display.h"

#define STK_DEVNUM      070

/* CONI/O bits. */
#define STK_PIA         0000007
#define STK_DONE        0000010

/* Bucky bits. */
#define SHFT   00100
#define CTRL   00200
#define TOP    00400
#define META   01000

static t_stat      stk_svc (UNIT *uptr);
static t_stat      stk_devio(uint32 dev, uint64 *data);
static t_stat      stk_reset (DEVICE *dptr);
static const char  *stk_description (DEVICE *dptr);

static uint64 status = 0;
static int key_code = 0;

UNIT                stk_unit[] = {
    {UDATA(stk_svc, UNIT_DISABLE, 0)},  /* 0 */
};
DIB stk_dib = {STK_DEVNUM, 1, &stk_devio, NULL};

MTAB stk_mod[] = {
    { 0 }
    };

DEVICE              stk_dev = {
    "STK", stk_unit, NULL, stk_mod,
    1, 8, 0, 1, 8, 36,
    NULL, NULL, &stk_reset, NULL, NULL, NULL,
    &stk_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, NULL,
    NULL, NULL, NULL, NULL, NULL, &stk_description
};

/* Special key codes. */
#define CR    033
#define BKSL  034
#define LF    035
#define TAB   045
#define FF    046
#define VT    047
#define BS    074
#define ALT   077 /* Not sure if 42, 75, 76, or 77. */

/* This maps ASCII codes to Stanford key codes plus bucky bits. */
static int translate[] = {
  0,       CTRL|001,CTRL|002,CTRL|003,CTRL|004,CTRL|005,CTRL|006,CTRL|007,
  CTRL|010,TAB,     LF,      VT,      FF,      CR,      CTRL|016,CTRL|017,
  CTRL|020,CTRL|021,CTRL|022,CTRL|023,CTRL|024,CTRL|025,CTRL|026,CTRL|027,
  CTRL|030,CTRL|031,CTRL|032,ALT,     CTRL|034,CTRL|035,0,       CTRL|037,
  ' ',     SHFT|',',TOP|031, TOP|022, SHFT|'6',SHFT|'7',TOP|024, TOP|011,
  '(',     ')',     '*',     '+',     ',',     '-',     '.',     '/',
  '0',     '1',     '2',     '3',     '4',     '5',     '6',     '7',
  '8',     '9',     ':',     ';',     TOP|004, TOP|010, TOP|006, TOP|'.',
  TOP|005, SHFT|001,SHFT|002,SHFT|003,SHFT|004,SHFT|005,SHFT|006,SHFT|007,
  SHFT|010,SHFT|011,SHFT|012,SHFT|013,SHFT|014,SHFT|015,SHFT|016,SHFT|017,
  SHFT|020,SHFT|021,SHFT|022,SHFT|023,SHFT|024,SHFT|025,SHFT|026,SHFT|027,
  SHFT|030,SHFT|031,SHFT|032,TOP|'(', BKSL,    TOP|')', 0,       TOP|'9',
  TOP|025, 001,     002,     003,     004,     005,     006,     007,
  010,     011,     012,     013,     014,     015,     016,     017,
  020,     021,     022,     023,     024,     025,     026,     027,
  030,     031,     032,     TOP|017, SHFT|'+',TOP|020, SHFT|'8',BS
};

static int bucky = 0;

static int stk_modifiers (SIM_KEY_EVENT *kev)
{
  if (kev->state == SIM_KEYPRESS_DOWN) {
    switch (kev->key) {
    case SIM_KEY_SHIFT_L:
    case SIM_KEY_SHIFT_R:
      bucky |= SHFT;
      return 1;
    case SIM_KEY_CTRL_L:
    case SIM_KEY_CTRL_R:
    case SIM_KEY_CAPS_LOCK:
      bucky |= CTRL;
      return 1;
    case SIM_KEY_WIN_L:
    case SIM_KEY_WIN_R:
      bucky |= TOP;
      return 1;
    case SIM_KEY_ALT_L:
    case SIM_KEY_ALT_R:
      bucky |= META;
      return 1;
    }
  } else if (kev->state == SIM_KEYPRESS_UP) {
    switch (kev->key) {
    case SIM_KEY_SHIFT_L:
    case SIM_KEY_SHIFT_R:
      bucky &= ~SHFT;
      return 1;
    case SIM_KEY_CTRL_L:
    case SIM_KEY_CTRL_R:
    case SIM_KEY_CAPS_LOCK:
      bucky &= ~CTRL;
      return 1;
    case SIM_KEY_WIN_L:
    case SIM_KEY_WIN_R:
      bucky &= ~TOP;
      return 1;
    case SIM_KEY_ALT_L:
    case SIM_KEY_ALT_R:
      bucky &= ~META;
      return 1;
    }
  }
  return 0;
}

static int stk_keys (SIM_KEY_EVENT *kev)
{
  if (kev->state == SIM_KEYPRESS_UP)
    return 0;

  switch (kev->key) {
  case SIM_KEY_0:
    key_code = bucky | '+';
    return 1;
  case SIM_KEY_1:
    key_code = bucky | '1';
    return 1;
  case SIM_KEY_2:
    key_code = bucky | '2';
    return 1;
  case SIM_KEY_3:
    key_code = bucky | '3';
    return 1;
  case SIM_KEY_4:
    key_code = bucky | '4';
    return 1;
  case SIM_KEY_5:
    key_code = bucky | '5';
    return 1;
  case SIM_KEY_6:
    key_code = bucky | '6';
    return 1;
  case SIM_KEY_7:
    key_code = bucky | '7';
    return 1;
  case SIM_KEY_8:
    key_code = bucky | '8';
    return 1;
  case SIM_KEY_9:
    key_code = bucky | '9';
    return 1;
  case SIM_KEY_A:
    key_code = bucky | 001;
    return 1;
  case SIM_KEY_B:
    key_code = bucky | 002;
    return 1;
  case SIM_KEY_C:
    key_code = bucky | 003;
    return 1;
  case SIM_KEY_D:
    key_code = bucky | 004;
    return 1;
  case SIM_KEY_E:
    key_code = bucky | 005;
    return 1;
  case SIM_KEY_F:
    key_code = bucky | 006;
    return 1;
  case SIM_KEY_G:
    key_code = bucky | 007;
    return 1;
  case SIM_KEY_H:
    key_code = bucky | 010;
    return 1;
  case SIM_KEY_I:
    key_code = bucky | 011;
    return 1;
  case SIM_KEY_J:
    key_code = bucky | 012;
    return 1;
  case SIM_KEY_K:
    key_code = bucky | 013;
    return 1;
  case SIM_KEY_L:
    key_code = bucky | 014;
    return 1;
  case SIM_KEY_M:
    key_code = bucky | 015;
    return 1;
  case SIM_KEY_N:
    key_code = bucky | 016;
    return 1;
  case SIM_KEY_O:
    key_code = bucky | 017;
    return 1;
  case SIM_KEY_P:
    key_code = bucky | 020;
    return 1;
  case SIM_KEY_Q:
    key_code = bucky | 021;
    return 1;
  case SIM_KEY_R:
    key_code = bucky | 022;
    return 1;
  case SIM_KEY_S:
    key_code = bucky | 023;
    return 1;
  case SIM_KEY_T:
    key_code = bucky | 024;
    return 1;
  case SIM_KEY_U:
    key_code = bucky | 025;
    return 1;
  case SIM_KEY_V:
    key_code = bucky | 026;
    return 1;
  case SIM_KEY_W:
    key_code = bucky | 027;
    return 1;
  case SIM_KEY_X:
    key_code = bucky | 030;
    return 1;
  case SIM_KEY_Y:
    key_code = bucky | 031;
    return 1;
  case SIM_KEY_Z:
    key_code = bucky | 032;
    return 1;
  case SIM_KEY_BACKQUOTE:
    key_code = bucky | '0';
    return 1;
  case SIM_KEY_MINUS:
    key_code = bucky | '-';
    return 1;
  case SIM_KEY_EQUALS:
    key_code = bucky | '*';
    return 1;
  case SIM_KEY_LEFT_BRACKET:
    key_code = bucky | '(';
    return 1;
  case SIM_KEY_RIGHT_BRACKET:
    key_code = bucky | ')';
    return 1;
  case SIM_KEY_SEMICOLON:
    key_code = bucky | ';';
    return 1;
  case SIM_KEY_SINGLE_QUOTE:
    key_code = bucky | ':';
    return 1;
  case SIM_KEY_BACKSLASH:
    key_code = bucky | BKSL;
    return 1;
  case SIM_KEY_LEFT_BACKSLASH:
    key_code = bucky | BKSL;
    return 1;
  case SIM_KEY_COMMA:
    key_code = bucky | ',';
    return 1;
  case SIM_KEY_PERIOD:
    key_code = bucky | '.';
    return 1;
  case SIM_KEY_SLASH:
    key_code = bucky | '/';
    return 1;
  case SIM_KEY_ESC:
    key_code = bucky | ALT;
    return 1;
  case SIM_KEY_BACKSPACE:
  case SIM_KEY_DELETE:
    key_code = bucky | BS;
    return 1;
  case SIM_KEY_TAB:
    key_code = bucky | TAB;
    return 1;
  case SIM_KEY_ENTER:
    key_code = bucky | CR;
    return 1;
  case SIM_KEY_SPACE:
    key_code = bucky | ' ';
    return 1;
  default:
    return 0;
  }
}

static int stk_keyboard (SIM_KEY_EVENT *kev)
{
    if (stk_modifiers (kev))
      return 0;
    
    if (stk_keys (kev)) {
      status |= STK_DONE;
      set_interrupt(STK_DEVNUM, status & STK_PIA);
      return 0;
    }

    return 1;
}

static t_stat stk_svc (UNIT *uptr)
{
  int c = SCPE_OK;

#ifdef USE_DISPLAY
  if (display_last_char) {
    c = display_last_char | SCPE_KFLAG;
    display_last_char = 0;
  }
#endif

  if (c & SCPE_KFLAG) {
    key_code = translate[c & 0177];
    status |= STK_DONE;
    set_interrupt(STK_DEVNUM, status & STK_PIA);
  }

  sim_activate (uptr, 100000);

  if (c & SCPE_KFLAG)
    return SCPE_OK;
  else
    return c;
}

t_stat stk_devio(uint32 dev, uint64 *data)
{
    DEVICE *dptr = &stk_dev;

    switch(dev & 07) {
    case CONO:
        status &= ~STK_PIA;
        status |= *data & STK_PIA;
        if (status & STK_PIA)
          sim_activate (stk_unit, 1);
        else
          sim_cancel (stk_unit);
        break;
    case CONI:
        *data = status;
        break;
    case DATAO:
        break;
    case DATAI:
        status &= ~STK_DONE;
        clr_interrupt(STK_DEVNUM);
        *data = key_code;
        break;
    }

    return SCPE_OK;
}

static t_stat stk_reset (DEVICE *dptr)
{
    vid_display_kb_event_process = stk_keyboard;
    return SCPE_OK;
}

const char *stk_description (DEVICE *dptr)
{
    return "Stanford keyboard";
}
#endif
#endif
