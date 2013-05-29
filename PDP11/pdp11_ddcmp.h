/* pdp11_ddcmp.h: Digital Data Communications Message Protocol support

   Copyright (c) 2013, Mark Pizzolato

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

   Digital Data Communications Message Protocol - DDCMP support routines 

   29-May-13    MP      Initial implementation
*/

#ifndef PDP11_DDCMP_H_
#define PDP11_DDCMP_H_    0

/* DDCMP packet types */

#define DDCMP_SOH 0201u   /* Numbered Data Message Identifier */
#define DDCMP_ENQ 0005u   /* Control Message Identifier */
#define DDCMP_DLE 0220u   /* Maintenance Message Identifier */

/* Support routines */

void ddcmp_packet_trace (uint32 reason, DEVICE *dptr, const char *txt, const uint8 *msg, int32 len, t_bool detail);
uint16 ddcmp_crc16(uint16 crc, const void* vbuf, size_t len);

#endif /* PDP11_DDCMP_H_ */
