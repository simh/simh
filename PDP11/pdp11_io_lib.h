/* pdp11_io_lib.h: Unibus/Qbus common support routines header file

   Copyright (c) 1993-2008, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.
*/

#ifndef PDP11_IO_LIB_H_
#define PDP11_IO_LIB_H_    0

t_stat set_autocon (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat show_autocon (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat set_addr (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat show_addr (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat show_mapped_addr (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat set_addr_flt (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat set_vec (UNIT *uptr, int32 arg, CONST char *cptr, void *desc);
t_stat show_vec (FILE *st, UNIT *uptr, int32 arg, CONST void *desc);
t_stat show_vec_mux (FILE *st, UNIT *uptr, int32 arg, CONST void *desc);
t_stat show_iospace (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat show_bus_map (FILE *st, const char *cptr, uint32 *busmap, uint32 nmapregs, const char *busname, uint32 mapvalid);
t_stat auto_config (const char *name, int32 nctrl);
t_stat pdp11_bad_block (UNIT *uptr, int32 sec, int32 wds);
void init_ubus_tab (void);
t_stat build_ubus_tab (DEVICE *dptr, DIB *dibp);

#endif
