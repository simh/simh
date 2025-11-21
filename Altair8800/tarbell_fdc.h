/* tarbell_fdc.h

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
   11/10/25 Initial version

*/

#ifndef _TARBELL_FDC_H
#define _TARBELL_FDC_H

#define TARBELL_NUM_DRIVES  4

#define TARBELL_IO_BASE     0xF8
#define TARBELL_IO_SIZE     6
#define TARBELL_IO_MASK     0x07

#define TARBELL_PROM_BASE   0x0000
#define TARBELL_PROM_SIZE   32
#define TARBELL_PROM_MASK   (TARBELL_PROM_SIZE - 1)

/* Tarbell Register Offsets */
#define TARBELL_REG_WAIT     0x04   /* Wait Port         */
#define TARBELL_REG_DRVSEL   0x04   /* Drive Select      */
#define TARBELL_REG_DMASTAT  0x05   /* DMA INTRQ Status  */
#define TARBELL_REG_EXTADDR  0x05   /* Extended Address  */

#define TARBELL_DENS_MASK          0x08
#define TARBELL_DSEL_MASK          0x30
#define TARBELL_SIDE_MASK          0x40

#define TARBELL_FLAG_DRQ           0x80  /* End of Job (DRQ) */

#define TARBELL_SD_CAPACITY        (77*26*128)                  /* SSSD 8" (IBM 3740) Disk Capacity */
#define TARBELL_DD_CAPACITY        ((26*128) + (76*51*128))     /* SSDD 8" Tarbell DD Disk Capacity */

#endif

