/* s100_bram.h

   Copyright (c) 2025 Patrick A. Linstruth

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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Patrick Linstruth shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Patrick Linstruth.

   History:
   11/07/25 Initial version

*/

#ifndef _S100_BRAM_H
#define _S100_BRAM_H

#include "sim_defs.h"

#define UNIT_BRAM_V_VERBOSE     (UNIT_V_UF+0)               /* Enable verbose messagesto */
#define UNIT_BRAM_VERBOSE       (1 << UNIT_BRAM_V_VERBOSE)

/* Supported Memory Boards */

#define BRAM_TYPE_NONE          0                 /* No type selected      */
#define BRAM_TYPE_ERAM          1                 /* SD Systems ExpandoRAM */
#define BRAM_TYPE_VRAM          2                 /* Vector Graphic RAM card */
#define BRAM_TYPE_CRAM          3                 /* Cromemco RAM card */
#define BRAM_TYPE_HRAM          4                 /* North Start Horizon RAM card */
#define BRAM_TYPE_B810          5                 /* AB Digital Design B810 RAM card */
#define BRAM_TYPE_MAX           BRAM_TYPE_B810    /* Maximum type          */

typedef struct {
    int32 baseport;                  /* Base IO address       */
    int32 size;                      /* Number of addresses   */
    int32 banks;                     /* Number of banks       */
    char *name;                      /* Short name            */
} BRAM;

#endif

