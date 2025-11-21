/* altair8800_dsk.h: Soft Sector Disk Library

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
   13-Nov-2025 Initial version

*/

#ifndef _ALTAIR8800_DSK_H
#define _ALTAIR8800_DSK_H

#include "sim_defs.h"

#define DSK_MAX_TRACKS  80
#define DSK_MAX_HEADS   2

#define DSK_DENSITY_SD  0x01
#define DSK_DENSITY_DD  0x02

typedef struct {
    int32 density;
    int32 sectors;
    int32 sectorsize;
    int32 startsector;
    int32 offset;
} DSK_TRACK;

typedef struct {
    int32 tracks;
    int32 heads;
    int32 interleaved;
    DSK_TRACK track[DSK_MAX_TRACKS][DSK_MAX_HEADS];
} DSK_FORMAT;

typedef struct {
    UNIT *unit;
    DSK_FORMAT fmt;
    uint32 dbg_verbose;
} DSK_INFO;

extern t_stat dsk_init(DSK_INFO *d, UNIT *unit, int32 tracks, int32 heads, int32 interleaved);
extern t_stat dsk_init_format(DSK_INFO *d, int32 strack, int32 etrack, int32 shead, int32 ehead,
            int32 den, int32 secs, int32 secsize, int32 stsec);
extern void dsk_set_verbose_flag(DSK_INFO *d, uint32 flag);
extern int32 dsk_size(DSK_INFO *d);
extern int32 dsk_tracks(DSK_INFO *d);
extern int32 dsk_track_size(DSK_INFO *d, int32 track, int32 head);
extern int32 dsk_sectors(DSK_INFO *d, int32 track, int32 head);
extern int32 dsk_sector_size(DSK_INFO *d, int32 track, int32 head);
extern int32 dsk_sector_offset(DSK_INFO *d, int32 track, int32 head, int32 sector);
extern int32 dsk_start_sector(DSK_INFO *d, int32 track, int32 head);
extern t_stat dsk_validate(DSK_INFO *d, int track, int head, int sector);
extern t_stat dsk_write_sector(DSK_INFO *d, int32 track, int32 head, int32 sector, const uint8 *buf, int32 *byteswritten);
extern t_stat dsk_read_sector(DSK_INFO *d, int32 track, int32 head, int32 sector, uint8 *buf, int32 *bytesread);
extern t_stat dsk_read_track(DSK_INFO *d, int32 track, int32 head, uint8 *buf);
extern t_stat dsk_write_track(DSK_INFO *d, int32 track, int32 head, uint8 fill);
extern t_stat dsk_format(DSK_INFO *d, uint8 fill);
extern void dsk_dump_buf(const uint8 *b, int32 size);
extern void dsk_show(DSK_INFO *d);
extern t_stat dsk_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);

#endif

