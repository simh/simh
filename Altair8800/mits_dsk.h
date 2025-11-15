/* mits_dsk.h

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
   11/07/25 Initial version

*/

#ifndef _MITS_DSK_H
#define _MITS_DSK_H

#define UNIT_V_DSK_WLK      (UNIT_V_UF + 0)         /* write locked         */
#define UNIT_DSK_WLK        (1 << UNIT_V_DSK_WLK)

#define NUM_OF_DSK          4                       /* NUM_OF_DSK must be power of two              */
#define NUM_OF_DSK_MASK     (NUM_OF_DSK - 1)

#define DSK_SECTSIZE        137                     /* size of sector                               */
#define DSK_SECT            32                      /* sectors per track                            */
#define MAX_TRACKS          2048                    /* number of tracks,
                                                    original Altair has 77 tracks only              */
#define DSK_TRACSIZE        (DSK_SECTSIZE * DSK_SECT)
#define MAX_DSK_SIZE        (DSK_TRACSIZE * MAX_TRACKS)
#define BOOTROM_SIZE_DSK    256                     /* size of boot rom                             */

#define MINI_DISK_SECT      16                      /* mini disk sectors per track                  */
#define MINI_DISK_TRACKS    35                      /* number of tracks on mini disk                */
#define MINI_DISK_SIZE      (MINI_DISK_TRACKS * MINI_DISK_SECT * DSK_SECTSIZE)
#define MINI_DISK_DELTA     4096                    /* threshold for detecting mini disks           */

#define ALTAIR_DISK_SIZE    337664                  /* size of regular Altair disks                 */
#define ALTAIR_DISK_DELTA   256                     /* threshold for detecting regular Altair disks */

#endif

