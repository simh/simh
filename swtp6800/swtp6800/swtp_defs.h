/* swtp_defs.h: SWTP 6800 simulator definitions

Copyright (c) 2005-2012, William Beech

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
   WILLIAM A BEECH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of William A Beech shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from William A Beech.
*/

#include <ctype.h>
#include "sim_defs.h"                   // simulator defs

/* Rename of global PC variable to avoid namespace conflicts on some platforms */

#define PC PC_Global

/* Memory */

#define MAXMEMSIZE      65536               // max memory size
#define MEMSIZE         (m6800_unit.capac)  // actual memory size
#define ADDRMASK        (MAXMEMSIZE - 1)    // address mask
#define MEM_ADDR_OK(x)  (((uint32) (x)) < MEMSIZE)

/* debug definitions */

#define DEBUG_flow      0x0001
#define DEBUG_read      0x0002
#define DEBUG_write     0x0004
#define DEBUG_level1    0x0008
#define DEBUG_level2    0x0010
#define DEBUG_reg       0x0020
#define DEBUG_asm       0x0040
#define DEBUG_all       0xFFFF

/* Simulator stop codes */

#define STOP_RSRV   1       // must be 1
#define STOP_HALT   2       // HALT-really WAI
#define STOP_IBKPT  3       // breakpoint
#define STOP_OPCODE 4       // invalid opcode
#define STOP_MEMORY 5       // invalid memory address

