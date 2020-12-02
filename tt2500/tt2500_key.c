/* tt2500_key.c: TT2500 keyboard device

   Copyright (c) 2020, Lars Brinkhoff

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
   LARS BRINKHOFF BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Lars Brinkhoff shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Lars Brinkhoff.
*/

#include "tt2500_defs.h"
#include "sim_video.h"

/* Debug */
#define DBG             0001

#define KEY_DISPLAY  (1 << UNIT_V_UF)
#define KEY_CONSOLE  (2 << UNIT_V_UF)
#define KEY_TYPE     (3 << UNIT_V_UF)

#define SHFT  01000
#define CTRL  02000
#define META  04000

#define NOKEY 0177777

static uint16 KBUF;
static uint16 suffix = NOKEY;
static uint16 modifiers;

/* Function declaration. */
static t_stat key_svc (UNIT *uptr);
static t_stat key_reset (DEVICE *dptr);
static uint16 key_read (uint16 reg);
static void key_write (uint16 reg, uint16 data);

#if defined(USE_DISPLAY) || (defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL))
#define KBD_FLAGS KEY_DISPLAY
#else
#define KBD_FLAGS KEY_CONSOLE
#endif

static UNIT key_unit = {
  UDATA (&key_svc, UNIT_IDLE+KBD_FLAGS, 0)
};

static REG key_reg[] = {
  { ORDATAD (KBUF, KBUF, 16, "Keyboard buffer") },
  { NULL }
};

MTAB key_mod[] = {
  { KEY_TYPE, KEY_DISPLAY, "DISPLAY", "DISPLAY", NULL, NULL, NULL,
              "Get keyboard events from display windows"},
  { KEY_TYPE, KEY_CONSOLE, "CONSOLE", "CONSOLE", NULL, NULL, NULL,
              "Get keyboard events from console"},
  { 0 }
};

static DEBTAB key_deb[] = {
  { "DBG", DBG },
  { NULL, 0 }
};

static TTDEV key_ttdev = {
  { REG_KEY, 0, 0, 0 },
  key_read,
  key_write,
};

DEVICE key_dev = {
  "KEY", &key_unit, key_reg, key_mod,
  0, 8, 16, 1, 8, 16,
  NULL, NULL, &key_reset,
  NULL, NULL, NULL, &key_ttdev,
  DEV_DISABLE | DEV_DEBUG, 0, key_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static t_stat key_svc (UNIT *uptr)
{
  t_stat ch = sim_poll_kbd ();

  if ((ch & SCPE_KFLAG) == 0) {
    sim_activate_after (&key_unit, 10000);
    return ch;
  }

  if (ch & SCPE_BREAK)
    KBUF = 0377;
  else
    KBUF = ch & 0177;
  flag_on (FLAG_KB);
  sim_debug (DBG, &key_dev, "Received character %03o\n", KBUF);
  return SCPE_OK;
}

static int
key_modifiers (SIM_KEY_EVENT *ev)
{
  int code = 0;

  switch (ev->key) {
  case SIM_KEY_SHIFT_L:
  case SIM_KEY_SHIFT_R:
    code = SHFT;
    break;
  case SIM_KEY_CTRL_L:
  case SIM_KEY_CTRL_R:
  case SIM_KEY_CAPS_LOCK:
    code = CTRL;
    break;
  case SIM_KEY_ALT_L:
  case SIM_KEY_ALT_R:
    code = META;
    break;
  }

  if (ev->state == SIM_KEYPRESS_DOWN)
    modifiers |= code;
  else if (ev->state == SIM_KEYPRESS_UP)
    modifiers &= ~code;

  return code != 0;
}

static int
key_both (uint32 key)
{
  uint16 code = NOKEY;
  switch (key) {
  case SIM_KEY_TAB:
    code = 0011;
    break;
  case SIM_KEY_PAGE_UP:
    code = 0014;
    break;
  case SIM_KEY_ENTER:
    code = 0015;
    break;
  case SIM_KEY_ESC:
    code = 0033;
    break;
  case SIM_KEY_SPACE:
    code = 0040;
    break;
  case SIM_KEY_BACKSPACE:
  case SIM_KEY_DELETE:
    code = 0177;
    break;
  case SIM_KEY_F11:
    vid_set_fullscreen (!vid_is_fullscreen ());
    break;
  }
  return code;
}

static int
key_shift (uint32 key)
{
  uint16 code = key_both (key);
  if (code != NOKEY)
    return code;

  switch (key) {
  case SIM_KEY_0:
    code = ')';
    break;
  case SIM_KEY_1:
    code = '!';
    break;
  case SIM_KEY_2:
    return '@';
    break;
  case SIM_KEY_3:
    code = '#';
    break;
  case SIM_KEY_4:
    code = '$';
    break;
  case SIM_KEY_5:
    code = '%';
    break;
  case SIM_KEY_6:
    return '^';
  case SIM_KEY_7:
    code = '&';
    break;
  case SIM_KEY_8:
    code = '*';
    break;
  case SIM_KEY_9:
    code = '(';
    break;
  case SIM_KEY_A:
    code = 'A';
    break;
  case SIM_KEY_B:
    code = 'B';
    break;
  case SIM_KEY_C:
    code = 'C';
    break;
  case SIM_KEY_D:
    code = 'D';
    break;
  case SIM_KEY_E:
    code = 'E';
    break;
  case SIM_KEY_F:
    code = 'F';
    break;
  case SIM_KEY_G:
    code = 'G';
    break;
  case SIM_KEY_H:
    code = 'H';
    break;
  case SIM_KEY_I:
    code = 'I';
    break;
  case SIM_KEY_J:
    code = 'J';
    break;
  case SIM_KEY_K:
    code = 'K';
    break;
  case SIM_KEY_L:
    code = 'L';
    break;
  case SIM_KEY_M:
    code = 'M';
    break;
  case SIM_KEY_N:
    code = 'N';
    break;
  case SIM_KEY_O:
    code = 'O';
    break;
  case SIM_KEY_P:
    code = 'P';
    break;
  case SIM_KEY_Q:
    code = 'Q';
    break;
  case SIM_KEY_R:
    code = 'R';
    break;
  case SIM_KEY_S:
    code = 'S';
    break;
  case SIM_KEY_T:
    code = 'T';
    break;
  case SIM_KEY_U:
    code = 'U';
    break;
  case SIM_KEY_V:
    code = 'V';
    break;
  case SIM_KEY_W:
    code = 'W';
    break;
  case SIM_KEY_X:
    code = 'X';
    break;
  case SIM_KEY_Y:
    code = 'Y';
    break;
  case SIM_KEY_Z:
    code = 'Z';
    break;
  case SIM_KEY_BACKQUOTE:
    return '~';
  case SIM_KEY_MINUS:
    return '_';
  case SIM_KEY_EQUALS:
    code = '+';
    break;
  case SIM_KEY_LEFT_BRACKET:
    return '{';
  case SIM_KEY_RIGHT_BRACKET:
    return '}';
  case SIM_KEY_SEMICOLON:
    code = ':';
    break;
  case SIM_KEY_SINGLE_QUOTE:
    code = '"';
    break;
  case SIM_KEY_BACKSLASH:
  case SIM_KEY_LEFT_BACKSLASH:
    return '|';
  case SIM_KEY_COMMA:
    code = '<';
    break;
  case SIM_KEY_PERIOD:
    code = '>';
    break;
  case SIM_KEY_SLASH:
    code = '?';
    break;
  }
  return code;
}

static int
key_noshift (uint32 key)
{
  uint16 code = key_both (key);
  if (code != NOKEY)
    return code;

  switch (key) {
  case SIM_KEY_0:
    code = '0';
    break;
  case SIM_KEY_1:
    code = '1';
    break;
  case SIM_KEY_2:
    code = '2';
    break;
  case SIM_KEY_3:
    code = '3';
    break;
  case SIM_KEY_4:
    code = '4';
    break;
  case SIM_KEY_5:
    code = '5';
    break;
  case SIM_KEY_6:
    code = '6';
    break;
  case SIM_KEY_7:
    code = '7';
    break;
  case SIM_KEY_8:
    code = '8';
    break;
  case SIM_KEY_9:
    code = '9';
    break;
  case SIM_KEY_A:
    code = 'a';
    break;
  case SIM_KEY_B:
    code = 'b';
    break;
  case SIM_KEY_C:
    code = 'c';
    break;
  case SIM_KEY_D:
    code = 'd';
    break;
  case SIM_KEY_E:
    code = 'e';
    break;
  case SIM_KEY_F:
    code = 'f';
    break;
  case SIM_KEY_G:
    code = 'g';
    break;
  case SIM_KEY_H:
    code = 'h';
    break;
  case SIM_KEY_I:
    code = 'i';
    break;
  case SIM_KEY_J:
    code = 'j';
    break;
  case SIM_KEY_K:
    code = 'k';
    break;
  case SIM_KEY_L:
    code = 'l';
    break;
  case SIM_KEY_M:
    code = 'm';
    break;
  case SIM_KEY_N:
    code = 'n';
    break;
  case SIM_KEY_O:
    code = 'o';
    break;
  case SIM_KEY_P:
    code = 'p';
    break;
  case SIM_KEY_Q:
    code = 'q';
    break;
  case SIM_KEY_R:
    code = 'r';
    break;
  case SIM_KEY_S:
    code = 's';
    break;
  case SIM_KEY_T:
    code = 't';
    break;
  case SIM_KEY_U:
    code = 'u';
    break;
  case SIM_KEY_V:
    code = 'v';
    break;
  case SIM_KEY_W:
    code = 'w';
    break;
  case SIM_KEY_X:
    code = 'x';
    break;
  case SIM_KEY_Y:
    code = 'y';
    break;
  case SIM_KEY_Z:
    code = 'z';
    break;
  case SIM_KEY_BACKQUOTE:
    code = '`';
    break;
  case SIM_KEY_MINUS:
    code = '-';
    break;
  case SIM_KEY_EQUALS:
    code = '=';
    break;
  case SIM_KEY_LEFT_BRACKET:
    code = '[';
    break;
  case SIM_KEY_RIGHT_BRACKET:
    code = ']';
    break;
  case SIM_KEY_SEMICOLON:
    code = ';';
    break;
  case SIM_KEY_SINGLE_QUOTE:
    code = '\'';
    break;
  case SIM_KEY_BACKSLASH:
  case SIM_KEY_LEFT_BACKSLASH:
    code = '/';
    break;
  case SIM_KEY_COMMA:
    code = ',';
    break;
  case SIM_KEY_PERIOD:
    code = '.';
    break;
  case SIM_KEY_SLASH:
    code = '/';
    break;
  }
  return code;
}

static int
key_event (SIM_KEY_EVENT *ev)
{
  sim_debug (DBG, &key_dev, "Key %s %s\n",
             ev->state == SIM_KEYPRESS_UP ? "up" : "down",
             vid_key_name (ev->key));

  if (key_modifiers (ev))
    return 0;
    
  if (ev->state == SIM_KEYPRESS_DOWN) {
    uint16 code;
    if (modifiers & SHFT)
      code = key_shift (ev->key);
    else
      code = key_noshift (ev->key);
    if (code == NOKEY)
      return 1;
    if (modifiers & CTRL)
      code &= 037;
    if (modifiers & META) {
      suffix = code;
      code = 033;
    }
    KBUF = code;
    sim_debug (DBG, &key_dev, "Received character %03o\n", KBUF);
    flag_on (FLAG_KB);
  } else if (ev->state == SIM_KEYPRESS_UP)
    KBUF = 0;

  return 0;
}

static t_stat
key_reset (DEVICE *dptr)
{
#ifdef USE_DISPLAY
  vid_display_kb_event_process = NULL;
#endif
  if (dptr->flags & DEV_DIS)
    return SCPE_OK;

  if (key_unit.flags & KEY_DISPLAY)
#ifdef USE_DISPLAY
    vid_display_kb_event_process = key_event;
#else
    ;
#endif
  else if (key_unit.flags & KEY_CONSOLE)
    sim_activate_abs (&key_unit, 0);
  else
    return SCPE_ARG;

  return SCPE_OK;
}

static uint16 key_read (uint16 reg)
{
  uint16 code = KBUF;
  sim_debug (DBG, &key_dev, "Read key %o\n", code);
  if (suffix == NOKEY) {
    flag_off (FLAG_KB);
    if (key_unit.flags & KEY_CONSOLE)
      sim_activate_abs (&key_unit, 0);
  } else {
    KBUF = suffix;
    suffix = NOKEY;
  }
  return code;
}

static void key_write (uint16 reg, uint16 data)
{
}
