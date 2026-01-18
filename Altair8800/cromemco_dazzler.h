/* cromemco_dazzler.h

   Copyright (c) 2026 Patrick A. Linstruth

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
   01/18/26 Initial version

*/

#ifndef _CROMEMCO_DAZZLER_H
#define _CROMEMCO_DAZZLER_H


#define DAZ_PIXELS      (128 * 128)    /* total number of pixels */

#define DAZ_IO_BASE     0x0e
#define DAZ_IO_SIZE     2
#define DAZ_MEM_SIZE    2048
#define DAZ_MEM_MASK    (2048 - 1)

#define DAZ_ON          0x80   /* On/Off */
#define DAZ_RESX4       0x40   /* Resolution X 4 */
#define DAZ_2K          0x20   /* Picture in 2K bytes of memory */
#define DAZ_COLOR       0x10   /* Picture in 2K bytes of memory */
#define DAZ_HIGH        0x08   /* High intensity color */
#define DAZ_BLUE        0x04   /* Blue */
#define DAZ_GREEN       0x02   /* Green */
#define DAZ_RED         0x01   /* Red */
#define DAZ_EOF         0x40   /* End of Frame */
#define DAZ_EVEN        0x80   /* Even Line */

extern VID_DISPLAY *daz_vptr;

#endif

