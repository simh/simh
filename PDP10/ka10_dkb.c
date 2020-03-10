/* ka10_dkb.c:Stanford Microswitch scanner.

   Copyright (c) 2019-2020, Richard Cornwell

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

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell

*/

#include "kx10_defs.h"
#ifndef NUM_DEVS_DKB
#define NUM_DEVS_DKB 0
#endif

#if NUM_DEVS_DKB > 0 

#include "sim_video.h"

#define DKB_DEVNUM        0310

#define DONE              010     /* Device has character */
#define SPW               020     /* Scanner in SPW mode */

#define VALID             010000
#define SPW_FLG           020000
#define CHAR              001777
#define SHFT              000100
#define TOP               000200
#define META              000400
#define CTRL              001000

#define STATUS            u3
#define DATA              u4
#define PIA               u5
#define LINE              u6

t_stat dkb_devio(uint32 dev, uint64 *data);
int dkb_keyboard (SIM_KEY_EVENT *kev);
t_stat dkb_svc(UNIT *uptr);
t_stat dkb_reset(DEVICE *dptr);
t_stat dkb_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *dkb_description (DEVICE *dptr);


int dkb_kmod = 0;

DIB dkb_dib = { DKB_DEVNUM, 1, dkb_devio, NULL};

UNIT dkb_unit[] = {
    {UDATA (&dkb_svc, UNIT_IDLE, 0) },
    { 0 }
    };


MTAB dkb_mod[] = {
    { 0 }
    };

DEVICE dkb_dev = {
    "DKB", dkb_unit, NULL, dkb_mod,
    2, 10, 31, 1, 8, 8,
    NULL, NULL, dkb_reset,
    NULL, NULL, NULL, &dkb_dib, DEV_DEBUG | DEV_DISABLE | DEV_DIS, 0, dev_debug,
    NULL, NULL, &dkb_help, NULL, NULL, &dkb_description
    };

t_stat dkb_devio(uint32 dev, uint64 *data) {
     UNIT    *uptr = &dkb_unit[0];
     switch(dev & 3) {
     case CONI:
        *data = (uint64)(uptr->STATUS|uptr->PIA);
        sim_debug(DEBUG_CONI, &dkb_dev, "DKB %03o CONI %06o\n", dev, (uint32)*data);
        break;
     case CONO:
         uptr->PIA = (int)(*data&7);
         if (*data & DONE)
            uptr->STATUS = 0;
         clr_interrupt(DKB_DEVNUM);
         sim_debug(DEBUG_CONO, &dkb_dev, "DKB %03o CONO %06o\n", dev, (uint32)*data);
         break;
     case DATAI:
         *data = (uint64)((uptr->LINE << 18) | (uptr->DATA));
         uptr->STATUS = 0;
         clr_interrupt(DKB_DEVNUM);
         sim_debug(DEBUG_DATAIO, &dkb_dev, "DKB %03o DATAI %06o\n", dev, (uint32)*data);
         break;
    case DATAO:
         if (*data & 010000) {
             uptr->STATUS |= SPW;
             uptr->LINE = (int)(*data & 077);
         }
         sim_debug(DEBUG_DATAIO, &dkb_dev, "DKB %03o DATAO %06o\n", dev, (uint32)*data);
         break;
    }
    return SCPE_OK;
}

int dkb_modifiers (SIM_KEY_EVENT *kev)
{
    if (kev->state == SIM_KEYPRESS_UP) {
        switch (kev->key) {
        case SIM_KEY_SHIFT_L:
        case SIM_KEY_SHIFT_R:
        case SIM_KEY_CAPS_LOCK:
            dkb_kmod |= SHFT;
            return 1;
        case SIM_KEY_CTRL_L:
        case SIM_KEY_CTRL_R:
            dkb_kmod |= CTRL;
            return 1;
        case SIM_KEY_WIN_L:
        case SIM_KEY_WIN_R:
            dkb_kmod |= META;
            return 1;
        case SIM_KEY_ALT_L:
        case SIM_KEY_ALT_R:
            dkb_kmod |= TOP;
            return 1;
        }
        return 0;
    }
    if (kev->state == SIM_KEYPRESS_DOWN) {
        switch (kev->key) {
        case SIM_KEY_SHIFT_L:
        case SIM_KEY_SHIFT_R:
        case SIM_KEY_CAPS_LOCK:
            dkb_kmod &= ~SHFT;
            return 1;
        case SIM_KEY_CTRL_L:
        case SIM_KEY_CTRL_R:
            dkb_kmod &= ~CTRL;
            return 1;
        case SIM_KEY_WIN_L:
        case SIM_KEY_WIN_R:
            dkb_kmod &= ~META;
            return 1;
        case SIM_KEY_ALT_L:
        case SIM_KEY_ALT_R:
            dkb_kmod &= ~TOP;
            return 1;
        }
        return 0;
     }
     return 0;
}

int dkb_keys (SIM_KEY_EVENT *kev, UNIT *uptr)
{
  if (kev->state == SIM_KEYPRESS_UP)
    return 0;

  switch (kev->key) {
  case SIM_KEY_0:  /* ok */
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = dkb_kmod | 051;  /* ) */
    else
        uptr->DATA = dkb_kmod | 060;
    return 1;
  case SIM_KEY_1:
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = (dkb_kmod | 054) & ~TOP;   /* ! */
    else
        uptr->DATA = dkb_kmod | 061;
    return 1;
  case SIM_KEY_2:
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = (dkb_kmod | 052) & ~TOP;  /* Circle Star */
    else
        uptr->DATA = dkb_kmod | 062;
    return 1;
  case SIM_KEY_3:
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = (dkb_kmod | 022) & ~TOP;  /* # */
    else
        uptr->DATA = dkb_kmod | 063;
    return 1;
  case SIM_KEY_4:
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = (dkb_kmod | 066) & ~TOP;  /* $ */
    else
        uptr->DATA = dkb_kmod | 064;
    return 1;
  case SIM_KEY_5:
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = (dkb_kmod | 067) & ~TOP;  /* % */
    else
        uptr->DATA = dkb_kmod | 065;
    return 1;
  case SIM_KEY_6:
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = (dkb_kmod | 073) & ~TOP;  /* ^ */
    else
        uptr->DATA = dkb_kmod | 066;
    return 1;
  case SIM_KEY_7:
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = (dkb_kmod | 024) & ~TOP;  /* & */
    else
        uptr->DATA = dkb_kmod | 067;
    return 1;
  case SIM_KEY_8:  /* ok */
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = (dkb_kmod | 052) & ~TOP;  /* * */
    else
        uptr->DATA = dkb_kmod | 070;
    return 1;
  case SIM_KEY_9:  /* ok */
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = dkb_kmod | 050;   /* ( */
    else
        uptr->DATA = dkb_kmod | 071;
    return 1;
  case SIM_KEY_A:
    uptr->DATA = dkb_kmod | 001;
    return 1;
  case SIM_KEY_B:
    uptr->DATA = dkb_kmod | 002;
    return 1;
  case SIM_KEY_C:
    if (dkb_kmod == (META|TOP|SHFT))   /* Control C */
        uptr->DATA = dkb_kmod | 043;
    else 
        uptr->DATA = dkb_kmod | 003;
    return 1;
  case SIM_KEY_D:
    uptr->DATA = dkb_kmod | 004;
    return 1;
  case SIM_KEY_E:
    uptr->DATA = dkb_kmod | 005;
    return 1;
  case SIM_KEY_F:
    uptr->DATA = dkb_kmod | 006;
    return 1;
  case SIM_KEY_G:
    uptr->DATA = dkb_kmod | 007;
    return 1;
  case SIM_KEY_H:
    uptr->DATA = dkb_kmod | 010;
    return 1;
  case SIM_KEY_I:
    uptr->DATA = dkb_kmod | 011;
    return 1;
  case SIM_KEY_J:
    uptr->DATA = dkb_kmod | 012;
    return 1;
  case SIM_KEY_K:
    uptr->DATA = dkb_kmod | 013;
    return 1;
  case SIM_KEY_L:
    uptr->DATA = dkb_kmod | 014;
    return 1;
  case SIM_KEY_M:
    uptr->DATA = dkb_kmod | 015;
    return 1;
  case SIM_KEY_N:
    uptr->DATA = dkb_kmod | 016;
    return 1;
  case SIM_KEY_O:
    uptr->DATA = dkb_kmod | 017;
    return 1;
  case SIM_KEY_P:
    uptr->DATA = dkb_kmod | 020;
    return 1;
  case SIM_KEY_Q:
    uptr->DATA = dkb_kmod | 021;
    return 1;
  case SIM_KEY_R:
    uptr->DATA = dkb_kmod | 022;
    return 1;
  case SIM_KEY_S:
    uptr->DATA = dkb_kmod | 023;
    return 1;
  case SIM_KEY_T:
    uptr->DATA = dkb_kmod | 024;
    return 1;
  case SIM_KEY_U:
    uptr->DATA = dkb_kmod | 025;
    return 1;
  case SIM_KEY_V:
    uptr->DATA = dkb_kmod | 026;
    return 1;
  case SIM_KEY_W:
    uptr->DATA = dkb_kmod | 027;
    return 1;
  case SIM_KEY_X:
    uptr->DATA = dkb_kmod | 030;
    return 1;
  case SIM_KEY_Y:
    uptr->DATA = dkb_kmod | 031;
    return 1;
  case SIM_KEY_Z:
    uptr->DATA = dkb_kmod | 032;
    return 1;
  case SIM_KEY_BACKQUOTE:                 /* ` ~ */
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = dkb_kmod | 043;
    else
        uptr->DATA = dkb_kmod | 00;
    return 1;
  case SIM_KEY_MINUS:                     /* - not */
    uptr->DATA = dkb_kmod | 055;
    return 1;
  case SIM_KEY_EQUALS:                    /* = + */
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
	uptr->DATA = dkb_kmod | 053;
    else
        uptr->DATA = (dkb_kmod | 010) & ~TOP;
    return 1;
  case SIM_KEY_LEFT_BRACKET:              /* [ { */
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = (dkb_kmod | 017) & ~TOP;
    else
        uptr->DATA = (dkb_kmod | 050) & ~TOP;;
    return 1;
  case SIM_KEY_RIGHT_BRACKET:             /* ] } */
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = (dkb_kmod | 020) & ~TOP;
    else
        uptr->DATA = (dkb_kmod | 051) & ~TOP;;
    return 1;
  case SIM_KEY_SEMICOLON:                 /* ; : */
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = dkb_kmod | 072 | TOP;
    else
        uptr->DATA = dkb_kmod | 073 | TOP;
    return 1;
  case SIM_KEY_SINGLE_QUOTE:  /* ok */    /* ' " */
    if ((dkb_kmod & (TOP|SHFT)) == TOP)
        uptr->DATA = (dkb_kmod | 031) & ~TOP;
    else
        uptr->DATA = (dkb_kmod | 011) & ~TOP;
    return 1;
  case SIM_KEY_BACKSLASH:  /* Ok */
    if ((dkb_kmod & (TOP|SHFT)) == TOP)    /* \ | */
        uptr->DATA = (dkb_kmod | 053) & ~TOP;
    else if ((dkb_kmod & (TOP|SHFT)) == SHFT) 
        uptr->DATA = (dkb_kmod | 034) & ~TOP;
    else
        uptr->DATA = dkb_kmod | 034 | TOP;
    return 1;
  case SIM_KEY_LEFT_BACKSLASH:
    uptr->DATA = dkb_kmod | 034;
    return 1;
  case SIM_KEY_COMMA: /* ok */
    if ((dkb_kmod & (TOP|SHFT)) == TOP)    /* , < */
        uptr->DATA = (dkb_kmod | 04) & ~TOP;
    else
        uptr->DATA = dkb_kmod | 054 | TOP;
    return 1;
  case SIM_KEY_PERIOD: /* Ok */
    if ((dkb_kmod & (TOP|SHFT)) == TOP)    /* . > */
        uptr->DATA = (dkb_kmod | 06) & ~TOP;
    else
        uptr->DATA = dkb_kmod | 056;
    return 1;
  case SIM_KEY_SLASH: /* Ok */
    if ((dkb_kmod & (TOP|SHFT)) == TOP)    /* / ? */
        uptr->DATA = (dkb_kmod | 056) & ~TOP;
    else 
        uptr->DATA = dkb_kmod | 057;
    return 1;
  case SIM_KEY_ESC:
    uptr->DATA = dkb_kmod | 042;
    return 1;
  case SIM_KEY_BACKSPACE:
    uptr->DATA = dkb_kmod | 074;
    return 1;
  case SIM_KEY_DELETE:
    uptr->DATA = dkb_kmod | 044;
    return 1;
  case SIM_KEY_TAB:
    uptr->DATA = dkb_kmod | 045;
    return 1;
  case SIM_KEY_ENTER:
    uptr->DATA = dkb_kmod | 033;
    return 1;
  case SIM_KEY_SPACE:
    uptr->DATA = dkb_kmod | 040;
    return 1;
  default:
    return 0;
  }
}

int dkb_keyboard (SIM_KEY_EVENT *kev)
{
    sim_debug(DEBUG_DETAIL, &dkb_dev, "DKB key %d %o\n", kev->key, kev->state);
    if (dkb_modifiers (kev))
      return 0;

    if (dkb_keys (kev, &dkb_unit[0])) {
      dkb_unit[0].DATA |= VALID;
      dkb_unit[0].STATUS |= DONE;
      set_interrupt(DKB_DEVNUM, dkb_unit[0].PIA);
      return 0;
    }

    return 1;
}


t_stat dkb_svc( UNIT *uptr)
{
    return SCPE_OK;
}

t_stat dkb_reset( DEVICE *dptr)
{
    if ((dkb_dev.flags & DEV_DIS) == 0)
        vid_display_kb_event_process = dkb_keyboard;
    dkb_kmod = SHFT|TOP|META|CTRL;
    return SCPE_OK;
}
    

t_stat dkb_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (stderr, "This is the keyboard input for the Stanford III display\n");
    return SCPE_OK;
}

const char *dkb_description (DEVICE *dptr)
{
    return "Keyboard scanner for III display devices";
}
#endif
