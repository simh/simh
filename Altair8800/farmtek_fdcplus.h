/* farmtek_fdcplus.h

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
   03/29/26 Initial version

*/

#ifndef _FARMTEK_FDCPLUS_H
#define _FARMTEK_FDCPLUS_H

#define UNIT_V_DSK_WLK      (UNIT_V_UF + 0)         /* write locked         */
#define UNIT_DSK_WLK        (1 << UNIT_V_DSK_WLK)

#define NUM_OF_DSK          4                       /* NUM_OF_DSK must be power of two              */
#define NUM_OF_DSK_MASK     (NUM_OF_DSK - 1)

#define DSK_ALTAIR_8IN      1
#define DSK_ALTAIR_MINIDISK 2
#define FDCP_TYPE_5      5

#define DSK_SECTSIZE        137                     /* size of sector                               */
#define DSK_SECT            32                      /* sectors per track                            */
#define MAX_TRACKS          2048                    /* number of tracks                             */

#define MINI_DISK_SECT      16                      /* mini disk sectors per track                  */
#define MINI_DISK_TRACKS    35                      /* number of tracks on mini disk                */
#define MINI_DISK_SIZE      (MINI_DISK_TRACKS * MINI_DISK_SECT * DSK_SECTSIZE)
#define MINI_DISK_DELTA     4096                    /* threshold for detecting mini disks           */

#define FDCP15_DISK_SECT    1                       /* FDC+ 1.5MB Type 5 sectors per track          */
#define FDCP15_DISK_TRACKS  149                     /* number of tracks on 1.5MB disk               */
#define FDCP15_DISK_SECTSIZE  10240                 /* 1.5MB disk sector size                       */
#define FDCP15_DISK_SIZE    (FDCP15_DISK_TRACKS * FDCP15_DISK_SECT * FDCP15_DISK_SECTSIZE)
#define FDCP15_DISK_DELTA   4096                    /* threshold for detecting 1.5MB disks          */

#define ALTAIR_DISK_SIZE    337664                  /* size of regular Altair disks                 */
#define ALTAIR_DISK_DELTA   256                     /* threshold for detecting regular Altair disks */

#define MAX_SECT_SIZE       (FDCP15_DISK_SECTSIZE)
#define MAX_TRK_SIZE        (FDCP15_DISK_SECTSIZE)
#define MAX_DSK_SIZE        (DSK_SECTSIZE * DSK_SECT * MAX_TRACKS)

#endif

