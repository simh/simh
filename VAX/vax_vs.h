/* vax_vs.h: DEC Mouse/Tablet (VSXXX)

   Copyright (c) 2017, Matt Burke

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   vs           VSXXX-nn pointing device
*/

#ifndef _VAX_VS_H_
#define _VAX_VS_H_

#include "vax_defs.h"
#include "sim_video.h"

/* command definitions */

#define VS_INCR         0x52                            /* set mode incremental */
#define VS_PROMPT       0x44                            /* set mode prompt */
#define VS_POLL         0x50                            /* poll */
#define VS_TEST         0x54                            /* test */

/* Report bit definitions */

#define RPT_SYNC        0x80                            /* synchronise */
#define RPT_TABP        0x40                            /* tablet position */
#define RPT_TEST        0x20                            /* self test */
#define RPT_TAB         0x4                             /* tablet device */
#define RPT_MOU         0x2                             /* mouse device */
#define RPT_V_MFR       4                               /* manufacturer location ID */
#define RPT_REV         0xF                             /* revision number */
#define RPT_BC          0x7                             /* button code */
#define RPT_EC          0x7F                            /* error code */

t_stat vs_wr (uint8 c);
t_stat vs_rd (uint8 *c);
void vs_event (SIM_MOUSE_EVENT *ev);

#endif
