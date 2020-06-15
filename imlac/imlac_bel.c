/* imlac_bel.c: MIT bell device.

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

/* Function declaration. */
static uint16 bel_iot (uint16, uint16);

static IMDEV bel_imdev = {
  1,
  { { 0071, bel_iot, { "BEL" } } }
};

static DEBTAB bel_deb[] = {
  { "DBG", DBG },
  { NULL, 0 }
};

DEVICE bel_dev = {
  "BEL", NULL, NULL, NULL,
  0, 8, 16, 1, 8, 16,
  NULL, NULL, NULL,
  NULL, NULL, NULL,
  &bel_imdev, DEV_DISABLE | DEV_DEBUG | DEV_DIS, 0, bel_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static uint16
bel_iot (uint16 insn, uint16 AC)
{
  sim_debug (DBG, &bel_dev, "IOT\n");
  if ((insn & 0771) == 0711) { /* BEL */
    sim_debug (DBG, &bel_dev, "Dong!\n");
#ifdef HAVE_LIBSDL
    vid_beep ();
#endif
  }
  return AC;
}
