/* pdp11_xu.c: DEUNA/DELUA Unibus Ethernet interface (stub)

   Copyright (c) 2003, Robert M Supnik

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

   xu		DEUNA/DELUNA Ethernet interface (stub)
*/

#if defined (VM_PDP10)					/* PDP10 version */
#include "pdp10_defs.h"
extern int32 int_req;
extern int32 int_vec[32];

#elif defined (VM_VAX)					/* VAX version */
#error "DEUNA/DELUA not supported on VAX!"

#else							/* PDP-11 version */
#include "pdp11_defs.h"
extern int32 int_req[IPL_HLVL];
extern int32 int_vec[IPL_HLVL][32];
#endif

/* XU data structures

   xu_dev	XU device descriptor
   xu_unit	XU unit list
   xu_reg	XU register list
*/

DIB xu_dib = { IOBA_XU, IOLN_XU, NULL, NULL,
		1, IVCL (XU), VEC_XU, { NULL } };

UNIT xu_unit = { UDATA (NULL, 0, 0) };

REG xu_reg[] = {
	{ NULL }  };

DEVICE xu_dev = {
	"XU", &xu_unit, xu_reg, NULL,
	1, 8, 8, 1, 8, 8,
	NULL, NULL, NULL,
	NULL, NULL, NULL,
	&xu_dib, DEV_DIS | DEV_UBUS };
