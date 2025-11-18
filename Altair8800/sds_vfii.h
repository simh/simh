/* sds_vfii.h

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
   11/16/25 Initial version

*/

#ifndef _VFII_FDC_H
#define _VFII_FDC_H

#define VFII_NUM_DRIVES  4

#define VFII_IO_BASE     0x63
#define VFII_IO_SIZE     5

#define VFII_PROM_BASE   0x0000
#define VFII_PROM_SIZE   32
#define VFII_PROM_MASK   (VFII_PROM_SIZE - 1)

/* VFII Register Offsets */
#define VFII_REG_STATUS   0x00   /* Status Port       */
#define VFII_REG_CONTROL  0x00   /* Control Port      */

#define VFII_DSEL_MASK          0x0f
#define VFII_SIDE_MASK          0x10
#define VFII_SIZE_MASK          0x20
#define VFII_DDEN_MASK          0x40
#define VFII_WAIT_MASK          0x80


#define VFII_FLAG_DRQ           0x80  /* End of Job (DRQ) */

#define VFII_SD_CAPACITY        (77*26*128)                  /* SSSD 8" (IBM 3740) Disk Capacity */
#define VFII_DD_CAPACITY        (77*26*256)                  /* SSDD 8" DD Disk Capacity         */

#endif

