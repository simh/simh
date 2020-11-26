/* imlac_kbd.c: Imlac keyboard device

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

#include "imlac_defs.h"
#include "sim_video.h"

/* Debug */
#define DBG             0001

#define KBD_DISPLAY  1
#define KBD_CONSOLE  2

#define SHFT  00400
#define CTRL  01000
#define REPT  02000
#define META  00000
#define TOP   00000

static uint16 KBUF;
static uint16 modifiers;
static int kbd_type = KBD_DISPLAY;

/* Function declaration. */
static t_stat kbd_svc (UNIT *uptr);
static t_stat kbd_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat kbd_show_type (FILE *st, UNIT *up, int32 v, CONST void *dp);
static t_stat kbd_reset (DEVICE *dptr);
static uint16 kbd_iot (uint16, uint16);

static UNIT kbd_unit = {
  UDATA (&kbd_svc, UNIT_IDLE, 0)
};

static REG kbd_reg[] = {
  { ORDATAD (KBUF, KBUF, 16, "Keyboard buffer") },
  { NULL }
};

MTAB kbd_mod[] = {
  { MTAB_VDV|MTAB_VALR, 1, "TYPE", "TYPE", &kbd_set_type,
    &kbd_show_type, NULL, "Set keyboard input type" },
  { 0 }
};

static IMDEV kbd_imdev = {
  1,
  { { 0002, kbd_iot, { NULL, "KRB", "KCF", "KRC" } } }
};

static DEBTAB kbd_deb[] = {
  { "DBG", DBG },
  { NULL, 0 }
};

DEVICE kbd_dev = {
  "KBD", &kbd_unit, kbd_reg, kbd_mod,
  0, 8, 16, 1, 8, 16,
  NULL, NULL, &kbd_reset,
  NULL, NULL, NULL, &kbd_imdev,
  DEV_DISABLE | DEV_DEBUG, 0, kbd_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static int32 kbd_translate (int32 ch)
{
  static int32 table[] = {
    01240, 01301, 00202, 01303, 00204, 00205, 00206, 01307, /* ^@ - ^G */
    00210, 00211, 00212, 01313, 00214, 00215, 00216, 00217,
    01320, 01321, 01322, 01323, 01324, 01325, 01326, 01327,
    00230, 01331, 01332, 00233, 00234, 00235, 00236, 01337,
    00240, 00241, 00242, 00243, 00244, 00245, 00246, 00247, /* SPC - ' */
    00250, 00251, 00252, 00253, 00254, 00255, 00256, 00257,
    00260, 00261, 00262, 00263, 00264, 00265, 00266, 00267,
    00270, 00271, 00272, 00273, 00274, 00275, 00276, 00277,
    00300, 00301, 00302, 00303, 00304, 00305, 00306, 00307, /* @ - G */
    00310, 00311, 00312, 00313, 00314, 00315, 00316, 00317, /* H - O */
    00320, 00321, 00322, 00323, 00324, 00325, 00326, 00327, /* P - W */
    00330, 00331, 00332, 00333, 00334, 00335, 00336, 00337, /* X - _ */
    00340, 00341, 00342, 00343, 00344, 00345, 00346, 00347, /* ` - g */
    00350, 00351, 00352, 00353, 00354, 00355, 00356, 00357,
    00360, 00361, 00362, 00363, 00364, 00365, 00366, 00367,
    00370, 00371, 00372, 00373, 00374, 00375, 00376, 00377
  };
  return table[ch];
}

static t_stat kbd_svc (UNIT *uptr)
{
  t_stat ch = sim_poll_kbd ();

  if ((ch & SCPE_KFLAG) == 0) {
    sim_activate_after (&kbd_unit, 10000);
    return ch;
  }

  if (ch & SCPE_BREAK)
    KBUF = 0231;
  else
    KBUF = kbd_translate (ch & 0177);
  flag_on (FLAG_KBD);
  sim_debug (DBG, &kbd_dev, "Received character %03o\n", KBUF);
  return SCPE_OK;
}

static int
kbd_modifiers (SIM_KEY_EVENT *ev)
{
  uint16 code = 0;

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
  case SIM_KEY_WIN_L:
  case SIM_KEY_WIN_R:
    code = TOP;
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
kbd_both (uint32 key)
{
  uint16 code;
  switch (key) {
  case SIM_KEY_END:
    code = 0002; // XMIT
    break;
  case SIM_KEY_DOWN:
    code = 0004;
    break;
  case SIM_KEY_RIGHT:
    code = 0005;
    break;
  case SIM_KEY_UP:
    code = 0006;
    break;
  case SIM_KEY_LEFT:
    code = 0010;
    break;
  case SIM_KEY_TAB:
    code = 0011;
    break;
#if 0
  case SIM_KEY_:
    code = 0012; // LF
    break;
#endif
  case SIM_KEY_PAGE_UP:
    code = 0014; // FORM
    break;
  case SIM_KEY_ENTER:
    code = 0015;
    break;
  case SIM_KEY_PAGE_DOWN:
    code = 0016; // PAGE XMIT
    break;
  case SIM_KEY_HOME:
    code = 0017;
    break;
  case SIM_KEY_KP_INSERT:
    code = 0030; // KP_0
    break;
  case SIM_KEY_PAUSE:
    code = 0031; // BRK
    break;
  case SIM_KEY_KP_DOWN:
    code = 0032; // KP_2
    break;
  case SIM_KEY_ESC:
    code = 0033;
    break;
  case SIM_KEY_KP_LEFT:
    code = 0034; // KP_4
    break;
  case SIM_KEY_KP_5:
    code = 0035; // KP_5
    break;
  case SIM_KEY_KP_RIGHT:
    code = 0036; // KP_6
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
    return 0;
  default:
    return 0;
  }
  return code | modifiers;
}

static int
kbd_shift (uint32 key)
{
  uint16 code;

  code = kbd_both (key);
  if (code != 0)
    return code;

  switch (key) {
  case SIM_KEY_0:
    code = ')';
    break;
  case SIM_KEY_1:
    code = '!';
    break;
  case SIM_KEY_2:
    return CTRL + ';';
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
    return CTRL + ':';
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
    return CTRL + '6';
  case SIM_KEY_MINUS:
    return CTRL + '-';
  case SIM_KEY_EQUALS:
    code = '+';
    break;
  case SIM_KEY_LEFT_BRACKET:
    return CTRL + '8';
  case SIM_KEY_RIGHT_BRACKET:
    return CTRL + '9';
  case SIM_KEY_SEMICOLON:
    code = ':';
    break;
  case SIM_KEY_SINGLE_QUOTE:
    code = '"';
    break;
  case SIM_KEY_BACKSLASH:
  case SIM_KEY_LEFT_BACKSLASH:
    return CTRL + '0';
  case SIM_KEY_COMMA:
    code = '<';
    break;
  case SIM_KEY_PERIOD:
    code = '>';
    break;
  case SIM_KEY_SLASH:
    code = '?';
    break;
  default:
    return 0;
  }
  return code | modifiers;
}

static int
kbd_noshift (uint32 key)
{
  uint16 code;

  code = kbd_both (key);
  if (code != 0)
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
    code = CTRL + '7';
    break;
  case SIM_KEY_MINUS:
    code = '-';
    break;
  case SIM_KEY_EQUALS:
    code = SHFT + '=';
    break;
  case SIM_KEY_LEFT_BRACKET:
    code = CTRL + ',';
    break;
  case SIM_KEY_RIGHT_BRACKET:
    code = CTRL + '.';
    break;
  case SIM_KEY_SEMICOLON:
    code = ';';
    break;
  case SIM_KEY_SINGLE_QUOTE:
    code = SHFT + '\'';
    break;
  case SIM_KEY_BACKSLASH:
  case SIM_KEY_LEFT_BACKSLASH:
    code = CTRL + '/';
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
  default:
    return 0;
  }
  return code | modifiers;
}

static int
kbd_event (SIM_KEY_EVENT *ev)
{
  sim_debug (DBG, &kbd_dev, "Key %s %s\n",
             ev->state == SIM_KEYPRESS_UP ? "up" : "down",
             vid_key_name (ev->key));

  if (kbd_modifiers (ev))
    return 0;
    
  if (ev->state == SIM_KEYPRESS_DOWN) {
    uint16 code;
    if (modifiers & SHFT)
      code = kbd_shift (ev->key);
    else
      code = kbd_noshift (ev->key);
    if (code != 0) {
      KBUF = code | 0200;
      sim_debug (DBG, &kbd_dev, "Received character %03o\n", KBUF);
      flag_on (FLAG_KBD);
    }
  } else if (ev->state == SIM_KEYPRESS_UP)
    KBUF = 0;

  return 0;
}

static t_stat
kbd_reset (DEVICE *dptr)
{
#ifdef USE_DISPLAY
  vid_display_kb_event_process = NULL;
#endif
  if (dptr->flags & DEV_DIS)
    return SCPE_OK;

  if (kbd_type == KBD_DISPLAY)
#ifdef USE_DISPLAY
    vid_display_kb_event_process = kbd_event;
#else
    ;
#endif
  else if (kbd_type == KBD_CONSOLE)
    sim_activate_abs (&kbd_unit, 0);
  else
    return SCPE_ARG;

  return SCPE_OK;
}

static uint16
kbd_iot (uint16 insn, uint16 AC)
{
  if ((insn & 0771) == 0021) { /* KRC */
    sim_debug (DBG, &kbd_dev, "Read character %03o\n", KBUF);
    AC |= KBUF;
    if (kbd_type == KBD_CONSOLE)
      KBUF = 0;
  }
  if ((insn & 0772) == 0022) { /* KCF */
    sim_debug (DBG, &kbd_dev, "Clear flag\n");
    flag_off (FLAG_KBD);
    if (kbd_type == KBD_CONSOLE)
      sim_activate_after (&kbd_unit, 10000);
  }
  return AC;
}

static t_stat
kbd_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (strcmp (cptr, "DISPLAY") == 0)
    kbd_type = KBD_DISPLAY;
  else if (strcmp (cptr, "CONSOLE") == 0)
    kbd_type = KBD_CONSOLE;
  else
    return SCPE_ARG;
  return kbd_reset (&kbd_dev);
}

static t_stat
kbd_show_type (FILE *st, UNIT *up, int32 v, CONST void *dp)
{
  switch (kbd_type) {
  case KBD_DISPLAY:
    fprintf (st, "TYPE=DISPLAY");
    break;
  case KBD_CONSOLE:
    fprintf (st, "TYPE=CONSOLE");
    break;
  default:
    fprintf (st, "TYPE=(invalid)");
    break;
  }
  return SCPE_OK;
}
