/* sim_vt.c: VT2xx compatible terminal emulator

   Copyright (c) 2002, Robert M Supnik
   Written by Fischer Franz and used with his gracious permission

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   13-Apr-02	FF	Added Hold-Screen on Pause-Key
                	Corrected scrolling
			Added additional Esc-sequences Erase-Insert-Delete-Char 
			Corrected decoding 
			(ESC [ H, ESC [ x H, ESC [ ; y H , ESC [ x ; y H
			 are all valid sequences)
   15-Mar-02	FF	Original version Fischer Franz
*/

#include <windows.h>
#include "sim_defs.h"

#define HOLD   0x1
#define INSERT 0x2
#define SCRMAX 64

static HANDLE kbdHdl;
static HANDLE scrHdl;
static INPUT_RECORD inBuf;
static DWORD act, mode;
static CONSOLE_SCREEN_BUFFER_INFO screen;
static COORD margin;
static WORD attrib;

static char *kbd_ptr;
static char *scr_ptr;
static char scr_buf[SCRMAX];
static uint8 scr_mode;

struct keyEntry {
   uint8 asciiCode;
   uint8 scanCode;
   char *escSeq;
};
typedef struct keyEntry KEY;

typedef void (*scr_func) ();
struct scrEntry {
   char last;
   scr_func interpret;
};
typedef struct scrEntry SCR;

static KEY keyTab[] = {
    {0  ,0x3B,"[31~"},	/* F1, mapped to F17 on VT320 */
	{0  ,0x3C,"[32~"},	/* F2, mapped to F18 on VT320 */
	{0  ,0x3D,"[33~"},	/* F3, mapped to F19 on VT320 */
	{0  ,0x3E,"[34~"},	/* F4, mapped to F20 on VT320 */
	{0  ,0x3F,"[17~"},	/* F5, mapped to F6  on VT320 */
	{0  ,0x40,"[18~"},	/* F6, mapped to F7  on VT320 */
	{0  ,0x41,"[19~"},	/* F7, mapped to F8  on VT320 */
	{0  ,0x42,"[20~"},	/* F8, mapped to F9  on VT320 */
	{0  ,0x43,"[23~"},	/* F9, mapped to F11 on VT320 */
	{0  ,0x44,"[24~"},	/* F10,mapped to F12 on VT320 */
	{0  ,0x57,"[25~"},	/* F11,mapped to F13 on VT320 */
	{0  ,0x58,"[26~"},	/* F12,mapped to F14 on VT320 */

	{0xE0,0x52,"[2~"},  /* INS,mapped to INSERT on VT320 */
	{0xE0,0x53,"[3~"},  /* DEL,mapped to REMOVE in VT320 */
	{0xE0,0x47,"[1~"},  /* HOME, mapped to FIND on VT320 */
	{0xE0,0x4F,"[4~"},  /* END,mapped to SELECT on VT320 */
	{0xE0,0x49,"[5~"},  /* PAGE UP, mapped to PREV on VT320   */
	{0xE0,0x51,"[6~"},  /* PAGE DOWN, mapped to NEXT on VT320 */

	{0xE0,0x48,"[A"},/* UP */
	{0xE0,0x50,"[B"},/* DOWN */
	{0xE0,0x4D,"[C"},/* RIGHT */
	{0xE0,0x4B,"[D"},/* LEFT */

    {0xE0,0x45,"OP"},/* NUM, mapped to PF1 on VT320 */
	{0xE0,0x35,"OQ"},/* /  , mapped to PF2 on VT320 */
	{'*' ,0x37,"OR"},/* *  , mapped to PF3 on VT320 */
	{'-', 0x4A,"OS"},/* /  , mapped to PF4 on VT320 */

    /* The following Keys send Application Keypad-Mode sequences */
	{0  ,0x52,"Op"},	/* KP0 */
	{'0',0x52,"Op"},	/* KP0 */
	{0  ,0x4F,"Oq"},	/* KP1 */
	{'1',0x4F,"Oq"},	/* KP1 */
	{0  ,0x50,"Or"},	/* KP2 */
	{'2',0x50,"Or"},	/* KP2 */
	{0  ,0x51,"Os"},	/* KP3 */
	{'3',0x51,"Os"},	/* KP3 */
	{0  ,0x4B,"Ot"},	/* KP4 */
	{'4',0x4B,"Ot"},	/* KP4 */
	{0  ,0x4C,"Ou"},	/* KP5 */
	{'5',0x4C,"Ou"},	/* KP5 */
	{0  ,0x4D,"Ov"},	/* KP6 */
	{'6',0x4D,"Ov"},	/* KP6 */
	{0  ,0x47,"Ow"},	/* KP7 */
	{'7',0x47,"Ow"},	/* KP7 */
	{0  ,0x48,"Ox"},	/* KP8 */
	{'8',0x48,"Ox"},	/* KP8 */
	{0  ,0x49,"Oy"},	/* KP9 */
	{'9',0x49,"Oy"},    /* KP9 */
	{0  ,0x53,"On"},	/* PERIOD */
	{'.',0x53,"On"},    /* PERIOD */
	{0xE0,0x1C,"OM"},	/* ENTER */
	{'+',0x4E,"Ol"},	/* COMMA */
	{0}	                /* End of List */
};

int vt_read() {
   DWORD cnt = 0;
   uint8 sCode, aCode;
   KEY *key;
 
   if (kbd_ptr && *kbd_ptr) {
      return (*kbd_ptr++ & 0177);
   }

   GetNumberOfConsoleInputEvents(kbdHdl, &cnt);
   if (!cnt) return -1;

   ReadConsoleInput(kbdHdl, &inBuf, 1, &act);

   if (inBuf.EventType != KEY_EVENT) return -1;
   if (!inBuf.Event.KeyEvent.bKeyDown) return -1;
   if (inBuf.Event.KeyEvent.dwControlKeyState & ENHANCED_KEY) inBuf.Event.KeyEvent.uChar.AsciiChar = (uint8)0xE0;

   sCode = (uint8)inBuf.Event.KeyEvent.wVirtualScanCode;
   aCode = inBuf.Event.KeyEvent.uChar.AsciiChar;

   if (scr_mode & HOLD) {
      scr_mode &= ~HOLD;
      return 0x11;
   }
   if (sCode < 0x37 && aCode != 0 && aCode != 0xE0) return aCode;
   if (sCode == 0x45 && aCode == 0) {
      scr_mode |= HOLD;
      return 0x13;
   }
   
   for (key = keyTab; key->scanCode; key++) {
      if (key->asciiCode != aCode || key->scanCode != sCode) continue;
      kbd_ptr = key->escSeq;
      return 0x1b;
   }

   if (aCode != 0 && aCode != 0xE0) return aCode;
   return -1;
}

static void scr_nop () {};

static void scr_scroll_up(int from, int to, int lines) {
   SMALL_RECT src;
   COORD dst;
   CHAR_INFO fill;

   dst.X=0; dst.Y=from;
   src.Top=from+lines; src.Left=0; src.Bottom=to; src.Right=screen.dwSize.X;
   fill.Char.AsciiChar = ' ';
   fill.Attributes = attrib;
   ScrollConsoleScreenBuffer(scrHdl,&src,0,dst,&fill);
};
static void scr_scroll_down(int from, int to, int lines) {
   SMALL_RECT src;
   COORD dst;
   CHAR_INFO fill;

   dst.X=0; dst.Y=from+lines;
   src.Top=from; src.Left=0; src.Bottom=to-lines; src.Right=screen.dwSize.X;
   fill.Char.AsciiChar = ' ';
   fill.Attributes = attrib;
   ScrollConsoleScreenBuffer(scrHdl,&src,0,dst,&fill);
};
static void scr_insert_char() {
   int nr = atoi(&scr_buf[1]);
   SMALL_RECT src;
   COORD dst;
   CHAR_INFO fill;

   if (!nr) nr = 1;
   src.Top=screen.dwCursorPosition.Y; src.Left=screen.dwCursorPosition.X-nr; 
   src.Bottom=src.Top; src.Right=screen.dwSize.X;
   dst.X=src.Left+nr; dst.Y=src.Top;
   fill.Char.AsciiChar = ' ';
   fill.Attributes = attrib;
   ScrollConsoleScreenBuffer(scrHdl,&src,0,dst,&fill);
};
static void scr_pos_up() {
   int nr = atoi(&scr_buf[1]);
   if (!nr) nr = 1;
   if (screen.dwCursorPosition.Y > nr) screen.dwCursorPosition.Y -= nr;
   else                                screen.dwCursorPosition.Y = 0;
   SetConsoleCursorPosition(scrHdl,screen.dwCursorPosition);
};
static void scr_pos_down() {
   int nr = atoi(&scr_buf[1]);
   if (!nr) nr = 1;
   screen.dwCursorPosition.Y += nr;
   if (screen.dwCursorPosition.Y >= margin.Y) screen.dwCursorPosition.Y = margin.Y-1;
   SetConsoleCursorPosition(scrHdl,screen.dwCursorPosition);
};
static void scr_pos_right() {
   int nr = atoi(&scr_buf[1]);
   if (!nr) nr = 1;
   screen.dwCursorPosition.X += nr;
   if (screen.dwCursorPosition.X >= screen.dwSize.X) screen.dwCursorPosition.X = screen.dwSize.X-1;
   SetConsoleCursorPosition(scrHdl,screen.dwCursorPosition);
};
static void scr_pos_left() {
   int nr = atoi(&scr_buf[1]);
   if (!nr) nr = 1;
   if (screen.dwCursorPosition.X > nr) screen.dwCursorPosition.X -= nr;
   else                                screen.dwCursorPosition.X = 0;
   SetConsoleCursorPosition(scrHdl,screen.dwCursorPosition);
};
static void scr_pos_cursor() {
   int x, y;
   if (scr_buf[0] == 'H') return;
   x = 1; y = 1;
   if (!sscanf(scr_buf,"[%d;%d",&y,&x)) sscanf(scr_buf,"[;%d",&x);
   screen.dwCursorPosition.X = x-1;
   screen.dwCursorPosition.Y = y-1;
   SetConsoleCursorPosition(scrHdl,screen.dwCursorPosition);
};
static void scr_prev_line() {
   scr_scroll_down(margin.X,margin.Y,1);
};
static void scr_next_line() {
   scr_scroll_up(margin.X,margin.Y,1);
};
static void scr_erase_display() {
   int nr = atoi(&scr_buf[1]);
   COORD pos;
   int len;

   if (nr == 0) {
      pos = screen.dwCursorPosition;
      len = (screen.dwSize.Y-screen.dwCursorPosition.Y-1)*screen.dwSize.X +
            (screen.dwSize.X-screen.dwCursorPosition.X);
   } else if (nr == 1) {
      pos.X = 0; pos.Y = 0;
      len = (screen.dwCursorPosition.Y-1)*screen.dwSize.X + screen.dwCursorPosition.X;
   } else if (nr == 2) {
      pos.X = 0; pos.Y = 0;
      len = screen.dwSize.X*screen.dwSize.Y;
   } else {
      return;
   }
      
   FillConsoleOutputAttribute(scrHdl,screen.wAttributes,len,pos,&act);
   FillConsoleOutputCharacter(scrHdl,' ',len,pos,&act);
};
static void scr_erase_line() {
   int nr = atoi(&scr_buf[1]);
   COORD pos;
   int len;

   if (nr == 0) {
      pos = screen.dwCursorPosition;
      len = screen.dwSize.X-screen.dwCursorPosition.X;
   } else if (nr == 1) {
      pos.X = 0; pos.Y = screen.dwCursorPosition.Y;
      len = screen.dwCursorPosition.X;
   } else if (nr == 2) {
      pos.X = 0; pos.Y = screen.dwCursorPosition.Y;
      len = screen.dwSize.X;
   } else {
      return;
   }

   FillConsoleOutputAttribute(scrHdl,screen.wAttributes,len,pos,&act);
   FillConsoleOutputCharacter(scrHdl,' ',len,pos,&act);
};
static void scr_delete_line() {
   if (scr_buf[0] == 'M') {
      scr_prev_line();
   } else {
      int nr = atoi(&scr_buf[1]);
      if (!nr) nr = 1;
      scr_scroll_up(screen.dwCursorPosition.Y,margin.Y,nr);
   }
};
static void scr_insert_line() {
   int nr = atoi(&scr_buf[1]);
   if (!nr) nr = 1;
   scr_scroll_down(screen.dwCursorPosition.Y,margin.Y,nr);
};
static void scr_delete_char() {
   int nr = atoi(&scr_buf[1]);
   SMALL_RECT src;
   COORD dst;
   CHAR_INFO fill;

   if (!nr) nr = 1;
   dst = screen.dwCursorPosition;
   src.Top=dst.Y; src.Left=dst.X+nr; src.Bottom=dst.Y; src.Right=screen.dwSize.X;
   fill.Char.AsciiChar = ' ';
   fill.Attributes = attrib;
   ScrollConsoleScreenBuffer(scrHdl,&src,0,dst,&fill);
};
static void scr_erase_char() {
   int nr = atoi(&scr_buf[1]);
   COORD pos;

   if (!nr) nr = 1;
   pos = screen.dwCursorPosition;

   FillConsoleOutputAttribute(scrHdl,screen.wAttributes,nr,pos,&act);
   FillConsoleOutputCharacter(scrHdl,' ',nr,pos,&act);
};
static void scr_request() {
   kbd_ptr = "\033[?6c";
};
static void scr_set() {
   int nr = atoi(&scr_buf[1]);
   if (nr == 4) scr_mode |= INSERT;
};
static void scr_reset() {
   int nr = atoi(&scr_buf[1]);
   if (nr == 4) scr_mode &= ~INSERT;
};
static void scr_attrib() {
   int nr = atoi(&scr_buf[1]);
   switch (nr) {
      case 0: 
         attrib = screen.wAttributes;
         break;
      case 1: 
         attrib = screen.wAttributes | 0x80;
         break;
      case 7: 
         attrib = ((screen.wAttributes & 0xf) << 4) | ((screen.wAttributes & 0x7) >> 4);
         break;
   }
};
static void scr_report() {};
static void scr_margin() {
   int top, bot;
   top = 1; bot = screen.dwSize.Y+1;
   if (!sscanf(scr_buf,"[%d;%d",&top,&bot)) sscanf(scr_buf,"[;%d",&bot);
   margin.X = top-1;
   if (bot != 24) margin.Y = bot-1;
   else           margin.Y = screen.dwSize.Y;
   
   screen.dwCursorPosition.X = 0;
   screen.dwCursorPosition.Y = 0;
   SetConsoleCursorPosition(scrHdl,screen.dwCursorPosition);
};

static SCR scrTab[] = {
    {'@', scr_insert_char},
    {'A', scr_pos_up},
    {'B', scr_pos_down},
    {'C', scr_pos_right},
    {'D', scr_pos_left},
    {'E', scr_next_line},
    {'H', scr_pos_cursor},
    {'I', scr_prev_line},
    {'J', scr_erase_display},
    {'K', scr_erase_line},
    {'L', scr_insert_line},
    {'M', scr_delete_line},
    {'P', scr_delete_char},
    {'X', scr_erase_char},
    {'Z', scr_request},

    {'c', scr_request},/* Device attribute request */
    {'f', scr_pos_cursor},
    {'h', scr_set},    /* Set   mode */
    {'l', scr_reset},  /* Reset mode */
    {'m', scr_attrib}, /* Display attributes */
    {'n', scr_report}, /* Device report */
    {'r', scr_margin},
    {'=', scr_nop},    /* Set applikation keypad mode */
    {'>', scr_nop},    /* Set numeric keypad mode */
    {0}
};

static void scr_char(char c) {
   
   if (c == 0x8) {         /* BS */
      if (screen.dwCursorPosition.X > 0) screen.dwCursorPosition.X--;
   } else if (c == 0xa) {  /* LF */
      if (screen.dwCursorPosition.Y < margin.Y-1) screen.dwCursorPosition.Y++;
      else scr_scroll_up(margin.X,margin.Y,1);
   } else if (c == 0xd) {  /* CR */
      screen.dwCursorPosition.X = 0;
   } else if (c == 0x9) {  /* Tab */
      screen.dwCursorPosition.X = (screen.dwCursorPosition.X + 8) & ~7;
      if (screen.dwCursorPosition.X >= screen.dwSize.X) screen.dwCursorPosition.X = screen.dwSize.X-1;
   } else if (c < ' ') {
      return;
   } else if (!(scr_mode & INSERT)) {
      WriteConsoleOutputCharacter(scrHdl,&c,1,screen.dwCursorPosition,&act);
      WriteConsoleOutputAttribute(scrHdl,&attrib,1,screen.dwCursorPosition,&act);

      if (screen.dwCursorPosition.X < screen.dwSize.X) screen.dwCursorPosition.X++;
      else if (screen.dwCursorPosition.Y < margin.Y-1) {
         screen.dwCursorPosition.X = 0;
         screen.dwCursorPosition.Y++;
      } else {
         scr_scroll_up(margin.X,margin.Y,1);
         screen.dwCursorPosition.X = 0;
      }
   } else {
      SMALL_RECT src;
      COORD dst;
      CHAR_INFO fill;

      src.Top=screen.dwCursorPosition.Y; src.Left=screen.dwCursorPosition.X; 
      src.Bottom=src.Top; src.Right=screen.dwSize.X;
      dst.X=src.Left+1; dst.Y=src.Top;
      fill.Char.AsciiChar = ' ';
      fill.Attributes = attrib;
      ScrollConsoleScreenBuffer(scrHdl,&src,0,dst,&fill);
      WriteConsoleOutputCharacter(scrHdl,&c,1,screen.dwCursorPosition,&act);

      if (screen.dwCursorPosition.X < screen.dwSize.X) screen.dwCursorPosition.X++;
      else if (screen.dwCursorPosition.Y < margin.Y-1) {
         screen.dwCursorPosition.X = 0;
         screen.dwCursorPosition.Y++;
      } else {
         scr_scroll_up(margin.X,margin.Y,1);
         screen.dwCursorPosition.X = 0;
      }
   }

   SetConsoleCursorPosition(scrHdl,screen.dwCursorPosition);
}

static void check_esc (char c) {
   SCR *scr;

   *scr_ptr = c;
   if ((scr_ptr - scr_buf) < SCRMAX) scr_ptr++;
   for (scr = scrTab; scr->last; scr++) {
      if (scr->last != c) continue;

      *scr_ptr = 0;
      scr->interpret();
      scr_ptr = 0;
      return;
   }
}

void vt_write(char c) {

   if (c != 0x1b && !scr_ptr) {
      scr_char(c);
   } else if (c == 0x1b) {
      scr_ptr = scr_buf;
   } else if (c < ' ') {
      scr_ptr = 0;
      scr_char(c);
   } else if (scr_ptr == scr_buf) {
      check_esc(c);
   } else if (c >= '@') {
      check_esc(c);
      scr_ptr = 0;
   } else  {
      *scr_ptr = c;
      if ((scr_ptr - scr_buf) < SCRMAX) scr_ptr++;
   }
}

void vt_init() {
   kbdHdl = GetStdHandle(STD_INPUT_HANDLE);
   scrHdl = GetStdHandle(STD_OUTPUT_HANDLE);

   GetConsoleMode(kbdHdl, &mode);
   GetConsoleScreenBufferInfo(scrHdl, &screen);
   margin.X = 0; margin.Y = screen.dwSize.Y;
   attrib = screen.wAttributes;
   scr_mode = 0;
}

void vt_cmd() {
   SetConsoleMode(kbdHdl, mode); 
}

void vt_run() {
   kbd_ptr = 0;
   SetConsoleMode(kbdHdl, 0); 
   GetConsoleScreenBufferInfo(scrHdl, &screen);
   margin.X = 0; margin.Y = screen.dwSize.Y;
   attrib = screen.wAttributes;
   scr_mode = 0;
}