#include "linc_defs.h"
#include "sim_video.h"

/* Debug */
#define DBG   0001

static t_stat kbd_svc(UNIT *uptr);
static t_stat kbd_reset(DEVICE *dptr);

static UNIT kbd_unit = {
  UDATA(&kbd_svc, UNIT_IDLE, 0)
};

static DEBTAB kbd_deb[] = {
  { "DBG", DBG },
  { NULL, 0 }
};

DEVICE kbd_dev = {
  "KBD", &kbd_unit, NULL, NULL,
  1, 8, 12, 1, 8, 12,
  NULL, NULL, &kbd_reset,
  NULL, NULL, NULL, NULL, DEV_DEBUG, 0, kbd_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

/*
CASE  0  1  2  3  4  5  6  7  8  9 DEL
  23 00 01 02 03 04 05 06 07 10 11 13

   Q  W  E  R  T  Y  U  I  O  P i=
  44 52 30 45 47 54 50 34 42 43 15

    A  S  D  F  G  H  J  K  L +. -,
   24 46 27 31 32 33 35 36 37 10 17

 #[  Z  X  C  V  B  N  M pu |⊟ META/EOL
 22 55 53 26 51 25 41 40 16 21 12

             SPACE
              14
*/

static int kbd_pressed = 0;
static uint16 kbd_code;

void kbd_translate(int ch)
{
  switch (ch) {
  case '0':
    kbd_code = 000;
    break;
  case '1':
    kbd_code = 001;
    break;
  case '2':
    kbd_code = 002;
    break;
  case '3':
    kbd_code = 003;
    break;
  case '4':
    kbd_code = 004;
    break;
  case '5':
    kbd_code = 005;
    break;
  case '6':
    kbd_code = 006;
    break;
  case '7':
    kbd_code = 007;
    break;
  case '8':
    kbd_code = 010;
    break;
  case '9':
    kbd_code = 011;
    break;
  case '\r': case '\n':
    kbd_code = 012;
    break;
  case '\b': case 0177:
    kbd_code = 013;
    break;
  case ' ':
    kbd_code = 014;
    break;
  case '=': case 'i':
    kbd_code = 015;
    break;
  case 'p': case 'u':
    kbd_code = 016;
    break;
  case ',': case '-':
    kbd_code = 017;
    break;
  case '.': case '+':
    kbd_code = 020;
    break;
/*case '⊟':*/ case '|':
    kbd_code = 021;
    break;
  case '[': case '#':
    kbd_code = 022;
    break;
 /*case CASE:*/
    kbd_code = 023;
    break;
  case 'A': case 'a':
    kbd_code = 024;
    break;
  case 'B': case 'b':
    kbd_code = 025;
    break;
  case 'C': case 'c':
    kbd_code = 026;
    break;
  case 'D': case 'd':
    kbd_code = 027;
    break;
  case 'E': case 'e':
    kbd_code = 030;
    break;
  case 'F': case 'f':
    kbd_code = 031;
    break;
  case 'G': case 'g':
    kbd_code = 032;
    break;
  case 'H': case 'h':
    kbd_code = 033;
    break;
  case 'I': /*case 'i':*/
    kbd_code = 034;
    break;
  case 'J': case 'j':
    kbd_code = 035;
    break;
  case 'K': case 'k':
    kbd_code = 036;
    break;
  case 'L': case 'l':
    kbd_code = 037;
    break;
  case 'M': case 'm':
    kbd_code = 040;
    break;
  case 'N': case 'n':
    kbd_code = 041;
    break;
  case 'O': case 'o':
    kbd_code = 042;
    break;
  case 'P': /*case 'p':*/
    kbd_code = 043;
    break;
  case 'Q': case 'q':
    kbd_code = 044;
    break;
  case 'R': case 'r':
    kbd_code = 045;
    break;
  case 'S': case 's':
    kbd_code = 046;
    break;
  case 'T': case 't':
    kbd_code = 047;
    break;
  case 'U': /*case 'u':*/
    kbd_code = 050;
    break;
  case 'V': case 'v':
    kbd_code = 051;
    break;
  case 'W': case 'w':
    kbd_code = 052;
    break;
  case 'X': case 'x':
    kbd_code = 053;
    break;
  case 'Y': case 'y':
    kbd_code = 054;
    break;
  case 'Z': case 'z':
    kbd_code = 055;
    break;
  default:
    return;
  }
  sim_debug(DBG, &kbd_dev, "Key struck %c -> %02o\n", ch, kbd_code);
  kbd_pressed = 1;
}

static t_stat kbd_svc(UNIT *uptr)
{
  t_stat ch = sim_poll_kbd();

  if ((ch & SCPE_KFLAG) == 0) {
    sim_activate_after(&kbd_unit, 10000);
    return ch;
  }

  if (ch & SCPE_BREAK)
    ;
  else
    kbd_translate(ch & 0177);
  return SCPE_OK;
}

int kbd_struck(void)
{
  if (kbd_pressed)
    sim_debug(DBG, &kbd_dev, "KST\n");
  return kbd_pressed;
}

uint16 kbd_key(uint16 wait)
{
  if (kbd_pressed) {
    sim_debug(DBG, &kbd_dev, "KEY %02o\n", kbd_code);
    sim_activate_abs(&kbd_unit, 1);
    kbd_pressed = 0;
    return kbd_code;
  } else {
    sim_debug(DBG, &kbd_dev, "KEY paused\n");
  }
  return 0;
}

static void kbd_convert(uint32 key)
{
  switch (key) {
  case SIM_KEY_0: /* 0 Q */
  case SIM_KEY_BACKQUOTE:
    kbd_code = 000;
    break;
  case SIM_KEY_1: /* 1 R */
    kbd_code = 001;
    break;
  case SIM_KEY_2: /* 2 S */
    kbd_code = 002;
    break;
  case SIM_KEY_3: /* 3 T */
    kbd_code = 003;
    break;
  case SIM_KEY_4: /* 4 U */
    kbd_code = 004;
    break;
  case SIM_KEY_5: /* 5 V */
    kbd_code = 005;
    break;
  case SIM_KEY_6: /* 6 W */
    kbd_code = 006;
    break;
  case SIM_KEY_7: /* 7 X */
    kbd_code = 007;
    break;
  case SIM_KEY_8: /* 8 Y */
    kbd_code = 010;
    break;
  case SIM_KEY_9: /* 9 Z */
    kbd_code = 011;
    break;
  case SIM_KEY_ENTER:
    kbd_code = 012;
    break;
  case SIM_KEY_BACKSPACE:
  case SIM_KEY_DELETE:
    kbd_code = 013;
    break;
  case SIM_KEY_SPACE: /* Space ? */
  case SIM_KEY_SLASH:
    kbd_code = 014;
    break;
  case SIM_KEY_EQUALS: /* i = */
    kbd_code = 015;
    break;
  case SIM_KEY_F1: /* p u */
    kbd_code = 016;
    break;
  case SIM_KEY_MINUS: /* - , */
  case SIM_KEY_COMMA:
    kbd_code = 017;
    break;
  case SIM_KEY_PERIOD: /* + . */
    kbd_code = 020;
    break;
  case SIM_KEY_BACKSLASH: /* | ⊟ */
    kbd_code = 021;
    break;
  case SIM_KEY_LEFT_BRACKET: /* # [ */
  case SIM_KEY_LEFT_BACKSLASH:
    kbd_code = 022;
    break;
  case SIM_KEY_SHIFT_L: /* CASE _ */
  case SIM_KEY_SHIFT_R:
    kbd_code = 023;
    break;
  case SIM_KEY_A: /* A " */
  case SIM_KEY_SINGLE_QUOTE:
    kbd_code = 024;
    break;
  case SIM_KEY_B: /* B „ */
    kbd_code = 025;
    break;
  case SIM_KEY_C: /* C < */
    kbd_code = 026;
    break;
  case SIM_KEY_D: /* D > */
    kbd_code = 027;
    break;
  case SIM_KEY_E: /* E ] */
  case SIM_KEY_RIGHT_BRACKET:
    kbd_code = 030;
    break;
  case SIM_KEY_F: /* F ˣ */
    kbd_code = 031;
    break;
  case SIM_KEY_G: /* G : */
  case SIM_KEY_SEMICOLON:
    kbd_code = 032;
    break;
  case SIM_KEY_H: /* H */
    kbd_code = 033;
    break;
  case SIM_KEY_I: /* I */
    kbd_code = 034;
    break;
  case SIM_KEY_J: /* J */
    kbd_code = 035;
    break;
  case SIM_KEY_K: /* K */
    kbd_code = 036;
    break;
  case SIM_KEY_L: /* L */
    kbd_code = 037;
    break;
  case SIM_KEY_M: /* M */
    kbd_code = 040;
    break;
  case SIM_KEY_N: /* N */
    kbd_code = 041;
    break;
  case SIM_KEY_O: /* O */
    kbd_code = 042;
    break;
  case SIM_KEY_P: /* P */
    kbd_code = 043;
    break;
  case SIM_KEY_Q: /* Q */
    kbd_code = 044;
    break;
  case SIM_KEY_R: /* R */
    kbd_code = 045;
    break;
  case SIM_KEY_S: /* S */
    kbd_code = 046;
    break;
  case SIM_KEY_T: /* T */
    kbd_code = 047;
    break;
  case SIM_KEY_U: /* U */
    kbd_code = 050;
    break;
  case SIM_KEY_V: /* V */
    kbd_code = 051;
    break;
  case SIM_KEY_W: /* W */
    kbd_code = 052;
    break;
  case SIM_KEY_X: /* X */
    kbd_code = 053;
    break;
  case SIM_KEY_Y: /* Y */
    kbd_code = 054;
    break;
  case SIM_KEY_Z: /* Z */
    kbd_code = 055;
    break;
  case SIM_KEY_ALT_L: /* META? */
  case SIM_KEY_ALT_R:
    kbd_code = 056;
    break;
    // → 57
    // ? 60
    // = 61
    // u 62
    // , 63
    // . 64
    // ⊟ 65
    // [ 66
    // _ 67
    // " 70
    // „ 71
    // < 72
    // > 73
    // ] 74
    // ˣ 75
    // : 76
    // ʸ 77
  default:
    return;
  }
  sim_debug(DBG, &kbd_dev, "Key struck %s -> %02o\n",
            vid_key_name(key), kbd_code);
  kbd_pressed = 1;
}

static int
kbd_event(SIM_KEY_EVENT *ev)
{
  if (ev->state == SIM_KEYPRESS_DOWN)
    kbd_convert(ev->key);
  return 0;
}

static t_stat
kbd_reset(DEVICE *dptr)
{
#ifdef USE_DISPLAY
  vid_display_kb_event_process = kbd_event;
#endif
  sim_activate_abs(&kbd_unit, 0);

  return SCPE_OK;
}
