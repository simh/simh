#ifdef USE_DISPLAY
/* pdp11_daz.c: Control box

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
   THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.
*/

#include "pdp11_defs.h"
#include "sim_video.h"
#include "pdp11_dazzle_dart_rom.h"

#define TURN_RIGHT  0001
#define TURN_LEFT   0002
#define GO_RIGHT    0004
#define GO_LEFT     0010
#define GO_UP       0020
#define GO_DOWN     0040
#define PASS        0100
#define FIRE        0200

t_stat daz_rd(int32 *data, int32 PA, int32 access);
t_stat daz_wr(int32 data, int32 PA, int32 access);
t_stat daz_reset(DEVICE *dptr);
t_stat daz_boot(int32 unit, DEVICE *dptr);
t_stat daz_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *daz_description (DEVICE *dptr);

static uint16 devadd = 0;
static uint16 buttons[4] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };

#define IOLN_DAZ  4
DIB daz_dib = {
  IOBA_AUTO, IOLN_DAZ, &daz_rd, &daz_wr,
  4, 0, VEC_AUTO, {NULL}, IOLN_DAZ
};

UNIT daz_unit = {
  UDATA (NULL, 0, 0)
};

REG daz_reg[] = {
  { GRDATAD(DEVADD, devadd, 16, 16, 0, "Box selection"), REG_FIT},
  { NULL }
};

MTAB daz_mod[] = {
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 020, "ADDRESS", "ADDRESS",
    &set_addr, &show_addr, NULL, "Bus address" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR",  "VECTOR",
    &set_vec, &show_vec, NULL, "Interrupt vector" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "AUTOCONFIGURE",
    &set_addr_flt, NULL, NULL, "Enable autoconfiguration of address & vector" },
  { 0 }  };

DEVICE daz_dev = {
  "DAZ", &daz_unit, daz_reg, daz_mod,
  1, 8, 16, 1, 8, 16,
  NULL, NULL, &daz_reset,
  &daz_boot, NULL, NULL,
  &daz_dib, DEV_DIS | DEV_DISABLE | DEV_UBUS,
  0, NULL, NULL, NULL, NULL, &daz_help, NULL, 
  &daz_description
};

const char *daz_regnam[] = {
  "DEVADD",
  "DEVICE",
};

t_stat
daz_rd(int32 *data, int32 PA, int32 access)
{
  switch (PA & 002) {
  case 000:
    *data = 0x0000;
    break; 
  case 002:
    *data = 0x8000 | buttons[(devadd >> 10) & 3];
    break; 
  default:
    return SCPE_NXM;
  }
  return SCPE_OK;
}

t_stat
daz_wr(int32 data, int32 PA, int32 access)
{
  switch (PA & 002) {
  case 000:
    devadd = data;
    return SCPE_OK;
  case 002:
    return SCPE_OK;
  }
  return SCPE_NXM;
}

int daz_keyboard (SIM_KEY_EVENT *kev)
{
  uint32 mask;
  int n;

  switch (kev->key) {
  case SIM_KEY_1: n = 0; mask = GO_LEFT; break;
  case SIM_KEY_2: n = 0; mask = GO_RIGHT; break;
  case SIM_KEY_3: n = 0; mask = GO_UP; break;
  case SIM_KEY_4: n = 0; mask = GO_DOWN; break;
  case SIM_KEY_5: n = 0; mask = TURN_LEFT; break;
  case SIM_KEY_6: n = 0; mask = TURN_RIGHT; break;
  case SIM_KEY_7: n = 0; mask = FIRE; break;
  case SIM_KEY_8: n = 0; mask = PASS; break;
  case SIM_KEY_Q: n = 1; mask = GO_LEFT; break;
  case SIM_KEY_W: n = 1; mask = GO_RIGHT; break;
  case SIM_KEY_E: n = 1; mask = GO_UP; break;
  case SIM_KEY_R: n = 1; mask = GO_DOWN; break;
  case SIM_KEY_T: n = 1; mask = TURN_LEFT; break;
  case SIM_KEY_Y: n = 1; mask = TURN_RIGHT; break;
  case SIM_KEY_U: n = 1; mask = FIRE; break;
  case SIM_KEY_I: n = 1; mask = PASS; break;
  case SIM_KEY_A: n = 2; mask = GO_LEFT; break;
  case SIM_KEY_S: n = 2; mask = GO_RIGHT; break;
  case SIM_KEY_D: n = 2; mask = GO_UP; break;
  case SIM_KEY_F: n = 2; mask = GO_DOWN; break;
  case SIM_KEY_G: n = 2; mask = TURN_LEFT; break;
  case SIM_KEY_H: n = 2; mask = TURN_RIGHT; break;
  case SIM_KEY_J: n = 2; mask = FIRE; break;
  case SIM_KEY_K: n = 2; mask = PASS; break;
  case SIM_KEY_Z: n = 3; mask = GO_LEFT; break;
  case SIM_KEY_X: n = 3; mask = GO_RIGHT; break;
  case SIM_KEY_C: n = 3; mask = GO_UP; break;
  case SIM_KEY_V: n = 3; mask = GO_DOWN; break;
  case SIM_KEY_B: n = 3; mask = TURN_LEFT; break;
  case SIM_KEY_N: n = 3; mask = TURN_RIGHT; break;
  case SIM_KEY_M: n = 3; mask = FIRE; break;
  case SIM_KEY_COMMA: n = 3; mask = PASS; break;
  default: return 0;
  }

  if (kev->state == SIM_KEYPRESS_UP)
    buttons[n] |= mask;
  else if  (kev->state == SIM_KEYPRESS_DOWN)
    buttons[n] &= ~mask;

  return 0;
}

static void daz_joy_motion (int device, int axis, int value)
{
    if (device < 4 && axis < 4) {
        int mask = 0;
        switch (axis) {
            case 0:
                buttons[device] |= GO_LEFT | GO_RIGHT;
                if (value < -10000)
                    mask = GO_LEFT;
                else if (value > 1000)
                    mask = GO_RIGHT;
                break;
            case 1:
                buttons[device] |= GO_UP | GO_DOWN;
                if (value < -10000)
                    mask = GO_DOWN;
                else if (value > 1000)
                    mask = GO_UP;
                break;
            case 2: /* Some gamepads have these mixed up. */
            case 3:
                buttons[device] |= TURN_LEFT | TURN_RIGHT;
                if (value < -10000)
                    mask = TURN_LEFT;
                else if (value > 1000)
                    mask = TURN_RIGHT;
                break;
            }
        buttons[device] &= ~mask;
        }
}

static void daz_joy_button (int device, int button, int state)
{
    if (device < 4 && button < 2) {
        int mask = (button == 0 ? FIRE : PASS);
        if (state)
            buttons[device] &= ~mask;
        else
            buttons[device] |= mask;
        }
}

t_stat
daz_reset(DEVICE *dptr)
{
  DEVICE *ng_dptr;
  t_stat r;

  if (dptr->flags & DEV_DIS) {
    vid_display_kb_event_process = NULL;
    return auto_config ("DAZ", (dptr->flags & DEV_DIS) ? 0 : 1);;
    }

  ng_dptr = find_dev ("NG");
  if ((ng_dptr != NULL) && ((ng_dptr->flags & DEV_DIS) != 0)) {
    set_cmd (0, "CPU 11/45");    /* Need a Unibus machine. */
    r = set_cmd (0, "NG ENABLED");   /* Need NG display. */
    if (r != SCPE_OK) {
      dptr->flags |= DEV_DIS;
      return r;
      }
    set_cmd (0, "DZ DISABLED");  /* DZ is in conflict with NG. */
  }

  r = auto_config ("DAZ", (dptr->flags & DEV_DIS) ? 0 : 1);
  if (r != SCPE_OK)
    return r;

  vid_display_kb_event_process = &daz_keyboard;
  vid_register_gamepad_motion_callback (daz_joy_motion);
  vid_register_gamepad_button_callback (daz_joy_button);
  return SCPE_OK;
}

/* Apologies to Wolfgang Petersen. */
t_stat
daz_boot(int32 unit, DEVICE *dptr)
{
    t_stat r;

    set_cmd (0, "CPU 56K");
    set_cmd (0, "NG TYPE=DAZZLE");
    set_cmd (0, "PCLK ENABLED");
    set_cmd (0, "KE ENABLED");
    sim_set_memory_load_file (BOOT_CODE_ARRAY, BOOT_CODE_SIZE);
    r = load_cmd (0, BOOT_CODE_FILENAME);
    sim_set_memory_load_file (NULL, 0);
    cpu_set_boot (03252);
    return r;
}

const char *daz_description (DEVICE *dptr)
{
  return "Input buttons for Dazzle Dart";
}

t_stat daz_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
 /* The '*'s in the next line represent the standard text width of a help line */
              /****************************************************************************/
  fprintf(st, "%s\n\n", daz_description (dptr));
  fprintf(st, "The DAZ is a set of input buttons for the simulation of the MIT Logo\n");
  fprintf(st, "group PDP-11/45.  There are four sets of eight buttons.  The buttons are:\n");
  fprintf(st, "ROTATE LEFT, ROTATE RIGHT, MOVE LEFT, MOVE RIGHT, MOVE UP, MOVE DOWN,\n");
  fprintf(st, "PASS, and FIRE.\n\n");
  fprintf(st, "The first set is mapped from the keys 1-8.  The second set is mapped from\n");
  fprintf(st, "Q-I.  The first set is mapped from A-K.  The fourth set is mapped\n");
  fprintf(st, "from Z-, (comma).\n\n");

  fprintf(st, "The only software for the DAZ was the Dazzle Dart game by\n");
  fprintf(st, "Hal Abelson, Andy diSessa, and Nat Goodman.  To play the game:\n\n\n");
  fprintf(st, "   sim> set daz enable\n");
  fprintf(st, "   sim> boot daz\n\n");

  return SCPE_OK;
}
#else  /* USE_DISPLAY not defined */
char pdp11_daz_unused;   /* sometimes empty object modules cause problems */
#endif /* USE_DISPLAY not defined */
