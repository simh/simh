#ifdef USE_DISPLAY
/* pdp11_tv.c: TV, Logo TV raster display

   Copyright (c) 2022, Lars Brinkhoff

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

t_stat tv_rd(int32 *data, int32 PA, int32 access);
t_stat tv_wr(int32 data, int32 PA, int32 access);
t_stat tv_svc(UNIT *uptr);
t_stat tv_reset(DEVICE *dptr);
const char *tv_description (DEVICE *dptr);

#define TV_WINDOWS   2
#define TV_WIDTH     576                    /* Display width. */
#define TV_HEIGHT    454                    /* Display height. */
#define TV_PIXELS    (TV_WIDTH * TV_HEIGHT) /* Total number of pixels. */
#define TV_KEYS      16                     /* Max keys in buffer. */

static uint32 tv_surface[TV_WINDOWS][TV_PIXELS];
static uint32 tv_palette[TV_WINDOWS][2];
static VID_DISPLAY *tv_vptr[TV_WINDOWS];
static char tv_updated[TV_WINDOWS];

static int    tv_keys = 0;
static uint16 tv_key[TV_KEYS];  /* Buffer for LKBB. */
static uint16 COLORD;
static uint16 VIDSW;     /* Video switch register. */
static uint16 COLORA;
static uint8  tv_source[256];
static uint8  tv_display[256];
static uint16 TVINCR;
static uint16 TVSEL;     /* Frame buffer select. */
static uint16 TVRADR;
static uint16 tvdata;
static uint16 TVWC;
static uint16 TVDADR;
static uint16 TVSHR;
static uint16 TVMSK;
static uint16 TVDWIN;
static uint16 TVRWIN;
static uint16 TVCNSL[64];

#define FB (16*1024)
static uint16 RAM[64*FB];

#define BASE ((TVSEL & 077) * FB)

/* VIDSW bits. */
// Low byte is the console number.
// High byte is the video source.

/* TVINCR bits. */
#define TVINC     0077  /* Mask for the increment. */

/* TVSEL bits. */
#define TVRCNS    0077  /* The console number mask. */
#define TVNSH     0000  /* No shift write mode. */
#define TVIOR     0100  /* The inclusive or mode. */
#define TVXOR     0200  /* The xor mode. */
#define TVMOV     0300  /* The move function. */

/* TVSHR bits. */
#define TVSHCN    0017  /* The shift count. */

/* TVCNSL bits. */
#define SCROLL  007777  /* Scroll pointer. */
#define REVSCR  010000  /* Reverse video. */

#define IOLN_TV   064
DIB tv_dib = {
  IOBA_AUTO, IOLN_TV, &tv_rd, &tv_wr,
  0, 0, 0, {NULL}, IOLN_TV
};

UNIT tv_unit = {
  UDATA (&tv_svc, UNIT_IDLE, 0), 0
};

REG tv_reg[] = {
  { ORDATAD(LKBB,   tv_key[0], 16, "Lebel keyboard interface") },
  { ORDATAD(COLORA, COLORA,    16, "Color map address") },
  { ORDATAD(VIDSW,  VIDSW,     16, "Video switch") },
  { ORDATAD(COLORD, COLORD,    16, "Color map data") },
  { ORDATAD(TVINCR, TVINCR,    16, "Increment") },
  { ORDATAD(TVSEL,  TVSEL,     16, "Console select") },
  { ORDATAD(TVRADR, TVRADR,    16, "Regular transfer address") },
  { ORDATAD(TVWC,   TVWC,      16, "Word count") },
  { ORDATAD(TVDADR, TVDADR,    16, "Disk transfer address") },
  { ORDATAD(TVSHR,  TVSHR,     16, "Shift register") },
  { ORDATAD(TVMSK,  TVMSK,     16, "Mask") },
  { ORDATAD(TVDWIN, TVDWIN,    16, "Disk data window") },
  { ORDATAD(TVRWIN, TVRWIN,    16, "Regular data window") },
  { ORDATAD(TVCNSL, TVCNSL[0], 16, "Console status") },
  { NULL }
};

MTAB tv_mod[] = {
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 020, "ADDRESS", "ADDRESS",
    &set_addr, &show_addr, NULL, "Bus address" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR,   0, "VECTOR",  "VECTOR",
    &set_vec,  &show_vec, NULL, "Interrupt vector" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "AUTOCONFIGURE",
    &set_addr_flt, NULL, NULL, "Enable autoconfiguration of address & vector" },
  { 0 }  };

#define DBG_IO        0001
#define DBG_VID       0002
#define DBG_KEY       0004

DEBTAB tv_deb[] = {
  { "IO",  DBG_IO,  "IO page" },
  { "VID", DBG_VID, "video" },
  { "KEY", DBG_KEY, "keyboard" },
  { NULL, 0 }
};

DEVICE tv_dev = {
    "TV", &tv_unit, tv_reg, tv_mod,
    1, 8, 16, 1, 8, 16,
    NULL, NULL, &tv_reset,
    NULL, NULL, NULL,
    &tv_dib, DEV_DIS | DEV_DISABLE | DEV_UBUS | DEV_DEBUG,
    0, tv_deb, NULL, NULL, NULL, NULL, NULL, 
    &tv_description
};

static void
render_word (uint8 buffer, uint16 address)
{
  uint8 display = tv_display[buffer];
  uint16 data;
  int i;

  if (display >= TV_WINDOWS)
    return;

  buffer &= 077;
  address /= 2;
  address += (TVCNSL[buffer] & SCROLL) << 2;
  address &= FB-1;
  data = RAM[FB * buffer + address];

  for (i = 0; i < 16; i++) {
    int pixel = (data & (0100000 >> i)) ? 0 : 1;
    if (TVCNSL[buffer] & REVSCR)
      pixel ^= 1;
    tv_surface[display][address * 16 + i] =
      tv_palette[display][pixel];
  }
}

static void
render_display (uint16 display)
{
  uint8 buffer = tv_source[display];
  int i;
  if (display >= TV_WINDOWS)
    return;
  sim_debug (DBG_VID, &tv_dev, "Render display %d buffer %d\n", display, buffer);
  for (i = 0; i < (TV_PIXELS / 8); i += 2)
    render_word (buffer, i);
  tv_updated[display] = 1;
}

static void
tv_alu (void)
{
  uint16 data = RAM[BASE + (TVRADR/2)];
  uint16 data2 = tvdata;
  int sh = (TVSHR & TVSHCN);

  if (TVSEL & TVMOV)
    data2 = (data2 << sh) | (data2 >> (16 - sh));
  data2 &= ~TVMSK;
  switch (TVSEL & TVMOV) {
  case TVIOR: data |= data2; break;
  case TVXOR: data ^= data2; break;
  case TVNSH:
  case TVMOV: data = (data & TVMSK) | data2; break;
  }
  RAM[BASE + (TVRADR/2)] = data;
  render_word (TVSEL & 077, TVRADR);
  tv_updated[tv_display[TVSEL & 077]] = 1;
  TVRADR += 2 * (TVINCR & TVINC);
}

static void
tv_loop (void)
{
  while (TVWC != 0) {
    tv_alu ();
    TVWC++;
  }
}

t_stat
tv_rd(int32 *data, int32 PA, int32 access)
{
  t_stat stat = SCPE_OK;
  *data = 0;
  switch (PA & 077) {
  case 000:
    if (tv_keys > 0)
      *data = tv_key[--tv_keys];
    else
      *data = 0;
    sim_debug (DBG_IO, &tv_dev, "READ LKBB %06o\n", *data);
    break;
  case 002:
    *data = COLORD;
    sim_debug (DBG_IO, &tv_dev, "READ COLORD %06o\n", *data);
    break;
  case 004:
    *data = VIDSW;
    sim_debug (DBG_IO, &tv_dev, "READ VIDSW %06o\n", *data);
    break;
  case 006:
    *data = COLORA;
    sim_debug (DBG_IO, &tv_dev, "READ COLORA %06o\n", *data);
    break;
  case 040:
    *data = TVINCR;
    sim_debug (DBG_IO, &tv_dev, "READ TVINCR %06o\n", *data);
    break;
  case 042:
    *data = TVSEL;
    sim_debug (DBG_IO, &tv_dev, "READ TVSEL %06o\n", *data);
    break;
  case 044:
    *data = TVRADR;
    sim_debug (DBG_IO, &tv_dev, "READ TVRADR %06o\n", *data);
    break;
  case 046:
    *data = TVWC;
    sim_debug (DBG_IO, &tv_dev, "READ TVWC %06o\n", *data);
    break;
  case 050:
    *data = TVDADR;
    sim_debug (DBG_IO, &tv_dev, "READ TVDADR %06o\n", *data);
    break;
  case 052:
    *data = TVSHR;
    sim_debug (DBG_IO, &tv_dev, "READ TVSHR %06o\n", *data);
    break;
  case 053:
    *data = TVSHR >> 8;
    sim_debug (DBG_IO, &tv_dev, "READ TVSHR+1 %06o\n", *data);
    break;
  case 054:
    *data = TVMSK;
    sim_debug (DBG_IO, &tv_dev, "READ TVMSK %06o\n", *data);
    break;
  case 056:
    *data = TVDWIN;
    sim_debug (DBG_IO, &tv_dev, "READ TVDWIN %06o\n", *data);
    break;
  case 060:
    *data = RAM[BASE + (TVRADR/2)];
    sim_debug (DBG_IO, &tv_dev, "READ TVRWIN[%06o] %06o\n",
               TVRADR, *data);
    break;
  case 062:
    *data = TVCNSL[TVSEL & 077];
    sim_debug (DBG_IO, &tv_dev, "READ TVCNSL %06o\n", *data);
    break;
  default:
    sim_debug (DBG_IO, &tv_dev, "READ %06o\n", PA);
    break;
  }
  return stat;
}

t_stat
tv_wr(int32 data, int32 PA, int32 access)
{
  t_stat stat = SCPE_OK;
  uint16 x;
  const char *a = "WRITE";
  if (access == WRITEB)
    a = "WRITEB";
  switch (PA & 077) {
  case 000:
    sim_debug (DBG_IO, &tv_dev, "%s LKBB %06o\n", a, data);
    break;
  case 002:
    sim_debug (DBG_IO, &tv_dev, "%s COLORD %06o\n", a, data);
    COLORD = data;
    break;
  case 004:
    sim_debug (DBG_IO, &tv_dev, "%s VIDSW %06o\n", a, data);
    tv_source[data >> 8] = data & 0377;
    tv_display[data & 0377] = data >> 8;
    render_display (data >> 8);
    break;
  case 006:
    sim_debug (DBG_IO, &tv_dev, "%s COLORA %06o\n", a, data);
    COLORA = data;
    break;
  case 040:
    sim_debug (DBG_IO, &tv_dev, "%s TVINCR %06o\n", a, data);
    TVINCR = data;
    break;
  case 042:
    sim_debug (DBG_IO, &tv_dev, "%s TVSEL %06o\n", a, data);
    if (access == WRITEB) {
      data &= 0377;
      data |= TVSEL & 0177400;
    }
    TVSEL = data;
    break;
  case 044:
    sim_debug (DBG_IO, &tv_dev, "%s TVRADR %06o\n", a, data);
    TVRADR = data;
    break;
  case 046:
    sim_debug (DBG_IO, &tv_dev, "%s TVWC %06o\n", a, data);
    TVWC = data;
    tv_loop ();
    break;
  case 050:
    sim_debug (DBG_IO, &tv_dev, "%s TVDADR %06o\n", a, data);
    TVDADR = data;
    break;
  case 052:
    sim_debug (DBG_IO, &tv_dev, "%s TVSHR %06o\n", a, data);
    TVSHR = data;
    break;
  case 053:
    sim_debug (DBG_IO, &tv_dev, "%s TVSHR+1 %06o\n", a, data);
    TVSHR &= 0377;
    TVSHR |= (data & 0377) << 8;
    break;
  case 054:
    sim_debug (DBG_IO, &tv_dev, "%s TVMSK %06o\n", a, data);
    TVMSK = data;
    break;
  case 056:
    sim_debug (DBG_IO, &tv_dev, "%s TVDWIN %06o\n", a, data);
    TVDWIN = data;
    break;
  case 060:
    sim_debug (DBG_IO, &tv_dev, "%s TVRWIN[%06o] %06o\n",
               a, TVRADR, data);
    tvdata = data;
    tv_alu ();
    break;
  case 062:
    sim_debug (DBG_IO, &tv_dev, "%s TVCNSL %06o\n", a, data);
    x = TVCNSL[TVSEL & 077];
    TVCNSL[TVSEL & 077] = data;
    if (x ^ data)
      render_display (tv_display[TVSEL & 077]);
    break;
  default:
    sim_debug (DBG_IO, &tv_dev, "%s %06o %06o\n", a, PA, data);
    break;
  }
  return stat;
}

static void
toggle_fullscreen (VID_DISPLAY *vptr)
{
  vid_set_fullscreen_window (vptr, !vid_is_fullscreen_window (vptr));
}

#define KEY(NORMAL, SHIFTED) (shifted ? (SHIFTED) : (NORMAL))
#define CTL(NORMAL, SHIFTED, CTLED) \
  (control ? (CTLED) : KEY(NORMAL, SHIFTED))

static uint16
translate_key (SIM_KEY_EVENT *ev)
{
  static int shifted = 0;
  static int control = 0;

  if (ev->state == SIM_KEYPRESS_UP) {
    switch (ev->key) {
    case SIM_KEY_F11:
      toggle_fullscreen (ev->vptr);
      break;
    case SIM_KEY_SHIFT_L:
    case SIM_KEY_SHIFT_R:
      shifted = 0;
      break;
    case SIM_KEY_CTRL_L:
    case SIM_KEY_CTRL_R:
    case SIM_KEY_CAPS_LOCK:
      control = 0;
      break;
    }
    return 0;
  }

  switch (ev->key) {
  case SIM_KEY_SHIFT_L:
  case SIM_KEY_SHIFT_R:
    shifted = 1;
    return 0;
  case SIM_KEY_CTRL_L:
  case SIM_KEY_CTRL_R:
  case SIM_KEY_CAPS_LOCK:
    control = 1;
    return 0;
  case SIM_KEY_0:
    return KEY (0060, 0004);
  case SIM_KEY_1:
    return KEY (0061, 0041);
  case SIM_KEY_2:
    return KEY (0062, 0052);
  case SIM_KEY_3:
    return KEY (0063, 0043);
  case SIM_KEY_4:
    return KEY (0064, 0044);
  case SIM_KEY_5:
    return KEY (0065, 0045);
  case SIM_KEY_6:
    return CTL (0066, 0137, 0037);
  case SIM_KEY_7:
    return KEY (0067, 0136);
  case SIM_KEY_8:
    return KEY (0070, 0030);
  case SIM_KEY_9:
    return KEY (0071, 0003);
  case SIM_KEY_A:
    return CTL (0141, 0101, 0000);
  case SIM_KEY_B:
    return CTL (0142, 0102, 0000);
  case SIM_KEY_C:
    return CTL (0143, 0103, 0000);
  case SIM_KEY_D:
    return CTL (0144, 0104, 0002);
  case SIM_KEY_E:
    return CTL (0145, 0105, 0000);
  case SIM_KEY_F:
    return CTL (0146, 0106, 0014);
  case SIM_KEY_G:
    return CTL (0147, 0107, 0034);
  case SIM_KEY_H:
    return CTL (0150, 0110, 0000);
  case SIM_KEY_I:
    return CTL (0151, 0111, 0012);
  case SIM_KEY_J:
    return CTL (0152, 0112, 0007);
  case SIM_KEY_K:
    return CTL (0153, 0113, 0000);
  case SIM_KEY_L:
    return CTL (0154, 0114, 0013);
  case SIM_KEY_M:
    return CTL (0155, 0115, 0015);
  case SIM_KEY_N:
    return CTL (0156, 0116, 0000);
  case SIM_KEY_O:
    return CTL (0157, 0117, 0000);
  case SIM_KEY_P:
    return CTL (0160, 0120, 0000);
  case SIM_KEY_Q:
    return CTL (0161, 0121, 0000);
  case SIM_KEY_R:
    return CTL (0162, 0122, 0000);
  case SIM_KEY_S:
    return CTL (0163, 0123, 0000);
  case SIM_KEY_T:
    return CTL (0164, 0124, 0000);
  case SIM_KEY_U:
    return CTL (0165, 0125, 0177);
  case SIM_KEY_V:
    return CTL (0166, 0126, 0000);
  case SIM_KEY_W:
    return CTL (0167, 0127, 0000);
  case SIM_KEY_X:
    return CTL (0170, 0130, 0000);
  case SIM_KEY_Y:
    return CTL (0171, 0131, 0000);
  case SIM_KEY_Z:
    return CTL (0172, 0132, 0000);
  case SIM_KEY_BACKQUOTE:
    return KEY (0050, 0051);
  case SIM_KEY_MINUS:
    return CTL (0020, 0033, 005);
  case SIM_KEY_EQUALS:
    return KEY (0055, 0001);
  case SIM_KEY_LEFT_BRACKET:
    return CTL (0133, 0000, 0011);
  case SIM_KEY_RIGHT_BRACKET:
    return CTL (0134, 0000, 0036);
  case SIM_KEY_SEMICOLON:
    return KEY (0042, 0047);
  case SIM_KEY_SINGLE_QUOTE:
    return KEY (0046, 0073);
  case SIM_KEY_BACKSLASH:
  case SIM_KEY_LEFT_BACKSLASH:
    return CTL (0032, 0176, 0035);
  case SIM_KEY_COMMA:
    return KEY (0054, 0075);
  case SIM_KEY_PERIOD:
    return KEY (0056, 0140);
  case SIM_KEY_SLASH:
    return KEY (0027, 0057);
  case SIM_KEY_ESC:
  case SIM_KEY_F1:
    return 011;
  case SIM_KEY_BACKSPACE:
  case SIM_KEY_DELETE:
    return 031;
  case SIM_KEY_TAB:
    return 012;
  case SIM_KEY_ENTER:
    return 015;
  case SIM_KEY_SPACE:
    return 040;
  default:
    return 0;
  }
}

static uint16
keyboard_number (VID_DISPLAY *vptr, uint16 code)
{
  int i;
  for (i = 0; i < TV_WINDOWS; i++) {
    if (vptr == tv_vptr[i])
      return code | (i << 8);
  }
  return 0;
}

t_stat tv_svc(UNIT *uptr)
{
  SIM_KEY_EVENT ev;
  uint16 key;
  int i;

  for (i = 0; i < TV_WINDOWS; i++) {
    if (!tv_updated[i])
      continue;
    vid_draw_window (tv_vptr[i], 0, 0, TV_WIDTH, TV_HEIGHT, tv_surface[i]);
    vid_refresh_window (tv_vptr[i]);
    tv_updated[i] = 0;
    sim_debug (DBG_VID, &tv_dev, "Display %d refreshed.\n", i);
  }

  while (vid_poll_kb (&ev) == SCPE_OK) {
    key = translate_key (&ev);
    if (key != 0)
      key = keyboard_number (ev.vptr, key);
    sim_debug (DBG_KEY, &tv_dev, "Keyboard %06o.\n", key);
    if (tv_keys < TV_KEYS)
      tv_key[tv_keys++] = key;
  }

  sim_activate_after (&tv_unit, 10000);

  return SCPE_OK;
}

t_stat tv_reset(DEVICE *dptr)
{
  t_stat r;
  int i;

  if (dptr->flags & DEV_DIS || sim_switches & SWMASK('P')) {
    for (i = 0; i < TV_WINDOWS; i++) {
      if (tv_vptr[i] != NULL)
        vid_close_window (tv_vptr[i]);
    }
    memset (tv_vptr, 0, sizeof tv_vptr);
    memset (tv_palette, 0, sizeof tv_palette);
    memset (tv_updated, 0, sizeof tv_updated);
    sim_cancel (&tv_unit);
    return SCPE_OK;
  }

  for (i = 0; i < TV_WINDOWS; i++) {
    if (tv_vptr[i] == NULL) {
      char title[40];
      snprintf (title, sizeof title, "Display %d", i);
      r = vid_open_window (&tv_vptr[i], &tv_dev, title, TV_WIDTH, TV_HEIGHT, 0);
      if (r != SCPE_OK)
        return r;
      tv_palette[i][0] = vid_map_rgb_window (tv_vptr[i], 0x00, 0x00, 0x00);
      tv_palette[i][1] = vid_map_rgb_window (tv_vptr[i], 0x00, 0xFF, 0x30);
      memset (TVCNSL, 0, sizeof TVCNSL);
      memset (tv_source, 0, sizeof tv_source);
      memset (tv_display, 0, sizeof tv_display);
      render_display (i);
    }
  }

  tv_keys = 0;

  sim_activate (&tv_unit, 1);
  return SCPE_OK;
}

const char *tv_description (DEVICE *dptr)
{
  return "Raster display controller for MIT Logo PDP-11/45";
}

#else  /* USE_DISPLAY not defined */
char pdp11_tv_unused;   /* sometimes empty object modules cause problems */
#endif /* USE_DISPLAY not defined */
