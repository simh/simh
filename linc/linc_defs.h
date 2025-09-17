/* linc_defs.h: LINC simulator definitions

   Copyright (c) 2025, Lars Brinkhoff

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

   17-Sept-25    LB      New simulator.
*/

#ifndef LINC_DEFS_H_
#define LINC_DEFS_H_  0

#include "sim_defs.h"

#define STOP_HALT       1
#define STOP_IBKPT      2
#define STOP_RBKPT      3
#define STOP_WBKPT      3

#define MEMSIZE  2048

extern REG cpu_reg[];
extern uint16 M[];

extern DEVICE cpu_dev;
extern DEVICE crt_dev;
extern DEVICE dpy_dev;
extern DEVICE kbd_dev;
extern DEVICE tape_dev;
extern DEVICE tty_dev;

extern t_bool build_dev_tab(void);
extern void dpy_dis(uint16 h, uint16 x, uint16 y);
extern void crt_point (uint16 x, uint16 y);
extern uint16 kbd_key(uint16 wait);
extern int kbd_struck(void);
extern void tape_op(void);

#endif /* LINC_DEFS_H_ */
