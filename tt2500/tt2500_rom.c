/* tt2500_cpu.c: TT2500 bootstrap ROM contents.

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

uint16 tt2500_rom[] =
{
/* BEGIN   0 */ 0010000, /* (NOP) */
/*         1 */ 0010000, /* (NOP) */
/*         2 */ 0010000, /* (NOP) */
/* START   3 */ 0040025, /* (PUSHJ GETC & GET CHAR) */
/*         4 */ 0007001, /* (SUBI 0 WD & IS WORD "LOGO") */
/*         5 */ 0147577, /* (147577) */
/*         6 */ 0133774, /* (BNE START) */
/*         7 */ 0040022, /* (PUSHJ GETW) */
/*        10 */ 0074201, /* (GET ADR WD) */
/*        11 */ 0040022, /* (PUSHJ GETW) */
/*        12 */ 0074301, /* (GET CNT WD) */
/* LOOP   13 */ 0040022, /* (PUSHJ GETW) */
/*        14 */ 0004220, /* (DEC ADR) */
/*        15 */ 0025100, /* (CWRITE WD) */
/*        16 */ 0004320, /* (DEC CNT) */
/*        17 */ 0133773, /* (BNE LOOP) */
/*        20 */ 0074023, /* (GET 0 XR) */
/*        21 */ 0050100, /* (JUMP 100) */
/* GETW   22 */ 0040025, /* (PUSHJ GETC) */
/*        23 */ 0040025, /* (PUSHJ GETC) */
/*        24 */ 0040025, /* (PUSHJ GETC) */
/* GETC   25 */ 0073320, /* (DIS INTS 2) */
/*        26 */ 0050025, /* (JUMP GETC) */
/*        27 */ 0010000, /* (NOP) */
/*        30 */ 0074424, /* (GET CH UART) */
/*        31 */ 0001444, /* (ANDI CH CH) */
/*        32 */ 0000017, /* (17) */
/*        33 */ 0004114, /* (ROT WD 14) */
/*        34 */ 0001141, /* (ANDI WD WD) */
/*        35 */ 0177760, /* (177760) */
/*        36 */ 0002104, /* (IOR WD CH) */
/*        37 */ 0076016  /* (POPJ) */


#if 0
  0010000,	/* NOP */
  0010000,	/* NOP */
  0010000,	/* NOP */
  0040025,	/* PUSHJ GETC      Call subroutine to read TTY character. */
  0007001,	/* SUBI 0 WD       32-bit instruction computes 147577 - WD */
  0147577,	/* 147577           (The constant "LOGO" is stored here.) */
  0133774,	/* BNE START       Branch to START if result Not zero. */
  0040022,	/* PUSHJ GETW      Reads 4 characters to make 16-bit word. */
  0074201,	/* GET ADR WD      Move word from WD to ADR (register 2). */
  0040022,	/* PUSHJ GETW      Get next data word into WD. */
  0074301,	/* GET CNT WD      Move it to CNT. */
  0040022,	/* PUSHJ GETW      Get another data word. */
  0004220,	/* DEC ADR         Decrement the Address and use it to */
  0025100,	/* CWRITE WD        write [WD] into control memory. */
  0004320,	/* DEC CNT         Decrement the word in CNT (count). */
  0133773,	/* BNE LOOP        Branch to LOOP unless CNT is zero. */
  0074023,	/* GET 0 XR        Makes PC leave Bootstrap Loader. */
  0050100,	/* JUMP 100        Jump to location 100. */
  0040025,	/* PUSHJ GETC      Get 4 bits and shift into WD. */
  0040025,	/* PUSHJ GETC      Get 4 more. */
  0040025,	/* PUSHJ GETC      Get 4 more. */
  0073320,	/* DIS INTS 2      Skip 2 steps if TTY interrupt active. */
  0050025,	/* JUMP GETC       If not, go back and wait. */
  0010000,	/* NOP                (Skip over this.) */
  0074424,	/* GET CH UART     Put the character into CH. */
  0001444,	/* ANDI CH CH      Mask out all but last four bits by */
  0000017,	/* 17                ANDing with 0 000 000 000 001 111. */
  0004114,	/* ROT WD 14       Rotate WD four places right. */
  0001141,	/* ANDI WD WD      Zero out last four bits by */
  0177760,	/* 177760           ANDing with 1 111 111 111 110 000. */
  0002104,	/* IOR WD CH       Finally, OR them together. */
  0075600,	/* POPJ            Return to calling program. */
#endif
};
