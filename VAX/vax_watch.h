/* vax_watch.h: VAX watch chip

   Copyright (c) 2019, Mark Pizzolato
   This module incorporates code from SimH:
        Copyright (c) 1998-2008, Robert M Supnik,
        Copyright (c) 2019, Matt Burke

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
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name(s) of the author(s) shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author(s).

   This file covers the watch chip (MC146818)

*/

#ifndef _VAX_WATCH_H_
#define _VAX_WATCH_H_ 1

#include "vax_defs.h"

extern t_stat wtc_set (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat wtc_show (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern void wtc_set_valid (void);
extern void wtc_set_invalid (void);
extern int32 wtc_rd (int32 rg);
extern int32 wtc_rd_pa (int32 pa);
extern void wtc_wr (int32 rg, int32 val);
extern void wtc_wr_pa (int32 pa, int32 val, int32 lnt);

#endif
